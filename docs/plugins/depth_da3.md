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
- **Model weights:** the embedded workflow's `DownloadAndLoadDepthAnythingV3Model`
  node auto-downloads the selected model variant to
  `ComfyUI/models/depthanything3/` on first use. Hugging Face source:
  [`depth-anything/DA3-*`](https://huggingface.co/depth-anything).

## Parameters

The plugin exposes the standard ComfyUI base parameters (server URL, mount
paths, project name, workflow path) plus:

| Parameter | Meaning |
|---|---|
| **Normalization** | How depth is normalized. *V2-Style* (default) gives normalized 0–1 depth with sky-mask support, recommended for ControlNet; *Raw* gives unnormalized metric depth for 3D reconstruction. |
| **Resize Method** | How the input is fitted to the model's resolution: *Resize* (scale to fit, default), *Pad* (preserve aspect ratio), or *Crop* (center-crop). |
| **Invert Depth** | Swap near and far, so near becomes far and far becomes near. Off by default. |
| **Keep Model Size** | Output depth at the model's internal resolution instead of upscaling back to the input resolution. Off by default. |
| **Model Variant** | Which DA3 checkpoint to load. Options: DA3-Small (80M, fast), DA3-Base (220M, balanced), DA3-Large (350M, high quality — default), DA3-Giant (1.15B, best quality), DA3-Mono-Large and DA3-Metric-Large (350M, sky-mask support), DA3-Nested-Giant-Large (1.4B, combined model with metric scaling). |
| **Precision** | Computation precision: *Auto* (default, picks the best for your GPU), *FP16* (faster, less VRAM), *FP32* (slower, more accurate), or *BF16* (Ampere+ GPUs). |
| **Attention** | Attention implementation: *Flash Attention* (fastest, default; needs the flash-attn library), *xFormers* (fast alternative), or *Math* (standard PyTorch, always available). |

## Demos & comparisons

![Depth Anything 3 vs prior depth/geometry models — teaser comparison.](https://depth-anything-3.github.io/assets/teaser.png)
*Performance comparison teaser. © Lin et al., ByteDance Seed, 2025. Source: [depth-anything-3.github.io](https://depth-anything-3.github.io/). Used for documentation under Apache 2.0 attribution.*

### Input → output

<figure class="single-demo" markdown="0">
  <video autoplay loop muted playsinline preload="metadata">
    <source src="https://depth-anything-3.github.io/assets/teaser_compress.mp4" type="video/mp4">
  </video>
  <figcaption>DA3 depth and geometry results on real-world clips</figcaption>
</figure>
*Hero teaser reel. © ByteDance Seed, 2025; reproduced for documentation purposes under Apache 2.0 attribution. Source: [depth-anything-3.github.io](https://depth-anything-3.github.io/).*

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
