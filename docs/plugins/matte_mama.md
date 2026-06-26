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
- **Model weights:** this plugin needs two model sources:
  - **SAM3 checkpoint** (for the seed-mask / tracking path) — **gated** on Hugging
    Face. Run `hf auth login` and accept the model terms, then place the checkpoint
    (e.g. `sam3.1_multiplex_fp16.safetensors`) manually in
    `ComfyUI/models/checkpoints/`. SAM3 is a shared dependency — see the
    [SAM3 plugin page](segmentation_sam3.md) and the
    [ComfyUI server setup](../comfyui-server-setup.md) for the full gating steps.
  - **MaMa pipeline weights** loaded by `VideoMaMaPipelineLoader` — both the SVD
    base model and the VideoMaMa UNet checkpoint:
    - [SammyLim/VideoMaMa](https://huggingface.co/SammyLim/VideoMaMa) (fine-tuned UNet + DINO projection).
    - SVD-XT base: [stabilityai/stable-video-diffusion-img2vid-xt](https://huggingface.co/stabilityai/stable-video-diffusion-img2vid-xt) — ~9.5 GB.

## Parameters

### SAM3 Segmentation

| Parameter | Meaning |
|---|---|
| **Prompt** | Text description of the subject to segment and matte (e.g. `person`, `hair`, `girl`). |
| **Object Indices** | Comma-separated SAM3 object indices to keep in the matte. |
| **Reference Frame** | Reference frame used for the initial SAM3 detection. |
| **Detect Threshold** | SAM3 detection confidence threshold on the reference frame (0–1). |
| **Track Threshold** | Per-frame detection threshold while tracking through the sequence. |
| **Max Objects** | Maximum number of objects to track (0 = unlimited). |
| **Detect Interval** | Re-run detection every N frames. |
| **Refine Iterations** | Number of detection refinement iterations on the reference frame. |
| **Individual Masks** | Emit individual per-object masks. |

### VideoMaMa Sampler

| Parameter | Meaning |
|---|---|
| **Max Resolution** | Maximum resolution of the longer side; frames are resized to fit. |
| **FPS** | Frames-per-second hint passed to the VideoMaMa sampler. |
| **Motion Bucket ID** | Amount of motion expected (higher = more motion). Inherited from SVD; 127 is the standard default. |
| **Noise Aug Strength** | Noise augmentation strength for the conditioning frame; 0 = none (recommended for clean mattes). |
| **Frame Limit** | Maximum number of frames to load per sequence pass (0 = no limit). |
| **Seed** | Random seed for reproducibility; 0 = a new random seed each run. |

### VideoMaMa Model

| Parameter | Meaning |
|---|---|
| **Base Model** | Path to the SVD base model, relative to the ComfyUI models dir. |
| **VideoMaMa UNet** | Path to the VideoMaMa UNet checkpoint, relative to the ComfyUI models dir. |
| **SAM3 Checkpoint** | SAM3 checkpoint filename in `ComfyUI/models/checkpoints`. |
| **Precision** | Model computation precision: BF16 (Ampere+ GPUs, recommended), FP16 (half), FP32 (full). |
| **Attention** | Attention implementation: Auto (best available), SDPA (PyTorch 2.0+), xFormers (memory-efficient). |
| **CPU Offload** | Offload model weights to CPU to reduce VRAM usage. |
| **VAE Chunk Size** | Frames encoded per VAE pass; lower = less VRAM. |
| **VAE Tiling** | Enable VAE tiling for very high resolution inputs. |
| **VAE Slicing** | Enable VAE slicing to reduce VRAM when encoding many frames. |

Plus the standard ComfyUI base parameters.

## Demos & comparisons

![VideoMaMa architecture and teaser — mask-guided video matting via generative prior.](https://cvlab-kaist.github.io/VideoMaMa/assets/videomama.png)
*VideoMaMa pipeline. © Lim et al., KAIST CVLab / Korea University / Adobe Research, CVPR 2026. Source: [cvlab-kaist.github.io/VideoMaMa](https://cvlab-kaist.github.io/VideoMaMa/). Reproduced under fair-use citation; weights non-commercial (CC BY-NC 4.0 + SVD).*

### Input → output

<div class="io-pair" markdown="0">
  <figure>
    <video autoplay loop muted playsinline preload="metadata">
      <source src="https://cvlab-kaist.github.io/VideoMaMa/comparison/basket_rgb.mp4" type="video/mp4">
    </video>
    <figcaption>Input clip</figcaption>
  </figure>
  <figure>
    <video autoplay loop muted playsinline preload="metadata">
      <source src="https://cvlab-kaist.github.io/VideoMaMa/comparison/basket_mask.mp4" type="video/mp4">
    </video>
    <figcaption>Refined alpha matte</figcaption>
  </figure>
</div>
*Mask-guided video matting result. © Lim et al., CVPR 2026, [cvlab-kaist.github.io/VideoMaMa](https://cvlab-kaist.github.io/VideoMaMa/). Fair-use citation.*

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

{% raw %}
```bibtex
@article{lim2026videomama,
  title   = {VideoMaMa: Mask-Guided Video Matting via Generative Prior},
  author  = {Lim, Sangbeom and Oh, Seoung Wug and Huang, Jiahui and
             Yoon, Heeji and Kim, Seungryong and Lee, Joon-Young},
  journal = {arXiv preprint arXiv:2601.14255},
  year    = {2026}
}
```
{% endraw %}
