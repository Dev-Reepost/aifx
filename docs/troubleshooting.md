---
title: Troubleshooting
nav_order: 7
---

# Troubleshooting

A grouped reference of the failure modes most users hit. If you don't find your
issue here, see the [GitHub issues](https://github.com/Dev-Reepost/aifx/issues).

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
- On Windows ComfyUI servers, Server Mount Path must use UNC format
  (`\\HOST\share`). The plugin handles JSON escaping; you do **not** need to
  double-escape backslashes yourself.

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

The plugin targets the OFX 1.4 standard. In practice, hosts have small
deviations. Hosts the maintainers have tested with, and known caveats:

| Host | Notes |
|---|---|
| _(populated as testing progresses)_ | _(populated as testing progresses)_ |

Hosts not in this table are still expected to work — the OFX standard is the
contract — but have not been verified by the maintainers. Bug reports for new
hosts are welcome.
