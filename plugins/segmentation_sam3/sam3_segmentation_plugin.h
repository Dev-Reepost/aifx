// Copyright OpenFX and contributors to the OpenFX project.
// SPDX-License-Identifier: BSD-3-Clause

#ifndef COMFYUI_SAM3_SEGMENTATION_PLUGIN_H
#define COMFYUI_SAM3_SEGMENTATION_PLUGIN_H

#include "comfyui_base_plugin.h"
#include <string>
#include <vector>

namespace ComfyUI {

/**
 * @brief OFX plugin for SAM3.1 (Segment Anything Model 3.1) segmentation via ComfyUI
 *
 * Uses the ComfyUI-SAM3 extension to perform video-aware semantic segmentation
 * based on text prompts. A reference frame seeds the detection, then SAM3 tracks
 * the detected objects across the loaded EXR batch.
 *
 * Workflow:
 * 1. Load input EXR sequence
 * 2. Load SAM3.1 checkpoint
 * 3. CLIP-encode the text prompt
 * 4. Pick reference frame from the batch
 * 5. SAM3 detect on the reference frame
 * 6. SAM3 video-track across the batch
 * 7. Convert selected tracks to mask, then to image
 * 8. Save result as EXR files
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

    // Static parameter definition (called by factory)
    static void describeInContext(OFX::ImageEffectDescriptor &desc,
                                  OFX::ContextEnum context,
                                  const json* configDefaults = nullptr);

private:
    // Segmentation parameters
    OFX::StringParam  *_textPrompt;         // Text description of what to segment
    OFX::DoubleParam  *_scoreThreshold;     // Detection confidence threshold (0.0-1.0)
    OFX::IntParam     *_batchIndex;         // Reference frame index in the loaded batch
    OFX::StringParam  *_objectIndices;      // Comma-separated list of object indices to keep
    OFX::IntParam     *_maxObjects;         // Maximum number of detected objects to track
    OFX::IntParam     *_detectInterval;     // Run SAM3 detection every N frames
    OFX::IntParam     *_refineIterations;   // Mask refinement iterations on the reference frame
    OFX::BooleanParam *_individualMasks;    // Generate separate masks per detected object
    OFX::IntParam     *_imageLoadCap;       // Max frames to load from sequence (0 = unlimited)

    // Model parameters
    OFX::StringParam  *_ckptName;           // SAM3.1 checkpoint name (CheckpointLoaderSimple)

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
