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
  `xattr -dr com.apple.quarantine /path/to/Plugin.ofx.bundle` to clear the
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
xattr -dr com.apple.quarantine "/Applications/AIFX Installer.app"
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

Then restart the host. The macOS installer defaults to system-wide from v0.2.2;
earlier versions defaulted to per-user.

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
