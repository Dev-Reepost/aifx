---
title: SAM3 Segmentation
parent: Plugins
nav_order: 4
---

# SAM3 Segmentation (`segmentation_sam3`)

**Sequence · Text/click-prompted mask propagation**

Segment objects in a clip from a text prompt or click, with the mask propagated
through the entire sequence. Built on Meta's Segment Anything Model 3.

## What you give it

- An RGB clip.
- A **text prompt** ("yellow school bus", "person in foreground"), an
  **image exemplar**, or a **click / box** on a frame.

## What you get back

- A per-frame alpha mask propagated across the clip.

Typical VFX use: rapid rotoscoping for sky replacement, character isolation,
garbage matte generation, or driving a secondary color grade — work that
previously required frame-by-frame manual roto.

## Commercial use

| Component | License | Commercial OK? |
|---|---|---|
| SAM3 code (Meta) | SAM License | ✅ With conditions |
| SAM3 weights | SAM License | ✅ With conditions |

The SAM License permits commercial use (non-exclusive, royalty-free) but
**prohibits use in military, weapons, ITAR/export-controlled, nuclear, and
surveillance applications**. Redistribution must keep the SAM License notice
attached. Acknowledgement is required in any publication using SAM materials.

## Requirements

- **GPU VRAM:** ~4 GB minimum for inference; 16 GB consumer GPU comfortable
  for HD work; 24 GB+ for 4K end-to-end. Multi-object real-time video targets
  H100/H200-class GPUs.
- **ComfyUI custom node:** [PozzettiAndrea/ComfyUI-SAM3](https://github.com/PozzettiAndrea/ComfyUI-SAM3) (alternative: [yolain/ComfyUI-Easy-Sam3](https://github.com/yolain/ComfyUI-Easy-Sam3)).
- **Model weights:** [facebook/sam3](https://huggingface.co/facebook/sam3) — single ~3.4 GB checkpoint. **Gated** on Hugging Face: run `hf auth login` and accept the model terms before downloading. The workflow loads it with `CheckpointLoaderSimple`, which does **not** auto-download — place the checkpoint **manually** in `ComfyUI/models/checkpoints/` (the `Checkpoint` parameter names the file, default `sam3.1_multiplex_fp16.safetensors`).

## Parameters

| Parameter | Meaning |
|---|---|
| **Prompt** | Open-vocabulary text description of what to segment (e.g. `person`, `car`, `girl`). Default `foreground`. More specific = better. |
| **Threshold** | Detection confidence threshold (0.0–1.0). Higher = stricter detection; lower finds more. Default `0.3`. |
| **Reference Frame** | 0-based index of the frame in the loaded EXR batch to use as the reference for the initial SAM3 detection (not the timeline frame). Default `0`. |
| **Object Indices** | Comma-separated list of detected object indices to keep in the mask (e.g. `0` or `0,2,5`). Default `0`. |
| **Max Objects** | Maximum number of detected objects to track. `0` = unlimited (default). |
| **Detect Interval** | Run SAM3 detection every N frames; intermediate frames are tracked. Default `1` (detect every frame). |
| **Refine Iterations** | Number of mask refinement iterations on the reference frame. Default `2`. |
| **Individual Masks** | Generate separate masks per detected object instead of a single combined mask. Default off. |
| **Frame Limit** | Maximum number of frames to load from the input sequence in one ComfyUI propagation pass. `0` = no limit. Default `50`. |
| **Checkpoint** | SAM3 checkpoint filename, relative to ComfyUI's `models/checkpoints/` directory. Default `sam3.1_multiplex_fp16.safetensors`. |

Plus the standard ComfyUI base parameters.

## Demos & comparisons

![SAM 3 model architecture — promptable concept and instance segmentation.](https://raw.githubusercontent.com/facebookresearch/sam3/main/assets/model_diagram.png)
*SAM 3 model architecture diagram. © Meta Platforms, Inc. Source: [facebookresearch/sam3](https://github.com/facebookresearch/sam3). Reproduced under SAM License with attribution.*

### Input → output

![SAM 3 mask propagation through a clip — dog example.](https://raw.githubusercontent.com/facebookresearch/sam3/main/assets/dog.gif)
*Animated demo: text/click prompt → propagated mask across a clip. © Meta Platforms, Inc., SAM License.*

- **Project page** — [ai.meta.com/sam3](https://ai.meta.com/sam3/) — Meta's official demos and capability overview.
- **Blog post** — [ai.meta.com/blog/segment-anything-model-3](https://ai.meta.com/blog/segment-anything-model-3/) — release announcement with hero illustrations and worked examples.
- **Interactive demo** — [segment-anything.com](https://segment-anything.com/) — try it on your own images.
- **GitHub examples** — [facebookresearch/sam3 notebooks](https://github.com/facebookresearch/sam3/tree/main/notebooks) — example outputs.

Image attribution: © Meta Platforms, Inc.; reproduced for documentation
purposes with citation to [arXiv:2511.16719](https://arxiv.org/abs/2511.16719).
See the [credits page](../assets/credits.md).

## Performance

- ~30 ms per image (H200, 100+ objects).
- SAM 3.1 reaches ~32 FPS on H100 for medium-object-count video.
- Latency scales with object count.

## Limitations

- **Long occlusion:** SAM 3 can lose track when an object is occluded for many
  frames and may fail to recover when it reappears.
- **Identity swaps:** between visually similar instances, especially when they
  cross paths.
- **Boundary drift:** under slow lighting changes or low-contrast edges.
- **Motion blur:** can hallucinate false positives under fast motion.
- **Small / thin objects:** weak (hair strands, fences, antennae).
- **Dense overlapping instances:** crowd scenes degrade significantly.
- Reported video accuracy is meaningfully below image accuracy (cgF1 30.3%
  video vs. 54.1% image on SA-Co benchmark) — propagation through time is
  harder than single-frame segmentation.

For high-quality alpha mattes (hair, soft edges, smoke), feed the SAM 3 mask
into [`matte_mama`](matte_mama.md) or [`matte_ma2`](matte_ma2.md) as a seed.

## Credits

> Nicolas Carion, Laura Gustafson, Yuan-Ting Hu, Shoubhik Debnath, Ronghang
> Hu, Didac Suris, Chaitanya Ryali, Kalyan Vasudev Alwala, Haitham Khedr,
> Andrew Huang, Jie Lei, Tengyu Ma, et al. **SAM 3: Segment Anything with
> Concepts.** Meta AI Research, 2025.
> [Paper](https://arxiv.org/abs/2511.16719) · [Project page](https://ai.meta.com/sam3/) · [Blog](https://ai.meta.com/blog/segment-anything-model-3/) · [GitHub](https://github.com/facebookresearch/sam3) · [Demo](https://segment-anything.com/)

ComfyUI node by [PozzettiAndrea](https://github.com/PozzettiAndrea/ComfyUI-SAM3).

### Citation

{% raw %}
```bibtex
@misc{carion2025sam3segmentconcepts,
  title         = {SAM 3: Segment Anything with Concepts},
  author        = {Nicolas Carion and Laura Gustafson and Yuan-Ting Hu and
                   Shoubhik Debnath and Ronghang Hu and Didac Suris and
                   Chaitanya Ryali and Kalyan Vasudev Alwala and
                   Haitham Khedr and Andrew Huang and Jie Lei and Tengyu Ma and
                   Baishan Guo and Arpit Kalla and Markus Marks and
                   Joseph Greer and others},
  year          = {2025},
  eprint        = {2511.16719},
  archivePrefix = {arXiv},
  primaryClass  = {cs.CV},
  url           = {https://arxiv.org/abs/2511.16719}
}
```
{% endraw %}
