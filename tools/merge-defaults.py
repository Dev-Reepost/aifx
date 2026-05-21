#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
# Copyright AIFX contributors.
"""
Merge config/defaults-base.json with a plugin's defaults-project.json and
write the result to the bundle's Contents/Resources/config/defaults.json.

Why this exists:
    Every AIFX plugin loads a defaults.json at first instantiation to seed
    its OFX parameter panel (ComfyUI server URL, mount paths, controls,
    project info). The server/controls blocks are shared studio config;
    duplicating them across seven per-plugin defaults.json files made any
    server-side change a seven-file edit. This script lets each plugin own
    only its project-specific block (workflow name, file, version, plus
    optional per-plugin overrides like a shorter timeout) and pulls in the
    studio-wide server + controls from a single base file at build time.

Merge semantics:
    - Start from the base JSON.
    - For each top-level key in the project JSON:
        * If the same key exists in the base and both values are objects,
          shallow-merge them (project wins on collision).
        * Otherwise, project replaces base wholesale.
    - This lets a plugin override e.g. controls.timeout without restating
      the whole controls block.

Invoked from each plugin's CMakeLists.txt POST_BUILD step.

Usage:
    merge-defaults.py BASE PROJECT OUTPUT
"""

import json
import os
import sys


def merge(base, project):
    out = dict(base)
    for key, value in project.items():
        if (
            key in out
            and isinstance(out[key], dict)
            and isinstance(value, dict)
        ):
            out[key] = {**out[key], **value}
        else:
            out[key] = value
    return out


def main():
    if len(sys.argv) != 4:
        sys.exit("Usage: merge-defaults.py BASE PROJECT OUTPUT")
    base_path, project_path, out_path = sys.argv[1:4]

    with open(base_path) as f:
        base = json.load(f)
    with open(project_path) as f:
        project = json.load(f)

    merged = merge(base, project)

    os.makedirs(os.path.dirname(out_path), exist_ok=True)
    with open(out_path, "w") as f:
        json.dump(merged, f, indent=2)
        f.write("\n")


if __name__ == "__main__":
    main()
