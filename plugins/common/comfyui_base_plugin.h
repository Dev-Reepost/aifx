// Copyright OpenFX and contributors to the OpenFX project.
// SPDX-License-Identifier: BSD-3-Clause

#ifndef COMFYUI_BASE_PLUGIN_H
#define COMFYUI_BASE_PLUGIN_H

#include "ofxsImageEffect.h"
// Compile-time guard that this translation unit's view of the OpenFX Support
// ABI matches the OfxSupport archive it will be linked against. Included here
// (rather than per-plugin) so no plugin can opt out by accident -- every plugin
// header pulls in comfyui_base_plugin.h.
#include "aifx_abi_check.h"
#include "comfyui_client.h"
#include "comfyui_image_io.h"
#include "async_job_manager.h"
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <algorithm>
#include <memory>
#include <mutex>
#include <map>
#include <unordered_set>
#include <unordered_map>
#include <cstdlib>
#include <cctype>
#include <string>
#include <vector>

using json = nlohmann::json;

namespace ComfyUI {

/**
 * @brief Build identity stamped into every .ofx binary.
 *
 * A plain string constant holding the AIFX version, the OpenFX ABI tag, and the
 * pinned OpenFX commit, e.g.
 *
 *     AIFX-BUILD|version=0.2.0|ofxabi=v1-gl1-cl0-cu0|openfx=96ccd1a...
 *
 * It exists so a bundle found on a workstation can be identified without the
 * build tree: `strings <plugin>.ofx | grep AIFX-BUILD` works on an installed
 * plugin, and tools/verify-ofx-abi.sh reads it to reject a bundle whose ABI
 * does not match the running source tree. `initializeLogger()` also writes it
 * as the first line of every log session, so support logs carry the exact build.
 *
 * Deliberately referenced from live code rather than marked
 * __attribute__((used)): a real reference keeps it in every toolchain's output
 * (including MSVC) without compiler-specific pragmas.
 */
extern const char* const kAifxBuildMarker;

/**
 * @brief Resolve the user's home directory in a cross-platform way.
 *
 * POSIX sets HOME, but Windows usually does not — there the home directory is
 * exposed via USERPROFILE (or HOMEDRIVE + HOMEPATH). Relying on getenv("HOME")
 * alone silently disables logging and config discovery on Windows, so every
 * caller must go through this helper instead.
 *
 * @return The home directory path, or an empty string if none could be found.
 */
inline std::string getHomeDir()
{
    if (const char* home = std::getenv("HOME")) {
        return home;
    }
#ifdef _WIN32
    if (const char* userProfile = std::getenv("USERPROFILE")) {
        return userProfile;
    }
    const char* drive = std::getenv("HOMEDRIVE");
    const char* path = std::getenv("HOMEPATH");
    if (drive && path) {
        return std::string(drive) + path;
    }
#endif
    return std::string();
}

/**
 * @brief Cross-platform test for whether a path is absolute.
 *
 * POSIX absolute paths start with '/'. Windows additionally has drive-letter
 * paths ("C:\\..." or "C:/...") and UNC paths ("\\\\server\\share"); a leading
 * backslash also denotes a (drive-relative) root on Windows.
 */
inline bool isAbsolutePath(const std::string& path)
{
    if (path.empty()) {
        return false;
    }
    if (path[0] == '/' || path[0] == '\\') {
        return true;
    }
#ifdef _WIN32
    // Drive-letter path, e.g. "C:\\foo" or "C:/foo".
    if (path.size() >= 3 && std::isalpha(static_cast<unsigned char>(path[0])) &&
        path[1] == ':' && (path[2] == '/' || path[2] == '\\')) {
        return true;
    }
#endif
    return false;
}

/**
 * @brief Does this path use Windows conventions (UNC or drive letter)?
 *
 * Used to decide which separator the ComfyUI server expects. The decision is
 * made from the configured server mount string, NOT from the OS this plugin
 * was built for: the ComfyUI box can run Windows (UNC "\\\\server\\share" or
 * "D:\\share") or POSIX ("/mnt/share"), independently of the host running the
 * plugin. Anything that is not UNC/drive-letter is treated as POSIX.
 */
inline bool isWindowsStylePath(const std::string& path)
{
    if (path.empty()) {
        return false;
    }
    // UNC share, e.g. "\\\\server\\share".
    if (path.size() >= 2 && path[0] == '\\' && path[1] == '\\') {
        return true;
    }
    // Drive-letter path, e.g. "D:\\share" or "D:/share" (and bare "D:").
    if (path.size() >= 2 && std::isalpha(static_cast<unsigned char>(path[0])) && path[1] == ':') {
        return true;
    }
    return false;
}

/**
 * @brief Normalise all separators in a path to the given server convention.
 *
 * Windows accepts forward slashes in most APIs but ComfyUI custom nodes hand
 * the string straight to os.path / open(), so a POSIX server chokes on
 * backslashes (they become literal filename characters) and a Windows server
 * on a mixed UNC prefix. Convert wholesale, in whichever direction applies.
 */
inline std::string normalizeSeparators(const std::string& path, bool windowsStyle)
{
    std::string out = path;
    std::replace(out.begin(), out.end(), windowsStyle ? '/' : '\\', windowsStyle ? '\\' : '/');
    return out;
}

/**
 * @brief Strip trailing '/' and '\\' separators from a mount path.
 *
 * Mount paths are spliced onto '/'-prefixed relative paths; a trailing
 * separator on either mount yields a doubled separator ("//" or "\\\\") in the
 * middle of the result, which a POSIX server tolerates but a Windows one
 * rejects (and which turns a UNC prefix into nonsense).
 */
inline std::string stripTrailingSeparators(const std::string& path)
{
    size_t end = path.size();
    while (end > 0 && (path[end - 1] == '/' || path[end - 1] == '\\')) {
        --end;
    }
    return path.substr(0, end);
}

/**
 * @brief Build the ordered list of candidate defaults.json paths for the given
 *        OFX bundles, across platforms.
 *
 * Searches, most specific first: every root listed in OFX_PLUGIN_PATH (the
 * canonical OFX host search path, so config is found wherever the host actually
 * loaded the bundle from), then the user's per-user OFX directories, then the
 * system-wide install locations for macOS (/Library/OFX/Plugins) and Windows
 * (C:\Program Files\Common Files\OFX\Plugins, plus %CommonProgramFiles%).
 *
 * @param bundleNames Bundle base names without the ".ofx.bundle" suffix, in
 *                    priority order (e.g. {"DepthAnything3"}).
 */
inline std::vector<std::string> getOfxConfigSearchPaths(const std::vector<std::string>& bundleNames)
{
    const std::string suffix = ".ofx.bundle/Contents/Resources/config/defaults.json";
    const std::string home = getHomeDir();

#ifdef _WIN32
    const char pathSep = ';';
#else
    const char pathSep = ':';
#endif

    // Roots that directly contain bundle directories (i.e. an ".../OFX/Plugins").
    std::vector<std::string> ofxRoots;
    if (const char* pluginPath = std::getenv("OFX_PLUGIN_PATH")) {
        const std::string value(pluginPath);
        size_t start = 0;
        while (start < value.size()) {
            size_t end = value.find(pathSep, start);
            if (end == std::string::npos) end = value.size();
            if (end > start) ofxRoots.push_back(value.substr(start, end - start));
            start = end + 1;
        }
    }

    std::vector<std::string> paths;
    for (const auto& bundle : bundleNames) {
        for (const auto& root : ofxRoots) {
            paths.push_back(root + "/" + bundle + suffix);
        }
        // Per-user OFX directories.
        if (!home.empty()) {
            paths.push_back(home + "/Library/OFX/Plugins/" + bundle + suffix);   // macOS user
            paths.push_back(home + "/OFX/Plugins/" + bundle + suffix);           // generic (Linux) user
        }

        // Standard system OFX plugin directories for every platform, per the OFX
        // spec. All are probed on every OS — a path that doesn't exist just fails
        // to open and is skipped — so config is found wherever the host loaded the
        // bundle from, whether that's macOS, Windows, or Linux (e.g. Flame loads
        // from /usr/OFX/Plugins, which earlier builds omitted and so never found
        // their defaults.json on Linux).
        paths.push_back("/Library/OFX/Plugins/" + bundle + suffix);                       // macOS
        paths.push_back("/usr/OFX/Plugins/" + bundle + suffix);                           // Linux
        paths.push_back("/usr/local/OFX/Plugins/" + bundle + suffix);                     // Linux (local install)
        paths.push_back("C:/Program Files/Common Files/OFX/Plugins/" + bundle + suffix);  // Windows
        if (const char* commonProgramFiles = std::getenv("CommonProgramFiles")) {         // Windows (relocated Common Files)
            paths.push_back(std::string(commonProgramFiles) + "/OFX/Plugins/" + bundle + suffix);
        }
    }
    return paths;
}

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
    //
    // NOTE: every clip/param pointer below carries an in-class `= nullptr`
    // initializer. This is load-bearing, not stylistic: the constructor's
    // env-discovery dump calls shouldFlipYForOFX() — which reads _flipYMode —
    // BEFORE the parameter-fetch block assigns these members. Any pointer left
    // indeterminate there is read as garbage; `if (_flipYMode)` then passes and
    // the following ->getValue() dereferences a wild pointer (SIGSEGV that no
    // try/catch can intercept). macOS hosts happened to get zeroed memory and
    // survived; Flame on Linux got non-zero garbage and crashed on instancing.
    // Keep these initializers even though the constructor init-list also sets
    // most of them — they guarantee safety for any read-before-fetch path.
    OFX::Clip *_srcClip = nullptr;      // Primary input (Source) - required
    OFX::Clip *_dstClip = nullptr;      // Output

    // Optional secondary input clips for multi-input workflows
    OFX::Clip *_src2Clip = nullptr;     // Secondary input (Source2) - optional
    OFX::Clip *_src3Clip = nullptr;     // Tertiary input (Source3) - optional

    // Common parameters
    OFX::BooleanParam *_enableProcessing = nullptr;  // Master enable/disable for ComfyUI processing
    OFX::StringParam *_serverAddress = nullptr;
    OFX::IntParam *_serverPort = nullptr;
    // The shared storage, addressed two ways: as this host sees it (local EXR
    // I/O) and as the ComfyUI server sees it (written into the workflow). They
    // differ because the host running the plugin and the ComfyUI box are
    // different machines mounting the same share at different paths.
    OFX::StringParam *_localMountPath = nullptr;      // This host's mount  (e.g. /Volumes/comfyui-share or /mnt/comfyui-share)
    OFX::StringParam *_serverMountPath = nullptr;     // ComfyUI server's mount (UNC, e.g. \\HOSTNAME\share)
    OFX::StringParam *_projectName = nullptr;         // Project name for file organization (e.g., "my_commercial")
    OFX::StringParam *_workflowName = nullptr;        // Workflow subdirectory (e.g., "segmentation")
    OFX::StringParam *_outputVersion = nullptr;       // Output version (e.g., "v001")
    OFX::StringParam *_workflowFilePath = nullptr;    // Path to workflow JSON file (supports bundle resources)
    OFX::BooleanParam *_enableCache = nullptr;
    OFX::IntParam *_timeout = nullptr;

    // Async rendering parameters
    OFX::ChoiceParam *_asyncMode = nullptr;          // Blocking vs Non-blocking rendering
    OFX::ChoiceParam *_placeholderMode = nullptr;    // What to show while processing
    OFX::DoubleParam *_refreshTrigger = nullptr;     // Hidden parameter for cache invalidation
    OFX::StringParam *_jobStatus = nullptr;          // Read-only job status display
    OFX::RGBParam *_jobStatusColor = nullptr;        // Visual status indicator (color swatch)
    OFX::ChoiceParam *_flipYMode = nullptr;          // EXR Y-flip override: 0=Auto, 1=Always, 2=Never

    // Sequence plugins only: button to collect all frames and submit the job
    OFX::PushButtonParam *_collectAndSubmit = nullptr;  // nullptr for non-sequence plugins

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

    /**
     * @brief Predicted output spatial scale, relative to the source clip RoD.
     *
     * Plugins whose ComfyUI workflow changes the output resolution (upscalers,
     * downscalers, croppers, format changers) override this so the OFX host
     * can size the downstream canvas correctly before the first frame has been
     * rendered to disk. The base implementation returns identity scale
     * ({1, 1}), which is correct for any plugin whose output matches the
     * source resolution.
     *
     * The base getRegionOfDefinition() applies this scale to the source RoD
     * whenever no rendered output is available yet on disk. Once an output
     * EXR exists, its actual dimensions take precedence — the predicted scale
     * is purely a first-frame / parameter-change hint to the host.
     *
     * Implementations must be cheap (no disk I/O, no ComfyUI calls). For
     * non-uniform scaling, return distinct x/y factors. For uniform scaling
     * (e.g. an upscaler that preserves aspect), return the same value for both.
     *
     * @param time OFX timeline time of the request.
     * @return Scale factors {x, y} where 1.0 = no change.
     */
    virtual OfxPointD getPredictedOutputScale(double time) const {
        (void)time;
        OfxPointD identity = { 1.0, 1.0 };
        return identity;
    }

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

    // Return this host's mount of the shared storage — the "client" mount used
    // for all local EXR read/write. Falls back to the ComfyUI server mount when
    // unset (single-box setup, or this host reaches the share at the same path).
    std::string getLocalMountPath() const;

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
    // Returns folder path: {mount}/in/projects/{project}/{workflow}/{version}/{basename}/
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
    /** @brief How a rendered frame has to be mapped onto the buffer the host gave us. */
    enum class OutputFitMode {
        FullFrame,       ///< Render window is exactly our output — copy straight through.
        SubRegion,       ///< Genuine tile inside a canvas that IS our output size.
        HostRefusedRoD,  ///< Host allocated a canvas that is not the size we reported.
        Fit              ///< Benign mismatch — scale to fit the window and centre.
    };

    /** @brief Decide which of the four cases above applies. Pure function, no OFX state.
     *
     *  The order matters. A fixed-format host's full-frame request is
     *  indistinguishable from a tile by geometry alone — a (0,0)-(1280,720)
     *  window IS "inside" a 1920x1080 result — so the host's own canvas
     *  (kOfxImagePropRegionOfDefinition on the output image) has to be checked
     *  before the sub-region test, or an upscale gets silently corner-cropped.
     *
     *  A refusal is a canvas materially SMALLER than the result — the only case
     *  where pixels would have to be destroyed. A canvas that is larger (a
     *  fixed-size model such as DepthCrafter with Force Size) or off by a
     *  rounding pixel or two falls through to Fit and loses nothing.
     *
     *  @param dstRodWidth/dstRodHeight The canvas the host allocated. Pass 0 when
     *         the host did not report one: the refusal check is then skipped,
     *         since a refusal cannot be told apart from a tile without it.
     */
    static OutputFitMode classifyOutputFit(int outputWidth, int outputHeight,
                                           int dstRodWidth, int dstRodHeight,
                                           const OfxRectI& renderWindow);

protected:

    // Returns true if the HOST supports multi-resolution — i.e. it accepts an
    // output image whose region of definition differs from the input clip's.
    //
    // Read from the host descriptor, NOT from our own property set: we declare
    // kOfxImageEffectPropSupportsMultiResolution on the plugin descriptor, so
    // querying the instance property set only tells us what *we* said, never
    // what the host can do. Hosts that report 0 here (Autodesk Flame / Flare)
    // are spec-compliant and are entitled to ignore the RoD we return from
    // getRegionOfDefinition(), handing us a buffer that is always the size of
    // the input clip. Resolution-changing plugins (SeedVR2) cannot widen the
    // canvas on such a host — see loadCachedResult()'s mismatch handling.
    bool hostSupportsMultiResolution() const;

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
};

} // namespace ComfyUI

#endif // COMFYUI_BASE_PLUGIN_H
