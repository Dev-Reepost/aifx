# Changelog

All notable changes to AIFX will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added

- macOS installer (`aifx-<version>-macos-installer.dmg`): SwiftUI wizard
  that asks for the ComfyUI server URL + port and the two shared-folder
  paths (this Mac's view, and the ComfyUI server's view), bakes them into
  each plugin's `defaults.json`, copies the seven `.ofx.bundle` directories
  into the chosen OFX directory, and clears macOS quarantine. Source at
  `installer/macos/`, build pipeline at `tools/release-macos-installer.sh`.
  Unsigned for v0.1.x — signing + notarisation lands once a Developer ID
  certificate is on the build machine.

## [0.1.1] - 2026-06-10

Build-portability release. Plugin behaviour is unchanged; only the build and
packaging tooling changed so the shipped `.ofx` bundles are self-contained and
load on a clean host without the build machine's Conan cache.

### Fixed

- Make plugin bundles self-contained via static linkage. Every third-party
  Conan dependency is now statically linked into each `.ofx` (`-o '*:shared=False'`
  forced on the Conan CLI across all build/release scripts, the highest-precedence
  layer, so an inherited developer profile carrying `*:shared=True` can no longer
  reintroduce non-portable `DT_RUNPATH`/`LC_RPATH` entries pointing back into the
  Conan cache). `expat` stays shared (OpenFX HostSupport only, never in a bundle).
- Pin RPATH to `$ORIGIN` (Linux) / `@loader_path` (macOS) and suppress CMake's
  auto-derived Conan-cache link-path RPATH, so no absolute build-host paths are
  baked into the binaries.
- Extend `build-plugin.sh`'s `verify_binary_portability()` to also reject
  non-portable absolute `LC_RPATH` entries on macOS.

  Verified on Linux x86_64 (Rocky 9): bundles depend only on base system
  libraries and require at most `GLIBC_2.34` / `GLIBCXX_3.4.29` / `CXXABI_1.3.13`,
  matching the RHEL 9.x (Rocky 9.x) base toolchain.

## [0.1.0] - 2026-06-04

First public pre-release. Windows x86_64, macOS universal (arm64 + x86_64), and
Linux x86_64 (glibc 2.34+) bundles published on GitHub Releases.

### Added

- Initial public release of seven OpenFX plugins:
  - `depth_da3` — Depth Anything V3 monocular depth estimation.
  - `normal_crafter` — NormalCrafter temporally consistent surface normal maps.
  - `depth_crafter` — DepthCrafter temporally consistent video depth.
  - `segmentation_sam3` — SAM3 text/click-prompted mask propagation.
  - `matte_mama` — VideoMaMa diffusion-based video alpha matting.
  - `matte_ma2` — MatAnyone2 fast recurrent video alpha matting.
  - `upscale_seedvr2` — SeedVR2 generative video super-resolution.
- Compile-time `isSequencePlugin()` dispatch separating per-frame from
  sequence-mode plugins.
- Shared infrastructure: REST + WebSocket ComfyUI client, async job manager,
  EXR I/O via TinyEXR, frame-level cache.
- Host-agnostic install across macOS, Linux, and Windows via standard OFX
  plugin directories.
- Comprehensive user documentation and GitHub Pages site.

[Unreleased]: https://github.com/Dev-Reepost/aifx/compare/v0.1.1...main
[0.1.1]: https://github.com/Dev-Reepost/aifx/compare/v0.1.0...v0.1.1
[0.1.0]: https://github.com/Dev-Reepost/aifx/releases/tag/v0.1.0
