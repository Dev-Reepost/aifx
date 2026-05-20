// Copyright OpenFX and contributors to the OpenFX project.
// SPDX-License-Identifier: BSD-3-Clause

#ifndef COMFYUI_NORMAL_CRAFTER_PLUGIN_H
#define COMFYUI_NORMAL_CRAFTER_PLUGIN_H

#include "comfyui_base_plugin.h"
#include <string>
#include <vector>

namespace ComfyUI {

/**
 * @brief OFX plugin for NormalCrafter normal-map estimation via ComfyUI
 *
 * Uses the ComfyUI-NormalCrafterWrapper extension to produce temporally-consistent
 * surface-normal maps from video sequences using diffusion-based estimation.
 *
 * Workflow:
 * 1. Load input EXR sequence  (node 1)
 * 2. Run NormalCrafter inference (node 2)
 * 3. Save normal map as EXR  (node 4)
 *
 * Based on: https://github.com/AIWarper/ComfyUI-NormalCrafterWrapper
 */
class NormalCrafterPlugin : public BasePlugin {
public:
    NormalCrafterPlugin(OfxImageEffectHandle handle);
    virtual ~NormalCrafterPlugin();

    virtual json buildWorkflow(int frame, const std::map<std::string, std::string>& inputPaths) override;
    virtual std::vector<std::string> getRequiredModels() override;
    bool isSequencePlugin()          const override { return true; }
    int  getImageLoadCap()           const override;

    static void describeInContext(OFX::ImageEffectDescriptor &desc,
                                  OFX::ContextEnum context,
                                  const json* configDefaults = nullptr);

private:
    // Processing parameters
    OFX::IntParam    *_seed;                       // RNG seed
    OFX::IntParam    *_maxResDimension;            // Max resolution (longest side)
    OFX::IntParam    *_windowSize;                 // Temporal window size (max frames per pass)
    OFX::IntParam    *_timeStepSize;               // Diffusion time step size
    OFX::IntParam    *_decodeChunkSize;            // VAE decode chunk size
    OFX::IntParam    *_imageLoadCap;               // Max frames to load from sequence

    // Model parameters
    OFX::BooleanParam *_offloadPipeToCpuOnFinish;  // Offload pipeline to CPU after finishing
    OFX::ChoiceParam  *_useXformers;               // xFormers usage: auto / enable / disable

    json buildHardcodedWorkflow(int frame, const std::string& inputPath);
    json customizeWorkflowWithParams(const json& baseWorkflow, int frame);

    static const char* xformersOptionToString(int idx);
};

/**
 * @brief Plugin factory for NormalCrafter
 */
class NormalCrafterPluginFactory : public OFX::PluginFactoryHelper<NormalCrafterPluginFactory> {
public:
    NormalCrafterPluginFactory();
    virtual void describe(OFX::ImageEffectDescriptor &desc) override;
    virtual void describeInContext(OFX::ImageEffectDescriptor &desc,
                                   OFX::ContextEnum context) override;
    virtual OFX::ImageEffect* createInstance(OfxImageEffectHandle handle,
                                            OFX::ContextEnum context) override;
private:
    static json loadNormalCrafterConfigDefaults();
};

} // namespace ComfyUI

#endif // COMFYUI_NORMAL_CRAFTER_PLUGIN_H
