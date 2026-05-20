---
title: ComfyUI server setup
nav_order: 3
---

# ComfyUI server setup

The plugins do not run AI models themselves. They send work to a ComfyUI server
that loads the models, executes them, and returns the results. This page is
the canonical guide to standing that server up.

## Architecture recap

```
[ OFX host ] ──HTTP──▶ [ ComfyUI server ]
       │                       │
       └──── shared filesystem ┘
            (input + output EXR files)
```

The host machine and the ComfyUI machine can be the same physical computer,
or separate machines on a network. In a studio setting, **separate** is the
more common pattern — one or more GPU machines serve many artists.

You need three things:

1. **A ComfyUI installation** with the right custom nodes installed.
2. **The model weights** for each plugin you intend to use.
3. **A shared filesystem** that both the host and the ComfyUI server can read
   and write.

## 1. Install ComfyUI

Follow the [official ComfyUI installation guide](https://github.com/comfyanonymous/ComfyUI#installing).

### GPU recommendations by plugin

The dominant resource is GPU VRAM. Approximate ranges (lower numbers achievable
with quantized variants and CPU offload):

| Plugin | Typical VRAM | Minimum VRAM | Notes |
|---|---|---|---|
| `depth_da3` | 6–10 GB | 4 GB | Smaller variants fit consumer GPUs comfortably. |
| `normal_crafter` | 20 GB @ 1024×576 | 6 GB @ 512×256 | SVD-based; resolution-sensitive. |
| `depth_crafter` | 26 GB @ 1024×576 | 8 GB with CPU offload | Diffusion video; the heaviest standard run. |
| `segmentation_sam3` | 4 GB | 4 GB | 24 GB+ recommended for 4K work. |
| `matte_mama` | High (SVD-XT base) | TBD | Single-pass diffusion despite the SVD base. |
| `matte_ma2` | Moderate | TBD | Recurrent network — lighter than diffusion. |
| `upscale_seedvr2` | 4–24+ GB | 4 GB (GGUF Q4_K_M) | 7B FP16 wants H100; 3B and quantized variants run on consumer cards. |

A single 24 GB GPU (RTX 3090 / 4090 / A5000-class) can handle every plugin in
this suite at standard production resolutions. Heavy work at 4K wants 48 GB+.

## 2. Install the custom nodes

ComfyUI itself does not ship the model code for these plugins — that lives in
community **custom node** repositories. Install via [ComfyUI Manager](https://github.com/ltdrdata/ComfyUI-Manager)
or by cloning into `ComfyUI/custom_nodes/`.

| Plugin | Required custom node |
|---|---|
| `depth_da3` | [PozzettiAndrea/ComfyUI-DepthAnythingV3](https://github.com/PozzettiAndrea/ComfyUI-DepthAnythingV3) |
| `normal_crafter` | [AIWarper/ComfyUI-NormalCrafterWrapper](https://github.com/AIWarper/ComfyUI-NormalCrafterWrapper) |
| `depth_crafter` | [akatz-ai/ComfyUI-DepthCrafter-Nodes](https://github.com/akatz-ai/ComfyUI-DepthCrafter-Nodes) |
| `segmentation_sam3` | [PozzettiAndrea/ComfyUI-SAM3](https://github.com/PozzettiAndrea/ComfyUI-SAM3) |
| `matte_mama` | [okdalto/ComfyUI-VideoMaMa](https://github.com/okdalto/ComfyUI-VideoMaMa) (depends on SAM3) |
| `matte_ma2` | [spiritform/comfy-matanyone2](https://github.com/spiritform/comfy-matanyone2) (depends on SAM3) |
| `upscale_seedvr2` | [numz/ComfyUI-SeedVR2_VideoUpscaler](https://github.com/numz/ComfyUI-SeedVR2_VideoUpscaler) |

Restart ComfyUI after installing custom nodes. Verify that each new node
appears in the ComfyUI workflow editor before continuing.

## 3. Download model weights

Each plugin's documentation page lists the weights it needs and where to get
them. The plugins themselves do not download anything — weights are pulled by
ComfyUI on first use, or you can pre-download them to avoid surprises.

> **License notice.** Model weights are governed by their upstream licenses.
> Several models in this suite are restricted to non-commercial use. See each
> plugin page and the [release spec](../RELEASE_SPEC.md#5-license-and-funding)
> for the full breakdown.

## 4. Set up the shared input/output folders

The plugin writes input frames as EXR files into an **input** directory. The
ComfyUI workflow loads from there, runs the model, and writes the output back
as EXR into an **output** directory. The plugin then reads the output back.

For this to work:

- Both the host machine and the ComfyUI server must be able to read and write
  the same physical location.
- Both sides need to know the path under their own mount point.

### Single machine

Both sides see the same paths natively. Configure both to e.g.:

```
/Users/<you>/comfyui-share/in/
/Users/<you>/comfyui-share/out/
```

In each plugin's parameters, set **Client Mount Path** and **Server Mount Path**
to the same value.

### Networked: Linux ComfyUI server, macOS / Linux clients

Mount the server's storage on each client via NFS or SMB.

| Side | Path |
|---|---|
| Linux ComfyUI server | `/mnt/comfyui-share/in/` |
| macOS client (Finder mount) | `/Volumes/comfyui-share/in/` |

In the plugin, set:
- **Client Mount Path:** `/Volumes/comfyui-share`
- **Server Mount Path:** `/mnt/comfyui-share`

The plugin substitutes the server path into the workflow JSON so ComfyUI
receives the path it can actually read.

### Networked: Windows ComfyUI server

Use UNC paths for the server side:

| Side | Path |
|---|---|
| Windows ComfyUI server | `\\COMFYBOX\share\in\` |
| macOS / Linux client (mount) | `/Volumes/share/in/` (or wherever) |

In the plugin:
- **Client Mount Path:** `/Volumes/share`
- **Server Mount Path:** `\\COMFYBOX\share`

The plugin handles the backslash escaping for the workflow JSON automatically.

### Folder layout the plugin creates

Inside the input and output directories, the plugin creates a project /
workflow / frame hierarchy automatically:

```
<input-mount>/
  <project-name>/
    <workflow-name>/
      <frame-or-sequence-id>.exr
<output-mount>/
  <project-name>/
    <workflow-name>/
      <frame-or-sequence-id>.exr
```

You only need to provision the top-level mount points. Everything underneath
is managed by the plugin.

## 5. Start ComfyUI

```bash
python main.py --listen 0.0.0.0 --port 8188
```

`--listen 0.0.0.0` lets remote hosts connect. Restrict via firewall to your
trusted network.

## 6. Configure the plugin

In your OFX host, apply a plugin to a clip and set:

- **ComfyUI Server URL:** `http://<server-ip>:8188`
- **Client Mount Path:** the input/output share path on the host machine.
- **Server Mount Path:** the same share path on the ComfyUI server machine.

Trigger a render. Watch the ComfyUI console — you should see a workflow
submitted and executed.

**For a permanent setup**, don't enter these values per-clip — pre-fill
them in each plugin's `defaults.json` so every clip starts with the right
server and paths. See [Configuration & defaults](configuration.md).

## Health check

A quick way to confirm everything is wired up correctly:

1. In any web browser on the host machine, open
   `http://<server-ip>:8188/`. The ComfyUI UI should load.
2. From a terminal on the host machine, write a test file to the share:
   `echo hello > /<client-mount>/test.txt`.
3. From a terminal on the server, read it back:
   `cat /<server-mount>/test.txt`. Same file, same content.

If both work, the plugin will work.

## Security notes

- ComfyUI's HTTP API has no authentication by default. Keep it on a trusted
  internal network and use firewall rules to restrict access.
- The shared filesystem similarly has no plugin-level access control. Anyone
  with access to the share can read EXR data flowing through it.
- Model weights are large. Download them from the official upstream sources
  listed on each plugin page, not from random mirrors.
