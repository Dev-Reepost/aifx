// Copyright OpenFX and contributors to the OpenFX project.
// SPDX-License-Identifier: BSD-3-Clause

#include "upscale_seedvr2_plugin.h"
#include <sstream>
#include <fstream>
#include <ctime>
#include <algorithm>  // std::min
#include <cmath>      // std::abs

namespace ComfyUI {

static const char* kColorCorrections[] = { "none", "lab", "hm", nullptr };
static const char* kAttentionModes[]   = { "sdpa", "flash_attn", "xformers", "math", nullptr };

// ============================================================================
// UpscaleSeedVR2Plugin Implementation
// ============================================================================

UpscaleSeedVR2Plugin::UpscaleSeedVR2Plugin(OfxImageEffectHandle handle)
    : BasePlugin(handle)
    , _seed(nullptr), _resolution(nullptr), _maxResolution(nullptr)
    , _batchSize(nullptr), _uniformBatchSize(nullptr), _colorCorrection(nullptr)
    , _temporalOverlap(nullptr), _prependFrames(nullptr)
    , _inputNoiseScale(nullptr), _latentNoiseScale(nullptr)
    , _enableDebug(nullptr), _imageLoadCap(nullptr)
    , _vaeModel(nullptr), _vaeDevice(nullptr)
    , _encodeTiled(nullptr), _encodeTileSize(nullptr), _encodeTileOverlap(nullptr)
    , _decodeTiled(nullptr), _decodeTileSize(nullptr), _decodeTileOverlap(nullptr)
    , _offloadVAE(nullptr), _cacheVAEModel(nullptr)
    , _ditModel(nullptr), _ditDevice(nullptr)
    , _blocksToSwap(nullptr), _swapIOComponents(nullptr)
    , _attentionMode(nullptr), _offloadDiT(nullptr), _cacheDiTModel(nullptr)
{
    _seed              = fetchIntParam("seed");
    _resolution        = fetchIntParam("resolution");
    _maxResolution     = fetchIntParam("maxResolution");
    _batchSize         = fetchIntParam("batchSize");
    _uniformBatchSize  = fetchBooleanParam("uniformBatchSize");
    _colorCorrection   = fetchChoiceParam("colorCorrection");
    _temporalOverlap   = fetchIntParam("temporalOverlap");
    _prependFrames     = fetchIntParam("prependFrames");
    _inputNoiseScale   = fetchDoubleParam("inputNoiseScale");
    _latentNoiseScale  = fetchDoubleParam("latentNoiseScale");
    _enableDebug       = fetchBooleanParam("enableDebug");
    _imageLoadCap      = fetchIntParam("imageLoadCap");

    _vaeModel          = fetchStringParam("vaeModel");
    _vaeDevice         = fetchStringParam("vaeDevice");
    _encodeTiled       = fetchBooleanParam("encodeTiled");
    _encodeTileSize    = fetchIntParam("encodeTileSize");
    _encodeTileOverlap = fetchIntParam("encodeTileOverlap");
    _decodeTiled       = fetchBooleanParam("decodeTiled");
    _decodeTileSize    = fetchIntParam("decodeTileSize");
    _decodeTileOverlap = fetchIntParam("decodeTileOverlap");
    _offloadVAE        = fetchStringParam("offloadVAE");
    _cacheVAEModel     = fetchBooleanParam("cacheVAEModel");

    _ditModel          = fetchStringParam("ditModel");
    _ditDevice         = fetchStringParam("ditDevice");
    _blocksToSwap      = fetchIntParam("blocksToSwap");
    _swapIOComponents  = fetchBooleanParam("swapIOComponents");
    _attentionMode     = fetchChoiceParam("attentionMode");
    _offloadDiT        = fetchStringParam("offloadDiT");
    _cacheDiTModel     = fetchBooleanParam("cacheDiTModel");
}

UpscaleSeedVR2Plugin::~UpscaleSeedVR2Plugin() {}

int UpscaleSeedVR2Plugin::getImageLoadCap() const {
    int cap = 0;
    if (_imageLoadCap) _imageLoadCap->getValue(cap);
    return cap;
}

OfxPointD UpscaleSeedVR2Plugin::getPredictedOutputScale(double time) const
{
    // Identity if we can't predict yet — the source RoD will be passed through.
    OfxPointD identity = { 1.0, 1.0 };

    if (!_srcClip || !_srcClip->isConnected() || !_resolution) {
        return identity;
    }

    int targetShortSide = 0;
    _resolution->getValueAtTime(time, targetShortSide);
    if (targetShortSide <= 0) return identity;

    // _srcClip->getRegionOfDefinition is non-const in the OFX Support API,
    // so cast away const on the cached clip pointer (we don't mutate it).
    OFX::Clip* src = const_cast<OFX::Clip*>(_srcClip);
    OfxRectD srcRod = src->getRegionOfDefinition(time);

    const double w = srcRod.x2 - srcRod.x1;
    const double h = srcRod.y2 - srcRod.y1;
    if (w <= 0.0 || h <= 0.0) return identity;

    // SeedVR2 fits the SHORT side to `resolution` and preserves aspect, so
    // the same scale factor applies to both axes.
    const double shortSide = std::min(w, h);
    const double scale = static_cast<double>(targetShortSide) / shortSide;

    // Don't pretend to scale by tiny amounts — anything within a pixel of
    // the source short side is treated as a no-op so we don't churn the
    // host's canvas for round-trip floating-point drift.
    if (std::abs(scale - 1.0) * shortSide < 1.0) {
        return identity;
    }

    OfxPointD out = { scale, scale };
    return out;
}

std::string UpscaleSeedVR2Plugin::getColorCorrectionName() const
{
    int idx = 0;
    _colorCorrection->getValue(idx);
    if (idx >= 0 && kColorCorrections[idx]) return kColorCorrections[idx];
    return "lab";
}

std::string UpscaleSeedVR2Plugin::getAttentionModeName() const
{
    int idx = 0;
    _attentionMode->getValue(idx);
    if (idx >= 0 && kAttentionModes[idx]) return kAttentionModes[idx];
    return "sdpa";
}

// ============================================================================
// Workflow building
// ============================================================================

json UpscaleSeedVR2Plugin::buildWorkflow(int frame,
                                          const std::map<std::string, std::string>& inputPaths)
{
    if (_logger) _logger->info("Building SeedVR2 upscale workflow for frame {}", frame);

    std::string inputPath;
    auto it = inputPaths.find("InputA");
    if (it != inputPaths.end()) {
        inputPath = it->second;
    } else if (!inputPaths.empty()) {
        inputPath = inputPaths.begin()->second;
    } else {
        throw std::runtime_error("No input path provided for SeedVR2 workflow");
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

json UpscaleSeedVR2Plugin::buildHardcodedWorkflow(int frame, const std::string& inputPath)
{
    if (_logger) _logger->info("Building hardcoded SeedVR2 workflow");

    // Collect all parameter values
    int seed, resolution, maxResolution, batchSize, temporalOverlap, prependFrames, imageLoadCap;
    bool uniformBatchSize, enableDebug;
    double inputNoiseScale, latentNoiseScale;

    _seed->getValue(seed);
    _resolution->getValue(resolution);
    _maxResolution->getValue(maxResolution);
    _batchSize->getValue(batchSize);
    _uniformBatchSize->getValue(uniformBatchSize);
    _temporalOverlap->getValue(temporalOverlap);
    _prependFrames->getValue(prependFrames);
    _inputNoiseScale->getValue(inputNoiseScale);
    _latentNoiseScale->getValue(latentNoiseScale);
    _enableDebug->getValue(enableDebug);
    _imageLoadCap->getValue(imageLoadCap);

    if (seed < 0) seed = static_cast<int>(std::time(nullptr));

    std::string vaeModel, vaeDevice, offloadVAE, ditModel, ditDevice, offloadDiT;
    bool encodeTiled, decodeTiled, swapIOComponents, cacheVAEModel, cacheDiTModel;
    int encodeTileSize, encodeTileOverlap, decodeTileSize, decodeTileOverlap, blocksToSwap;

    _vaeModel->getValue(vaeModel);
    _vaeDevice->getValue(vaeDevice);
    _encodeTiled->getValue(encodeTiled);
    _encodeTileSize->getValue(encodeTileSize);
    _encodeTileOverlap->getValue(encodeTileOverlap);
    _decodeTiled->getValue(decodeTiled);
    _decodeTileSize->getValue(decodeTileSize);
    _decodeTileOverlap->getValue(decodeTileOverlap);
    _offloadVAE->getValue(offloadVAE);
    _cacheVAEModel->getValue(cacheVAEModel);

    _ditModel->getValue(ditModel);
    _ditDevice->getValue(ditDevice);
    _blocksToSwap->getValue(blocksToSwap);
    _swapIOComponents->getValue(swapIOComponents);
    _offloadDiT->getValue(offloadDiT);
    _cacheDiTModel->getValue(cacheDiTModel);

    std::string colorCorrection = getColorCorrectionName();
    std::string attentionMode   = getAttentionModeName();

    std::string mountPath, project, workflowName, version;
    mountPath = getLocalMountPath();
    project = getTrimmedStringParam(_projectName);
    workflowName = getTrimmedStringParam(_workflowName);
    version = getTrimmedStringParam(_outputVersion);

    std::string basename = getEffectiveBasename();

    std::ostringstream outputPrefix;
    outputPrefix << mountPath << "/out/" << project << "/" << workflowName
                 << "/" << version << "/" << basename;

    std::string comfyInputPath    = convertPathForComfyUI(inputPath);
    std::string comfyOutputPrefix = convertPathForComfyUI(outputPrefix.str());

    if (_logger) {
        _logger->info("SeedVR2 parameters:");
        _logger->info("  Seed: {}, Resolution: {}, MaxRes: {}", seed, resolution, maxResolution);
        _logger->info("  BatchSize: {}, UniformBatch: {}", batchSize, uniformBatchSize);
        _logger->info("  ColorCorrection: {}, TemporalOverlap: {}", colorCorrection, temporalOverlap);
        _logger->info("  NoiseScale in/lat: {}/{}", inputNoiseScale, latentNoiseScale);
        _logger->info("  ImageLoadCap: {}", imageLoadCap);
        _logger->info("  VAE: {}, DiT: {}", vaeModel, ditModel);
        _logger->info("  AttentionMode: {}, BlocksToSwap: {}", attentionMode, blocksToSwap);
        _logger->info("  Input (ComfyUI): {}", comfyInputPath);
        _logger->info("  Output prefix (ComfyUI): {}", comfyOutputPrefix);
    }

    json workflow = {

        // Node 41: Load VAE model
        {"41", {
            {"inputs", {
                {"model",                vaeModel},
                {"device",               vaeDevice},
                {"encode_tiled",         encodeTiled},
                {"encode_tile_size",     encodeTileSize},
                {"encode_tile_overlap",  encodeTileOverlap},
                {"decode_tiled",         decodeTiled},
                {"decode_tile_size",     decodeTileSize},
                {"decode_tile_overlap",  decodeTileOverlap},
                {"tile_debug",           enableDebug ? "true" : "false"},
                {"offload_device",       offloadVAE},
                {"cache_model",          cacheVAEModel}
            }},
            {"class_type", "SeedVR2LoadVAEModel"},
            {"_meta", {{"title", "SeedVR2 Load VAE Model"}}}
        }},

        // Node 42: Load DiT model
        {"42", {
            {"inputs", {
                {"model",               ditModel},
                {"device",              ditDevice},
                {"blocks_to_swap",      blocksToSwap},
                {"swap_io_components",  swapIOComponents},
                {"offload_device",      offloadDiT},
                {"cache_model",         cacheDiTModel},
                {"attention_mode",      attentionMode}
            }},
            {"class_type", "SeedVR2LoadDiTModel"},
            {"_meta", {{"title", "SeedVR2 Load DiT Model"}}}
        }},

        // Node 43: Load EXR sequence
        {"43", {
            {"inputs", {
                {"filepath",          comfyInputPath},
                {"tonemap",           "linear"},
                {"image_load_cap",    imageLoadCap},
                {"skip_first_images", 0},
                {"select_every_nth",  1}
            }},
            {"class_type", "LoadEXR"},
            {"_meta", {{"title", "Load EXR"}}}
        }},

        // Node 44: SeedVR2 upscaler
        {"44", {
            {"inputs", {
                {"seed",               seed},
                {"resolution",         resolution},
                {"max_resolution",     maxResolution},
                {"batch_size",         batchSize},
                {"uniform_batch_size", uniformBatchSize},
                {"color_correction",   colorCorrection},
                {"temporal_overlap",   temporalOverlap},
                {"prepend_frames",     prependFrames},
                {"input_noise_scale",  inputNoiseScale},
                {"latent_noise_scale", latentNoiseScale},
                {"offload_device",     "cpu"},
                {"enable_debug",       enableDebug},
                {"image",              json::array({"43", 0})},
                {"dit",                json::array({"42", 0})},
                {"vae",                json::array({"41", 0})}
            }},
            {"class_type", "SeedVR2VideoUpscaler"},
            {"_meta", {{"title", "SeedVR2 Video Upscaler"}}}
        }},

        // Node 45: Save EXR
        {"45", {
            {"inputs", {
                {"filename_prefix", comfyOutputPrefix},
                {"tonemap",         "linear"},
                {"version",         -1},
                {"start_frame",     frame},
                {"frame_pad",       4},
                {"save_workflow",   "none"},
                {"create_path_if_missing", true},
                {"images",          json::array({"44", 0})}
            }},
            {"class_type", "SaveEXR"},
            {"_meta", {{"title", "Save EXR"}}}
        }}
    };

    if (_logger) {
        _logger->info("=== SEEDVR2 WORKFLOW DUMP ===");
        _logger->info("{}", workflow.dump(2));
        _logger->info("=== END WORKFLOW DUMP ===");
    }

    return workflow;
}

json UpscaleSeedVR2Plugin::customizeWorkflowWithParams(const json& baseWorkflow, int frame)
{
    if (_logger) _logger->debug("Customizing SeedVR2 workflow with parameters");

    int seed, resolution, maxResolution, batchSize, temporalOverlap, prependFrames, imageLoadCap;
    bool uniformBatchSize, enableDebug;
    double inputNoiseScale, latentNoiseScale;

    _seed->getValue(seed);
    _resolution->getValue(resolution);
    _maxResolution->getValue(maxResolution);
    _batchSize->getValue(batchSize);
    _uniformBatchSize->getValue(uniformBatchSize);
    _temporalOverlap->getValue(temporalOverlap);
    _prependFrames->getValue(prependFrames);
    _inputNoiseScale->getValue(inputNoiseScale);
    _latentNoiseScale->getValue(latentNoiseScale);
    _enableDebug->getValue(enableDebug);
    _imageLoadCap->getValue(imageLoadCap);

    if (seed < 0) seed = static_cast<int>(std::time(nullptr));

    std::string vaeModel, vaeDevice, offloadVAE, ditModel, ditDevice, offloadDiT;
    bool encodeTiled, decodeTiled, swapIOComponents, cacheVAEModel, cacheDiTModel;
    int encodeTileSize, encodeTileOverlap, decodeTileSize, decodeTileOverlap, blocksToSwap;

    _vaeModel->getValue(vaeModel);
    _vaeDevice->getValue(vaeDevice);
    _encodeTiled->getValue(encodeTiled);
    _encodeTileSize->getValue(encodeTileSize);
    _encodeTileOverlap->getValue(encodeTileOverlap);
    _decodeTiled->getValue(decodeTiled);
    _decodeTileSize->getValue(decodeTileSize);
    _decodeTileOverlap->getValue(decodeTileOverlap);
    _offloadVAE->getValue(offloadVAE);
    _cacheVAEModel->getValue(cacheVAEModel);

    _ditModel->getValue(ditModel);
    _ditDevice->getValue(ditDevice);
    _blocksToSwap->getValue(blocksToSwap);
    _swapIOComponents->getValue(swapIOComponents);
    _offloadDiT->getValue(offloadDiT);
    _cacheDiTModel->getValue(cacheDiTModel);

    std::string colorCorrection = getColorCorrectionName();
    std::string attentionMode   = getAttentionModeName();

    std::string workflowStr = baseWorkflow.dump();

    // String replacements (stay quoted)
    auto replaceStr = [&](const std::string& placeholder, const std::string& value) {
        size_t pos = 0;
        while ((pos = workflowStr.find(placeholder, pos)) != std::string::npos) {
            workflowStr.replace(pos, placeholder.length(), value);
            pos += value.length();
        }
    };
    // Numeric/bool replacements (strip surrounding quotes)
    auto replaceNumeric = [&](const std::string& quotedPlaceholder, const std::string& numStr) {
        size_t pos = 0;
        while ((pos = workflowStr.find(quotedPlaceholder, pos)) != std::string::npos) {
            workflowStr.replace(pos, quotedPlaceholder.length(), numStr);
            pos += numStr.length();
        }
    };

    // String params
    replaceStr("${VAE_MODEL}",    vaeModel);
    replaceStr("${VAE_DEVICE}",   vaeDevice);
    replaceStr("${OFFLOAD_VAE}",  offloadVAE);
    replaceStr("${DIT_MODEL}",    ditModel);
    replaceStr("${DIT_DEVICE}",   ditDevice);
    replaceStr("${OFFLOAD_DIT}",  offloadDiT);
    replaceStr("${OFFLOAD_UPSCALER}", "cpu");
    replaceStr("${COLOR_CORRECTION}", colorCorrection);
    replaceStr("${ATTENTION_MODE}",   attentionMode);
    // tile_debug is a string "true"/"false" in the workflow
    replaceStr("${TILE_DEBUG}", enableDebug ? "true" : "false");

    // Numeric params
    replaceNumeric("\"${SEED}\"",              std::to_string(seed));
    replaceNumeric("\"${RESOLUTION}\"",        std::to_string(resolution));
    replaceNumeric("\"${MAX_RESOLUTION}\"",    std::to_string(maxResolution));
    replaceNumeric("\"${BATCH_SIZE}\"",        std::to_string(batchSize));
    replaceNumeric("\"${TEMPORAL_OVERLAP}\"",  std::to_string(temporalOverlap));
    replaceNumeric("\"${PREPEND_FRAMES}\"",    std::to_string(prependFrames));
    replaceNumeric("\"${INPUT_NOISE_SCALE}\"", std::to_string(inputNoiseScale));
    replaceNumeric("\"${LATENT_NOISE_SCALE}\"",std::to_string(latentNoiseScale));
    replaceNumeric("\"${IMAGE_LOAD_CAP}\"",    std::to_string(imageLoadCap));
    replaceNumeric("\"${FRAME}\"",             std::to_string(frame));
    replaceNumeric("\"${ENCODE_TILE_SIZE}\"",  std::to_string(encodeTileSize));
    replaceNumeric("\"${ENCODE_TILE_OVERLAP}\"", std::to_string(encodeTileOverlap));
    replaceNumeric("\"${DECODE_TILE_SIZE}\"",  std::to_string(decodeTileSize));
    replaceNumeric("\"${DECODE_TILE_OVERLAP}\"", std::to_string(decodeTileOverlap));
    replaceNumeric("\"${BLOCKS_TO_SWAP}\"",    std::to_string(blocksToSwap));

    // Booleans
    replaceNumeric("\"${UNIFORM_BATCH_SIZE}\"",  uniformBatchSize  ? "true" : "false");
    replaceNumeric("\"${ENCODE_TILED}\"",         encodeTiled       ? "true" : "false");
    replaceNumeric("\"${DECODE_TILED}\"",         decodeTiled       ? "true" : "false");
    replaceNumeric("\"${SWAP_IO_COMPONENTS}\"",   swapIOComponents  ? "true" : "false");
    replaceNumeric("\"${CACHE_VAE_MODEL}\"",      cacheVAEModel     ? "true" : "false");
    replaceNumeric("\"${CACHE_DIT_MODEL}\"",      cacheDiTModel     ? "true" : "false");
    replaceNumeric("\"${ENABLE_DEBUG}\"",         enableDebug       ? "true" : "false");

    try {
        json finalWorkflow = json::parse(workflowStr);
        if (_logger) {
            _logger->info("=== SEEDVR2 FINAL WORKFLOW ===");
            _logger->info("{}", finalWorkflow.dump(2));
            _logger->info("=== END FINAL WORKFLOW ===");
        }
        return finalWorkflow;
    } catch (const json::exception& e) {
        if (_logger) _logger->error("Failed to parse customized SeedVR2 workflow: {}", e.what());
        throw std::runtime_error("Failed to parse SeedVR2 customized workflow: " + std::string(e.what()));
    }
}

std::vector<std::string> UpscaleSeedVR2Plugin::getRequiredModels()
{
    return {"seedvr2"};
}

// ============================================================================
// Parameter Definitions
// ============================================================================

void UpscaleSeedVR2Plugin::describeInContext(OFX::ImageEffectDescriptor &desc,
                                              OFX::ContextEnum context,
                                              const json* configDefaults)
{
    OFX::PageParamDescriptor *page = desc.definePageParam("Controls");

    // ==== UPSCALING GROUP ====
    OFX::GroupParamDescriptor *upscaleGroup = desc.defineGroupParam("upscaleGroup");
    upscaleGroup->setLabel("Upscaling");
    upscaleGroup->setOpen(true);
    page->addChild(*upscaleGroup);

    // Seed
    OFX::IntParamDescriptor *seed = desc.defineIntParam("seed");
    seed->setLabel("Seed");
    seed->setHint("Random seed for the diffusion process. Set to -1 for a random seed each render.");
    seed->setDefault(42);
    seed->setRange(-1, INT_MAX);
    seed->setDisplayRange(-1, 1000);
    seed->setAnimates(false);
    seed->setParent(*upscaleGroup);
    page->addChild(*seed);

    // Resolution
    OFX::IntParamDescriptor *resolution = desc.defineIntParam("resolution");
    resolution->setLabel("Resolution");
    resolution->setHint("Target output resolution for the shorter side (e.g. 1080 upscales to 1080p). "
                        "The model will upscale the input to this resolution.");
    resolution->setDefault(1080);
    resolution->setRange(256, 8192);
    resolution->setDisplayRange(512, 4096);
    resolution->setAnimates(false);
    resolution->setParent(*upscaleGroup);
    page->addChild(*resolution);

    // Max resolution
    OFX::IntParamDescriptor *maxResolution = desc.defineIntParam("maxResolution");
    maxResolution->setLabel("Max Resolution");
    maxResolution->setHint("Maximum output resolution cap. Set to 0 to disable (no cap).");
    maxResolution->setDefault(0);
    maxResolution->setRange(0, 8192);
    maxResolution->setDisplayRange(0, 4096);
    maxResolution->setAnimates(false);
    maxResolution->setParent(*upscaleGroup);
    page->addChild(*maxResolution);

    // Batch size
    OFX::IntParamDescriptor *batchSize = desc.defineIntParam("batchSize");
    batchSize->setLabel("Batch Size");
    batchSize->setHint("Number of frames per inference batch. Larger batches improve temporal consistency "
                       "but require more VRAM. Must be odd (e.g. 33). Reduce if you get OOM errors.");
    batchSize->setDefault(33);
    batchSize->setRange(1, 128);
    batchSize->setDisplayRange(1, 65);
    batchSize->setAnimates(false);
    batchSize->setParent(*upscaleGroup);
    page->addChild(*batchSize);

    // Temporal overlap
    OFX::IntParamDescriptor *temporalOverlap = desc.defineIntParam("temporalOverlap");
    temporalOverlap->setLabel("Temporal Overlap");
    temporalOverlap->setHint("Number of frames to overlap between consecutive batches for smoother transitions.");
    temporalOverlap->setDefault(3);
    temporalOverlap->setRange(0, 32);
    temporalOverlap->setDisplayRange(0, 16);
    temporalOverlap->setAnimates(false);
    temporalOverlap->setParent(*upscaleGroup);
    page->addChild(*temporalOverlap);

    // Color correction
    OFX::ChoiceParamDescriptor *colorCorrection = desc.defineChoiceParam("colorCorrection");
    colorCorrection->setLabel("Color Correction");
    colorCorrection->setHint("Method used to match the output color to the input. "
                              "'lab' (CIELab) is recommended for most footage.");
    colorCorrection->appendOption("none");
    colorCorrection->appendOption("lab");
    colorCorrection->appendOption("hm");
    colorCorrection->setDefault(1); // "lab"
    colorCorrection->setAnimates(false);
    colorCorrection->setParent(*upscaleGroup);
    page->addChild(*colorCorrection);

    // Image load cap
    OFX::IntParamDescriptor *imageLoadCap = desc.defineIntParam("imageLoadCap");
    imageLoadCap->setLabel("Frame Limit");
    imageLoadCap->setHint("Maximum number of frames to load from the input sequence. Set to 0 for no limit.");
    imageLoadCap->setDefault(10);
    imageLoadCap->setRange(0, 2048);
    imageLoadCap->setDisplayRange(0, 500);
    imageLoadCap->setAnimates(false);
    imageLoadCap->setParent(*upscaleGroup);
    page->addChild(*imageLoadCap);

    // ==== ADVANCED GROUP ====
    OFX::GroupParamDescriptor *advGroup = desc.defineGroupParam("advancedGroup");
    advGroup->setLabel("Advanced");
    advGroup->setOpen(false);
    page->addChild(*advGroup);

    // Uniform batch size
    OFX::BooleanParamDescriptor *uniformBatchSize = desc.defineBooleanParam("uniformBatchSize");
    uniformBatchSize->setLabel("Uniform Batch Size");
    uniformBatchSize->setHint("Pad all batches to the same size for consistent VRAM usage.");
    uniformBatchSize->setDefault(true);
    uniformBatchSize->setAnimates(false);
    uniformBatchSize->setParent(*advGroup);
    page->addChild(*uniformBatchSize);

    // Prepend frames
    OFX::IntParamDescriptor *prependFrames = desc.defineIntParam("prependFrames");
    prependFrames->setLabel("Prepend Frames");
    prependFrames->setHint("Number of black frames to prepend to the sequence before processing.");
    prependFrames->setDefault(0);
    prependFrames->setRange(0, 32);
    prependFrames->setDisplayRange(0, 16);
    prependFrames->setAnimates(false);
    prependFrames->setParent(*advGroup);
    page->addChild(*prependFrames);

    // Input noise scale
    OFX::DoubleParamDescriptor *inputNoiseScale = desc.defineDoubleParam("inputNoiseScale");
    inputNoiseScale->setLabel("Input Noise Scale");
    inputNoiseScale->setHint("Adds noise to the input before upscaling. 0 = no noise.");
    inputNoiseScale->setDefault(0.0);
    inputNoiseScale->setRange(0.0, 1.0);
    inputNoiseScale->setDisplayRange(0.0, 1.0);
    inputNoiseScale->setAnimates(false);
    inputNoiseScale->setParent(*advGroup);
    page->addChild(*inputNoiseScale);

    // Latent noise scale
    OFX::DoubleParamDescriptor *latentNoiseScale = desc.defineDoubleParam("latentNoiseScale");
    latentNoiseScale->setLabel("Latent Noise Scale");
    latentNoiseScale->setHint("Adds noise in latent space. 0 = no noise.");
    latentNoiseScale->setDefault(0.0);
    latentNoiseScale->setRange(0.0, 1.0);
    latentNoiseScale->setDisplayRange(0.0, 1.0);
    latentNoiseScale->setAnimates(false);
    latentNoiseScale->setParent(*advGroup);
    page->addChild(*latentNoiseScale);

    // Enable debug
    OFX::BooleanParamDescriptor *enableDebug = desc.defineBooleanParam("enableDebug");
    enableDebug->setLabel("Enable Debug");
    enableDebug->setHint("Enable tiling debug visualization and verbose ComfyUI output.");
    enableDebug->setDefault(false);
    enableDebug->setAnimates(false);
    enableDebug->setParent(*advGroup);
    page->addChild(*enableDebug);

    // ==== VAE MODEL GROUP ====
    OFX::GroupParamDescriptor *vaeGroup = desc.defineGroupParam("vaeGroup");
    vaeGroup->setLabel("VAE Model");
    vaeGroup->setOpen(false);
    page->addChild(*vaeGroup);

    OFX::StringParamDescriptor *vaeModel = desc.defineStringParam("vaeModel");
    vaeModel->setLabel("VAE Model");
    vaeModel->setHint("Filename of the SeedVR2 VAE checkpoint in the ComfyUI models directory.");
    vaeModel->setDefault("ema_vae_fp16.safetensors");
    vaeModel->setAnimates(false);
    vaeModel->setParent(*vaeGroup);
    page->addChild(*vaeModel);

    OFX::StringParamDescriptor *vaeDevice = desc.defineStringParam("vaeDevice");
    vaeDevice->setLabel("VAE Device");
    vaeDevice->setHint("Compute device for the VAE (e.g. cuda:0, cuda:1).");
    vaeDevice->setDefault("cuda:0");
    vaeDevice->setAnimates(false);
    vaeDevice->setParent(*vaeGroup);
    page->addChild(*vaeDevice);

    OFX::StringParamDescriptor *offloadVAE = desc.defineStringParam("offloadVAE");
    offloadVAE->setLabel("Offload Device");
    offloadVAE->setHint("Device to offload the VAE model to when not in use (e.g. cpu).");
    offloadVAE->setDefault("cpu");
    offloadVAE->setAnimates(false);
    offloadVAE->setParent(*vaeGroup);
    page->addChild(*offloadVAE);

    OFX::BooleanParamDescriptor *encodeTiled = desc.defineBooleanParam("encodeTiled");
    encodeTiled->setLabel("Encode Tiled");
    encodeTiled->setHint("Tile the VAE encoding pass to reduce VRAM usage.");
    encodeTiled->setDefault(true);
    encodeTiled->setAnimates(false);
    encodeTiled->setParent(*vaeGroup);
    page->addChild(*encodeTiled);

    OFX::IntParamDescriptor *encodeTileSize = desc.defineIntParam("encodeTileSize");
    encodeTileSize->setLabel("Encode Tile Size");
    encodeTileSize->setHint("Pixel size of each tile during VAE encoding.");
    encodeTileSize->setDefault(1024);
    encodeTileSize->setRange(64, 4096);
    encodeTileSize->setDisplayRange(256, 2048);
    encodeTileSize->setAnimates(false);
    encodeTileSize->setParent(*vaeGroup);
    page->addChild(*encodeTileSize);

    OFX::IntParamDescriptor *encodeTileOverlap = desc.defineIntParam("encodeTileOverlap");
    encodeTileOverlap->setLabel("Encode Tile Overlap");
    encodeTileOverlap->setHint("Overlap between encoding tiles to reduce seam artifacts.");
    encodeTileOverlap->setDefault(128);
    encodeTileOverlap->setRange(0, 512);
    encodeTileOverlap->setDisplayRange(0, 256);
    encodeTileOverlap->setAnimates(false);
    encodeTileOverlap->setParent(*vaeGroup);
    page->addChild(*encodeTileOverlap);

    OFX::BooleanParamDescriptor *decodeTiled = desc.defineBooleanParam("decodeTiled");
    decodeTiled->setLabel("Decode Tiled");
    decodeTiled->setHint("Tile the VAE decoding pass to reduce VRAM usage.");
    decodeTiled->setDefault(true);
    decodeTiled->setAnimates(false);
    decodeTiled->setParent(*vaeGroup);
    page->addChild(*decodeTiled);

    OFX::IntParamDescriptor *decodeTileSize = desc.defineIntParam("decodeTileSize");
    decodeTileSize->setLabel("Decode Tile Size");
    decodeTileSize->setHint("Pixel size of each tile during VAE decoding.");
    decodeTileSize->setDefault(768);
    decodeTileSize->setRange(64, 4096);
    decodeTileSize->setDisplayRange(256, 2048);
    decodeTileSize->setAnimates(false);
    decodeTileSize->setParent(*vaeGroup);
    page->addChild(*decodeTileSize);

    OFX::IntParamDescriptor *decodeTileOverlap = desc.defineIntParam("decodeTileOverlap");
    decodeTileOverlap->setLabel("Decode Tile Overlap");
    decodeTileOverlap->setHint("Overlap between decoding tiles to reduce seam artifacts.");
    decodeTileOverlap->setDefault(128);
    decodeTileOverlap->setRange(0, 512);
    decodeTileOverlap->setDisplayRange(0, 256);
    decodeTileOverlap->setAnimates(false);
    decodeTileOverlap->setParent(*vaeGroup);
    page->addChild(*decodeTileOverlap);

    OFX::BooleanParamDescriptor *cacheVAEModel = desc.defineBooleanParam("cacheVAEModel");
    cacheVAEModel->setLabel("Cache Model");
    cacheVAEModel->setHint("Keep the VAE model loaded in VRAM between jobs.");
    cacheVAEModel->setDefault(false);
    cacheVAEModel->setAnimates(false);
    cacheVAEModel->setParent(*vaeGroup);
    page->addChild(*cacheVAEModel);

    // ==== DiT MODEL GROUP ====
    OFX::GroupParamDescriptor *ditGroup = desc.defineGroupParam("ditGroup");
    ditGroup->setLabel("DiT Model");
    ditGroup->setOpen(false);
    page->addChild(*ditGroup);

    OFX::StringParamDescriptor *ditModel = desc.defineStringParam("ditModel");
    ditModel->setLabel("DiT Model");
    ditModel->setHint("Filename of the SeedVR2 DiT checkpoint in the ComfyUI models directory.");
    ditModel->setDefault("seedvr2_ema_7b_fp8_e4m3fn_mixed_block35_fp16.safetensors");
    ditModel->setAnimates(false);
    ditModel->setParent(*ditGroup);
    page->addChild(*ditModel);

    OFX::StringParamDescriptor *ditDevice = desc.defineStringParam("ditDevice");
    ditDevice->setLabel("DiT Device");
    ditDevice->setHint("Compute device for the DiT model (e.g. cuda:0, cuda:1).");
    ditDevice->setDefault("cuda:0");
    ditDevice->setAnimates(false);
    ditDevice->setParent(*ditGroup);
    page->addChild(*ditDevice);

    OFX::StringParamDescriptor *offloadDiT = desc.defineStringParam("offloadDiT");
    offloadDiT->setLabel("Offload Device");
    offloadDiT->setHint("Device to offload the DiT model to when not in use (e.g. cpu).");
    offloadDiT->setDefault("cpu");
    offloadDiT->setAnimates(false);
    offloadDiT->setParent(*ditGroup);
    page->addChild(*offloadDiT);

    OFX::IntParamDescriptor *blocksToSwap = desc.defineIntParam("blocksToSwap");
    blocksToSwap->setLabel("Blocks To Swap");
    blocksToSwap->setHint("Number of DiT transformer blocks to swap to the offload device during inference. "
                           "Higher values reduce VRAM at the cost of speed.");
    blocksToSwap->setDefault(32);
    blocksToSwap->setRange(0, 128);
    blocksToSwap->setDisplayRange(0, 64);
    blocksToSwap->setAnimates(false);
    blocksToSwap->setParent(*ditGroup);
    page->addChild(*blocksToSwap);

    OFX::BooleanParamDescriptor *swapIOComponents = desc.defineBooleanParam("swapIOComponents");
    swapIOComponents->setLabel("Swap IO Components");
    swapIOComponents->setHint("Also swap the DiT input/output projection layers to the offload device.");
    swapIOComponents->setDefault(true);
    swapIOComponents->setAnimates(false);
    swapIOComponents->setParent(*ditGroup);
    page->addChild(*swapIOComponents);

    OFX::ChoiceParamDescriptor *attentionMode = desc.defineChoiceParam("attentionMode");
    attentionMode->setLabel("Attention Mode");
    attentionMode->setHint("Attention implementation. 'sdpa' (PyTorch scaled dot-product attention) is the default "
                            "and works on all hardware. 'flash_attn' / 'xformers' require extra packages.");
    attentionMode->appendOption("sdpa");
    attentionMode->appendOption("flash_attn");
    attentionMode->appendOption("xformers");
    attentionMode->appendOption("math");
    attentionMode->setDefault(0); // "sdpa"
    attentionMode->setAnimates(false);
    attentionMode->setParent(*ditGroup);
    page->addChild(*attentionMode);

    OFX::BooleanParamDescriptor *cacheDiTModel = desc.defineBooleanParam("cacheDiTModel");
    cacheDiTModel->setLabel("Cache Model");
    cacheDiTModel->setHint("Keep the DiT model loaded in VRAM between jobs.");
    cacheDiTModel->setDefault(false);
    cacheDiTModel->setAnimates(false);
    cacheDiTModel->setParent(*ditGroup);
    page->addChild(*cacheDiTModel);

    // Common parameters (server, project, cache, etc.)
    BasePlugin::describeCommonParameters(desc, context, page, page, page, configDefaults,
                                         /*skipGroupHeaders=*/false, /*isSequencePlugin=*/true);

    // Override workflowName default
    OFX::StringParamDescriptor *workflowName = desc.defineStringParam("workflowName");
    workflowName->setDefault("upscale_seedvr2");
}

// ============================================================================
// UpscaleSeedVR2PluginFactory Implementation
// ============================================================================

UpscaleSeedVR2PluginFactory::UpscaleSeedVR2PluginFactory()
    : OFX::PluginFactoryHelper<UpscaleSeedVR2PluginFactory>(
        "com.comfyui.UpscaleSeedVR2",
        1,
        0
    )
{
}

json UpscaleSeedVR2PluginFactory::loadConfigDefaults()
{
    // Search the UpscaleSeedVR2 bundle across all platform OFX plugin locations.
    std::vector<std::string> searchPaths =
        getOfxConfigSearchPaths({"UpscaleSeedVR2"});

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

void UpscaleSeedVR2PluginFactory::describe(OFX::ImageEffectDescriptor &desc)
{
    desc.setLabel("ComfyUI SeedVR2 Upscale");
    desc.setLabels("ComfyUI SeedVR2 Upscale", "SeedVR2 Upscale", "ComfyUI SeedVR2 Video Upscaler");
    desc.setPluginGrouping("ComfyUI");

    desc.setPluginDescription(
        "Diffusion-based video upscaling using SeedVR2 via ComfyUI.\n\n"
        "SeedVR2 (Seed Video Restoration 2) is a large-scale diffusion model that performs "
        "temporally-consistent video super-resolution. It uses separate VAE and DiT (Diffusion "
        "Transformer) models to upscale video sequences to high resolutions.\n\n"
        "Typical settings:\n"
        "- Resolution 1080 with Batch Size 33 for HD upscaling\n"
        "- Color Correction 'lab' for natural colour matching\n"
        "- Blocks To Swap 32+ for GPUs with limited VRAM\n"
        "- Reduce Batch Size if you encounter out-of-memory errors\n\n"
        "Note: SeedVR2 processes the full sequence in batches, not per-frame. "
        "Frame Limit controls how many frames are loaded from the input sequence.\n\n"
        "Requires:\n"
        "- ComfyUI server with ComfyUI-SeedVR2_VideoUpscaler extension\n"
        "- SeedVR2 VAE and DiT model checkpoints\n"
        "- Shared network storage for image exchange\n\n"
        "Based on: https://github.com/kijai/ComfyUI-SeedVR2_VideoUpscaler"
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

void UpscaleSeedVR2PluginFactory::describeInContext(OFX::ImageEffectDescriptor &desc,
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
    UpscaleSeedVR2Plugin::describeInContext(desc, context, &configDefaults);
}

OFX::ImageEffect* UpscaleSeedVR2PluginFactory::createInstance(OfxImageEffectHandle handle,
                                                                OFX::ContextEnum /*context*/)
{
    return new UpscaleSeedVR2Plugin(handle);
}

} // namespace ComfyUI

// ============================================================================
// Plugin Registration
// ============================================================================

namespace OFX {
namespace Plugin {

void getPluginIDs(OFX::PluginFactoryArray &ids)
{
    static ComfyUI::UpscaleSeedVR2PluginFactory factory;
    ids.push_back(&factory);
}

} // namespace Plugin
} // namespace OFX
