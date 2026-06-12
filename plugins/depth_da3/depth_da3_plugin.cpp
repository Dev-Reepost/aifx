// Copyright OpenFX and contributors to the OpenFX project.
// SPDX-License-Identifier: BSD-3-Clause

#include "depth_da3_plugin.h"
#include <sstream>
#include <iomanip>
#include <fstream>

namespace ComfyUI {

// ============================================================================
// Model / Precision / Attention name tables
// ============================================================================

static const char* kDA3Models[] = {
    "da3_small.safetensors",
    "da3_base.safetensors",
    "da3_large.safetensors",
    "da3_giant.safetensors",
    "da3mono_large.safetensors",
    "da3metric_large.safetensors",
    "da3nested_giant_large.safetensors",
    nullptr
};

static const char* kPrecisions[] = { "auto", "fp16", "fp32", "bf16", nullptr };
static const char* kAttentions[]  = { "flash_attn", "xformers", "math", nullptr };
static const char* kNormModes[]   = { "V2-Style", "Raw", nullptr };
static const char* kResizeMethods[]= { "resize", "pad", "crop", nullptr };

// ============================================================================
// DepthDA3Plugin Implementation
// ============================================================================

DepthDA3Plugin::DepthDA3Plugin(OfxImageEffectHandle handle)
    : BasePlugin(handle)
    , _normalizationMode(nullptr)
    , _resizeMethod(nullptr)
    , _invertDepth(nullptr)
    , _keepModelSize(nullptr)
    , _da3Model(nullptr)
    , _precision(nullptr)
    , _attention(nullptr)
{
    _normalizationMode = fetchChoiceParam("normalizationMode");
    _resizeMethod      = fetchChoiceParam("resizeMethod");
    _invertDepth       = fetchBooleanParam("invertDepth");
    _keepModelSize     = fetchBooleanParam("keepModelSize");
    _da3Model          = fetchChoiceParam("da3Model");
    _precision         = fetchChoiceParam("precision");
    _attention         = fetchChoiceParam("attention");
}

DepthDA3Plugin::~DepthDA3Plugin()
{
}

// ============================================================================
// Helper: index → string
// ============================================================================

std::string DepthDA3Plugin::getModelName() const
{
    int idx; _da3Model->getValue(idx);
    if (idx >= 0 && kDA3Models[idx]) return kDA3Models[idx];
    return "da3_large.safetensors";
}

std::string DepthDA3Plugin::getPrecisionName() const
{
    int idx; _precision->getValue(idx);
    if (idx >= 0 && kPrecisions[idx]) return kPrecisions[idx];
    return "auto";
}

std::string DepthDA3Plugin::getAttentionName() const
{
    int idx; _attention->getValue(idx);
    if (idx >= 0 && kAttentions[idx]) return kAttentions[idx];
    return "flash_attn";
}

std::string DepthDA3Plugin::getNormalizationModeName() const
{
    int idx; _normalizationMode->getValue(idx);
    if (idx >= 0 && kNormModes[idx]) return kNormModes[idx];
    return "V2-Style";
}

std::string DepthDA3Plugin::getResizeMethodName() const
{
    int idx; _resizeMethod->getValue(idx);
    if (idx >= 0 && kResizeMethods[idx]) return kResizeMethods[idx];
    return "resize";
}

// ============================================================================
// Workflow building
// ============================================================================

json DepthDA3Plugin::buildWorkflow(int frame,
                                    const std::map<std::string, std::string>& inputPaths)
{
    if (_logger) _logger->info("Building Depth Anything V3 workflow for frame {}", frame);

    std::string inputPath;
    auto it = inputPaths.find("InputA");
    if (it != inputPaths.end()) {
        inputPath = it->second;
    } else if (!inputPaths.empty()) {
        inputPath = inputPaths.begin()->second;
    } else {
        throw std::runtime_error("No input path provided for DepthDA3 workflow");
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

json DepthDA3Plugin::buildHardcodedWorkflow(int frame, const std::string& inputPath)
{
    if (_logger) _logger->info("Building hardcoded Depth Anything V3 workflow");

    const std::string modelName       = getModelName();
    const std::string precisionName   = getPrecisionName();
    const std::string attentionName   = getAttentionName();
    const std::string normMode        = getNormalizationModeName();
    const std::string resizeMethod    = getResizeMethodName();

    bool invertDepth, keepModelSize;
    _invertDepth->getValue(invertDepth);
    _keepModelSize->getValue(keepModelSize);

    // Build output path
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
        _logger->info("DepthDA3 parameters:");
        _logger->info("  Model: {}", modelName);
        _logger->info("  Precision: {}", precisionName);
        _logger->info("  Attention: {}", attentionName);
        _logger->info("  Normalization mode: {}", normMode);
        _logger->info("  Resize method: {}", resizeMethod);
        _logger->info("  Invert depth: {}", invertDepth);
        _logger->info("  Keep model size: {}", keepModelSize);
        _logger->info("  Input (ComfyUI): {}", comfyInputPath);
        _logger->info("  Output prefix (ComfyUI): {}", comfyOutputPrefix);
    }

    // Build workflow JSON matching Depth_Any3_video_multiview_ofx_api.json structure
    json workflow = {

        // Node 77: Load a single input frame. Depth Anything V3 is a
        // per-frame image model (paper: "Recovering the Visual Space from
        // Any Views" describes multi-view stereo, not temporal video), so
        // we only need the one frame the host is currently rendering. For
        // temporally consistent video depth, use the DepthCrafter plugin
        // instead.
        {"77", {
            {"inputs", {
                {"filepath", comfyInputPath},
                {"tonemap", "linear"},
                {"image_load_cap", 1},
                {"skip_first_images", 0},
                {"select_every_nth", 1}
            }},
            {"class_type", "LoadEXR"},
            {"_meta", {{"title", "Load EXR"}}}
        }},

        // Node 84: Download and load DA3 model
        {"84", {
            {"inputs", {
                {"model", modelName},
                {"precision", precisionName},
                {"attention", attentionName}
            }},
            {"class_type", "DownloadAndLoadDepthAnything3Model"},
            {"_meta", {{"title", "Load Depth Anything V3 Model"}}}
        }},

        // Node 79: Depth Anything V3 estimation
        {"79", {
            {"inputs", {
                {"normalization_mode", normMode},
                {"resize_method", resizeMethod},
                {"invert_depth", invertDepth},
                {"keep_model_size", keepModelSize},
                {"da3_model", json::array({"84", 0})},
                {"images", json::array({"77", 0})}
            }},
            {"class_type", "DepthAnything_V3"},
            {"_meta", {{"title", "Depth Anything V3"}}}
        }},

        // Node 83: Save depth map as EXR
        {"83", {
            {"inputs", {
                {"filename_prefix", comfyOutputPrefix},
                {"tonemap", "linear"},
                {"version", -1},
                {"start_frame", frame},
                {"frame_pad", 4},
                {"save_workflow", "none"},
                {"create_path_if_missing", true},
                {"images", json::array({"79", 0})}
            }},
            {"class_type", "SaveEXR"},
            {"_meta", {{"title", "Save EXR"}}}
        }}
    };

    if (_logger) {
        _logger->info("=== DEPTH DA3 HARDCODED WORKFLOW DUMP ===");
        _logger->info("{}", workflow.dump(2));
        _logger->info("=== END WORKFLOW DUMP ===");
    }

    return workflow;
}

json DepthDA3Plugin::customizeWorkflowWithParams(const json& baseWorkflow, int frame)
{
    if (_logger) _logger->debug("Customizing DepthDA3 workflow with parameters");

    const std::string modelName    = getModelName();
    const std::string precisionName= getPrecisionName();
    const std::string attentionName= getAttentionName();
    const std::string normMode     = getNormalizationModeName();
    const std::string resizeMethod = getResizeMethodName();

    bool invertDepth, keepModelSize;
    _invertDepth->getValue(invertDepth);
    _keepModelSize->getValue(keepModelSize);

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

    replaceStr("${DA3_MODEL}",          modelName);
    replaceStr("${PRECISION}",          precisionName);
    replaceStr("${ATTENTION}",          attentionName);
    replaceStr("${NORMALIZATION_MODE}", normMode);
    replaceStr("${RESIZE_METHOD}",      resizeMethod);

    replaceNumeric("\"${INVERT_DEPTH}\"",    invertDepth    ? "true" : "false");
    replaceNumeric("\"${KEEP_MODEL_SIZE}\"", keepModelSize  ? "true" : "false");
    // Note: ${INPUT_PATH}, ${OUTPUT_PREFIX}, ${FRAME} handled by base class customizeWorkflow()

    try {
        json finalWorkflow = json::parse(workflowStr);

        if (_logger) {
            _logger->info("=== DEPTH DA3 FINAL WORKFLOW TO COMFYUI ===");
            _logger->info("{}", finalWorkflow.dump(2));
            _logger->info("=== END FINAL WORKFLOW ===");
        }

        return finalWorkflow;
    } catch (const json::exception& e) {
        if (_logger) _logger->error("Failed to parse customized DepthDA3 workflow: {}", e.what());
        throw std::runtime_error("Failed to parse DepthDA3 customized workflow: " + std::string(e.what()));
    }
}

std::vector<std::string> DepthDA3Plugin::getRequiredModels()
{
    return {getModelName()};
}

// ============================================================================
// Parameter Definitions
// ============================================================================

void DepthDA3Plugin::describeInContext(OFX::ImageEffectDescriptor &desc,
                                        OFX::ContextEnum context,
                                        const json* configDefaults)
{
    OFX::PageParamDescriptor *page = desc.definePageParam("Controls");

    // ==== DEPTH PROCESSING GROUP ====
    OFX::GroupParamDescriptor *depthGroup = desc.defineGroupParam("da3DepthGroup");
    depthGroup->setLabel("Depth Processing");
    depthGroup->setOpen(true);
    page->addChild(*depthGroup);

    // Normalization mode
    OFX::ChoiceParamDescriptor *normMode = desc.defineChoiceParam("normalizationMode");
    normMode->setLabel("Normalization");
    normMode->setHint("V2-Style: normalized depth with sky mask support (recommended for ControlNet). "
                      "Raw: unnormalized metric depth (for 3D reconstruction).");
    normMode->appendOption("V2-Style", "Normalized depth (0-1 range, recommended)");
    normMode->appendOption("Raw",      "Raw metric depth values");
    normMode->setDefault(0);
    normMode->setAnimates(false);
    normMode->setParent(*depthGroup);
    page->addChild(*normMode);

    // Resize method
    OFX::ChoiceParamDescriptor *resizeMethod = desc.defineChoiceParam("resizeMethod");
    resizeMethod->setLabel("Resize Method");
    resizeMethod->setHint("How to resize input images to model resolution.");
    resizeMethod->appendOption("Resize", "Scale image to fit model input size");
    resizeMethod->appendOption("Pad",    "Pad image to preserve aspect ratio");
    resizeMethod->appendOption("Crop",   "Center-crop image to model input size");
    resizeMethod->setDefault(0);
    resizeMethod->setAnimates(false);
    resizeMethod->setParent(*depthGroup);
    page->addChild(*resizeMethod);

    // Invert depth
    OFX::BooleanParamDescriptor *invertDepth = desc.defineBooleanParam("invertDepth");
    invertDepth->setLabel("Invert Depth");
    invertDepth->setHint("Invert depth values (near becomes far, far becomes near).");
    invertDepth->setDefault(false);
    invertDepth->setParent(*depthGroup);
    page->addChild(*invertDepth);

    // Keep model size
    OFX::BooleanParamDescriptor *keepModelSize = desc.defineBooleanParam("keepModelSize");
    keepModelSize->setLabel("Keep Model Size");
    keepModelSize->setHint("Output depth at the model's internal resolution instead of upscaling back to input resolution.");
    keepModelSize->setDefault(false);
    keepModelSize->setParent(*depthGroup);
    page->addChild(*keepModelSize);

    // ==== MODEL GROUP ====
    OFX::GroupParamDescriptor *modelGroup = desc.defineGroupParam("da3ModelGroup");
    modelGroup->setLabel("Model");
    modelGroup->setOpen(false);
    page->addChild(*modelGroup);

    // DA3 model variant
    OFX::ChoiceParamDescriptor *da3Model = desc.defineChoiceParam("da3Model");
    da3Model->setLabel("Model Variant");
    da3Model->setHint("DA3-Small (80M): fast. DA3-Base (220M): balanced. DA3-Large (350M): high quality. "
                      "DA3-Giant (1.15B): best quality. Mono/Metric: sky mask support. "
                      "Nested-Giant: combined model with metric scaling.");
    da3Model->appendOption("DA3-Small  (80M)",              "da3_small.safetensors");
    da3Model->appendOption("DA3-Base   (220M)",             "da3_base.safetensors");
    da3Model->appendOption("DA3-Large  (350M)",             "da3_large.safetensors");
    da3Model->appendOption("DA3-Giant  (1.15B)",            "da3_giant.safetensors");
    da3Model->appendOption("DA3-Mono-Large  (350M)",        "da3mono_large.safetensors");
    da3Model->appendOption("DA3-Metric-Large  (350M)",      "da3metric_large.safetensors");
    da3Model->appendOption("DA3-Nested-Giant-Large (1.4B)", "da3nested_giant_large.safetensors");
    da3Model->setDefault(2); // DA3-Large
    da3Model->setAnimates(false);
    da3Model->setParent(*modelGroup);
    page->addChild(*da3Model);

    // Precision
    OFX::ChoiceParamDescriptor *precision = desc.defineChoiceParam("precision");
    precision->setLabel("Precision");
    precision->setHint("Computation precision. Auto selects the best option for your GPU.");
    precision->appendOption("Auto",  "Automatically select precision");
    precision->appendOption("FP16",  "Half precision (faster, less VRAM)");
    precision->appendOption("FP32",  "Full precision (slower, more accurate)");
    precision->appendOption("BF16",  "Brain float 16 (Ampere+ GPUs)");
    precision->setDefault(0); // auto
    precision->setAnimates(false);
    precision->setParent(*modelGroup);
    page->addChild(*precision);

    // Attention implementation
    OFX::ChoiceParamDescriptor *attention = desc.defineChoiceParam("attention");
    attention->setLabel("Attention");
    attention->setHint("Attention mechanism implementation. flash_attn is fastest when available.");
    attention->appendOption("Flash Attention", "flash_attn — fastest (requires flash-attn library)");
    attention->appendOption("xFormers",        "xformers — fast alternative");
    attention->appendOption("Math",            "math — standard PyTorch (always available)");
    attention->setDefault(0); // flash_attn
    attention->setAnimates(false);
    attention->setParent(*modelGroup);
    page->addChild(*attention);

    // Add common parameters (server, project, cache, etc.) with optional config defaults
    BasePlugin::describeCommonParameters(desc, context, page, page, page, configDefaults);

    // Override workflowName default for this plugin
    OFX::StringParamDescriptor *workflow = desc.defineStringParam("workflowName");
    workflow->setDefault("depth_da3");
}

// ============================================================================
// DepthDA3PluginFactory Implementation
// ============================================================================

DepthDA3PluginFactory::DepthDA3PluginFactory()
    : OFX::PluginFactoryHelper<DepthDA3PluginFactory>(
        "com.comfyui.DepthAnything3",  // Plugin identifier
        1,                               // Version major
        0                                // Version minor
    )
{
}

json DepthDA3PluginFactory::loadDA3ConfigDefaults()
{
    // Search the DepthAnything3 bundle across all platform OFX plugin locations.
    std::vector<std::string> searchPaths =
        getOfxConfigSearchPaths({"DepthAnything3"});

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

void DepthDA3PluginFactory::describe(OFX::ImageEffectDescriptor &desc)
{
    desc.setLabel("ComfyUI Depth Anything 3");
    desc.setLabels("ComfyUI DA3", "Depth Anything 3", "ComfyUI Depth Anything 3");
    desc.setPluginGrouping("ComfyUI");

    desc.setPluginDescription(
        "Monocular depth estimation using Depth Anything V3 (DA3) via ComfyUI.\n\n"
        "Produces high-quality depth maps from image sequences. Multiple model variants "
        "are available, from the fast DA3-Small (80M) to the highly accurate DA3-Giant (1.15B).\n\n"
        "Use cases:\n"
        "- ControlNet depth conditioning\n"
        "- 3D scene reconstruction (point clouds)\n"
        "- Depth-based compositing and relighting\n"
        "- Multi-view consistency with DA3 main series\n\n"
        "For ControlNet: use V2-Style normalization (default).\n"
        "For 3D reconstruction: use Raw normalization.\n"
        "Sky masks available with Mono/Metric/Nested variants.\n\n"
        "Requires:\n"
        "- ComfyUI server with ComfyUI-DepthAnything3 extension installed\n"
        "- Shared network storage for image exchange\n\n"
        "Based on: https://github.com/PozzettiAndrea/ComfyUI-DepthAnything3"
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
    desc.setTemporalClipAccess(false);
    desc.setRenderTwiceAlways(false);
    desc.setSupportsMultipleClipPARs(false);
    desc.setSupportsMultipleClipDepths(true);
    desc.setRenderThreadSafety(OFX::eRenderInstanceSafe);
}

void DepthDA3PluginFactory::describeInContext(OFX::ImageEffectDescriptor &desc,
                                               OFX::ContextEnum context)
{
    // Source clip
    OFX::ClipDescriptor *srcClip = desc.defineClip(kOfxImageEffectSimpleSourceClipName);
    srcClip->addSupportedComponent(OFX::ePixelComponentRGBA);
    srcClip->addSupportedComponent(OFX::ePixelComponentRGB);
    srcClip->setTemporalClipAccess(false);
    srcClip->setSupportsTiles(false);
    srcClip->setIsMask(false);

    // Output clip
    OFX::ClipDescriptor *dstClip = desc.defineClip(kOfxImageEffectOutputClipName);
    dstClip->addSupportedComponent(OFX::ePixelComponentRGBA);
    dstClip->addSupportedComponent(OFX::ePixelComponentRGB);
    dstClip->setSupportsTiles(false);

    // Load config defaults
    json configDefaults = loadDA3ConfigDefaults();

    DepthDA3Plugin::describeInContext(desc, context, &configDefaults);
}

OFX::ImageEffect* DepthDA3PluginFactory::createInstance(OfxImageEffectHandle handle,
                                                          OFX::ContextEnum /*context*/)
{
    return new DepthDA3Plugin(handle);
}

} // namespace ComfyUI

// ============================================================================
// Plugin Registration
// ============================================================================

namespace OFX {
namespace Plugin {

void getPluginIDs(OFX::PluginFactoryArray &ids)
{
    static ComfyUI::DepthDA3PluginFactory factory;
    ids.push_back(&factory);
}

} // namespace Plugin
} // namespace OFX
