---
title: DepthCrafter
parent: Plugins
nav_order: 3
---

# DepthCrafter (`depth_crafter`)

**Sequence · Temporally consistent video depth (diffusion)**

A video diffusion model that estimates a depth sequence with temporal
consistency over an entire window of frames jointly, rather than estimating
each frame independently.

## What you give it

- An RGB clip.

## What you get back

- A depth sequence, one depth map per input frame, scaled in the same
  relative-depth space across the whole window. No flicker, no per-frame swim.

Typical compositing uses: 2.5D camera moves, depth-based defocus and
atmospherics, relighting, rotoscoping assists, stereo / parallax generation
from monocular plates — anywhere per-frame methods would create a wobble that
sells the depth pass as fake.

## Commercial use

| Component | License | Commercial OK? |
|---|---|---|
| DepthCrafter code (Tencent) | Custom Tencent non-commercial | ❌ No |
| DepthCrafter weights | Same Tencent non-commercial terms | ❌ No |
| Stable Video Diffusion XT base | Stability AI Non-Commercial Community License | ❌ No |

**Not safe for paid production work** without engaging both Tencent and
Stability AI for commercial agreements.

## Requirements

- **GPU VRAM:** ~26 GB at 1024×576, ~9 GB at 512×256. The ComfyUI port supports
  CPU offload down to an 8 GB minimum at low resolution.
- **ComfyUI custom node:** [akatz-ai/ComfyUI-DepthCrafter-Nodes](https://github.com/akatz-ai/ComfyUI-DepthCrafter-Nodes).
- **Model weights:**
  - [tencent/DepthCrafter](https://huggingface.co/tencent/DepthCrafter) — ~3.05 GB.
  - SVD-XT base: [stabilityai/stable-video-diffusion-img2vid-xt](https://huggingface.co/stabilityai/stable-video-diffusion-img2vid-xt) — ~9 GB.
- **Resolution:** dimensions must be multiples of 64.

## Parameters

| Parameter | Meaning |
|---|---|
| **Image Load Cap** | Number of frames in one ComfyUI job. Default and recommended: 75–110. The model runs all loaded frames jointly. |
| **Window Overlap** | Frames shared between consecutive sliding windows on long clips. Default: 25. |
| **Diffusion Steps** | Number of denoising steps. Higher = smoother output, longer render time. Typical: 5–25. |
| **Resolution** | Internal processing resolution. Multiples of 64 only. |
| **CPU Offload** | Enable to fit on smaller GPUs at the cost of speed. |

Plus the standard ComfyUI base parameters.

## Demos & comparisons

![DepthCrafter overview — long-video temporally-consistent depth estimation.](https://depthcrafter.github.io/img/overview.jpg)
*DepthCrafter overview figure. © Hu et al., Tencent AI Lab, CVPR 2025 Highlight. Source: [depthcrafter.github.io](https://depthcrafter.github.io/). Reproduced under fair-use citation; Tencent code/weights non-commercial.*

### Input → output

<div class="io-pair" markdown="0">
  <figure>
    <img src="https://depthcrafter.github.io/img/img01.jpg" alt="Input frame from open-world video">
    <figcaption>Input frame</figcaption>
  </figure>
  <figure>
    <img src="https://depthcrafter.github.io/img/d6_ours.png" alt="DepthCrafter depth estimate">
    <figcaption>DepthCrafter depth output</figcaption>
  </figure>
</div>
*Side-by-side: input RGB frame and DepthCrafter's per-pixel depth result. © Hu et al., Tencent AI Lab, CVPR 2025; reproduced under fair-use citation.*

The strongest argument for a temporally-consistent depth model is visual.
The upstream sources show side-by-side reels against per-frame methods:

- **Project page** — [depthcrafter.github.io](https://depthcrafter.github.io/) — hero comparison videos vs. Depth Anything, Marigold, NVDS, ChronoDepth.
- **GitHub README** — [Tencent/DepthCrafter](https://github.com/Tencent/DepthCrafter) — animated open-world clips with their depth sequences.

Image attribution: Hu et al., Tencent AI Lab, CVPR 2025 Highlight;
reproduced for documentation purposes with citation to
[arXiv:2409.02095](https://arxiv.org/abs/2409.02095).
See the [credits page](../assets/credits.md).

## Performance

- ~2.1 fps at 1024×576 on an A100 (~465 ms/frame), times the number of
  diffusion steps.
- ~8.6 fps at 512×256 on the same hardware.
- Quadratic scaling in resolution; sub-linear in window size up to the cap.

## Limitations

- **Affine-invariant relative depth.** Not metric. Don't interpret raw values
  as world-scale distances.
- **Window scale drift:** on very long clips (multiple sliding windows),
  scale/shift can drift between windows. The plugin and node blend in the
  overlap region, but extreme cases need post-processing.
- **Fast motion:** very fast motion or heavy motion blur degrades temporal
  stability.
- **Transparent / reflective surfaces:** poorly handled (inherited from the
  SVD prior).

## When to use this vs Depth Anything V3

- **Use DepthCrafter** when temporal stability dominates the project — e.g.
  parallax moves, depth-based comp work where flicker is unacceptable.
- **Use [Depth Anything V3](depth_da3.md)** when per-frame fidelity matters
  more, when you need metric depth, when you need commercial-license-safe
  output, or when VRAM and time budgets rule out diffusion.

## Credits

> Wenbo Hu, Xiangjun Gao, Xiaoyu Li, Sijie Zhao, Xiaodong Cun, Yong Zhang,
> Long Quan, Ying Shan. **DepthCrafter: Generating Consistent Long Depth
> Sequences for Open-world Videos.** CVPR 2025 (Highlight). Tencent AI Lab.
> [Paper](https://arxiv.org/abs/2409.02095) · [Project page](https://depthcrafter.github.io/) · [GitHub](https://github.com/Tencent/DepthCrafter)

ComfyUI port by [akatz-ai](https://github.com/akatz-ai/ComfyUI-DepthCrafter-Nodes).

### Citation

{% raw %}
```bibtex
@inproceedings{hu2025depthcrafter,
  author    = {Hu, Wenbo and Gao, Xiangjun and Li, Xiaoyu and Zhao, Sijie and
               Cun, Xiaodong and Zhang, Yong and Quan, Long and Shan, Ying},
  title     = {DepthCrafter: Generating Consistent Long Depth Sequences for Open-world Videos},
  booktitle = {Proceedings of the IEEE/CVF Conference on Computer Vision and Pattern Recognition (CVPR)},
  year      = {2025},
  note      = {Highlight},
  eprint    = {2409.02095},
  archivePrefix = {arXiv}
}
```
{% endraw %}
