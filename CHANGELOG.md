# Changelog

All notable changes to AIFX will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added

- Initial public release of seven OpenFX plugins:
  - `depth_da3` — Depth Anything V3 monocular depth estimation.
  - `normal_crafter` — NormalCrafter surface normal map estimation.
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

[Unreleased]: https://github.com/Dev-Reepost/aifx/commits/main
