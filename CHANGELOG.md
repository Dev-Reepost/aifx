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

[Unreleased]: https://github.com/Dev-Reepost/aifx/compare/v0.1.0...main
[0.1.0]: https://github.com/Dev-Reepost/aifx/releases/tag/v0.1.0
