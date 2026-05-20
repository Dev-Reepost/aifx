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
 * Combines SAM3 video segmentation with MatAnyone2 memory-based video matting
 * to produce temporally-consistent alpha mattes from video sequences.
 *
 * Pipeline:
 * 1. Load reference frame for SAM3          (node 93 - small load cap)
 * 2. SAM3 segmentation + propagation        (nodes 94, 91, 89, 88)
 * 3. Load full sequence for MatAnyone2      (node 171 - full load cap)
 * 4. MatAnyone2 matting                     (node 166)
 * 5. Save matte as EXR                      (node 172)
 *
 * Note: MatAnyone2 outputs an image directly (no MaskToImage step needed).
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
    int  getImageLoadCap()  const override;  // returns _imageLoadCap (full MatAnyone2 sequence)

    virtual void changedParam(const OFX::InstanceChangedArgs &args,
                              const std::string &paramName) override;

    static void describeInContext(OFX::ImageEffectDescriptor &desc,
                                  OFX::ContextEnum context,
                                  const json* configDefaults = nullptr);

private:
    // --- SAM3 parameters ---
    OFX::StringParam  *_textPrompt;
    OFX::DoubleParam  *_scoreThreshold;
    OFX::DoubleParam  *_frameIdx;        // 0.0–1.0 ratio (matches workflow's float value)
    OFX::ChoiceParam  *_direction;
    OFX::BooleanParam *_plotAllMasks;
    OFX::IntParam     *_objId;
    OFX::StringParam  *_sam3ModelPath;
    OFX::BooleanParam *_offloadSam3Model;
    OFX::IntParam     *_sam3ImageLoadCap; // Frames loaded for SAM3 (typically 1–few)

    // --- MatAnyone2 parameters ---
    OFX::IntParam     *_maskFrame;
    OFX::IntParam     *_nWarmup;
    OFX::IntParam     *_rErode;
    OFX::IntParam     *_rDilate;
    OFX::IntParam     *_maxInternalSize;
    OFX::IntParam     *_maxMemFrames;
    OFX::BooleanParam *_useLongTerm;
    OFX::IntParam     *_imageLoadCap;    // Frames loaded for MatAnyone2 (full sequence)

    json buildHardcodedWorkflow(int frame, const std::string& inputPath);
    json customizeWorkflowWithParams(const json& baseWorkflow, int frame);

    std::string getDirectionName() const;
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
