// Copyright OpenFX and contributors to the OpenFX project.
// SPDX-License-Identifier: BSD-3-Clause

#include "matte_ma2_plugin.h"
#include <sstream>
#include <fstream>

namespace ComfyUI {

static const char* kDirections[] = { "forward", "backward", "both", nullptr };

// ============================================================================
// MatteMA2Plugin Implementation
// ============================================================================

MatteMA2Plugin::MatteMA2Plugin(OfxImageEffectHandle handle)
    : BasePlugin(handle)
    , _textPrompt(nullptr), _scoreThreshold(nullptr), _frameIdx(nullptr)
    , _direction(nullptr), _plotAllMasks(nullptr), _objId(nullptr)
    , _sam3ModelPath(nullptr), _offloadSam3Model(nullptr), _sam3ImageLoadCap(nullptr)
    , _maskFrame(nullptr), _nWarmup(nullptr), _rErode(nullptr), _rDilate(nullptr)
    , _maxInternalSize(nullptr), _maxMemFrames(nullptr), _useLongTerm(nullptr)
    , _imageLoadCap(nullptr)
{
    _textPrompt        = fetchStringParam("textPrompt");
    _scoreThreshold    = fetchDoubleParam("scoreThreshold");
    _frameIdx          = fetchDoubleParam("frameIdx");
    _direction         = fetchChoiceParam("direction");
    _plotAllMasks      = fetchBooleanParam("plotAllMasks");
    _objId             = fetchIntParam("objId");
    _sam3ModelPath     = fetchStringParam("sam3ModelPath");
    _offloadSam3Model  = fetchBooleanParam("offloadSam3Model");
    _sam3ImageLoadCap  = fetchIntParam("sam3ImageLoadCap");

    _maskFrame         = fetchIntParam("maskFrame");
    _nWarmup           = fetchIntParam("nWarmup");
    _rErode            = fetchIntParam("rErode");
    _rDilate           = fetchIntParam("rDilate");
    _maxInternalSize   = fetchIntParam("maxInternalSize");
    _maxMemFrames      = fetchIntParam("maxMemFrames");
    _useLongTerm       = fetchBooleanParam("useLongTerm");
    _imageLoadCap      = fetchIntParam("imageLoadCap");
}

MatteMA2Plugin::~MatteMA2Plugin() {}

int MatteMA2Plugin::getImageLoadCap() const {
    int cap = 0;
    if (_imageLoadCap) _imageLoadCap->getValue(cap);
    return cap;
}

void MatteMA2Plugin::changedParam(const OFX::InstanceChangedArgs &args,
                                   const std::string &paramName)
{
    BasePlugin::changedParam(args, paramName);

    if (paramName == "plotAllMasks") {
        bool plotAll;
        _plotAllMasks->getValueAtTime(args.time, plotAll);
        _objId->setEnabled(!plotAll);
        _objId->setIsSecret(plotAll);
    }
}

std::string MatteMA2Plugin::getDirectionName() const
{
    int idx; _direction->getValue(idx);
    if (idx >= 0 && kDirections[idx]) return kDirections[idx];
    return "forward";
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
    std::string textPrompt, sam3ModelPath, direction;
    double scoreThreshold, frameIdx;
    int objId, sam3ImageLoadCap;
    bool plotAllMasks, offloadSam3Model;

    _textPrompt->getValue(textPrompt);
    _scoreThreshold->getValue(scoreThreshold);
    _frameIdx->getValue(frameIdx);
    direction = getDirectionName();
    _plotAllMasks->getValue(plotAllMasks);
    _objId->getValue(objId);
    _sam3ModelPath->getValue(sam3ModelPath);
    _offloadSam3Model->getValue(offloadSam3Model);
    _sam3ImageLoadCap->getValue(sam3ImageLoadCap);

    // MatAnyone2 params
    int maskFrame, nWarmup, rErode, rDilate, maxInternalSize, maxMemFrames, imageLoadCap;
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
    mountPath = getTrimmedStringParam(_macMountPath);
    project = getTrimmedStringParam(_projectName);
    workflow_name = getTrimmedStringParam(_workflowName);
    version = getTrimmedStringParam(_outputVersion);

    std::string basename = getEffectiveBasename();
    std::ostringstream outputPrefix;
    outputPrefix << mountPath << "/out/" << project << "/" << workflow_name
                 << "/" << version << "/" << basename;

    std::string comfyInputPath    = convertPathForComfyUI(inputPath);
    std::string comfyOutputPrefix = convertPathForComfyUI(outputPrefix.str());

    if (_logger) {
        _logger->info("MatteMA2 parameters:");
        _logger->info("  SAM3 prompt: {}", textPrompt);
        _logger->info("  SAM3 frame_idx: {}", frameIdx);
        _logger->info("  SAM3 load cap: {}", sam3ImageLoadCap);
        _logger->info("  MatAnyone2 load cap: {}", imageLoadCap);
        _logger->info("  mask_frame: {}", maskFrame);
        _logger->info("  n_warmup: {}", nWarmup);
        _logger->info("  r_erode/r_dilate: {}/{}", rErode, rDilate);
        _logger->info("  Input (ComfyUI): {}", comfyInputPath);
        _logger->info("  Output prefix (ComfyUI): {}", comfyOutputPrefix);
    }

    json workflow = {

        // Node 93: LoadEXR for SAM3 reference (small load cap)
        {"93", {
            {"inputs", {
                {"filepath", comfyInputPath},
                {"tonemap", "linear"},
                {"image_load_cap", sam3ImageLoadCap},
                {"skip_first_images", 0},
                {"select_every_nth", 1}
            }},
            {"class_type", "LoadEXR"},
            {"_meta", {{"title", "Load EXR (SAM3 reference)"}}}
        }},

        // Node 94: Load SAM3 model
        {"94", {
            {"inputs", {{"model_path", sam3ModelPath}}},
            {"class_type", "LoadSAM3Model"},
            {"_meta", {{"title", "(down)Load SAM3 Model"}}}
        }},

        // Node 91: SAM3 initial segmentation
        {"91", {
            {"inputs", {
                {"prompt_mode", "text"},
                {"text_prompt", textPrompt},
                {"frame_idx", frameIdx},
                {"score_threshold", scoreThreshold},
                {"video_frames", json::array({"93", 0})}
            }},
            {"class_type", "SAM3VideoSegmentation"},
            {"_meta", {{"title", "SAM3 Video Segmentation"}}}
        }},

        // Node 89: SAM3 propagate
        {"89", {
            {"inputs", {
                {"start_frame", 0},
                {"end_frame", -1},
                {"direction", direction},
                {"offload_model", offloadSam3Model},
                {"sam3_model",  json::array({"94", 0})},
                {"video_state", json::array({"91", 0})}
            }},
            {"class_type", "SAM3Propagate"},
            {"_meta", {{"title", "SAM3 Propagate"}}}
        }},

        // Node 88: SAM3 video output (mask)
        {"88", {
            {"inputs", {
                {"obj_id", objId},
                {"plot_all_masks", plotAllMasks},
                {"masks",       json::array({"89", 0})},
                {"video_state", json::array({"89", 2})},
                {"scores",      json::array({"89", 1})}
            }},
            {"class_type", "SAM3VideoOutput"},
            {"_meta", {{"title", "SAM3 Video Output"}}}
        }},

        // Node 171: LoadEXR full sequence for MatAnyone2
        {"171", {
            {"inputs", {
                {"filepath", comfyInputPath},
                {"tonemap", "linear"},
                {"image_load_cap", imageLoadCap},
                {"skip_first_images", 0},
                {"select_every_nth", 1}
            }},
            {"class_type", "LoadEXR"},
            {"_meta", {{"title", "Load EXR (MatAnyone2 full sequence)"}}}
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
                {"src_video",        json::array({"171", 0})},
                {"foreground_MASK",  json::array({"88",  0})}
            }},
            {"class_type", "MatAnyone2"},
            {"_meta", {{"title", "MatAnyone2"}}}
        }},

        // Node 172: Save matte as EXR
        {"172", {
            {"inputs", {
                {"filename_prefix", comfyOutputPrefix},
                {"tonemap", "linear"},
                {"version", -1},
                {"start_frame", frame},
                {"frame_pad", 4},
                {"save_workflow", "none"},
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

    std::string textPrompt, sam3ModelPath, direction;
    double scoreThreshold, frameIdx;
    int objId, sam3ImageLoadCap;
    bool plotAllMasks, offloadSam3Model;

    _textPrompt->getValue(textPrompt);
    _scoreThreshold->getValue(scoreThreshold);
    _frameIdx->getValue(frameIdx);
    direction = getDirectionName();
    _plotAllMasks->getValue(plotAllMasks);
    _objId->getValue(objId);
    _sam3ModelPath->getValue(sam3ModelPath);
    _offloadSam3Model->getValue(offloadSam3Model);
    _sam3ImageLoadCap->getValue(sam3ImageLoadCap);

    int maskFrame, nWarmup, rErode, rDilate, maxInternalSize, maxMemFrames, imageLoadCap;
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

    replaceStr("${TEXT_PROMPT}",     textPrompt);
    replaceStr("${DIRECTION}",       direction);
    replaceStr("${SAM3_MODEL_PATH}", sam3ModelPath);

    replaceNumeric("\"${SCORE_THRESHOLD}\"",    std::to_string(scoreThreshold));
    replaceNumeric("\"${FRAME_IDX}\"",          std::to_string(frameIdx));
    replaceNumeric("\"${OBJ_ID}\"",             std::to_string(objId));
    replaceNumeric("\"${SAM3_IMAGE_LOAD_CAP}\"",std::to_string(sam3ImageLoadCap));
    replaceNumeric("\"${PLOT_ALL_MASKS}\"",     plotAllMasks     ? "true" : "false");
    replaceNumeric("\"${OFFLOAD_SAM3_MODEL}\"", offloadSam3Model ? "true" : "false");

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
    std::string sam3;
    _sam3ModelPath->getValue(sam3);
    return {sam3};
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

    OFX::DoubleParamDescriptor *scoreThreshold = desc.defineDoubleParam("scoreThreshold");
    scoreThreshold->setLabel("Threshold");
    scoreThreshold->setHint("SAM3 detection confidence threshold (0–1).");
    scoreThreshold->setDefault(0.3);
    scoreThreshold->setRange(0.0, 1.0);
    scoreThreshold->setDisplayRange(0.0, 1.0);
    scoreThreshold->setParent(*sam3Group);
    page->addChild(*scoreThreshold);

    OFX::DoubleParamDescriptor *frameIdx = desc.defineDoubleParam("frameIdx");
    frameIdx->setLabel("Reference Frame");
    frameIdx->setHint("Fraction of the sequence used as the SAM3 reference frame "
                       "(0.0 = first frame, 1.0 = last frame).");
    frameIdx->setDefault(0.3);
    frameIdx->setRange(0.0, 1.0);
    frameIdx->setDisplayRange(0.0, 1.0);
    frameIdx->setAnimates(false);
    frameIdx->setParent(*sam3Group);
    page->addChild(*frameIdx);

    OFX::ChoiceParamDescriptor *direction = desc.defineChoiceParam("direction");
    direction->setLabel("Propagation Direction");
    direction->appendOption("Forward",  "From reference frame to end");
    direction->appendOption("Backward", "From reference frame to start");
    direction->appendOption("Both",     "Both directions");
    direction->setDefault(0);
    direction->setAnimates(false);
    direction->setParent(*sam3Group);
    page->addChild(*direction);

    OFX::BooleanParamDescriptor *plotAllMasks = desc.defineBooleanParam("plotAllMasks");
    plotAllMasks->setLabel("All Masks");
    plotAllMasks->setHint("Show all detected objects. Disable to isolate a single object by ID.");
    plotAllMasks->setDefault(true);
    plotAllMasks->setParent(*sam3Group);
    page->addChild(*plotAllMasks);

    OFX::IntParamDescriptor *objId = desc.defineIntParam("objId");
    objId->setLabel("Object ID");
    objId->setHint("Index of the object to extract when All Masks is disabled.");
    objId->setDefault(0);
    objId->setRange(0, 99);
    objId->setDisplayRange(0, 10);
    objId->setIsSecret(true);
    objId->setEnabled(false);
    objId->setParent(*sam3Group);
    page->addChild(*objId);

    OFX::IntParamDescriptor *sam3ImageLoadCap = desc.defineIntParam("sam3ImageLoadCap");
    sam3ImageLoadCap->setLabel("SAM3 Frame Limit");
    sam3ImageLoadCap->setHint("Frames loaded for SAM3 segmentation. "
                               "Usually 1 is enough — SAM3 only needs a reference frame. "
                               "MatAnyone2 loads the full sequence separately.");
    sam3ImageLoadCap->setDefault(1);
    sam3ImageLoadCap->setRange(1, 512);
    sam3ImageLoadCap->setDisplayRange(1, 50);
    sam3ImageLoadCap->setAnimates(false);
    sam3ImageLoadCap->setParent(*sam3Group);
    page->addChild(*sam3ImageLoadCap);

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

    OFX::StringParamDescriptor *sam3ModelPath = desc.defineStringParam("sam3ModelPath");
    sam3ModelPath->setLabel("SAM3 Model");
    sam3ModelPath->setHint("Path to the SAM3 model (relative to ComfyUI models dir).");
    sam3ModelPath->setDefault("models/sam3/sam3.pt");
    sam3ModelPath->setAnimates(false);
    sam3ModelPath->setParent(*modelGroup);
    page->addChild(*sam3ModelPath);

    OFX::BooleanParamDescriptor *offloadSam3Model = desc.defineBooleanParam("offloadSam3Model");
    offloadSam3Model->setLabel("Offload SAM3");
    offloadSam3Model->setHint("Offload SAM3 to CPU after propagation to free VRAM for MatAnyone2.");
    offloadSam3Model->setDefault(false);
    offloadSam3Model->setAnimates(false);
    offloadSam3Model->setParent(*modelGroup);
    page->addChild(*offloadSam3Model);

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
    const char* home = getenv("HOME");
    if (!home) return json{};

    std::vector<std::string> searchPaths = {
        std::string(home) + "/Library/OFX/Plugins/MatteMA2.ofx.bundle/Contents/Resources/config/defaults.json",
        std::string(home) + "/OFX/Plugins/MatteMA2.ofx.bundle/Contents/Resources/config/defaults.json",
        "/Library/OFX/Plugins/MatteMA2.ofx.bundle/Contents/Resources/config/defaults.json",
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
        "Two LoadEXR nodes are used:\n"
        "- SAM3 loads a small number of frames (typically 1) for the reference mask\n"
        "- MatAnyone2 loads the full sequence independently\n\n"
        "Requires:\n"
        "- ComfyUI server with ComfyUI-MatAnyone and ComfyUI-SAM3 extensions\n"
        "- SAM3 model (sam3.pt)\n"
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
