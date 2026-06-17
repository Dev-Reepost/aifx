# Changelog

All notable changes to AIFX will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

## [0.1.12] - 2026-06-17

A load-time regression from the 0.1.8 static-runtime fix: the plugins stopped
being detected by Flame on Linux, failing with
``lib64/libc.so.6: version `GLIBC_2.35' not found``. Ships Linux and Windows
binaries; this is a Linux-only build fix, so the Windows build (rebuilt from the
0.1.12 source) is unchanged in behaviour from 0.1.11. macOS is unchanged from
0.1.11.

### Fixed

- **Plugins load again on glibc 2.34 hosts (Rocky/RHEL 9).** The 0.1.8 Resolve
  crash fix linked *both* libstdc++ and libgcc statically
  (`-static-libstdc++ -static-libgcc`). The libstdc++ half is what actually fixes
  Resolve (it stops the host's re-exported `std::filesystem::path::_M_split_cmpts`
  from interposing ours). The libgcc half, built on a modern toolchain, baked the
  unwinder's fast EH-frame lookup `_dl_find_object@GLIBC_2.35` into the `.ofx` as a
  **GLOBAL undefined reference**. Rocky Linux 9 ships glibc 2.34, which has no
  `GLIBC_2.35` version node, so the dynamic loader rejected the plugin and Flame
  never registered it. We now drop `-static-libgcc` and keep `-static-libstdc++`:
  libgcc is linked dynamically so the unwinder comes from the host's
  `libgcc_s.so.1` (ABI-stable, interposition-safe), the glibc floor drops back to
  2.34, and the Resolve fix is unchanged (`_M_split_cmpts` is still a LOCAL,
  non-preemptible symbol). Verified: the `.ofx` no longer references any
  `GLIBC_2.35` symbol.

## [0.1.11] - 2026-06-16

A "File exists already" failure surfaced by a real SeedVR2 upscale on DaVinci
Resolve (Linux): a multi-minute sequence job that completed the upscale but
crashed at save. Root-caused to duplicate in-flight submissions, plus a cache
toggle that never did anything. Ships Linux, Windows, and macOS binaries; the
fix is in shared code, so the Windows and macOS builds (rebuilt from the 0.1.11
source) carry the same fix.

### Fixed

- **No more "File exists already" aborts from the HQ-Image-Save `SaveEXR` node.**
  That node refuses to overwrite, so a second submission for the same output
  frame is fatal once the first job has written it. Two holes let duplicates
  through, both now closed:
  - **In-flight de-duplication.** The output cache check only sees files already
    written, never a job still running — and a sequence upscale can run for many
    minutes. A second **Collect & Submit** during that window queued a duplicate
    that died at save when the first job wrote its frames. The submit path now
    ignores a press while a job for the same output prefix is `QUEUED`/`PROCESSING`
    ("Already processing this shot — duplicate submission ignored").
  - **Button no longer cancels-and-forgets.** Pressing Collect & Submit tripped
    the generic parameter-invalidation branch, which marked the job cancelled
    **locally only** — the ComfyUI server kept running it, wrote its output, and
    blocked the resubmit. The button is now excluded from that branch, and a
    genuine output-affecting parameter change **interrupts the server**
    (`cancelAllJobs(interruptServer=true)`) instead of a local-only cancel, so an
    invalidated job stops before it can write.

### Changed

- **The "Enable Cache" toggle now actually works.** It was defined and fetched
  but its value was never read, so the plugin always behaved cache-on (reuse an
  existing output) and the cache-off path did not exist. Now: **cache on** serves
  an existing output and skips submission; **cache off** deletes any stale output
  first (single-frame and sequence paths alike) so `SaveEXR` writes into a clean
  slot instead of aborting. Wired into all three submission paths
  (`renderAsync`, `executeWorkflow`, and the sequence Collect & Submit).

## [0.1.10] - 2026-06-16

A teardown-time crash fix found while auditing the job lifecycle after 0.1.9.
Linux binaries only.

### Fixed

- **No use-after-free when a plugin is torn down mid-submission.** The per-frame
  async path (`submitJobAsync`, used by per-frame plugins such as DepthAnything3)
  launched a **detached** worker thread that captures the job manager, the
  ComfyUI `Client`, and the plugin, then calls back into all three to build and
  submit the workflow. The destructor joined the monitor and sequence-write
  threads but could not join this detached worker, so deleting the node /
  closing the project / switching clips while a submission was in flight could
  leave the worker dereferencing freed objects. `~AsyncJobManager` now tracks
  in-flight workers with a counter + condition variable and **drains** them
  before its members are destroyed, and workers re-check the shutdown flag and
  bail early (notify is done under the lock to avoid a condition-variable
  teardown race). This is the lifecycle sibling of the client-recreation
  use-after-free fixed in 0.1.9.

## [0.1.9] - 2026-06-16

Follow-up to 0.1.8: with the Resolve crash gone, the next real Resolve Studio
job reached submission and surfaced a use-after-free in the ComfyUI client.
Linux binaries only.

### Fixed

- **ComfyUI submission no longer fails after editing the Server params.**
  `AsyncJobManager` holds a non-owning `Client*` captured once at construction,
  but changing **Server Address** or **Port** ran `_comfyClient.reset(new
  Client(...))`, destroying the very object the job manager (and its background
  submission/polling thread) still pointed at. At Collect & Submit the freed
  client was dereferenced — surfacing as a submit failure with an empty host and
  garbage port (`Failed to connect to ComfyUI server at :-508401456`) and
  `std::bad_alloc`. The server address is now updated **in place**
  (`Client::setServerAddress()`), keeping the pointer stable; the client's
  `hostname`/`port` are mutex-guarded and read as an atomic snapshot per request,
  so an in-place update can't tear-read against an in-flight background request.

### Changed

- **Shipped config and code/test examples no longer contain the studio's real
  internal network details.** The seed `defaults.json`, the hardcoded mount
  fallbacks, and a few comments/tests carried the real ComfyUI server IP, SMB
  share, and mount paths. They are now the generic placeholders already used in
  `docs/configuration.md` (`comfyui.example.local:8188`, `\\HOSTNAME\share`,
  `/Volumes/comfyui-share`, `/mnt/comfyui-share`). No behaviour change — these
  are only seed defaults users override in the panel.

## [0.1.8] - 2026-06-16

A crash fix for DaVinci Resolve on Linux, surfaced by a real Resolve Studio job
at "Collect Process". Ships Linux and Windows binaries; the runtime fix is
Linux-only, so the Windows build (rebuilt from the 0.1.8 source) is unchanged in
behaviour from 0.1.7. macOS is unchanged from 0.1.7.

### Fixed

- **No more SIGSEGV in DaVinci Resolve on Linux at Collect & Submit.** The
  plugins linked `libstdc++` dynamically, leaving `std::filesystem` symbols as
  undefined, preemptible imports in each `.ofx`. Resolve loads its
  `libProResRAW.so` — which statically baked in an *older* libstdc++ and
  re-exports its C++ symbols globally — into the process before the plugin. The
  dynamic linker then bound the plugin's `std::filesystem::path` calls to that
  stale copy, whose internal layout differs, and the first real filesystem call
  (`std::filesystem::exists()` in the frame-collection loop) dereferenced
  garbage → crash (`signal 11`, `fault_addr=0x2b`). The plugins now link the C++
  runtime statically (`-static-libstdc++ -static-libgcc`); combined with the
  existing version-script (`local: *`) + `-Bsymbolic` hardening, `std::filesystem`
  is now a local, non-preemptible symbol and the C++ runtime is fully
  self-contained — immune to whatever libstdc++ the host drags into the global
  scope. This is the import-side counterpart to the export-side interposition fix
  in 0.1.5 (which stopped Flame binding to the plugin's symbols); the two hosts
  crashed in opposite directions for the same underlying ELF-scope reason.

### Changed

- **Linux source builds now require the static C++ runtime (`libstdc++.a`).** It
  ships with the g++ dev package on Debian/Ubuntu (`build-essential`) but is a
  separate CRB-repo package on Rocky/RHEL (`libstdc++-static`).
  `tools/setup-env.sh` now probes for it and offers a distro-aware install, and
  `tools/release-linux.sh` preflight-checks it with a clear install hint.

## [0.1.7] - 2026-06-12

Storage-mount UX simplification and a networked-output reliability fix, both
surfaced by real Flame-on-Linux jobs against the 0.1.6 build.

### Fixed

- **Jobs no longer fail with a false "output file not found" on networked
  storage.** When ComfyUI reported a frame complete, the plugin checked for the
  output EXR exactly once and failed immediately if it wasn't there. On an
  SMB/NFS share the file written by the ComfyUI server lags its completion
  report on the client's view, so the check raced and lost — the frame was
  marked failed even though the EXR materialised a moment later (a re-submit
  then found it cached and read it fine). The plugin now waits a bounded grace
  period (30 s, re-checking each poll) for the output to become visible before
  declaring failure.

### Changed

- **Storage mounts reduced from three per-OS fields to two.** The panel's
  **Storage Mounts** group now has **Local Storage Mount** (this host's view of
  the share, for the plugin's local EXR I/O) and **ComfyUI Server Mount** (the
  server's view, written into the workflow), replacing the macOS / Windows /
  Linux trio. A plugin bundle only runs on the OS it was built for, so a single
  "local" field suffices; per-OS defaults still live in config
  (`storage.localMountPath.{macos,windows,linux}`) and each platform build seeds
  its Local Storage Mount default from the matching entry. Local falls back to
  the server mount when blank (single-box setups). Internal parameter names
  changed (`macMountPath` / `winMountPath` / `linuxMountPath` →
  `localMountPath` / `serverMountPath`), so existing saved projects re-default
  these two fields once.

## [0.1.6] - 2026-06-12

Path-management rationalization. With the Flame instancing crash fixed in 0.1.5,
real jobs surfaced a cluster of cross-platform path bugs on Linux and Windows.
This release reworks how the plugins resolve storage mounts, server paths,
config, and bundled workflows.

### Fixed

- **Per-OS mount paths now actually work.** The code only ever read
  `macMountPath`/`winMountPath` and **ignored `linuxMountPath` entirely**, and it
  forced the macOS field as the local mount on every platform. So on a Linux host
  the plugin tried to read/write a macOS or Windows path (`/Volumes/…`, `S:\…`)
  and failed (`Failed to create directory`, `output file missing`); on a Windows
  host it aborted because the macOS field was empty. The plugin now auto-selects
  the mount for the OS it is running on (`getLocalMountPath()` — macOS→mac,
  Windows→win, Linux→linux) for all local EXR I/O.
- **Empty server mount no longer produces broken paths.** With the Windows mount
  blank, `convertPathForComfyUI()` used to strip the local mount and prepend
  nothing, emitting rootless `\in\…` / `\out\…` paths that crashed the job. It now
  returns the path unchanged (with a warning) when no server mount is set, and
  only rewrites when the path genuinely starts with the local mount.
- **Every plugin built its output path from the macOS mount directly**, bypassing
  the per-OS selection — so output paths were wrong on Linux/Windows even when the
  input path was right. All seven plugins now use `getLocalMountPath()`.
- **`create_path_if_missing` is set on every SaveEXR node.** It was present in
  only three of the seven plugins; DepthCrafter / NormalCrafter / DepthDA3 /
  SeedVR2 failed when the output directory didn't already exist. Added to all
  seven (hardcoded workflows and bundled JSON).
- **Bundled `defaults.json` is now found on Linux.** The config search path
  omitted the standard Linux OFX directory `/usr/OFX/Plugins` (where Flame loads
  plugins), so on Linux config was never read and every config-seeded parameter
  silently fell back to its hard-coded default — including a stale, wrong
  `workflowFile` (`resources/workflows/sam_segmentation.json`) that no plugin
  could resolve. The search now covers the standard macOS, Windows, **and** Linux
  system directories on every host. The neutral `workflowFile` fallback is now
  empty (use the built-in workflow) instead of a bogus path.
- **Stale duplicate `Resources/` no longer ships.** Bundles carried a top-level
  `Resources/` left over from before the `Contents/Resources/` (OFX-spec) layout
  migration, holding out-of-date config/workflow copies. The release scripts
  (Linux/macOS/Windows) now strip it before packaging.

### Changed

- **Config restructured.** `defaults-base.json` splits the old catch-all
  `server` block into **`server`** (just the ComfyUI HTTP endpoint) and
  **`storage`** (the three per-OS mount paths). The OFX panel gains a dedicated
  **Storage Mounts** group, separate from the Server connection. Internal OFX
  parameter names are unchanged, so existing saved projects keep working.
- Removed four dead config keys (`macComfyUIInputDir`, `winComfyUIInputDir`,
  `linuxComfyUIInputDir`, `comfyUIInputDir`) — unreferenced in code and redundant
  with the mount paths. `docs/configuration.md` rewritten to match.

## [0.1.5] - 2026-06-11

**The Flame-on-Linux instancing crash is fixed.** A vtable-layout mismatch
crashed the host with `SIGSEGV` at `0x0` the instant a node was instanced — the
crash that survived the 0.1.2 and 0.1.4 attempts.

### Fixed

- **Flame no longer segfaults when a plugin is instanced (for real this time).**
  Root cause: `OFX_SUPPORTS_OPENGLRENDER` (upstream OpenFX default ON) gates
  extra *virtual* methods on `OFX::ImageEffect` (`contextAttached` /
  `contextDetached` and the OpenGL render-arg overloads), so it changes that
  class's vtable layout. Upstream applies the macro only within its own CMake
  directory scope, so the `OfxSupport` library was built **with** it while our
  plugin sources (a sibling directory) were built **without** it. The two then
  disagreed on `ImageEffect`'s vtable: `OfxSupport` dispatched host actions
  through the longer layout while the plugins used the shorter one. Flame is a
  GL/Vulkan host and fires the OpenGL context-attach action when a node is
  instanced; the support library routed it through the `contextAttached` vtable
  slot, which in the plugin's mismatched vtable held `BasePlugin::buildWorkflow`
  — called with the wrong arguments, an immediate null dereference. The build
  now compiles plugin code with the same `OFX_SUPPORTS_OPENGLRENDER` (and
  sibling render defines) that `OfxSupport` was built with, so both agree on the
  layout. Verified at the binary level across all seven plugins: the dispatched
  slot now resolves to `OFX::ImageEffect::contextDetached()` instead of
  `buildWorkflow`.
- Diagnosed with the crash-backtrace handler added in 0.1.4, which captured the
  faulting stack (`buildWorkflow` ← `OFX::Private::mainEntryStr` doing a vtable
  call) that spdlog could never reach.

## [0.1.4] - 2026-06-11

Linux symbol-export hygiene, plus a crash-backtrace handler to chase the Flame
instancing segfault. NOTE: the export fix below eliminates a real latent bug but
did **not** resolve the Flame-on-Linux instancing crash — that was root-caused
and fixed in 0.1.5 (a vtable-layout mismatch). The backtrace handler added here
is what captured the faulting stack.

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

[Unreleased]: https://github.com/Dev-Reepost/aifx/compare/v0.1.6...main
[0.1.6]: https://github.com/Dev-Reepost/aifx/compare/v0.1.5...v0.1.6
[0.1.5]: https://github.com/Dev-Reepost/aifx/compare/v0.1.4...v0.1.5
[0.1.4]: https://github.com/Dev-Reepost/aifx/compare/v0.1.3...v0.1.4
[0.1.3]: https://github.com/Dev-Reepost/aifx/compare/v0.1.2...v0.1.3
[0.1.2]: https://github.com/Dev-Reepost/aifx/compare/v0.1.1...v0.1.2
[0.1.1]: https://github.com/Dev-Reepost/aifx/compare/v0.1.0...v0.1.1
[0.1.0]: https://github.com/Dev-Reepost/aifx/releases/tag/v0.1.0
