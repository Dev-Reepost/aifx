// Copyright OpenFX and contributors to the OpenFX project.
// SPDX-License-Identifier: BSD-3-Clause

#include "matte_mama_plugin.h"
#include <sstream>
#include <fstream>
#include <ctime>

namespace ComfyUI {

static const char* kDirections[]    = { "forward", "backward", "both", nullptr };
static const char* kPrecisions[]    = { "bf16", "fp16", "fp32", nullptr };
static const char* kAttentionModes[]= { "auto", "sdpa", "xformers", nullptr };

// ============================================================================
// MatteMaMaPlugin Implementation
// ============================================================================

MatteMaMaPlugin::MatteMaMaPlugin(OfxImageEffectHandle handle)
    : BasePlugin(handle)
    , _textPrompt(nullptr), _scoreThreshold(nullptr), _frameIdx(nullptr)
    , _direction(nullptr), _plotAllMasks(nullptr), _objId(nullptr)
    , _sam3ModelPath(nullptr), _offloadSam3Model(nullptr)
    , _seed(nullptr), _maxResolution(nullptr), _fps(nullptr)
    , _motionBucketId(nullptr), _noiseAugStrength(nullptr), _imageLoadCap(nullptr)
    , _baseModelPath(nullptr), _unetCheckpointPath(nullptr)
    , _precision(nullptr), _enableModelCpuOffload(nullptr)
    , _vaeEncodeChunkSize(nullptr), _attentionMode(nullptr)
    , _enableVaeTiling(nullptr), _enableVaeSlicing(nullptr)
{
    _textPrompt           = fetchStringParam("textPrompt");
    _scoreThreshold       = fetchDoubleParam("scoreThreshold");
    _frameIdx             = fetchIntParam("frameIdx");
    _direction            = fetchChoiceParam("direction");
    _plotAllMasks         = fetchBooleanParam("plotAllMasks");
    _objId                = fetchIntParam("objId");
    _sam3ModelPath        = fetchStringParam("sam3ModelPath");
    _offloadSam3Model     = fetchBooleanParam("offloadSam3Model");

    _seed                 = fetchIntParam("seed");
    _maxResolution        = fetchIntParam("maxResolution");
    _fps                  = fetchIntParam("fps");
    _motionBucketId       = fetchIntParam("motionBucketId");
    _noiseAugStrength     = fetchDoubleParam("noiseAugStrength");
    _imageLoadCap         = fetchIntParam("imageLoadCap");

    _baseModelPath        = fetchStringParam("baseModelPath");
    _unetCheckpointPath   = fetchStringParam("unetCheckpointPath");
    _precision            = fetchChoiceParam("precision");
    _enableModelCpuOffload= fetchBooleanParam("enableModelCpuOffload");
    _vaeEncodeChunkSize   = fetchIntParam("vaeEncodeChunkSize");
    _attentionMode        = fetchChoiceParam("attentionMode");
    _enableVaeTiling      = fetchBooleanParam("enableVaeTiling");
    _enableVaeSlicing     = fetchBooleanParam("enableVaeSlicing");
}

MatteMaMaPlugin::~MatteMaMaPlugin() {}

int MatteMaMaPlugin::getImageLoadCap() const {
    int cap = 0;
    if (_imageLoadCap) _imageLoadCap->getValue(cap);
    return cap;
}

void MatteMaMaPlugin::changedParam(const OFX::InstanceChangedArgs &args,
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

// ============================================================================
// Helper: index → string
// ============================================================================

std::string MatteMaMaPlugin::getDirectionName() const
{
    int idx; _direction->getValue(idx);
    if (idx >= 0 && kDirections[idx]) return kDirections[idx];
    return "forward";
}

std::string MatteMaMaPlugin::getPrecisionName() const
{
    int idx; _precision->getValue(idx);
    if (idx >= 0 && kPrecisions[idx]) return kPrecisions[idx];
    return "bf16";
}

std::string MatteMaMaPlugin::getAttentionModeName() const
{
    int idx; _attentionMode->getValue(idx);
    if (idx >= 0 && kAttentionModes[idx]) return kAttentionModes[idx];
    return "auto";
}

// ============================================================================
// Workflow building
// ============================================================================

json MatteMaMaPlugin::buildWorkflow(int frame,
                                     const std::map<std::string, std::string>& inputPaths)
{
    if (_logger) _logger->info("Building MatteMaMa workflow for frame {}", frame);

    std::string inputPath;
    auto it = inputPaths.find("InputA");
    if (it != inputPaths.end()) {
        inputPath = it->second;
    } else if (!inputPaths.empty()) {
        inputPath = inputPaths.begin()->second;
    } else {
        throw std::runtime_error("No input path provided for MatteMaMa workflow");
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

json MatteMaMaPlugin::buildHardcodedWorkflow(int frame, const std::string& inputPath)
{
    if (_logger) _logger->info("Building hardcoded MatteMaMa workflow");

    // SAM3 params
    std::string textPrompt, sam3ModelPath, direction;
    double scoreThreshold;
    int frameIdx, objId, imageLoadCap;
    bool plotAllMasks, offloadSam3Model;

    _textPrompt->getValue(textPrompt);
    _scoreThreshold->getValue(scoreThreshold);
    _frameIdx->getValue(frameIdx);
    direction = getDirectionName();
    _plotAllMasks->getValue(plotAllMasks);
    _objId->getValue(objId);
    _sam3ModelPath->getValue(sam3ModelPath);
    _offloadSam3Model->getValue(offloadSam3Model);
    _imageLoadCap->getValue(imageLoadCap);

    // VideoMaMa sampler params
    int seed, maxResolution, fps, motionBucketId;
    double noiseAugStrength;
    _seed->getValue(seed);
    _maxResolution->getValue(maxResolution);
    _fps->getValue(fps);
    _motionBucketId->getValue(motionBucketId);
    _noiseAugStrength->getValue(noiseAugStrength);

    // VideoMaMa model params
    std::string baseModelPath, unetCheckpointPath, precision, attentionMode;
    bool enableModelCpuOffload, enableVaeTiling, enableVaeSlicing;
    int vaeEncodeChunkSize;
    _baseModelPath->getValue(baseModelPath);
    _unetCheckpointPath->getValue(unetCheckpointPath);
    precision      = getPrecisionName();
    attentionMode  = getAttentionModeName();
    _enableModelCpuOffload->getValue(enableModelCpuOffload);
    _vaeEncodeChunkSize->getValue(vaeEncodeChunkSize);
    _enableVaeTiling->getValue(enableVaeTiling);
    _enableVaeSlicing->getValue(enableVaeSlicing);

    // Resolve seed: 0 → use current time as random seed
    long long effectiveSeed = (seed == 0) ? static_cast<long long>(std::time(nullptr)) : seed;

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
        _logger->info("MatteMaMa parameters:");
        _logger->info("  SAM3 prompt: {}", textPrompt);
        _logger->info("  SAM3 threshold: {}", scoreThreshold);
        _logger->info("  SAM3 frame idx: {}", frameIdx);
        _logger->info("  SAM3 direction: {}", direction);
        _logger->info("  MaMa seed: {}", effectiveSeed);
        _logger->info("  MaMa max resolution: {}", maxResolution);
        _logger->info("  MaMa fps: {}", fps);
        _logger->info("  Input (ComfyUI): {}", comfyInputPath);
        _logger->info("  Output prefix (ComfyUI): {}", comfyOutputPrefix);
    }

    json workflow = {

        // Node 44: Load EXR
        {"44", {
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

        // Node 55: Load SAM3 model
        {"55", {
            {"inputs", {{"model_path", sam3ModelPath}}},
            {"class_type", "LoadSAM3Model"},
            {"_meta", {{"title", "(down)Load SAM3 Model"}}}
        }},

        // Node 52: SAM3 initial segmentation
        {"52", {
            {"inputs", {
                {"prompt_mode", "text"},
                {"text_prompt", textPrompt},
                {"frame_idx", frameIdx},
                {"score_threshold", scoreThreshold},
                {"video_frames", json::array({"44", 0})}
            }},
            {"class_type", "SAM3VideoSegmentation"},
            {"_meta", {{"title", "SAM3 Video Segmentation"}}}
        }},

        // Node 53: SAM3 propagate
        {"53", {
            {"inputs", {
                {"start_frame", 0},
                {"end_frame", -1},
                {"direction", direction},
                {"offload_model", offloadSam3Model},
                {"sam3_model", json::array({"55", 0})},
                {"video_state", json::array({"52", 0})}
            }},
            {"class_type", "SAM3Propagate"},
            {"_meta", {{"title", "SAM3 Propagate"}}}
        }},

        // Node 54: SAM3 video output (mask extraction)
        {"54", {
            {"inputs", {
                {"obj_id", objId},
                {"plot_all_masks", plotAllMasks},
                {"masks",      json::array({"53", 0})},
                {"video_state",json::array({"53", 2})},
                {"scores",     json::array({"53", 1})}
            }},
            {"class_type", "SAM3VideoOutput"},
            {"_meta", {{"title", "SAM3 Video Output"}}}
        }},

        // Node 37: VideoMaMa pipeline loader
        {"37", {
            {"inputs", {
                {"base_model_path", baseModelPath},
                {"unet_checkpoint_path", unetCheckpointPath},
                {"precision", precision},
                {"enable_model_cpu_offload", enableModelCpuOffload},
                {"vae_encode_chunk_size", vaeEncodeChunkSize},
                {"attention_mode", attentionMode},
                {"enable_vae_tiling", enableVaeTiling},
                {"enable_vae_slicing", enableVaeSlicing}
            }},
            {"class_type", "VideoMaMaPipelineLoader"},
            {"_meta", {{"title", "VideoMaMa Pipeline Loader"}}}
        }},

        // Node 42: VideoMaMa sampler
        {"42", {
            {"inputs", {
                {"seed", effectiveSeed},
                {"max_resolution", maxResolution},
                {"fps", fps},
                {"motion_bucket_id", motionBucketId},
                {"noise_aug_strength", noiseAugStrength},
                {"pipeline", json::array({"37", 0})},
                {"images",   json::array({"44", 0})},
                {"masks",    json::array({"54", 0})}
            }},
            {"class_type", "VideoMaMaSampler"},
            {"_meta", {{"title", "VideoMaMa Sampler"}}}
        }},

        // Node 35: Mask to image (MaMa output)
        {"35", {
            {"inputs", {{"mask", json::array({"42", 0})}}},
            {"class_type", "MaskToImage"},
            {"_meta", {{"title", "Convert Mask to Image"}}}
        }},

        // Node 45: Save EXR
        {"45", {
            {"inputs", {
                {"filename_prefix", comfyOutputPrefix},
                {"tonemap", "linear"},
                {"version", -1},
                {"start_frame", frame},
                {"frame_pad", 4},
                {"save_workflow", "none"},
                {"images", json::array({"35", 0})}
            }},
            {"class_type", "SaveEXR"},
            {"_meta", {{"title", "SAVE_SEG_MATTE"}}}
        }}
    };

    if (_logger) {
        _logger->info("=== MATTE MAMA WORKFLOW DUMP ===");
        _logger->info("{}", workflow.dump(2));
        _logger->info("=== END WORKFLOW DUMP ===");
    }

    return workflow;
}

json MatteMaMaPlugin::customizeWorkflowWithParams(const json& baseWorkflow, int frame)
{
    if (_logger) _logger->debug("Customizing MatteMaMa workflow with parameters");

    std::string textPrompt, sam3ModelPath, direction, baseModelPath, unetCheckpointPath;
    std::string precision, attentionMode;
    double scoreThreshold, noiseAugStrength;
    int frameIdx, objId, imageLoadCap, seed, maxResolution, fps, motionBucketId, vaeEncodeChunkSize;
    bool plotAllMasks, offloadSam3Model, enableModelCpuOffload, enableVaeTiling, enableVaeSlicing;

    _textPrompt->getValue(textPrompt);
    _scoreThreshold->getValue(scoreThreshold);
    _frameIdx->getValue(frameIdx);
    direction = getDirectionName();
    _plotAllMasks->getValue(plotAllMasks);
    _objId->getValue(objId);
    _sam3ModelPath->getValue(sam3ModelPath);
    _offloadSam3Model->getValue(offloadSam3Model);
    _imageLoadCap->getValue(imageLoadCap);

    _seed->getValue(seed);
    _maxResolution->getValue(maxResolution);
    _fps->getValue(fps);
    _motionBucketId->getValue(motionBucketId);
    _noiseAugStrength->getValue(noiseAugStrength);

    _baseModelPath->getValue(baseModelPath);
    _unetCheckpointPath->getValue(unetCheckpointPath);
    precision     = getPrecisionName();
    attentionMode = getAttentionModeName();
    _enableModelCpuOffload->getValue(enableModelCpuOffload);
    _vaeEncodeChunkSize->getValue(vaeEncodeChunkSize);
    _enableVaeTiling->getValue(enableVaeTiling);
    _enableVaeSlicing->getValue(enableVaeSlicing);

    long long effectiveSeed = (seed == 0) ? static_cast<long long>(std::time(nullptr)) : seed;

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

    // String replacements
    replaceStr("${TEXT_PROMPT}",          textPrompt);
    replaceStr("${DIRECTION}",            direction);
    replaceStr("${SAM3_MODEL_PATH}",      sam3ModelPath);
    replaceStr("${BASE_MODEL_PATH}",      baseModelPath);
    replaceStr("${UNET_CHECKPOINT_PATH}", unetCheckpointPath);
    replaceStr("${PRECISION}",            precision);
    replaceStr("${ATTENTION_MODE}",       attentionMode);

    // Numeric replacements
    replaceNumeric("\"${SCORE_THRESHOLD}\"",      std::to_string(scoreThreshold));
    replaceNumeric("\"${FRAME_IDX}\"",            std::to_string(frameIdx));
    replaceNumeric("\"${OBJ_ID}\"",               std::to_string(objId));
    replaceNumeric("\"${IMAGE_LOAD_CAP}\"",       std::to_string(imageLoadCap));
    replaceNumeric("\"${SEED}\"",                 std::to_string(effectiveSeed));
    replaceNumeric("\"${MAX_RESOLUTION}\"",       std::to_string(maxResolution));
    replaceNumeric("\"${FPS}\"",                  std::to_string(fps));
    replaceNumeric("\"${MOTION_BUCKET_ID}\"",     std::to_string(motionBucketId));
    replaceNumeric("\"${NOISE_AUG_STRENGTH}\"",   std::to_string(noiseAugStrength));
    replaceNumeric("\"${VAE_ENCODE_CHUNK_SIZE}\"",std::to_string(vaeEncodeChunkSize));
    replaceNumeric("\"${PLOT_ALL_MASKS}\"",       plotAllMasks        ? "true" : "false");
    replaceNumeric("\"${OFFLOAD_SAM3_MODEL}\"",   offloadSam3Model    ? "true" : "false");
    replaceNumeric("\"${ENABLE_MODEL_CPU_OFFLOAD}\"", enableModelCpuOffload ? "true" : "false");
    replaceNumeric("\"${ENABLE_VAE_TILING}\"",    enableVaeTiling     ? "true" : "false");
    replaceNumeric("\"${ENABLE_VAE_SLICING}\"",   enableVaeSlicing    ? "true" : "false");

    try {
        json finalWorkflow = json::parse(workflowStr);
        if (_logger) {
            _logger->info("=== MATTE MAMA FINAL WORKFLOW ===");
            _logger->info("{}", finalWorkflow.dump(2));
            _logger->info("=== END FINAL WORKFLOW ===");
        }
        return finalWorkflow;
    } catch (const json::exception& e) {
        if (_logger) _logger->error("Failed to parse customized MatteMaMa workflow: {}", e.what());
        throw std::runtime_error("Failed to parse MatteMaMa customized workflow: " + std::string(e.what()));
    }
}

std::vector<std::string> MatteMaMaPlugin::getRequiredModels()
{
    std::string sam3, base, unet;
    _sam3ModelPath->getValue(sam3);
    _baseModelPath->getValue(base);
    _unetCheckpointPath->getValue(unet);
    return {sam3, base, unet};
}

// ============================================================================
// Parameter Definitions
// ============================================================================

void MatteMaMaPlugin::describeInContext(OFX::ImageEffectDescriptor &desc,
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
    textPrompt->setHint("Text description of the subject to segment and matte (e.g. 'person', 'hair', 'girl').");
    textPrompt->setDefault("foreground");
    textPrompt->setAnimates(false);
    textPrompt->setParent(*sam3Group);
    page->addChild(*textPrompt);

    OFX::DoubleParamDescriptor *scoreThreshold = desc.defineDoubleParam("scoreThreshold");
    scoreThreshold->setLabel("Threshold");
    scoreThreshold->setHint("SAM3 detection confidence threshold (0–1). Higher = stricter detection.");
    scoreThreshold->setDefault(0.3);
    scoreThreshold->setRange(0.0, 1.0);
    scoreThreshold->setDisplayRange(0.0, 1.0);
    scoreThreshold->setParent(*sam3Group);
    page->addChild(*scoreThreshold);

    OFX::IntParamDescriptor *frameIdx = desc.defineIntParam("frameIdx");
    frameIdx->setLabel("Reference Frame");
    frameIdx->setHint("Frame index (0-based) used as the reference for initial SAM3 segmentation.");
    frameIdx->setDefault(1);
    frameIdx->setRange(0, 9999);
    frameIdx->setDisplayRange(0, 100);
    frameIdx->setAnimates(false);
    frameIdx->setParent(*sam3Group);
    page->addChild(*frameIdx);

    OFX::ChoiceParamDescriptor *direction = desc.defineChoiceParam("direction");
    direction->setLabel("Propagation Direction");
    direction->setHint("Direction to propagate SAM3 segmentation through the sequence.");
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

    // ---- VIDEOMAMA SAMPLER GROUP ----
    OFX::GroupParamDescriptor *samplerGroup = desc.defineGroupParam("mamaGroup");
    samplerGroup->setLabel("VideoMaMa Sampler");
    samplerGroup->setOpen(true);
    page->addChild(*samplerGroup);

    OFX::IntParamDescriptor *maxResolution = desc.defineIntParam("maxResolution");
    maxResolution->setLabel("Max Resolution");
    maxResolution->setHint("Maximum resolution of the longer side. Frames are resized to fit.");
    maxResolution->setDefault(1080);
    maxResolution->setRange(64, 4096);
    maxResolution->setDisplayRange(256, 2048);
    maxResolution->setAnimates(false);
    maxResolution->setParent(*samplerGroup);
    page->addChild(*maxResolution);

    OFX::IntParamDescriptor *fps = desc.defineIntParam("fps");
    fps->setLabel("FPS");
    fps->setHint("Frames per second hint passed to the VideoMaMa sampler.");
    fps->setDefault(24);
    fps->setRange(1, 120);
    fps->setDisplayRange(1, 60);
    fps->setAnimates(false);
    fps->setParent(*samplerGroup);
    page->addChild(*fps);

    OFX::IntParamDescriptor *motionBucketId = desc.defineIntParam("motionBucketId");
    motionBucketId->setLabel("Motion Bucket ID");
    motionBucketId->setHint("Controls the amount of motion expected. Higher values = more motion. "
                              "Inherited from SVD; 127 is the standard default.");
    motionBucketId->setDefault(127);
    motionBucketId->setRange(1, 255);
    motionBucketId->setDisplayRange(1, 255);
    motionBucketId->setAnimates(false);
    motionBucketId->setParent(*samplerGroup);
    page->addChild(*motionBucketId);

    OFX::DoubleParamDescriptor *noiseAugStrength = desc.defineDoubleParam("noiseAugStrength");
    noiseAugStrength->setLabel("Noise Aug Strength");
    noiseAugStrength->setHint("Noise augmentation strength for the conditioning frame. "
                               "0 = no augmentation (recommended for clean mattes).");
    noiseAugStrength->setDefault(0.0);
    noiseAugStrength->setRange(0.0, 1.0);
    noiseAugStrength->setDisplayRange(0.0, 0.5);
    noiseAugStrength->setAnimates(false);
    noiseAugStrength->setParent(*samplerGroup);
    page->addChild(*noiseAugStrength);

    OFX::IntParamDescriptor *imageLoadCap = desc.defineIntParam("imageLoadCap");
    imageLoadCap->setLabel("Frame Limit");
    imageLoadCap->setHint("Maximum number of frames to load from the input sequence. 0 = no limit.");
    imageLoadCap->setDefault(50);
    imageLoadCap->setRange(0, 1024);
    imageLoadCap->setDisplayRange(0, 200);
    imageLoadCap->setAnimates(false);
    imageLoadCap->setParent(*samplerGroup);
    page->addChild(*imageLoadCap);

    OFX::IntParamDescriptor *seed = desc.defineIntParam("seed");
    seed->setLabel("Seed");
    seed->setHint("Random seed for reproducibility. 0 = random seed each run.");
    seed->setDefault(0);
    seed->setRange(0, 2147483647);
    seed->setAnimates(false);
    seed->setParent(*samplerGroup);
    page->addChild(*seed);

    // ---- VIDEOMAMA MODEL GROUP ----
    OFX::GroupParamDescriptor *modelGroup = desc.defineGroupParam("mamaModelGroup");
    modelGroup->setLabel("VideoMaMa Model");
    modelGroup->setOpen(false);
    page->addChild(*modelGroup);

    OFX::StringParamDescriptor *baseModelPath = desc.defineStringParam("baseModelPath");
    baseModelPath->setLabel("Base Model");
    baseModelPath->setHint("Path to the SVD base model (relative to ComfyUI models dir).");
    baseModelPath->setDefault("checkpoints/stabilityai/stable-video-diffusion-img2vid-xt");
    baseModelPath->setAnimates(false);
    baseModelPath->setParent(*modelGroup);
    page->addChild(*baseModelPath);

    OFX::StringParamDescriptor *unetCheckpointPath = desc.defineStringParam("unetCheckpointPath");
    unetCheckpointPath->setLabel("VideoMaMa UNet");
    unetCheckpointPath->setHint("Path to the VideoMaMa UNet checkpoint (relative to ComfyUI models dir).");
    unetCheckpointPath->setDefault("checkpoints/VideoMaMa");
    unetCheckpointPath->setAnimates(false);
    unetCheckpointPath->setParent(*modelGroup);
    page->addChild(*unetCheckpointPath);

    OFX::StringParamDescriptor *sam3ModelPath = desc.defineStringParam("sam3ModelPath");
    sam3ModelPath->setLabel("SAM3 Model");
    sam3ModelPath->setHint("Path to the SAM3 model file (relative to ComfyUI models dir).");
    sam3ModelPath->setDefault("models/sam3/sam3.pt");
    sam3ModelPath->setAnimates(false);
    sam3ModelPath->setParent(*modelGroup);
    page->addChild(*sam3ModelPath);

    OFX::ChoiceParamDescriptor *precision = desc.defineChoiceParam("precision");
    precision->setLabel("Precision");
    precision->setHint("Model computation precision.");
    precision->appendOption("BF16", "Brain float 16 (Ampere+ GPUs, recommended)");
    precision->appendOption("FP16", "Half precision");
    precision->appendOption("FP32", "Full precision");
    precision->setDefault(0);
    precision->setAnimates(false);
    precision->setParent(*modelGroup);
    page->addChild(*precision);

    OFX::ChoiceParamDescriptor *attentionMode = desc.defineChoiceParam("attentionMode");
    attentionMode->setLabel("Attention");
    attentionMode->setHint("Attention implementation.");
    attentionMode->appendOption("Auto",     "Automatically select best available");
    attentionMode->appendOption("SDPA",     "Scaled dot-product attention (PyTorch 2.0+)");
    attentionMode->appendOption("xFormers", "xFormers memory-efficient attention");
    attentionMode->setDefault(0);
    attentionMode->setAnimates(false);
    attentionMode->setParent(*modelGroup);
    page->addChild(*attentionMode);

    OFX::BooleanParamDescriptor *enableModelCpuOffload = desc.defineBooleanParam("enableModelCpuOffload");
    enableModelCpuOffload->setLabel("CPU Offload");
    enableModelCpuOffload->setHint("Offload model weights to CPU to reduce VRAM usage.");
    enableModelCpuOffload->setDefault(true);
    enableModelCpuOffload->setAnimates(false);
    enableModelCpuOffload->setParent(*modelGroup);
    page->addChild(*enableModelCpuOffload);

    OFX::IntParamDescriptor *vaeEncodeChunkSize = desc.defineIntParam("vaeEncodeChunkSize");
    vaeEncodeChunkSize->setLabel("VAE Chunk Size");
    vaeEncodeChunkSize->setHint("Number of frames to encode per VAE pass. Lower = less VRAM.");
    vaeEncodeChunkSize->setDefault(4);
    vaeEncodeChunkSize->setRange(1, 32);
    vaeEncodeChunkSize->setDisplayRange(1, 16);
    vaeEncodeChunkSize->setAnimates(false);
    vaeEncodeChunkSize->setParent(*modelGroup);
    page->addChild(*vaeEncodeChunkSize);

    OFX::BooleanParamDescriptor *offloadSam3Model = desc.defineBooleanParam("offloadSam3Model");
    offloadSam3Model->setLabel("Offload SAM3");
    offloadSam3Model->setHint("Offload SAM3 model to CPU after propagation to free VRAM for VideoMaMa.");
    offloadSam3Model->setDefault(false);
    offloadSam3Model->setAnimates(false);
    offloadSam3Model->setParent(*modelGroup);
    page->addChild(*offloadSam3Model);

    OFX::BooleanParamDescriptor *enableVaeTiling = desc.defineBooleanParam("enableVaeTiling");
    enableVaeTiling->setLabel("VAE Tiling");
    enableVaeTiling->setHint("Enable VAE tiling for very high resolution inputs.");
    enableVaeTiling->setDefault(false);
    enableVaeTiling->setAnimates(false);
    enableVaeTiling->setParent(*modelGroup);
    page->addChild(*enableVaeTiling);

    OFX::BooleanParamDescriptor *enableVaeSlicing = desc.defineBooleanParam("enableVaeSlicing");
    enableVaeSlicing->setLabel("VAE Slicing");
    enableVaeSlicing->setHint("Enable VAE slicing to reduce VRAM when encoding many frames.");
    enableVaeSlicing->setDefault(true);
    enableVaeSlicing->setAnimates(false);
    enableVaeSlicing->setParent(*modelGroup);
    page->addChild(*enableVaeSlicing);

    // Common parameters
    BasePlugin::describeCommonParameters(desc, context, page, page, page, configDefaults,
                                         /*skipGroupHeaders=*/false, /*isSequencePlugin=*/true);

    OFX::StringParamDescriptor *workflow = desc.defineStringParam("workflowName");
    workflow->setDefault("matte_mama");
}

// ============================================================================
// MatteMaMaPluginFactory Implementation
// ============================================================================

MatteMaMaPluginFactory::MatteMaMaPluginFactory()
    : OFX::PluginFactoryHelper<MatteMaMaPluginFactory>(
        "com.comfyui.MatteMaMa",
        1,
        0
    )
{
}

json MatteMaMaPluginFactory::loadConfigDefaults()
{
    const char* home = getenv("HOME");
    if (!home) return json{};

    std::vector<std::string> searchPaths = {
        std::string(home) + "/Library/OFX/Plugins/MatteMaMa.ofx.bundle/Contents/Resources/config/defaults.json",
        std::string(home) + "/OFX/Plugins/MatteMaMa.ofx.bundle/Contents/Resources/config/defaults.json",
        "/Library/OFX/Plugins/MatteMaMa.ofx.bundle/Contents/Resources/config/defaults.json",
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

void MatteMaMaPluginFactory::describe(OFX::ImageEffectDescriptor &desc)
{
    desc.setLabel("ComfyUI MatteMaMa");
    desc.setLabels("ComfyUI MatteMaMa", "MatteMaMa", "ComfyUI MatteMaMa");
    desc.setPluginGrouping("ComfyUI");

    desc.setPluginDescription(
        "High-quality video matting using VideoMaMa + SAM3 via ComfyUI.\n\n"
        "Combines SAM3 text-prompted segmentation with VideoMaMa's diffusion-based "
        "matting to produce temporally-consistent alpha mattes from video sequences.\n\n"
        "Pipeline:\n"
        "1. SAM3 generates a coarse mask from a text prompt\n"
        "2. SAM3 propagates the mask through the video sequence\n"
        "3. VideoMaMa refines the mask into a high-quality alpha matte\n\n"
        "Requires:\n"
        "- ComfyUI server with ComfyUI-MaMa and ComfyUI-SAM3 extensions\n"
        "- SAM3 model (sam3.pt) and VideoMaMa UNet checkpoint\n"
        "- SVD base model (stable-video-diffusion-img2vid-xt)\n"
        "- Shared network storage for image exchange\n\n"
        "Based on: https://github.com/kijai/ComfyUI-MaMa\n"
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

void MatteMaMaPluginFactory::describeInContext(OFX::ImageEffectDescriptor &desc,
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
    MatteMaMaPlugin::describeInContext(desc, context, &configDefaults);
}

OFX::ImageEffect* MatteMaMaPluginFactory::createInstance(OfxImageEffectHandle handle,
                                                           OFX::ContextEnum /*context*/)
{
    return new MatteMaMaPlugin(handle);
}

} // namespace ComfyUI

// ============================================================================
// Plugin Registration
// ============================================================================

namespace OFX {
namespace Plugin {

void getPluginIDs(OFX::PluginFactoryArray &ids)
{
    static ComfyUI::MatteMaMaPluginFactory factory;
    ids.push_back(&factory);
}

} // namespace Plugin
} // namespace OFX
