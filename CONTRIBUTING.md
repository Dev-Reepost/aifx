# Contributing to AIFX

Thanks for considering a contribution. This document covers what to expect
when filing issues, opening pull requests, and adding new plugins.

> Two contact channels:
>
> - **[GitHub Issues](https://github.com/Dev-Reepost/aifx/issues)** —
>   default channel for bug reports, feature requests, and questions.
> - **[contact@reepost.fr](mailto:contact@reepost.fr)** — for non-public
>   reports (security, NDA-covered footage), partnership / commercial
>   integration enquiries, or if you'd rather email than open an issue.

## Ways to contribute

- **File a bug.** Use the bug-report issue template. Include:
  - Your OS and host application + version.
  - The plugin and ComfyUI custom node versions.
  - Steps to reproduce, ideally with a small clip.
  - Console output from both the host (if any) and ComfyUI server.
- **Improve the docs.** PRs against the `docs/` tree are very welcome.
- **Add or improve a plugin.** See [docs/architecture.md](docs/architecture.md)
  for the shared infrastructure.
- **Test on a new host.** If you verified the plugins on a host not listed in
  the troubleshooting page, send a PR adding it (with any caveats you found).

## Pull request process

1. Fork the repo and create a feature branch off `main`.
2. Make your changes. Keep commits focused and the messages descriptive.
3. If you changed plugin behavior, update the relevant `docs/plugins/*.md`
   page.
4. If you added a parameter, update both the code and the plugin docs.
5. Open a PR against `main`. Describe what changed, why, and how you tested.

### Commit message style

Project-style Conventional Commits:

```
feat(<plugin>): short subject

Optional longer body explaining the why.
```

Common types: `feat`, `fix`, `docs`, `chore`, `refactor`, `test`.

## Adding a new plugin

The shortest path:

1. Pick a ComfyUI custom node that wraps the model you want.
2. Author and test a workflow JSON in ComfyUI end-to-end (LoadEXR → model →
   SaveEXR).
3. Create a new directory under `plugins/<name>/` containing:
   - `<name>_plugin.h` + `<name>_plugin.cpp` (inheriting `ComfyUIBasePlugin`).
   - `resources/workflow/<name>.json` (with template variables).
   - `resources/config/defaults.json`.
   - `CMakeLists.txt` linking against `ComfyUICommon`.
4. Register the new plugin in `plugins/CMakeLists.txt` via `add_subdirectory`.
5. Add `docs/plugins/<name>.md` following the structure of the existing
   pages (Summary / Inputs / Outputs / Commercial use / Requirements /
   Parameters / Limitations / Credits / Citation).
6. Open a PR.

For the per-frame vs sequence design and the `isSequencePlugin()` hook, see
[docs/architecture.md](docs/architecture.md).

## Code style

- Follow the existing style in the codebase. We don't enforce a specific
  formatter; readability matters more than prescriptive style.
- C++17. Avoid C++20 features unless necessary, for compatibility with older
  host build environments.
- License header on every new source file:

  ```
  // SPDX-License-Identifier: BSD-3-Clause
  // Copyright 2026 OpenFX and contributors to the OpenFX project.
  ```

## License

By contributing you agree that your contributions are licensed under the
BSD-3-Clause license that covers this project.

## Acknowledgements

This work was supported by **CNC (Centre national du cinéma et de l'image
animée)**.
