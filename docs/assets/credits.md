---
title: Image credits & attribution
nav_order: 9
---

# Image credits & attribution

This page lists every upstream image, figure, and demo asset referenced in
the AIFX documentation, with full attribution and the applicable
license terms.

## Policy

The documentation reproduces or links to selected figures and demo imagery
from the upstream research projects that each plugin bridges to. This usage
is for **documentation, commentary, and educational purposes** under
applicable fair use / fair dealing provisions, with attribution to the
original authors and citation to the source publications.

The repository's BSD-3-Clause license applies to the plugin code only. Each
image listed below retains the license of its original source. If you are an
author and would prefer a different attribution form, or removal, please
[open an issue](https://github.com/Dev-Reepost/aifx/issues) and we will
respond promptly.

## Sources by plugin

Each entry below lists every upstream asset referenced from the plugin's
documentation page (schema images, input/output demo videos and GIFs).

### Depth Anything V3 (`depth_da3`)

| Source | Authors / Institution | License | Citation |
|---|---|---|---|
| [Project page](https://depth-anything-3.github.io/) | Lin et al., ByteDance Seed, 2025 | Author copyright; reuse terms not explicitly stated | [arXiv:2511.10647](https://arxiv.org/abs/2511.10647) |
| `assets/teaser.png` and `assets/teaser_compress.mp4` (project page) | Same | Apache 2.0 attribution | Same |
| [GitHub README assets](https://github.com/ByteDance-Seed/Depth-Anything-3) | Same | Apache 2.0 (code repo) — README image reuse with NOTICE | Same |
| [Hugging Face Space demo](https://huggingface.co/spaces/depth-anything/depth-anything-3) | Same | App: Apache 2.0; output: per generation | Same |

### NormalCrafter (`normal_crafter`)

| Source | Authors / Institution | License | Citation |
|---|---|---|---|
| [Project page](https://normalcrafter.github.io/) | Bin, Hu, Wang, Chen, Wang, ICCV 2025 | Author copyright; reuse terms not explicitly stated | [arXiv:2504.11427](https://arxiv.org/abs/2504.11427) |
| `img/pipeline.png` and `video/comparison/breakdance.mp4` (project page) | Same | MIT attribution (project repo MIT-licensed) | Same |
| [GitHub README assets](https://github.com/Binyr/NormalCrafter) | Same | MIT (code repo) — README image reuse with copyright preserved | Same |

### DepthCrafter (`depth_crafter`)

| Source | Authors / Institution | License | Citation |
|---|---|---|---|
| [Project page](https://depthcrafter.github.io/) | Hu et al., Tencent AI Lab, CVPR 2025 Highlight | Tencent custom non-commercial license; documentation reuse with attribution | [arXiv:2409.02095](https://arxiv.org/abs/2409.02095) |
| `img/overview.jpg`, `img/img01.jpg`, `img/d6_ours.png` (project page) | Same | Tencent non-commercial; fair-use citation | Same |
| [GitHub README assets](https://github.com/Tencent/DepthCrafter) | Same | Same non-commercial; cite + attribute | Same |

### SAM3 Segmentation (`segmentation_sam3`)

| Source | Authors / Institution | License | Citation |
|---|---|---|---|
| [Project page](https://ai.meta.com/sam3/) | Carion et al., Meta AI Research, 2025 | © Meta Platforms; SAM License for code and weights | [arXiv:2511.16719](https://arxiv.org/abs/2511.16719) |
| `assets/model_diagram.png` and `assets/dog.gif` (GitHub repo) | Same | SAM License — redistribution permitted with attribution | Same |
| [Meta AI blog post](https://ai.meta.com/blog/segment-anything-model-3/) | Same | © Meta Platforms; reuse with attribution per Meta's standard terms | Same |
| [GitHub README assets](https://github.com/facebookresearch/sam3) | Same | SAM License | Same |

### MaMa Matting (`matte_mama`)

| Source | Authors / Institution | License | Citation |
|---|---|---|---|
| [Project page](https://cvlab-kaist.github.io/VideoMaMa/) | Lim, Oh, Huang, Yoon, Kim, Lee, CVPR 2026 | CC BY-NC 4.0 (code repo); reuse for documentation permitted with attribution | [arXiv:2601.14255](https://arxiv.org/abs/2601.14255) |
| `assets/videomama.png`, `comparison/basket_rgb.mp4`, `comparison/basket_mask.mp4` (project page) | Same | CC BY-NC 4.0; fair-use citation | Same |
| [HF Space demo](https://huggingface.co/spaces/SammyLim/VideoMaMa) | Same | App: per HF Space terms | Same |

### MatAnyone2 Matting (`matte_ma2`)

| Source | Authors / Institution | License | Citation |
|---|---|---|---|
| [Project page (v2)](https://pq-yang.github.io/projects/MatAnyone2/) | Yang, Zhou, Hao, Tao, CVPR 2026 Highlight | NTU S-Lab License 1.0 (non-commercial) | [arXiv:2512.11782](https://arxiv.org/abs/2512.11782) |
| `assets/figures/matanyone1vs2.png` and `assets/videos_mat/mixkit-man-breakdancing-452-full-hd_78_{input,pha}_sm.mp4` (project page) | Same | NTU S-Lab License 1.0; fair-use citation | Same |
| [Project page (v1)](https://pq-yang.github.io/projects/MatAnyone/) | Yang, Zhou, Zhao, Tao, Loy, CVPR 2025 | Same | [arXiv:2501.14677](https://arxiv.org/abs/2501.14677) |

### SeedVR2 Upscaler (`upscale_seedvr2`)

| Source | Authors / Institution | License | Citation |
|---|---|---|---|
| [Project page](https://iceclear.github.io/projects/seedvr2/) | Wang et al., ByteDance Seed, ICLR 2026 | Author copyright; reuse terms not explicitly stated | [arXiv:2506.05301](https://arxiv.org/abs/2506.05301) |
| `images/result1.png` (project page); demo MP4s under [datasets/Iceclear/SeedVR_VideoDemos](https://huggingface.co/datasets/Iceclear/SeedVR_VideoDemos) on Hugging Face | Same | Apache 2.0 — redistributable with NOTICE preserved | Same |
| [GitHub README assets](https://github.com/ByteDance-Seed/SeedVR) | Same | **Apache 2.0** — redistributable with NOTICE preserved | Same |

## ComfyUI custom node attributions

Many plugins also reference ComfyUI custom node implementations by community
authors. These wrappers do the bridge work between ComfyUI and the upstream
model. The plugins are grateful to:

- [PozzettiAndrea](https://github.com/PozzettiAndrea) — ComfyUI-DepthAnythingV3, ComfyUI-SAM3
- [AIWarper](https://github.com/AIWarper) — ComfyUI-NormalCrafterWrapper
- [akatz-ai](https://github.com/akatz-ai) — ComfyUI-DepthCrafter-Nodes
- [okdalto](https://github.com/okdalto) — ComfyUI-VideoMaMa
- [spiritform](https://github.com/spiritform) — comfy-matanyone2
- [NumZ](https://github.com/numz) and [AInVFX / Adrien Toupet](https://github.com/AInVFX) — ComfyUI-SeedVR2_VideoUpscaler

Each is referenced from the relevant plugin documentation page.

## OpenFX

The plugins implement the [OpenFX specification](https://openeffects.org/),
maintained by the Academy Software Foundation
([openfx on GitHub](https://github.com/AcademySoftwareFoundation/openfx)).
License: BSD-3-Clause.

## Authors & funding

AIFX was developed by
[MaGMa](https://www.linkedin.com/company/ma-g-ma/)
for [Reepost Studio](https://www.reepoststudio.fr/), with funding from
**CNC (Centre national du cinéma et de l'image animée)**.
