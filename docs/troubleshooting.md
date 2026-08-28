---
title: Troubleshooting
nav_order: 7
---

# Troubleshooting

A grouped reference of the failure modes most users hit. If you don't find
your issue here, see [Reporting bugs / getting help](#reporting-bugs--getting-help)
at the bottom of this page.

## The plugin does not appear in my host

- Confirm the `.ofx.bundle` is in one of the standard
  [OFX plugin directories](installation.md#standard-openfx-plugin-directories).
- Confirm the bundle is a directory, not a `.zip`. Some browsers leave the
  archive un-extracted.
- Restart the host. Most hosts only scan plugin directories at startup.
- Check that the bundle's `Contents/<arch>/` directory contains a `.ofx`
  shared library and that the host's CPU architecture matches.
- On macOS, if the bundle was downloaded from the internet, run
  `find /path/to/Plugin.ofx.bundle -exec xattr -d com.apple.quarantine {} \;`
  to clear the
  quarantine bit.
- Check the host's plugin loading log if it has one (most hosts do).

## The host crashes the moment I select or add the plugin
{: #host-crashes-on-select }

Almost always a **stale bundle** left behind by an earlier install, not a bug in
the version you just installed. Bundles built before 0.1.5 were compiled without
`OFX_SUPPORTS_OPENGLRENDER`, which leaves their vtable two slots shorter than
the OpenFX Support library they are linked against: the host dispatches an
action into the wrong virtual and segfaults immediately after the plugin's
constructor. In the plugin log the tell is a workflow being built when no render
started, with an absurd frame number:

```text
[info] BasePlugin constructor completed successfully
[info] Building Depth Anything V3 workflow for frame 1241540245
```

Run the bundled checker — it needs no build tree, so it works on the
workstation:

```bash
tools/verify-ofx-abi.sh          # scans this OS's OFX plugin directories
```

Any bundle it reports as `FAIL … SHORT vtable` must be deleted and reinstalled.
Note that installers do not remove bundles from a *different* install location:
check both the per-user and system-wide
[OFX plugin directories](installation.md#standard-openfx-plugin-directories).

To identify what is installed without the checker:

```bash
cat "<bundle>.ofx.bundle/Contents/Resources/aifx-build.txt"
```

Bundles from 0.2.1 onward carry that file plus a matching `AIFX-BUILD` stamp in
the binary. **If the file is absent, the bundle predates 0.2.1** and should be
replaced regardless.

## macOS blocks the installer: "Apple could not verify … is free of malware"
{: #macos-installer-blocked }

Gatekeeper on macOS 15 (Sequoia) and later refuses apps that are not notarised,
and the old right-click → **Open** bypass no longer works. If you have an
unsigned build of the installer, copy it out of the read-only disk image first,
then clear the quarantine flag:

```bash
cp -R "/Volumes/AIFX <version>/AIFX Installer.app" /Applications/
find "/Applications/AIFX Installer.app" -exec xattr -d com.apple.quarantine {} \; 2>/dev/null
open "/Applications/AIFX Installer.app"
```

Alternatively, double-click the app, dismiss the warning, then approve it under
**System Settings → Privacy & Security → Open Anyway**.

Signed and notarised releases do not need any of this.

## The plugins installed fine but Flame / Flare does not list them
{: #flame-cannot-see-plugins }

Autodesk Flame and Flare only scan the **system-wide** OFX directory,
`/Library/OFX/Plugins` on macOS and `/usr/OFX/Plugins` on Linux. They ignore the
per-user locations that Nuke, Resolve and Fusion read. If you installed
per-user, move the bundles:

```bash
sudo mkdir -p /Library/OFX/Plugins
sudo mv ~/Library/OFX/Plugins/*.ofx.bundle /Library/OFX/Plugins/
sudo chmod -R go+rX /Library/OFX/Plugins
```

Then restart the host. From v0.2.5 there is no per-user mode left in any of the
three installers — the machine-wide directory is the only location they offer,
and `--prefix` is the explicit escape hatch. The macOS wizard defaulted to
system-wide from v0.2.2 and `install.sh` from v0.2.4; anything installed by an
earlier version may still be sitting in the per-user path.

If you have already installed system-wide and Flame still does not see them,
the next section is the usual culprit: an `OFX_PLUGIN_PATH` left over in your
shell profile overrides the standard directories entirely.

## The host keeps loading plugins from `~/OFX/Plugins`, whatever I install
{: #ofx-plugin-path-override }

Symptom: you install system-wide, the installer reports success, and the host
still shows the old plugins — or none at all. Checking the disk shows the new
bundles sitting exactly where you put them.

The cause is almost always the **`OFX_PLUGIN_PATH`** environment variable. It is
a machine-wide switch that tells every OFX host where to look for plugins, and
while it is set the host reads that path *instead of* the standard locations. No
installer can work around it, and nothing in the host reports it.

AIFX's own `tools/setup-env.sh` used to export it into your shell profile —
`~/Library/OFX/Plugins` on macOS, `~/OFX/Plugins` on Linux — as a side effect of
setting up a build environment. Any workstation where that script was run before
v0.2.5 still carries the export. Check for it:

```bash
echo "${OFX_PLUGIN_PATH:-<unset>}"
grep -n OFX_PLUGIN_PATH ~/.zshrc ~/.bashrc ~/.bash_profile 2>/dev/null
```

If it is set and does not name the directory you installed into, remove the
`export OFX_PLUGIN_PATH=…` line from the shell profile it came from, open a new
terminal, and **relaunch the host from that terminal** (a host started from the
Dock or a desktop launcher inherits the environment from its own login session,
so log out and back in to be sure).

To keep the override instead, install straight into it:

```bash
./install.sh --prefix "$OFX_PLUGIN_PATH"
```

From v0.2.5 `install.sh` detects this case and warns when `OFX_PLUGIN_PATH` does
not cover the destination, and `setup-env.sh` no longer sets the variable at all.

## "Connection refused" or "Cannot reach ComfyUI server"

- Confirm ComfyUI is actually running: open `http://<server-ip>:8188/` in a
  browser.
- Confirm the server URL in the plugin parameters matches. Typo in IP, wrong
  port, or `https://` instead of `http://` are common.
- If the server is on another machine, confirm it's listening on the right
  interface: ComfyUI must be started with `--listen 0.0.0.0` (not the default
  `127.0.0.1`) to accept connections from outside the local machine.
- Check that no firewall is blocking the port. On Linux, `ufw status` /
  `iptables -L`. On macOS, System Settings → Network → Firewall. On Windows,
  Windows Defender Firewall.

## "Path not found" or "Failed to write EXR"

- Verify that **Client Mount Path** points to a directory that is mounted
  and writable from the host machine: `ls /<client-mount>/`.
- Verify that **Server Mount Path** points to the same physical location as
  seen from the ComfyUI server. Both sides must be able to read and write the
  same files.
- Verify both sides have permission to create subdirectories under the mount.
- **Write Server Mount Path in the ComfyUI server's own convention.** The
  plugin picks the separator style from that value: a UNC path
  (`\\HOST\share`) or drive letter (`Z:\share`) means a Windows server and
  the submitted paths use backslashes; anything else (`/mnt/share`) means a
  Linux/macOS server and they use forward slashes. A Linux ComfyUI treats `\`
  as an ordinary filename character, so a UNC-style value against a Linux
  server produces `Path not found: \mnt\share\in\…` for every frame. The
  plugin handles JSON escaping; you do **not** need to double-escape
  backslashes yourself.
- Don't leave a trailing `/` or `\` on either mount path.

## The render hangs forever

- Check the ComfyUI server console for errors. The plugin polls `/history` for
  the job's completion status; if the job errored on the server, the plugin
  may not surface it clearly until you look at the server console.
- Out-of-memory (OOM) on the GPU is the most common silent hang for diffusion
  plugins. Reduce input resolution, reduce sequence length, or move to a
  lower-VRAM model variant.
- Confirm the model weights have actually downloaded. Some custom nodes
  download large files on first use; if the disk fills up mid-download, the
  job stalls.

## The output looks corrupted, dim, washed out, or shifted

- **Color space:** the plugin works in linear scene-referred floating-point
  EXR. If your host is sending log or display-referred values, results will be
  wrong. Use the host's color management to provide linear scene-referred
  input to the plugin, and to convert the linear output back to your working
  space.
- **Y-flip:** if the output is upside down, this is a host-vs-OFX-spec
  mismatch in image origin. The plugin auto-detects the host's `nativeOrigin`
  property; if it gets it wrong, the plugin exposes a **Flip Y** parameter
  with `Auto / Always / Never` modes — try `Always` or `Never`.
- **Wrong channels:** some hosts send 3-channel RGB, some send RGBA, and EXR
  loaders in some custom nodes are picky. Check that your input clip has the
  channel layout the plugin expects (RGB for most plugins; the matting
  plugins also accept an alpha as a mask seed).

## "Workflow validation failed" / "Node not found"

- The custom node referenced by the workflow is not installed in ComfyUI, or
  the version installed has a different node name than the workflow expects.
  Re-check the [custom node list](comfyui-server-setup.md#2-install-the-custom-nodes)
  and make sure each one is installed and ComfyUI was restarted after install.
- Custom node updates sometimes rename nodes. If you upgraded a custom node
  recently, the workflow may need to be regenerated (open it in ComfyUI, fix
  any red nodes, re-export).

## The mask / alpha is empty or wrong (segmentation, matting plugins)

- For text-prompted segmentation: make the prompt more specific. `"person in
  foreground"` works better than `"person"` when there are multiple people.
- For matting: confirm the seed mask from SAM3 actually selects the subject.
  Use the plugin's preview to verify the seed before running the matting model.
- **Frame Index** is 0-based within the loaded sequence, not the timeline frame
  number. Off-by-one is a common mistake on first use.
- **Direction:** `forward` propagates the mask forward in time only; if the
  subject enters the frame mid-clip, try `both`.

## The upscaled output is cropped back to the source resolution (SeedVR2)
{: #fixed-format-host-crops-upscale }

**Symptom:** SeedVR2 is set to upscale 1280×720 → 1920×1080. The EXR files in
the shared output folder are correctly 1920×1080, but the node errors out with
*"The host allocated a 1280x720 output canvas…"*. On versions before this check
existed the node did not error — it silently delivered a 1280×720 **crop** of
the upscaled frame.

**Cause:** the host is *fixed-format* — it does not support multi-resolution.
The plugin reports its upscaled size through the standard OFX
`getRegionOfDefinition` action, but a host that reports
`kOfxImageEffectPropSupportsMultiResolution = 0` is entitled by the spec to
ignore it and always allocate an output buffer the size of the input clip.
**Autodesk Flame and Flare are in this group.** Nuke, Natron, Fusion Studio and
Resolve Studio's Fusion/Color pages are not, which is why the same setup
resizes the canvas correctly there.

**Confirm it** in `~/comfyui_plugin_<YYYYMMDD>.log`:

```text
Supports Multi-Resolution (HOST capability): NO  <-- host is fixed-format: ...
Frame 1: loadCachedResult branch = HOST-REFUSED-ROD | output=1920x1080 dstRoD=1280x720 ...
```

Note the log distinguishes *"plugin declares"* from *"HOST capability"* — only
the second one matters here.

**Fix — establish the target format upstream of the plugin.** In Flame Batch:

1. Add a **Resize** node (FX / Format tab) before the AIFX node and set its
   **Destination** output format to the target resolution (1920×1080). Its
   default `Centre/Crop` behaviour is what you want.
2. Apply the **SeedVR2** node after it, and set its **Resolution** parameter to
   the same target short side (1080).

The node's own format is then already 1080p, ComfyUI receives and returns
1920×1080, and nothing is refused. SeedVR2 still does its restoration pass at
the target resolution — the upstream Resize only establishes the canvas.

Without that upstream Resize the plugin refuses to render rather than deliver
something misleading. Scaling the upscale back down to fit would produce a
full frame with correct framing that is merely soft — the kind of shot that
passes review and ships. So the node raises a persistent error naming the two
resolutions and the fix, and **Job Status** turns red. The EXR already written
to the output folder is untouched and correct.

## Stale output: I changed a parameter and got the same result

- The plugin caches outputs by workflow hash. Most parameter changes
  invalidate the cache automatically. If they don't:
  - Toggle a parameter and toggle it back to force a re-render.
  - Use the host's per-clip cache clear.
  - Manually delete the cached output EXR from the shared output folder.

## Performance: it's much slower than it should be

- The dominant cost is the model itself. Open ComfyUI's console while
  rendering: if model inference is fast there but the plugin feels slow, the
  bottleneck is filesystem I/O.
- Network filesystem latency (NFS, SMB) can dominate for small frames. A 10
  Gbit local network mostly hides it; 1 Gbit doesn't.
- Sequence plugins amortize per-job overhead across many frames. If you're
  scrubbing one frame at a time, sequence plugins re-load the whole window
  every time. This is normal.

## Host quirks

The plugins target the OFX 1.4 standard, so any conforming host should load
them. In practice hosts have small deviations. The one caveat worth calling out:

- **DaVinci Resolve / Fusion (Linux):** use the official Linux builds, which
  link the C++ runtime statically. A dynamically-linked plugin binds to
  Resolve's own `libstdc++` and crashes on `std::filesystem` calls — this is
  exactly why the release binaries are built static (see the 0.1.8 changelog).
- **Autodesk Flame / Flare:** fixed-format host — an OFX node's output
  resolution is always the input clip's. Only affects the resolution-changing
  plugin, SeedVR2; see
  [the upscale is cropped back to source resolution](#fixed-format-host-crops-upscale).

No other host-specific quirks have been catalogued yet. If you hit one, please
[report it](#reporting-bugs--getting-help) with the host name and version so we
can add it here.

## Reporting bugs / getting help

We use two channels:

- **[GitHub Issues](https://github.com/Dev-Reepost/aifx/issues)** — the
  default place for bug reports, feature requests, host-compatibility notes,
  and questions. Search existing issues first; if nothing matches, open a new
  one with the information listed in
  [CONTRIBUTING.md](https://github.com/Dev-Reepost/aifx/blob/main/CONTRIBUTING.md#ways-to-contribute)
  (OS + host + plugin versions, repro steps, console output from both the
  host and the ComfyUI server).

- **[contact@reepost.fr](mailto:contact@reepost.fr)** — for anything that
  shouldn't be public yet: security issues, NDA-covered footage, partnership
  / commercial integration enquiries, or simply if you'd rather email than
  open an issue.

Both channels are read by the same maintainers — pick whichever fits the
situation.
