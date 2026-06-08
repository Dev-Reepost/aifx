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

Prebuilt binaries are published on the
[GitHub Releases](https://github.com/Dev-Reepost/aifx/releases) page,
organized per operating system.

| Platform | Asset | Built? |
|---|---|---|
| **macOS (universal: arm64 + x86_64)** | `aifx-<version>-macos-installer.dmg` (wizard, recommended) **or** `aifx-<version>-macos-universal.tar.gz` (manual) | ✅ Available from v0.1.0 |
| **Windows (x86_64)** | `aifx-<version>-windows-x86_64.zip` | ✅ Available from v0.1.0 |
| **Linux (x86_64)** | `aifx-<version>-linux-x86_64.tar.gz` | ✅ Available from v0.1.0 (glibc 2.34+) |

### macOS (recommended: the installer)

1. Download `aifx-<version>-macos-installer.dmg` from the latest release and
   double-click it.
2. Drag **AIFX Installer.app** to wherever you like (or run it directly from
   the mounted DMG) and launch it.
3. The wizard walks you through:
   - Install location: per-user `~/Library/OFX/Plugins/` (default — no
     password) or system-wide `/Library/OFX/Plugins/`.
   - **Site configuration**: ComfyUI server URL + port, this Mac's view of
     the shared folder (typically `/Volumes/<share>/<root>`), and the
     **ComfyUI server's view** of the same shared folder (typically a
     Windows UNC path like `\\<server-host>\<share>\<root>`). These values
     are baked into each plugin's `defaults.json` before the bundle is
     copied into your OFX directory.
   - **Optional skip**: tick "Keep each plugin's bundled defaults" if you'd
     rather configure everything in your host UI on first use.
4. Click Install. The wizard copies the seven bundles, writes the site
   config into each, and clears macOS quarantine.
5. Restart your OFX host. The plugins appear under the **AIFX** category.

The installer is unsigned in v0.1.0. On first launch macOS will ask you to
confirm — right-click → **Open**, or run
`xattr -dr com.apple.quarantine '/path/to/AIFX Installer.app'`. A signed +
notarised installer is planned for a follow-up patch release.

### macOS (manual: the tarball)

If you'd rather wire everything by hand (or you're scripting deployment),
download `aifx-<version>-macos-universal.tar.gz` instead and follow the
same steps as the Windows / Linux paths below: extract, move the seven
`.ofx.bundle` directories into your OFX plugin directory, clear the
quarantine bit with `xattr -dr com.apple.quarantine
~/Library/OFX/Plugins/*.ofx.bundle`, restart your host, and override the
bundled `defaults.json` per the [Configuration & defaults](configuration.md)
guide.

### Windows

1. Download `aifx-<version>-windows-x86_64.zip` from the latest release and
   extract it. You will get a directory containing seven `.ofx.bundle`
   directories.
2. Copy all seven `.ofx.bundle` directories into one of the standard OFX
   plugin paths from the table above (`%LOCALAPPDATA%\OFX\Plugins\` is the
   safest choice — per-user, no admin rights needed).
3. Ensure the **Microsoft Visual C++ Redistributable (x64)** is installed on
   the machine — the plugins link against the standard MSVC runtime. Most
   systems already have it; otherwise install it from Microsoft.
4. Restart your OFX host. The plugins appear under the **AIFX** category.
5. **Configure for your network** — see step 6 under macOS above and
   [Configuration & defaults](configuration.md).

### Linux

1. Download `aifx-<version>-linux-x86_64.tar.gz` from the latest release and
   extract it. You will get a directory containing seven `.ofx.bundle`
   directories.
2. Copy all seven `.ofx.bundle` directories into one of the standard OFX
   plugin paths from the table above (`~/OFX/Plugins/` is the safest choice —
   per-user, no `sudo`).
3. The binaries require **glibc 2.34 or later** (Ubuntu 22.04+, Debian 12+,
   RHEL/Rocky 9+). On older hosts (Ubuntu 20.04, RHEL/Rocky 8, CentOS 7) they
   will not load — [build from source](#building-from-source) instead.
4. Restart your OFX host. The plugins appear under the **AIFX** category.
5. **Configure for your network** — see step 6 under macOS above and
   [Configuration & defaults](configuration.md).

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
