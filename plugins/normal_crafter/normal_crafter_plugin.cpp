// Copyright OpenFX and contributors to the OpenFX project.
// SPDX-License-Identifier: BSD-3-Clause

#include "normal_crafter_plugin.h"
#include <sstream>
#include <fstream>
#include <algorithm>

namespace ComfyUI {

// ============================================================================
// NormalCrafterPlugin Implementation
// ============================================================================

NormalCrafterPlugin::NormalCrafterPlugin(OfxImageEffectHandle handle)
    : BasePlugin(handle)
    , _seed(nullptr)
    , _maxResDimension(nullptr)
    , _windowSize(nullptr)
    , _timeStepSize(nullptr)
    , _decodeChunkSize(nullptr)
    , _imageLoadCap(nullptr)
    , _offloadPipeToCpuOnFinish(nullptr)
    , _useXformers(nullptr)
{
    _seed                     = fetchIntParam("seed");
    _maxResDimension          = fetchIntParam("maxResDimension");
    _windowSize               = fetchIntParam("windowSize");
    _timeStepSize             = fetchIntParam("timeStepSize");
    _decodeChunkSize          = fetchIntParam("decodeChunkSize");
    _imageLoadCap             = fetchIntParam("imageLoadCap");
    _offloadPipeToCpuOnFinish = fetchBooleanParam("offloadPipeToCpuOnFinish");
    _useXformers              = fetchChoiceParam("useXformers");
}

NormalCrafterPlugin::~NormalCrafterPlugin() {}

int NormalCrafterPlugin::getImageLoadCap() const {
    int cap = 0;
    if (_imageLoadCap) _imageLoadCap->getValue(cap);
    return cap;
}

const char* NormalCrafterPlugin::xformersOptionToString(int idx) {
    switch (idx) {
        case 0:  return "auto";
        case 1:  return "enable";
        case 2:  return "disable";
        default: return "auto";
    }
}

// ============================================================================
// Workflow building
// ============================================================================

json NormalCrafterPlugin::buildWorkflow(int frame,
                                        const std::map<std::string, std::string>& inputPaths)
{
    if (_logger) _logger->info("Building NormalCrafter workflow for frame {}", frame);

    std::string inputPath;
    auto it = inputPaths.find("InputA");
    if (it != inputPaths.end()) {
        inputPath = it->second;
    } else if (!inputPaths.empty()) {
        inputPath = inputPaths.begin()->second;
    } else {
        throw std::runtime_error("No input path provided for NormalCrafter workflow");
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

json NormalCrafterPlugin::buildHardcodedWorkflow(int frame, const std::string& inputPath)
{
    if (_logger) _logger->info("Building hardcoded NormalCrafter workflow");

    bool offloadPipeToCpuOnFinish;
    int seed, maxResDimension, windowSize, timeStepSize, decodeChunkSize, imageLoadCap, useXformersIdx;

    _seed->getValue(seed);
    _maxResDimension->getValue(maxResDimension);
    _windowSize->getValue(windowSize);
    _timeStepSize->getValue(timeStepSize);
    _decodeChunkSize->getValue(decodeChunkSize);
    _imageLoadCap->getValue(imageLoadCap);
    _offloadPipeToCpuOnFinish->getValue(offloadPipeToCpuOnFinish);
    _useXformers->getValue(useXformersIdx);

    // Clamp window_size so NormalCrafter never allocates more than the actual frame count.
    int effectiveWindowSize = (imageLoadCap > 0) ? std::min(windowSize, imageLoadCap) : windowSize;

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

    const char* useXformersStr = xformersOptionToString(useXformersIdx);

    if (_logger) {
        _logger->info("NormalCrafter parameters:");
        _logger->info("  Seed: {}", seed);
        _logger->info("  Max res dimension: {}", maxResDimension);
        _logger->info("  Window size: {} (clamped from {})", effectiveWindowSize, windowSize);
        _logger->info("  Time step size: {}", timeStepSize);
        _logger->info("  Decode chunk size: {}", decodeChunkSize);
        _logger->info("  Image load cap: {}", imageLoadCap);
        _logger->info("  Offload pipe on finish: {}", offloadPipeToCpuOnFinish);
        _logger->info("  Use xFormers: {}", useXformersStr);
        _logger->info("  Input (ComfyUI): {}", comfyInputPath);
        _logger->info("  Output prefix (ComfyUI): {}", comfyOutputPrefix);
    }

    json workflow = {

        // Node 1: Load EXR sequence
        {"1", {
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

        // Node 2: NormalCrafter inference (window_size clamped to imageLoadCap)
        {"2", {
            {"inputs", {
                {"seed", seed},
                {"max_res_dimension", maxResDimension},
                {"window_size", effectiveWindowSize},
                {"time_step_size", timeStepSize},
                {"decode_chunk_size", decodeChunkSize},
                {"offload_pipe_to_cpu_on_finish", offloadPipeToCpuOnFinish},
                {"use_xformers", useXformersStr},
                {"images", json::array({"1", 0})}
            }},
            {"class_type", "NormalCrafterNode"},
            {"_meta", {{"title", "NormalCrafter (Process Video)"}}}
        }},

        // Node 4: Save normal map as EXR
        {"4", {
            {"inputs", {
                {"filename_prefix", comfyOutputPrefix},
                {"tonemap", "linear"},
                {"version", -1},
                {"start_frame", frame},
                {"frame_pad", 4},
                {"save_workflow", "none"},
                {"create_path_if_missing", true},
                {"images", json::array({"2", 0})}
            }},
            {"class_type", "SaveEXR"},
            {"_meta", {{"title", "Save EXR"}}}
        }}
    };

    if (_logger) {
        _logger->info("=== NORMAL CRAFTER WORKFLOW DUMP ===");
        _logger->info("{}", workflow.dump(2));
        _logger->info("=== END WORKFLOW DUMP ===");
    }

    return workflow;
}

json NormalCrafterPlugin::customizeWorkflowWithParams(const json& baseWorkflow, int frame)
{
    if (_logger) _logger->debug("Customizing NormalCrafter workflow with parameters");

    bool offloadPipeToCpuOnFinish;
    int seed, maxResDimension, windowSize, timeStepSize, decodeChunkSize, imageLoadCap, useXformersIdx;

    _seed->getValue(seed);
    _maxResDimension->getValue(maxResDimension);
    _windowSize->getValue(windowSize);
    _timeStepSize->getValue(timeStepSize);
    _decodeChunkSize->getValue(decodeChunkSize);
    _imageLoadCap->getValue(imageLoadCap);
    _offloadPipeToCpuOnFinish->getValue(offloadPipeToCpuOnFinish);
    _useXformers->getValue(useXformersIdx);

    // Clamp window_size to imageLoadCap so NormalCrafter never allocates a temporal
    // attention window larger than the actual frame count — the primary OOM cause.
    int effectiveWindowSize = (imageLoadCap > 0) ? std::min(windowSize, imageLoadCap) : windowSize;
    if (effectiveWindowSize != windowSize) {
        if (_logger) {
            _logger->info("Clamped window_size {} → {} (imageLoadCap={})",
                          windowSize, effectiveWindowSize, imageLoadCap);
        }
    }

    const char* useXformersStr = xformersOptionToString(useXformersIdx);

    std::string workflowStr = baseWorkflow.dump();

    auto replaceQuoted = [&](const std::string& quotedPlaceholder, const std::string& numStr) {
        size_t pos = 0;
        while ((pos = workflowStr.find(quotedPlaceholder, pos)) != std::string::npos) {
            workflowStr.replace(pos, quotedPlaceholder.length(), numStr);
            pos += numStr.length();
        }
    };

    auto replaceUnquoted = [&](const std::string& placeholder, const std::string& value) {
        size_t pos = 0;
        while ((pos = workflowStr.find(placeholder, pos)) != std::string::npos) {
            workflowStr.replace(pos, placeholder.length(), value);
            pos += value.length();
        }
    };

    // Quoted-placeholder replacements convert "string"-typed JSON values into
    // numeric/boolean primitives. Pattern: workflow has "key": "${TOKEN}".
    replaceQuoted("\"${SEED}\"",                       std::to_string(seed));
    replaceQuoted("\"${MAX_RES_DIMENSION}\"",          std::to_string(maxResDimension));
    replaceQuoted("\"${WINDOW_SIZE}\"",                std::to_string(effectiveWindowSize));
    replaceQuoted("\"${TIME_STEP_SIZE}\"",             std::to_string(timeStepSize));
    replaceQuoted("\"${DECODE_CHUNK_SIZE}\"",          std::to_string(decodeChunkSize));
    replaceQuoted("\"${IMAGE_LOAD_CAP}\"",             std::to_string(imageLoadCap));
    replaceQuoted("\"${OFFLOAD_PIPE_TO_CPU_ON_FINISH}\"", offloadPipeToCpuOnFinish ? "true" : "false");

    // Unquoted: USE_XFORMERS is already inside JSON quotes in the template
    // ("use_xformers": "${USE_XFORMERS}"), so we substitute the bare string.
    replaceUnquoted("${USE_XFORMERS}", useXformersStr);

    try {
        json finalWorkflow = json::parse(workflowStr);
        if (_logger) {
            _logger->info("=== NORMAL CRAFTER FINAL WORKFLOW ===");
            _logger->info("{}", finalWorkflow.dump(2));
            _logger->info("=== END FINAL WORKFLOW ===");
        }
        return finalWorkflow;
    } catch (const json::exception& e) {
        if (_logger) _logger->error("Failed to parse customized NormalCrafter workflow: {}", e.what());
        throw std::runtime_error("Failed to parse NormalCrafter customized workflow: " + std::string(e.what()));
    }
}

std::vector<std::string> NormalCrafterPlugin::getRequiredModels()
{
    return {"normalcrafter"};
}

// ============================================================================
// Parameter Definitions
// ============================================================================

void NormalCrafterPlugin::describeInContext(OFX::ImageEffectDescriptor &desc,
                                            OFX::ContextEnum context,
                                            const json* configDefaults)
{
    OFX::PageParamDescriptor *page = desc.definePageParam("Controls");

    // ==== NORMAL PROCESSING GROUP ====
    OFX::GroupParamDescriptor *normalGroup = desc.defineGroupParam("crafterNormalGroup");
    normalGroup->setLabel("Normal Processing");
    normalGroup->setOpen(true);
    page->addChild(*normalGroup);

    // Seed
    OFX::IntParamDescriptor *seed = desc.defineIntParam("seed");
    seed->setLabel("Seed");
    seed->setHint("Random seed for diffusion sampling. Change to vary the result; "
                  "keep the same value for reproducible runs.");
    seed->setDefault(0);
    seed->setRange(0, 2147483647);
    seed->setDisplayRange(0, 2147483647);
    seed->setAnimates(false);
    seed->setParent(*normalGroup);
    page->addChild(*seed);

    // Max resolution dimension
    OFX::IntParamDescriptor *maxResDimension = desc.defineIntParam("maxResDimension");
    maxResDimension->setLabel("Max Res Dimension");
    maxResDimension->setHint("Maximum resolution along the longest image side. "
                              "Larger values give finer detail but require more VRAM. 1024 is the recommended default.");
    maxResDimension->setDefault(1024);
    maxResDimension->setRange(256, 4096);
    maxResDimension->setDisplayRange(512, 2048);
    maxResDimension->setAnimates(false);
    maxResDimension->setParent(*normalGroup);
    page->addChild(*maxResDimension);

    // Window size
    OFX::IntParamDescriptor *windowSize = desc.defineIntParam("windowSize");
    windowSize->setLabel("Window Size");
    windowSize->setHint("Number of frames processed per temporal window. Must not exceed total frame count. "
                         "Larger windows improve temporal consistency but require more VRAM.");
    windowSize->setDefault(14);
    windowSize->setRange(2, 512);
    windowSize->setDisplayRange(4, 64);
    windowSize->setAnimates(false);
    windowSize->setParent(*normalGroup);
    page->addChild(*windowSize);

    // Time step size
    OFX::IntParamDescriptor *timeStepSize = desc.defineIntParam("timeStepSize");
    timeStepSize->setLabel("Time Step Size");
    timeStepSize->setHint("Diffusion time-step size. Higher values are faster but lower quality.");
    timeStepSize->setDefault(10);
    timeStepSize->setRange(1, 50);
    timeStepSize->setDisplayRange(1, 25);
    timeStepSize->setAnimates(false);
    timeStepSize->setParent(*normalGroup);
    page->addChild(*timeStepSize);

    // Decode chunk size
    OFX::IntParamDescriptor *decodeChunkSize = desc.defineIntParam("decodeChunkSize");
    decodeChunkSize->setLabel("Decode Chunk Size");
    decodeChunkSize->setHint("Number of frames decoded by the VAE at once. "
                              "Lower values use less VRAM at the cost of speed.");
    decodeChunkSize->setDefault(4);
    decodeChunkSize->setRange(1, 64);
    decodeChunkSize->setDisplayRange(1, 16);
    decodeChunkSize->setAnimates(false);
    decodeChunkSize->setParent(*normalGroup);
    page->addChild(*decodeChunkSize);

    // Image load cap
    OFX::IntParamDescriptor *imageLoadCap = desc.defineIntParam("imageLoadCap");
    imageLoadCap->setLabel("Frame Limit");
    imageLoadCap->setHint("Maximum number of frames to load from the sequence. "
                           "Set to 0 for no limit. Keep <= Window Size for best results.");
    imageLoadCap->setDefault(49);
    imageLoadCap->setRange(0, 1024);
    imageLoadCap->setDisplayRange(0, 200);
    imageLoadCap->setAnimates(false);
    imageLoadCap->setParent(*normalGroup);
    page->addChild(*imageLoadCap);

    // ==== MODEL GROUP ====
    OFX::GroupParamDescriptor *modelGroup = desc.defineGroupParam("crafterModelGroup");
    modelGroup->setLabel("Model");
    modelGroup->setOpen(false);
    page->addChild(*modelGroup);

    // Offload pipeline to CPU on finish
    OFX::BooleanParamDescriptor *offloadPipeToCpuOnFinish = desc.defineBooleanParam("offloadPipeToCpuOnFinish");
    offloadPipeToCpuOnFinish->setLabel("Offload Pipe On Finish");
    offloadPipeToCpuOnFinish->setHint("Move the NormalCrafter pipeline to CPU once inference completes, "
                                       "freeing VRAM for downstream nodes.");
    offloadPipeToCpuOnFinish->setDefault(true);
    offloadPipeToCpuOnFinish->setAnimates(false);
    offloadPipeToCpuOnFinish->setParent(*modelGroup);
    page->addChild(*offloadPipeToCpuOnFinish);

    // Use xFormers
    OFX::ChoiceParamDescriptor *useXformers = desc.defineChoiceParam("useXformers");
    useXformers->setLabel("Use xFormers");
    useXformers->setHint("Memory-efficient attention via xFormers. "
                          "Auto detects availability; Enable forces it on; Disable forces it off.");
    useXformers->appendOption("Auto",    "auto — use xFormers if available");
    useXformers->appendOption("Enable",  "enable — force xFormers on");
    useXformers->appendOption("Disable", "disable — never use xFormers");
    useXformers->setDefault(0); // auto
    useXformers->setAnimates(false);
    useXformers->setParent(*modelGroup);
    page->addChild(*useXformers);

    // Add common parameters — pass isSequencePlugin=true to omit the enable checkbox
    BasePlugin::describeCommonParameters(desc, context, page, page, page, configDefaults,
                                         /*skipGroupHeaders=*/false, /*isSequencePlugin=*/true);

    // Override workflowName default for this plugin
    OFX::StringParamDescriptor *workflow = desc.defineStringParam("workflowName");
    workflow->setDefault("normal_crafter");
}

// ============================================================================
// NormalCrafterPluginFactory Implementation
// ============================================================================

NormalCrafterPluginFactory::NormalCrafterPluginFactory()
    : OFX::PluginFactoryHelper<NormalCrafterPluginFactory>(
        "com.comfyui.NormalCrafter",
        1,
        0
    )
{
}

json NormalCrafterPluginFactory::loadNormalCrafterConfigDefaults()
{
    // Search the NormalCrafter bundle across all platform OFX plugin locations.
    std::vector<std::string> searchPaths =
        getOfxConfigSearchPaths({"NormalCrafter"});

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

void NormalCrafterPluginFactory::describe(OFX::ImageEffectDescriptor &desc)
{
    desc.setLabel("ComfyUI NormalCrafter");
    desc.setLabels("ComfyUI NormalCrafter", "NormalCrafter", "ComfyUI NormalCrafter");
    desc.setPluginGrouping("ComfyUI");

    desc.setPluginDescription(
        "Temporally-consistent video surface-normal estimation using NormalCrafter via ComfyUI.\n\n"
        "NormalCrafter uses a diffusion-based approach to generate normal maps that are "
        "consistent across video frames — unlike per-frame methods, it processes the entire "
        "sequence in temporal windows to preserve normal continuity over time.\n\n"
        "Typical settings:\n"
        "- Resize your source footage to the desired resolution upstream in Flame before processing\n"
        "- Max Res Dimension 1024, Time Step Size 10 — fast, good quality\n"
        "- Window Size should match or exceed your sequence length for best consistency\n"
        "- Lower Decode Chunk Size for tighter VRAM budgets\n\n"
        "Note: NormalCrafter processes the full sequence per job, not per frame. "
        "Frame Limit controls how many frames are loaded from the input sequence.\n\n"
        "Requires:\n"
        "- ComfyUI server with ComfyUI-NormalCrafterWrapper extension\n"
        "- Shared network storage for image exchange\n\n"
        "Based on: https://github.com/AIWarper/ComfyUI-NormalCrafterWrapper"
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

void NormalCrafterPluginFactory::describeInContext(OFX::ImageEffectDescriptor &desc,
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

    json configDefaults = loadNormalCrafterConfigDefaults();
    NormalCrafterPlugin::describeInContext(desc, context, &configDefaults);
}

OFX::ImageEffect* NormalCrafterPluginFactory::createInstance(OfxImageEffectHandle handle,
                                                              OFX::ContextEnum /*context*/)
{
    return new NormalCrafterPlugin(handle);
}

} // namespace ComfyUI

// ============================================================================
// Plugin Registration
// ============================================================================

namespace OFX {
namespace Plugin {

void getPluginIDs(OFX::PluginFactoryArray &ids)
{
    static ComfyUI::NormalCrafterPluginFactory factory;
    ids.push_back(&factory);
}

} // namespace Plugin
} // namespace OFX
