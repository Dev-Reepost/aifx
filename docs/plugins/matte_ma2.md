---
title: MatAnyone2 Matting
parent: Plugins
nav_order: 6
---

# MatAnyone2 Matting (`matte_ma2`)

**Sequence · Fast recurrent video alpha matting**

Refines a binary mask seed into a soft alpha matte using a recurrent video
matting network with memory propagation. The lighter, faster counterpart to
[`matte_mama`](matte_mama.md).

## What you give it

- An RGB clip.
- A binary mask seed — typically from [`segmentation_sam3`](segmentation_sam3.md)
  in the same workflow.

## What you get back

- A single-channel soft alpha matte clip with hair-and-edge detail.

Typical VFX use: rotoscoping a person/subject for keying, background
replacement, relighting passes — wherever you want clean edges without paying
the per-frame cost of a diffusion sampler.

## Commercial use

| Component | License | Commercial OK? |
|---|---|---|
| MatAnyone2 code | NTU S-Lab License 1.0 (non-commercial) | ❌ No |
| MatAnyone2 weights | NTU S-Lab License 1.0 (non-commercial) | ❌ No |

**Not safe for paid production work.** Commercial use requires a separate
agreement with NTUitive / SenseTime.

## Requirements

- **GPU VRAM:** Substantially lower than diffusion-based matting. Expected to
  fit on 8–12 GB GPUs at 1080p (upstream does not publish a hard minimum).
- **ComfyUI custom node:** [spiritform/comfy-matanyone2](https://github.com/spiritform/comfy-matanyone2) (alternative: [FuouM/ComfyUI-MatAnyone](https://github.com/FuouM/ComfyUI-MatAnyone)).
- **Model weights:** this plugin needs **two** sets of weights:
  - **SAM3 checkpoint** — placed **manually** in `ComfyUI/models/checkpoints/`
    (default `sam3.1_multiplex_fp16.safetensors`). SAM3 is **gated** on Hugging
    Face: run `hf auth login` and accept the model terms before downloading.
    SAM3 is a **shared dependency** also used by the
    [`segmentation_sam3`](segmentation_sam3.md) plugin — see that page and the
    [ComfyUI server setup](../comfyui-server-setup.md) for the one-time download
    and placement steps.
  - **MatAnyone2 weights** —
    [`matanyone2.pth`](https://github.com/pq-yang/MatAnyone2/releases/download/v1.0.0/matanyone2.pth)
    (~400 MB), pulled automatically by the comfy-matanyone2 custom node. Also
    mirrored on Hugging Face at
    [`PeiqingYang/MatAnyone2`](https://huggingface.co/PeiqingYang/MatAnyone2).

## Parameters

This plugin runs SAM3 segmentation and MatAnyone2 matting in a single workflow,
so it exposes both sets of controls.

**SAM3 Segmentation**

| Parameter | Meaning |
|---|---|
| **Prompt** | Text description of the subject to segment (e.g. `person`, `hair`, `girl`). |
| **Object Indices** | Comma-separated indices of the detected objects to keep. |
| **Reference Frame** | Integer index of the frame fed to SAM3 detection. |
| **Detection Threshold** | SAM3 detection confidence threshold (used by both detect and video-track stages). |
| **Max Objects** | Cap on the number of tracked objects (`0` = no cap). |
| **Detect Interval** | How often (in frames) detection is re-run during tracking. |
| **Refine Iterations** | Number of mask-refinement passes on the reference frame. |
| **Individual Masks** | Emit a separate mask per detected object instead of one combined mask. |

**MatAnyone2**

| Parameter | Meaning |
|---|---|
| **Frame Limit** | Maximum frames loaded for MatAnyone2 (`0` = no limit). |
| **Mask Frame** | Index of the frame whose SAM3 mask seeds MatAnyone2's propagation. |
| **Warmup Frames** | Warmup frames processed before the main sequence to initialise the memory network for smoother mattes. |
| **Erode Radius** | Morphological erosion radius applied to the input mask; shrinks the boundary (`0` = none). |
| **Dilate Radius** | Morphological dilation radius applied to the input mask; expands the boundary (`0` = none). |
| **Max Internal Size** | Maximum resolution for internal processing (`-1` = use input resolution). |
| **Memory Frames** | Frames kept in the short-term memory buffer; more = better temporal consistency, more VRAM. |
| **Long-Term Memory** | Enable long-term memory for better consistency over longer sequences. |

**Model**

| Parameter | Meaning |
|---|---|
| **SAM3 Checkpoint** | Checkpoint filename in `ComfyUI/models/checkpoints` (e.g. `sam3.1_multiplex_fp16.safetensors`). |

Plus the standard ComfyUI base parameters.

## Demos & comparisons

![MatAnyone 1 vs MatAnyone 2 — alpha matte quality comparison.](https://pq-yang.github.io/projects/MatAnyone2/assets/figures/matanyone1vs2.png)
*Side-by-side v1 vs v2 comparison. © Yang et al., S-Lab @ NTU + SenseTime, CVPR 2025/2026. Source: [pq-yang.github.io/projects/MatAnyone2](https://pq-yang.github.io/projects/MatAnyone2/). Reproduced under fair-use citation; weights non-commercial (NTU S-Lab License 1.0).*

### Input → output

<div class="io-pair" markdown="0">
  <figure>
    <video autoplay loop muted playsinline preload="metadata">
      <source src="https://pq-yang.github.io/projects/MatAnyone2/assets/videos_mat/mixkit-man-breakdancing-452-full-hd_78_input_sm.mp4" type="video/mp4">
    </video>
    <figcaption>Input clip</figcaption>
  </figure>
  <figure>
    <video autoplay loop muted playsinline preload="metadata">
      <source src="https://pq-yang.github.io/projects/MatAnyone2/assets/videos_mat/mixkit-man-breakdancing-452-full-hd_78_pha_sm.mp4" type="video/mp4">
    </video>
    <figcaption>MatAnyone 2 alpha matte</figcaption>
  </figure>
</div>
*Fast recurrent video matting result. © Yang et al., S-Lab @ NTU + SenseTime, CVPR 2026 Highlight. Source: [pq-yang.github.io/projects/MatAnyone2](https://pq-yang.github.io/projects/MatAnyone2/). Reproduced under fair-use citation.*

- **MatAnyone2 project page** — [pq-yang.github.io/projects/MatAnyone2](https://pq-yang.github.io/projects/MatAnyone2/) — teaser, hair/edge demonstrations, v1-vs-v2 comparisons.
- **MatAnyone (v1) project page** — [pq-yang.github.io/projects/MatAnyone](https://pq-yang.github.io/projects/MatAnyone/) — extensive demo gallery on difficult shots.
- **GitHub** — [pq-yang/MatAnyone2](https://github.com/pq-yang/MatAnyone2) — code and pretrained checkpoint.

Image attribution: Yang et al., S-Lab @ NTU + SenseTime, CVPR 2025/2026;
reproduced for documentation purposes with citation to
[arXiv:2512.11782](https://arxiv.org/abs/2512.11782) and
[arXiv:2501.14677](https://arxiv.org/abs/2501.14677). See the
[credits page](../assets/credits.md).

## Performance

- Each frame is a single forward pass with a recurrent state — no iterative
  denoising. Substantially faster than diffusion-based matting.
- Interactive rates on a single mid-range GPU at 1080p in practice.
- 4K works but is slower and VRAM-hungry.

## Limitations

- **Seed quality matters.** The binary mask from SAM3 defines the target.
  A poor seed (missed limbs, halo, wrong instance) propagates.
- **Memory drift on long clips:** the recurrent state can drift across very
  long shots, occlusions, or shot changes. Re-seed with a fresh mask after
  each cut.
- **Failure modes:** fast motion blur on thin structures, near-camera
  transparent objects, identical-color background/foreground, multi-instance
  ambiguity (the model tracks only the seeded subject).
- **Humans-first training:** non-human subjects are out-of-distribution and
  may degrade. Test before relying on it for animals, objects, or stylized
  characters.

## When to use this vs MaMa

- **Use MatAnyone2** for throughput, smaller VRAM budgets, and the bulk of
  human-subject roto/keying work.
- **Use [MaMa](matte_mama.md)** when you need maximum alpha quality on hard
  cases (fine hair, semi-transparent edges) and have the VRAM and time budget.

## Credits

> Peiqing Yang, Shangchen Zhou, Kai Hao, Qingyi Tao. **MatAnyone 2: Scaling
> Video Matting via a Learned Quality Evaluator.** CVPR 2026 (Highlight).
> S-Lab @ NTU + SenseTime.
> [Paper](https://arxiv.org/abs/2512.11782) · [Project page](https://pq-yang.github.io/projects/MatAnyone2/) · [GitHub](https://github.com/pq-yang/MatAnyone2)

> Peiqing Yang, Shangchen Zhou, Jixin Zhao, Qingyi Tao, Chen Change Loy.
> **MatAnyone: Stable Video Matting with Consistent Memory Propagation.**
> CVPR 2025.
> [Paper](https://arxiv.org/abs/2501.14677) · [Project page](https://pq-yang.github.io/projects/MatAnyone/) · [GitHub](https://github.com/pq-yang/MatAnyone)

ComfyUI wrapper by [spiritform](https://github.com/spiritform/comfy-matanyone2).

### Citation

{% raw %}
```bibtex
@InProceedings{yang2026matanyone2,
  title     = {{MatAnyone 2}: Scaling Video Matting via a Learned Quality Evaluator},
  author    = {Yang, Peiqing and Zhou, Shangchen and Hao, Kai and Tao, Qingyi},
  booktitle = {CVPR},
  year      = {2026}
}

@InProceedings{yang2025matanyone,
  title     = {{MatAnyone}: Stable Video Matting with Consistent Memory Propagation},
  author    = {Yang, Peiqing and Zhou, Shangchen and Zhao, Jixin and Tao, Qingyi and Loy, Chen Change},
  booktitle = {CVPR},
  year      = {2025}
}
```
{% endraw %}
