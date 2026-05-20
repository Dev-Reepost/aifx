---
title: Depth Anything V3
parent: Plugins
nav_order: 1
---

# Depth Anything V3 (`depth_da3`)

**Per-frame · Monocular depth estimation**

Estimate a per-pixel depth map from a single RGB frame.

## What you give it

- An RGB clip.

## What you get back

- A per-frame depth map encoded as image data (relative depth by default;
  metric depth with the `DA3METRIC-LARGE` variant).

Typical VFX uses: depth-based defocus, atmospheric haze, depth-driven roto
assistance, parallax 2.5D moves, relighting passes — without needing tracked
geometry or stereo capture.

## Commercial use

| Variant | License | Commercial OK? |
|---|---|---|
| `DA3-SMALL` | Apache 2.0 | ✅ Yes |
| `DA3-BASE` | Apache 2.0 | ✅ Yes |
| `DA3METRIC-LARGE` | Apache 2.0 | ✅ Yes |
| `DA3MONO-LARGE` | Apache 2.0 | ✅ Yes |
| `DA3-LARGE` | CC BY-NC 4.0 | ❌ No |
| `DA3-GIANT` | CC BY-NC 4.0 | ❌ No |
| `DA3NESTED-*` | CC BY-NC 4.0 | ❌ No |

For paid production work, use one of the Apache-licensed variants.

## Requirements

- **GPU VRAM** (approximate):
  - SMALL: ~2 GB
  - BASE: ~3 GB
  - LARGE: ~6 GB
  - GIANT: ~12 GB+
- **ComfyUI custom node:** [PozzettiAndrea/ComfyUI-DepthAnythingV3](https://github.com/PozzettiAndrea/ComfyUI-DepthAnythingV3) (MIT)
- **Model weights:** auto-downloaded by the custom node to
  `ComfyUI/models/depthanything3/`. Hugging Face source:
  [`depth-anything/DA3-*`](https://huggingface.co/depth-anything).

## Parameters

The plugin exposes the standard ComfyUI base parameters (server URL, mount
paths, project name, workflow path) plus:

| Parameter | Meaning |
|---|---|
| **Model Variant** | Which DA3 checkpoint to load: SMALL, BASE, LARGE, GIANT, METRIC-LARGE, MONO-LARGE. |
| **Process Resolution** | Internal processing resolution. Higher = more detail, more VRAM. |
| **Output Mode** | Relative depth (default) or metric depth (METRIC-LARGE only). |

## Demos & comparisons

![Depth Anything 3 vs prior depth/geometry models — teaser comparison.](https://depth-anything-3.github.io/assets/teaser.png)
*Performance comparison teaser. © Lin et al., ByteDance Seed, 2025. Source: [depth-anything-3.github.io](https://depth-anything-3.github.io/). Used for documentation under Apache 2.0 attribution.*

To see what this model produces, the upstream sources have the most
authoritative demos:

- **Project page** — [depth-anything-3.github.io](https://depth-anything-3.github.io/) — gallery and comparisons against DA2 and VGGT.
- **Hugging Face Space** — [interactive demo](https://huggingface.co/spaces/depth-anything/depth-anything-3) — upload your own image and see the result.
- **GitHub README** — [ByteDance-Seed/Depth-Anything-3](https://github.com/ByteDance-Seed/Depth-Anything-3) — side-by-side RGB / depth / point-cloud comparisons.

Image attribution: ByteDance Seed; reproduced for documentation purposes
with citation to [arXiv:2511.10647](https://arxiv.org/abs/2511.10647).
See the [credits page](../assets/credits.md).

## Limitations

- Depth is **relative / affine-invariant** unless you use the metric variant.
  Do not interpret raw values as world-scale distances.
- Transparent surfaces (glass, water, smoke) collapse depth ambiguously.
- Mirrors and strong specular highlights return the depth of the reflected
  scene, not the surface.
- Heavy motion blur degrades stability. The model has no temporal consistency
  for video — consider [DepthCrafter](depth_crafter.md) when temporal
  stability matters more than per-frame fidelity.

## Credits

This plugin is a thin wrapper around the work of:

> Haotong Lin, Sili Chen, Jun Hao Liew, Donny Y. Chen, Zhenyu Li, Guang Shi,
> Jiashi Feng, Bingyi Kang. **Depth Anything 3: Recovering the Visual Space
> from Any Views.** arXiv preprint arXiv:2511.10647, 2025. ByteDance Seed.
> [Paper](https://arxiv.org/abs/2511.10647) · [Project page](https://depth-anything-3.github.io/) · [GitHub](https://github.com/ByteDance-Seed/Depth-Anything-3)

ComfyUI node by [PozzettiAndrea](https://github.com/PozzettiAndrea/ComfyUI-DepthAnythingV3).

### Citation

{% raw %}
```bibtex
@article{depthanything3,
  title   = {Depth Anything 3: Recovering the Visual Space from Any Views},
  author  = {Haotong Lin and Sili Chen and Jun Hao Liew and Donny Y. Chen and
             Zhenyu Li and Guang Shi and Jiashi Feng and Bingyi Kang},
  journal = {arXiv preprint arXiv:2511.10647},
  year    = {2025}
}
```
{% endraw %}
