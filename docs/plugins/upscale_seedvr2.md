---
title: SeedVR2 Upscaler
parent: Plugins
nav_order: 7
---

# SeedVR2 Upscaler (`upscale_seedvr2`)

**Sequence · Diffusion-transformer video super-resolution**

Generative video super-resolution that hallucinates plausible high-frequency
detail with temporal coherence. Built on a Diffusion Transformer (DiT) and a
VAE, distilled to a single-step denoising pass.

## What you give it

- A low-resolution RGB clip.

## What you get back

- A high-resolution RGB clip with synthesized detail that stays coherent
  across time (no swimming or boiling micro-detail).

Typical VFX uses: legacy SD/HD footage uprez to UHD/4K, plate restoration on
damaged or compressed elements, archival recovery, AI-generated-content cleanup.

## Commercial use

| Component | License | Commercial OK? |
|---|---|---|
| SeedVR2 code (ByteDance Seed) | Apache 2.0 | ✅ Yes |
| SeedVR2-3B / SeedVR2-7B weights | Apache 2.0 | ✅ Yes |
| ComfyUI port (numz) | Apache 2.0 | ✅ Yes |

**Safe for paid production work.** Preserve the Apache 2.0 NOTICE in
redistributions. Cite the ICLR 2026 paper.

## Requirements

- **GPU VRAM** (per ComfyUI port):

  | Variant | VRAM |
  |---|---|
  | 7B FP16 | ~20–24 GB+ |
  | 7B FP8 mixed | ~16–20 GB |
  | 3B FP16 | ~12–16 GB |
  | 3B FP8 | ~8–12 GB |
  | GGUF Q4_K_M | ~4–8 GB (with BlockSwap + VAE tiling) |

  VAE adds ~2–4 GB on top.

- **ComfyUI custom node:** [numz/ComfyUI-SeedVR2_VideoUpscaler](https://github.com/numz/ComfyUI-SeedVR2_VideoUpscaler) (NumZ + AInVFX/Adrien Toupet).
- **Model weights:**
  - [ByteDance-Seed/SeedVR2-3B](https://huggingface.co/ByteDance-Seed/SeedVR2-3B) (~6 GB FP16).
  - [ByteDance-Seed/SeedVR2-7B](https://huggingface.co/ByteDance-Seed/SeedVR2-7B) (~14 GB FP16).
  - ComfyUI-packaged variants: [numz/SeedVR2_comfyUI](https://huggingface.co/numz/SeedVR2_comfyUI), [AInVFX/SeedVR2_comfyUI](https://huggingface.co/AInVFX/SeedVR2_comfyUI).
  - Low-VRAM quantizations: [cmeka/SeedVR2-GGUF](https://huggingface.co/cmeka/SeedVR2-GGUF) (Q8_0 / Q4_K_M).
  - VAE checkpoint: ~2–4 GB.

## Parameters

| Parameter | Meaning |
|---|---|
| **Model Variant** | 3B / 7B, FP16 / FP8 / GGUF — pick to match your VRAM. |
| **Target Shortest-Edge Resolution** | The output resolution. The model handles arbitrary upscale ratios via adaptive window attention. |
| **Image Load Cap** | Frame batch size. **Must follow the 4n+1 rule** (1, 5, 9, 13, 17, 21, 25, …). Minimum 5 for temporal consistency. |
| **Temporal Tile Overlap** | When tiling long clips, the overlap between consecutive batches (0–16 frames). |
| **VAE Tiling** | Enable above ~1080p output to keep VAE within VRAM. |
| **BlockSwap** | Off-loads layers to CPU/RAM to fit smaller GPUs (no effect on macOS/MPS). |
| **torch.compile** | Significant speedup; longer first-run compile time. |

Plus the standard ComfyUI base parameters.

## Demos & comparisons

- **Project page** — [iceclear.github.io/projects/seedvr2](https://iceclear.github.io/projects/seedvr2/) — side-by-side LR input / SeedVR2 / multi-step baselines (RealBasicVSR, Upscale-A-Video, VEnhancer).
- **GitHub README** — [ByteDance-Seed/SeedVR](https://github.com/ByteDance-Seed/SeedVR) — degraded real-world plates restored to high resolution. Repository assets are Apache 2.0 (redistributable with NOTICE preserved).

Image attribution: Wang et al., ByteDance Seed, ICLR 2026; reproduced for
documentation purposes with citation to
[arXiv:2506.05301](https://arxiv.org/abs/2506.05301). See the
[credits page](../assets/credits.md).

## Performance

- One-step inference (vs. 15–50 steps for prior diffusion VR models) — paper
  claims >4× speedup over multi-step diffusion VR baselines at comparable or
  better quality.
- Speed scales with: model size (3B faster than 7B), batch size (larger faster
  per-frame), torch.compile, output resolution.
- Reference setups from the official repo: 1×H100-80G handles 100×720×1280;
  4×H100-80G handles 1080p / 2K via sequence parallel (`sp_size=4`).

## Limitations

- **Heavy degradations:** the model is not robust to extreme degradation or
  very large motion — may fail to remove the degradation or produce
  unpleasing detail.
- **Lightly degraded input:** on very clean input (e.g. native 720p AIGC),
  the model tends to **over-generate detail**, producing an oversharpened
  "fake-crisp" look. Use a lighter variant or a smaller upscale ratio.
- **Long clips:** require streaming / chunked mode to avoid OOM. The plugin's
  `Image Load Cap` controls this.
- **macOS/MPS:** BlockSwap unavailable (unified memory architecture).
- **Older GPUs without bfloat16** (e.g. GTX 970-class): automatic fallback
  path with caveats.

## Credits

> Jianyi Wang, Shanchuan Lin, Zhijie Lin, Yuxi Ren, Meng Wei, Zongsheng Yue,
> Shangchen Zhou, Hao Chen, Yang Zhao, Ceyuan Yang, Xuefeng Xiao, Chen Change
> Loy, Lu Jiang. **SeedVR2: One-Step Video Restoration via Diffusion
> Adversarial Post-Training.** ICLR 2026. ByteDance Seed.
> [Paper](https://arxiv.org/abs/2506.05301) · [Project page](https://iceclear.github.io/projects/seedvr2/) · [GitHub](https://github.com/ByteDance-Seed/SeedVR)

ComfyUI port by [NumZ](https://github.com/numz/ComfyUI-SeedVR2_VideoUpscaler)
and AInVFX (Adrien Toupet).

### Citation

```bibtex
@inproceedings{wang2026seedvr2,
  author    = {Wang, Jianyi and Lin, Shanchuan and Lin, Zhijie and Ren, Yuxi and
               Wei, Meng and Yue, Zongsheng and Zhou, Shangchen and Chen, Hao and
               Zhao, Yang and Yang, Ceyuan and Xiao, Xuefeng and Loy, Chen Change and
               Jiang, Lu},
  title     = {SeedVR2: One-Step Video Restoration via Diffusion Adversarial Post-Training},
  booktitle = {International Conference on Learning Representations (ICLR)},
  year      = {2026},
  eprint    = {2506.05301},
  archivePrefix = {arXiv}
}

@inproceedings{wang2025seedvr,
  author    = {Wang, Jianyi and Lin, Zhijie and others},
  title     = {SeedVR: Seeding Infinity in Diffusion Transformer Towards Generic Video Restoration},
  booktitle = {Proceedings of the IEEE/CVF Conference on Computer Vision and Pattern Recognition (CVPR)},
  year      = {2025},
  note      = {Highlight}
}
```
