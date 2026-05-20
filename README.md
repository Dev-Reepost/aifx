# AIFX

Open-source OpenFX plugins that bring state-of-the-art ComfyUI AI models into
any OFX-compatible compositor, editor, or color grading tool.

> **Status:** pre-release. Plugins are functional but undergoing testing in
> production hosts. Until V1.0.0 is tagged, breaking changes may land at any
> time.

## What's in the box

Seven plugins that bridge an OFX host to a ComfyUI server:

| Plugin | What it does |
|---|---|
| **Depth Anything V3** (`depth_da3`) | Per-frame monocular depth estimation. |
| **NormalCrafter** (`normal_crafter`) | Temporally coherent surface normal maps from a clip (diffusion video prior). |
| **DepthCrafter** (`depth_crafter`) | Temporally consistent video depth (diffusion). |
| **SAM3 Segmentation** (`segmentation_sam3`) | Text/click-prompted mask propagation through a clip. |
| **MaMa Matting** (`matte_mama`) | Diffusion-based high-quality video alpha matting. |
| **MatAnyone2 Matting** (`matte_ma2`) | Fast recurrent video alpha matting. |
| **SeedVR2 Upscaler** (`upscale_seedvr2`) | Diffusion-based video super-resolution. |

See [the plugin index](docs/plugins/) for what each one needs and how to use it.

## Quick start

1. **Install the plugins** — see [docs/installation.md](docs/installation.md).
   The `.ofx.bundle` files go into the standard OFX plugin directory for your
   OS; any OFX-compatible host will pick them up.
2. **Set up a ComfyUI server** — see
   [docs/comfyui-server-setup.md](docs/comfyui-server-setup.md). Install
   ComfyUI, install the custom nodes for the plugins you want, download the
   model weights, and configure the shared input/output folders.
3. **Use the plugins in your host** — they appear under the `AIFX`
   category. Each plugin has parameters for the ComfyUI server URL, input/output
   folder paths, and model-specific settings. See the per-plugin pages.

## Architecture in one paragraph

The plugin runs inside the host. The AI models run in ComfyUI on a separate
process (often a separate, GPU-equipped machine). They communicate through:
**(a)** a shared filesystem for image data (EXR files in/out), and
**(b)** the ComfyUI HTTP API for orchestration. This means your host machine
doesn't need a GPU, and one ComfyUI server can serve multiple workstations.

For details, see [docs/architecture.md](docs/architecture.md).

## Important: model weight licenses

The **plugin code** in this repository is BSD-3-Clause and freely usable for
any purpose, including commercial production. The **AI model weights** that
ComfyUI loads are governed by their upstream licenses, several of which
restrict use to non-commercial / research purposes. Each plugin's documentation
page lists the relevant license. Verify before shipping a paid project.

## Requirements

- An OFX-compatible host (any application implementing OpenFX 1.4 or later).
- A ComfyUI server reachable from the host (local or networked).
- A shared filesystem accessible from both the host and the ComfyUI server.
- A GPU on the ComfyUI server side. Most plugins want 8 GB+ VRAM; some want
  24 GB+. See per-plugin requirements.

## Build from source

```bash
./tools/build-plugin.sh plugins/<name> --install
```

See [docs/building.md](docs/building.md) for the full build matrix and
dependencies.

## Documentation

- [Installation](docs/installation.md)
- [ComfyUI server setup](docs/comfyui-server-setup.md)
- [Workflow customization](docs/workflow-customization.md)
- [Architecture](docs/architecture.md)
- [Troubleshooting](docs/troubleshooting.md)
- [Plugin reference](docs/plugins/)

The full documentation is also published as a website at
`https://<org>.github.io/<repo>/`.

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md). Bug reports and pull requests are welcome.
For new plugins, see [docs/architecture.md](docs/architecture.md) for the shared
infrastructure and the `isSequencePlugin()` design.

## License

Plugin code is BSD-3-Clause. See [LICENSE](LICENSE).

## Acknowledgements

This work was supported by **CNC (Centre national du cinéma et de l'image
animée)**.

The plugins are bridges to upstream models built by other teams. Each plugin's
documentation page credits the original authors and links to their papers,
code, and project pages. Without their work, this project would not exist.

The plugins build on the [OpenFX specification](https://openeffects.org/)
([source on GitHub](https://github.com/AcademySoftwareFoundation/openfx)),
maintained by the Academy Software Foundation.
