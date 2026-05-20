# AIFX — Public Release Specification

**Status:** Draft — pre-publication
**Owner:** Julien Martin
**Last updated:** 2026-05-07
**Target distribution:** Public GitHub repository (separate from current internal/fork repo), MIT-style open-source release
**License:** BSD-3-Clause (matches OpenFX upstream)

---

## 1. Mission

> We believe the future of visual effects is operator augmentation,
> not replacement. So we've been building integrations of cutting-edge
> AI models into the traditional workflows and DCCs human operators
> already know and love.

Concretely: make state-of-the-art ComfyUI-based AI image and sequence
processing models usable inside any OpenFX-compatible host as first-class
plugins, so VFX artists can work without leaving their compositor, editor,
or color grading tool.

The bridge runs as a thin OFX plugin in the host and a ComfyUI server (local or
networked) that loads the heavy AI models. The plugin marshals frames over a
shared filesystem, submits ComfyUI workflows over HTTP, and returns processed
frames to the host.

## 2. Audience

- **VFX artists** working in any OFX-compatible application (compositors, NLEs,
  color grading tools). Users are assumed to be domain-comfortable with their
  host but not expected to be ML practitioners.
- **Studio TDs** responsible for installing plugins and standing up the ComfyUI
  server side.
- **Plugin developers** extending the suite or adding new ComfyUI workflows.

## 3. Scope

### In scope for V1

Seven OFX plugins that bridge to ComfyUI:

| Plugin | Mode | Model | Function |
|---|---|---|---|
| `depth_da3` | Per-frame | Depth Anything V3 | Monocular depth estimation |
| `normal_crafter` | Sequence | NormalCrafter | Temporally consistent surface normals (diffusion video prior) |
| `depth_crafter` | Sequence | DepthCrafter | Temporally consistent depth (diffusion) |
| `segmentation_sam3` | Sequence | SAM3 | Text/click-prompted mask propagation |
| `matte_mama` | Sequence | SAM3 + VideoMaMa | High-quality alpha matting (diffusion) |
| `matte_ma2` | Sequence | SAM3 + MatAnyone2 | Fast alpha matting (recurrent) |
| `upscale_seedvr2` | Sequence | SeedVR2 | Video super-resolution (DiT) |

### Out of scope for V1

- The `segmentation` plugin (SAM1/2-based) — superseded by `segmentation_sam3`.
- The `anycomfy` generic-workflow plugin — kept internal until a V2 release with
  hardened UX.
- The OFX core C API and Support library — these remain in the upstream OpenFX
  project and are consumed as a dependency, not redistributed.
- Bundled ComfyUI server distribution — users install ComfyUI separately.
- Bundled model weights — users download from upstream sources directly.

## 4. Architecture overview

```
┌──────────────────────────┐         ┌─────────────────────────────┐
│  OFX-compatible host     │         │  ComfyUI server              │
│  (any DCC supporting     │         │  (local or networked,        │
│   the OFX 1.4+ standard) │         │   GPU machine recommended)   │
│                          │         │                              │
│  ┌────────────────────┐  │  HTTP   │  ┌────────────────────────┐  │
│  │ AIFX               │──┼─────────┼──▶ /prompt /history       │  │
│  │ plugins (.ofx)     │  │         │  │ (REST + WebSocket)     │  │
│  └────────────────────┘  │         │  └────────────────────────┘  │
│           │              │         │           │                  │
└───────────┼──────────────┘         └───────────┼──────────────────┘
            │                                    │
            │       Shared filesystem            │
            │       (NFS / SMB / local)          │
            │       ┌──────────────┐             │
            └──────▶│  EXR I/O     │◀────────────┘
                    │  in/  out/   │
                    └──────────────┘
```

**Data flow per render:**
1. Host requests a render of one or more frames.
2. Plugin writes the input frame(s) as EXR to the shared input folder.
3. Plugin POSTs a workflow JSON to the ComfyUI `/prompt` endpoint, with template
   variables (input path, output prefix, frame index, sequence length, etc.) substituted.
4. Plugin polls `/history` until the job completes (or subscribes to the WebSocket).
5. Plugin reads output EXR(s) from the shared output folder and returns pixels
   to the host.

**Per-frame vs sequence dispatch:** A compile-time
`isSequencePlugin()` virtual hook determines whether the plugin submits one
ComfyUI job per frame (frame-based) or batches a contiguous range of frames into
a single job (sequence). Sequence plugins are required for models that need
temporal context (diffusion video models, recurrent matting, mask propagation).

## 5. License and funding

**Code license:** BSD-3-Clause, matching OpenFX upstream. SPDX headers on every
source file:
```
SPDX-License-Identifier: BSD-3-Clause
```

**Model weights — important license reality check:**

The plugin code is BSD-3-Clause and freely redistributable. **The model weights
that ComfyUI loads are not.** Several of the upstream models the plugins depend
on are licensed for non-commercial / research use only. As of research conducted
2026-05-07:

| Plugin | Code license | Weights license | Commercial use? |
|---|---|---|---|
| `depth_da3` | Apache 2.0 | Split: Apache 2.0 (BASE / SMALL / METRIC-LARGE / MONO-LARGE) **or** CC BY-NC 4.0 (LARGE / GIANT / Nested) | ✅ Yes — with the Apache-licensed variants only |
| `normal_crafter` | MIT (code), Apache 2.0 (weights) | Inherits Stability AI Non-Commercial Community License via SVD-XT base | ❌ No (SVD base) |
| `depth_crafter` | Apache 2.0 (code) | Tencent non-commercial **+** Stability non-commercial (SVD-XT base) | ❌ No |
| `segmentation_sam3` | SAM License (custom) | SAM License (commercial OK with attribution; prohibits military / weapons / some surveillance use) | ✅ With conditions |
| `matte_mama` | CC BY-NC 4.0 | CC BY-NC 4.0 **+** Stability non-commercial (SVD-XT base) | ❌ No |
| `matte_ma2` | NTU S-Lab License 1.0 | NTU S-Lab License 1.0 (non-commercial) | ❌ No |
| `upscale_seedvr2` | Apache 2.0 | Apache 2.0 (3B and 7B variants) | ✅ Yes |

**Implication:** VFX artists using these plugins on paid production work must
verify each model's terms and may need to engage upstream model owners for
commercial agreements. This must be surfaced loudly in:
- The site landing page.
- Each per-plugin page (a colored "Commercial use" callout at the top).
- The README quick start.

The project ships **no weights**. Users download from upstream HuggingFace /
GitHub release pages directly. This separates plugin redistribution (BSD) from
model redistribution (upstream's terms).

**Attribution policy:** Every plugin's documentation page must credit the
upstream model's authors and link to their official sources (paper, project
page, GitHub). See per-plugin pages under `docs/plugins/` for citations.

**Authors & funding acknowledgement:**

> AIFX was developed by **Julien Martin** ([MaGMa](https://www.linkedin.com/company/ma-g-ma/))
> for **[Reepost Studio](https://www.reepoststudio.fr/)**, with funding from
> **CNC (Centre national du cinéma et de l'image animée)**.
>
> _(Funding wording will be refined once the final program name and any
> required attribution form from CNC are available.)_

## 6. Repository layout (for the new repo)

```
AIFX/
├── README.md                       ← landing page, quick start
├── LICENSE                         ← BSD-3-Clause
├── CITATION.cff                    ← machine-readable citation metadata
├── CHANGELOG.md
├── CONTRIBUTING.md
├── CODE_OF_CONDUCT.md
├── _config.yml                     ← Jekyll config for GitHub Pages
├── plugins/                        ← OFX plugin sources
│   ├── common/                     ← shared infrastructure
│   ├── depth_da3/
│   ├── depth_crafter/
│   ├── normal_crafter/
│   ├── segmentation_sam3/
│   ├── matte_mama/
│   ├── matte_ma2/
│   ├── upscale_seedvr2/
│   └── CMakeLists.txt
├── tools/                          ← build helpers (build-plugin.sh etc.)
├── tests/                          ← integration tests (see §10)
├── docs/                           ← user docs + GitHub Pages site
│   ├── index.md
│   ├── installation.md
│   ├── comfyui-server-setup.md
│   ├── workflow-customization.md
│   ├── troubleshooting.md
│   ├── architecture.md
│   ├── plugins/
│   │   ├── depth_da3.md
│   │   ├── ... (one per plugin)
│   │   └── upscale_seedvr2.md
│   ├── assets/
│   │   └── plugins/<plugin>/...    ← screenshots, example imagery
│   └── _layouts/, _includes/       ← Jekyll theme overrides
└── .github/
    ├── workflows/
    │   ├── build.yml               ← CMake matrix per OS
    │   └── pages.yml               ← Jekyll build + deploy
    └── ISSUE_TEMPLATE/
```

**Dependencies on OpenFX upstream:** the OFX C API headers and Support library
are consumed via a CMake `FetchContent` or git submodule pointing at
[AcademySoftwareFoundation/openfx](https://github.com/AcademySoftwareFoundation/openfx).
Decision deferred — see §11.

## 7. Documentation plan

### Top-level pages

- **`README.md`** — what this is, why it exists, quick start (install plugin →
  start ComfyUI → use in host). 1-screen scannable.
- **`docs/index.md`** — site landing page with plugin gallery cards.
- **`docs/installation.md`** — per-OS plugin install (macOS, Linux, Windows),
  bundle paths, verification steps. Host-agnostic; plugins are valid `.ofx`
  bundles per the OFX standard, so any compliant host loads them.
- **`docs/comfyui-server-setup.md`** — end-to-end ComfyUI install, custom node
  list per plugin, model download URLs, shared folder configuration (local and
  networked), security notes. The single highest-value document.
- **`docs/workflow-customization.md`** — how the workflow JSON template
  variables work (`${INPUT_PATH}`, `${OUTPUT_PREFIX}`, `${FRAME}`,
  `${IMAGE_LOAD_CAP}`), how to swap in alternate workflows.
- **`docs/troubleshooting.md`** — common errors organized by symptom. Includes
  a **Host quirks** appendix listing tested hosts and any host-specific
  caveats (Y-flip handling, sandbox restrictions, etc.) without making the main
  docs host-segmented.
- **`docs/architecture.md`** — for advanced users and contributors:
  per-frame vs sequence dispatch, the `isSequencePlugin()` hook, EXR I/O,
  threading model.

### Per-plugin pages

One Markdown file per plugin under `docs/plugins/`, each containing:

1. One-paragraph functional summary.
2. **What you give it** (inputs, prompts, parameters).
3. **What you get back** (outputs, expected channel layout).
4. **Requirements** — VRAM, ComfyUI custom nodes, model weights with download
   URLs and sizes.
5. **Parameters reference** — every parameter exposed in the host UI, with
   meaning, default, range, and tips.
6. **Example imagery** — before/after, model behavior, with attribution.
7. **Known limitations**.
8. **Upstream credits and citation**.

The structure is identical across all 7 plugins for predictability.

## 8. GitHub Pages plan

### Theme

**Jekyll with `just-the-docs` theme.** Searchable, sectioned, technical-doc
oriented. Deployed via `_config.yml` at the repo root, sourced from `docs/`.

### Site structure

```
/                     ← landing (hero + plugin cards + quick start)
/docs/installation/
/docs/comfyui-server-setup/
/docs/workflow-customization/
/docs/architecture/
/docs/troubleshooting/
/docs/plugins/<plugin>/    ← one page per plugin
```

### Hero / landing page

A short hero block, three-column "what does it do" feature grid, then plugin
cards (one per plugin) linking to the plugin pages. Footer with funding
acknowledgement and BSD-3-Clause notice.

### Build and deploy

GitHub Actions workflow `pages.yml`: on push to `main`, build Jekyll, deploy to
`gh-pages`. No manual steps.

### Asset pipeline

Plugin imagery is sourced from upstream project pages, papers, and GitHub
repositories under fair-use / fair-dealing for documentation purposes, with
full attribution recorded centrally in `docs/assets/credits.md` and linked
from the site footer. Each plugin page's "Demos & comparisons" section
prominently links to upstream sources; embedded images can be added by either
hot-linking or mirroring into `docs/assets/plugins/<plugin>/`, always paired
with a caption containing source / authors / year / license / paper citation.

Self-generated imagery (parameter-panel screenshots, before/after frames from
CC0 or owned plates) is preferred where available but not required for V1.
The credits page invites upstream authors to request alternative attribution
or removal via GitHub issue.

## 9. Build and distribution plan

### Source build

The same CMake + Conan flow used in the current OpenFX fork. The `build-plugin.sh`
helper script is included in `tools/`.

### Binary distribution

**V1: source-only.** Users build from source on each platform.

**V1.1 (post-release):** GitHub Releases with prebuilt `.ofx.bundle` artifacts
for macOS (universal), Linux (x86_64), Windows (x86_64). Built by CI from a
tagged commit.

### CI

- Build matrix: macOS arm64+x86_64, Linux x86_64, Windows x86_64.
- Unit / integration tests where they exist (see §10).
- Pages deploy on push to `main`.

## 10. Testing plan

**Status: blocked.** Testing is not yet complete on real DCC hosts; this is
why the public repo is not yet created.

### Test matrix to complete before public release

| Plugin | Local server | Networked server | Tested in host |
|---|---|---|---|
| depth_da3 | ☐ | ☐ | ☐ |
| normal_crafter | ☐ | ☐ | ☐ |
| depth_crafter | ☐ | ☐ | ☐ |
| segmentation_sam3 | ☐ | ☐ | ☐ |
| matte_mama | ☐ | ☐ | ☐ |
| matte_ma2 | ☐ | ☐ | ☐ |
| upscale_seedvr2 | ☐ | ☐ | ☐ |

Tested-in-host check is per-host; document each host the team verifies in
the troubleshooting "host quirks" appendix.

### Specific test cases per plugin

- Cold start (no cached frames).
- Repeated render of same frame (cache hit verification).
- Mid-clip frame edits (cache invalidation).
- Sequence plugins: clip shorter than load cap, longer than load cap, exactly equal.
- Path mounting: identical and divergent client/server paths.
- Pixel correctness: golden-frame regression for at least one frame per plugin.

## 11. Open decisions

| # | Decision | Notes |
|---|---|---|
| D1 | Suite name | ✅ Resolved: `AIFX` for display/branding; `aifx` for URLs and repo path (lowercase per GitHub convention). |
| D2 | Repo location | ✅ Resolved: `github.com/Dev-Reepost/aifx`; Pages at `Dev-Reepost.github.io/aifx/`. |
| D3 | OpenFX dependency | ✅ Resolved: CMake `FetchContent`, pinned to a specific OpenFX tag. Contributors clone the AIFX repo, run `cmake`, and the OpenFX C API + Support library are fetched into `build/_deps/openfx-src/` automatically. Upgrades are a one-line `GIT_TAG` bump. |
| D4 | CNC funding wording | Final approved program name and required attribution form. |
| D5 | Image sourcing | ✅ Strategy resolved: fair-use reproduction with central attribution in `docs/assets/credits.md`. Per-plugin direct image URLs still to embed in the "Demos & comparisons" sections. |
| D6 | Citation format | ✅ Resolved: `CITATION.cff` v1.2.0 at the repo root + per-plugin BibTeX in the docs. |
| D7 | Issue templates | ✅ Resolved: bug + feature templates in `.github/ISSUE_TEMPLATE/`. |
| D8 | Code of Conduct | Contributor Covenant v2.1 unless the team has a preferred alternative. |
| D9 | Repo positioning vs. existing openfx fork | ✅ Resolved: **Option A** — `Dev-Reepost/aifx` is a new repo, materialized from this staging tree with a fresh root commit. The existing `Dev-Reepost/openfx` fork is archived once aifx is verified live, with a README banner redirecting users to aifx. OpenFX upstream is consumed via FetchContent (D3) rather than carried in tree. The fork stays browsable for historical context and any future patches contributed back to `AcademySoftwareFoundation/openfx`. |

## 12. Migration playbook

When testing is complete and decisions D1–D8 are settled, the new repo is
created via:

1. `mkdir <new-repo> && cd <new-repo> && git init`.
2. Copy `contrib/release-staging/*` into the root (rename to match repo layout).
3. Copy plugin sources (`contrib/plugins/ComfyUI/{plugin}` → `plugins/{plugin}`).
4. Copy shared common code (`contrib/plugins/ComfyUI/common/` → `plugins/common/`).
5. Copy build helpers (`contrib/dev-tools/build-plugin.sh` → `tools/`).
6. Adjust `CMakeLists.txt` paths.
7. Replace `AIFX` placeholders globally.
8. Insert final CNC wording and any other decision-blocked content.
9. `git add . && git commit -m "Initial public release"`.
10. `git remote add origin <new repo URL> && git push origin main`.
11. Enable GitHub Pages on `gh-pages` branch (or `/docs` on main).
12. Tag `v1.0.0` and publish a release.

History is intentionally not preserved — the new repo starts at a clean root
commit.

## 13. Non-goals

- Compatibility with non-OFX plugin standards (AVX, OpenColorIO, etc.).
- Distributing the ComfyUI server itself or model weights.
- Providing a hosted/SaaS ComfyUI service.
- Supporting hosts that violate the OFX 1.4+ standard.

---

_End of spec._
