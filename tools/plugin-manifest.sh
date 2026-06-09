#!/bin/bash
# SPDX-License-Identifier: BSD-3-Clause
# Copyright AIFX contributors.
#
# Populate AIFX_PLUGIN_DIRS and AIFX_PLUGIN_TARGETS from plugins/manifest.txt,
# the single source of truth for the plugin set.
#
# Usage: source this file; do not execute it.
#
#   source "$(dirname "${BASH_SOURCE[0]}")/plugin-manifest.sh"
#   for target in "${AIFX_PLUGIN_TARGETS[@]}"; do ... done
#
# After sourcing, the two arrays are index-aligned: AIFX_PLUGIN_DIRS[i] is the
# plugins/ subdirectory whose CMake target is AIFX_PLUGIN_TARGETS[i].

_aifx_manifest="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)/plugins/manifest.txt"

if [[ ! -f "$_aifx_manifest" ]]; then
    echo "[ERROR] plugin manifest not found: $_aifx_manifest" >&2
    return 1 2>/dev/null || exit 1
fi

AIFX_PLUGIN_DIRS=()
AIFX_PLUGIN_TARGETS=()

# `|| [[ -n "$_dir" ]]` processes a final line lacking a trailing newline.
while read -r _dir _target _rest || [[ -n "$_dir" ]]; do
    _dir="${_dir%$'\r'}"        # tolerate CRLF if the file was edited on Windows
    _target="${_target%$'\r'}"
    [[ -z "$_dir" || "$_dir" == \#* ]] && continue
    if [[ -z "$_target" ]]; then
        echo "[ERROR] manifest line missing target: '$_dir' ($_aifx_manifest)" >&2
        return 1 2>/dev/null || exit 1
    fi
    AIFX_PLUGIN_DIRS+=("$_dir")
    AIFX_PLUGIN_TARGETS+=("$_target")
done < "$_aifx_manifest"

unset _aifx_manifest _dir _target _rest
