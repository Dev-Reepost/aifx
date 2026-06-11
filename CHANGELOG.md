# Changelog

All notable changes to AIFX will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

## [0.1.4] - 2026-06-11

Linux symbol-export hygiene, plus a crash-backtrace handler to chase the Flame
instancing segfault. NOTE: the export fix below eliminates a real latent bug but
did **not** resolve the Flame-on-Linux instancing crash — that remains open and
is now under investigation via the new backtrace handler.

### Fixed

- **Plugins no longer leak their static dependencies' symbols.** Every
  dependency (zlib via miniz/tinyexr, OpenSSL, httplib, ixwebsocket, the OFX
  C++ Support library) links statically into each `.ofx`, and those archives
  are built with default ELF visibility — so the bundle re-exported ~3700 of
  their symbols (`crc32`, `adler32`, `deflate`/`inflate`, `EXRLayers`, the whole
  OpenSSL surface, …). Flame links its *own* copies of zlib/OpenSSL/OpenEXR;
  when it `dlopen`ed the plugin, ELF's global symbol table bound Flame's
  internal calls to the plugin's copies (symbol interposition), and the moment
  the host touched its image/codec subsystem to lay out the new node it ran
  against a mismatched library build and crashed with `SIGSEGV` at `0x0`. macOS
  dyld's two-level namespace is immune, which is why Resolve/macOS never hit it.
  The build now links each `.ofx` with a version script
  (`plugins/ofx_exports.version`) that exports only the OFX C entry points
  (`OfxGetNumberOfPlugins`, `OfxGetPlugin`, `OfxSetHost`) and localizes
  everything else, plus `-Bsymbolic` so the plugin's own references bind to
  itself. Exported-symbol count drops from ~3700 to 2, closing the
  interposition vector. Linux-only link change; macOS/Windows binaries are
  unaffected. (This closes the interposition hole but, per the note above, is
  not the cause of the Flame instancing crash.)

### Added

- **Crash-backtrace handler (diagnostic).** The plugin installs an
  async-signal-safe `SIGSEGV`/`SIGABRT`/`SIGBUS`/`SIGFPE`/`SIGILL` handler that
  writes a symbolized backtrace to `~/comfyui_crash_<date>.log` and then chains
  to the host's previous handler. The Flame instancing segfault happens in the
  unlogged window between the base constructor finishing and the first logged
  instance method, so spdlog never captured it; this handler records the
  faulting stack so the culprit can be identified. The `.ofx` ships unstripped
  so `module+offset` frames resolve to function names with `addr2line`.

## [0.1.3] - 2026-06-10

Windows resource- and log-loading fixes. The plugins built and loaded on
Windows but could not find the user's home directory, their bundled
`defaults.json`, or their workflow templates; this release makes all three work
and standardises the bundle layout on the OFX spec across every platform.

### Fixed

- **Plugin log is now written on Windows.** `initializeLogger()` resolved the
  log directory from `getenv("HOME")`, which Windows does not set, so logging
  silently disabled itself (the only trace was a `std::cerr` line the host
  discards). Home resolution now falls back to `USERPROFILE` (then
  `HOMEDRIVE`+`HOMEPATH`), so the daily `comfyui_plugin_YYYYMMDD.log` lands in
  `%USERPROFILE%`.
- **Bundled `defaults.json` now loads on Windows/Linux.** The runtime config
  search looked under `Contents/Resources/config/`, but the Windows and Linux
  builds packaged resources at the bundle's top-level `Resources/`, so the
  search never matched and the parameter panel fell back to hard-coded defaults
  instead of the merged studio `defaults.json`. Resources are now packaged
  under `Contents/Resources/` per the OFX spec on all platforms (matching
  macOS).
- **Bundled workflow templates now load on Windows/Linux.**
  `getBundleResourcePath()` resolved workflow files from the old top-level
  `Resources/`; it now uses `Contents/Resources/` on every platform, consistent
  with the packaging change above.
- **Absolute Windows workflow paths are recognised.** `resolveWorkflowPath()`
  treated only POSIX `/…` paths as absolute; drive-letter (`C:\…`, `C:/…`) and
  UNC (`\\…`) paths are now handled, so a user-supplied absolute workflow path
  on Windows is used directly instead of being mis-resolved as bundle-relative.

### Changed

- Config-defaults lookup is centralised in a single cross-platform
  `getOfxConfigSearchPaths()` helper that also honours `OFX_PLUGIN_PATH` and the
  Windows system OFX directory. The dead `BasePlugin::loadConfigDefaults()` and
  its vestigial `AnyComfy` fallback paths were removed.

## [0.1.2] - 2026-06-10

Stability fix for OFX hosts that don't zero plugin instance memory.

### Fixed

- **Flame no longer segfaults when instancing a plugin.** Every clip/param
  pointer in `BasePlugin` is now zero-initialized (`= nullptr`). The
  constructor's environment-discovery dump calls `shouldFlipYForOFX()` — which
  reads `_flipYMode` — *before* the parameter-fetch block assigns those members.
  With an indeterminate pointer, `if (_flipYMode)` passed and the following
  `->getValue()` dereferenced garbage, an uncatchable SIGSEGV. macOS hosts
  happened to hand out zeroed memory and survived; Flame on Linux got non-zero
  garbage and crashed on instancing.

## [0.1.1] - 2026-06-10

Build-portability and host-visibility release. Plugin behaviour is unchanged;
the build and packaging tooling changed so the shipped `.ofx` bundles are
self-contained, use OFX-spec architecture directories, and load on a clean host
without the build machine's Conan cache.

### Added

- macOS installer (`aifx-<version>-macos-installer.dmg`): SwiftUI wizard
  that asks for the ComfyUI server URL + port and the two shared-folder
  paths (this Mac's view, and the ComfyUI server's view), bakes them into
  each plugin's `defaults.json`, copies the seven `.ofx.bundle` directories
  into the chosen OFX directory, and clears macOS quarantine. Source at
  `installer/macos/`, build pipeline at `tools/release-macos-installer.sh`.
  Unsigned for v0.1.x — signing + notarisation lands once a Developer ID
  certificate is on the build machine.

### Fixed

- **Windows/Linux bundles are now visible to OFX hosts.** Plugin binaries were
  packaged into non-spec architecture subdirectories (`Contents/Win64-AMD64`
  and `Contents/Linux-x86_64`, derived from `CMAKE_SYSTEM_PROCESSOR`). The OFX
  bundle spec requires exactly `Contents/Win64` and `Contents/Linux-x86-64`, so
  hosts (e.g. DaVinci Resolve) found the `.ofx.bundle` but no loadable binary
  inside and silently skipped the plugin. macOS (`Contents/MacOS`) was already
  correct. All seven plugins, the Windows release verifier, and the manual
  bundle fallback now emit the spec names.
- **Windows resource loading implemented.** `getBundleResourcePath()` was an
  unimplemented stub on Windows (`#else` returned `""`), so even a visible
  Windows plugin could not locate its bundled workflow template or
  `defaults.json`. It now resolves resources from the module path, with a
  `GetModuleHandleEx`-based fallback for hosts that don't populate
  `kOfxPluginPropFilePath` (mirrors the macOS `dladdr` path).
- Build `ixwebsocket` from source on Windows when no local Conan binary is
  cached. Conan Center's prebuilt is keyed on `compiler.version=194` (MSVC
  19.40–19.49) but can be built with a newer toolset than the build host has;
  its C++ runtime then references STL symbols the host's import libraries don't
  export, breaking plugin linking. Building it against the locally installed
  MSVC avoids the mismatch.
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

[Unreleased]: https://github.com/Dev-Reepost/aifx/compare/v0.1.4...main
[0.1.4]: https://github.com/Dev-Reepost/aifx/compare/v0.1.3...v0.1.4
[0.1.3]: https://github.com/Dev-Reepost/aifx/compare/v0.1.2...v0.1.3
[0.1.2]: https://github.com/Dev-Reepost/aifx/compare/v0.1.1...v0.1.2
[0.1.1]: https://github.com/Dev-Reepost/aifx/compare/v0.1.0...v0.1.1
[0.1.0]: https://github.com/Dev-Reepost/aifx/releases/tag/v0.1.0
