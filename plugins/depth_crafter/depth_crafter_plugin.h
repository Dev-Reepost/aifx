// Copyright OpenFX and contributors to the OpenFX project.
// SPDX-License-Identifier: BSD-3-Clause

#ifndef COMFYUI_DEPTH_CRAFTER_PLUGIN_H
#define COMFYUI_DEPTH_CRAFTER_PLUGIN_H

#include "comfyui_base_plugin.h"
#include <string>
#include <vector>

namespace ComfyUI {

/**
 * @brief OFX plugin for DepthCrafter depth estimation via ComfyUI
 *
 * Uses the ComfyUI-DepthCrafter extension to produce temporally-consistent
 * depth maps from video sequences using diffusion-based estimation.
 *
 * Workflow:
 * 1. Load input EXR sequence  (node 22)
 * 2. Load DepthCrafter model  (node 26)
 * 3. Run depth estimation     (node 24)
 * 4. Save depth map as EXR   (node 23)
 *
 * Based on: https://github.com/kijai/ComfyUI-DepthCrafterWrapper
 */
class DepthCrafterPlugin : public BasePlugin {
public:
    DepthCrafterPlugin(OfxImageEffectHandle handle);
    virtual ~DepthCrafterPlugin();

    virtual json buildWorkflow(int frame, const std::map<std::string, std::string>& inputPaths) override;
    virtual std::vector<std::string> getRequiredModels() override;
    bool isSequencePlugin()          const override { return true; }
    int  getImageLoadCap()           const override;

    static void describeInContext(OFX::ImageEffectDescriptor &desc,
                                  OFX::ContextEnum context,
                                  const json* configDefaults = nullptr);

private:
    // Processing parameters
    OFX::BooleanParam *_forceSize;         // Enable force-size mode (boolean flag for DepthCrafter node)
    OFX::IntParam    *_numInferenceSteps;  // Diffusion denoising steps
    OFX::DoubleParam *_guidanceScale;      // Classifier-free guidance scale
    OFX::IntParam    *_windowSize;         // Temporal window size (max frames per pass)
    OFX::IntParam    *_overlap;            // Frame overlap between windows
    OFX::IntParam    *_imageLoadCap;       // Max frames to load from sequence

    // Model parameters
    OFX::BooleanParam *_enableModelCpuOffload;      // Offload model to CPU between passes
    OFX::BooleanParam *_enableSequentialCpuOffload; // Sequential CPU offload (lower VRAM)

    json buildHardcodedWorkflow(int frame, const std::string& inputPath);
    json customizeWorkflowWithParams(const json& baseWorkflow, int frame);
};

/**
 * @brief Plugin factory for DepthCrafter
 */
class DepthCrafterPluginFactory : public OFX::PluginFactoryHelper<DepthCrafterPluginFactory> {
public:
    DepthCrafterPluginFactory();
    virtual void describe(OFX::ImageEffectDescriptor &desc) override;
    virtual void describeInContext(OFX::ImageEffectDescriptor &desc,
                                   OFX::ContextEnum context) override;
    virtual OFX::ImageEffect* createInstance(OfxImageEffectHandle handle,
                                            OFX::ContextEnum context) override;
private:
    static json loadDepthCrafterConfigDefaults();
};

} // namespace ComfyUI

#endif // COMFYUI_DEPTH_CRAFTER_PLUGIN_H
