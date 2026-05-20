---
title: Architecture
nav_order: 6
---

# Architecture

This page is for users who want to understand how the plugins work, and for
developers contributing new plugins to the suite.

## Overview

```
┌──────────────────────────────────┐    ┌──────────────────────────────────────┐
│  OFX-compatible host             │    │  ComfyUI server                       │
│                                  │    │                                       │
│  ┌────────────────────────────┐  │    │  ┌────────────────────────────────┐  │
│  │ Plugin (.ofx bundle)       │  │    │  │ Custom nodes (per plugin)      │  │
│  │  - Parameter UI            │  │    │  │  - LoadEXR / SaveEXR           │  │
│  │  - Frame queue             │  │    │  │  - Model node(s)               │  │
│  │  - Async job manager       │──┼────┼──▶  - Workflow execution           │  │
│  │  - EXR I/O                 │  │HTTP│  │                                │  │
│  └────────────────────────────┘  │ &  │  └────────────────────────────────┘  │
│            │   ▲                 │ WS │             │   ▲                    │
└────────────┼───┼─────────────────┘    └─────────────┼───┼────────────────────┘
             ▼   │                                    ▼   │
             ┌───────────────────────────────────────────┐
             │  Shared filesystem (NFS / SMB / local)     │
             │   - input EXRs (host writes, server reads) │
             │   - output EXRs (server writes, host reads)│
             └───────────────────────────────────────────┘
```

## Components

### Plugin runtime (host side)

Each plugin is an OFX `.ofx.bundle` that the host loads via the standard OFX
plugin discovery. The plugin code is built on the OpenFX C++ Support library
(see [openeffects.org](https://openeffects.org/) for the standard, and
[openfx on GitHub](https://github.com/AcademySoftwareFoundation/openfx) for
the reference implementation), which provides the `ImageEffect` base class
and parameter primitives.

All plugins inherit from a shared `ComfyUIBasePlugin` class that handles:

- Parameter creation (server URL, mount paths, common parameters).
- The render dispatch (per-frame or sequence).
- EXR I/O via TinyEXR.
- Async job submission to ComfyUI and result polling.
- Frame caching to avoid re-running already-completed jobs.
- Template variable substitution into the workflow JSON.

Per-plugin classes implement only the model-specific bits: parameter declaration,
`buildWorkflow(...)` (which produces the workflow JSON for one frame or one
sequence), and `getRequiredModels()`.

### ComfyUI server side

ComfyUI is a Python application with a node-based workflow editor. Each plugin
ships a workflow JSON template under its `resources/workflow/` directory. The
template references:

- A `LoadEXR` node, which reads the input frames the plugin wrote.
- One or more model-specific nodes (e.g. `DepthCrafter`, `SAM3VideoSegmentation`).
- A `SaveEXR` node, which writes the output frames to the shared output folder.

When the plugin submits a job, it substitutes template variables for the
specific input path, output prefix, frame index, and sequence length, then
POSTs the JSON to ComfyUI's `/prompt` endpoint.

### Shared filesystem

The plugin and the server exchange image data via EXR files on a filesystem
that both can access. The plugin's `Client Mount Path` and `Server Mount Path`
parameters tell the plugin how to translate between the path the host sees
and the path ComfyUI sees. The plugin substitutes the server path into the
workflow JSON; the host always uses the client path itself.

Both sides need read and write access. The plugin's working directory layout
is:

```
<mount>/<project>/<workflow>/<frame-or-sequence-id>.exr
```

This isolates work from different projects and different plugins, and lets the
plugin discover existing outputs (cache) on subsequent renders.

## Per-frame vs sequence dispatch

Some upstream models reason about a single frame at a time (Depth Anything V3,
NormalCrafter). Others need a window of contiguous frames for temporal
consistency (DepthCrafter, MaMa, MatAnyone2, SAM3 propagation, SeedVR2). The
plugin architecture makes the choice explicit at compile time:

```cpp
class MyPlugin : public ComfyUIBasePlugin {
  // Per-frame plugins inherit the default:
  //   bool isSequencePlugin() const override { return false; }

  // Sequence plugins override to return true:
  //   bool isSequencePlugin() const override { return true; }
  //   int  getImageLoadCap()  const override;
};
```

When `isSequencePlugin()` returns `false`, each frame the host requests becomes
a separate ComfyUI job. The plugin queues jobs as the host renders frames in
its preview / scrubbing pattern, so the user does not wait for the whole clip.

When `isSequencePlugin()` returns `true`, the plugin instead submits one
ComfyUI job for a contiguous block of frames. The block size is governed by
`getImageLoadCap()`, which defaults to a per-plugin parameter (the artist can
tune it to match VRAM). The workflow JSON's `LoadEXR.image_load_cap` template
variable receives this value.

## Async job manager

Frame submissions are non-blocking from the host's perspective. The plugin
maintains a background job queue and a thread that polls ComfyUI's `/history`
endpoint (with optional WebSocket subscription for low-latency status). Job
states are `QUEUED → PROCESSING → COMPLETED` or `→ FAILED`, with thread-safe
state transitions.

The host's `render()` call blocks waiting for the specific frame it requested,
but other frames the plugin has already queued continue to make progress in the
background. Result: a clip scrub feels responsive, and the cache fills as you
work.

## Cache

Once a frame is rendered, its output EXR sits in the shared output folder.
On subsequent renders of the same frame with the same parameters, the plugin
detects the existing output and returns it without contacting ComfyUI. A
parameter change invalidates the cache by changing the workflow hash that
the output filename is derived from.

To force a re-render, change a parameter (or use the host's per-clip cache
clear). The plugin does not garbage-collect old outputs; that is left to the
user / studio process.

## Adding a new plugin

The shortest path to a new plugin in this suite:

1. Pick a ComfyUI custom node that wraps the model you want.
2. Author a workflow JSON in ComfyUI that runs the model end to end, with
   `LoadEXR` for input and `SaveEXR` for output.
3. Create a new plugin directory under `plugins/<name>/` with:
   - `<name>_plugin.h` and `<name>_plugin.cpp` inheriting `ComfyUIBasePlugin`.
   - `resources/workflow/<name>.json` (the exported workflow with template
     variables substituted in).
   - `resources/config/defaults.json` (default parameter values).
   - A `CMakeLists.txt` that links against `ComfyUICommon`.
4. Implement `buildWorkflow()` to produce the workflow JSON for one
   frame / sequence, with template variables filled in.
5. Implement `getRequiredModels()` returning the list of model paths the
   plugin needs.
6. Decide per-frame vs sequence and override `isSequencePlugin()` accordingly.
7. Register the new plugin in the top-level `plugins/CMakeLists.txt` via
   `add_subdirectory(<name>)`.

The shared infrastructure handles everything else.
