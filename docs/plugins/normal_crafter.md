---
title: NormalCrafter
parent: Plugins
nav_order: 2
---

# NormalCrafter (`normal_crafter`)

**Sequence · Surface normal map estimation (diffusion video prior)**

Estimate per-pixel surface normals across a clip, encoded as RGB where the
channels map to the X/Y/Z components of the unit normal vector. The plugin
loads a contiguous window of frames into ComfyUI in one job so the upstream
SVD-based sliding-window sampler can produce temporally coherent normals
across time.

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
- **Model weights:** the shipped workflow has **no separate model-loader node** — the
  `NormalCrafterNode` custom node resolves and downloads its (SVD-based) weights
  internally on first use, including the Stable Video Diffusion base it builds on.
  Upstream source: [Yanrui95/NormalCrafter](https://huggingface.co/Yanrui95/NormalCrafter)
  (SVD-XT base: [stabilityai/stable-video-diffusion-img2vid-xt](https://huggingface.co/stabilityai/stable-video-diffusion-img2vid-xt), ~9 GB, pulled on first run).
- **Resolution constraint:** dimensions are typically rounded to multiples of 64 (SVD constraint).

## Parameters

| Parameter | Meaning |
|---|---|
| **Seed** | Random seed for diffusion sampling. Change to vary the result; keep the same value for reproducible runs. |
| **Max Res Dimension** | Maximum resolution along the longest image side (`max_res_dimension`). Larger values give finer detail but require more VRAM; 1024 is the recommended default. |
| **Window Size** | Number of frames processed per temporal window. Must not exceed the total frame count (clamped down to **Frame Limit** automatically). Larger windows improve temporal consistency but require more VRAM. |
| **Time Step Size** | Diffusion time-step size. Higher values are faster but lower quality. |
| **Decode Chunk Size** | Number of frames the VAE decodes at once. Lower values use less VRAM at the cost of speed. |
| **Frame Limit** | Maximum number of frames loaded from the sequence (`image_load_cap`). Set to 0 for no limit. Keep ≤ **Window Size** for best results. |
| **Offload Pipe On Finish** | Move the NormalCrafter pipeline to CPU once inference completes, freeing VRAM for downstream nodes. |
| **Use xFormers** | Memory-efficient attention via xFormers. Choices: **Auto** (use it if available), **Enable** (force on), **Disable** (force off). |
| _Wrapper-disabled knobs_ | `fps_for_time_ids`, `motion_bucket_id`, `noise_aug_strength` were observed by the wrapper author to have minimal effect and are hardcoded — not exposed as parameters. |

Plus the standard ComfyUI base parameters (server URL, mount paths, project
name, workflow path).

## Demos & comparisons

![NormalCrafter pipeline: video-to-normal-map diffusion architecture.](https://normalcrafter.github.io/img/pipeline.png)
*NormalCrafter pipeline diagram. © Bin et al., ICCV 2025. Source: [normalcrafter.github.io](https://normalcrafter.github.io/). Used for documentation under MIT attribution.*

### Input → output

<figure class="single-demo" markdown="0">
  <video autoplay loop muted playsinline preload="metadata">
    <source src="https://normalcrafter.github.io/video/comparison/breakdance.mp4" type="video/mp4">
  </video>
  <figcaption>Input clip and the NormalCrafter normal-map result (upstream side-by-side reel)</figcaption>
</figure>
*Comparison reel. © Bin et al., ICCV 2025, [normalcrafter.github.io](https://normalcrafter.github.io/). Reproduced under MIT attribution; cite [arXiv:2504.11427](https://arxiv.org/abs/2504.11427).*

To see what this model produces, the upstream sources have the most
authoritative demos:

- **Project page** — [normalcrafter.github.io](https://normalcrafter.github.io/) — side-by-side video comparisons against per-frame normal estimators.
- **GitHub README** — [Binyr/NormalCrafter](https://github.com/Binyr/NormalCrafter) — open-world clips with their normal sequences.

Image attribution: Bin et al., ICCV 2025; reproduced for documentation
purposes with citation to [arXiv:2504.11427](https://arxiv.org/abs/2504.11427).
See the [credits page](../assets/credits.md).

## Limitations

- **Window-scale drift:** on very long clips processed in multiple sliding
  windows, scale/orientation drift can occur at window joins. Use a larger
  `Image Load Cap` when VRAM allows so the whole shot fits in one pass.
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
