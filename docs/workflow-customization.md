---
title: Workflow customization
nav_order: 4
---

# Workflow customization

Each plugin ships a default ComfyUI workflow JSON under
`resources/workflow/<plugin>.json`. You can replace it with a custom workflow
to swap models, add pre/post-processing, or chain effects, as long as the
workflow respects the contract the plugin expects.

This page documents that contract.

## What the plugin sends

When the plugin submits a job, it loads the workflow JSON from disk and
substitutes a fixed set of template variables before POSTing to ComfyUI. The
plugin does **not** otherwise modify the JSON — every node and edge in your
custom workflow is preserved.

## Template variables

| Variable | Replaced with | When |
|---|---|---|
| `${INPUT_PATH}` | The full path to the input EXR file on the **server** side. | Every render. |
| `${OUTPUT_PREFIX}` | The full path prefix the output EXR(s) should be written to on the server side. | Every render. |
| `${FRAME}` | The numeric frame index for per-frame plugins. | Per-frame plugins only. |
| `${IMAGE_LOAD_CAP}` | The number of frames to load in this sequence pass. | Sequence plugins only. |
| `${PROJECT_NAME}` | The user-facing project name (set in the plugin parameters). | Every render. |
| `${WORKFLOW_NAME}` | The workflow identifier (defaults to the plugin name). | Every render. |

Strings are substituted literally. For server paths on Windows ComfyUI hosts
(UNC paths), the plugin handles backslash escaping for valid JSON automatically.

Variables that are not present in the workflow are silently ignored. Variables
referenced in the workflow but unknown to the plugin remain in the JSON
literally, which will likely cause ComfyUI to error.

## Required nodes

A workflow must contain at least:

1. **A `LoadEXR` node** (or equivalent) reading from `${INPUT_PATH}`. For
   sequence plugins this node also receives `${IMAGE_LOAD_CAP}` as the
   `image_load_cap` field.
2. **A `SaveEXR` node** (or equivalent) writing to `${OUTPUT_PREFIX}`. The
   prefix already includes the project / workflow / frame hierarchy; do not
   prepend or modify it.

Anything in between is your business: you can insert color management nodes,
pre-processing, additional models, or post-processing.

## Replacing the default workflow

1. In ComfyUI, build and test your custom workflow until it runs end to end
   with manually-supplied input and output paths.
2. Export the workflow (`Workflow → Export → API Format JSON`).
3. In the exported JSON, replace your test input path with `${INPUT_PATH}`,
   your test output prefix with `${OUTPUT_PREFIX}`, and any other applicable
   template variables.
4. Save the result to `resources/workflow/<plugin>.json` in the plugin bundle,
   replacing the default.
5. Restart your host application (so the bundle is reloaded).

Alternatively, some plugins expose a **Workflow Path** parameter that lets you
point at an external workflow JSON without modifying the bundle.

## Worked example: replacing the upscaler model

Suppose you want to use a different upscaler than `upscale_seedvr2` ships with,
e.g. an ESRGAN variant for fast comp work where temporal consistency does not
matter.

1. Open ComfyUI and create a workflow with: `LoadEXR` → `ESRGAN Upscale` →
   `SaveEXR`.
2. Test it manually. Confirm the EXR output looks right.
3. Export to API format. Open the JSON in a text editor.
4. Find the `LoadEXR` node's `filepath` value. Replace it with `${INPUT_PATH}`.
5. Find the `LoadEXR` node's `image_load_cap` value. Replace it with
   `${IMAGE_LOAD_CAP}` if you want sequence behavior, or set it to `1` if you
   want per-frame.
6. Find the `SaveEXR` node's `filename_prefix` value. Replace it with
   `${OUTPUT_PREFIX}`.
7. Save as `~/my-custom-upscaler.json`.
8. In the plugin's `Workflow Path` parameter (if exposed), point at this file.
9. Render.

## Limitations

- The plugin does not validate the workflow before submission. A broken
  workflow surfaces as a ComfyUI error in the plugin's status panel.
- The plugin does not introspect node parameters; you cannot expose a custom
  node's parameter as a host-side OFX parameter without modifying the plugin
  source. The recommended pattern for that is to add a new plugin instead.
- Some custom nodes have side effects (downloading weights, writing logs, etc.)
  the plugin does not see. That is by design — the plugin treats ComfyUI as a
  black box.

For deeper integrations, see [Architecture](architecture.md) and consider
authoring a new plugin.
