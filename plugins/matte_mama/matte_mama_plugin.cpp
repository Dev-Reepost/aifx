// Copyright OpenFX and contributors to the OpenFX project.
// SPDX-License-Identifier: BSD-3-Clause

#include "matte_mama_plugin.h"
#include <sstream>
#include <fstream>
#include <ctime>

namespace ComfyUI {

static const char* kPrecisions[]    = { "bf16", "fp16", "fp32", nullptr };
static const char* kAttentionModes[]= { "auto", "sdpa", "xformers", nullptr };

// ============================================================================
// MatteMaMaPlugin Implementation
// ============================================================================

MatteMaMaPlugin::MatteMaMaPlugin(OfxImageEffectHandle handle)
    : BasePlugin(handle)
    , _textPrompt(nullptr), _scoreThreshold(nullptr), _sam3CheckpointName(nullptr)
    , _objectIndices(nullptr), _detectionThreshold(nullptr), _maxObjects(nullptr)
    , _detectInterval(nullptr), _refineIterations(nullptr), _individualMasks(nullptr)
    , _referenceBatchIndex(nullptr)
    , _seed(nullptr), _maxResolution(nullptr), _fps(nullptr)
    , _motionBucketId(nullptr), _noiseAugStrength(nullptr), _imageLoadCap(nullptr)
    , _baseModelPath(nullptr), _unetCheckpointPath(nullptr)
    , _precision(nullptr), _enableModelCpuOffload(nullptr)
    , _vaeEncodeChunkSize(nullptr), _attentionMode(nullptr)
    , _enableVaeTiling(nullptr), _enableVaeSlicing(nullptr)
{
    _textPrompt           = fetchStringParam("textPrompt");
    _scoreThreshold       = fetchDoubleParam("scoreThreshold");
    _sam3CheckpointName   = fetchStringParam("sam3CheckpointName");
    _objectIndices        = fetchStringParam("objectIndices");
    _detectionThreshold   = fetchDoubleParam("detectionThreshold");
    _maxObjects           = fetchIntParam("maxObjects");
    _detectInterval       = fetchIntParam("detectInterval");
    _refineIterations     = fetchIntParam("refineIterations");
    _individualMasks      = fetchBooleanParam("individualMasks");
    _referenceBatchIndex  = fetchIntParam("referenceBatchIndex");

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

// ============================================================================
// Helper: index → string
// ============================================================================

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
    std::string textPrompt, sam3CheckpointName, objectIndices;
    double scoreThreshold, detectionThreshold;
    int    maxObjects, detectInterval, refineIterations, referenceBatchIndex, imageLoadCap;
    bool   individualMasks;

    _textPrompt->getValue(textPrompt);
    _scoreThreshold->getValue(scoreThreshold);
    _sam3CheckpointName->getValue(sam3CheckpointName);
    _objectIndices->getValue(objectIndices);
    _detectionThreshold->getValue(detectionThreshold);
    _maxObjects->getValue(maxObjects);
    _detectInterval->getValue(detectInterval);
    _refineIterations->getValue(refineIterations);
    _individualMasks->getValue(individualMasks);
    _referenceBatchIndex->getValue(referenceBatchIndex);
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
    mountPath = getLocalMountPath();
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
        _logger->info("  SAM3 checkpoint: {}", sam3CheckpointName);
        _logger->info("  SAM3 score threshold: {}", scoreThreshold);
        _logger->info("  SAM3 detection threshold: {}", detectionThreshold);
        _logger->info("  SAM3 object indices: {}", objectIndices);
        _logger->info("  SAM3 reference batch index: {}", referenceBatchIndex);
        _logger->info("  SAM3 max objects: {}", maxObjects);
        _logger->info("  SAM3 detect interval: {}", detectInterval);
        _logger->info("  SAM3 refine iterations: {}", refineIterations);
        _logger->info("  SAM3 individual masks: {}", individualMasks);
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

        // Node 85: CheckpointLoaderSimple (SAM3 checkpoint)
        {"85", {
            {"inputs", {
                {"ckpt_name", sam3CheckpointName}
            }},
            {"class_type", "CheckpointLoaderSimple"},
            {"_meta", {{"title", "Load Checkpoint"}}}
        }},

        // Node 86: CLIPTextEncode (text prompt)
        {"86", {
            {"inputs", {
                {"text", textPrompt},
                {"clip", json::array({"85", 1})}
            }},
            {"class_type", "CLIPTextEncode"},
            {"_meta", {{"title", "CLIP Text Encode (Prompt)"}}}
        }},

        // Node 91: ImageFromBatch — picks reference frame
        {"91", {
            {"inputs", {
                {"batch_index", referenceBatchIndex},
                {"length", 1},
                {"image", json::array({"44", 0})}
            }},
            {"class_type", "ImageFromBatch"},
            {"_meta", {{"title", "ImageFromBatch"}}}
        }},

        // Node 89: SAM3_Detect on the reference frame
        {"89", {
            {"inputs", {
                {"threshold", scoreThreshold},
                {"refine_iterations", refineIterations},
                {"individual_masks", individualMasks},
                {"model",        json::array({"85", 0})},
                {"image",        json::array({"91", 0})},
                {"conditioning", json::array({"86", 0})}
            }},
            {"class_type", "SAM3_Detect"},
            {"_meta", {{"title", "SAM3 Detect"}}}
        }},

        // Node 88: SAM3_VideoTrack across the full sequence
        {"88", {
            {"inputs", {
                {"detection_threshold", detectionThreshold},
                {"max_objects", maxObjects},
                {"detect_interval", detectInterval},
                {"images",       json::array({"44", 0})},
                {"model",        json::array({"85", 0})},
                {"initial_mask", json::array({"89", 0})},
                {"conditioning", json::array({"86", 0})}
            }},
            {"class_type", "SAM3_VideoTrack"},
            {"_meta", {{"title", "SAM3 Video Track"}}}
        }},

        // Node 87: SAM3_TrackToMask
        {"87", {
            {"inputs", {
                {"object_indices", objectIndices},
                {"track_data",     json::array({"88", 0})}
            }},
            {"class_type", "SAM3_TrackToMask"},
            {"_meta", {{"title", "SAM3 Track to Mask"}}}
        }},

        // Node 99: VideoMaMa pipeline loader
        {"99", {
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

        // Node 98: VideoMaMa sampler
        {"98", {
            {"inputs", {
                {"seed", effectiveSeed},
                {"max_resolution", maxResolution},
                {"fps", fps},
                {"motion_bucket_id", motionBucketId},
                {"noise_aug_strength", noiseAugStrength},
                {"pipeline", json::array({"99", 0})},
                {"images",   json::array({"44", 0})},
                {"masks",    json::array({"87", 0})}
            }},
            {"class_type", "VideoMaMaSampler"},
            {"_meta", {{"title", "VideoMaMa Sampler"}}}
        }},

        // Node 35: Mask to image (MaMa output)
        {"35", {
            {"inputs", {{"mask", json::array({"98", 0})}}},
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
                {"create_path_if_missing", true},
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

    std::string textPrompt, sam3CheckpointName, objectIndices;
    std::string baseModelPath, unetCheckpointPath, precision, attentionMode;
    double scoreThreshold, detectionThreshold, noiseAugStrength;
    int    maxObjects, detectInterval, refineIterations, referenceBatchIndex, imageLoadCap;
    int    seed, maxResolution, fps, motionBucketId, vaeEncodeChunkSize;
    bool   individualMasks, enableModelCpuOffload, enableVaeTiling, enableVaeSlicing;

    _textPrompt->getValue(textPrompt);
    _scoreThreshold->getValue(scoreThreshold);
    _sam3CheckpointName->getValue(sam3CheckpointName);
    _objectIndices->getValue(objectIndices);
    _detectionThreshold->getValue(detectionThreshold);
    _maxObjects->getValue(maxObjects);
    _detectInterval->getValue(detectInterval);
    _refineIterations->getValue(refineIterations);
    _individualMasks->getValue(individualMasks);
    _referenceBatchIndex->getValue(referenceBatchIndex);
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
    replaceStr("${SAM3_CHECKPOINT_NAME}", sam3CheckpointName);
    replaceStr("${OBJECT_INDICES}",       objectIndices);
    replaceStr("${BASE_MODEL_PATH}",      baseModelPath);
    replaceStr("${UNET_CHECKPOINT_PATH}", unetCheckpointPath);
    replaceStr("${PRECISION}",            precision);
    replaceStr("${ATTENTION_MODE}",       attentionMode);

    // Numeric replacements
    replaceNumeric("\"${SCORE_THRESHOLD}\"",         std::to_string(scoreThreshold));
    replaceNumeric("\"${DETECTION_THRESHOLD}\"",     std::to_string(detectionThreshold));
    replaceNumeric("\"${MAX_OBJECTS}\"",             std::to_string(maxObjects));
    replaceNumeric("\"${DETECT_INTERVAL}\"",         std::to_string(detectInterval));
    replaceNumeric("\"${REFINE_ITERATIONS}\"",       std::to_string(refineIterations));
    replaceNumeric("\"${REFERENCE_BATCH_INDEX}\"",   std::to_string(referenceBatchIndex));
    replaceNumeric("\"${IMAGE_LOAD_CAP}\"",          std::to_string(imageLoadCap));
    replaceNumeric("\"${SEED}\"",                    std::to_string(effectiveSeed));
    replaceNumeric("\"${MAX_RESOLUTION}\"",          std::to_string(maxResolution));
    replaceNumeric("\"${FPS}\"",                     std::to_string(fps));
    replaceNumeric("\"${MOTION_BUCKET_ID}\"",        std::to_string(motionBucketId));
    replaceNumeric("\"${NOISE_AUG_STRENGTH}\"",      std::to_string(noiseAugStrength));
    replaceNumeric("\"${VAE_ENCODE_CHUNK_SIZE}\"",   std::to_string(vaeEncodeChunkSize));
    replaceNumeric("\"${INDIVIDUAL_MASKS}\"",        individualMasks       ? "true" : "false");
    replaceNumeric("\"${ENABLE_MODEL_CPU_OFFLOAD}\"",enableModelCpuOffload ? "true" : "false");
    replaceNumeric("\"${ENABLE_VAE_TILING}\"",       enableVaeTiling       ? "true" : "false");
    replaceNumeric("\"${ENABLE_VAE_SLICING}\"",      enableVaeSlicing      ? "true" : "false");

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
    _sam3CheckpointName->getValue(sam3);
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

    OFX::StringParamDescriptor *objectIndices = desc.defineStringParam("objectIndices");
    objectIndices->setLabel("Object Indices");
    objectIndices->setHint("Comma-separated SAM3 object indices to keep in the matte.");
    objectIndices->setDefault("0");
    objectIndices->setAnimates(false);
    objectIndices->setParent(*sam3Group);
    page->addChild(*objectIndices);

    OFX::IntParamDescriptor *referenceBatchIndex = desc.defineIntParam("referenceBatchIndex");
    referenceBatchIndex->setLabel("Reference Frame");
    referenceBatchIndex->setHint("Reference frame for SAM3_Detect.");
    referenceBatchIndex->setDefault(0);
    referenceBatchIndex->setRange(0, 9999);
    referenceBatchIndex->setDisplayRange(0, 200);
    referenceBatchIndex->setAnimates(false);
    referenceBatchIndex->setParent(*sam3Group);
    page->addChild(*referenceBatchIndex);

    OFX::DoubleParamDescriptor *scoreThreshold = desc.defineDoubleParam("scoreThreshold");
    scoreThreshold->setLabel("Detect Threshold");
    scoreThreshold->setHint("SAM3_Detect confidence threshold on the reference frame (0–1).");
    scoreThreshold->setDefault(0.3);
    scoreThreshold->setRange(0.0, 1.0);
    scoreThreshold->setDisplayRange(0.0, 1.0);
    scoreThreshold->setParent(*sam3Group);
    page->addChild(*scoreThreshold);

    OFX::DoubleParamDescriptor *detectionThreshold = desc.defineDoubleParam("detectionThreshold");
    detectionThreshold->setLabel("Track Threshold");
    detectionThreshold->setHint("SAM3_VideoTrack per-frame detection threshold.");
    detectionThreshold->setDefault(0.5);
    detectionThreshold->setRange(0.0, 1.0);
    detectionThreshold->setDisplayRange(0.0, 1.0);
    detectionThreshold->setParent(*sam3Group);
    page->addChild(*detectionThreshold);

    OFX::IntParamDescriptor *maxObjects = desc.defineIntParam("maxObjects");
    maxObjects->setLabel("Max Objects");
    maxObjects->setHint("Max objects to track (0 = unlimited).");
    maxObjects->setDefault(0);
    maxObjects->setRange(0, 32);
    maxObjects->setDisplayRange(0, 16);
    maxObjects->setAnimates(false);
    maxObjects->setParent(*sam3Group);
    page->addChild(*maxObjects);

    OFX::IntParamDescriptor *detectInterval = desc.defineIntParam("detectInterval");
    detectInterval->setLabel("Detect Interval");
    detectInterval->setHint("Detect every N frames.");
    detectInterval->setDefault(1);
    detectInterval->setRange(1, 64);
    detectInterval->setDisplayRange(1, 16);
    detectInterval->setAnimates(false);
    detectInterval->setParent(*sam3Group);
    page->addChild(*detectInterval);

    OFX::IntParamDescriptor *refineIterations = desc.defineIntParam("refineIterations");
    refineIterations->setLabel("Refine Iterations");
    refineIterations->setHint("SAM3_Detect refinement iterations.");
    refineIterations->setDefault(2);
    refineIterations->setRange(0, 16);
    refineIterations->setDisplayRange(0, 16);
    refineIterations->setAnimates(false);
    refineIterations->setParent(*sam3Group);
    page->addChild(*refineIterations);

    OFX::BooleanParamDescriptor *individualMasks = desc.defineBooleanParam("individualMasks");
    individualMasks->setLabel("Individual Masks");
    individualMasks->setHint("Individual per-object masks.");
    individualMasks->setDefault(false);
    individualMasks->setAnimates(false);
    individualMasks->setParent(*sam3Group);
    page->addChild(*individualMasks);

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

    OFX::StringParamDescriptor *sam3CheckpointName = desc.defineStringParam("sam3CheckpointName");
    sam3CheckpointName->setLabel("SAM3 Checkpoint");
    sam3CheckpointName->setHint("Checkpoint filename in ComfyUI/models/checkpoints.");
    sam3CheckpointName->setDefault("sam3.1_multiplex_fp16.safetensors");
    sam3CheckpointName->setAnimates(false);
    sam3CheckpointName->setParent(*modelGroup);
    page->addChild(*sam3CheckpointName);

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
    // Search the MatteMaMa bundle across all platform OFX plugin locations.
    std::vector<std::string> searchPaths =
        getOfxConfigSearchPaths({"MatteMaMa"});

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
        "- SAM3 checkpoint (e.g. sam3.1_multiplex_fp16.safetensors) and VideoMaMa UNet checkpoint\n"
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
