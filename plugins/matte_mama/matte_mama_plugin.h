// Copyright OpenFX and contributors to the OpenFX project.
// SPDX-License-Identifier: BSD-3-Clause

#ifndef COMFYUI_MATTE_MAMA_PLUGIN_H
#define COMFYUI_MATTE_MAMA_PLUGIN_H

#include "comfyui_base_plugin.h"
#include <string>
#include <vector>

namespace ComfyUI {

/**
 * @brief OFX plugin for VideoMaMa matting via ComfyUI (MaMa Matting V2)
 *
 * Combines SAM3 (checkpoint-based) detect + video tracking with VideoMaMa
 * diffusion-based matting to produce high-quality alpha mattes from video
 * sequences.
 *
 * Pipeline:
 * 1. Load EXR sequence                              (node 44)
 * 2. CheckpointLoaderSimple + CLIPTextEncode        (nodes 85, 86)
 * 3. ImageFromBatch reference frame                 (node 91)
 * 4. SAM3_Detect on reference frame                 (node 89)
 * 5. SAM3_VideoTrack across full sequence           (node 88)
 * 6. SAM3_TrackToMask                               (node 87)
 * 7. VideoMaMaPipelineLoader + VideoMaMaSampler     (nodes 99, 98)
 * 8. MaskToImage + SaveEXR                          (nodes 35, 45)
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

    static void describeInContext(OFX::ImageEffectDescriptor &desc,
                                  OFX::ContextEnum context,
                                  const json* configDefaults = nullptr);

private:
    // --- SAM3 segmentation parameters ---
    OFX::StringParam  *_textPrompt;
    OFX::DoubleParam  *_scoreThreshold;
    OFX::StringParam  *_sam3CheckpointName;
    OFX::StringParam  *_objectIndices;
    OFX::DoubleParam  *_detectionThreshold;
    OFX::IntParam     *_maxObjects;
    OFX::IntParam     *_detectInterval;
    OFX::IntParam     *_refineIterations;
    OFX::BooleanParam *_individualMasks;
    OFX::IntParam     *_referenceBatchIndex;

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
