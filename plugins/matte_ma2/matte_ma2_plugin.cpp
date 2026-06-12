// Copyright OpenFX and contributors to the OpenFX project.
// SPDX-License-Identifier: BSD-3-Clause

#include "matte_ma2_plugin.h"
#include <sstream>
#include <fstream>

namespace ComfyUI {

// ============================================================================
// MatteMA2Plugin Implementation
// ============================================================================

MatteMA2Plugin::MatteMA2Plugin(OfxImageEffectHandle handle)
    : BasePlugin(handle)
    , _textPrompt(nullptr), _sam3CkptName(nullptr), _objectIndices(nullptr)
    , _frameSelect(nullptr), _detectionThreshold(nullptr), _maxObjects(nullptr)
    , _detectInterval(nullptr), _refineIterations(nullptr), _individualMasks(nullptr)
    , _maskFrame(nullptr), _nWarmup(nullptr), _rErode(nullptr), _rDilate(nullptr)
    , _maxInternalSize(nullptr), _maxMemFrames(nullptr), _useLongTerm(nullptr)
    , _imageLoadCap(nullptr)
{
    _textPrompt         = fetchStringParam("textPrompt");
    _sam3CkptName       = fetchStringParam("sam3CkptName");
    _objectIndices      = fetchStringParam("objectIndices");
    _frameSelect        = fetchIntParam("frameSelect");
    _detectionThreshold = fetchDoubleParam("detectionThreshold");
    _maxObjects         = fetchIntParam("maxObjects");
    _detectInterval     = fetchIntParam("detectInterval");
    _refineIterations   = fetchIntParam("refineIterations");
    _individualMasks    = fetchBooleanParam("individualMasks");

    _maskFrame          = fetchIntParam("maskFrame");
    _nWarmup            = fetchIntParam("nWarmup");
    _rErode             = fetchIntParam("rErode");
    _rDilate            = fetchIntParam("rDilate");
    _maxInternalSize    = fetchIntParam("maxInternalSize");
    _maxMemFrames       = fetchIntParam("maxMemFrames");
    _useLongTerm        = fetchBooleanParam("useLongTerm");
    _imageLoadCap       = fetchIntParam("imageLoadCap");
}

MatteMA2Plugin::~MatteMA2Plugin() {}

int MatteMA2Plugin::getImageLoadCap() const {
    int cap = 0;
    if (_imageLoadCap) _imageLoadCap->getValue(cap);
    return cap;
}

// ============================================================================
// Workflow building
// ============================================================================

json MatteMA2Plugin::buildWorkflow(int frame,
                                    const std::map<std::string, std::string>& inputPaths)
{
    if (_logger) _logger->info("Building MatteMA2 workflow for frame {}", frame);

    std::string inputPath;
    auto it = inputPaths.find("InputA");
    if (it != inputPaths.end()) {
        inputPath = it->second;
    } else if (!inputPaths.empty()) {
        inputPath = inputPaths.begin()->second;
    } else {
        throw std::runtime_error("No input path provided for MatteMA2 workflow");
    }

    std::string workflowPath;
    _workflowFilePath->getValue(workflowPath);

    if (!workflowPath.empty()) {
        try {
            std::string resolvedPath = resolveWorkflowPath(workflowPath);
            if (!resolvedPath.empty()) {
                json baseWorkflow = loadWorkflowFromFile(resolvedPath);
                json customized   = customizeWorkflow(baseWorkflow, frame, inputPaths);
                json final        = customizeWorkflowWithParams(customized, frame);
                if (_logger) _logger->info("Loaded workflow from file: {}", resolvedPath);
                return final;
            }
        } catch (const std::exception& e) {
            if (_logger) _logger->error("Failed to load workflow from file: {}. Falling back.", e.what());
        }
    }

    return buildHardcodedWorkflow(frame, inputPath);
}

json MatteMA2Plugin::buildHardcodedWorkflow(int frame, const std::string& inputPath)
{
    if (_logger) _logger->info("Building hardcoded MatteMA2 workflow");

    // SAM3 params
    std::string textPrompt, sam3CkptName, objectIndices;
    int    frameSelect, maxObjects, detectInterval, refineIterations;
    double detectionThreshold;
    bool   individualMasks;

    _textPrompt->getValue(textPrompt);
    _sam3CkptName->getValue(sam3CkptName);
    _objectIndices->getValue(objectIndices);
    _frameSelect->getValue(frameSelect);
    _detectionThreshold->getValue(detectionThreshold);
    _maxObjects->getValue(maxObjects);
    _detectInterval->getValue(detectInterval);
    _refineIterations->getValue(refineIterations);
    _individualMasks->getValue(individualMasks);

    // MatAnyone2 params
    int  maskFrame, nWarmup, rErode, rDilate, maxInternalSize, maxMemFrames, imageLoadCap;
    bool useLongTerm;

    _maskFrame->getValue(maskFrame);
    _nWarmup->getValue(nWarmup);
    _rErode->getValue(rErode);
    _rDilate->getValue(rDilate);
    _maxInternalSize->getValue(maxInternalSize);
    _maxMemFrames->getValue(maxMemFrames);
    _useLongTerm->getValue(useLongTerm);
    _imageLoadCap->getValue(imageLoadCap);

    // Output path
    std::string mountPath, project, workflow_name, version;
    mountPath     = getLocalMountPath();
    project       = getTrimmedStringParam(_projectName);
    workflow_name = getTrimmedStringParam(_workflowName);
    version       = getTrimmedStringParam(_outputVersion);

    std::string basename = getEffectiveBasename();
    std::ostringstream outputPrefix;
    outputPrefix << mountPath << "/out/" << project << "/" << workflow_name
                 << "/" << version << "/" << basename;

    std::string comfyInputPath    = convertPathForComfyUI(inputPath);
    std::string comfyOutputPrefix = convertPathForComfyUI(outputPrefix.str());

    if (_logger) {
        _logger->info("MatteMA2 parameters:");
        _logger->info("  Text prompt: {}", textPrompt);
        _logger->info("  SAM3 ckpt: {}", sam3CkptName);
        _logger->info("  Object indices: {}", objectIndices);
        _logger->info("  Frame select: {}", frameSelect);
        _logger->info("  Detection threshold: {}", detectionThreshold);
        _logger->info("  Max objects: {}", maxObjects);
        _logger->info("  Detect interval: {}", detectInterval);
        _logger->info("  Refine iterations: {}", refineIterations);
        _logger->info("  Individual masks: {}", individualMasks);
        _logger->info("  MatAnyone2 load cap: {}", imageLoadCap);
        _logger->info("  mask_frame: {}", maskFrame);
        _logger->info("  n_warmup: {}", nWarmup);
        _logger->info("  r_erode/r_dilate: {}/{}", rErode, rDilate);
        _logger->info("  Input (ComfyUI): {}", comfyInputPath);
        _logger->info("  Output prefix (ComfyUI): {}", comfyOutputPrefix);
    }

    json workflow = {

        // Node 171: LoadEXR full sequence
        {"171", {
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

        // Node 179: Frame Select — picks integer reference frame
        {"179", {
            {"inputs", {
                {"select", frameSelect},
                {"frames", json::array({"171", 0})}
            }},
            {"class_type", "Frame Select"},
            {"_meta", {{"title", "Frame Select"}}}
        }},

        // Node 183: CheckpointLoaderSimple (SAM3 checkpoint)
        {"183", {
            {"inputs", {
                {"ckpt_name", sam3CkptName}
            }},
            {"class_type", "CheckpointLoaderSimple"},
            {"_meta", {{"title", "Load Checkpoint"}}}
        }},

        // Node 184: CLIPTextEncode (text prompt)
        {"184", {
            {"inputs", {
                {"text", textPrompt},
                {"clip", json::array({"183", 1})}
            }},
            {"class_type", "CLIPTextEncode"},
            {"_meta", {{"title", "CLIP Text Encode (Prompt)"}}}
        }},

        // Node 187: SAM3_Detect — initial detection on reference frame
        {"187", {
            {"inputs", {
                {"threshold", detectionThreshold},
                {"refine_iterations", refineIterations},
                {"individual_masks", individualMasks},
                {"model",        json::array({"183", 0})},
                {"image",        json::array({"179", 0})},
                {"conditioning", json::array({"184", 0})}
            }},
            {"class_type", "SAM3_Detect"},
            {"_meta", {{"title", "SAM3 Detect"}}}
        }},

        // Node 186: SAM3_VideoTrack — track on the single reference frame
        // from Frame Select (179), NOT the full sequence. MatAnyone2 then
        // takes the resulting single-frame mask as its propagation seed.
        // Wiring images to ["171",0] (the full video) would feed MatAnyone2
        // a T-frame mask instead of a seed and trigger a 5-vs-6-dim
        // tensor mismatch deep inside MatAnyone2's mask encoder.
        {"186", {
            {"inputs", {
                {"detection_threshold", detectionThreshold},
                {"max_objects", maxObjects},
                {"detect_interval", detectInterval},
                {"images",       json::array({"179", 0})},
                {"model",        json::array({"183", 0})},
                {"initial_mask", json::array({"187", 0})},
                {"conditioning", json::array({"184", 0})}
            }},
            {"class_type", "SAM3_VideoTrack"},
            {"_meta", {{"title", "SAM3 Video Track"}}}
        }},

        // Node 185: SAM3_TrackToMask — convert track data to mask
        {"185", {
            {"inputs", {
                {"object_indices", objectIndices},
                {"track_data",     json::array({"186", 0})}
            }},
            {"class_type", "SAM3_TrackToMask"},
            {"_meta", {{"title", "SAM3 Track to Mask"}}}
        }},

        // Node 166: MatAnyone2 matting
        {"166", {
            {"inputs", {
                {"mask_frame", maskFrame},
                {"n_warmup", nWarmup},
                {"r_erode", rErode},
                {"r_dilate", rDilate},
                {"max_internal_size", maxInternalSize},
                {"max_mem_frames", maxMemFrames},
                {"use_long_term", useLongTerm},
                {"src_video",       json::array({"171", 0})},
                {"foreground_MASK", json::array({"185", 0})}
            }},
            {"class_type", "MatAnyone2"},
            {"_meta", {{"title", "MatAnyone2"}}}
        }},

        // Node 172: Save matte as EXR
        {"172", {
            {"inputs", {
                {"filename_prefix", comfyOutputPrefix},
                {"tonemap", "linear"},
                {"version", 0},
                {"start_frame", frame},
                {"frame_pad", 4},
                {"save_workflow", "none"},
                {"create_path_if_missing", true},
                {"images", json::array({"166", 0})}
            }},
            {"class_type", "SaveEXR"},
            {"_meta", {{"title", "SAVE_MATTE"}}}
        }}
    };

    if (_logger) {
        _logger->info("=== MATTE MA2 WORKFLOW DUMP ===");
        _logger->info("{}", workflow.dump(2));
        _logger->info("=== END WORKFLOW DUMP ===");
    }

    return workflow;
}

json MatteMA2Plugin::customizeWorkflowWithParams(const json& baseWorkflow, int frame)
{
    if (_logger) _logger->debug("Customizing MatteMA2 workflow with parameters");

    std::string textPrompt, sam3CkptName, objectIndices;
    int    frameSelect, maxObjects, detectInterval, refineIterations;
    double detectionThreshold;
    bool   individualMasks;

    _textPrompt->getValue(textPrompt);
    _sam3CkptName->getValue(sam3CkptName);
    _objectIndices->getValue(objectIndices);
    _frameSelect->getValue(frameSelect);
    _detectionThreshold->getValue(detectionThreshold);
    _maxObjects->getValue(maxObjects);
    _detectInterval->getValue(detectInterval);
    _refineIterations->getValue(refineIterations);
    _individualMasks->getValue(individualMasks);

    int  maskFrame, nWarmup, rErode, rDilate, maxInternalSize, maxMemFrames, imageLoadCap;
    bool useLongTerm;

    _maskFrame->getValue(maskFrame);
    _nWarmup->getValue(nWarmup);
    _rErode->getValue(rErode);
    _rDilate->getValue(rDilate);
    _maxInternalSize->getValue(maxInternalSize);
    _maxMemFrames->getValue(maxMemFrames);
    _useLongTerm->getValue(useLongTerm);
    _imageLoadCap->getValue(imageLoadCap);

    std::string workflowStr = baseWorkflow.dump();

    auto replaceStr = [&](const std::string& placeholder, const std::string& value) {
        size_t pos = 0;
        while ((pos = workflowStr.find(placeholder, pos)) != std::string::npos) {
            workflowStr.replace(pos, placeholder.length(), value);
            pos += value.length();
        }
    };

    auto replaceNumeric = [&](const std::string& quotedPlaceholder, const std::string& numStr) {
        size_t pos = 0;
        while ((pos = workflowStr.find(quotedPlaceholder, pos)) != std::string::npos) {
            workflowStr.replace(pos, quotedPlaceholder.length(), numStr);
            pos += numStr.length();
        }
    };

    // String substitutions
    replaceStr("${TEXT_PROMPT}",     textPrompt);
    replaceStr("${SAM3_CKPT_NAME}",  sam3CkptName);
    replaceStr("${OBJECT_INDICES}",  objectIndices);

    // Numeric / bool substitutions (replace quoted placeholder with raw literal)
    replaceNumeric("\"${FRAME_SELECT}\"",        std::to_string(frameSelect));
    replaceNumeric("\"${DETECTION_THRESHOLD}\"", std::to_string(detectionThreshold));
    replaceNumeric("\"${MAX_OBJECTS}\"",         std::to_string(maxObjects));
    replaceNumeric("\"${DETECT_INTERVAL}\"",     std::to_string(detectInterval));
    replaceNumeric("\"${REFINE_ITERATIONS}\"",   std::to_string(refineIterations));
    replaceNumeric("\"${INDIVIDUAL_MASKS}\"",    individualMasks ? "true" : "false");

    replaceNumeric("\"${MASK_FRAME}\"",        std::to_string(maskFrame));
    replaceNumeric("\"${N_WARMUP}\"",          std::to_string(nWarmup));
    replaceNumeric("\"${R_ERODE}\"",           std::to_string(rErode));
    replaceNumeric("\"${R_DILATE}\"",          std::to_string(rDilate));
    replaceNumeric("\"${MAX_INTERNAL_SIZE}\"", std::to_string(maxInternalSize));
    replaceNumeric("\"${MAX_MEM_FRAMES}\"",    std::to_string(maxMemFrames));
    replaceNumeric("\"${IMAGE_LOAD_CAP}\"",    std::to_string(imageLoadCap));
    replaceNumeric("\"${USE_LONG_TERM}\"",     useLongTerm ? "true" : "false");

    try {
        json finalWorkflow = json::parse(workflowStr);
        if (_logger) {
            _logger->info("=== MATTE MA2 FINAL WORKFLOW ===");
            _logger->info("{}", finalWorkflow.dump(2));
            _logger->info("=== END FINAL WORKFLOW ===");
        }
        return finalWorkflow;
    } catch (const json::exception& e) {
        if (_logger) _logger->error("Failed to parse customized MatteMA2 workflow: {}", e.what());
        throw std::runtime_error("Failed to parse MatteMA2 customized workflow: " + std::string(e.what()));
    }
}

std::vector<std::string> MatteMA2Plugin::getRequiredModels()
{
    std::string ckpt;
    _sam3CkptName->getValue(ckpt);
    return {ckpt};
}

// ============================================================================
// Parameter Definitions
// ============================================================================

void MatteMA2Plugin::describeInContext(OFX::ImageEffectDescriptor &desc,
                                        OFX::ContextEnum context,
                                        const json* configDefaults)
{
    OFX::PageParamDescriptor *page = desc.definePageParam("Controls");

    // ---- SAM3 SEGMENTATION GROUP ----
    OFX::GroupParamDescriptor *sam3Group = desc.defineGroupParam("sam3Group");
    sam3Group->setLabel("SAM3 Segmentation");
    sam3Group->setOpen(true);
    page->addChild(*sam3Group);

    OFX::StringParamDescriptor *textPrompt = desc.defineStringParam("textPrompt");
    textPrompt->setLabel("Prompt");
    textPrompt->setHint("Text description of the subject to segment (e.g. 'person', 'hair', 'girl').");
    textPrompt->setDefault("foreground");
    textPrompt->setAnimates(false);
    textPrompt->setParent(*sam3Group);
    page->addChild(*textPrompt);

    OFX::StringParamDescriptor *objectIndices = desc.defineStringParam("objectIndices");
    objectIndices->setLabel("Object Indices");
    objectIndices->setHint("Comma-separated detected object indices to keep.");
    objectIndices->setDefault("0");
    objectIndices->setAnimates(false);
    objectIndices->setParent(*sam3Group);
    page->addChild(*objectIndices);

    OFX::IntParamDescriptor *frameSelect = desc.defineIntParam("frameSelect");
    frameSelect->setLabel("Reference Frame");
    frameSelect->setHint("Integer index of the frame fed to SAM3_Detect.");
    frameSelect->setDefault(0);
    frameSelect->setRange(0, 9999);
    frameSelect->setDisplayRange(0, 200);
    frameSelect->setAnimates(false);
    frameSelect->setParent(*sam3Group);
    page->addChild(*frameSelect);

    OFX::DoubleParamDescriptor *detectionThreshold = desc.defineDoubleParam("detectionThreshold");
    detectionThreshold->setLabel("Detection Threshold");
    detectionThreshold->setHint("SAM3 detection confidence threshold (used by both SAM3_Detect and SAM3_VideoTrack).");
    detectionThreshold->setDefault(0.5);
    detectionThreshold->setRange(0.0, 1.0);
    detectionThreshold->setDisplayRange(0.0, 1.0);
    detectionThreshold->setParent(*sam3Group);
    page->addChild(*detectionThreshold);

    OFX::IntParamDescriptor *maxObjects = desc.defineIntParam("maxObjects");
    maxObjects->setLabel("Max Objects");
    maxObjects->setDefault(0);
    maxObjects->setRange(0, 32);
    maxObjects->setDisplayRange(0, 16);
    maxObjects->setAnimates(false);
    maxObjects->setParent(*sam3Group);
    page->addChild(*maxObjects);

    OFX::IntParamDescriptor *detectInterval = desc.defineIntParam("detectInterval");
    detectInterval->setLabel("Detect Interval");
    detectInterval->setDefault(1);
    detectInterval->setRange(1, 64);
    detectInterval->setDisplayRange(1, 16);
    detectInterval->setAnimates(false);
    detectInterval->setParent(*sam3Group);
    page->addChild(*detectInterval);

    OFX::IntParamDescriptor *refineIterations = desc.defineIntParam("refineIterations");
    refineIterations->setLabel("Refine Iterations");
    refineIterations->setDefault(2);
    refineIterations->setRange(0, 16);
    refineIterations->setDisplayRange(0, 16);
    refineIterations->setAnimates(false);
    refineIterations->setParent(*sam3Group);
    page->addChild(*refineIterations);

    OFX::BooleanParamDescriptor *individualMasks = desc.defineBooleanParam("individualMasks");
    individualMasks->setLabel("Individual Masks");
    individualMasks->setDefault(false);
    individualMasks->setAnimates(false);
    individualMasks->setParent(*sam3Group);
    page->addChild(*individualMasks);

    // ---- MATANYONE2 GROUP ----
    OFX::GroupParamDescriptor *ma2Group = desc.defineGroupParam("ma2Group");
    ma2Group->setLabel("MatAnyone2");
    ma2Group->setOpen(true);
    page->addChild(*ma2Group);

    OFX::IntParamDescriptor *imageLoadCap = desc.defineIntParam("imageLoadCap");
    imageLoadCap->setLabel("Frame Limit");
    imageLoadCap->setHint("Maximum frames loaded for MatAnyone2. 0 = no limit.");
    imageLoadCap->setDefault(100);
    imageLoadCap->setRange(0, 4096);
    imageLoadCap->setDisplayRange(0, 500);
    imageLoadCap->setAnimates(false);
    imageLoadCap->setParent(*ma2Group);
    page->addChild(*imageLoadCap);

    OFX::IntParamDescriptor *maskFrame = desc.defineIntParam("maskFrame");
    maskFrame->setLabel("Mask Frame");
    maskFrame->setHint("Index of the frame whose SAM3 mask is used as the MatAnyone2 reference.");
    maskFrame->setDefault(1);
    maskFrame->setRange(0, 9999);
    maskFrame->setDisplayRange(0, 200);
    maskFrame->setAnimates(false);
    maskFrame->setParent(*ma2Group);
    page->addChild(*maskFrame);

    OFX::IntParamDescriptor *nWarmup = desc.defineIntParam("nWarmup");
    nWarmup->setLabel("Warmup Frames");
    nWarmup->setHint("Number of warmup frames to process before the main sequence. "
                      "Helps initialise the memory network for smoother mattes.");
    nWarmup->setDefault(10);
    nWarmup->setRange(0, 100);
    nWarmup->setDisplayRange(0, 50);
    nWarmup->setAnimates(false);
    nWarmup->setParent(*ma2Group);
    page->addChild(*nWarmup);

    OFX::IntParamDescriptor *rErode = desc.defineIntParam("rErode");
    rErode->setLabel("Erode Radius");
    rErode->setHint("Morphological erosion radius applied to the input mask. "
                     "Shrinks the mask boundary. 0 = no erosion.");
    rErode->setDefault(0);
    rErode->setRange(0, 50);
    rErode->setDisplayRange(0, 20);
    rErode->setAnimates(false);
    rErode->setParent(*ma2Group);
    page->addChild(*rErode);

    OFX::IntParamDescriptor *rDilate = desc.defineIntParam("rDilate");
    rDilate->setLabel("Dilate Radius");
    rDilate->setHint("Morphological dilation radius applied to the input mask. "
                      "Expands the mask boundary. 0 = no dilation.");
    rDilate->setDefault(0);
    rDilate->setRange(0, 50);
    rDilate->setDisplayRange(0, 20);
    rDilate->setAnimates(false);
    rDilate->setParent(*ma2Group);
    page->addChild(*rDilate);

    OFX::IntParamDescriptor *maxInternalSize = desc.defineIntParam("maxInternalSize");
    maxInternalSize->setLabel("Max Internal Size");
    maxInternalSize->setHint("Maximum resolution for internal processing. -1 = no limit (use input resolution).");
    maxInternalSize->setDefault(-1);
    maxInternalSize->setRange(-1, 4096);
    maxInternalSize->setDisplayRange(-1, 2048);
    maxInternalSize->setAnimates(false);
    maxInternalSize->setParent(*ma2Group);
    page->addChild(*maxInternalSize);

    OFX::IntParamDescriptor *maxMemFrames = desc.defineIntParam("maxMemFrames");
    maxMemFrames->setLabel("Memory Frames");
    maxMemFrames->setHint("Number of frames kept in the short-term memory buffer. "
                           "More frames = better temporal consistency, more VRAM.");
    maxMemFrames->setDefault(5);
    maxMemFrames->setRange(1, 64);
    maxMemFrames->setDisplayRange(1, 32);
    maxMemFrames->setAnimates(false);
    maxMemFrames->setParent(*ma2Group);
    page->addChild(*maxMemFrames);

    OFX::BooleanParamDescriptor *useLongTerm = desc.defineBooleanParam("useLongTerm");
    useLongTerm->setLabel("Long-Term Memory");
    useLongTerm->setHint("Enable long-term memory for better consistency over longer sequences.");
    useLongTerm->setDefault(true);
    useLongTerm->setAnimates(false);
    useLongTerm->setParent(*ma2Group);
    page->addChild(*useLongTerm);

    // ---- MODEL GROUP ----
    OFX::GroupParamDescriptor *modelGroup = desc.defineGroupParam("ma2ModelGroup");
    modelGroup->setLabel("Model");
    modelGroup->setOpen(false);
    page->addChild(*modelGroup);

    OFX::StringParamDescriptor *sam3CkptName = desc.defineStringParam("sam3CkptName");
    sam3CkptName->setLabel("SAM3 Checkpoint");
    sam3CkptName->setHint("Checkpoint filename in ComfyUI/models/checkpoints.");
    sam3CkptName->setDefault("sam3.1_multiplex_fp16.safetensors");
    sam3CkptName->setAnimates(false);
    sam3CkptName->setParent(*modelGroup);
    page->addChild(*sam3CkptName);

    BasePlugin::describeCommonParameters(desc, context, page, page, page, configDefaults,
                                         /*skipGroupHeaders=*/false, /*isSequencePlugin=*/true);

    OFX::StringParamDescriptor *workflow = desc.defineStringParam("workflowName");
    workflow->setDefault("matte_ma2");
}

// ============================================================================
// MatteMA2PluginFactory Implementation
// ============================================================================

MatteMA2PluginFactory::MatteMA2PluginFactory()
    : OFX::PluginFactoryHelper<MatteMA2PluginFactory>(
        "com.comfyui.MatteMA2",
        1,
        0
    )
{
}

json MatteMA2PluginFactory::loadConfigDefaults()
{
    // Search the MatteMA2 bundle across all platform OFX plugin locations.
    std::vector<std::string> searchPaths =
        getOfxConfigSearchPaths({"MatteMA2"});

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

void MatteMA2PluginFactory::describe(OFX::ImageEffectDescriptor &desc)
{
    desc.setLabel("ComfyUI MatAnyone V2");
    desc.setLabels("ComfyUI MatAnyone V2", "MatAnyone V2", "ComfyUI MatAnyone V2");
    desc.setPluginGrouping("ComfyUI");

    desc.setPluginDescription(
        "High-quality video matting using MatAnyone V2 + SAM3 via ComfyUI.\n\n"
        "Combines SAM3 text-prompted segmentation with MatAnyone2's memory-based "
        "video matting for temporally-consistent alpha mattes.\n\n"
        "Key difference from MatteMaMa: MatAnyone2 uses a recurrent memory network "
        "rather than diffusion — it is significantly faster and requires less VRAM "
        "while still producing high-quality results.\n\n"
        "Requires:\n"
        "- ComfyUI server with ComfyUI-MatAnyone and ComfyUI-SAM3 extensions\n"
        "- SAM3 checkpoint (e.g. sam3.1_multiplex_fp16.safetensors)\n"
        "- Shared network storage for image exchange\n\n"
        "Based on: https://github.com/kijai/ComfyUI-MatAnyone\n"
        "          https://github.com/PozzettiAndrea/ComfyUI-SAM3"
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

void MatteMA2PluginFactory::describeInContext(OFX::ImageEffectDescriptor &desc,
                                               OFX::ContextEnum context)
{
    OFX::ClipDescriptor *srcClip = desc.defineClip(kOfxImageEffectSimpleSourceClipName);
    srcClip->addSupportedComponent(OFX::ePixelComponentRGBA);
    srcClip->addSupportedComponent(OFX::ePixelComponentRGB);
    // Sequence plugin: render() collects all frames in [start..end] via fetchImage(t),
    // so the source clip must permit temporal access (effect-level was already true).
    srcClip->setTemporalClipAccess(true);
    srcClip->setSupportsTiles(false);
    srcClip->setIsMask(false);

    OFX::ClipDescriptor *dstClip = desc.defineClip(kOfxImageEffectOutputClipName);
    dstClip->addSupportedComponent(OFX::ePixelComponentRGBA);
    dstClip->addSupportedComponent(OFX::ePixelComponentRGB);
    dstClip->setSupportsTiles(false);

    json configDefaults = loadConfigDefaults();
    MatteMA2Plugin::describeInContext(desc, context, &configDefaults);
}

OFX::ImageEffect* MatteMA2PluginFactory::createInstance(OfxImageEffectHandle handle,
                                                          OFX::ContextEnum /*context*/)
{
    return new MatteMA2Plugin(handle);
}

} // namespace ComfyUI

// ============================================================================
// Plugin Registration
// ============================================================================

namespace OFX {
namespace Plugin {

void getPluginIDs(OFX::PluginFactoryArray &ids)
{
    static ComfyUI::MatteMA2PluginFactory factory;
    ids.push_back(&factory);
}

} // namespace Plugin
} // namespace OFX
