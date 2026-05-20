---
title: MaMa Matting
parent: Plugins
nav_order: 5
---

# MaMa Matting (`matte_mama`)

**Sequence · Diffusion-based high-quality video alpha matting**

Refines a coarse binary mask (typically from SAM3) into a high-quality,
temporally consistent alpha matte with hair, edge, and semi-transparent detail.
Built on Stable Video Diffusion as a generative prior.

## What you give it

- An RGB clip.
- A binary mask seed — typically from [`segmentation_sam3`](segmentation_sam3.md)
  in the same workflow, but any binary mask source works.

## What you get back

- A single-channel soft alpha matte clip.

Typical VFX use: greenscreen-free keying of talent or objects, refining roto
for hair and fur, producing soft mattes from rough roto without per-frame
manual cleanup.

## Commercial use

| Component | License | Commercial OK? |
|---|---|---|
| VideoMaMa code | CC BY-NC 4.0 | ❌ No |
| VideoMaMa weights | Stability AI Community License (via SVD-XT base) | ❌ No (above the SVD revenue threshold) |
| Stable Video Diffusion XT base | Stability AI Non-Commercial Community License | ❌ No |

**Not safe for paid production** as shipped. Both the code and weights stack
require non-commercial use.

## Requirements

- **GPU VRAM:** ~24 GB consumer GPU (RTX 3090/4090) recommended at default
  1024 px. Lower `max_resolution` reduces VRAM roughly linearly.
- **ComfyUI custom node:** [okdalto/ComfyUI-VideoMaMa](https://github.com/okdalto/ComfyUI-VideoMaMa) (depends on a SAM3 node for the seed mask path).
- **Model weights:**
  - [SammyLim/VideoMaMa](https://huggingface.co/SammyLim/VideoMaMa) (fine-tuned UNet + DINO projection).
  - SVD-XT base: [stabilityai/stable-video-diffusion-img2vid-xt](https://huggingface.co/stabilityai/stable-video-diffusion-img2vid-xt) — ~9.5 GB.

## Parameters

| Parameter | Meaning |
|---|---|
| **Max Resolution** | Longest-axis processing resolution (256–2048, default 1024). Aspect ratio preserved, snapped to multiples of 8. |
| **Image Load Cap** | Frames per VideoMaMa pass. SVD backbone processes ~14–25 frames per window. |
| **Noise Augmentation Strength** | Increase for unusual cinematic looks if results don't match the seed. |
| **SAM3 sub-parameters** | Text Prompt, Score Threshold, Direction, etc. — see the [SAM3 plugin page](segmentation_sam3.md) for meanings. |

Plus the standard ComfyUI base parameters.

## Demos & comparisons

- **Project page** — [cvlab-kaist.github.io/VideoMaMa](https://cvlab-kaist.github.io/VideoMaMa/) — side-by-side input mask vs. refined alpha on hair-heavy subjects, comparison reels vs. MatAnyone and RVM.
- **Hugging Face Space demo** — [SammyLim/VideoMaMa](https://huggingface.co/spaces/SammyLim/VideoMaMa) — try the model on your own clips.
- **arXiv paper figures** — [arxiv.org/html/2601.14255v1](https://arxiv.org/html/2601.14255v1) — qualitative before/after grids.

Image attribution: Lim et al., KAIST CVLab / Korea University / Adobe
Research, CVPR 2026; reproduced for documentation purposes with citation to
[arXiv:2601.14255](https://arxiv.org/abs/2601.14255). See the
[credits page](../assets/credits.md).

## Performance

- Single forward pass per window — fast for a video diffusion model, but
  still seconds-to-minutes per shot, not real-time.
- Quality scales meaningfully with input resolution and seed-mask quality.

## Limitations

- **Seed-mask quality bounds the result.** A bad seed produces a bad matte —
  no amount of refinement can recover content the seed missed entirely.
- **Failure modes:** extremely fine isolated hair against busy backgrounds,
  heavy motion blur, smoke / fog / transparent fluids, very thin filaments,
  subjects whose silhouette diverges drastically from the seed mask.
- **Training data:** synthetic only. Unusual cinematic looks may need
  `Noise Augmentation Strength` tuned up.
- **Window seam artifacts:** very long clips are processed in overlapping
  windows; minor seams may appear at window joins on hard cuts.

## When to use this vs MatAnyone2

- **Use MaMa** when you need maximum alpha quality, especially for hair and
  semi-transparent edges, and have the VRAM budget.
- **Use [MatAnyone2](matte_ma2.md)** when you need throughput, are running on
  a smaller GPU, or are processing many shots.

## Credits

> Sangbeom Lim, Seoung Wug Oh, Jiahui Huang, Heeji Yoon, Seungryong Kim,
> Joon-Young Lee. **VideoMaMa: Mask-Guided Video Matting via Generative
> Prior.** CVPR 2026. KAIST CVLab / Korea University / Adobe Research.
> [Paper](https://arxiv.org/abs/2601.14255) · [Project page](https://cvlab-kaist.github.io/VideoMaMa/) · [GitHub](https://github.com/cvlab-kaist/VideoMaMa) · [HF Space demo](https://huggingface.co/spaces/SammyLim/VideoMaMa)

ComfyUI wrapper by [okdalto](https://github.com/okdalto/ComfyUI-VideoMaMa).

### Citation

```bibtex
@article{lim2026videomama,
  title   = {VideoMaMa: Mask-Guided Video Matting via Generative Prior},
  author  = {Lim, Sangbeom and Oh, Seoung Wug and Huang, Jiahui and
             Yoon, Heeji and Kim, Seungryong and Lee, Joon-Young},
  journal = {arXiv preprint arXiv:2601.14255},
  year    = {2026}
}
```
