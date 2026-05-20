---
title: Plugins
nav_order: 5
has_children: true
---

# Plugin reference

Each plugin in this suite bridges an OpenFX host to a specific upstream model
running inside ComfyUI. Pick a plugin from the sidebar, or use the table below
as a quick selector.

## At a glance

| Plugin | Mode | Function | Commercial-safe? |
|---|---|---|---|
| [Depth Anything V3](depth_da3.md) | Per-frame | Monocular depth | ✅ Yes (with Apache variants) |
| [NormalCrafter](normal_crafter.md) | Per-frame | Surface normals | ❌ No (SVD base) |
| [DepthCrafter](depth_crafter.md) | Sequence | Temporally-consistent depth | ❌ No |
| [SAM3 Segmentation](segmentation_sam3.md) | Sequence | Text/click-prompted mask propagation | ✅ With conditions |
| [MaMa Matting](matte_mama.md) | Sequence | Diffusion-based alpha matting | ❌ No |
| [MatAnyone2 Matting](matte_ma2.md) | Sequence | Recurrent alpha matting | ❌ No |
| [SeedVR2 Upscaler](upscale_seedvr2.md) | Sequence | Generative video super-resolution | ✅ Yes |

"Commercial-safe" means the upstream model's terms permit use on paid
production work. See each plugin page for the exact license breakdown.

## Per-frame vs sequence

- **Per-frame** plugins process each frame independently. Fast first frame,
  no temporal context.
- **Sequence** plugins process a window of contiguous frames jointly. Better
  temporal stability, larger up-front compute per submission. Controlled by
  the `Image Load Cap` parameter.

See [Architecture](../architecture.md#per-frame-vs-sequence-dispatch) for the
underlying dispatch design.

## Common workflow

Most VFX use cases combine plugins:

- **Roto + key:** [SAM3](segmentation_sam3.md) → [MatAnyone2](matte_ma2.md)
  (fast) or [MaMa](matte_mama.md) (best quality).
- **Depth comp:** [Depth Anything V3](depth_da3.md) for per-frame work, or
  [DepthCrafter](depth_crafter.md) when temporal stability matters.
- **Plate restoration:** [SeedVR2](upscale_seedvr2.md) for uprez, optionally
  followed by depth or normals for relighting.
