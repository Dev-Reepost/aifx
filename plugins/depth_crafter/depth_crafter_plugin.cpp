// Copyright OpenFX and contributors to the OpenFX project.
// SPDX-License-Identifier: BSD-3-Clause

#include "depth_crafter_plugin.h"
#include <sstream>
#include <fstream>

namespace ComfyUI {

// ============================================================================
// DepthCrafterPlugin Implementation
// ============================================================================

DepthCrafterPlugin::DepthCrafterPlugin(OfxImageEffectHandle handle)
    : BasePlugin(handle)
    , _forceSize(nullptr)
    , _numInferenceSteps(nullptr)
    , _guidanceScale(nullptr)
    , _windowSize(nullptr)
    , _overlap(nullptr)
    , _imageLoadCap(nullptr)
    , _enableModelCpuOffload(nullptr)
    , _enableSequentialCpuOffload(nullptr)
{
    _forceSize                  = fetchBooleanParam("forceSize");
    _numInferenceSteps          = fetchIntParam("numInferenceSteps");
    _guidanceScale              = fetchDoubleParam("guidanceScale");
    _windowSize                 = fetchIntParam("windowSize");
    _overlap                    = fetchIntParam("overlap");
    _imageLoadCap               = fetchIntParam("imageLoadCap");
    _enableModelCpuOffload      = fetchBooleanParam("enableModelCpuOffload");
    _enableSequentialCpuOffload = fetchBooleanParam("enableSequentialCpuOffload");
}

DepthCrafterPlugin::~DepthCrafterPlugin() {}

int DepthCrafterPlugin::getImageLoadCap() const {
    int cap = 0;
    if (_imageLoadCap) _imageLoadCap->getValue(cap);
    return cap;
}

// ============================================================================
// Workflow building
// ============================================================================

json DepthCrafterPlugin::buildWorkflow(int frame,
                                        const std::map<std::string, std::string>& inputPaths)
{
    if (_logger) _logger->info("Building DepthCrafter workflow for frame {}", frame);

    std::string inputPath;
    auto it = inputPaths.find("InputA");
    if (it != inputPaths.end()) {
        inputPath = it->second;
    } else if (!inputPaths.empty()) {
        inputPath = inputPaths.begin()->second;
    } else {
        throw std::runtime_error("No input path provided for DepthCrafter workflow");
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

json DepthCrafterPlugin::buildHardcodedWorkflow(int frame, const std::string& inputPath)
{
    if (_logger) _logger->info("Building hardcoded DepthCrafter workflow");

    bool forceSize, enableModelCpuOffload, enableSequentialCpuOffload;
    int numInferenceSteps, windowSize, overlap, imageLoadCap;
    double guidanceScale;

    _forceSize->getValue(forceSize);
    _numInferenceSteps->getValue(numInferenceSteps);
    _guidanceScale->getValue(guidanceScale);
    _windowSize->getValue(windowSize);
    _overlap->getValue(overlap);
    _imageLoadCap->getValue(imageLoadCap);
    _enableModelCpuOffload->getValue(enableModelCpuOffload);
    _enableSequentialCpuOffload->getValue(enableSequentialCpuOffload);

    // Clamp window_size so DepthCrafter never allocates more than the actual frame count.
    int effectiveWindowSize = (imageLoadCap > 0) ? std::min(windowSize, imageLoadCap) : windowSize;
    int effectiveOverlap    = std::min(overlap, effectiveWindowSize - 1);

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
        _logger->info("DepthCrafter parameters:");
        _logger->info("  Force size: {}", forceSize ? "true" : "false");
        _logger->info("  Inference steps: {}", numInferenceSteps);
        _logger->info("  Guidance scale: {}", guidanceScale);
        _logger->info("  Window size: {} (clamped from {})", effectiveWindowSize, windowSize);
        _logger->info("  Overlap: {} (clamped from {})", effectiveOverlap, overlap);
        _logger->info("  Image load cap: {}", imageLoadCap);
        _logger->info("  CPU offload: {}", enableModelCpuOffload);
        _logger->info("  Sequential CPU offload: {}", enableSequentialCpuOffload);
        _logger->info("  Input (ComfyUI): {}", comfyInputPath);
        _logger->info("  Output prefix (ComfyUI): {}", comfyOutputPrefix);
    }

    json workflow = {

        // Node 22: Load EXR sequence
        {"22", {
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

        // Node 26: Download and load DepthCrafter model
        {"26", {
            {"inputs", {
                {"enable_model_cpu_offload", enableModelCpuOffload},
                {"enable_sequential_cpu_offload", enableSequentialCpuOffload}
            }},
            {"class_type", "DownloadAndLoadDepthCrafterModel"},
            {"_meta", {{"title", "DownloadAndLoadDepthCrafterModel"}}}
        }},

        // Node 24: DepthCrafter inference (window_size clamped to imageLoadCap)
        {"24", {
            {"inputs", {
                {"force_size", forceSize},
                {"num_inference_steps", numInferenceSteps},
                {"guidance_scale", guidanceScale},
                {"window_size", effectiveWindowSize},
                {"overlap", effectiveOverlap},
                {"depthcrafter_model", json::array({"26", 0})},
                {"images", json::array({"22", 0})}
            }},
            {"class_type", "DepthCrafter"},
            {"_meta", {{"title", "DepthCrafter"}}}
        }},

        // Node 23: Save depth map as EXR
        {"23", {
            {"inputs", {
                {"filename_prefix", comfyOutputPrefix},
                {"tonemap", "linear"},
                {"version", -1},
                {"start_frame", frame},
                {"frame_pad", 4},
                {"save_workflow", "none"},
                {"images", json::array({"24", 0})}
            }},
            {"class_type", "SaveEXR"},
            {"_meta", {{"title", "Save EXR"}}}
        }}
    };

    if (_logger) {
        _logger->info("=== DEPTH CRAFTER WORKFLOW DUMP ===");
        _logger->info("{}", workflow.dump(2));
        _logger->info("=== END WORKFLOW DUMP ===");
    }

    return workflow;
}

json DepthCrafterPlugin::customizeWorkflowWithParams(const json& baseWorkflow, int frame)
{
    if (_logger) _logger->debug("Customizing DepthCrafter workflow with parameters");

    bool forceSize, enableModelCpuOffload, enableSequentialCpuOffload;
    int numInferenceSteps, windowSize, overlap, imageLoadCap;
    double guidanceScale;

    _forceSize->getValue(forceSize);
    _numInferenceSteps->getValue(numInferenceSteps);
    _guidanceScale->getValue(guidanceScale);
    _windowSize->getValue(windowSize);
    _overlap->getValue(overlap);
    _imageLoadCap->getValue(imageLoadCap);
    _enableModelCpuOffload->getValue(enableModelCpuOffload);
    _enableSequentialCpuOffload->getValue(enableSequentialCpuOffload);

    // Clamp window_size to imageLoadCap so DepthCrafter never allocates a temporal
    // attention window larger than the actual frame count — the primary OOM cause.
    int effectiveWindowSize = (imageLoadCap > 0) ? std::min(windowSize, imageLoadCap) : windowSize;
    // Overlap must be strictly less than window_size.
    int effectiveOverlap = std::min(overlap, effectiveWindowSize - 1);
    if (effectiveWindowSize != windowSize || effectiveOverlap != overlap) {
        if (_logger) {
            _logger->info("Clamped window_size {} → {} (imageLoadCap={}), overlap {} → {}",
                          windowSize, effectiveWindowSize, imageLoadCap, overlap, effectiveOverlap);
        }
    }

    std::string workflowStr = baseWorkflow.dump();

    auto replaceNumeric = [&](const std::string& quotedPlaceholder, const std::string& numStr) {
        size_t pos = 0;
        while ((pos = workflowStr.find(quotedPlaceholder, pos)) != std::string::npos) {
            workflowStr.replace(pos, quotedPlaceholder.length(), numStr);
            pos += numStr.length();
        }
    };

    replaceNumeric("\"${FORCE_SIZE}\"",          forceSize ? "true" : "false");
    replaceNumeric("\"${NUM_INFERENCE_STEPS}\"", std::to_string(numInferenceSteps));
    replaceNumeric("\"${GUIDANCE_SCALE}\"",      std::to_string(guidanceScale));
    replaceNumeric("\"${WINDOW_SIZE}\"",         std::to_string(effectiveWindowSize));
    replaceNumeric("\"${OVERLAP}\"",             std::to_string(effectiveOverlap));
    replaceNumeric("\"${IMAGE_LOAD_CAP}\"",      std::to_string(imageLoadCap));
    replaceNumeric("\"${ENABLE_MODEL_CPU_OFFLOAD}\"",      enableModelCpuOffload      ? "true" : "false");
    replaceNumeric("\"${ENABLE_SEQUENTIAL_CPU_OFFLOAD}\"", enableSequentialCpuOffload ? "true" : "false");

    try {
        json finalWorkflow = json::parse(workflowStr);
        if (_logger) {
            _logger->info("=== DEPTH CRAFTER FINAL WORKFLOW ===");
            _logger->info("{}", finalWorkflow.dump(2));
            _logger->info("=== END FINAL WORKFLOW ===");
        }
        return finalWorkflow;
    } catch (const json::exception& e) {
        if (_logger) _logger->error("Failed to parse customized DepthCrafter workflow: {}", e.what());
        throw std::runtime_error("Failed to parse DepthCrafter customized workflow: " + std::string(e.what()));
    }
}

std::vector<std::string> DepthCrafterPlugin::getRequiredModels()
{
    return {"depthcrafter"};
}

// ============================================================================
// Parameter Definitions
// ============================================================================

void DepthCrafterPlugin::describeInContext(OFX::ImageEffectDescriptor &desc,
                                            OFX::ContextEnum context,
                                            const json* configDefaults)
{
    OFX::PageParamDescriptor *page = desc.definePageParam("Controls");

    // ==== DEPTH PROCESSING GROUP ====
    OFX::GroupParamDescriptor *depthGroup = desc.defineGroupParam("crafterDepthGroup");
    depthGroup->setLabel("Depth Processing");
    depthGroup->setOpen(true);
    page->addChild(*depthGroup);

    // Force size (boolean flag — tells the DepthCrafter node to use its internal fixed size)
    OFX::BooleanParamDescriptor *forceSize = desc.defineBooleanParam("forceSize");
    forceSize->setLabel("Force Size");
    forceSize->setHint("When enabled, the DepthCrafter node uses its internal fixed resolution. "
                       "Resize your source footage upstream (in Flame) to the desired resolution before processing.");
    forceSize->setDefault(true);
    forceSize->setAnimates(false);
    forceSize->setParent(*depthGroup);
    page->addChild(*forceSize);

    // Inference steps
    OFX::IntParamDescriptor *numInferenceSteps = desc.defineIntParam("numInferenceSteps");
    numInferenceSteps->setLabel("Inference Steps");
    numInferenceSteps->setHint("Number of diffusion denoising steps. More steps = higher quality but slower. "
                                "10 is a good balance; 5 for quick previews.");
    numInferenceSteps->setDefault(10);
    numInferenceSteps->setRange(1, 50);
    numInferenceSteps->setDisplayRange(1, 25);
    numInferenceSteps->setAnimates(false);
    numInferenceSteps->setParent(*depthGroup);
    page->addChild(*numInferenceSteps);

    // Guidance scale
    OFX::DoubleParamDescriptor *guidanceScale = desc.defineDoubleParam("guidanceScale");
    guidanceScale->setLabel("Guidance Scale");
    guidanceScale->setHint("Classifier-free guidance scale. Higher values follow the depth prior more strongly. "
                            "1.2 is the recommended default.");
    guidanceScale->setDefault(1.2);
    guidanceScale->setRange(1.0, 10.0);
    guidanceScale->setDisplayRange(1.0, 5.0);
    guidanceScale->setAnimates(false);
    guidanceScale->setParent(*depthGroup);
    page->addChild(*guidanceScale);

    // Window size
    OFX::IntParamDescriptor *windowSize = desc.defineIntParam("windowSize");
    windowSize->setLabel("Window Size");
    windowSize->setHint("Number of frames processed per temporal window. Must not exceed total frame count. "
                         "Larger windows improve temporal consistency but require more VRAM.");
    windowSize->setDefault(25);
    windowSize->setRange(2, 512);
    windowSize->setDisplayRange(10, 200);
    windowSize->setAnimates(false);
    windowSize->setParent(*depthGroup);
    page->addChild(*windowSize);

    // Overlap
    OFX::IntParamDescriptor *overlap = desc.defineIntParam("overlap");
    overlap->setLabel("Window Overlap");
    overlap->setHint("Number of overlapping frames between consecutive temporal windows. "
                      "Higher overlap improves temporal consistency at longer sequences.");
    overlap->setDefault(25);
    overlap->setRange(0, 128);
    overlap->setDisplayRange(0, 64);
    overlap->setAnimates(false);
    overlap->setParent(*depthGroup);
    page->addChild(*overlap);

    // Image load cap
    OFX::IntParamDescriptor *imageLoadCap = desc.defineIntParam("imageLoadCap");
    imageLoadCap->setLabel("Frame Limit");
    imageLoadCap->setHint("Maximum number of frames to load from the sequence. "
                           "Set to 0 for no limit. Keep <= Window Size for best results.");
    imageLoadCap->setDefault(25);
    imageLoadCap->setRange(0, 1024);
    imageLoadCap->setDisplayRange(0, 200);
    imageLoadCap->setAnimates(false);
    imageLoadCap->setParent(*depthGroup);
    page->addChild(*imageLoadCap);

    // ==== MODEL GROUP ====
    OFX::GroupParamDescriptor *modelGroup = desc.defineGroupParam("crafterModelGroup");
    modelGroup->setLabel("Model");
    modelGroup->setOpen(false);
    page->addChild(*modelGroup);

    // Enable model CPU offload
    OFX::BooleanParamDescriptor *enableModelCpuOffload = desc.defineBooleanParam("enableModelCpuOffload");
    enableModelCpuOffload->setLabel("CPU Offload");
    enableModelCpuOffload->setHint("Offload model weights to CPU between inference passes to reduce VRAM usage. "
                                    "Recommended for GPUs with < 16GB VRAM.");
    enableModelCpuOffload->setDefault(true);
    enableModelCpuOffload->setAnimates(false);
    enableModelCpuOffload->setParent(*modelGroup);
    page->addChild(*enableModelCpuOffload);

    // Enable sequential CPU offload
    OFX::BooleanParamDescriptor *enableSequentialCpuOffload = desc.defineBooleanParam("enableSequentialCpuOffload");
    enableSequentialCpuOffload->setLabel("Sequential CPU Offload");
    enableSequentialCpuOffload->setHint("More aggressive layer-by-layer CPU offloading. "
                                         "Minimises VRAM at the cost of speed. "
                                         "Only enable if CPU Offload alone is insufficient.");
    enableSequentialCpuOffload->setDefault(false);
    enableSequentialCpuOffload->setAnimates(false);
    enableSequentialCpuOffload->setParent(*modelGroup);
    page->addChild(*enableSequentialCpuOffload);

    // Add common parameters — pass isSequencePlugin=true to omit the enable checkbox
    BasePlugin::describeCommonParameters(desc, context, page, page, page, configDefaults,
                                         /*skipGroupHeaders=*/false, /*isSequencePlugin=*/true);

    // Override workflowName default for this plugin
    OFX::StringParamDescriptor *workflow = desc.defineStringParam("workflowName");
    workflow->setDefault("depth_crafter");
}

// ============================================================================
// DepthCrafterPluginFactory Implementation
// ============================================================================

DepthCrafterPluginFactory::DepthCrafterPluginFactory()
    : OFX::PluginFactoryHelper<DepthCrafterPluginFactory>(
        "com.comfyui.DepthCrafter",
        1,
        0
    )
{
}

json DepthCrafterPluginFactory::loadDepthCrafterConfigDefaults()
{
    const char* home = getenv("HOME");
    if (!home) return json{};

    std::vector<std::string> searchPaths = {
        std::string(home) + "/Library/OFX/Plugins/DepthCrafter.ofx.bundle/Contents/Resources/config/defaults.json",
        std::string(home) + "/OFX/Plugins/DepthCrafter.ofx.bundle/Contents/Resources/config/defaults.json",
        "/Library/OFX/Plugins/DepthCrafter.ofx.bundle/Contents/Resources/config/defaults.json",
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

void DepthCrafterPluginFactory::describe(OFX::ImageEffectDescriptor &desc)
{
    desc.setLabel("ComfyUI DepthCrafter");
    desc.setLabels("ComfyUI DepthCrafter", "DepthCrafter", "ComfyUI DepthCrafter");
    desc.setPluginGrouping("ComfyUI");

    desc.setPluginDescription(
        "Temporally-consistent video depth estimation using DepthCrafter via ComfyUI.\n\n"
        "DepthCrafter uses a diffusion-based approach to generate depth maps that are "
        "consistent across video frames — unlike per-frame methods, it processes the entire "
        "sequence in temporal windows to preserve depth continuity over time.\n\n"
        "Typical settings:\n"
        "- Resize your source footage to the desired resolution upstream in Flame before processing\n"
        "- Steps 10, Guidance 1.2 — fast, good quality\n"
        "- Window Size should match or exceed your sequence length for best consistency\n"
        "- Enable CPU Offload for GPUs with < 16 GB VRAM\n\n"
        "Note: DepthCrafter processes the full sequence per job, not per frame. "
        "Frame Limit controls how many frames are loaded from the input sequence.\n\n"
        "Requires:\n"
        "- ComfyUI server with ComfyUI-DepthCrafterWrapper extension\n"
        "- Shared network storage for image exchange\n\n"
        "Based on: https://github.com/kijai/ComfyUI-DepthCrafterWrapper"
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

void DepthCrafterPluginFactory::describeInContext(OFX::ImageEffectDescriptor &desc,
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

    json configDefaults = loadDepthCrafterConfigDefaults();
    DepthCrafterPlugin::describeInContext(desc, context, &configDefaults);
}

OFX::ImageEffect* DepthCrafterPluginFactory::createInstance(OfxImageEffectHandle handle,
                                                              OFX::ContextEnum /*context*/)
{
    return new DepthCrafterPlugin(handle);
}

} // namespace ComfyUI

// ============================================================================
// Plugin Registration
// ============================================================================

namespace OFX {
namespace Plugin {

void getPluginIDs(OFX::PluginFactoryArray &ids)
{
    static ComfyUI::DepthCrafterPluginFactory factory;
    ids.push_back(&factory);
}

} // namespace Plugin
} // namespace OFX
