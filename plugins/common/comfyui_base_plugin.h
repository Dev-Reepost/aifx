// Copyright OpenFX and contributors to the OpenFX project.
// SPDX-License-Identifier: BSD-3-Clause

#ifndef COMFYUI_BASE_PLUGIN_H
#define COMFYUI_BASE_PLUGIN_H

#include "ofxsImageEffect.h"
#include "comfyui_client.h"
#include "comfyui_image_io.h"
#include "async_job_manager.h"
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <memory>
#include <mutex>
#include <map>
#include <unordered_set>
#include <unordered_map>

using json = nlohmann::json;

namespace ComfyUI {

/**
 * @brief Base class for all ComfyUI OFX plugins
 *
 * Provides common functionality for:
 * - Server connection management
 * - Workflow execution orchestration
 * - File I/O for image exchange
 * - Parameter management
 *
 * Derived classes must implement:
 * - buildWorkflow() - Construct the ComfyUI workflow JSON
 * - setupParameters() - Define plugin-specific parameters
 * - getRequiredModels() - List required AI models
 * - processOutput() - Handle ComfyUI results
 */
class BasePlugin : public OFX::ImageEffect {
protected:
    // Clips - Primary input and output
    OFX::Clip *_srcClip;      // Primary input (Source) - required
    OFX::Clip *_dstClip;      // Output

    // Optional secondary input clips for multi-input workflows
    OFX::Clip *_src2Clip;     // Secondary input (Source2) - optional
    OFX::Clip *_src3Clip;     // Tertiary input (Source3) - optional

    // Common parameters
    OFX::BooleanParam *_enableProcessing;      // Master enable/disable for ComfyUI processing
    OFX::StringParam *_serverAddress;
    OFX::IntParam *_serverPort;
    OFX::StringParam *_macMountPath;           // macOS client mount path
    OFX::StringParam *_winMountPath;           // Windows server mount path (UNC, e.g., "\\\\192.168.1.110\\share")
    OFX::StringParam *_projectName;            // Project name for file organization (e.g., "my_commercial")
    OFX::StringParam *_workflowName;           // Workflow subdirectory (e.g., "segmentation")
    OFX::StringParam *_outputVersion;          // Output version (e.g., "v001")
    OFX::StringParam *_workflowFilePath;       // Path to workflow JSON file (supports bundle resources)
    OFX::BooleanParam *_enableCache;
    OFX::IntParam *_timeout;

    // Async rendering parameters
    OFX::ChoiceParam *_asyncMode;              // Blocking vs Non-blocking rendering
    OFX::ChoiceParam *_placeholderMode;        // What to show while processing
    OFX::DoubleParam *_refreshTrigger;         // Hidden parameter for cache invalidation
    OFX::StringParam *_jobStatus;              // Read-only job status display
    OFX::RGBParam *_jobStatusColor;            // Visual status indicator (color swatch)
    OFX::ChoiceParam *_flipYMode;              // EXR Y-flip override: 0=Auto, 1=Always, 2=Never

    // Sequence plugins only: button to collect all frames and submit the job
    OFX::PushButtonParam *_collectAndSubmit;   // nullptr for non-sequence plugins

    // Instance identification
    std::string _instanceName;                 // OFX instance name for auto-basename generation

    // ComfyUI client and thread safety
    std::unique_ptr<Client> _comfyClient;
    std::unique_ptr<AsyncJobManager> _jobManager;  // Async job management
    mutable std::mutex _renderMutex;               // Protect client access in concurrent renders

    // Cache optimization (avoid slow network file existence checks)
    mutable std::unordered_set<std::string> _cacheFileExists;  // Files known to exist
    mutable std::unordered_map<int, std::pair<int, int>> _cacheDimensions;  // Frame -> (width, height)
    mutable std::mutex _cacheMutex;                            // Protect cache access

    // Sequence job state (only used when isSequencePlugin() == true)
    // Tracks which output prefix has an active sequence job.  Empty = none submitted.
    std::string _pendingSequenceOutputPrefix;
    // The first frame of the last-submitted sequence — used to key into the job manager
    // and to set SaveEXR.start_frame via ${FRAME} in customizeWorkflow().
    int _sequenceStartFrame = 0;
    // The last frame of the last-submitted sequence — used in onJobComplete() to seed
    // the in-memory cache and fire refresh triggers for all frames, not just startFrame.
    int _sequenceEndFrame = 0;

    // Incremental frame collection (sequence plugins only).
    // Each render(N) call fetches frame N on the render thread (safe: we are inside the
    // OFX render action).  When all frames are collected we hand the data to the background
    // thread for writing + submission.  The background thread NEVER calls fetchImage().
    std::map<int, ImageData> _pendingFrames;   // frame → pixel data collected so far
    std::string _pendingCollectionKey;          // identifies the current collection run
    mutable std::mutex _pendingFramesMutex;    // protects _pendingFrames/_pendingCollectionKey

    // Deferred Collect & Submit request.
    // changedParam(collectAndSubmit) stashes the request and bumps _refreshTrigger; the
    // next render() call performs the actual fetchImage loop on the render thread.
    // Calling fetchImage() from changedParam deadlocks DaVinci Resolve / Fusion (the
    // host's clipGetImage routes through Fusion::Input::GetSourceTagList which waits on
    // the same UI thread that owns the in-flight changedParam) — so we always defer.
    // The render thread is the OFX-canonical place for fetchImage() and works on every
    // host (Flame, Nuke, Resolve, Fusion).
    struct PendingCollectRequest {
        bool active     = false;
        int  startFrame = 0;
        int  endFrame   = 0;
    };
    PendingCollectRequest _pendingCollect;
    mutable std::mutex   _pendingCollectMutex;

    // One-shot flag so shouldFlipYForOFX() logs its decision once per instance
    // instead of once per frame. Mutable because shouldFlipYForOFX() is const.
    mutable bool _flipDecisionLogged = false;



    // Logging
    std::shared_ptr<spdlog::logger> _logger;

    void initializeLogger();

public:
    BasePlugin(OfxImageEffectHandle handle);
    virtual ~BasePlugin();

    // OFX lifecycle
    virtual void changedParam(const OFX::InstanceChangedArgs &args,
                             const std::string &paramName) override;
    virtual void render(const OFX::RenderArguments &args) override;
    virtual bool getRegionOfDefinition(const OFX::RegionOfDefinitionArguments &args,
                                      OfxRectD &rod) override;

    // Template method pattern - derived classes implement these
    // inputPaths maps input identifiers to file paths:
    //   "InputA" -> "/path/to/input.0001.exr"      (Source clip)
    //   "InputB" -> "/path/to/input_B.0001.exr"    (Source2 clip, if connected)
    //   "InputC" -> "/path/to/input_C.0001.exr"    (Source3 clip, if connected)
    virtual json buildWorkflow(int frame, const std::map<std::string, std::string>& inputPaths) = 0;
    virtual std::vector<std::string> getRequiredModels() = 0;

    // Override in derived classes to control basename generation
    // Returns true for generic plugins (AnyComfy) where workflow name varies
    // Returns false for specialized plugins (SAMSegmentation) where workflow is fixed
    virtual bool includeWorkflowInBasename() const { return false; }

    // Sequence plugin support
    // Returns true for plugins that process the full frame sequence as one ComfyUI job.
    // Frame-based plugins (depth_da3, segmentation, anycomfy) leave this as false.
    virtual bool isSequencePlugin() const { return false; }

    // Maximum number of frames to load from the sequence (0 = unlimited).
    // Sequence plugins that expose an imageLoadCap parameter override this.
    virtual int getImageLoadCap() const { return 0; }


protected:
    // Helper methods - Blocking workflow execution
    void executeWorkflow(const OFX::RenderArguments &args);
    std::string writeInputImage(OFX::Image* img, int frame, const std::string& suffix = "");
    void renderPassthrough(const OFX::RenderArguments &args);  // Fast passthrough for proxy renders
    void copyPixelData(const OFX::Image* src, OFX::Image* dst);
    std::string parseOutputPath(const json& history, int frame);
    std::string convertPathForComfyUI(const std::string& localPath);
    std::string constructExpectedOutputPath(int frame);
    std::string constructInputPath(int frame, const std::string& suffix = "");  // Construct path to input EXR file
    virtual std::string getEffectiveBasename();  // Get basename (auto-generated or manual) - virtual to allow custom naming schemes

    // Read a path-component string param (project/workflow/version/mount paths) and
    // strip surrounding whitespace + control chars. Without this, a pasted value with
    // a trailing newline silently corrupts every constructed path; on macOS the folder
    // gets created with the newline in its name, but the SMB-mounted Windows side
    // rejects it and ComfyUI fails with "Path not found".
    std::string getTrimmedStringParam(OFX::StringParam* param) const;

    // Multi-input support
    // Writes all connected input clips to EXR files, returns map of input ID -> path
    // InputA = Source (primary), InputB = Source2, InputC = Source3
    std::map<std::string, std::string> writeInputImages(int frame);

    // Returns count of connected input clips (1-3)
    int getConnectedInputCount() const;

    // Workflow file management
    std::string getBundleResourcePath(const std::string& resourceName);
    std::string resolveWorkflowPath(const std::string& workflowPath);
    json loadWorkflowFromFile(const std::string& filepath);
    json customizeWorkflow(const json& baseWorkflow, int frame, const std::map<std::string, std::string>& inputPaths);

    // Sequence input helpers
    // Returns folder path: {macMountPath}/in/projects/{project}/{workflow}/{version}/{basename}/
    std::string constructInputFolderPath() const;
    // Writes frames [startFrame..endFrame] from _srcClip to the input folder, one frame at a
    // time on the calling (render) thread — satisfying the OFX main-thread constraint.
    // Returns the folder path passed to the ComfyUI LoadEXR node.
    std::string writeInputSequence(int startFrame, int endFrame);

    // Drains a stashed Collect & Submit request, if any, on the render thread.
    // Safe to call unconditionally at the top of render(); becomes a no-op when no
    // request is pending. Sequence plugins only — non-sequence plugins never set the
    // pending request flag.
    void executePendingCollect();

    // Async rendering helper methods
    void renderBlocking(const OFX::RenderArguments &args);
    void renderAsync(const OFX::RenderArguments &args);
    void returnPlaceholder(const OFX::RenderArguments &args, int frame);
    void loadCachedResult(const OFX::RenderArguments &args, const std::string& cachedPath);
    void onJobComplete(int frame, bool success);
    void updateJobStatusDisplay();

    // Placeholder rendering modes
    void renderCheckerboard(OFX::Image* dst);
    void renderSolidColor(OFX::Image* dst, double r, double g, double b);
    int findLastValidFrame(double currentTime);

    // Returns true if the host's pixel buffer origin is bottom-left (OFX spec
    // default: Flame, Nuke). Returns false for top-left hosts (DaVinci Resolve,
    // Premiere). Used by image_io to decide whether to flip Y on EXR I/O so the
    // depth map / mask matches what the host actually displays.
    bool shouldFlipYForOFX() const;

public:
    // Static methods for parameter definition (called by derived plugin factories)
    static void describeCommonParameters(OFX::ImageEffectDescriptor &desc,
                                         OFX::ContextEnum context,
                                         OFX::PageParamDescriptor *projectPage,
                                         OFX::PageParamDescriptor *processingPage,
                                         OFX::PageParamDescriptor *serverPage,
                                         const json* configDefaults = nullptr,
                                         bool skipGroupHeaders = false,
                                         bool isSequencePlugin = false);

    // Configuration file management (public for factory access)
    static json loadConfigDefaults();
};

} // namespace ComfyUI

#endif // COMFYUI_BASE_PLUGIN_H
