// Copyright OpenFX and contributors to the OpenFX project.
// SPDX-License-Identifier: BSD-3-Clause

#include "sam3_segmentation_plugin.h"
#include <sstream>
#include <iomanip>
#include <fstream>

namespace ComfyUI {

// ============================================================================
// SAM3SegmentationPlugin Implementation
// ============================================================================

SAM3SegmentationPlugin::SAM3SegmentationPlugin(OfxImageEffectHandle handle)
    : BasePlugin(handle)
    , _textPrompt(nullptr)
    , _scoreThreshold(nullptr)
    , _frameIdx(nullptr)
    , _direction(nullptr)
    , _objId(nullptr)
    , _plotAllMasks(nullptr)
    , _imageLoadCap(nullptr)
    , _modelPath(nullptr)
    , _offloadModel(nullptr)
{
    _textPrompt     = fetchStringParam("textPrompt");
    _scoreThreshold = fetchDoubleParam("scoreThreshold");
    _frameIdx       = fetchDoubleParam("frameIdx");
    _direction      = fetchChoiceParam("direction");
    _objId          = fetchIntParam("objId");
    _plotAllMasks   = fetchBooleanParam("plotAllMasks");
    _imageLoadCap   = fetchIntParam("imageLoadCap");
    _modelPath      = fetchStringParam("modelPath");
    _offloadModel   = fetchBooleanParam("offloadModel");
}

SAM3SegmentationPlugin::~SAM3SegmentationPlugin()
{
}

int SAM3SegmentationPlugin::getImageLoadCap() const {
    int cap = 0;
    if (_imageLoadCap) _imageLoadCap->getValue(cap);
    return cap;
}

void SAM3SegmentationPlugin::changedParam(const OFX::InstanceChangedArgs &args,
                                          const std::string &paramName)
{
    // Base class handles server address / port changes
    BasePlugin::changedParam(args, paramName);

    // objId only matters when plotAllMasks is false
    if (paramName == "plotAllMasks") {
        bool plotAll;
        _plotAllMasks->getValueAtTime(args.time, plotAll);
        _objId->setEnabled(!plotAll);
        _objId->setIsSecret(plotAll);
    }
}

// ============================================================================
// Workflow building
// ============================================================================

json SAM3SegmentationPlugin::buildWorkflow(int frame,
                                            const std::map<std::string, std::string>& inputPaths)
{
    if (_logger) _logger->info("Building SAM3 segmentation workflow for frame {}", frame);

    // Extract primary input path
    std::string inputPath;
    auto it = inputPaths.find("InputA");
    if (it != inputPaths.end()) {
        inputPath = it->second;
    } else if (!inputPaths.empty()) {
        inputPath = inputPaths.begin()->second;
    } else {
        throw std::runtime_error("No input path provided for SAM3 workflow");
    }

    if (_logger) _logger->info("Using input path: {}", inputPath);

    // Try to load workflow from file first
    std::string workflowPath;
    _workflowFilePath->getValue(workflowPath);

    if (!workflowPath.empty()) {
        if (_logger) _logger->info("Attempting to load workflow from file: {}", workflowPath);
        try {
            std::string resolvedPath = resolveWorkflowPath(workflowPath);
            if (!resolvedPath.empty()) {
                json baseWorkflow = loadWorkflowFromFile(resolvedPath);

                // Base class handles ${INPUT_PATH}, ${OUTPUT_PREFIX}, ${FRAME}
                json customized = customizeWorkflow(baseWorkflow, frame, inputPaths);
                json final = customizeWorkflowWithParams(customized, frame);
                if (_logger) _logger->info("Successfully loaded and customized workflow from file");
                return final;
            } else {
                if (_logger) _logger->warn("Could not resolve workflow path, falling back to hardcoded workflow");
            }
        } catch (const std::exception& e) {
            if (_logger) _logger->error("Failed to load workflow from file: {}. Falling back to hardcoded workflow.", e.what());
        }
    }

    return buildHardcodedWorkflow(frame, inputPath);
}

json SAM3SegmentationPlugin::buildHardcodedWorkflow(int frame, const std::string& inputPath)
{
    if (_logger) _logger->info("Building hardcoded SAM3 segmentation workflow");

    // Fetch parameter values
    std::string prompt;
    _textPrompt->getValue(prompt);

    double scoreThreshold = _scoreThreshold->getValue();
    double frameIdx       = _frameIdx->getValue();

    int directionIndex;
    _direction->getValue(directionIndex);
    std::string direction;
    switch (directionIndex) {
        case 0: direction = "forward";  break;
        case 1: direction = "backward"; break;
        case 2: direction = "both";     break;
        default: direction = "forward";
    }

    int  objId;
    _objId->getValue(objId);

    bool plotAllMasks;
    _plotAllMasks->getValue(plotAllMasks);

    int imageLoadCap;
    _imageLoadCap->getValue(imageLoadCap);

    std::string modelPath;
    _modelPath->getValue(modelPath);

    bool offloadModel;
    _offloadModel->getValue(offloadModel);

    // Build output path
    std::string mountPath, project, workflow_name, version;
    mountPath     = getTrimmedStringParam(_macMountPath);
    project       = getTrimmedStringParam(_projectName);
    workflow_name = getTrimmedStringParam(_workflowName);
    version       = getTrimmedStringParam(_outputVersion);

    std::string basename = getEffectiveBasename();

    std::ostringstream outputPrefix;
    outputPrefix << mountPath << "/out/" << project << "/" << workflow_name
                 << "/" << version << "/" << basename;

    // Convert paths for ComfyUI (Windows server)
    std::string comfyInputPath    = convertPathForComfyUI(inputPath);
    std::string comfyOutputPrefix = convertPathForComfyUI(outputPrefix.str());

    if (_logger) {
        _logger->info("SAM3 parameters:");
        _logger->info("  Prompt: {}", prompt);
        _logger->info("  Score threshold: {}", scoreThreshold);
        _logger->info("  Frame idx: {}", frameIdx);
        _logger->info("  Direction: {}", direction);
        _logger->info("  Obj ID: {}", objId);
        _logger->info("  Plot all masks: {}", plotAllMasks);
        _logger->info("  Image load cap: {}", imageLoadCap);
        _logger->info("  Model path: {}", modelPath);
        _logger->info("  Offload model: {}", offloadModel);
        _logger->info("  Input (ComfyUI): {}", comfyInputPath);
        _logger->info("  Output prefix (ComfyUI): {}", comfyOutputPrefix);
    }

    // Build workflow JSON matching segmentation_SAM3_ofx_api.json structure
    json workflow = {

        // Node 137: Load EXR (single frame file path, same as old SAM plugin)
        {"137", {
            {"inputs", {
                {"filepath", comfyInputPath},
                {"tonemap", "linear"},
                {"image_load_cap", imageLoadCap},
                {"skip_first_images", 0},
                {"select_every_nth", 1}
            }},
            {"class_type", "LoadEXR"},
            {"_meta", {{"title", "Load EXR"}}}
        }},

        // Node 138: Load SAM3 model
        {"138", {
            {"inputs", {
                {"model_path", modelPath}
            }},
            {"class_type", "LoadSAM3Model"},
            {"_meta", {{"title", "(down)Load SAM3 Model"}}}
        }},

        // Node 136: SAM3 initial segmentation on reference frame
        {"136", {
            {"inputs", {
                {"prompt_mode", "text"},
                {"text_prompt", prompt},
                {"frame_idx", frameIdx},
                {"score_threshold", scoreThreshold},
                {"video_frames", json::array({"137", 0})}
            }},
            {"class_type", "SAM3VideoSegmentation"},
            {"_meta", {{"title", "SAM3 Video Segmentation"}}}
        }},

        // Node 134: Propagate segmentation through all frames
        {"134", {
            {"inputs", {
                {"start_frame", 0},
                {"end_frame", -1},
                {"direction", direction},
                {"offload_model", offloadModel},
                {"sam3_model", json::array({"138", 0})},
                {"video_state", json::array({"136", 0})}
            }},
            {"class_type", "SAM3Propagate"},
            {"_meta", {{"title", "SAM3 Propagate"}}}
        }},

        // Node 133: Extract mask from propagation result
        {"133", {
            {"inputs", {
                {"obj_id", objId},
                {"plot_all_masks", plotAllMasks},
                {"masks", json::array({"134", 0})},
                {"video_state", json::array({"134", 2})},
                {"scores", json::array({"134", 1})}
            }},
            {"class_type", "SAM3VideoOutput"},
            {"_meta", {{"title", "SAM3 Video Output"}}}
        }},

        // Node 132: Convert mask to image
        {"132", {
            {"inputs", {
                {"mask", json::array({"133", 0})}
            }},
            {"class_type", "MaskToImage"},
            {"_meta", {{"title", "Convert Mask to Image"}}}
        }},

        // Node 139: Save result as EXR
        {"139", {
            {"inputs", {
                {"filename_prefix", comfyOutputPrefix},
                {"tonemap", "linear"},
                {"version", -1},
                {"start_frame", frame},
                {"frame_pad", 4},
                {"save_workflow", "none"},
                {"images", json::array({"132", 0})}
            }},
            {"class_type", "SaveEXR"},
            {"_meta", {{"title", "SAVE_SEG_MATTE"}}}
        }}
    };

    if (_logger) {
        _logger->info("=== SAM3 HARDCODED WORKFLOW DUMP ===");
        _logger->info("{}", workflow.dump(2));
        _logger->info("=== END WORKFLOW DUMP ===");
    }

    return workflow;
}

json SAM3SegmentationPlugin::customizeWorkflowWithParams(const json& baseWorkflow, int frame)
{
    if (_logger) _logger->debug("Customizing SAM3 workflow with parameters");

    // Fetch values
    std::string prompt;
    _textPrompt->getValue(prompt);

    double scoreThreshold = _scoreThreshold->getValue();
    double frameIdx       = _frameIdx->getValue();

    int directionIndex;
    _direction->getValue(directionIndex);
    std::string direction;
    switch (directionIndex) {
        case 0: direction = "forward";  break;
        case 1: direction = "backward"; break;
        case 2: direction = "both";     break;
        default: direction = "forward";
    }

    int  objId;
    _objId->getValue(objId);

    bool plotAllMasks;
    _plotAllMasks->getValue(plotAllMasks);

    int imageLoadCap;
    _imageLoadCap->getValue(imageLoadCap);

    std::string modelPath;
    _modelPath->getValue(modelPath);

    bool offloadModel;
    _offloadModel->getValue(offloadModel);

    // Placeholder-based replacement in serialised JSON
    std::string workflowStr = baseWorkflow.dump();

    auto replaceStr = [&](const std::string& placeholder, const std::string& value) {
        size_t pos = 0;
        while ((pos = workflowStr.find(placeholder, pos)) != std::string::npos) {
            workflowStr.replace(pos, placeholder.length(), value);
            pos += value.length();
        }
    };

    auto replaceNumeric = [&](const std::string& quotedPlaceholder, const std::string& numStr) {
        // Remove surrounding quotes so the JSON value becomes a number, not a string
        size_t pos = 0;
        while ((pos = workflowStr.find(quotedPlaceholder, pos)) != std::string::npos) {
            workflowStr.replace(pos, quotedPlaceholder.length(), numStr);
            pos += numStr.length();
        }
    };

    replaceStr("${PROMPT}", prompt);
    replaceStr("${DIRECTION}", direction);
    replaceStr("${MODEL_PATH}", modelPath);

    replaceNumeric("\"${SCORE_THRESHOLD}\"", std::to_string(scoreThreshold));
    replaceNumeric("\"${FRAME_IDX}\"",       std::to_string(frameIdx));
    replaceNumeric("\"${OBJ_ID}\"",          std::to_string(objId));
    replaceNumeric("\"${PLOT_ALL_MASKS}\"",  plotAllMasks ? "true" : "false");
    replaceNumeric("\"${OFFLOAD_MODEL}\"",   offloadModel ? "true" : "false");
    replaceNumeric("\"${IMAGE_LOAD_CAP}\"",  std::to_string(imageLoadCap));
    // Note: ${FRAME} is handled by the base class customizeWorkflow()

    try {
        json finalWorkflow = json::parse(workflowStr);

        if (_logger) {
            _logger->info("=== SAM3 FINAL WORKFLOW TO COMFYUI ===");
            _logger->info("{}", finalWorkflow.dump(2));
            _logger->info("=== END FINAL WORKFLOW ===");
        }

        return finalWorkflow;
    } catch (const json::exception& e) {
        if (_logger) _logger->error("Failed to parse customized SAM3 workflow: {}", e.what());
        throw std::runtime_error("Failed to parse SAM3 customized workflow: " + std::string(e.what()));
    }
}

std::vector<std::string> SAM3SegmentationPlugin::getRequiredModels()
{
    std::string modelPath;
    _modelPath->getValue(modelPath);
    return {modelPath};
}

// ============================================================================
// Parameter Definitions
// ============================================================================

void SAM3SegmentationPlugin::describeInContext(OFX::ImageEffectDescriptor &desc,
                                                OFX::ContextEnum context,
                                                const json* configDefaults)
{
    OFX::PageParamDescriptor *page = desc.definePageParam("Controls");

    // ==== SEGMENTATION GROUP ====
    OFX::GroupParamDescriptor *samGroup = desc.defineGroupParam("sam3Group");
    samGroup->setLabel("SAM3 Segmentation");
    samGroup->setOpen(true);
    page->addChild(*samGroup);

    // Text prompt
    OFX::StringParamDescriptor *textPrompt = desc.defineStringParam("textPrompt");
    textPrompt->setLabel("Prompt");
    textPrompt->setHint("Text description of what to segment (e.g., 'person', 'car', 'girl')");
    textPrompt->setDefault("foreground");
    textPrompt->setAnimates(false);
    textPrompt->setParent(*samGroup);
    page->addChild(*textPrompt);

    // Score threshold
    OFX::DoubleParamDescriptor *scoreThreshold = desc.defineDoubleParam("scoreThreshold");
    scoreThreshold->setLabel("Threshold");
    scoreThreshold->setHint("Detection confidence threshold (0.0-1.0). Higher = stricter detection.");
    scoreThreshold->setDefault(0.3);
    scoreThreshold->setRange(0.0, 1.0);
    scoreThreshold->setDisplayRange(0.0, 1.0);
    scoreThreshold->setParent(*samGroup);
    page->addChild(*scoreThreshold);

    // Reference frame index
    OFX::DoubleParamDescriptor *frameIdx = desc.defineDoubleParam("frameIdx");
    frameIdx->setLabel("Reference Frame");
    frameIdx->setHint("Fraction of the sequence to use as reference for initial segmentation (0.0 = first frame, 1.0 = last frame).");
    frameIdx->setDefault(0.3);
    frameIdx->setRange(0.0, 1.0);
    frameIdx->setDisplayRange(0.0, 1.0);
    frameIdx->setParent(*samGroup);
    page->addChild(*frameIdx);

    // Propagation direction
    OFX::ChoiceParamDescriptor *direction = desc.defineChoiceParam("direction");
    direction->setLabel("Propagation Direction");
    direction->setHint("Direction to propagate segmentation through the sequence.");
    direction->appendOption("Forward",  "Propagate from reference frame to end");
    direction->appendOption("Backward", "Propagate from reference frame to start");
    direction->appendOption("Both",     "Propagate in both directions");
    direction->setDefault(0);
    direction->setAnimates(false);
    direction->setParent(*samGroup);
    page->addChild(*direction);

    // Plot all masks
    OFX::BooleanParamDescriptor *plotAllMasks = desc.defineBooleanParam("plotAllMasks");
    plotAllMasks->setLabel("All Masks");
    plotAllMasks->setHint("When enabled, all detected objects are shown. When disabled, only the object with Object ID is shown.");
    plotAllMasks->setDefault(true);
    plotAllMasks->setParent(*samGroup);
    page->addChild(*plotAllMasks);

    // Object ID (visible when plotAllMasks is false)
    OFX::IntParamDescriptor *objId = desc.defineIntParam("objId");
    objId->setLabel("Object ID");
    objId->setHint("Index of the object to extract when All Masks is disabled (0 = first detected object).");
    objId->setDefault(0);
    objId->setRange(0, 99);
    objId->setDisplayRange(0, 10);
    objId->setIsSecret(true);   // Hidden by default (plotAllMasks = true)
    objId->setEnabled(false);
    objId->setParent(*samGroup);
    page->addChild(*objId);

    // Frame limit (image load cap)
    OFX::IntParamDescriptor *imageLoadCap = desc.defineIntParam("imageLoadCap");
    imageLoadCap->setLabel("Frame Limit");
    imageLoadCap->setHint("Maximum number of frames to load from the input sequence. 0 = no limit.");
    imageLoadCap->setDefault(50);
    imageLoadCap->setRange(0, 1024);
    imageLoadCap->setDisplayRange(0, 200);
    imageLoadCap->setAnimates(false);
    imageLoadCap->setParent(*samGroup);
    page->addChild(*imageLoadCap);

    // ==== MODEL GROUP ====
    OFX::GroupParamDescriptor *modelGroup = desc.defineGroupParam("sam3ModelGroup");
    modelGroup->setLabel("Model");
    modelGroup->setOpen(false);
    page->addChild(*modelGroup);

    // SAM3 model path
    OFX::StringParamDescriptor *modelPath = desc.defineStringParam("modelPath");
    modelPath->setLabel("SAM3 Model Path");
    modelPath->setHint("Path to the SAM3 model file, relative to the ComfyUI models directory (e.g., 'models/sam3/sam3.pt').");
    modelPath->setDefault("models/sam3/sam3.pt");
    modelPath->setAnimates(false);
    modelPath->setParent(*modelGroup);
    page->addChild(*modelPath);

    // Offload model (advanced)
    OFX::BooleanParamDescriptor *offloadModel = desc.defineBooleanParam("offloadModel");
    offloadModel->setLabel("Offload Model");
    offloadModel->setHint("Offload SAM3 model to CPU after inference to save VRAM. Slower but uses less GPU memory.");
    offloadModel->setDefault(false);
    offloadModel->setParent(*modelGroup);
    page->addChild(*offloadModel);

    // Add common parameters (server, project, cache, etc.) with optional config defaults
    BasePlugin::describeCommonParameters(desc, context, page, page, page, configDefaults,
                                         /*skipGroupHeaders=*/false, /*isSequencePlugin=*/true);

    // Override workflowName default for this plugin
    OFX::StringParamDescriptor *workflow = desc.defineStringParam("workflowName");
    workflow->setDefault("segmentation_sam3");
}

// ============================================================================
// SAM3SegmentationPluginFactory Implementation
// ============================================================================

SAM3SegmentationPluginFactory::SAM3SegmentationPluginFactory()
    : OFX::PluginFactoryHelper<SAM3SegmentationPluginFactory>(
        "com.comfyui.SegmentationSAM3",  // Plugin identifier
        1,                                // Version major
        0                                 // Version minor
    )
{
}

json SAM3SegmentationPluginFactory::loadSAM3ConfigDefaults()
{
    // Search for config in the SAM3 bundle locations, then fall back to AnyComfy
    const char* home = getenv("HOME");
    if (!home) return json{};

    std::vector<std::string> searchPaths = {
        std::string(home) + "/Library/OFX/Plugins/SegmentationSAM3.ofx.bundle/Contents/Resources/config/defaults.json",
        std::string(home) + "/OFX/Plugins/SegmentationSAM3.ofx.bundle/Contents/Resources/config/defaults.json",
        "/Library/OFX/Plugins/SegmentationSAM3.ofx.bundle/Contents/Resources/config/defaults.json",
        // Fall back to AnyComfy config if SAM3 bundle config not found
        std::string(home) + "/Library/OFX/Plugins/AnyComfy.ofx.bundle/Contents/Resources/config/defaults.json",
        std::string(home) + "/OFX/Plugins/AnyComfy.ofx.bundle/Contents/Resources/config/defaults.json",
        "/Library/OFX/Plugins/AnyComfy.ofx.bundle/Contents/Resources/config/defaults.json"
    };

    for (const auto& path : searchPaths) {
        std::ifstream f(path);
        if (f.is_open()) {
            json config;
            f >> config;
            return config;
        }
    }

    return json{};
}

void SAM3SegmentationPluginFactory::describe(OFX::ImageEffectDescriptor &desc)
{
    desc.setLabel("ComfyUI SAM3 Segmentation");
    desc.setLabels("ComfyUI SAM3", "SAM3", "ComfyUI SAM3 Segmentation");
    desc.setPluginGrouping("ComfyUI");

    desc.setPluginDescription(
        "Video-aware semantic segmentation using SAM3 (Segment Anything Model 3) via ComfyUI.\n\n"
        "Uses text prompts to segment objects and propagates the result across the entire "
        "frame sequence — no per-frame re-processing required.\n\n"
        "Requires:\n"
        "- ComfyUI server with ComfyUI-SAM3 extension installed\n"
        "- SAM3 model file (downloaded on first use by ComfyUI-SAM3)\n"
        "- Shared network storage for image exchange\n\n"
        "Based on: https://github.com/PozzettiAndrea/ComfyUI-SAM3\n"
        "Research: https://github.com/facebookresearch/sam3"
    );

    desc.addSupportedContext(OFX::eContextFilter);
    desc.addSupportedContext(OFX::eContextGeneral);

    desc.addSupportedBitDepth(OFX::eBitDepthUByte);
    desc.addSupportedBitDepth(OFX::eBitDepthUShort);
    desc.addSupportedBitDepth(OFX::eBitDepthFloat);

    desc.setSingleInstance(false);
    desc.setHostFrameThreading(false);
    desc.setSupportsMultiResolution(true);
    desc.setSupportsTiles(false);
    desc.setTemporalClipAccess(true);
    desc.setRenderTwiceAlways(false);
    desc.setSupportsMultipleClipPARs(false);
    desc.setSupportsMultipleClipDepths(true);
    desc.setRenderThreadSafety(OFX::eRenderInstanceSafe);
}

void SAM3SegmentationPluginFactory::describeInContext(OFX::ImageEffectDescriptor &desc,
                                                       OFX::ContextEnum context)
{
    // Source clip
    OFX::ClipDescriptor *srcClip = desc.defineClip(kOfxImageEffectSimpleSourceClipName);
    srcClip->addSupportedComponent(OFX::ePixelComponentRGBA);
    srcClip->addSupportedComponent(OFX::ePixelComponentRGB);
    // Sequence plugin: render() collects all frames in [start..end] via fetchImage(t),
    // so the source clip must permit temporal access (effect-level was already true).
    srcClip->setTemporalClipAccess(true);
    srcClip->setSupportsTiles(false);
    srcClip->setIsMask(false);

    // Output clip
    OFX::ClipDescriptor *dstClip = desc.defineClip(kOfxImageEffectOutputClipName);
    dstClip->addSupportedComponent(OFX::ePixelComponentRGBA);
    dstClip->addSupportedComponent(OFX::ePixelComponentRGB);
    dstClip->setSupportsTiles(false);

    // Load config defaults (port 8388, studio-specific paths)
    json configDefaults = loadSAM3ConfigDefaults();

    // Pass config so BasePlugin::describeCommonParameters can set correct defaults
    SAM3SegmentationPlugin::describeInContext(desc, context, &configDefaults);
}

OFX::ImageEffect* SAM3SegmentationPluginFactory::createInstance(OfxImageEffectHandle handle,
                                                                  OFX::ContextEnum /*context*/)
{
    return new SAM3SegmentationPlugin(handle);
}

} // namespace ComfyUI

// ============================================================================
// Plugin Registration
// ============================================================================

namespace OFX {
namespace Plugin {

void getPluginIDs(OFX::PluginFactoryArray &ids)
{
    static ComfyUI::SAM3SegmentationPluginFactory factory;
    ids.push_back(&factory);
}

} // namespace Plugin
} // namespace OFX
