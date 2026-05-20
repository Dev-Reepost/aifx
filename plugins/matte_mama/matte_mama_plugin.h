// Copyright OpenFX and contributors to the OpenFX project.
// SPDX-License-Identifier: BSD-3-Clause

#ifndef COMFYUI_MATTE_MAMA_PLUGIN_H
#define COMFYUI_MATTE_MAMA_PLUGIN_H

#include "comfyui_base_plugin.h"
#include <string>
#include <vector>

namespace ComfyUI {

/**
 * @brief OFX plugin for VideoMaMa matting via ComfyUI
 *
 * Combines SAM3 video segmentation with VideoMaMa diffusion-based matting
 * to produce high-quality alpha mattes from video sequences.
 *
 * Pipeline:
 * 1. Load EXR sequence                    (node 44)
 * 2. SAM3 initial segmentation            (nodes 55, 52, 53, 54)
 * 3. VideoMaMa pipeline load              (node 37)
 * 4. VideoMaMa sampler (image + SAM mask) (node 42)
 * 5. Mask to image + save                 (nodes 35, 45)
 *
 * Based on: https://github.com/kijai/ComfyUI-MaMa (VideoMaMa)
 *           https://github.com/PozzettiAndrea/ComfyUI-SAM3
 */
class MatteMaMaPlugin : public BasePlugin {
public:
    MatteMaMaPlugin(OfxImageEffectHandle handle);
    virtual ~MatteMaMaPlugin();

    virtual json buildWorkflow(int frame, const std::map<std::string, std::string>& inputPaths) override;
    virtual std::vector<std::string> getRequiredModels() override;
    bool isSequencePlugin() const override { return true; }
    int  getImageLoadCap()  const override;

    virtual void changedParam(const OFX::InstanceChangedArgs &args,
                              const std::string &paramName) override;

    static void describeInContext(OFX::ImageEffectDescriptor &desc,
                                  OFX::ContextEnum context,
                                  const json* configDefaults = nullptr);

private:
    // --- SAM3 segmentation parameters ---
    OFX::StringParam  *_textPrompt;
    OFX::DoubleParam  *_scoreThreshold;
    OFX::IntParam     *_frameIdx;       // Reference frame index (integer frame number)
    OFX::ChoiceParam  *_direction;
    OFX::BooleanParam *_plotAllMasks;
    OFX::IntParam     *_objId;
    OFX::StringParam  *_sam3ModelPath;
    OFX::BooleanParam *_offloadSam3Model;

    // --- VideoMaMa sampler parameters ---
    OFX::IntParam     *_seed;
    OFX::IntParam     *_maxResolution;
    OFX::IntParam     *_fps;
    OFX::IntParam     *_motionBucketId;
    OFX::DoubleParam  *_noiseAugStrength;
    OFX::IntParam     *_imageLoadCap;

    // --- VideoMaMa model parameters ---
    OFX::StringParam  *_baseModelPath;
    OFX::StringParam  *_unetCheckpointPath;
    OFX::ChoiceParam  *_precision;
    OFX::BooleanParam *_enableModelCpuOffload;
    OFX::IntParam     *_vaeEncodeChunkSize;
    OFX::ChoiceParam  *_attentionMode;
    OFX::BooleanParam *_enableVaeTiling;
    OFX::BooleanParam *_enableVaeSlicing;

    json buildHardcodedWorkflow(int frame, const std::string& inputPath);
    json customizeWorkflowWithParams(const json& baseWorkflow, int frame);

    std::string getDirectionName() const;
    std::string getPrecisionName() const;
    std::string getAttentionModeName() const;
};

/**
 * @brief Plugin factory for MatteMaMa
 */
class MatteMaMaPluginFactory : public OFX::PluginFactoryHelper<MatteMaMaPluginFactory> {
public:
    MatteMaMaPluginFactory();
    virtual void describe(OFX::ImageEffectDescriptor &desc) override;
    virtual void describeInContext(OFX::ImageEffectDescriptor &desc,
                                   OFX::ContextEnum context) override;
    virtual OFX::ImageEffect* createInstance(OfxImageEffectHandle handle,
                                            OFX::ContextEnum context) override;
private:
    static json loadConfigDefaults();
};

} // namespace ComfyUI

#endif // COMFYUI_MATTE_MAMA_PLUGIN_H
