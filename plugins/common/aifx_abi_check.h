// Copyright OpenFX and contributors to the OpenFX project.
// SPDX-License-Identifier: BSD-3-Clause
//
// ============================================================================
// Compile-time ABI guard against the OpenFX Support library.
// ============================================================================
//
// WHY THIS EXISTS
//
// OFX_SUPPORTS_OPENGLRENDER (and its OpenCL/CUDA siblings) do not merely add
// optional features to <ofxsImageEffect.h> -- they change the *layout* of
// types that cross the boundary between the OfxSupport archive and our plugin:
//
//   * OFX::ImageEffect gains two trailing virtuals, contextAttached() and
//     contextDetached(), so its vtable grows by two slots.
//   * OFX::RenderArguments / BeginSequenceRenderArguments /
//     EndSequenceRenderArguments each gain an `openGLEnabled` member, so their
//     sizeof() changes.
//   * OFX::ImageEffectHostDescription gains `supportsOpenGLRender`.
//
// Upstream OpenFX applies the macro with a directory-scoped add_definitions()
// inside its own tree, so the archive is built WITH it while a sibling
// directory (ours) silently compiles WITHOUT it. The result is an ODR
// violation with no diagnostic: OfxSupport dispatches host actions through the
// LONGER vtable, our plugin published the SHORTER one, and the host ends up
// calling whatever virtual happens to sit at that index. In the field this
// showed up as Flare/macOS jumping straight from a successful constructor into
// BasePlugin::buildWorkflow() with garbage arguments -- an immediate SIGSEGV
// at ~0x8 as the bogus std::map reference was dereferenced.
//
// WHAT THIS CHECKS
//
// The top-level CMakeLists mirrors the flags onto every AIFX target and records
// them in the generated aifx_abi.h. Rather than re-testing the macro name
// (which would just be the build system agreeing with itself), we measure the
// two ABI properties the macro actually controls, as this translation unit sees
// them, and assert they match what OfxSupport was built with:
//
//   * does OFX::ImageEffect declare contextAttached()  -> vtable length
//   * sizeof(OFX::RenderArguments)                     -> struct layout
//
// So an OpenFX upgrade that renames the option, moves the gate, or adds another
// conditional member trips this too, instead of shipping a silent miscompile.
//
// Include this from a header every plugin translation unit already pulls in
// (comfyui_base_plugin.h does exactly that), so no plugin can opt out by
// accident.

#ifndef AIFX_ABI_CHECK_H
#define AIFX_ABI_CHECK_H

#include "aifx_abi.h"
#include "ofxsImageEffect.h"

#include <cstddef>
#include <type_traits>

namespace ComfyUI {
namespace abi {

// --- Probe 1: vtable length -------------------------------------------------
//
// contextAttached() is the last virtual of OFX::ImageEffect and exists only
// under OFX_SUPPORTS_OPENGLRENDER. Detecting it detects the two-slot vtable
// difference that mis-dispatches host actions.
template <typename T, typename = void>
struct has_context_attached : std::false_type {};

template <typename T>
struct has_context_attached<
    T, decltype(static_cast<void (T::*)(void)>(&T::contextAttached), void())>
    : std::true_type {};

constexpr bool kThisTuHasOpenGLRender = has_context_attached<OFX::ImageEffect>::value;

static_assert(
    kThisTuHasOpenGLRender == (AIFX_OFX_EXPECT_OPENGLRENDER != 0),
    "OpenFX ABI mismatch: this translation unit's view of OFX::ImageEffect "
    "disagrees with the OfxSupport archive on OFX_SUPPORTS_OPENGLRENDER. The "
    "vtables differ by two slots and the host will dispatch actions into the "
    "wrong virtual (SIGSEGV on instancing). Ensure the target compiles with the "
    "definitions the top-level CMakeLists mirrors from OpenFX -- see the "
    "'vtable-layout consistency' block in CMakeLists.txt.");

// --- Probe 2: render-argument struct layout ---------------------------------
//
// The same macro adds an `openGLEnabled` member to the POD structs the host
// fills in and passes to render()/beginSequenceRender()/endSequenceRender().
// A disagreement here means every field after it is read at the wrong offset,
// independently of the vtable problem above -- so probe it separately rather
// than assuming one macro implies the other.
template <typename T, typename = void>
struct has_opengl_enabled : std::false_type {};

template <typename T>
struct has_opengl_enabled<T, decltype((void)T::openGLEnabled, void())>
    : std::true_type {};

static_assert(
    has_opengl_enabled<OFX::RenderArguments>::value ==
        (AIFX_OFX_EXPECT_OPENGLRENDER != 0),
    "OpenFX ABI mismatch: OFX::RenderArguments layout disagrees with the "
    "OfxSupport archive on OFX_SUPPORTS_OPENGLRENDER. Every field after "
    "openGLEnabled would be read at the wrong offset during render.");

static_assert(
    has_opengl_enabled<OFX::BeginSequenceRenderArguments>::value ==
        has_opengl_enabled<OFX::RenderArguments>::value,
    "OpenFX ABI: RenderArguments and BeginSequenceRenderArguments are gated "
    "inconsistently. The Support headers changed shape in a way AIFX has not "
    "been reviewed against -- re-check aifx_abi_check.h before bumping the "
    "OpenFX GIT_TAG in CMakeLists.txt.");

} // namespace abi
} // namespace ComfyUI

#endif // AIFX_ABI_CHECK_H
