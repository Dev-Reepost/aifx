---
title: Configuration & defaults
nav_order: 5
---

# Configuration & defaults (`defaults.json`)

Every plugin reads a `defaults.json` file from its bundle at first
instantiation and uses it to seed the values shown in the host's parameter
panel — the ComfyUI server URL, the shared-folder mount paths for each OS,
caching toggles, timeouts, and so on.

> **The prebuilt v0.1.0 macOS bundles ship with the Reepost studio's
> defaults baked in** (server `192.168.1.211:8388`, NFS paths under
> `/Volumes/silo2/002_COMFYUI`). These won't work on any other network.
> See [Customizing for your studio](#customizing-for-your-studio) below.

## Where it lives

| Location | Path |
|---|---|
| In source | `plugins/<plugin>/resources/config/defaults.json` |
| In a built bundle | `<Plugin>.ofx.bundle/Contents/Resources/config/defaults.json` |

The file is identical at both locations — the bundle copy is produced from
the source copy at build time. Each of the seven plugins has its own
`defaults.json`, but the `server` block is intentionally identical across
all of them.

## Schema

```jsonc
{
  "server": {
    "serverAddress":         "192.168.1.211",     // ComfyUI server IP or hostname
    "serverPort":            8388,                // ComfyUI port
    "macMountPath":          "/Volumes/silo2/002_COMFYUI",
    "winMountPath":          "\\\\192.168.1.110\\silo2\\002_COMFYUI",
    "linuxMountPath":        "/mnt/silo2/002_COMFYUI",
    "macComfyUIInputDir":    "/Volumes/silo2/002_COMFYUI/in",
    "winComfyUIInputDir":    "\\\\192.168.1.110\\silo2\\002_COMFYUI\\in",
    "linuxComfyUIInputDir":  "/mnt/silo2/002_COMFYUI/in",
    "comfyUIInputDir":       "/Volumes/silo2/002_COMFYUI/in"
  },
  "controls": {
    "enableProcessing":  false,    // Master on/off — start disabled so a clip
                                   // doesn't fire jobs the moment the plugin
                                   // is dropped on it.
    "enableCache":       true,     // Reuse rendered EXRs when params unchanged
    "timeout":           600,      // Per-job timeout (seconds)
    "asyncMode":         1,        // 0=blocking, 1=async background queue
    "placeholderMode":   1         // 0=black, 1=passthrough source until result
  },
  "project": {
    "workflowName":      "depth_crafter",
    "workflowFile":      "resources/workflow/depth_crafter.json",
    "outputVersion":     "v001"
  }
}
```

### `server` block — paths and the host (DCC) side

The plugin runs inside the host application (Flame / Nuke / Resolve / …),
which may be on a different machine than the ComfyUI server. To exchange
images, both sides need to read/write a shared filesystem location, **and
each side needs to address that location with its own OS-correct path**.

- `serverAddress`, `serverPort` — where the plugin sends ComfyUI workflow
  jobs over HTTP.
- `macMountPath`, `winMountPath`, `linuxMountPath` — the path at which
  the shared folder is mounted on the **host** machine. The plugin reads
  the one that matches the OS it's running on.
- `macComfyUIInputDir`, `winComfyUIInputDir`, `linuxComfyUIInputDir` — the
  path at which the **ComfyUI server** sees the same shared folder's
  `in/` directory. The plugin substitutes this path into the workflow
  JSON when it submits a job, so ComfyUI's `LoadEXR` node can find what
  the host just wrote.
- `comfyUIInputDir` — legacy single-path field, kept for compatibility.

Note the Windows path uses **doubled backslashes** in JSON
(`\\\\192.168.1.110\\silo2`) — each `\` is escaped, so `\\\\` in the file
becomes a literal `\\` on the wire, which is a valid UNC path.

### `controls` block — runtime behavior

- **`enableProcessing`** — master switch. Set to `false` by default so the
  plugin doesn't start firing jobs as soon as the artist drops it on a
  clip. Toggled by the artist via the OFX parameter panel.
- **`enableCache`** — when `true`, the plugin checks the shared output
  folder for an existing EXR matching the workflow hash before submitting
  a new job. Critical for interactive scrubbing.
- **`timeout`** — how long the plugin waits for a single ComfyUI job
  before declaring it failed (seconds). 600 = 10 min, comfortable for
  diffusion-heavy plugins; 300 = 5 min, fine for fast plugins like SAM3.
- **`asyncMode`** — `0` blocks the host thread until the result comes
  back (simple but freezes the UI); `1` queues jobs in a background
  thread and the host stays responsive. Always use `1` in production.
- **`placeholderMode`** — what the plugin returns while a real result is
  still rendering. `0` returns black frames; `1` passes the source
  through unmodified. `1` is the usual choice — the artist sees the
  comp evolve as results land.

### `project` block — workflow identification

- **`workflowName`** — a tag the plugin uses for the on-disk output
  folder hierarchy (so multiple workflows for the same shot don't
  collide).
- **`workflowFile`** — relative path inside the bundle's `Resources/`
  to the ComfyUI workflow JSON the plugin submits. Change this to point
  at a custom workflow you authored — see
  [Workflow customization](workflow-customization.md).
- **`outputVersion`** — version suffix appended to the output folder.
  Bump (e.g. `v001` → `v002`) when you want a clean re-render alongside
  prior results.

## Customizing for your studio

You have three ways to make the plugins point at your own ComfyUI server
and shared filesystem.

### 1. Edit the parameters in the host UI (simplest, per-clip)

Every field in `defaults.json` is also exposed as an OFX parameter in the
plugin's parameter panel. The values in `defaults.json` are just initial
values — the artist can override any of them per clip in the host UI, and
those overrides are saved with the project file. **This is the right
approach if you only need to test on a different server occasionally** or
on a per-shot basis.

### 2. Edit `defaults.json` in the installed bundle (per-machine)

Each installed bundle has its own copy of `defaults.json`. You can edit
the copy inside the bundle directly:

```bash
# macOS, per-user install:
~/Library/OFX/Plugins/<Plugin>.ofx.bundle/Contents/Resources/config/defaults.json

# Linux:
~/OFX/Plugins/<Plugin>.ofx.bundle/Contents/Resources/config/defaults.json

# Windows:
%LOCALAPPDATA%\OFX\Plugins\<Plugin>.ofx.bundle\Contents\Resources\config\defaults.json
```

Edit in any text editor, save, restart the host. The defaults reload on
the next plugin instantiation.

**This is the right approach for a studio TD setting up a single machine
or rolling out a uniform config across many machines.** A central script
that overwrites each `Contents/Resources/config/defaults.json` after
install is a common pattern.

### 3. Edit `defaults.json` in source and rebuild (per-build)

If you maintain a fork of AIFX, edit each
`plugins/<plugin>/resources/config/defaults.json` in your fork and
re-run `tools/build-plugin.sh` (or `tools/release-macos.sh` for the full
suite). The studio defaults are now baked into every bundle you build.

This is the right approach for shops with their own internal release
pipeline.

## Recommended template for a new studio

If you're starting from scratch, replace each `defaults.json`'s `server`
block with values matching your environment. Examples:

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
  }
}
```

(Windows fields can stay populated with `\\\\…` placeholders even if no
Windows clients exist — the plugin only reads the field matching the
running OS.)

## What NOT to change in `defaults.json`

- **`controls.asyncMode`** should stay at `1` in production. Synchronous
  mode is only useful for debugging.
- **`project.workflowFile`** should match an actual file under the
  bundle's `Resources/workflow/`. Pointing it at a non-existent path
  breaks the plugin at first invocation. If you want a custom workflow,
  see [Workflow customization](workflow-customization.md).

## See also

- [Installation](installation.md) — getting the plugins onto your machine.
- [ComfyUI server setup](comfyui-server-setup.md) — standing up the model
  server that the plugin talks to.
- [Workflow customization](workflow-customization.md) — replacing the
  ComfyUI workflow JSON without changing the plugin.
- [Troubleshooting](troubleshooting.md) — common errors related to
  paths, server connectivity, and timeouts.
