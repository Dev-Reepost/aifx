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
    , _batchIndex(nullptr)
    , _objectIndices(nullptr)
    , _maxObjects(nullptr)
    , _detectInterval(nullptr)
    , _refineIterations(nullptr)
    , _individualMasks(nullptr)
    , _imageLoadCap(nullptr)
    , _ckptName(nullptr)
{
    _textPrompt       = fetchStringParam("textPrompt");
    _scoreThreshold   = fetchDoubleParam("scoreThreshold");
    _batchIndex       = fetchIntParam("batchIndex");
    _objectIndices    = fetchStringParam("objectIndices");
    _maxObjects       = fetchIntParam("maxObjects");
    _detectInterval   = fetchIntParam("detectInterval");
    _refineIterations = fetchIntParam("refineIterations");
    _individualMasks  = fetchBooleanParam("individualMasks");
    _imageLoadCap     = fetchIntParam("imageLoadCap");
    _ckptName         = fetchStringParam("ckptName");
}

SAM3SegmentationPlugin::~SAM3SegmentationPlugin()
{
}

int SAM3SegmentationPlugin::getImageLoadCap() const {
    int cap = 0;
    if (_imageLoadCap) _imageLoadCap->getValue(cap);
    return cap;
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

    int batchIndex;
    _batchIndex->getValue(batchIndex);

    std::string objectIndices;
    _objectIndices->getValue(objectIndices);

    int maxObjects;
    _maxObjects->getValue(maxObjects);

    int detectInterval;
    _detectInterval->getValue(detectInterval);

    int refineIterations;
    _refineIterations->getValue(refineIterations);

    bool individualMasks;
    _individualMasks->getValue(individualMasks);

    int imageLoadCap;
    _imageLoadCap->getValue(imageLoadCap);

    std::string ckptName;
    _ckptName->getValue(ckptName);

    // Build output path
    std::string mountPath, project, workflow_name, version;
    mountPath     = getLocalMountPath();
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
        _logger->info("  Batch index: {}", batchIndex);
        _logger->info("  Object indices: {}", objectIndices);
        _logger->info("  Max objects: {}", maxObjects);
        _logger->info("  Detect interval: {}", detectInterval);
        _logger->info("  Refine iterations: {}", refineIterations);
        _logger->info("  Individual masks: {}", individualMasks);
        _logger->info("  Image load cap: {}", imageLoadCap);
        _logger->info("  Checkpoint: {}", ckptName);
        _logger->info("  Input (ComfyUI): {}", comfyInputPath);
        _logger->info("  Output prefix (ComfyUI): {}", comfyOutputPrefix);
    }

    // Build workflow JSON matching segmentation_sam3.json structure
    json workflow = {

        // Node 2: Load SAM3.1 checkpoint
        {"2", {
            {"inputs", {
                {"ckpt_name", ckptName}
            }},
            {"class_type", "CheckpointLoaderSimple"},
            {"_meta", {{"title", "Load Checkpoint"}}}
        }},

        // Node 8: SAM3 Video Track (propagate across all frames)
        {"8", {
            {"inputs", {
                {"detection_threshold", scoreThreshold},
                {"max_objects", maxObjects},
                {"detect_interval", detectInterval},
                {"images", json::array({"24", 0})},
                {"model", json::array({"2", 0})},
                {"initial_mask", json::array({"13", 0})},
                {"conditioning", json::array({"14", 0})}
            }},
            {"class_type", "SAM3_VideoTrack"},
            {"_meta", {{"title", "SAM3 Video Track"}}}
        }},

        // Node 9: SAM3 Track to Mask (select objects of interest)
        {"9", {
            {"inputs", {
                {"object_indices", objectIndices},
                {"track_data", json::array({"8", 0})}
            }},
            {"class_type", "SAM3_TrackToMask"},
            {"_meta", {{"title", "SAM3 Track to Mask"}}}
        }},

        // Node 13: SAM3 Detect (initial detection on reference frame)
        {"13", {
            {"inputs", {
                {"threshold", scoreThreshold},
                {"refine_iterations", refineIterations},
                {"individual_masks", individualMasks},
                {"model", json::array({"2", 0})},
                {"image", json::array({"15", 0})},
                {"conditioning", json::array({"14", 0})}
            }},
            {"class_type", "SAM3_Detect"},
            {"_meta", {{"title", "SAM3 Detect"}}}
        }},

        // Node 14: CLIP Text Encode (prompt -> conditioning)
        {"14", {
            {"inputs", {
                {"text", prompt},
                {"clip", json::array({"2", 1})}
            }},
            {"class_type", "CLIPTextEncode"},
            {"_meta", {{"title", "CLIP Text Encode (Prompt)"}}}
        }},

        // Node 15: ImageFromBatch (pick reference frame from loaded batch)
        {"15", {
            {"inputs", {
                {"batch_index", batchIndex},
                {"length", 1},
                {"image", json::array({"24", 0})}
            }},
            {"class_type", "ImageFromBatch"},
            {"_meta", {{"title", "ImageFromBatch"}}}
        }},

        // Node 16: Convert mask to image
        {"16", {
            {"inputs", {
                {"mask", json::array({"9", 0})}
            }},
            {"class_type", "MaskToImage"},
            {"_meta", {{"title", "Convert Mask to Image"}}}
        }},

        // Node 23: Save EXR
        {"23", {
            {"inputs", {
                {"filename_prefix", comfyOutputPrefix},
                {"tonemap", "linear"},
                {"version", -1},
                {"start_frame", frame},
                {"frame_pad", 4},
                {"save_workflow", "none"},
                {"create_path_if_missing", true},
                {"images", json::array({"16", 0})}
            }},
            {"class_type", "SaveEXR"},
            {"_meta", {{"title", "Save EXR"}}}
        }},

        // Node 24: Load EXR sequence
        {"24", {
            {"inputs", {
                {"filepath", comfyInputPath},
                {"tonemap", "linear"},
                {"image_load_cap", imageLoadCap},
                {"skip_first_images", 0},
                {"select_every_nth", 1}
            }},
            {"class_type", "LoadEXR"},
            {"_meta", {{"title", "Load EXR"}}}
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

    int batchIndex;
    _batchIndex->getValue(batchIndex);

    std::string objectIndices;
    _objectIndices->getValue(objectIndices);

    int maxObjects;
    _maxObjects->getValue(maxObjects);

    int detectInterval;
    _detectInterval->getValue(detectInterval);

    int refineIterations;
    _refineIterations->getValue(refineIterations);

    bool individualMasks;
    _individualMasks->getValue(individualMasks);

    int imageLoadCap;
    _imageLoadCap->getValue(imageLoadCap);

    std::string ckptName;
    _ckptName->getValue(ckptName);

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
        // Remove surrounding quotes so the JSON value becomes a number, not a string/bool
        size_t pos = 0;
        while ((pos = workflowStr.find(quotedPlaceholder, pos)) != std::string::npos) {
            workflowStr.replace(pos, quotedPlaceholder.length(), numStr);
            pos += numStr.length();
        }
    };

    replaceStr("${PROMPT}",         prompt);
    replaceStr("${CKPT_NAME}",      ckptName);
    replaceStr("${OBJECT_INDICES}", objectIndices);

    replaceNumeric("\"${SCORE_THRESHOLD}\"",   std::to_string(scoreThreshold));
    replaceNumeric("\"${BATCH_INDEX}\"",       std::to_string(batchIndex));
    replaceNumeric("\"${MAX_OBJECTS}\"",       std::to_string(maxObjects));
    replaceNumeric("\"${DETECT_INTERVAL}\"",   std::to_string(detectInterval));
    replaceNumeric("\"${REFINE_ITERATIONS}\"", std::to_string(refineIterations));
    replaceNumeric("\"${INDIVIDUAL_MASKS}\"",  individualMasks ? "true" : "false");
    replaceNumeric("\"${IMAGE_LOAD_CAP}\"",    std::to_string(imageLoadCap));
    // Note: ${FRAME}, ${INPUT_PATH}, ${OUTPUT_PREFIX} are handled by the base class customizeWorkflow()

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
    std::string ckptName;
    _ckptName->getValue(ckptName);
    return {ckptName};
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

    // Reference frame index (in the loaded batch)
    OFX::IntParamDescriptor *batchIndex = desc.defineIntParam("batchIndex");
    batchIndex->setLabel("Reference Frame");
    batchIndex->setHint("Index of the frame in the loaded EXR batch to use as the reference frame for initial SAM3 detection.");
    batchIndex->setDefault(0);
    batchIndex->setRange(0, 1023);
    batchIndex->setDisplayRange(0, 50);
    batchIndex->setAnimates(false);
    batchIndex->setParent(*samGroup);
    page->addChild(*batchIndex);

    // Object indices (comma-separated list)
    OFX::StringParamDescriptor *objectIndices = desc.defineStringParam("objectIndices");
    objectIndices->setLabel("Object Indices");
    objectIndices->setHint("Comma-separated list of detected object indices to keep in the mask (e.g. '0' or '0,2,5').");
    objectIndices->setDefault("0");
    objectIndices->setAnimates(false);
    objectIndices->setParent(*samGroup);
    page->addChild(*objectIndices);

    // Max objects
    OFX::IntParamDescriptor *maxObjects = desc.defineIntParam("maxObjects");
    maxObjects->setLabel("Max Objects");
    maxObjects->setHint("Maximum number of detected objects to track (0 = unlimited).");
    maxObjects->setDefault(0);
    maxObjects->setRange(0, 100);
    maxObjects->setDisplayRange(0, 20);
    maxObjects->setAnimates(false);
    maxObjects->setParent(*samGroup);
    page->addChild(*maxObjects);

    // Detect interval
    OFX::IntParamDescriptor *detectInterval = desc.defineIntParam("detectInterval");
    detectInterval->setLabel("Detect Interval");
    detectInterval->setHint("Run SAM3 detection every N frames; intermediate frames are tracked.");
    detectInterval->setDefault(1);
    detectInterval->setRange(1, 100);
    detectInterval->setDisplayRange(1, 30);
    detectInterval->setAnimates(false);
    detectInterval->setParent(*samGroup);
    page->addChild(*detectInterval);

    // Refine iterations
    OFX::IntParamDescriptor *refineIterations = desc.defineIntParam("refineIterations");
    refineIterations->setLabel("Refine Iterations");
    refineIterations->setHint("Number of mask refinement iterations on the reference frame.");
    refineIterations->setDefault(2);
    refineIterations->setRange(0, 10);
    refineIterations->setDisplayRange(0, 10);
    refineIterations->setAnimates(false);
    refineIterations->setParent(*samGroup);
    page->addChild(*refineIterations);

    // Individual masks
    OFX::BooleanParamDescriptor *individualMasks = desc.defineBooleanParam("individualMasks");
    individualMasks->setLabel("Individual Masks");
    individualMasks->setHint("Generate separate masks per detected object instead of a combined mask.");
    individualMasks->setDefault(false);
    individualMasks->setParent(*samGroup);
    page->addChild(*individualMasks);

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

    // SAM3.1 checkpoint name (CheckpointLoaderSimple)
    OFX::StringParamDescriptor *ckptName = desc.defineStringParam("ckptName");
    ckptName->setLabel("Checkpoint");
    ckptName->setHint("SAM3.1 checkpoint filename, relative to ComfyUI's models/checkpoints directory.");
    ckptName->setDefault("sam3.1_multiplex_fp16.safetensors");
    ckptName->setAnimates(false);
    ckptName->setParent(*modelGroup);
    page->addChild(*ckptName);

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
    // Search the SegmentationSAM3 bundle across all platform OFX plugin locations.
    std::vector<std::string> searchPaths =
        getOfxConfigSearchPaths({"SegmentationSAM3"});

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
        "- SAM3 checkpoint (gated: run 'hf auth login', accept the terms at\n"
        "  huggingface.co/facebook/sam3, then place it in ComfyUI/models/checkpoints/)\n"
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

    // Load config defaults (server address, port, mount paths)
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
