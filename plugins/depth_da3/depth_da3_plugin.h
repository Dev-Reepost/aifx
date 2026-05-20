// Copyright OpenFX and contributors to the OpenFX project.
// SPDX-License-Identifier: BSD-3-Clause

#ifndef COMFYUI_DEPTH_DA3_PLUGIN_H
#define COMFYUI_DEPTH_DA3_PLUGIN_H

#include "comfyui_base_plugin.h"
#include <string>
#include <vector>

namespace ComfyUI {

/**
 * @brief OFX plugin for Depth Anything V3 (DA3) depth estimation via ComfyUI
 *
 * Uses the ComfyUI-DepthAnythingV3 extension to perform monocular depth estimation
 * on image sequences. Produces depth maps suitable for ControlNet, 3D reconstruction,
 * and compositing workflows.
 *
 * Workflow:
 * 1. Load input EXR sequence
 * 2. Load DA3 model
 * 3. Run depth estimation on all frames
 * 4. Save depth map as EXR files
 *
 * Based on: https://github.com/PozzettiAndrea/ComfyUI-DepthAnythingV3
 */
class DepthDA3Plugin : public BasePlugin {
public:
    DepthDA3Plugin(OfxImageEffectHandle handle);
    virtual ~DepthDA3Plugin();

    // Implement abstract methods from BasePlugin
    virtual json buildWorkflow(int frame, const std::map<std::string, std::string>& inputPaths) override;
    virtual std::vector<std::string> getRequiredModels() override;

    // Static parameter definition (called by factory)
    static void describeInContext(OFX::ImageEffectDescriptor &desc,
                                  OFX::ContextEnum context,
                                  const json* configDefaults = nullptr);

private:
    // Depth processing parameters
    OFX::ChoiceParam  *_normalizationMode;  // V2-Style or Raw
    OFX::ChoiceParam  *_resizeMethod;       // resize, pad, crop
    OFX::BooleanParam *_invertDepth;        // Invert depth values
    OFX::BooleanParam *_keepModelSize;      // Keep model output size (no upscale)

    // Model parameters
    OFX::ChoiceParam  *_da3Model;           // Model variant (small/base/large/giant/etc.)
    OFX::ChoiceParam  *_precision;          // Computation precision (auto/fp16/fp32/bf16)
    OFX::ChoiceParam  *_attention;          // Attention implementation (flash_attn/xformers/math)

    // Internal helpers
    json buildHardcodedWorkflow(int frame, const std::string& inputPath);
    json customizeWorkflowWithParams(const json& baseWorkflow, int frame);

    // Maps choice index to string value
    std::string getModelName() const;
    std::string getPrecisionName() const;
    std::string getAttentionName() const;
    std::string getNormalizationModeName() const;
    std::string getResizeMethodName() const;
};

/**
 * @brief Plugin factory for Depth Anything V3
 */
class DepthDA3PluginFactory : public OFX::PluginFactoryHelper<DepthDA3PluginFactory> {
public:
    DepthDA3PluginFactory();
    virtual void describe(OFX::ImageEffectDescriptor &desc) override;
    virtual void describeInContext(OFX::ImageEffectDescriptor &desc,
                                   OFX::ContextEnum context) override;
    virtual OFX::ImageEffect* createInstance(OfxImageEffectHandle handle,
                                            OFX::ContextEnum context) override;

private:
    static json loadDA3ConfigDefaults();
};

} // namespace ComfyUI

#endif // COMFYUI_DEPTH_DA3_PLUGIN_H
