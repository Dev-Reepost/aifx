---
title: NormalCrafter
parent: Plugins
nav_order: 2
---

# NormalCrafter (`normal_crafter`)

**Per-frame · Surface normal map estimation**

Estimate per-pixel surface normals from a single RGB frame, encoded as RGB
where the channels map to the X/Y/Z components of the unit normal vector.

## What you give it

- An RGB clip.

## What you get back

- A per-frame surface normal map (RGB-encoded unit normals).

Typical VFX uses: relighting and synthetic shading passes, normal-based AOVs
for matte/roto refinement, retopology and projection-mapping guides, bump and
displacement extraction, re-shading of plate elements without rebuilding
geometry.

## Commercial use

| Component | License | Commercial OK? |
|---|---|---|
| Code (`Binyr/NormalCrafter`) | MIT | ✅ Yes |
| NormalCrafter weights | Apache 2.0 | ✅ Yes (in isolation) |
| Stable Video Diffusion base (pulled at runtime) | Stability AI Non-Commercial Community License | ❌ No |

The runtime SVD dependency is the binding constraint. **Not safe for paid
production** without engaging Stability AI for a commercial license.

## Requirements

- **GPU VRAM:** ~20 GB at 1024×576, ~6 GB at 512×256.
- **ComfyUI custom node:** [AIWarper/ComfyUI-NormalCrafterWrapper](https://github.com/AIWarper/ComfyUI-NormalCrafterWrapper).
- **Model weights:**
  - [Yanrui95/NormalCrafter](https://huggingface.co/Yanrui95/NormalCrafter)
  - SVD-XT base: [stabilityai/stable-video-diffusion-img2vid-xt](https://huggingface.co/stabilityai/stable-video-diffusion-img2vid-xt) (~9 GB, pulled on first run).
- **Resolution constraint:** dimensions are typically rounded to multiples of 64 (SVD constraint).

## Parameters

| Parameter | Meaning |
|---|---|
| **Process Resolution** | Internal processing resolution (`max_res_dimension`). Higher = more detail and more VRAM. |
| _Wrapper-disabled knobs_ | `fps_for_time_ids`, `motion_bucket_id`, `noise_aug_strength` were observed by the wrapper author to have minimal effect and are hardcoded — not exposed as parameters. |

Plus the standard ComfyUI base parameters (server URL, mount paths, project
name, workflow path).

## Demos & comparisons

To see what this model produces, the upstream sources have the most
authoritative demos:

- **Project page** — [normalcrafter.github.io](https://normalcrafter.github.io/) — side-by-side video comparisons against per-frame normal estimators.
- **GitHub README** — [Binyr/NormalCrafter](https://github.com/Binyr/NormalCrafter) — open-world clips with their normal sequences.

Image attribution: Bin et al., ICCV 2025; reproduced for documentation
purposes with citation to [arXiv:2504.11427](https://arxiv.org/abs/2504.11427).
See the [credits page](../assets/credits.md).

## Limitations

- **Per-frame invocation:** the upstream model is sequence-aware via a sliding
  window. The OFX plugin invokes it one frame at a time, so temporal flicker
  between frames is expected. For shots where stability dominates, run the
  upstream pipeline directly with full window context outside the plugin and
  bring the result in as a clip.
- **Failure modes:** transparent / refractive surfaces, mirrors, strong
  speculars, very low-light footage with crushed blacks, fine subpixel
  structures (hair, foliage edges), and heavy motion blur all degrade output.
- **Output convention:** RGB-packed unit normals; verify camera vs world
  space and axis convention in your shading pipeline before relying on the
  values.

## Credits

> Yanrui Bin, Wenbo Hu, Haoyuan Wang, Xinya Chen, Bing Wang. **NormalCrafter:
> Learning Temporally Consistent Normals from Video Diffusion Priors.** ICCV
> 2025. [Paper](https://arxiv.org/abs/2504.11427) · [Project page](https://normalcrafter.github.io/) · [GitHub](https://github.com/Binyr/NormalCrafter)

ComfyUI wrapper by [AIWarper](https://github.com/AIWarper/ComfyUI-NormalCrafterWrapper).

### Citation

{% raw %}
```bibtex
@inproceedings{bin2025normalcrafter,
  author    = {Bin, Yanrui and Hu, Wenbo and Wang, Haoyuan and Chen, Xinya and Wang, Bing},
  title     = {NormalCrafter: Learning Temporally Consistent Normals from Video Diffusion Priors},
  booktitle = {Proceedings of the IEEE/CVF International Conference on Computer Vision (ICCV)},
  year      = {2025},
  eprint    = {2504.11427},
  archivePrefix = {arXiv}
}
```
{% endraw %}
