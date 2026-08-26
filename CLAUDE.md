# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

**AIFX** is a suite of open-source **OpenFX (OFX) plugins** that bridge an OFX
host (compositor / editor / color grader) to a **ComfyUI** AI model server. The
plugins run inside the host; the AI models run in ComfyUI on a separate process
(often a separate, GPU-equipped machine). They exchange image data as EXR files
over a shared filesystem and orchestrate jobs over ComfyUI's HTTP/WebSocket API.
The host machine does not need a GPU.

This repository was forked from
[AcademySoftwareFoundation/openfx](https://github.com/AcademySoftwareFoundation/openfx)
but no longer vendors it. OpenFX is pulled at configure time via CMake
`FetchContent` (pinned `GIT_TAG` in [CMakeLists.txt](CMakeLists.txt)); only the
OpenFX C++ **Support** library is consumed. Do **not** look for `Support/`,
`HostSupport/`, `Examples/`, `include/`, `contrib/`, or `scripts/` here — they
belong to upstream and are not part of this repo.

AIFX is developed by [MaGMa](https://www.linkedin.com/company/ma-g-ma/) for
Reepost Studio, funded by the CNC. Plugin code is BSD-3-Clause; the AI model
weights ComfyUI loads carry their own (often non-commercial) upstream licenses.

### The seven plugins (V1 scope)

| Directory | CMake target | Model |
| --- | --- | --- |
| `plugins/depth_da3` | `DepthAnything3` | Depth Anything V3 — per-frame monocular depth |
| `plugins/normal_crafter` | `NormalCrafter` | Temporally coherent surface normals |
| `plugins/depth_crafter` | `DepthCrafter` | Temporally consistent video depth |
| `plugins/segmentation_sam3` | `SegmentationSAM3` | SAM3 text/click-prompted mask propagation |
| `plugins/matte_mama` | `MatteMaMa` | MaMa diffusion video alpha matting |
| `plugins/matte_ma2` | `MatteMA2` | MatAnyone2 fast recurrent matting |
| `plugins/upscale_seedvr2` | `UpscaleSeedVR2` | SeedVR2 diffusion super-resolution |

The directory→target mapping is the single source of truth in
[plugins/manifest.txt](plugins/manifest.txt). The CMake target name differs from
the directory name and must be passed explicitly to the build script — the build
cannot infer it.

## Build System

CMake 3.28+ with Conan 2.1+. OpenFX comes via FetchContent (not Conan); all
other C/C++ deps come via Conan. AIFX itself is a consumer only — it is not
published as a Conan package.

### One-time environment setup

The build needs a Conan `default` profile. `build-plugin.sh` runs
`conan install ... -pr:b=default` on every build and fails immediately without
it:

```bash
conan profile detect        # once per machine
# or, on a fresh machine (installs Conan, makes OFX dirs, detects profile):
./tools/setup-env.sh        # macOS / Linux
./tools/setup-env.ps1       # Windows (PowerShell)
```

### Building plugins

The primary workflow is **per-plugin** via `tools/build-plugin.sh`, passing the
plugin directory and its CMake target name:

```bash
# Build one plugin and install its bundle to the per-user OFX dir:
./tools/build-plugin.sh plugins/depth_da3 DepthAnything3 --install

# Build all seven (see docs/installation.md):
./tools/build-plugin.sh plugins/depth_da3 DepthAnything3 --install
./tools/build-plugin.sh plugins/normal_crafter NormalCrafter --install
./tools/build-plugin.sh plugins/depth_crafter DepthCrafter --install
./tools/build-plugin.sh plugins/segmentation_sam3 SegmentationSAM3 --install
./tools/build-plugin.sh plugins/matte_mama MatteMaMa --install
./tools/build-plugin.sh plugins/matte_ma2 MatteMA2 --install
./tools/build-plugin.sh plugins/upscale_seedvr2 UpscaleSeedVR2 --install
```

`build-plugin.sh` options: `-d/--debug`, `-c/--clean`, `-v/--verbose`,
`--bundle-name "Name"`, `--install`, `--install-dir <path>`. Without `--install`
the `.ofx.bundle` is left under `build/Release/`. macOS defaults to a universal
binary (arm64 + x86_64); pass `-DCMAKE_OSX_ARCHITECTURES=arm64` (or `x86_64`)
for a single arch.

### Key CMake options

- `BUILD_COMFYUI_PLUGINS=ON` — build the AIFX plugins (forced ON by the
  top-level CMakeLists; the sentinel the `plugins/` tree expects).
- OpenFX subcomponents (`BUILD_EXAMPLE_PLUGINS`, `OFX_SUPPORTS_OPENCLRENDER`,
  `OFX_SUPPORTS_CUDARENDER`) are forced OFF — AIFX only needs the Support lib.

### Upgrading OpenFX

Bump the `GIT_TAG` in the `FetchContent_Declare(openfx ...)` block in
[CMakeLists.txt](CMakeLists.txt). It is currently pinned to an upstream `main`
commit pending a tagged release with the modern Examples layout / `Info.plist.in`.

## Architecture

See [docs/architecture.md](docs/architecture.md) for the full picture. Summary:

```text
OFX host ──HTTP/WS──▶ ComfyUI server
   │                      │
   └──── shared FS (EXR in/out) ────┘
```

### Shared infrastructure: `ComfyUICommon`

[plugins/common/](plugins/common/) builds a static library `ComfyUICommon` that
every plugin links against. It contains:

- `comfyui_base_plugin.{h,cpp}` — the `ComfyUIBasePlugin` OFX `ImageEffect`
  base class: parameter creation, render dispatch, template substitution.
- `comfyui_client.{h,cpp}` — ComfyUI HTTP/WebSocket client (`/prompt`,
  `/history`).
- `async_job_manager.{h,cpp}` — background job queue and polling thread.
  Job states: `QUEUED → PROCESSING → COMPLETED | FAILED`.
- `comfyui_image_io.{h,cpp}` — EXR read/write via TinyEXR.

Third-party libs (via Conan): `nlohmann_json`, `cpp-httplib`, `ixwebsocket`,
`tinyexr`, `miniz`, `openssl`, `spdlog`. EXR channels must be supplied to
TinyEXR in **alphabetical order**.

### Per-plugin structure

Each `plugins/<name>/` directory contains:

- `<name>_plugin.{h,cpp}` — a class inheriting `ComfyUIBasePlugin`, implementing
  only the model-specific bits: parameter declaration, `buildWorkflow(...)`
  (produces the ComfyUI workflow JSON for one frame/sequence with template
  variables filled in), and `getRequiredModels()`.
- `resources/workflow/<name>.json` — the exported ComfyUI workflow template
  (with `LoadEXR` input and `SaveEXR` output nodes).
- `defaults-project.json` — this plugin's project-specific default parameters
  (and any per-plugin override).
- `CMakeLists.txt` — links against `ComfyUICommon`, packages the `.ofx.bundle`.

### Per-frame vs sequence dispatch

The choice is explicit at compile time via `isSequencePlugin()`:

- `return false` (default) — each requested frame is a separate ComfyUI job.
  **Depth Anything V3 is the only plugin in this group**: DA3 is a per-frame
  monocular model, so there is nothing to gain from batching. Its panel
  therefore shows an **Enable Processing** toggle and no **Collect & Process**
  button — that is by design, not a missing control.
- `return true` — one job per contiguous block of frames, for temporal
  consistency; block size governed by `getImageLoadCap()` →
  `LoadEXR.image_load_cap`. This is every other plugin: NormalCrafter,
  DepthCrafter, SAM3, MaMa, MatAnyone2, SeedVR2. Their panels show a
  **Collect & Process** button instead of the toggle.

### Caching

Output EXRs sit in the shared output folder. On re-render, an existing output
for the same frame + parameters is returned without contacting ComfyUI. A
parameter change invalidates the cache via the workflow hash in the output
filename. The plugin does not garbage-collect old outputs.

### `defaults.json` build-time merge

Each bundle ships `Contents/Resources/config/defaults.json` seeding the OFX
parameter panel. Studio-wide settings (ComfyUI server URL, controls) live in a
single source of truth at [config/defaults-base.json](config/defaults-base.json);
each plugin owns only its project-specific block in its `defaults-project.json`.
The CMake function `aifx_merge_defaults()` (in
[plugins/CMakeLists.txt](plugins/CMakeLists.txt)) runs
[tools/merge-defaults.py](tools/merge-defaults.py) as a POST_BUILD step to
produce one complete `defaults.json` — no run-time file lookups.

## Adding a new plugin

1. Pick a ComfyUI custom node wrapping the model; author a working ComfyUI
   workflow (`LoadEXR` → model → `SaveEXR`).
2. Create `plugins/<name>/` with `<name>_plugin.{h,cpp}` (inherit
   `ComfyUIBasePlugin`), `resources/workflow/<name>.json`,
   `defaults-project.json`, and a `CMakeLists.txt` linking `ComfyUICommon`.
3. Implement `buildWorkflow()`, `getRequiredModels()`, and override
   `isSequencePlugin()` (and `getImageLoadCap()` if a sequence plugin).
4. Add the directory + CMake target to [plugins/manifest.txt](plugins/manifest.txt)
   and `add_subdirectory(<name>)` in [plugins/CMakeLists.txt](plugins/CMakeLists.txt).
   Every consumer (build, release scripts, docs) reads the manifest.

## Releases & installers

- `tools/release-{macos,linux}.sh`, `tools/release-windows.ps1` — build and
  package per-OS release artifacts (driven by the plugin manifest).
- `tools/release-macos-installer.sh` + [installer/macos/](installer/macos/) — a
  SwiftUI wizard installer (`.dmg`) that bakes site config (ComfyUI URL, client
  vs server mount paths) into each plugin's `defaults.json` before install.
- See [RELEASING.md](RELEASING.md) and [RELEASE_SPEC.md](RELEASE_SPEC.md).

## Plugin installation directories

| OS | System-wide | Per-user |
| --- | --- | --- |
| **macOS** | `/Library/OFX/Plugins` | `~/Library/OFX/Plugins` |
| **Linux** | `/usr/OFX/Plugins` | `~/OFX/Plugins` |
| **Windows** | `%COMMONPROGRAMFILES%\OFX\Plugins` | `%LOCALAPPDATA%\OFX\Plugins` |

The host reads these at startup — restart the host after install/update. Plugins
appear under the **AIFX** category.

## File Organization

- `plugins/` — the seven OFX plugins + `common/` (`ComfyUICommon`) + `tests/`
- `plugins/manifest.txt` — directory→target single source of truth
- `config/defaults-base.json` — studio-wide default parameters
- `tools/` — build, release, env-setup scripts; `merge-defaults.py`
- `installer/macos/` — SwiftUI installer app
- `docs/` — user + dev docs (also published as a GitHub Pages site)
- `CMakeLists.txt` / `conanfile.py` — top-level build + dependency config

## Documentation

- [docs/installation.md](docs/installation.md) — install + build from source
- [docs/comfyui-server-setup.md](docs/comfyui-server-setup.md) — the model server side
- [docs/architecture.md](docs/architecture.md) — how it all fits together
- [docs/configuration.md](docs/configuration.md), [docs/workflow-customization.md](docs/workflow-customization.md), [docs/troubleshooting.md](docs/troubleshooting.md)
- [docs/plugins/](docs/plugins/) — per-plugin reference, requirements, model licenses

## Conventions

- All source files carry an SPDX header: `SPDX-License-Identifier: BSD-3-Clause`.
- Prefer the OpenFX C++ Support library wrappers (via `ComfyUIBasePlugin`) over
  the raw C API.
- Bug reports / help: [GitHub Issues](https://github.com/Dev-Reepost/aifx/issues)
  or `contact@reepost.fr`.
