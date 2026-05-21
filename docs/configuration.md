---
title: Configuration & defaults
nav_order: 5
---

# Configuration & defaults (`defaults.json`)

Every plugin reads a `defaults.json` file from its bundle at first
instantiation and uses it to seed the values shown in the host's
parameter panel — the ComfyUI server URL, the shared-folder mount paths
for each OS, caching toggles, timeouts, and so on.

> **The prebuilt bundles ship with the project's own studio defaults
> baked in.** Those won't work on your network — see
> [Customizing for your studio](#customizing-for-your-studio) below.

## How the defaults are organised

AIFX keeps a single source of truth for the studio-wide configuration
and merges it with per-plugin overrides **at build time**, so every
bundle ends up with one complete `defaults.json` and no runtime lookup
is needed.

```
aifx/
├── config/
│   └── defaults-base.json           ← studio-wide: server + controls
├── plugins/
│   ├── depth_da3/
│   │   ├── defaults-project.json    ← per-plugin: project block + overrides
│   │   └── …
│   ├── depth_crafter/
│   │   ├── defaults-project.json
│   │   └── …
│   └── …
└── tools/
    └── merge-defaults.py            ← simple JSON merge invoked from CMake
```

At build time, each plugin's `CMakeLists.txt` runs a POST_BUILD step
that calls `merge-defaults.py BASE PROJECT OUTPUT`, producing the
final `<Plugin>.ofx.bundle/Contents/Resources/config/defaults.json`.

### Merge rules

- Top-level keys in `defaults-project.json` override the corresponding
  keys in `defaults-base.json`.
- When both sides have the same key with object values, they are
  shallow-merged (per-plugin entries override base entries). This lets
  a plugin tweak just `controls.timeout` without restating the entire
  `controls` block.
- Top-level keys present only in `defaults-project.json` (e.g.
  `project`) are added wholesale to the output.

### Where the final file lives

| Location | Path |
|---|---|
| Source — studio config | `config/defaults-base.json` |
| Source — per-plugin overrides | `plugins/<plugin>/defaults-project.json` |
| Built bundle | `<Plugin>.ofx.bundle/Contents/Resources/config/defaults.json` |

The merged file is **only** present in the built bundle; the source
tree never contains a merged version.

## Schema

The merged `defaults.json` produced for every plugin has this shape.
Values shown here are **generic placeholders** — the prebuilt bundles
substitute the project's studio values, and you replace them with your
own when deploying.

```jsonc
{
  "server": {
    "serverAddress":         "comfyui.example.local",
    "serverPort":            8188,
    "macMountPath":          "/Volumes/comfyui-share",
    "winMountPath":          "\\\\HOSTNAME\\share",
    "linuxMountPath":        "/mnt/comfyui-share",
    "macComfyUIInputDir":    "/Volumes/comfyui-share/in",
    "winComfyUIInputDir":    "\\\\HOSTNAME\\share\\in",
    "linuxComfyUIInputDir":  "/mnt/comfyui-share/in",
    "comfyUIInputDir":       "/mnt/comfyui-share/in"
  },
  "controls": {
    "enableProcessing":  false,
    "enableCache":       true,
    "timeout":           600,
    "asyncMode":         1,
    "placeholderMode":   1
  },
  "project": {
    "workflowName":      "<plugin-slug>",
    "workflowFile":      "resources/workflow/<plugin-slug>.json",
    "outputVersion":     "v001"
  }
}
```

### `server` block — paths and the host (DCC) side

Lives in `config/defaults-base.json` (shared across all plugins).

The plugin runs inside the host application (Flame / Nuke / Resolve / …),
which may be on a different machine than the ComfyUI server. Both
sides need to read/write a shared filesystem location and each side
needs to address that location with its own OS-correct path.

- `serverAddress`, `serverPort` — where the plugin sends ComfyUI
  workflow jobs over HTTP.
- `macMountPath`, `winMountPath`, `linuxMountPath` — the path at which
  the shared folder is mounted on the **host** machine. The plugin
  reads the one that matches the OS it's running on.
- `macComfyUIInputDir`, `winComfyUIInputDir`, `linuxComfyUIInputDir` —
  the path at which the **ComfyUI server** sees the same shared
  folder's `in/` directory. The plugin substitutes this path into the
  workflow JSON when it submits a job, so ComfyUI's `LoadEXR` node
  can find what the host just wrote.
- `comfyUIInputDir` — legacy single-path field, kept for
  compatibility.

The Windows path uses **doubled backslashes** in JSON
(`\\\\HOSTNAME\\share`) — each `\` is escaped, so `\\\\` in the file
becomes a literal `\\` on the wire, which is a valid UNC path.

### `controls` block — runtime behaviour

Lives in `config/defaults-base.json`. Any plugin may override individual
fields via its own `defaults-project.json` (e.g. fast-inference
plugins override `timeout` to a shorter value).

- **`enableProcessing`** — master switch. Default `false` so the
  plugin doesn't start firing jobs as soon as the artist drops it on
  a clip. Toggled by the artist via the OFX parameter panel.
- **`enableCache`** — when `true`, the plugin checks the shared
  output folder for an existing EXR matching the workflow hash before
  submitting a new job. Critical for interactive scrubbing.
- **`timeout`** — how long the plugin waits for a single ComfyUI job
  before declaring it failed (seconds).
- **`asyncMode`** — `0` blocks the host thread until the result comes
  back (simple but freezes the UI); `1` queues jobs in a background
  thread and the host stays responsive. Always use `1` in production.
- **`placeholderMode`** — what the plugin returns while a real
  result is still rendering. `0` returns black frames; `1` passes the
  source through unmodified. `1` is the usual choice — the artist
  sees the comp evolve as results land.

### `project` block — workflow identification

Lives in each plugin's `defaults-project.json` (per-plugin only — there
is no `project` block in the base file).

- **`workflowName`** — a tag the plugin uses for the on-disk output
  folder hierarchy (so multiple workflows for the same shot don't
  collide).
- **`workflowFile`** — path inside the bundle's `Resources/` to the
  ComfyUI workflow JSON the plugin submits. Change this to point at
  a custom workflow you authored — see
  [Workflow customization](workflow-customization.md).
- **`outputVersion`** — version suffix appended to the output folder.
  Bump (e.g. `v001` → `v002`) when you want a clean re-render
  alongside prior results.

## Per-plugin override examples

Most plugins ship a minimal `defaults-project.json` with just the
project block — they inherit everything from the base:

```jsonc
// plugins/depth_crafter/defaults-project.json
{
  "project": {
    "workflowName":  "depth_crafter",
    "workflowFile":  "resources/workflow/depth_crafter.json",
    "outputVersion": "v001"
  }
}
```

A plugin that overrides one control field — for example, a fast
per-frame plugin that doesn't need the default 600-second timeout —
adds a partial `controls` block that gets shallow-merged on top of
the base:

```jsonc
// plugins/depth_da3/defaults-project.json
{
  "project": {
    "workflowName":  "depth_da3",
    "workflowFile":  "resources/workflow/depth_da3.json",
    "outputVersion": "v001"
  },
  "controls": {
    "timeout": 300
  }
}
```

The merged `defaults.json` in the bundle ends up with every base
`controls` field plus `timeout: 300` from the override.

## Customizing for your studio

You have three ways to make the plugins point at your own ComfyUI
server and shared filesystem.

### 1. Edit the parameters in the host UI (simplest, per-clip)

Every field in `defaults.json` is also exposed as an OFX parameter in
the plugin's parameter panel. The values from `defaults.json` are
just initial values — the artist can override any of them per clip in
the host UI, and those overrides are saved with the project file.
**This is the right approach if you only need to test on a different
server occasionally** or on a per-shot basis.

### 2. Edit `defaults.json` in the installed bundle (per-machine)

Each installed bundle has its own copy of `defaults.json` (the merged
file produced at build time). You can edit it in place:

```bash
# macOS, per-user install:
~/Library/OFX/Plugins/<Plugin>.ofx.bundle/Contents/Resources/config/defaults.json

# Linux:
~/OFX/Plugins/<Plugin>.ofx.bundle/Resources/config/defaults.json

# Windows:
%LOCALAPPDATA%\OFX\Plugins\<Plugin>.ofx.bundle\Resources\config\defaults.json
```

Edit in any text editor, save, restart the host. The defaults reload
on the next plugin instantiation.

**This is the right approach for a studio TD setting up a single
machine or rolling out a uniform config across many machines.** A
central script that overwrites each installed `defaults.json` is a
common pattern.

### 3. Edit `config/defaults-base.json` in source and rebuild (per-build)

If you maintain a fork of AIFX, edit the single
`config/defaults-base.json` in your fork once and re-run
`tools/build-plugin.sh` (or `tools/release-macos.sh` for the full
suite). The merge step bakes your studio defaults into every bundle
you build, no per-plugin file changes required.

This is the right approach for shops with their own internal release
pipeline.

## Recommended template for a new studio

If you're starting from scratch, replace `config/defaults-base.json`'s
`server` block with values matching your environment. Two common
shapes:

### Single workstation (server runs locally on the host)

```jsonc
{
  "server": {
    "serverAddress":         "127.0.0.1",
    "serverPort":            8188,
    "macMountPath":          "/Users/<you>/comfyui-share",
    "winMountPath":          "C:\\Users\\<you>\\comfyui-share",
    "linuxMountPath":        "/home/<you>/comfyui-share",
    "macComfyUIInputDir":    "/Users/<you>/comfyui-share/in",
    "winComfyUIInputDir":    "C:\\Users\\<you>\\comfyui-share\\in",
    "linuxComfyUIInputDir":  "/home/<you>/comfyui-share/in",
    "comfyUIInputDir":       "/Users/<you>/comfyui-share/in"
  },
  "controls": {
    "enableProcessing": false,
    "enableCache":      true,
    "timeout":          600,
    "asyncMode":        1,
    "placeholderMode":  1
  }
}
```

### Studio with a Linux ComfyUI server and macOS / Linux clients

```jsonc
{
  "server": {
    "serverAddress":         "comfy.studio.local",
    "serverPort":            8188,
    "macMountPath":          "/Volumes/comfy-share",
    "linuxMountPath":        "/mnt/comfy-share",
    "macComfyUIInputDir":    "/Volumes/comfy-share/in",
    "linuxComfyUIInputDir":  "/mnt/comfy-share/in",
    "comfyUIInputDir":       "/mnt/comfy-share/in"
  },
  "controls": {
    "enableProcessing": false,
    "enableCache":      true,
    "timeout":          600,
    "asyncMode":        1,
    "placeholderMode":  1
  }
}
```

(Windows fields can be omitted or left with `\\\\HOSTNAME\\share`
placeholders if no Windows clients exist — the plugin only reads the
field matching the running OS.)

## What NOT to change

- **`controls.asyncMode`** should stay at `1` in production.
  Synchronous mode is only useful for debugging.
- **`project.workflowFile`** should match an actual file under the
  bundle's `Resources/workflow/`. Pointing it at a non-existent path
  breaks the plugin at first invocation. If you want a custom
  workflow, see [Workflow customization](workflow-customization.md).

## See also

- [Installation](installation.md) — getting the plugins onto your
  machine.
- [ComfyUI server setup](comfyui-server-setup.md) — standing up the
  model server that the plugin talks to.
- [Workflow customization](workflow-customization.md) — replacing the
  ComfyUI workflow JSON without changing the plugin.
- [Troubleshooting](troubleshooting.md) — common errors related to
  paths, server connectivity, and timeouts.
