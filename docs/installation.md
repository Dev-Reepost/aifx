---
title: Installation
nav_order: 2
---

# Installation

AIFX ships as a set of OpenFX plugin bundles (`.ofx.bundle` directories).
Any application implementing the [OpenFX](https://openeffects.org/) 1.4
standard or later will discover and load them from the standard plugin
directory for your operating system.

This page covers installing the plugins. The companion document
[ComfyUI server setup](comfyui-server-setup.md) covers the model server side,
which is required for the plugins to do useful work.

## Standard OpenFX plugin directories

| OS | System-wide | Per-user |
|---|---|---|
| **macOS** | `/Library/OFX/Plugins` | `~/Library/OFX/Plugins` |
| **Linux** | `/usr/OFX/Plugins` | `~/OFX/Plugins` |
| **Windows** | `%COMMONPROGRAMFILES%\OFX\Plugins` | `%LOCALAPPDATA%\OFX\Plugins` |

The host application reads from these directories at startup. Restart the host
after installing or updating plugins.

## Installing prebuilt bundles

> Prebuilt binaries are not yet published. Until V1.0.0 is tagged, install
> from source — see below.

When prebuilt bundles are available, the install procedure is:

1. Download the archive for your OS from the
   [GitHub Releases](https://github.com/Dev-Reepost/aifx/releases) page.
2. Extract the `*.ofx.bundle` directories.
3. Move each `.ofx.bundle` into one of the plugin directories from the table
   above. Use the system-wide directory if all users on the machine should see
   the plugins, or the per-user directory otherwise.
4. Restart your host application.

## Building from source

### Prerequisites

- **CMake 3.28** or later
- **Conan 2.1** or later
- A C++17 compiler:
  - macOS: Xcode command line tools (Apple Clang)
  - Linux: GCC 10+ or Clang 12+
  - Windows: Visual Studio 2022 with the C++ workload
- **Git**

### Build

```bash
git clone https://github.com/Dev-Reepost/aifx.git
cd AIFX

# Build and install all plugins to the per-user OFX directory:
./tools/build-plugin.sh plugins/depth_da3 --install
./tools/build-plugin.sh plugins/normal_crafter --install
./tools/build-plugin.sh plugins/depth_crafter --install
./tools/build-plugin.sh plugins/segmentation_sam3 --install
./tools/build-plugin.sh plugins/matte_mama --install
./tools/build-plugin.sh plugins/matte_ma2 --install
./tools/build-plugin.sh plugins/upscale_seedvr2 --install
```

The `--install` flag copies the resulting `.ofx.bundle` into your per-user OFX
plugin directory. Without it, the bundle is left under `build/Release/`.

For a system-wide install, run with elevated privileges and pass an explicit
install path:

```bash
sudo ./tools/build-plugin.sh plugins/depth_da3 --install \
  --install-dir /Library/OFX/Plugins
```

### Build options

| Option | Effect |
|---|---|
| `-d`, `--debug` | Debug build (slower, larger, with symbols). |
| `-c`, `--clean` | Wipe `build/` first for a clean rebuild. |
| `-v`, `--verbose` | Verbose CMake / build output. |
| `--bundle-name "Name"` | Override the bundle directory name. |
| `--install` | Copy the built bundle to the OFX plugin directory. |
| `--install-dir <path>` | Override the install destination. |

### Universal binaries on macOS

The build defaults to a universal binary (arm64 + x86_64). To build for the
host architecture only, pass `-DCMAKE_OSX_ARCHITECTURES=arm64` (or `x86_64`)
to CMake.

## Verifying the install

1. Restart your OFX host.
2. Open the effect / filter browser.
3. Look under the **AIFX** category.
4. Apply one of the plugins to a clip and check that the parameters panel
   appears.

If the plugins do not appear:

- Confirm the `.ofx.bundle` directories are inside one of the standard
  plugin paths from the table above (not nested in a subdirectory).
- Confirm the bundle structure is intact: each `.ofx.bundle` must contain a
  `Contents/<arch>/` directory with the `.ofx` shared library.
- Check the host's OFX plugin loading log if it has one.
- See [Troubleshooting](troubleshooting.md).

The plugin will appear in the host even when no ComfyUI server is running. The
plugin only fails when you actually invoke a render. Set up the server side
next: [ComfyUI server setup](comfyui-server-setup.md).
