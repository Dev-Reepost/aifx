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
- **Model weights:**
  [`matanyone2.pth`](https://github.com/pq-yang/MatAnyone2/releases/download/v1.0.0/matanyone2.pth) (~400 MB).
  Also mirrored on Hugging Face at [`PeiqingYang/MatAnyone2`](https://huggingface.co/PeiqingYang/MatAnyone2).

## Parameters

| Parameter | Meaning |
|---|---|
| **Image Load Cap** | Maximum frames per pass. Recurrent design supports arbitrarily long clips, but memory state can drift over very long shots — re-seed per shot. |
| **Max Size** | Optional resolution cap; downsample if minimum dimension exceeds the threshold. |
| **SAM3 sub-parameters** | Text Prompt, Score Threshold, Direction, etc. — see the [SAM3 plugin page](segmentation_sam3.md). |

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
