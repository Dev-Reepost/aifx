// Copyright OpenFX and contributors to the OpenFX project.
// SPDX-License-Identifier: BSD-3-Clause

#ifndef COMFYUI_UPSCALE_SEEDVR2_PLUGIN_H
#define COMFYUI_UPSCALE_SEEDVR2_PLUGIN_H

#include "comfyui_base_plugin.h"
#include <string>
#include <vector>

namespace ComfyUI {

/**
 * @brief OFX plugin for SeedVR2 video upscaling via ComfyUI
 *
 * Uses the SeedVR2 diffusion-based upscaling pipeline consisting of:
 * 1. Load VAE model         (node 41 - SeedVR2LoadVAEModel)
 * 2. Load DiT model         (node 42 - SeedVR2LoadDiTModel)
 * 3. Load EXR sequence      (node 43 - LoadEXR)
 * 4. SeedVR2 upscaling      (node 44 - SeedVR2VideoUpscaler)
 * 5. Save upscaled EXR      (node 45 - SaveEXR)
 *
 * Based on: https://github.com/kijai/ComfyUI-SeedVR2_VideoUpscaler
 */
class UpscaleSeedVR2Plugin : public BasePlugin {
public:
    UpscaleSeedVR2Plugin(OfxImageEffectHandle handle);
    virtual ~UpscaleSeedVR2Plugin();

    virtual json buildWorkflow(int frame, const std::map<std::string, std::string>& inputPaths) override;
    virtual std::vector<std::string> getRequiredModels() override;
    bool isSequencePlugin() const override { return true; }
    int  getImageLoadCap()  const override;

    static void describeInContext(OFX::ImageEffectDescriptor &desc,
                                  OFX::ContextEnum context,
                                  const json* configDefaults = nullptr);

private:
    // --- Upscaler parameters ---
    OFX::IntParam     *_seed;
    OFX::IntParam     *_resolution;
    OFX::IntParam     *_maxResolution;
    OFX::IntParam     *_batchSize;
    OFX::BooleanParam *_uniformBatchSize;
    OFX::ChoiceParam  *_colorCorrection;
    OFX::IntParam     *_temporalOverlap;
    OFX::IntParam     *_prependFrames;
    OFX::DoubleParam  *_inputNoiseScale;
    OFX::DoubleParam  *_latentNoiseScale;
    OFX::BooleanParam *_enableDebug;
    OFX::IntParam     *_imageLoadCap;

    // --- VAE model parameters ---
    OFX::StringParam  *_vaeModel;
    OFX::StringParam  *_vaeDevice;
    OFX::BooleanParam *_encodeTiled;
    OFX::IntParam     *_encodeTileSize;
    OFX::IntParam     *_encodeTileOverlap;
    OFX::BooleanParam *_decodeTiled;
    OFX::IntParam     *_decodeTileSize;
    OFX::IntParam     *_decodeTileOverlap;
    OFX::StringParam  *_offloadVAE;
    OFX::BooleanParam *_cacheVAEModel;

    // --- DiT model parameters ---
    OFX::StringParam  *_ditModel;
    OFX::StringParam  *_ditDevice;
    OFX::IntParam     *_blocksToSwap;
    OFX::BooleanParam *_swapIOComponents;
    OFX::ChoiceParam  *_attentionMode;
    OFX::StringParam  *_offloadDiT;
    OFX::BooleanParam *_cacheDiTModel;

    json buildHardcodedWorkflow(int frame, const std::string& inputPath);
    json customizeWorkflowWithParams(const json& baseWorkflow, int frame);

    std::string getColorCorrectionName() const;
    std::string getAttentionModeName() const;
};

/**
 * @brief Plugin factory for UpscaleSeedVR2
 */
class UpscaleSeedVR2PluginFactory : public OFX::PluginFactoryHelper<UpscaleSeedVR2PluginFactory> {
public:
    UpscaleSeedVR2PluginFactory();
    virtual void describe(OFX::ImageEffectDescriptor &desc) override;
    virtual void describeInContext(OFX::ImageEffectDescriptor &desc,
                                   OFX::ContextEnum context) override;
    virtual OFX::ImageEffect* createInstance(OfxImageEffectHandle handle,
                                             OFX::ContextEnum context) override;
private:
    static json loadConfigDefaults();
};

} // namespace ComfyUI

#endif // COMFYUI_UPSCALE_SEEDVR2_PLUGIN_H
