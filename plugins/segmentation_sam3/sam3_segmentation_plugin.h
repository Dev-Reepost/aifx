// Copyright OpenFX and contributors to the OpenFX project.
// SPDX-License-Identifier: BSD-3-Clause

#ifndef COMFYUI_SAM3_SEGMENTATION_PLUGIN_H
#define COMFYUI_SAM3_SEGMENTATION_PLUGIN_H

#include "comfyui_base_plugin.h"
#include <string>
#include <vector>

namespace ComfyUI {

/**
 * @brief OFX plugin for SAM3 (Segment Anything Model 3) segmentation via ComfyUI
 *
 * Uses the ComfyUI-SAM3 extension to perform video-aware semantic segmentation
 * based on text prompts. SAM3 propagates the initial segmentation across all frames.
 *
 * Workflow:
 * 1. Load input EXR sequence
 * 2. Load SAM3 model
 * 3. Perform initial segmentation on reference frame (text prompt)
 * 4. Propagate segmentation through entire sequence
 * 5. Extract mask and convert to image
 * 6. Save result as EXR files
 *
 * Based on: https://github.com/PozzettiAndrea/ComfyUI-SAM3
 */
class SAM3SegmentationPlugin : public BasePlugin {
public:
    SAM3SegmentationPlugin(OfxImageEffectHandle handle);
    virtual ~SAM3SegmentationPlugin();

    // Implement abstract methods from BasePlugin
    virtual json buildWorkflow(int frame, const std::map<std::string, std::string>& inputPaths) override;
    virtual std::vector<std::string> getRequiredModels() override;
    bool isSequencePlugin() const override { return true; }
    int  getImageLoadCap()  const override;

    // OFX lifecycle
    virtual void changedParam(const OFX::InstanceChangedArgs &args,
                             const std::string &paramName) override;

    // Static parameter definition (called by factory)
    static void describeInContext(OFX::ImageEffectDescriptor &desc,
                                  OFX::ContextEnum context,
                                  const json* configDefaults = nullptr);

private:
    // Segmentation parameters
    OFX::StringParam  *_textPrompt;         // Text description of what to segment
    OFX::DoubleParam  *_scoreThreshold;     // Detection confidence threshold (0.0-1.0)
    OFX::DoubleParam  *_frameIdx;           // Reference frame ratio for initial segmentation (0.0-1.0)

    // Propagation parameters
    OFX::ChoiceParam  *_direction;          // Propagation direction (forward/backward/both)
    OFX::IntParam     *_objId;             // Object ID to extract from output
    OFX::BooleanParam *_plotAllMasks;       // Show all detected masks vs. single object
    OFX::IntParam     *_imageLoadCap;       // Max frames to load from sequence (0 = unlimited)

    // Model parameters
    OFX::StringParam  *_modelPath;          // SAM3 model path (relative to ComfyUI models dir)
    OFX::BooleanParam *_offloadModel;       // Offload model to CPU after inference (memory saving)

    // Internal helpers
    json buildHardcodedWorkflow(int frame, const std::string& inputPath);
    json customizeWorkflowWithParams(const json& baseWorkflow, int frame);
};

/**
 * @brief Plugin factory for SAM3 Segmentation
 */
class SAM3SegmentationPluginFactory : public OFX::PluginFactoryHelper<SAM3SegmentationPluginFactory> {
public:
    SAM3SegmentationPluginFactory();
    virtual void describe(OFX::ImageEffectDescriptor &desc) override;
    virtual void describeInContext(OFX::ImageEffectDescriptor &desc,
                                   OFX::ContextEnum context) override;
    virtual OFX::ImageEffect* createInstance(OfxImageEffectHandle handle,
                                            OFX::ContextEnum context) override;

private:
    static json loadSAM3ConfigDefaults();
};

} // namespace ComfyUI

#endif // COMFYUI_SAM3_SEGMENTATION_PLUGIN_H
