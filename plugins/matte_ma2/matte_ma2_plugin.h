// Copyright OpenFX and contributors to the OpenFX project.
// SPDX-License-Identifier: BSD-3-Clause

#ifndef COMFYUI_MATTE_MA2_PLUGIN_H
#define COMFYUI_MATTE_MA2_PLUGIN_H

#include "comfyui_base_plugin.h"
#include <string>
#include <vector>

namespace ComfyUI {

/**
 * @brief OFX plugin for MatAnyone V2 matting via ComfyUI
 *
 * Combines SAM3 (checkpoint-based) detect + video tracking with MatAnyone2
 * memory-based video matting to produce temporally-consistent alpha mattes
 * from video sequences.
 *
 * Pipeline:
 * 1. Load full sequence                       (node 171)
 * 2. Pick reference frame                     (node 179 - Frame Select)
 * 3. CheckpointLoaderSimple + CLIPTextEncode  (nodes 183, 184)
 * 4. SAM3 detect on reference frame           (node 187)
 * 5. SAM3 video track over full sequence      (node 186)
 * 6. SAM3 track-to-mask                       (node 185)
 * 7. MatAnyone2 matting                       (node 166)
 * 8. Save matte as EXR                        (node 172)
 *
 * Based on: https://github.com/kijai/ComfyUI-MatAnyone
 *           https://github.com/PozzettiAndrea/ComfyUI-SAM3
 */
class MatteMA2Plugin : public BasePlugin {
public:
    MatteMA2Plugin(OfxImageEffectHandle handle);
    virtual ~MatteMA2Plugin();

    virtual json buildWorkflow(int frame, const std::map<std::string, std::string>& inputPaths) override;
    virtual std::vector<std::string> getRequiredModels() override;
    bool isSequencePlugin() const override { return true; }
    int  getImageLoadCap()  const override;  // returns _imageLoadCap (full sequence cap)

    static void describeInContext(OFX::ImageEffectDescriptor &desc,
                                  OFX::ContextEnum context,
                                  const json* configDefaults = nullptr);

private:
    // --- SAM3 parameters ---
    OFX::StringParam  *_textPrompt;
    OFX::StringParam  *_sam3CkptName;
    OFX::StringParam  *_objectIndices;
    OFX::IntParam     *_frameSelect;
    OFX::DoubleParam  *_detectionThreshold;
    OFX::IntParam     *_maxObjects;
    OFX::IntParam     *_detectInterval;
    OFX::IntParam     *_refineIterations;
    OFX::BooleanParam *_individualMasks;

    // --- MatAnyone2 parameters ---
    OFX::IntParam     *_maskFrame;
    OFX::IntParam     *_nWarmup;
    OFX::IntParam     *_rErode;
    OFX::IntParam     *_rDilate;
    OFX::IntParam     *_maxInternalSize;
    OFX::IntParam     *_maxMemFrames;
    OFX::BooleanParam *_useLongTerm;
    OFX::IntParam     *_imageLoadCap;    // Frames loaded (full sequence)

    json buildHardcodedWorkflow(int frame, const std::string& inputPath);
    json customizeWorkflowWithParams(const json& baseWorkflow, int frame);
};

/**
 * @brief Plugin factory for MatteMA2
 */
class MatteMA2PluginFactory : public OFX::PluginFactoryHelper<MatteMA2PluginFactory> {
public:
    MatteMA2PluginFactory();
    virtual void describe(OFX::ImageEffectDescriptor &desc) override;
    virtual void describeInContext(OFX::ImageEffectDescriptor &desc,
                                   OFX::ContextEnum context) override;
    virtual OFX::ImageEffect* createInstance(OfxImageEffectHandle handle,
                                            OFX::ContextEnum context) override;
private:
    static json loadConfigDefaults();
};

} // namespace ComfyUI

#endif // COMFYUI_MATTE_MA2_PLUGIN_H
