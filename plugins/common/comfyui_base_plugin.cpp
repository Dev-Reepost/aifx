// Copyright OpenFX and contributors to the OpenFX project.
// SPDX-License-Identifier: BSD-3-Clause

#include "comfyui_base_plugin.h"
#include "comfyui_image_io.h"
#include <sstream>
#include <iomanip>
#include <iostream>
#include <fstream>
#include <chrono>
#include <ctime>
#include <thread>
#include <cstring>
#include <filesystem>
#include <system_error>
#include <algorithm>

#ifdef _WIN32
    #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
    #endif
    #include <windows.h>
    #include <sys/stat.h>
    #define S_ISDIR(m) (((m) & _S_IFMT) == _S_IFDIR)
#else
    #include <sys/stat.h>
#endif

#ifdef __APPLE__
#include <dlfcn.h>
#endif

#if defined(__APPLE__) || defined(_WIN32)
// Anchor symbol used to locate this shared library at runtime when the host
// does not populate kOfxPluginPropFilePath (e.g. DaVinci Resolve): its address
// lies inside this module, resolved via dladdr (macOS) or GetModuleHandleEx
// FROM_ADDRESS (Windows). A pointer-to-member-function cannot be cast to void*,
// so we use this free symbol instead.
static const char _dladdr_anchor = 0;
#endif

#if !defined(_WIN32)
#include <execinfo.h>
#include <csignal>
#include <fcntl.h>
#include <unistd.h>
#endif

namespace ComfyUI {

#if !defined(_WIN32)
// ---------------------------------------------------------------------------
// Crash backtrace handler (diagnostic).
//
// Flame on Linux segfaults the instant a plugin is instanced, in a code path
// that runs AFTER the constructor finishes logging but BEFORE any logged
// instance method — so spdlog never captures where. This installs an
// async-signal-safe SIGSEGV/SIGABRT/SIGBUS handler that writes a symbolized
// backtrace straight to a raw fd (~/comfyui_crash_<date>.log) and then chains
// to whatever handler Flame had installed, so the host still does its own
// reporting. backtrace_symbols_fd() and write() are async-signal-safe.
// ---------------------------------------------------------------------------
namespace {

int                g_crashFd = -1;
struct sigaction   g_oldSegv, g_oldAbrt, g_oldBus, g_oldFpe, g_oldIll;

// async-signal-safe raw write of a C string
void crashWrite(const char* s) {
    if (g_crashFd < 0 || !s) return;
    size_t n = 0; while (s[n]) ++n;
    ssize_t rc = ::write(g_crashFd, s, n); (void)rc;
}

// async-signal-safe unsigned -> hex
void crashWriteHex(unsigned long v) {
    char buf[2 + sizeof(v) * 2];
    char* p = buf;
    *p++ = '0'; *p++ = 'x';
    bool started = false;
    for (int shift = (int)(sizeof(v) * 8) - 4; shift >= 0; shift -= 4) {
        unsigned nib = (v >> shift) & 0xF;
        if (nib || started || shift == 0) {
            *p++ = (char)(nib < 10 ? '0' + nib : 'a' + (nib - 10));
            started = true;
        }
    }
    ssize_t rc = ::write(g_crashFd, buf, (size_t)(p - buf)); (void)rc;
}

void crashHandler(int sig, siginfo_t* info, void* ucontext) {
    crashWrite("\n=== AIFX PLUGIN CRASH ===\nsignal=");
    crashWriteHex((unsigned long)sig);
    crashWrite(" fault_addr=");
    crashWriteHex(info ? (unsigned long)info->si_addr : 0UL);
    crashWrite("\nbacktrace:\n");

    void* frames[64];
    int n = backtrace(frames, 64);
    backtrace_symbols_fd(frames, n, g_crashFd);
    crashWrite("=== END CRASH ===\n");
    if (g_crashFd >= 0) ::fsync(g_crashFd);

    // Chain to the host's previous handler so Flame still reports/cleans up.
    struct sigaction* old = nullptr;
    switch (sig) {
        case SIGSEGV: old = &g_oldSegv; break;
        case SIGABRT: old = &g_oldAbrt; break;
        case SIGBUS:  old = &g_oldBus;  break;
        case SIGFPE:  old = &g_oldFpe;  break;
        case SIGILL:  old = &g_oldIll;  break;
    }
    if (old) {
        sigaction(sig, old, nullptr);
        if (old->sa_flags & SA_SIGINFO) {
            if (old->sa_sigaction) { old->sa_sigaction(sig, info, ucontext); return; }
        } else if (old->sa_handler != SIG_DFL && old->sa_handler != SIG_IGN) {
            old->sa_handler(sig); return;
        }
    }
    raise(sig);  // fall back to default disposition
}

} // anonymous namespace

void installCrashHandler(const std::string& home) {
    static std::once_flag once;
    std::call_once(once, [&]() {
        // Open a dedicated crash log next to the daily plugin log.
        auto now = std::chrono::system_clock::now();
        std::time_t now_c = std::chrono::system_clock::to_time_t(now);
        std::tm* now_tm = std::localtime(&now_c);
        char dateStamp[16];
        std::strftime(dateStamp, sizeof(dateStamp), "%Y%m%d", now_tm);
        std::string path = (home.empty() ? std::string("/tmp") : home)
                           + "/comfyui_crash_" + dateStamp + ".log";
        g_crashFd = ::open(path.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0644);
        if (g_crashFd < 0) return;

        struct sigaction sa;
        std::memset(&sa, 0, sizeof(sa));
        sa.sa_sigaction = crashHandler;
        sa.sa_flags = SA_SIGINFO | SA_RESTART;
        sigemptyset(&sa.sa_mask);
        sigaction(SIGSEGV, &sa, &g_oldSegv);
        sigaction(SIGABRT, &sa, &g_oldAbrt);
        sigaction(SIGBUS,  &sa, &g_oldBus);
        sigaction(SIGFPE,  &sa, &g_oldFpe);
        sigaction(SIGILL,  &sa, &g_oldIll);
    });
}
#else
void installCrashHandler(const std::string&) {}
#endif

void BasePlugin::initializeLogger()
{
    try {
        // Resolve the home directory (HOME on POSIX, USERPROFILE on Windows).
        std::string home = getHomeDir();
        if (home.empty()) {
            std::cerr << "WARNING: could not resolve home directory, logging disabled" << std::endl;
            return;
        }

        // Create daily log file (YYYYMMDD format - one log per day)
        auto now = std::chrono::system_clock::now();
        std::time_t now_c = std::chrono::system_clock::to_time_t(now);
        std::tm* now_tm = std::localtime(&now_c);

        char dateStamp[16];
        std::strftime(dateStamp, sizeof(dateStamp), "%Y%m%d", now_tm);

        std::string logPath = std::string(home) + "/comfyui_plugin_" +
                             std::string(dateStamp) + ".log";

        // Check if logger already exists (reuse same logger for the day)
        if (spdlog::get("comfyui_plugin")) {
            _logger = spdlog::get("comfyui_plugin");
        } else {
            // Create new logger for this day
            _logger = spdlog::basic_logger_mt("comfyui_plugin", logPath);
            _logger->set_level(spdlog::level::trace);  // Log everything
            _logger->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] %v");
            _logger->flush_on(spdlog::level::info);  // Flush on every info message
        }

        if (_logger) {
            char timeStamp[32];
            std::strftime(timeStamp, sizeof(timeStamp), "%H:%M:%S", now_tm);
            _logger->info("=== ComfyUI Plugin Session Started at {} ===", timeStamp);
            _logger->info("Log file: {}", logPath);
        }
    } catch (const spdlog::spdlog_ex& ex) {
        std::cerr << "Log initialization failed: " << ex.what() << std::endl;
        _logger.reset(); // Make sure logger is null if initialization failed
    } catch (const std::exception& ex) {
        std::cerr << "Log initialization exception: " << ex.what() << std::endl;
        _logger.reset();
    } catch (...) {
        std::cerr << "Unknown exception during log initialization" << std::endl;
        _logger.reset();
    }
}

BasePlugin::BasePlugin(OfxImageEffectHandle handle)
    : ImageEffect(handle)
    , _srcClip(nullptr)
    , _dstClip(nullptr)
    , _src2Clip(nullptr)
    , _src3Clip(nullptr)
    , _enableProcessing(nullptr)
    , _serverAddress(nullptr)
    , _serverPort(nullptr)
    , _localMountPath(nullptr)
    , _serverMountPath(nullptr)
    , _projectName(nullptr)
    , _workflowName(nullptr)
    , _outputVersion(nullptr)
    , _enableCache(nullptr)
    , _timeout(nullptr)
    , _collectAndSubmit(nullptr)
    , _instanceName("")
{
    // Initialize logger first
    initializeLogger();

    // Install a crash backtrace handler (diagnostic). Writes a symbolized stack
    // to ~/comfyui_crash_<date>.log on SIGSEGV/SIGABRT/SIGBUS, then chains to
    // the host's handler. Idempotent across instances.
    installCrashHandler(getHomeDir());

    if (_logger) _logger->info("BasePlugin constructor started");

    // Try to get instance name from OFX properties (store for auto-basename generation)
    try {
        _instanceName = getPropertySet().propGetString(kOfxPropName, false);
        if (_logger && !_instanceName.empty()) {
            _logger->info("OFX Instance Name (kOfxPropName): {}", _instanceName);
        }
    } catch (...) {
        if (_logger) _logger->warn("Could not retrieve kOfxPropName from instance");
    }

    // Fall back to label if name is empty
    if (_instanceName.empty()) {
        try {
            _instanceName = getPropertySet().propGetString(kOfxPropLabel, false);
            if (_logger && !_instanceName.empty()) {
                _logger->info("OFX Instance Label (kOfxPropLabel): {}", _instanceName);
            }
        } catch (...) {
            if (_logger) _logger->warn("Could not retrieve kOfxPropLabel from instance");
        }
    }

    _srcClip = fetchClip(kOfxImageEffectSimpleSourceClipName);
    _dstClip = fetchClip(kOfxImageEffectOutputClipName);

    // Fetch optional secondary input clips (may return nullptr if not defined)
    try {
        _src2Clip = fetchClip("Source2");
        if (_logger && _src2Clip) _logger->info("Source2 clip available");
    } catch (...) {
        _src2Clip = nullptr;
        if (_logger) _logger->debug("Source2 clip not defined for this plugin");
    }

    try {
        _src3Clip = fetchClip("Source3");
        if (_logger && _src3Clip) _logger->info("Source3 clip available");
    } catch (...) {
        _src3Clip = nullptr;
        if (_logger) _logger->debug("Source3 clip not defined for this plugin");
    }

    // Log all available OFX context properties for testing
    if (_logger) {
        _logger->info("=======================================================");
        _logger->info("=== COMPREHENSIVE OFX ENVIRONMENT DISCOVERY ===");
        _logger->info("=======================================================");

        // Effect context
        try {
            OFX::ContextEnum ctx = getContext();
            std::string ctxStr = "UNKNOWN";
            if (ctx == OFX::eContextFilter) ctxStr = "Filter";
            else if (ctx == OFX::eContextGeneral) ctxStr = "General";
            else if (ctx == OFX::eContextGenerator) ctxStr = "Generator";
            else if (ctx == OFX::eContextTransition) ctxStr = "Transition";
            else if (ctx == OFX::eContextPaint) ctxStr = "Paint";
            else if (ctx == OFX::eContextRetimer) ctxStr = "Retimer";
            _logger->info("Effect Context: {}", ctxStr);
        } catch (...) { _logger->warn("Effect Context: NOT AVAILABLE"); }

        // Plugin handle properties
        _logger->info("--- Plugin Descriptor Properties ---");
        try {
            std::string pluginId = getPropertySet().propGetString(kOfxPluginPropFilePath, false);
            _logger->info("Plugin File Path: '{}'", pluginId);
        } catch (...) { _logger->warn("Plugin File Path: NOT AVAILABLE"); }

        // Instance properties
        _logger->info("--- Instance Properties ---");
        try {
            std::string name = getPropertySet().propGetString(kOfxPropName, false);
            _logger->info("Instance kOfxPropName: '{}'", name);
        } catch (...) { _logger->warn("Instance kOfxPropName: NOT AVAILABLE"); }

        try {
            std::string label = getPropertySet().propGetString(kOfxPropLabel, false);
            _logger->info("Instance kOfxPropLabel: '{}'", label);
        } catch (...) { _logger->warn("Instance kOfxPropLabel: NOT AVAILABLE"); }

        try {
            std::string shortLabel = getPropertySet().propGetString(kOfxPropShortLabel, false);
            _logger->info("Instance kOfxPropShortLabel: '{}'", shortLabel);
        } catch (...) { _logger->warn("Instance kOfxPropShortLabel: NOT AVAILABLE"); }

        try {
            std::string longLabel = getPropertySet().propGetString(kOfxPropLongLabel, false);
            _logger->info("Instance kOfxPropLongLabel: '{}'", longLabel);
        } catch (...) { _logger->warn("Instance kOfxPropLongLabel: NOT AVAILABLE"); }

        try {
            std::string type = getPropertySet().propGetString(kOfxPropType, false);
            _logger->info("Instance kOfxPropType: '{}'", type);
        } catch (...) { _logger->warn("Instance kOfxPropType: NOT AVAILABLE"); }

        // Effect Instance Handle (pointer - NOT stable across runs)
        try {
            void* instanceHandle = getPropertySet().propGetPointer(kOfxPropEffectInstance, false);
            if (instanceHandle) {
                std::ostringstream hexStr;
                hexStr << "0x" << std::hex << reinterpret_cast<uintptr_t>(instanceHandle);
                _logger->info("kOfxPropEffectInstance: {} (RUNTIME POINTER - not stable)", hexStr.str());
            } else {
                _logger->warn("kOfxPropEffectInstance: NULL");
            }
        } catch (...) { _logger->warn("kOfxPropEffectInstance: NOT AVAILABLE"); }

        // Instance Data Pointer (custom data - NOT stable across runs)
        try {
            void* instanceData = getPropertySet().propGetPointer(kOfxPropInstanceData, false);
            if (instanceData) {
                std::ostringstream hexStr;
                hexStr << "0x" << std::hex << reinterpret_cast<uintptr_t>(instanceData);
                _logger->info("kOfxPropInstanceData: {} (RUNTIME POINTER - not stable)", hexStr.str());
            } else {
                _logger->info("kOfxPropInstanceData: NULL (expected if not set)");
            }
        } catch (...) { _logger->warn("kOfxPropInstanceData: NOT AVAILABLE"); }

        // Project size/extent properties
        _logger->info("--- Project Properties ---");
        try {
            int projectWidth = getPropertySet().propGetInt(kOfxImageEffectPropProjectSize, 0, false);
            int projectHeight = getPropertySet().propGetInt(kOfxImageEffectPropProjectSize, 1, false);
            _logger->info("Project Size: {}x{}", projectWidth, projectHeight);
        } catch (...) { _logger->warn("Project Size: NOT AVAILABLE"); }

        try {
            int extentWidth = getPropertySet().propGetInt(kOfxImageEffectPropProjectExtent, 0, false);
            int extentHeight = getPropertySet().propGetInt(kOfxImageEffectPropProjectExtent, 1, false);
            _logger->info("Project Extent: {}x{}", extentWidth, extentHeight);
        } catch (...) { _logger->warn("Project Extent: NOT AVAILABLE"); }

        try {
            int offsetX = getPropertySet().propGetInt(kOfxImageEffectPropProjectOffset, 0, false);
            int offsetY = getPropertySet().propGetInt(kOfxImageEffectPropProjectOffset, 1, false);
            _logger->info("Project Offset: ({},{})", offsetX, offsetY);
        } catch (...) { _logger->warn("Project Offset: NOT AVAILABLE"); }

        try {
            double pixelAspect = getPropertySet().propGetDouble(kOfxImageEffectPropProjectPixelAspectRatio, false);
            _logger->info("Project Pixel Aspect Ratio: {}", pixelAspect);
        } catch (...) { _logger->warn("Project Pixel Aspect Ratio: NOT AVAILABLE"); }

        try {
            double duration = getPropertySet().propGetDouble(kOfxImageEffectInstancePropEffectDuration, false);
            _logger->info("Effect Duration: {}", duration);
        } catch (...) { _logger->warn("Effect Duration: NOT AVAILABLE"); }

        try {
            double frameRate = getPropertySet().propGetDouble(kOfxImageEffectPropFrameRate, false);
            _logger->info("Frame Rate: {}", frameRate);
        } catch (...) { _logger->warn("Frame Rate: NOT AVAILABLE"); }

        try {
            double frameMin = getPropertySet().propGetDouble(kOfxImageEffectPropFrameRange, 0, false);
            double frameMax = getPropertySet().propGetDouble(kOfxImageEffectPropFrameRange, 1, false);
            _logger->info("Frame Range: {} - {}", frameMin, frameMax);
        } catch (...) { _logger->warn("Frame Range: NOT AVAILABLE"); }

        // Source clip properties
        if (_srcClip) {
            _logger->info("--- Source Clip Properties ---");
            try {
                std::string clipName = _srcClip->getPropertySet().propGetString(kOfxPropName, false);
                _logger->info("Source Clip kOfxPropName: '{}'", clipName);
            } catch (...) { _logger->warn("Source Clip kOfxPropName: NOT AVAILABLE"); }

            try {
                std::string clipLabel = _srcClip->getPropertySet().propGetString(kOfxPropLabel, false);
                _logger->info("Source Clip kOfxPropLabel: '{}'", clipLabel);
            } catch (...) { _logger->warn("Source Clip kOfxPropLabel: NOT AVAILABLE"); }

            try {
                std::string clipConnected = _srcClip->isConnected() ? "YES" : "NO";
                _logger->info("Source Clip Connected: {}", clipConnected);
            } catch (...) { _logger->warn("Source Clip Connected: UNKNOWN"); }
        } else {
            _logger->warn("Source Clip: NULL");
        }

        // Output clip properties
        if (_dstClip) {
            _logger->info("--- Output Clip Properties ---");
            try {
                std::string clipName = _dstClip->getPropertySet().propGetString(kOfxPropName, false);
                _logger->info("Output Clip kOfxPropName: '{}'", clipName);
            } catch (...) { _logger->warn("Output Clip kOfxPropName: NOT AVAILABLE"); }

            try {
                std::string clipLabel = _dstClip->getPropertySet().propGetString(kOfxPropLabel, false);
                _logger->info("Output Clip kOfxPropLabel: '{}'", clipLabel);
            } catch (...) { _logger->warn("Output Clip kOfxPropLabel: NOT AVAILABLE"); }
        } else {
            _logger->warn("Output Clip: NULL");
        }

        // Environment/System info
        _logger->info("--- System Environment ---");
        try {
            const char* home = getenv("HOME");
            if (home) _logger->info("HOME: '{}'", home);
            else _logger->warn("HOME: NOT SET");
            _logger->info("Resolved home dir: '{}'", getHomeDir());
        } catch (...) { _logger->warn("HOME: ERROR"); }

        try {
            const char* user = getenv("USER");
            if (user) _logger->info("USER: '{}'", user);
            else _logger->warn("USER: NOT SET");
        } catch (...) { _logger->warn("USER: ERROR"); }

        try {
            const char* hostname = getenv("HOSTNAME");
            if (hostname) _logger->info("HOSTNAME: '{}'", hostname);
            else _logger->warn("HOSTNAME: NOT SET");
        } catch (...) { _logger->warn("HOSTNAME: ERROR"); }

        try {
            const char* pwd = getenv("PWD");
            if (pwd) _logger->info("PWD: '{}'", pwd);
            else _logger->warn("PWD: NOT SET");
        } catch (...) { _logger->warn("PWD: ERROR"); }

        // Additional render properties
        _logger->info("--- Render Properties ---");
        try {
            double scaleX = getPropertySet().propGetDouble(kOfxImageEffectPropRenderScale, 0, false);
            double scaleY = getPropertySet().propGetDouble(kOfxImageEffectPropRenderScale, 1, false);
            _logger->info("Render Scale: {}x{}", scaleX, scaleY);
        } catch (...) { _logger->warn("Render Scale: NOT AVAILABLE"); }

        try {
            int seqRender = getPropertySet().propGetInt(kOfxImageEffectInstancePropSequentialRender, false);
            _logger->info("Sequential Render: {}", seqRender ? "YES" : "NO");
        } catch (...) { _logger->warn("Sequential Render: NOT AVAILABLE"); }

        // Plugin capabilities
        _logger->info("--- Plugin Capabilities ---");
        try {
            int supportsTiles = getPropertySet().propGetInt(kOfxImageEffectPropSupportsTiles, false);
            _logger->info("Supports Tiles: {}", supportsTiles ? "YES" : "NO");
        } catch (...) { _logger->warn("Supports Tiles: NOT AVAILABLE"); }

        try {
            int supportsMultiRes = getPropertySet().propGetInt(kOfxImageEffectPropSupportsMultiResolution, false);
            _logger->info("Supports Multi-Resolution: {}", supportsMultiRes ? "YES" : "NO");
        } catch (...) { _logger->warn("Supports Multi-Resolution: NOT AVAILABLE"); }

        try {
            int temporalAccess = getPropertySet().propGetInt(kOfxImageEffectPropTemporalClipAccess, false);
            _logger->info("Temporal Clip Access: {}", temporalAccess ? "YES" : "NO");
        } catch (...) { _logger->warn("Temporal Clip Access: NOT AVAILABLE"); }

        // GPU Support (properties may not be available in all contexts)
        _logger->info("--- GPU Support ---");
        _logger->info("(GPU render properties require GPU rendering extensions)");

        // Clip format properties (Source clip)
        if (_srcClip && _srcClip->isConnected()) {
            _logger->info("--- Source Clip Format ---");
            try {
                std::string components = _srcClip->getPropertySet().propGetString(kOfxImageEffectPropComponents, false);
                _logger->info("Source Components: '{}'", components);
            } catch (...) { _logger->warn("Source Components: NOT AVAILABLE"); }

            try {
                std::string pixelDepth = _srcClip->getPropertySet().propGetString(kOfxImageEffectPropPixelDepth, false);
                _logger->info("Source Pixel Depth: '{}'", pixelDepth);
            } catch (...) { _logger->warn("Source Pixel Depth: NOT AVAILABLE"); }

            try {
                std::string preMultiplication = _srcClip->getPropertySet().propGetString(kOfxImageEffectPropPreMultiplication, false);
                _logger->info("Source PreMultiplication: '{}'", preMultiplication);
            } catch (...) { _logger->warn("Source PreMultiplication: NOT AVAILABLE"); }

            try {
                std::string fieldOrder = _srcClip->getPropertySet().propGetString(kOfxImageClipPropFieldOrder, false);
                _logger->info("Source Field Order: '{}'", fieldOrder);
            } catch (...) { _logger->warn("Source Field Order: NOT AVAILABLE"); }

            try {
                double frameRate = _srcClip->getPropertySet().propGetDouble(kOfxImageEffectPropFrameRate, false);
                _logger->info("Source Frame Rate: {}", frameRate);
            } catch (...) { _logger->warn("Source Frame Rate: NOT AVAILABLE"); }

            try {
                std::string unmappedComponents = _srcClip->getPropertySet().propGetString(kOfxImageClipPropUnmappedComponents, false);
                _logger->info("Source Unmapped Components: '{}'", unmappedComponents);
            } catch (...) { _logger->warn("Source Unmapped Components: NOT AVAILABLE"); }

            try {
                int optional = _srcClip->getPropertySet().propGetInt(kOfxImageClipPropOptional, false);
                _logger->info("Source Clip Optional: {}", optional ? "YES" : "NO");
            } catch (...) { _logger->warn("Source Clip Optional: NOT AVAILABLE"); }

            try {
                int isMask = _srcClip->getPropertySet().propGetInt(kOfxImageClipPropIsMask, false);
                _logger->info("Source Clip Is Mask: {}", isMask ? "YES" : "NO");
            } catch (...) { _logger->warn("Source Clip Is Mask: NOT AVAILABLE"); }

            try {
                int continuousSamples = _srcClip->getPropertySet().propGetInt(kOfxImageClipPropContinuousSamples, false);
                _logger->info("Source Continuous Samples: {}", continuousSamples ? "YES" : "NO");
            } catch (...) { _logger->warn("Source Continuous Samples: NOT AVAILABLE"); }
        }

        // Host description (used to decide Y-flip for EXR I/O — see shouldFlipYForOFX)
        try {
            OFX::ImageEffectHostDescription* hd = OFX::getImageEffectHostDescription();
            if (hd) {
                const char* originStr = "BottomLeft";
                if (hd->nativeOrigin == OFX::eNativeOriginTopLeft) originStr = "TopLeft";
                else if (hd->nativeOrigin == OFX::eNativeOriginCenter) originStr = "Center";
                _logger->info("Host Name: '{}' | Label: '{}' | nativeOrigin: {} | flipYForEXR: {}",
                              hd->hostName, hd->hostLabel, originStr,
                              shouldFlipYForOFX() ? "YES" : "NO");
            } else {
                _logger->warn("Host description not available");
            }
        } catch (...) { _logger->warn("Host description: NOT AVAILABLE"); }

        _logger->info("=======================================================");
        _logger->info("=== END COMPREHENSIVE OFX ENVIRONMENT DISCOVERY ===");
        _logger->info("=======================================================");
    }

    // Fetch common parameters
    // Sequence plugins don't define this param — _enableProcessing stays nullptr
    try { _enableProcessing = fetchBooleanParam("enableProcessing"); } catch (...) {}
    _serverAddress = fetchStringParam("serverAddress");
    _serverPort = fetchIntParam("serverPort");
    _localMountPath = fetchStringParam("localMountPath");
    _serverMountPath = fetchStringParam("serverMountPath");
    _projectName = fetchStringParam("projectName");
    _workflowName = fetchStringParam("workflowName");
    _outputVersion = fetchStringParam("outputVersion");
    _workflowFilePath = fetchStringParam("workflowFilePath");
    _enableCache = fetchBooleanParam("enableCache");
    _timeout = fetchIntParam("timeout");

    // Fetch async rendering parameters
    _asyncMode = fetchChoiceParam("asyncMode");
    _placeholderMode = fetchChoiceParam("placeholderMode");
    _refreshTrigger = fetchDoubleParam("refreshTrigger");
    _jobStatus = fetchStringParam("jobStatus");
    _jobStatusColor = fetchRGBParam("jobStatusColor");
    try { _flipYMode = fetchChoiceParam("flipYMode"); } catch (...) { _flipYMode = nullptr; }

    // Sequence plugins expose a "Collect & Submit" button; non-sequence plugins don't.
    try { _collectAndSubmit = fetchPushButtonParam("collectAndSubmit"); } catch (...) {}

    if (_logger) {
        _logger->info("Parameter fetch results:");
        _logger->info("  _jobStatus: {}", _jobStatus ? "OK" : "NULL");
        _logger->info("  _jobStatusColor: {}", _jobStatusColor ? "OK" : "NULL");

        if (_jobStatusColor) {
            double r, g, b;
            _jobStatusColor->getValue(r, g, b);
            _logger->info("  Initial jobStatusColor: RGB({}, {}, {})", r, g, b);
        }
    }

    // Initialize project status indicator color based on initial project name state
    if (_projectName) {
        std::string projectName;
        projectName = getTrimmedStringParam(_projectName);

        if (_logger) _logger->info("Initializing project status indicator. Project name is: '{}'", projectName);

        OFX::RGBParam *indicatorParam = fetchRGBParam("projectNameIndicator");
        if (indicatorParam) {
            if (_logger) _logger->info("Indicator parameter fetched successfully");

            if (projectName.empty()) {
                // RED when empty (warning)
                if (_logger) _logger->info("Setting indicator to RED (project is empty)");
                indicatorParam->setValue(1.0, 0.0, 0.0);
                if (_logger) _logger->info("Project indicator initialized: RED (project name is empty)");
            } else {
                // BLACK when set (normal)
                if (_logger) _logger->info("Setting indicator to BLACK (project is: '{}')", projectName);
                indicatorParam->setValue(0.0, 0.0, 0.0);
                if (_logger) _logger->info("Project indicator initialized: BLACK (project name: '{}')", projectName);
            }
        } else {
            if (_logger) _logger->error("FAILED to fetch projectNameIndicator parameter!");
        }
    } else {
        if (_logger) _logger->error("FAILED to fetch projectName parameter!");
    }

    // NOTE: Job manager will be initialized lazily when needed (after _comfyClient is created)
    // We can't create it here because _comfyClient might not exist yet

    if (_logger) _logger->info("BasePlugin constructor completed successfully");
}

BasePlugin::~BasePlugin()
{
    try {
        if (_logger) {
            _logger->info("=== ComfyUI Plugin Session Ended ===");
            _logger->flush();
            _logger.reset();
        }
    } catch (...) {
        // Silently ignore exceptions during destruction
    }
}

void BasePlugin::changedParam(const OFX::InstanceChangedArgs &args,
                              const std::string &paramName)
{
    // Skip logging for frequently-updated status parameters (reduces log noise)
    if (paramName != "jobStatus" && paramName != "jobStatusColor" && paramName != "refreshTrigger") {
        if (_logger) _logger->info("Parameter changed: {}", paramName);
    }

    // Update project status indicator color based on whether project is set
    if (paramName == "projectName") {
        std::string projectName;
        projectName = getTrimmedStringParam(_projectName);

        // Get the color indicator parameter
        OFX::RGBParam *indicatorParam = fetchRGBParam("projectNameIndicator");
        if (indicatorParam) {
            if (projectName.empty()) {
                // RED when empty (warning)
                indicatorParam->setValue(1.0, 0.0, 0.0);
                if (_logger) _logger->info("Project indicator set to RED (project name is empty)");
            } else {
                // BLACK when set (normal)
                indicatorParam->setValue(0.0, 0.0, 0.0);
                if (_logger) _logger->info("Project indicator set to BLACK (project name: '{}')", projectName);
            }
        }
    }

    // Invalidate RoD cache when parameters affecting output path or output
    // dimensions change.
    //   - Path-affecting params (project/workflow/version/mount) move the
    //     cached file to a different on-disk location.
    //   - Dimension-affecting params (e.g. SeedVR2 'resolution',
    //     'maxResolution') keep the path the same but invalidate the cached
    //     width/height — the next render will produce different sized output.
    if (paramName == "projectName" || paramName == "workflowName" ||
        paramName == "outputVersion" || paramName == "workflowFilePath" ||
        paramName == "localMountPath" || paramName == "serverMountPath" ||
        paramName == "resolution"    || paramName == "maxResolution") {
        std::lock_guard<std::mutex> lock(_cacheMutex);
        if (!_cacheDimensions.empty() || !_cacheFileExists.empty()) {
            if (_logger) {
                _logger->info("Parameter '{}' changed - invalidating RoD cache ({} frames, {} paths cached)",
                             paramName, _cacheDimensions.size(), _cacheFileExists.size());
            }
            _cacheDimensions.clear();
            _cacheFileExists.clear();
        }

    }

    // For sequence plugins: cancel the pending job on any non-system parameter change.
    // Unlike per-frame plugins (where each frame is independent), sequence plugins submit
    // one job for all frames — any inference param change (imageLoadCap, windowSize,
    // textPrompt, guidanceScale, etc.) invalidates the current job's output.
    // System params that update independently of user intent are excluded.
    static const std::unordered_set<std::string> kSystemParams = {
        "jobStatus", "jobStatusColor", "refreshTrigger", "asyncMode", "placeholderMode"
    };
    // A genuine output-affecting parameter change invalidates any in-flight job.
    // The "collectAndSubmit" button is an action, not an output parameter, so it
    // must NOT land here: otherwise every press cancels the running job locally
    // (without stopping it on the server), the orphaned job still writes its
    // output, and the next submit collides on it ("File exists already").
    if (isSequencePlugin() && paramName != "collectAndSubmit" &&
        kSystemParams.find(paramName) == kSystemParams.end()) {
        if (!_pendingSequenceOutputPrefix.empty()) {
            if (_logger) _logger->info("Parameter '{}' changed - cancelling in-flight sequence job (startFrame={})", paramName, _sequenceStartFrame);
            // Interrupt on the server too — a local-only cancel leaves the job
            // running, so it would still write its output and block the resubmit.
            if (_jobManager) _jobManager->cancelAllJobs(/*interruptServer=*/true);
            _pendingSequenceOutputPrefix.clear();
        }
    }

    // "Collect & Submit" button: stash the request and bump _refreshTrigger so the
    // host's next render() call performs the per-frame fetchImage loop on the render
    // thread.  Calling fetchImage() directly here deadlocks DaVinci Resolve / Fusion
    // (the host's clipGetImage routes through Fusion::Input::GetSourceTagList which
    // waits on the same UI thread we're currently on).  Deferring to render() costs
    // nothing on hosts that allowed the in-place call (Flame, Nuke), and unblocks
    // Resolve/Fusion.  See executePendingCollect() for the actual work.
    if (paramName == "collectAndSubmit" && isSequencePlugin()) {
        if (_logger) {
            _logger->info("=== Collect & Submit requested ===");
            _logger->flush();
        }

        try {
            std::string project, mountPath;
            project = getTrimmedStringParam(_projectName);
            mountPath = getLocalMountPath();

            if (project.empty()) {
                if (_logger) { _logger->warn("  ABORT: project name is empty"); _logger->flush(); }
                return;
            }
            if (mountPath.empty()) {
                if (_logger) { _logger->warn("  ABORT: no mount path set for this OS — fill in the macOS / Windows / Linux mount path for this machine in the Server tab"); _logger->flush(); }
                return;
            }

            // Resolve frame range here (cheap; doesn't fetch any pixel data).
            OfxRangeD clipRange = _srcClip->getFrameRange();
            int startFrame = static_cast<int>(std::round(clipRange.min));
            int cap        = getImageLoadCap();
            int endFrame   = (cap > 0)
                             ? std::min(static_cast<int>(std::round(clipRange.max)), startFrame + cap - 1)
                             : static_cast<int>(std::round(clipRange.max));

            if (_logger) {
                _logger->info("  Frame range: [{}, {}] ({} frames, cap={})",
                              startFrame, endFrame, endFrame - startFrame + 1, cap);
            }

            // Stash request — will be drained by the next render() call.
            {
                std::lock_guard<std::mutex> lock(_pendingCollectMutex);
                _pendingCollect.active     = true;
                _pendingCollect.startFrame = startFrame;
                _pendingCollect.endFrame   = endFrame;
            }

            // Show interim status so the user has visible feedback before the host renders.
            try { if (_jobStatus)      _jobStatus->setValue("Collect & Submit queued — rendering will start collection"); } catch (...) {}
            try { if (_jobStatusColor) _jobStatusColor->setValue(0.0, 0.7, 1.0); } catch (...) {}

            // Bump _refreshTrigger so the host invalidates its render cache and re-renders
            // the current frame.  That render call will execute the deferred collect.
            if (_refreshTrigger) {
                double cur = 0.0;
                try { _refreshTrigger->getValue(cur); } catch (...) {}
                try { _refreshTrigger->setValue(cur + 1.0); } catch (...) {}
            }

            if (_logger) { _logger->info("  Request stashed; refreshTrigger bumped — waiting for render()"); _logger->flush(); }
        } catch (const std::exception& e) {
            if (_logger) {
                _logger->error("  EXCEPTION in Collect & Submit (queue phase): {}", e.what());
                _logger->flush();
            }
        } catch (...) {
            if (_logger) {
                _logger->error("  UNKNOWN EXCEPTION in Collect & Submit (queue phase)");
                _logger->flush();
            }
        }
        return;
    }

    // Recreate ComfyUI client if server parameters change
    if (paramName == "serverAddress" || paramName == "serverPort") {
        // Get current values
        std::string address;
        _serverAddress->getValue(address);
        int port = _serverPort->getValue();

        if (_logger) _logger->info("Updating ComfyUI server to {}:{}", address, port);

        // Update the server address IN PLACE — do NOT recreate the Client.
        // AsyncJobManager holds a raw (non-owning) Client* captured once at
        // construction; destroying the Client here while a job (or its background
        // submission/polling thread) still references it is a use-after-free —
        // observed as a submit failure with an empty host / garbage port
        // (":-508401456") and std::bad_alloc. setServerAddress() mutates the
        // existing object under its own lock, keeping the pointer stable.
        std::string serverUrl = address + ":" + std::to_string(port);
        if (_comfyClient) {
            _comfyClient->setServerAddress(serverUrl);
        } else {
            _comfyClient.reset(new Client(serverUrl));
        }

        if (_logger) _logger->info("ComfyUI server updated successfully");
    }
}

void BasePlugin::executePendingCollect()
{
    // Atomically claim the pending request so concurrent renders don't double-submit.
    int startFrame = 0, endFrame = 0;
    {
        std::lock_guard<std::mutex> lock(_pendingCollectMutex);
        if (!_pendingCollect.active) return;
        startFrame = _pendingCollect.startFrame;
        endFrame   = _pendingCollect.endFrame;
        _pendingCollect.active = false;
    }

    if (_logger) {
        _logger->info("=== Collect & Submit (deferred) executing on render thread ===");
        _logger->flush();
    }

    try {
        // Lazy-init client + job manager (mirrors the prior changedParam path).
        if (!_comfyClient) {
            std::string address; _serverAddress->getValue(address);
            int port = _serverPort->getValue();
            if (_logger) _logger->info("  Creating ComfyUI client: {}:{}", address, port);
            _comfyClient.reset(new Client(address + ":" + std::to_string(port)));
        }
        if (!_jobManager) {
            _jobManager.reset(new AsyncJobManager(_comfyClient.get(), _logger));
            _jobManager->setCompletionCallback([this](int f, bool ok){ onJobComplete(f, ok); });
            _jobManager->setStatusUpdateCallback([this](){ updateJobStatusDisplay(); });
            _jobManager->setAdaptivePollingIntervals(0.5, 5.0);
            // Wall-clock cap covers the user-visible "Timeout (s)" param —
            // keep the poll-count cap as a final runaway-loop guard (well
            // above 1h at the fast 0.5s cadence).
            _jobManager->setMaxPollAttempts(7200);
            _jobManager->setJobRetentionTime(300);
        }
        // Apply the live timeout value at every submit so user param changes
        // take effect without recreating the AsyncJobManager.
        if (_jobManager && _timeout) {
            _jobManager->setMaxJobDurationSec(_timeout->getValue());
        }

        std::string mountPath, project, workflowName, version, serverAddress;
        int serverPort = 0;
        mountPath = getLocalMountPath();
        project = getTrimmedStringParam(_projectName);
        workflowName = getTrimmedStringParam(_workflowName);
        version = getTrimmedStringParam(_outputVersion);
        _serverAddress->getValue(serverAddress);
        serverPort = _serverPort->getValue();

        if (_logger) {
            _logger->info("  Server:      {}:{}", serverAddress, serverPort);
            _logger->info("  Mount path:  '{}'", mountPath);
            _logger->info("  Project:     '{}'", project);
            _logger->info("  Workflow:    '{}'", workflowName);
            _logger->info("  Version:     '{}'", version);
            _logger->flush();
        }

        // Re-validate (params may have changed between queue and render).
        if (project.empty()) {
            if (_logger) { _logger->warn("  ABORT: project name is empty"); _logger->flush(); }
            return;
        }
        if (mountPath.empty()) {
            if (_logger) { _logger->warn("  ABORT: no mount path set for this OS (macOS/Windows/Linux mount path for this machine is empty in the Server tab)"); _logger->flush(); }
            return;
        }

        std::string outputDir   = mountPath + "/out/" + project + "/" + workflowName + "/" + version;
        std::string inputFolder = constructInputFolderPath();
        std::string base        = getEffectiveBasename();

        if (_logger) {
            _logger->info("  Frame range: [{}, {}] ({} frames)", startFrame, endFrame, endFrame - startFrame + 1);
            _logger->info("  Input folder: '{}'", inputFolder);
            _logger->info("  Output dir:   '{}'", outputDir);
            _logger->info("  Basename:     '{}'", base);
            _logger->flush();
        }

        ImageIO::createDirectoryRecursive(outputDir);
        ImageIO::createDirectoryRecursive(inputFolder);

        std::map<int, ImageData> frameData;
        int nTotal = endFrame - startFrame + 1;
        int cached = 0, fetched = 0, failed = 0;
        if (_logger) _logger->info("  Starting frame collection ({} frames)...", nTotal);

        auto setStatus = [&](const std::string& text, double sr, double sg, double sb) {
            try { if (_jobStatus)      _jobStatus->setValue(text); }      catch (...) {}
            try { if (_jobStatusColor) _jobStatusColor->setValue(sr, sg, sb); } catch (...) {}
        };

        // In-flight de-duplication. The disk cache check below only sees outputs
        // already written, not a job still running (a sequence job can take many
        // minutes). Without this guard, a second Collect & Submit during that
        // window queues a duplicate that SaveEXR later aborts with "File exists
        // already" once the first job writes its frames. If a job for this exact
        // output is already queued/processing, ignore the press.
        {
            std::string outputPrefix = outputDir + "/" + base;
            if (_jobManager && _pendingSequenceOutputPrefix == outputPrefix) {
                JobStatus st = _jobManager->getJobStatus(_sequenceStartFrame);
                if (st == JobStatus::QUEUED || st == JobStatus::PROCESSING) {
                    if (_logger) {
                        _logger->info("  Sequence job already in flight for '{}' (startFrame={}) — ignoring duplicate Collect & Submit",
                                      outputPrefix, _sequenceStartFrame);
                        _logger->flush();
                    }
                    setStatus("Already processing this shot — duplicate submission ignored", 1.0, 0.55, 0.0);
                    return;
                }
            }
        }

        setStatus("Collecting: 0 / " + std::to_string(nTotal), 0.0, 0.7, 1.0);

        for (int t = startFrame; t <= endFrame; ++t) {
            std::ostringstream ss;
            ss << inputFolder << "/" << base << "."
               << std::setfill('0') << std::setw(4) << t << ".exr";
            if (std::filesystem::exists(ss.str())) {
                ++cached;
                setStatus("Collecting: " + std::to_string(fetched + cached) + " / " + std::to_string(nTotal) + " (cached)",
                          0.0, 0.7, 1.0);
                continue;
            }
            if (_logger && (t == startFrame || t % 10 == 0)) {
                _logger->info("  Fetching frame {} / {}...", t, endFrame);
                _logger->flush();
            }
            setStatus("Collecting: " + std::to_string(fetched + cached) + " / " + std::to_string(nTotal),
                      0.0, 0.7, 1.0);
            std::unique_ptr<OFX::Image> img(_srcClip->fetchImage(static_cast<double>(t)));
            if (!img) {
                if (_logger) _logger->warn("  fetchImage({}) returned null — skipping", t);
                ++failed;
                continue;
            }
            OfxRectI bounds = img->getBounds();
            int w = bounds.x2 - bounds.x1, h = bounds.y2 - bounds.y1;
            int rb = img->getRowBytes(), nc = img->getPixelComponentCount();
            int bd = 32;
            OFX::BitDepthEnum bde = img->getPixelDepth();
            if (bde == OFX::eBitDepthUByte)  bd = 8;
            else if (bde == OFX::eBitDepthUShort) bd = 16;
            frameData[t] = ImageIO::fromOFXBuffer(img->getPixelData(), w, h, rb, nc, bd, shouldFlipYForOFX());
            img.reset();
            ++fetched;
        }

        if (_logger) {
            _logger->info("  Collection done: {} fetched, {} cached on disk, {} failed",
                          fetched, cached, failed);
            _logger->flush();
        }
        setStatus("Writing " + std::to_string(nTotal) + " frames to disk...", 1.0, 0.55, 0.0);

        if (frameData.empty() && cached == 0) {
            if (_logger) { _logger->warn("  ABORT: no frames collected and none cached"); _logger->flush(); }
            return;
        }

        {
            std::lock_guard<std::mutex> lock(_pendingFramesMutex);
            _pendingFrames.clear();
            _pendingCollectionKey.clear();
        }

        std::string outputPrefix = mountPath + "/out/" + project + "/" + workflowName + "/" + version + "/" + base;
        std::map<std::string, std::string> inputPaths = {{"InputA", inputFolder}};
        std::string firstOutputPath = constructExpectedOutputPath(startFrame);

        if (_logger) {
            _logger->info("  Output prefix:     '{}'", outputPrefix);
            _logger->info("  First output path: '{}'", firstOutputPath);
            _logger->flush();
        }

        // Output cache check.
        //   Cache ON  : if every output frame already exists, serve it and skip
        //               submission entirely.
        //   Cache OFF : never reuse — fall through and delete any existing frames
        //               first, because the SaveEXR node refuses to overwrite and
        //               would abort the whole job with "File exists already".
        {
            const bool useCache = !_enableCache || _enableCache->getValue();
            int nFrames = endFrame - startFrame + 1;
            int existingCount = 0;
            for (int t = startFrame; t <= endFrame; ++t) {
                std::ostringstream ss;
                ss << outputDir << "/" << base << "."
                   << std::setfill('0') << std::setw(4) << t << ".exr";
                if (std::filesystem::exists(ss.str())) ++existingCount;
            }

            if (useCache && existingCount == nFrames) {
                if (_logger) {
                    _logger->info("  All {} output frame(s) cached on disk — skipping submission", nFrames);
                    _logger->flush();
                }
                return;
            }

            // Either the cache is disabled, or only some frames exist. In both
            // cases we are about to (re)submit, so clear any existing outputs in
            // range to keep SaveEXR from aborting on a pre-existing file.
            if (existingCount > 0) {
                int deleted = 0;
                for (int t = startFrame; t <= endFrame; ++t) {
                    std::ostringstream ss;
                    ss << outputDir << "/" << base << "."
                       << std::setfill('0') << std::setw(4) << t << ".exr";
                    try {
                        if (std::filesystem::remove(ss.str())) ++deleted;
                    } catch (const std::exception& e) {
                        if (_logger) _logger->warn("  Could not delete '{}': {}", ss.str(), e.what());
                    }
                }
                if (_logger) {
                    _logger->info("  Removed {} stale output EXR(s) before re-submission", deleted);
                    _logger->flush();
                }
            }
        }

        _sequenceStartFrame = startFrame;
        _sequenceEndFrame   = endFrame;
        _jobManager->submitSequenceJobAsync(startFrame, std::move(frameData),
                                            inputFolder, base,
                                            inputPaths, firstOutputPath, this);
        _pendingSequenceOutputPrefix = outputPrefix;

        if (_logger) {
            _logger->info("  Job submitted (startFrame={}, endFrame={})", startFrame, endFrame);
            _logger->flush();
        }
    } catch (const std::exception& e) {
        if (_logger) {
            _logger->error("  EXCEPTION in deferred Collect & Submit: {}", e.what());
            _logger->flush();
        }
    } catch (...) {
        if (_logger) {
            _logger->error("  UNKNOWN EXCEPTION in deferred Collect & Submit");
            _logger->flush();
        }
    }
}

void BasePlugin::render(const OFX::RenderArguments &args)
{
    int frame = static_cast<int>(args.time);

    if (_logger) {
        _logger->info("========================================");
        _logger->info("RENDER STARTED - Frame: {}", frame);
        _logger->info("Render window: ({},{}) to ({},{})",
                     args.renderWindow.x1, args.renderWindow.y1,
                     args.renderWindow.x2, args.renderWindow.y2);
        _logger->info("Render scale: {}", args.renderScale.x);
        _logger->info("Interactive: {}, Draft: {}, Sequential: {}",
                     args.interactiveRenderStatus,
                     args.renderQualityDraft,
                     args.sequentialRenderStatus);

        // Log runtime clip information
        if (_srcClip) {
            _logger->info("--- Runtime Source Clip Info ---");
            _logger->info("Source Clip Connected: {}", _srcClip->isConnected() ? "YES" : "NO");
            if (_srcClip->isConnected()) {
                try {
                    std::string clipName = _srcClip->getPropertySet().propGetString(kOfxPropName, false);
                    _logger->info("Source Clip Name: '{}'", clipName);
                } catch (...) {}
                try {
                    std::string clipLabel = _srcClip->getPropertySet().propGetString(kOfxPropLabel, false);
                    _logger->info("Source Clip Label: '{}'", clipLabel);
                } catch (...) {}
                try {
                    std::string components = _srcClip->getPixelComponentsProperty();
                    _logger->info("Source Clip Components: {}", components);
                } catch (...) {}
            }
        }

        _logger->info("========================================");
    }

    std::lock_guard<std::mutex> lock(_renderMutex);  // Thread-safe for concurrent renders

    // CRITICAL FIX: Detect proxy/preview renders (Resolve thumbnails, scrubbing, etc.)
    // For proxy renders, use fast passthrough instead of full ComfyUI workflow
    // This prevents crashes from NULL buffers and provides instant feedback
    bool isProxyRender = (args.renderScale.x < 1.0 || args.renderScale.y < 1.0);

    if (isProxyRender) {
        if (_logger) {
            _logger->info("=== PROXY/PREVIEW RENDER DETECTED ===");
            _logger->info("Render scale: {}x{} (< 1.0 = proxy mode)",
                         args.renderScale.x, args.renderScale.y);
            _logger->info("Using fast passthrough (no ComfyUI processing)");
            _logger->info("This is expected for thumbnails, timeline scrubbing, and draft previews");
        }

        renderPassthrough(args);

        if (_logger) {
            _logger->info("Proxy render completed successfully");
        }
        return;
    }

    if (_logger) {
        _logger->info("Full resolution render (scale: {}x{}) - will execute ComfyUI workflow",
                     args.renderScale.x, args.renderScale.y);
    }

    // Drain any deferred Collect & Submit before running the per-frame render path.
    // No-op unless changedParam(collectAndSubmit) stashed a request.  Sequence plugins only.
    // We're on the render thread now, so fetchImage() at arbitrary times is legal on every host.
    if (isSequencePlugin()) {
        executePendingCollect();
    }

    // Check if processing is enabled - if not, just passthrough.
    // Sequence plugins don't have the enable param at all (_enableProcessing == nullptr):
    // their submit action is the "Collect & Submit" button, not this toggle.
    bool processingEnabled = !_enableProcessing || _enableProcessing->getValue();
    if (!processingEnabled) {
        if (_logger) _logger->info("ComfyUI processing DISABLED - passthrough mode");

        // Check if source is connected
        if (_srcClip && _srcClip->isConnected()) {
            std::unique_ptr<OFX::Image> src(_srcClip->fetchImage(args.time));
            std::unique_ptr<OFX::Image> dst(_dstClip->fetchImage(args.time));

            if (src.get() && dst.get()) {
                copyPixelData(src.get(), dst.get());
            }
        }
        return;
    }

    if (_logger) _logger->info("ComfyUI processing ENABLED");

    // Always use non-blocking mode (asyncModeValue = 1) - parameter is hidden
    int asyncModeValue = 1;
    // _asyncMode->getValue(asyncModeValue);  // No longer reading from parameter

    if (asyncModeValue == 0) {
        // BLOCKING MODE - traditional behavior
        if (_logger) _logger->info("Rendering in BLOCKING mode");
        renderBlocking(args);
    } else {
        // NON-BLOCKING MODE - async rendering with placeholder
        if (_logger) _logger->info("Rendering in NON-BLOCKING mode");
        renderAsync(args);
    }
}

bool BasePlugin::getRegionOfDefinition(const OFX::RegionOfDefinitionArguments &args,
                                      OfxRectD &rod)
{
    int frame = static_cast<int>(args.time);

    // IMPORTANT: Always construct the expected output path for THIS instance
    // This ensures we're checking the correct file when switching between instances
    std::string expectedOutputPath = constructExpectedOutputPath(frame);

    // Check if we have cached dimensions AND verify the cached file still exists
    // This prevents stale cache from wrong instance/workflow
    {
        std::lock_guard<std::mutex> lock(_cacheMutex);
        auto it = _cacheDimensions.find(frame);
        if (it != _cacheDimensions.end()) {
            // Validate that the cached output file still exists and matches our expected path
            bool cacheValid = _cacheFileExists.count(expectedOutputPath) > 0;

            // Also verify file actually exists on disk (not just in memory cache)
            if (cacheValid) {
                try {
                    cacheValid = std::filesystem::exists(expectedOutputPath);
                } catch (...) {
                    cacheValid = false;
                }
            }

            if (cacheValid) {
                // Use cached output dimensions
                int width = it->second.first;
                int height = it->second.second;
                rod.x1 = 0;
                rod.y1 = 0;
                rod.x2 = width;
                rod.y2 = height;

                if (_logger) {
                    _logger->debug("Frame {}: RoD from validated cache: {}x{}", frame, width, height);
                }
                return true;
            } else {
                // Cache is stale (file was deleted or we switched instances)
                if (_logger) {
                    _logger->debug("Frame {}: Cached dimensions invalid, re-checking output file", frame);
                }
                // Clear stale cache entry
                _cacheDimensions.erase(frame);
            }
        }
    }

    // No valid cached dimensions - check if output file exists
    std::string cachedPath = expectedOutputPath;
    bool fileExists = false;

    {
        std::lock_guard<std::mutex> lock(_cacheMutex);
        fileExists = _cacheFileExists.count(cachedPath) > 0;
    }

    if (!fileExists) {
        // Quick check if file exists on disk
        try {
            fileExists = std::filesystem::exists(cachedPath);
        } catch (...) {
            fileExists = false;
        }
    }

    if (fileExists) {
        // Output file exists - read its dimensions
        try {
            ImageData outputData = ImageIO::readEXR(cachedPath);

            // Cache the dimensions
            {
                std::lock_guard<std::mutex> lock(_cacheMutex);
                _cacheDimensions[frame] = {outputData.width, outputData.height};
                _cacheFileExists.insert(cachedPath);
            }

            // Return RoD based on actual output dimensions
            rod.x1 = 0;
            rod.y1 = 0;
            rod.x2 = outputData.width;
            rod.y2 = outputData.height;

            if (_logger) {
                _logger->debug("Frame {}: RoD from output file: {}x{}", frame, outputData.width, outputData.height);
            }
            return true;

        } catch (const std::exception& e) {
            if (_logger) {
                _logger->debug("Frame {}: Could not read output dimensions: {}", frame, e.what());
            }
            // Fall through to source RoD
        }
    }

    // No output yet - use source RoD as default, with optional predicted
    // scale applied so resolution-changing plugins (e.g. SeedVR2 upscaler)
    // can report the correct downstream canvas size before any frame has
    // been rendered to disk.
    if (_srcClip && _srcClip->isConnected()) {
        rod = _srcClip->getRegionOfDefinition(args.time);

        OfxPointD scale = getPredictedOutputScale(args.time);
        const bool scaled = (scale.x != 1.0 || scale.y != 1.0)
                            && scale.x > 0.0 && scale.y > 0.0;
        if (scaled) {
            const double w = rod.x2 - rod.x1;
            const double h = rod.y2 - rod.y1;
            rod.x2 = rod.x1 + w * scale.x;
            rod.y2 = rod.y1 + h * scale.y;
        }

        if (_logger) {
            if (scaled) {
                _logger->debug("Frame {}: RoD from source × predicted scale ({:.3f},{:.3f}): {}x{}",
                              frame, scale.x, scale.y,
                              static_cast<int>(rod.x2 - rod.x1),
                              static_cast<int>(rod.y2 - rod.y1));
            } else {
                _logger->debug("Frame {}: RoD from source: {}x{}",
                              frame,
                              static_cast<int>(rod.x2 - rod.x1),
                              static_cast<int>(rod.y2 - rod.y1));
            }
        }
        return true;
    }

    // Generator workflow (no source connected) - use project size as default
    // This allows generator workflows to run without a source clip
    try {
        double projectW = getPropertySet().propGetDouble(kOfxImageEffectPropProjectSize, 0, false);
        double projectH = getPropertySet().propGetDouble(kOfxImageEffectPropProjectSize, 1, false);

        if (projectW > 0 && projectH > 0) {
            rod.x1 = 0;
            rod.y1 = 0;
            rod.x2 = projectW;
            rod.y2 = projectH;

            if (_logger) {
                _logger->info("Frame {}: Generator workflow - RoD from project size: {}x{}",
                             frame, static_cast<int>(projectW), static_cast<int>(projectH));
            }
            return true;
        }
    } catch (...) {
        // Project size not available
    }

    // Last resort: return a reasonable default size (1920x1080 HD)
    rod.x1 = 0;
    rod.y1 = 0;
    rod.x2 = 1920;
    rod.y2 = 1080;

    if (_logger) {
        _logger->warn("Frame {}: Generator workflow - using default RoD 1920x1080", frame);
    }
    return true;
}

void BasePlugin::executeWorkflow(const OFX::RenderArguments &args)
{
    if (_logger) _logger->info("Starting workflow execution");

    // Log all parameters
    std::string address, mountPath, serverMount, project, workflowName, version;
    _serverAddress->getValue(address);
    int port = _serverPort->getValue();
    mountPath = getLocalMountPath();
    serverMount = getTrimmedStringParam(_serverMountPath);
    project = getTrimmedStringParam(_projectName);
    workflowName = getTrimmedStringParam(_workflowName);
    version = getTrimmedStringParam(_outputVersion);

    // Get auto-generated basename
    std::string basename = getEffectiveBasename();

    if (_logger) {
        _logger->info("Parameters:");
        _logger->info("  Server: {}:{}", address, port);
        _logger->info("  Client Mount: {}", mountPath);
        _logger->info("  Server Mount: {}", serverMount);
        _logger->info("  Project: {}", project);
        _logger->info("  Workflow: {}", workflowName);
        _logger->info("  Basename: {} (auto-generated)", basename);
        _logger->info("  Version: {}", version);
    }

    // Initialize ComfyUI client if needed
    if (!_comfyClient) {
        if (_logger) _logger->info("Creating new ComfyUI client");
        std::string serverUrl = address + ":" + std::to_string(port);
        _comfyClient.reset(new Client(serverUrl));
        if (_logger) _logger->info("ComfyUI client created successfully");
    }

    // Get current frame number
    int frame = static_cast<int>(args.time);
    if (_logger) _logger->info("Processing frame: {}", frame);

    // Step 1: Write input images to shared storage (if any connected)
    if (_logger) _logger->info("Step 1: Writing input image(s) to shared storage");
    progressUpdate(0.1);

    // Write all connected input clips to EXR files
    // Returns map: "InputA" -> path, "InputB" -> path, "InputC" -> path
    // For generator workflows (0 inputs), this map will be empty
    std::map<std::string, std::string> inputPaths = writeInputImages(frame);

    if (inputPaths.empty()) {
        // No inputs connected - this is valid for generator workflows
        if (_logger) _logger->info("No input clips connected - running as generator workflow (0 inputs)");
    } else {
        if (_logger) {
            _logger->info("Input images written: {} file(s)", inputPaths.size());
            for (const auto& [inputId, path] : inputPaths) {
                _logger->info("  {} -> {}", inputId, path);
            }
        }
    }

    // Step 2: Check if output already exists (avoid overwrite error)
    if (_logger) _logger->info("Step 2: Checking if output file already exists");
    progressUpdate(0.15);
    std::string expectedOutputPath = constructExpectedOutputPath(frame);
    if (_logger) _logger->info("Expected output path: {}", expectedOutputPath);

    // Cache ON: reuse an existing output. Cache OFF: never reuse — the existing
    // file is removed below so the SaveEXR node won't abort on "File exists".
    const bool useCache = !_enableCache || _enableCache->getValue();
    std::ifstream cachedFile(expectedOutputPath);
    if (cachedFile.good() && useCache) {
        cachedFile.close();
        if (_logger) _logger->info("✓ Output file already exists (cached): {}", expectedOutputPath);
        if (_logger) _logger->info("Skipping workflow submission, using cached result");

        // Read cached output directly
        progressUpdate(0.9);
        std::unique_ptr<OFX::Image> dst(_dstClip->fetchImage(args.time));
        if (!dst.get()) {
            if (_logger) _logger->error("Failed to fetch destination image");
            throw std::runtime_error("Failed to fetch destination image");
        }

        ImageData outputImageData = ImageIO::readEXR(expectedOutputPath);

        // Convert back to OFX buffer
        progressUpdate(0.95);
        if (_logger) _logger->info("Converting cached image to OFX buffer");

        int dstPixelComponents = dst->getPixelComponentCount();
        OFX::BitDepthEnum dstBitDepth = dst->getPixelDepth();
        int dstBitDepthInt = 32;  // default to float
        if (dstBitDepth == OFX::eBitDepthUByte) dstBitDepthInt = 8;
        else if (dstBitDepth == OFX::eBitDepthUShort) dstBitDepthInt = 16;

        // Critical: Validate pixel data pointer before use
        void* dstPixelData = dst->getPixelData();
        if (!dstPixelData) {
            if (_logger) _logger->error("CRITICAL: Destination pixel data pointer is NULL (cached path)!");
            throw std::runtime_error("Failed to allocate destination image buffer for cached result");
        }

        ImageIO::toOFXBuffer(outputImageData, dstPixelData, dst->getRowBytes(), dstPixelComponents, dstBitDepthInt, shouldFlipYForOFX());
        if (_logger) _logger->info("Cached result loaded successfully");

        progressUpdate(1.0);
        return;
    }

    // Cache disabled but an old output is present: delete it so SaveEXR can
    // rewrite the frame instead of aborting with "File exists already".
    if (!useCache) {
        std::error_code ec;
        if (std::filesystem::remove(expectedOutputPath, ec) && _logger) {
            _logger->info("Cache disabled — removed stale output before re-submission: {}", expectedOutputPath);
        }
    }

    if (_logger) _logger->info("Output file does not exist, proceeding with workflow submission");

    // Step 3: Build workflow (implemented by derived class)
    if (_logger) _logger->info("Step 3: Building workflow JSON");
    progressUpdate(0.2);
    json workflow = buildWorkflow(frame, inputPaths);
    if (_logger) {
        _logger->info("Workflow JSON built: {} characters", workflow.dump().length());
        _logger->info("Workflow JSON content:\n{}", workflow.dump(2));  // Pretty print with 2-space indent
    }

    // Step 4: Queue workflow to ComfyUI
    if (_logger) _logger->info("Step 4: Queueing workflow to ComfyUI server");
    progressUpdate(0.25);
    std::string promptId = _comfyClient->queuePrompt(workflow, "");
    if (_logger) _logger->info("Workflow queued with prompt ID: {}", promptId);

    // Step 5: Wait for execution (using polling instead of WebSocket to avoid crashes)
    if (_logger) _logger->info("Step 5: Waiting for workflow execution (polling mode)");
    progressUpdate(0.3);

    // Poll for completion instead of using WebSocket
    const int maxAttempts = 300; // 5 minutes at 1 second intervals
    bool completed = false;
    std::string errorMsg;

    for (int attempt = 0; attempt < maxAttempts; ++attempt) {
        try {
            // Check history to see if workflow is done
            json history = _comfyClient->getHistory(promptId);

            // Log what we got back
            if (_logger && attempt == 0) {
                if (history.empty()) {
                    _logger->info("History response is empty");
                } else if (history.is_object()) {
                    _logger->info("History response has {} keys", history.size());
                    if (history.contains(promptId)) {
                        _logger->info("Found promptId {} in history!", promptId);
                    } else {
                        // Log what keys ARE present
                        std::vector<std::string> keys;
                        for (auto& el : history.items()) {
                            keys.push_back(el.key());
                            if (keys.size() >= 3) break;
                        }
                        if (!keys.empty()) {
                            _logger->info("History contains other prompts: {}", nlohmann::json(keys).dump());
                        }
                    }
                } else {
                    _logger->warn("History response is not an object: {}", history.dump().substr(0, 200));
                }
            }

            // When workflow is in history, it means it's done (either cached or executed)
            if (history.contains(promptId)) {
                auto& promptData = history[promptId];

                if (_logger) {
                    _logger->info("✓ Prompt {} found in history on attempt {}", promptId, attempt);
                    // Log the full prompt data to see structure
                    _logger->info("Full prompt data:\n{}", promptData.dump(2));
                }

                // Check status first
                if (promptData.contains("status")) {
                    auto& status = promptData["status"];

                    // Check status_str
                    if (status.contains("status_str")) {
                        std::string statusStr = status["status_str"].get<std::string>();
                        if (_logger) _logger->info("Status: {}", statusStr);

                        if (statusStr == "success") {
                            if (_logger) _logger->info("Workflow completed successfully");
                            completed = true;
                            break;
                        } else if (statusStr == "error") {
                            errorMsg = "Workflow execution failed";
                            if (status.contains("messages")) {
                                errorMsg += ": " + status["messages"].dump();
                            }
                            if (_logger) _logger->error(errorMsg);
                            break;
                        }
                    }

                    // Check completed flag
                    if (status.contains("completed") && status["completed"].get<bool>()) {
                        if (_logger) _logger->info("Workflow marked as completed");
                        completed = true;
                        break;
                    }
                }

                // If we have outputs, workflow is done (even if cached)
                if (promptData.contains("outputs") && !promptData["outputs"].is_null()) {
                    // Count number of output nodes
                    int outputCount = promptData["outputs"].size();
                    if (_logger) _logger->info("Workflow has {} output nodes - marking as completed", outputCount);

                    if (outputCount > 0) {
                        completed = true;
                        break;
                    }
                }
            }

            // Update progress periodically
            if (attempt % 10 == 0) {
                double progress = 0.3 + (0.5 * static_cast<double>(attempt) / maxAttempts);
                progressUpdate(progress);
                if (_logger && attempt > 0) {
                    _logger->info("Waiting for completion... ({}/{})", attempt, maxAttempts);
                }
            }

            // Sleep for 1 second
            std::this_thread::sleep_for(std::chrono::seconds(1));

        } catch (const std::exception& ex) {
            if (_logger) _logger->warn("Error polling history: {}", ex.what());
            // Continue polling
        }
    }

    if (!completed) {
        errorMsg = "Workflow execution timed out after " + std::to_string(maxAttempts) + " seconds";
        if (_logger) _logger->error(errorMsg);
    }

    if (!completed || !errorMsg.empty()) {
        throw std::runtime_error("ComfyUI execution failed: " + errorMsg);
    }

    // Step 6: Get history to find output files
    if (_logger) _logger->info("Step 6: Retrieving execution history from ComfyUI");
    progressUpdate(0.85);
    json history = _comfyClient->getHistory(promptId);
    if (_logger) _logger->info("History retrieved: {} characters", history.dump().length());

    // Step 7: Parse output path
    if (_logger) _logger->info("Step 7: Parsing output path from history");
    std::string outputPath = parseOutputPath(history, frame);

    if (outputPath.empty()) {
        if (_logger) _logger->error("Failed to find output file in ComfyUI history");
        throw std::runtime_error("Failed to find output file in ComfyUI history");
    }
    if (_logger) _logger->info("Output path found: {}", outputPath);

    // Step 8: Load result and copy to output buffer
    if (_logger) _logger->info("Step 8: Loading result image and copying to output buffer");
    progressUpdate(0.9);
    std::unique_ptr<OFX::Image> dst(_dstClip->fetchImage(args.time));
    if (!dst.get()) {
        if (_logger) _logger->error("Failed to fetch destination image");
        throw std::runtime_error("Failed to fetch destination image");
    }
    if (_logger) _logger->info("Destination image buffer allocated");

    if (_logger) _logger->info("Reading EXR file: {}", outputPath);
    ImageData resultImage = ImageIO::readEXR(outputPath);
    if (_logger) _logger->info("EXR file read: {}x{} pixels, {} channels",
                               resultImage.width, resultImage.height, resultImage.channels);

    // Copy to output buffer
    progressUpdate(0.95);
    OfxRectI dstBounds = dst->getBounds();
    int dstWidth = dstBounds.x2 - dstBounds.x1;
    int dstHeight = dstBounds.y2 - dstBounds.y1;
    int dstRowBytes = dst->getRowBytes();
    int dstPixelComponents = dst->getPixelComponentCount();
    OFX::BitDepthEnum dstBitDepth = dst->getPixelDepth();

    int dstBitDepthInt = 32;
    if (dstBitDepth == OFX::eBitDepthUByte) dstBitDepthInt = 8;
    else if (dstBitDepth == OFX::eBitDepthUShort) dstBitDepthInt = 16;

    if (_logger) _logger->info("Destination buffer: {}x{}, {} components, {} bits",
                               dstWidth, dstHeight, dstPixelComponents, dstBitDepthInt);

    if (resultImage.width != dstWidth || resultImage.height != dstHeight) {
        if (_logger) _logger->error("Output image size mismatch: expected {}x{}, got {}x{}",
                                   dstWidth, dstHeight, resultImage.width, resultImage.height);
        throw std::runtime_error("Output image size mismatch: expected " +
                                 std::to_string(dstWidth) + "x" + std::to_string(dstHeight) +
                                 ", got " + std::to_string(resultImage.width) + "x" +
                                 std::to_string(resultImage.height));
    }

    // Critical: Validate pixel data pointer before use (prevents SIGSEGV crash)
    void* dstPixelData = dst->getPixelData();
    if (!dstPixelData) {
        if (_logger) _logger->error("CRITICAL: Destination pixel data pointer is NULL!");
        if (_logger) _logger->error("This may indicate buffer allocation failure or unsupported format");
        if (_logger) _logger->error("Buffer details: {}x{}, {} components, {} bits, {} bytes/row",
                                   dstWidth, dstHeight, dstPixelComponents, dstBitDepthInt, dstRowBytes);
        throw std::runtime_error("Failed to allocate destination image buffer. "
                                "This may be caused by unsupported image format, resolution, or insufficient memory. "
                                "Check DaVinci Resolve project settings and available system memory.");
    }

    // Additional validation: Check buffer size is reasonable
    size_t expectedBufferSize = static_cast<size_t>(dstHeight) * dstRowBytes;
    size_t requiredSize = static_cast<size_t>(resultImage.width) * resultImage.height *
                         dstPixelComponents * (dstBitDepthInt / 8);

    if (_logger) _logger->info("Buffer validation: expected {} bytes, required {} bytes",
                               expectedBufferSize, requiredSize);

    if (expectedBufferSize < requiredSize) {
        if (_logger) _logger->error("Buffer size mismatch: expected {} bytes, but need {} bytes",
                                   expectedBufferSize, requiredSize);
        throw std::runtime_error("Destination buffer is too small for image data");
    }

    if (_logger) _logger->info("Copying image data to OFX buffer (pointer: {})", dstPixelData);
    ImageIO::toOFXBuffer(resultImage, dstPixelData,
                        dstRowBytes, dstPixelComponents, dstBitDepthInt, shouldFlipYForOFX());

    if (_logger) _logger->info("Image data copied successfully");
    progressUpdate(1.0);
}

void BasePlugin::renderPassthrough(const OFX::RenderArguments &args)
{
    // Fast passthrough for proxy/preview renders
    // This provides instant visual feedback without executing the full ComfyUI workflow

    if (_logger) {
        _logger->info("renderPassthrough: Fetching source and destination images");
    }

    // Fetch source and destination images
    std::unique_ptr<OFX::Image> src(_srcClip->fetchImage(args.time));
    std::unique_ptr<OFX::Image> dst(_dstClip->fetchImage(args.time));

    if (!src.get()) {
        if (_logger) _logger->error("renderPassthrough: Failed to fetch source image");
        return;  // Graceful failure for previews
    }

    if (!dst.get()) {
        if (_logger) _logger->error("renderPassthrough: Failed to fetch destination image");
        return;  // Graceful failure for previews
    }

    // Critical: Validate destination pixel data (prevents Resolve crash)
    void* dstPixels = dst->getPixelData();
    if (!dstPixels) {
        if (_logger) {
            _logger->warn("renderPassthrough: Destination buffer is NULL");
            _logger->warn("This is acceptable for proxy renders - skipping frame");
        }
        return;  // Graceful failure - no crash
    }

    if (_logger) {
        _logger->info("renderPassthrough: Copying source to destination");
    }

    // Use existing copyPixelData function
    copyPixelData(src.get(), dst.get());

    if (_logger) {
        _logger->info("renderPassthrough: Completed successfully");
    }
}

void BasePlugin::copyPixelData(const OFX::Image* src, OFX::Image* dst)
{
    // Simple passthrough copy for thumbnails/previews
    if (!src || !dst) {
        if (_logger) _logger->warn("copyPixelData: src or dst is null");
        return;
    }

    // Get source and destination info
    OfxRectI srcBounds = src->getBounds();
    OfxRectI dstBounds = dst->getBounds();

    int srcWidth = srcBounds.x2 - srcBounds.x1;
    int srcHeight = srcBounds.y2 - srcBounds.y1;
    int dstWidth = dstBounds.x2 - dstBounds.x1;
    int dstHeight = dstBounds.y2 - dstBounds.y1;

    // Check if sizes match
    if (srcWidth != dstWidth || srcHeight != dstHeight) {
        if (_logger) _logger->warn("copyPixelData: size mismatch {}x{} vs {}x{}",
                                   srcWidth, srcHeight, dstWidth, dstHeight);
        return;
    }

    int srcRowBytes = src->getRowBytes();
    int dstRowBytes = dst->getRowBytes();
    const void* srcData = src->getPixelData();
    void* dstData = dst->getPixelData();

    if (!srcData || !dstData) {
        if (_logger) _logger->warn("copyPixelData: pixel data is null");
        return;
    }

    // Simple row-by-row copy
    const uint8_t* srcPtr = static_cast<const uint8_t*>(srcData);
    uint8_t* dstPtr = static_cast<uint8_t*>(dstData);

    int bytesPerRow = std::min(srcRowBytes, dstRowBytes);

    for (int y = 0; y < srcHeight; ++y) {
        std::memcpy(dstPtr + y * dstRowBytes,
                   srcPtr + y * srcRowBytes,
                   bytesPerRow);
    }
}

std::string BasePlugin::writeInputImage(OFX::Image* img, int frame, const std::string& suffix)
{
    if (!img) {
        if (_logger) _logger->error("No input image to write");
        throw std::runtime_error("No input image to write");
    }

    // Get path components
    std::string mountPath, project, workflow, version;
    mountPath = getLocalMountPath();
    project = getTrimmedStringParam(_projectName);
    workflow = getTrimmedStringParam(_workflowName);
    version = getTrimmedStringParam(_outputVersion);

    // Get effective basename (auto-generated or manual)
    std::string basename = getEffectiveBasename();

    // Build path matching output pattern: basename[_suffix].frame.exr
    // Input and output now use the same naming convention for consistency
    // Example: /Volumes/comfyui-share/in/projects/acme_spot/segmentation/v001/shot01.0001.exr
    // With suffix: /Volumes/comfyui-share/in/projects/acme_spot/segmentation/v001/shot01_B.0001.exr
    std::ostringstream filename;
    filename << mountPath << "/in/projects/"
             << project << "/"
             << workflow << "/"
             << version << "/"
             << basename << suffix << "."
             << std::setw(4) << std::setfill('0') << frame << ".exr";

    if (_logger) _logger->info("Writing input image to: {}", filename.str());

    // Convert OFX image to ImageData
    OfxRectI bounds = img->getBounds();
    int width = bounds.x2 - bounds.x1;
    int height = bounds.y2 - bounds.y1;
    int rowBytes = img->getRowBytes();
    int pixelComponents = img->getPixelComponentCount();
    OFX::BitDepthEnum bitDepth = img->getPixelDepth();

    // Convert bit depth enum to integer
    int bitDepthInt = 32; // default to float
    if (bitDepth == OFX::eBitDepthUByte) {
        bitDepthInt = 8;
    } else if (bitDepth == OFX::eBitDepthUShort) {
        bitDepthInt = 16;
    }

    if (_logger) _logger->info("Input image specs: {}x{}, {} components, {} bits",
                               width, height, pixelComponents, bitDepthInt);

    // Convert OFX buffer to ImageData
    if (_logger) _logger->info("Converting OFX buffer to ImageData");

    const void* pixelData = img->getPixelData();

    ImageData imageData = ImageIO::fromOFXBuffer(
        pixelData,
        width,
        height,
        rowBytes,
        pixelComponents,
        bitDepthInt,
        shouldFlipYForOFX()
    );

    // Log first pixel after safe conversion
    if (_logger && imageData.pixels.size() >= 4) {
        _logger->info("First pixel after conversion (RGBA): [{:.4f}, {:.4f}, {:.4f}, {:.4f}]",
                     imageData.pixels[0], imageData.pixels[1], imageData.pixels[2],
                     pixelComponents >= 4 ? imageData.pixels[3] : 1.0f);

        // Check if image appears to be blank/white
        float sum = 0.0f;
        int sampleCount = std::min(1000, static_cast<int>(imageData.pixels.size() / pixelComponents));
        for (int i = 0; i < sampleCount; ++i) {
            sum += imageData.pixels[i * pixelComponents + 0]; // Sample R channel
        }
        float avg = sum / sampleCount;
        _logger->info("Average pixel value (sampled): {:.4f}", avg);

        if (avg > 0.95f) {
            _logger->warn("WARNING: Input image appears to be mostly white! Check source connection in host.");
        } else if (avg < 0.05f) {
            _logger->warn("WARNING: Input image appears to be mostly black! Check source connection in host.");
        }
    }

    // Write EXR file
    if (_logger) _logger->info("Writing EXR file");
    ImageIO::writeEXR(filename.str(), imageData);
    if (_logger) _logger->info("EXR file written successfully");

    return filename.str();
}

std::string BasePlugin::parseOutputPath(const json& history, int frame)
{
    if (_logger) _logger->info("Parsing output path from history JSON");

    // ComfyUI history structure:
    // {
    //   "prompt_id": {
    //     "outputs": {
    //       "node_id": {
    //         "images": [ { "filename": "...", "subfolder": "...", "type": "output" } ]
    //       }
    //     }
    //   }
    // }

    // Get path components
    std::string mountPath, project, workflow, version;
    mountPath = getLocalMountPath();
    project = getTrimmedStringParam(_projectName);
    workflow = getTrimmedStringParam(_workflowName);
    version = getTrimmedStringParam(_outputVersion);

    // Get auto-generated basename
    std::string basename = getEffectiveBasename();

    try {
        // History is a dict with prompt_id as key
        for (auto& [promptId, promptData] : history.items()) {
            if (_logger) _logger->info("Checking prompt ID: {}", promptId);

            if (!promptData.contains("outputs")) {
                if (_logger) _logger->warn("Prompt {} has no outputs", promptId);
                continue;
            }

            const json& outputs = promptData["outputs"];

            // Iterate through all output nodes
            for (auto& [nodeId, nodeData] : outputs.items()) {
                if (_logger) _logger->info("Checking output node: {}", nodeId);

                if (!nodeData.contains("images")) {
                    if (_logger) _logger->warn("Node {} has no images", nodeId);
                    continue;
                }

                const json& images = nodeData["images"];
                if (!images.is_array() || images.empty()) {
                    if (_logger) _logger->warn("Node {} has empty images array - likely cached workflow", nodeId);

                    // CACHED WORKFLOW HANDLING:
                    // When ComfyUI caches a workflow (0.00s execution), it returns empty image arrays
                    // because the files already exist on disk from a previous run.
                    // We need to construct the expected output filename from the workflow JSON.

                    if (_logger) _logger->info("Extracting filename pattern from workflow for cached result");

                    // The history contains the full workflow in prompt[2] (third element)
                    if (!promptData.contains("prompt") || !promptData["prompt"].is_array() || promptData["prompt"].size() < 3) {
                        if (_logger) _logger->warn("Cannot extract workflow from prompt - invalid structure");
                        continue;
                    }

                    const json& workflowNodes = promptData["prompt"][2];
                    if (!workflowNodes.contains(nodeId)) {
                        if (_logger) _logger->warn("Node {} not found in workflow JSON", nodeId);
                        continue;
                    }

                    const json& node = workflowNodes[nodeId];
                    if (!node.contains("inputs") || !node["inputs"].contains("filename_prefix")) {
                        if (_logger) _logger->warn("Node {} has no filename_prefix in workflow", nodeId);
                        continue;
                    }

                    std::string filenamePrefix = node["inputs"]["filename_prefix"].get<std::string>();
                    if (_logger) _logger->info("Extracted filename_prefix from node {}: {}", nodeId, filenamePrefix);

                    // Get SaveEXR version and frame padding from workflow
                    int saveVersion = node["inputs"].value("version", 1);
                    int framePad = node["inputs"].value("frame_pad", 4);

                    if (_logger) _logger->info("SaveEXR version: {}, frame_pad: {}", saveVersion, framePad);

                    // SaveEXR pattern depends on version parameter:
                    // - If version >= 0: {prefix}_v{version:03d}.{frame:0{pad}d}.exr
                    // - If version == -1: {prefix}.{frame:0{pad}d}.exr (no version suffix)
                    // Example (version=-1): Z:\out\TEST_SAM\segmentation\v001\shot01.0056.exr
                    // Extract just the filename part (last component after last slash/backslash)
                    size_t lastSlash = filenamePrefix.find_last_of("/\\");
                    std::string prefixBasename = (lastSlash != std::string::npos)
                        ? filenamePrefix.substr(lastSlash + 1)
                        : filenamePrefix;

                    // Construct filename based on SaveEXR version parameter
                    std::ostringstream filename;
                    filename << prefixBasename;

                    // Add version suffix only if version >= 0
                    if (saveVersion >= 0) {
                        filename << "_v" << std::setfill('0') << std::setw(3) << saveVersion;
                    }

                    filename << "."
                             << std::setfill('0') << std::setw(framePad) << frame
                             << ".exr";

                    std::string constructedFilename = filename.str();
                    if (_logger) _logger->info("Constructed SaveEXR filename: {}", constructedFilename);

                    // Build full path
                    // Output: /Volumes/comfyui-share/out/<PROJECT>/<WORKFLOW>/<VERSION>/{filename}
                    std::ostringstream fullPath;
                    fullPath << mountPath << "/out/" << project << "/"
                             << workflow << "/" << version << "/" << constructedFilename;

                    std::string constructedPath = fullPath.str();
                    if (_logger) _logger->info("Constructed output path: {}", constructedPath);

                    // Verify file exists (cached workflows should have file on disk)
                    std::ifstream testFile(constructedPath);
                    if (testFile.good()) {
                        if (_logger) _logger->info("✓ Verified cached output file exists on disk");
                        testFile.close();
                        return constructedPath;
                    } else {
                        if (_logger) _logger->warn("✗ Constructed path does not exist on disk: {}", constructedPath);
                        // Continue checking other nodes - maybe another node has the file
                        continue;
                    }
                }

                // Get the first image (normal workflow with filename in history)
                const json& firstImage = images[0];
                if (firstImage.contains("filename")) {
                    std::string filename = firstImage["filename"].get<std::string>();
                    if (_logger) _logger->info("Found output filename from history: {}", filename);

                    // Build full path matching Python pattern
                    // Output: /Volumes/comfyui-share/out/<PROJECT>/<WORKFLOW>/<VERSION>/basename_layer_frame_version_.exr
                    std::ostringstream fullPath;
                    fullPath << mountPath << "/out/" << project << "/"
                             << workflow << "/" << version << "/" << filename;

                    if (_logger) _logger->info("Constructed output path: {}", fullPath.str());
                    return fullPath.str();
                }
            }
        }
    } catch (const json::exception& e) {
        if (_logger) _logger->error("JSON parsing error: {}", e.what());
        throw std::runtime_error("Failed to parse ComfyUI history: " + std::string(e.what()));
    }

    // No output found
    if (_logger) _logger->warn("No output found in history and no cached file found on disk");
    return "";
}

std::string BasePlugin::getTrimmedStringParam(OFX::StringParam* param) const
{
    // Path-component string params (project/workflow/version/mount paths) are spliced
    // into filesystem paths. A pasted value can carry trailing whitespace or a newline,
    // which silently produces a path like ".../v004\n\006_OFX_TEST" that mac creates
    // happily but the SMB-mounted Windows side rejects with "Path not found".
    // Strip leading/trailing whitespace and any embedded control characters.
    std::string value;
    if (!param) return value;
    param->getValue(value);

    auto isWhitespaceOrCtrl = [](unsigned char c) {
        return c <= 0x20 || c == 0x7f;
    };

    size_t start = 0;
    while (start < value.size() && isWhitespaceOrCtrl(static_cast<unsigned char>(value[start]))) ++start;
    size_t end = value.size();
    while (end > start && isWhitespaceOrCtrl(static_cast<unsigned char>(value[end - 1]))) --end;
    std::string trimmed = value.substr(start, end - start);

    // Also defend against embedded control characters (e.g., a stray \r in the middle).
    trimmed.erase(std::remove_if(trimmed.begin(), trimmed.end(),
                                 [](unsigned char c) { return c < 0x20 || c == 0x7f; }),
                  trimmed.end());
    return trimmed;
}

std::string BasePlugin::getLocalMountPath() const
{
    // This host's mount of the shared storage, used for all local EXR I/O.
    // If unset, assume the ComfyUI server mount is also reachable at the same
    // path on this host (single-box setup, or an identically-mounted share) —
    // better than silently producing empty, rootless paths.
    std::string local = getTrimmedStringParam(_localMountPath);
    if (!local.empty()) return local;
    return getTrimmedStringParam(_serverMountPath);
}

std::string BasePlugin::convertPathForComfyUI(const std::string& localPath)
{
    // Rewrite a path from THIS machine's mount namespace into the ComfyUI
    // server's namespace. The ComfyUI box is the Windows storage server, so the
    // server mount is always the Windows view and the result uses backslashes.
    // Example: /mnt/comfyui-share/in/x  ->  \\HOSTNAME\share\in\x

    if (_logger) _logger->info("Converting path for ComfyUI: {}", localPath);

    const std::string clientMount = getLocalMountPath();
    const std::string serverMount = getTrimmedStringParam(_serverMountPath);

    if (_logger) {
        _logger->info("  Client mount (this host): {}", clientMount);
        _logger->info("  Server mount (ComfyUI):   {}", serverMount);
    }

    // No server mount configured: assume ComfyUI sees the very same path this
    // machine does (single-box setup, or identical mount point). Return the path
    // unchanged rather than stripping the client mount and emitting a rootless
    // "\in\..." path — that produced empty server paths and crashed jobs.
    if (serverMount.empty()) {
        if (_logger) _logger->warn("  No Windows/server mount set — leaving path unchanged");
        return localPath;
    }

    std::string serverPath = localPath;

    // Swap the client mount prefix for the server mount. Only rewrite when the
    // path actually starts with the client mount; otherwise leave it intact (we
    // must never drop the prefix and leave a rootless path).
    if (!clientMount.empty() && serverPath.rfind(clientMount, 0) == 0) {
        serverPath.replace(0, clientMount.length(), serverMount);
        if (_logger) _logger->info("  After mount replacement: {}", serverPath);
    } else if (clientMount.empty()) {
        if (_logger) _logger->warn("  No client mount for this host — set the mount path for this OS in the Server tab; leaving path unchanged");
        return localPath;
    } else {
        if (_logger) _logger->warn("  Path does not start with the client mount '{}' — leaving as-is", clientMount);
    }

    // The server is Windows: normalise to backslashes.
    std::replace(serverPath.begin(), serverPath.end(), '/', '\\');

    if (_logger) _logger->info("  Server path: {}", serverPath);

    // nlohmann/json escapes backslashes automatically when this is assigned to a
    // JSON field; customizeWorkflow()'s raw string-replacement path does its own
    // escaping when substituting into raw JSON text.
    return serverPath;
}

std::string BasePlugin::constructExpectedOutputPath(int frame)
{
    // Construct the expected output file path based on SaveEXR naming pattern
    // Pattern: {mountPath}/out/{project}/{workflow}/{version}/{basename}.{frame:04d}.exr
    //
    // NOTE: SaveEXR version is set to -1 (no version suffix) because the directory
    // already contains the version number (e.g., v001, v002).
    //
    // Example: /Volumes/comfyui-share/out/TEST_SAM/segmentation/v001/shot01.0056.exr

    std::string mountPath, project, workflow, version;
    mountPath = getLocalMountPath();
    project = getTrimmedStringParam(_projectName);
    workflow = getTrimmedStringParam(_workflowName);
    version = getTrimmedStringParam(_outputVersion);

    // Get effective basename (auto-generated or manual)
    std::string basename = getEffectiveBasename();

    // Construct full path (no version suffix in filename, version is in directory)
    std::ostringstream outputPath;
    outputPath << mountPath << "/out/" 
               << project << "/"
               << workflow << "/" 
               << version << "/" 
               << basename << "."
               << std::setfill('0') << std::setw(4) << frame << ".exr";

    return outputPath.str();
}

std::string BasePlugin::constructInputPath(int frame, const std::string& suffix)
{
    // Construct the input file path that was written by writeInputImage()
    // Pattern: {mountPath}/in/projects/{project}/{workflow}/{version}/{basename}[_suffix].{frame:04d}.exr
    //
    // Example: /Volumes/comfyui-share/in/projects/TEST_SAM/segmentation/v001/shot01.0056.exr
    // With suffix: /Volumes/comfyui-share/in/projects/TEST_SAM/segmentation/v001/shot01_B.0056.exr

    std::string mountPath, project, workflow, version;
    mountPath = getLocalMountPath();
    project = getTrimmedStringParam(_projectName);
    workflow = getTrimmedStringParam(_workflowName);
    version = getTrimmedStringParam(_outputVersion);

    // Get effective basename (auto-generated or manual)
    std::string basename = getEffectiveBasename();

    // Construct full path
    std::ostringstream inputPath;
    inputPath << mountPath << "/in/projects/"
              << project << "/"
              << workflow << "/"
              << version << "/"
              << basename << suffix << "."
              << std::setfill('0') << std::setw(4) << frame << ".exr";

    return inputPath.str();
}

std::string BasePlugin::constructInputFolderPath() const
{
    // Returns the folder that holds the full EXR sequence for sequence-mode plugins.
    // Pattern: {mount}/in/projects/{project}/{workflow}/{version}/{basename}/
    std::string mountPath, project, workflow, version;
    mountPath = getLocalMountPath();
    project = getTrimmedStringParam(_projectName);
    workflow = getTrimmedStringParam(_workflowName);
    version = getTrimmedStringParam(_outputVersion);
    std::string basename = const_cast<BasePlugin*>(this)->getEffectiveBasename();

    std::ostringstream folder;
    folder << mountPath << "/in/projects/"
           << project   << "/"
           << workflow  << "/"
           << version   << "/"
           << basename;
    return folder.str();
}

std::string BasePlugin::writeInputSequence(int startFrame, int endFrame)
{
    // Write every frame in [startFrame, endFrame] from _srcClip into a dedicated folder.
    // Each frame is fetched, converted, written, and immediately released to keep peak
    // memory at one frame (OFX images can be large).
    //
    // IMPORTANT: getImage() / fetchImage() must be called on the render thread.
    // This function must never be called from a background thread.

    std::string folder = constructInputFolderPath();
    if (!ImageIO::createDirectoryRecursive(folder)) {
        throw std::runtime_error("writeInputSequence: failed to create input folder: " + folder);
    }

    std::string base = getEffectiveBasename();
    int written = 0;

    if (_logger) {
        _logger->info("writeInputSequence: writing frames {}-{} to {}", startFrame, endFrame, folder);
    }

    for (int t = startFrame; t <= endFrame; ++t) {
        // fetchImage respects temporal clip access; may return nullptr for out-of-range frames
        std::unique_ptr<OFX::Image> img(_srcClip->fetchImage(static_cast<double>(t)));
        if (!img.get()) {
            if (_logger) _logger->warn("writeInputSequence: fetchImage({}) returned null, skipping", t);
            continue;
        }

        OfxRectI bounds = img->getBounds();
        int width      = bounds.x2 - bounds.x1;
        int height     = bounds.y2 - bounds.y1;
        int rowBytes   = img->getRowBytes();
        int nComp      = img->getPixelComponentCount();
        int bitDepthInt = 32;
        OFX::BitDepthEnum bd = img->getPixelDepth();
        if (bd == OFX::eBitDepthUByte)  bitDepthInt = 8;
        else if (bd == OFX::eBitDepthUShort) bitDepthInt = 16;

        ImageData imgData = ImageIO::fromOFXBuffer(
            img->getPixelData(), width, height, rowBytes, nComp, bitDepthInt, shouldFlipYForOFX());
        img.reset();  // release OFX buffer immediately — do not hold while writing

        std::ostringstream path;
        path << folder << "/" << base << "."
             << std::setfill('0') << std::setw(4) << t << ".exr";

        ImageIO::writeEXR(path.str(), imgData);
        written++;
    }

    if (_logger) {
        _logger->info("writeInputSequence: wrote {} frame(s) to {}", written, folder);
    }

    return folder;
}

std::map<std::string, std::string> BasePlugin::writeInputImages(int frame)
{
    if (_logger) _logger->info("=== WRITING INPUT IMAGES (CONFLICT-FREE NAMING) ===");

    std::map<std::string, std::string> inputPaths;

    // Write primary input (Source clip)
    // Naming: {basename}.{frame}.exr (no suffix for backward compatibility)
    if (_srcClip && _srcClip->isConnected()) {
        std::unique_ptr<OFX::Image> srcImg(_srcClip->fetchImage(frame));
        if (srcImg.get()) {
            std::string path = writeInputImage(srcImg.get(), frame, "");  // No suffix for primary
            inputPaths["InputA"] = path;
            if (_logger) _logger->info("✓ InputA (Source): {} [no suffix]", path);
        }
    }

    // Write secondary input (Source2 clip)
    // Naming: {basename}_B.{frame}.exr (suffix prevents collision with InputA)
    if (_src2Clip && _src2Clip->isConnected()) {
        std::unique_ptr<OFX::Image> src2Img(_src2Clip->fetchImage(frame));
        if (src2Img.get()) {
            std::string path = writeInputImage(src2Img.get(), frame, "_B");
            inputPaths["InputB"] = path;
            if (_logger) _logger->info("✓ InputB (Source2): {} [suffix: _B]", path);
        }
    }

    // Write tertiary input (Source3 clip)
    // Naming: {basename}_C.{frame}.exr (suffix prevents collision with InputA & InputB)
    if (_src3Clip && _src3Clip->isConnected()) {
        std::unique_ptr<OFX::Image> src3Img(_src3Clip->fetchImage(frame));
        if (src3Img.get()) {
            std::string path = writeInputImage(src3Img.get(), frame, "_C");
            inputPaths["InputC"] = path;
            if (_logger) _logger->info("✓ InputC (Source3): {} [suffix: _C]", path);
        }
    }

    if (_logger) {
        _logger->info("Total input images written: {} (all with unique filenames)", inputPaths.size());
        if (inputPaths.size() > 1) {
            _logger->info("✓ No filename conflicts: suffixes (_B, _C) ensure unique paths");
        }
    }
    return inputPaths;
}

int BasePlugin::getConnectedInputCount() const
{
    int count = 0;
    if (_srcClip && _srcClip->isConnected()) count++;
    if (_src2Clip && _src2Clip->isConnected()) count++;
    if (_src3Clip && _src3Clip->isConnected()) count++;
    return count;
}

std::string BasePlugin::getEffectiveBasename()
{
    // Auto-generate basename from project + instance name
    // For generic plugins (AnyComfy), workflow name is also included via includeWorkflowInBasename()
    //
    // Specialized plugins (SAMSegmentation): {project}_{instance}
    //   Example: TEST_SAMSegmentation
    //
    // Generic plugins (AnyComfy): {project}_{workflow}_{instance}
    //   Example: TEST_JULIEN_AnyComfy

    std::string project;
    project = getTrimmedStringParam(_projectName);

    // Check if this plugin type wants workflow in basename (generic plugins like AnyComfy)
    bool useWorkflow = includeWorkflowInBasename();
    std::string sanitizedWorkflow;

    if (useWorkflow) {
        std::string workflow;
        workflow = getTrimmedStringParam(_workflowName);

        // Sanitize workflow name: replace non-alphanumeric characters with underscores
        sanitizedWorkflow = workflow;
        for (char& c : sanitizedWorkflow) {
            if (!std::isalnum(static_cast<unsigned char>(c))) {
                c = '_';
            }
        }
        // Remove leading/trailing underscores that might result from sanitization
        while (!sanitizedWorkflow.empty() && sanitizedWorkflow.front() == '_') {
            sanitizedWorkflow.erase(0, 1);
        }
        while (!sanitizedWorkflow.empty() && sanitizedWorkflow.back() == '_') {
            sanitizedWorkflow.pop_back();
        }
    }

    if (!_instanceName.empty()) {
        // Sanitize instance name: replace non-alphanumeric characters with underscores
        std::string sanitizedInstance = _instanceName;
        for (char& c : sanitizedInstance) {
            if (!std::isalnum(static_cast<unsigned char>(c))) {
                c = '_';
            }
        }

        // Generate basename
        std::string generatedBasename;
        if (useWorkflow && !sanitizedWorkflow.empty()) {
            // Generic plugin: {project}_{workflow}_{instance}
            generatedBasename = project + "_" + sanitizedWorkflow + "_" + sanitizedInstance;
        } else {
            // Specialized plugin: {project}_{instance}
            generatedBasename = project + "_" + sanitizedInstance;
        }

        if (_logger) {
            if (useWorkflow) {
                _logger->info("Auto-generated basename: {} (project='{}', workflow='{}', instance='{}')",
                             generatedBasename, project, sanitizedWorkflow, sanitizedInstance);
            } else {
                _logger->info("Auto-generated basename: {} (project='{}', instance='{}')",
                             generatedBasename, project, sanitizedInstance);
            }
        }

        return generatedBasename;
    } else {
        // Fallback: if no instance name
        std::string generatedBasename;
        if (useWorkflow && !sanitizedWorkflow.empty()) {
            generatedBasename = project + "_" + sanitizedWorkflow;
        } else {
            generatedBasename = project;
        }

        if (_logger) {
            _logger->warn("No instance name available, using basename: {}", generatedBasename);
        }

        return generatedBasename;
    }
}

// ============================================================================
// Workflow File Management
// ============================================================================

std::string BasePlugin::getBundleResourcePath(const std::string& resourceName)
{
#ifdef __APPLE__
    // On macOS, use CoreFoundation to locate bundle resources
    // Try to get bundle from plugin binary path
    std::string pluginPath;
    try {
        pluginPath = getPropertySet().propGetString(kOfxPluginPropFilePath, false);
        if (_logger) _logger->debug("Plugin file path: {}", pluginPath);
    } catch (...) {
        if (_logger) _logger->warn("Could not retrieve plugin file path");
    }

    // Fallback: some hosts (e.g. DaVinci Resolve) do not populate kOfxPluginPropFilePath.
    // Use dladdr with a symbol from this shared library to get its own dylib path.
    // (_NSGetExecutablePath would return the host app's path, not the plugin's.)
    if (pluginPath.empty()) {
        Dl_info info;
        if (dladdr(static_cast<const void*>(&_dladdr_anchor), &info) && info.dli_fname) {
            pluginPath = info.dli_fname;
            if (_logger) _logger->info("kOfxPluginPropFilePath empty, using dladdr path: {}", pluginPath);
        } else {
            if (_logger) _logger->warn("dladdr failed, cannot locate bundle resources");
            return "";
        }
    }

    // Plugin path format: /path/to/Plugin.ofx.bundle/Contents/MacOS/Plugin.ofx
    // We want: /path/to/Plugin.ofx.bundle/Contents/Resources/{resourceName}

    size_t bundlePos = pluginPath.find(".ofx.bundle");
    if (bundlePos == std::string::npos) {
        if (_logger) _logger->warn("Plugin path does not contain .ofx.bundle: {}", pluginPath);
        return "";
    }

    // Extract bundle path (include .ofx.bundle)
    std::string bundlePath = pluginPath.substr(0, bundlePos + 11); // +11 for ".ofx.bundle"
    std::string resourcePath = bundlePath + "/Contents/Resources/" + resourceName;

    if (_logger) _logger->debug("Resolved bundle resource path: {}", resourcePath);
    return resourcePath;

#elif defined(__linux__)
    // On Linux, bundle structure: Plugin.ofx.bundle/Contents/Linux-{arch}/Plugin.ofx
    //                 Resources: Plugin.ofx.bundle/Contents/Resources/{resourceName}
    std::string pluginPath;
    try {
        pluginPath = getPropertySet().propGetString(kOfxPluginPropFilePath, false);
        if (_logger) _logger->debug("Plugin file path: {}", pluginPath);
    } catch (...) {
        if (_logger) _logger->warn("Could not retrieve plugin file path");
        return "";
    }

    size_t bundlePos = pluginPath.find(".ofx.bundle");
    if (bundlePos == std::string::npos) {
        if (_logger) _logger->warn("Plugin path does not contain .ofx.bundle: {}", pluginPath);
        return "";
    }

    std::string bundlePath = pluginPath.substr(0, bundlePos + 11);
    std::string resourcePath = bundlePath + "/Contents/Resources/" + resourceName;

    if (_logger) _logger->debug("Resolved bundle resource path: {}", resourcePath);
    return resourcePath;

#elif defined(_WIN32)
    // On Windows, bundle structure: Plugin.ofx.bundle\Contents\Win64\Plugin.ofx
    //                   Resources:  Plugin.ofx.bundle\Contents\Resources\{resourceName}
    std::string pluginPath;
    try {
        pluginPath = getPropertySet().propGetString(kOfxPluginPropFilePath, false);
        if (_logger) _logger->debug("Plugin file path: {}", pluginPath);
    } catch (...) {
        if (_logger) _logger->warn("Could not retrieve plugin file path");
    }

    // Fallback: some hosts (e.g. DaVinci Resolve) do not populate
    // kOfxPluginPropFilePath. Recover this module's own path from an address
    // inside it -- the Win32 equivalent of macOS dladdr.
    if (pluginPath.empty()) {
        HMODULE hModule = nullptr;
        if (GetModuleHandleExA(
                GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                    GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                reinterpret_cast<LPCSTR>(&_dladdr_anchor),
                &hModule) && hModule) {
            char modPath[MAX_PATH] = {0};
            DWORD n = GetModuleFileNameA(hModule, modPath, MAX_PATH);
            if (n > 0 && n < MAX_PATH) {
                pluginPath = modPath;
                if (_logger) _logger->info("kOfxPluginPropFilePath empty, using module path: {}", pluginPath);
            }
        }
        if (pluginPath.empty()) {
            if (_logger) _logger->warn("GetModuleFileName failed, cannot locate bundle resources");
            return "";
        }
    }

    size_t bundlePos = pluginPath.find(".ofx.bundle");
    if (bundlePos == std::string::npos) {
        if (_logger) _logger->warn("Plugin path does not contain .ofx.bundle: {}", pluginPath);
        return "";
    }

    // Forward slashes are accepted by Windows file APIs even when the rest of
    // the path uses backslashes, so we can append the resource subpath as-is.
    std::string bundlePath = pluginPath.substr(0, bundlePos + 11);
    std::string resourcePath = bundlePath + "/Contents/Resources/" + resourceName;

    if (_logger) _logger->debug("Resolved bundle resource path: {}", resourcePath);
    return resourcePath;

#else
    // Unsupported platform
    if (_logger) _logger->warn("Bundle resource lookup not implemented for this platform");
    return "";
#endif
}

std::string BasePlugin::resolveWorkflowPath(const std::string& workflowPath)
{
    if (_logger) _logger->info("Resolving workflow path: {}", workflowPath);

    // If path is absolute and exists, use it directly
    if (isAbsolutePath(workflowPath)) {
        std::ifstream test(workflowPath);
        if (test.good()) {
            test.close();
            if (_logger) _logger->info("Using absolute workflow path: {}", workflowPath);
            return workflowPath;
        }
    }

    // If path starts with "resources/", treat it as bundle-relative
    if (workflowPath.find("resources/") == 0) {
        std::string bundlePath = getBundleResourcePath(workflowPath.substr(10)); // Skip "resources/"
        if (!bundlePath.empty()) {
            std::ifstream test(bundlePath);
            if (test.good()) {
                test.close();
                if (_logger) _logger->info("Found workflow in bundle: {}", bundlePath);
                return bundlePath;
            }
        }
    }

    // Try as bundle-relative path directly
    std::string bundlePath = getBundleResourcePath(workflowPath);
    if (!bundlePath.empty()) {
        std::ifstream test(bundlePath);
        if (test.good()) {
            test.close();
            if (_logger) _logger->info("Found workflow in bundle: {}", bundlePath);
            return bundlePath;
        }
    }

    if (_logger) _logger->warn("Could not resolve workflow path: {}", workflowPath);
    return "";
}

json BasePlugin::loadWorkflowFromFile(const std::string& filepath)
{
    if (_logger) _logger->info("Loading workflow from file: {}", filepath);

    if (filepath.empty()) {
        throw std::runtime_error("Workflow file path is empty");
    }

    std::ifstream file(filepath);
    if (!file.is_open()) {
        std::string error = "Failed to open workflow file: " + filepath;
        if (_logger) _logger->error(error);
        throw std::runtime_error(error);
    }

    try {
        // Read entire file as string first (don't parse yet - file may have placeholders)
        std::stringstream buffer;
        buffer << file.rdbuf();
        std::string workflowStr = buffer.str();

        if (_logger) {
            _logger->info("Workflow file read successfully: {} bytes", workflowStr.size());
        }

        // Parse as JSON - this will work if file has no placeholders, or
        // will be done after placeholder replacement in customizeWorkflowWithParams()
        json workflow = json::parse(workflowStr);

        if (_logger) {
            _logger->debug("Workflow parsed successfully");
        }

        return workflow;
    } catch (const json::exception& e) {
        std::string error = "Failed to parse workflow JSON: " + std::string(e.what());
        if (_logger) _logger->error(error);
        throw std::runtime_error(error);
    }
}

json BasePlugin::customizeWorkflow(const json& baseWorkflow, int frame, const std::map<std::string, std::string>& inputPaths)
{
    // Default implementation: replace placeholders in workflow JSON
    // Derived classes can override for more complex customization

    if (_logger) _logger->debug("Customizing workflow for frame {} with {} input(s)", frame, inputPaths.size());

    // Convert the workflow to a string for placeholder replacement
    std::string workflowStr = baseWorkflow.dump();

    // Helper: escape backslashes for insertion into raw JSON text.
    // convertPathForComfyUI() returns a raw Windows path; when substituting into
    // a JSON string (obtained via dump()), backslashes must be doubled manually.
    auto jsonEscape = [](const std::string& path) {
        std::string escaped;
        escaped.reserve(path.size() * 2);
        for (char c : path) {
            if (c == '\\') escaped += "\\\\";
            else            escaped += c;
        }
        return escaped;
    };

    // Get parameters for replacements
    std::string mountPath, project, workflowName, version;
    mountPath = getLocalMountPath();
    project = getTrimmedStringParam(_projectName);
    workflowName = getTrimmedStringParam(_workflowName);
    version = getTrimmedStringParam(_outputVersion);

    std::string basename = getEffectiveBasename();

    // Build output prefix
    std::ostringstream outputPrefix;
    outputPrefix << mountPath << "/out/" << project << "/" << workflowName
                 << "/" << version << "/" << basename;

    // Convert paths for ComfyUI and escape for raw JSON string substitution
    std::string comfyOutputPrefix = jsonEscape(convertPathForComfyUI(outputPrefix.str()));

    // Common placeholder replacements
    // ${INPUT_PATH} - primary input EXR (legacy, backward compatible, same as INPUT_PATH_A)
    // ${INPUT_PATH_A} - InputA (Source clip)
    // ${INPUT_PATH_B} - InputB (Source2 clip)
    // ${INPUT_PATH_C} - InputC (Source3 clip)
    // ${OUTPUT_PREFIX} - output file prefix (Windows format for ComfyUI)
    // ${FRAME} - current frame number

    size_t pos = 0;
    std::string placeholder;
    int replaceCount = 0;

    // Replace ${INPUT_PATH_A}, ${INPUT_PATH_B}, ${INPUT_PATH_C}
    std::map<std::string, std::string> placeholderMap = {
        {"${INPUT_PATH_A}", "InputA"},
        {"${INPUT_PATH_B}", "InputB"},
        {"${INPUT_PATH_C}", "InputC"},
        {"${INPUT_PATH}", "InputA"}  // Legacy placeholder maps to InputA
    };

    for (const auto& [placeholderStr, inputId] : placeholderMap) {
        if (inputPaths.count(inputId) > 0) {
            std::string comfyInputPath = jsonEscape(convertPathForComfyUI(inputPaths.at(inputId)));
            if (_logger) _logger->debug("Replacing {} with: {}", placeholderStr, comfyInputPath);
            replaceCount = 0;
            while ((pos = workflowStr.find(placeholderStr)) != std::string::npos) {
                workflowStr.replace(pos, placeholderStr.length(), comfyInputPath);
                replaceCount++;
            }
            if (_logger && replaceCount > 0) {
                _logger->debug("  Replaced {} {} time(s)", placeholderStr, replaceCount);
            }
        }
    }

    // Warn if no input placeholders were found at all
    bool anyInputPlaceholderFound = false;
    for (const auto& [placeholderStr, inputId] : placeholderMap) {
        if (baseWorkflow.dump().find(placeholderStr) != std::string::npos) {
            anyInputPlaceholderFound = true;
            break;
        }
    }
    if (!anyInputPlaceholderFound && _logger) {
        _logger->debug("No INPUT_PATH placeholders found in workflow (may use direct node injection instead)");
    }

    // Replace ${OUTPUT_PREFIX}
    placeholder = "${OUTPUT_PREFIX}";
    if (_logger) _logger->debug("Replacing ${{OUTPUT_PREFIX}} with: {}", comfyOutputPrefix);
    replaceCount = 0;
    while ((pos = workflowStr.find(placeholder)) != std::string::npos) {
        workflowStr.replace(pos, placeholder.length(), comfyOutputPrefix);
        replaceCount++;
    }
    if (_logger && replaceCount == 0) {
        _logger->warn("OUTPUT_PREFIX placeholder not found in workflow!");
    }

    // Replace "${FRAME}" with number (no quotes)
    // Search for the placeholder INCLUDING surrounding quotes to convert string to number
    // For sequence plugins: use _sequenceStartFrame so SaveEXR.start_frame matches the
    // actual OFX frame numbers (e.g. 1001 not 0).  For frame-based plugins: use frame.
    placeholder = "\"${FRAME}\"";
    int frameForWorkflow = isSequencePlugin() ? _sequenceStartFrame : frame;
    std::string frameStr = std::to_string(frameForWorkflow);
    if (_logger) _logger->info("Replacing \"${{FRAME}}\" with numeric: {}", frameStr);
    replaceCount = 0;
    while ((pos = workflowStr.find(placeholder)) != std::string::npos) {
        workflowStr.replace(pos, placeholder.length(), frameStr);
        replaceCount++;
        if (_logger) _logger->debug("  Replaced FRAME at position {}", pos);
    }
    if (_logger) {
        if (replaceCount == 0) {
            _logger->warn("FRAME placeholder not found in workflow! Search pattern: {}", placeholder);
        } else {
            _logger->info("  Replaced FRAME {} time(s)", replaceCount);
        }
    }

    if (_logger) {
        _logger->debug("Workflow customization complete");
        _logger->trace("Customized workflow:\n{}", workflowStr.substr(0, 500) + "...");
    }

    // Parse back to JSON
    try {
        json finalWorkflow = json::parse(workflowStr);

        // Log the workflow after base customization (before plugin-specific customization)
        if (_logger) {
            _logger->debug("=== INTERMEDIATE WORKFLOW (after base customization) ===");
            _logger->debug("Workflow JSON after base replacements: {}", finalWorkflow.dump(2));
            _logger->debug("=== END INTERMEDIATE WORKFLOW ===");
            _logger->info("Base workflow customization complete (paths and frame replaced)");
        }

        return finalWorkflow;
    } catch (const json::exception& e) {
        std::string error = "Failed to parse customized workflow: " + std::string(e.what());
        if (_logger) _logger->error(error);
        throw std::runtime_error(error);
    }
}

// ============================================================================
// Async Rendering Helper Methods
// ============================================================================

void BasePlugin::renderBlocking(const OFX::RenderArguments &args)
{
    // Traditional blocking render - same as old render() logic
    int frame = static_cast<int>(args.time);

    // Validate required parameters
    std::string projectName;
    projectName = getTrimmedStringParam(_projectName);

    if (projectName.empty()) {
        std::string warningMsg = "Project Name is required but not set. Please set the Project Name parameter in the plugin settings.";
        if (_logger) _logger->warn(warningMsg);
        setPersistentMessage(OFX::Message::eMessageWarning, "", warningMsg.c_str());

        // Return passthrough
        if (_srcClip && _srcClip->isConnected()) {
            std::unique_ptr<OFX::Image> src(_srcClip->fetchImage(args.time));
            std::unique_ptr<OFX::Image> dst(_dstClip->fetchImage(args.time));
            if (src.get() && dst.get()) {
                copyPixelData(src.get(), dst.get());
            }
        }
        return;
    }

    // Check if source is connected
    if (!_srcClip || !_srcClip->isConnected()) {
        if (_logger) _logger->error("Source clip not connected");
        throw std::runtime_error("Source clip not connected");
    }

    // Pre-create output directory on CLIENT side (will sync to SERVER via network mount)
    std::string mountPath, workflowName, version, serverMount;
    mountPath = getLocalMountPath();
    workflowName = getTrimmedStringParam(_workflowName);
    version = getTrimmedStringParam(_outputVersion);
    serverMount = getTrimmedStringParam(_serverMountPath);

    if (_logger) {
        _logger->info("========================================");
        _logger->info("DIRECTORY CREATION");
        _logger->info("========================================");
        _logger->info("Client mount: {}", mountPath);
        _logger->info("Server mount: {}", serverMount);
    }

    // First, ensure the base /out directory exists
    std::string baseOutDir = mountPath + "/out";
    try {
        // Check if mount path exists
        struct stat mountStat;
        if (stat(mountPath.c_str(), &mountStat) != 0 || !S_ISDIR(mountStat.st_mode)) {
            std::string errorMsg = "Shared mount path does not exist or is not accessible: " + mountPath;
            errorMsg += "\n\nPlease verify:";
            errorMsg += "\n1. The shared mount is properly configured";
            errorMsg += "\n2. The mount path is accessible from this machine";
            errorMsg += "\n3. You have write permissions to the mount";
            if (_logger) _logger->error(errorMsg);
            setPersistentMessage(OFX::Message::eMessageError, "", errorMsg.c_str());
            throw std::runtime_error(errorMsg);
        }

        // Create base /out directory
        if (_logger) _logger->info("Creating base output directory: {}", baseOutDir);
        if (!ImageIO::createDirectoryRecursive(baseOutDir)) {
            std::string errorMsg = "Failed to create base output directory: " + baseOutDir;
            if (_logger) _logger->error(errorMsg);
            setPersistentMessage(OFX::Message::eMessageError, "", errorMsg.c_str());
            throw std::runtime_error(errorMsg);
        }
    } catch (const std::exception& e) {
        std::string errorMsg = std::string("Failed to create base output directory: ") + e.what();
        if (_logger) _logger->error(errorMsg);
        setPersistentMessage(OFX::Message::eMessageError, "", errorMsg.c_str());
        throw std::runtime_error(errorMsg);
    }

    // Now create the full output directory path
    std::string outputDir = mountPath + "/out/" + projectName + "/" + workflowName + "/" + version;
    if (_logger) _logger->info("Creating full output directory: {}", outputDir);

    try {
        if (!ImageIO::createDirectoryRecursive(outputDir)) {
            std::string errorMsg = "Failed to create output directory: " + outputDir;
            if (_logger) _logger->error(errorMsg);
            setPersistentMessage(OFX::Message::eMessageError, "", errorMsg.c_str());
            throw std::runtime_error(errorMsg);
        }

        // Verify directory was created successfully
        struct stat st;
        if (stat(outputDir.c_str(), &st) != 0 || !S_ISDIR(st.st_mode)) {
            std::string errorMsg = "Directory creation succeeded but verification failed: " + outputDir;
            if (_logger) _logger->error(errorMsg);
            setPersistentMessage(OFX::Message::eMessageError, "", errorMsg.c_str());
            throw std::runtime_error(errorMsg);
        }

        if (_logger) {
            _logger->info("Successfully created and verified directory: {}", outputDir);

            // Log what the server-side path will be
            std::string serverPath = outputDir;
            if (!mountPath.empty() && serverPath.find(mountPath) == 0) {
                serverPath.replace(0, mountPath.length(), serverMount);
            }
            std::replace(serverPath.begin(), serverPath.end(), '/', '\\');
            _logger->info("Server-side path should be: {}", serverPath);
            _logger->info("");
            _logger->info("IMPORTANT: If ComfyUI fails with 'path not found' error,");
            _logger->info("manually verify that this directory exists on the Windows server:");
            _logger->info("  {}", serverPath);
            _logger->info("========================================");
        }
    } catch (const std::exception& e) {
        std::string errorMsg = std::string("Failed to create output directory: ") + e.what();
        if (_logger) _logger->error(errorMsg);
        setPersistentMessage(OFX::Message::eMessageError, "", errorMsg.c_str());
        throw std::runtime_error(errorMsg);
    }

    // Execute workflow synchronously (BLOCKING)
    progressStart("Processing with ComfyUI...");

    try {
        executeWorkflow(args);
        progressEnd();
        if (_logger) {
            _logger->info("========================================");
            _logger->info("RENDER COMPLETED SUCCESSFULLY");
            _logger->info("========================================");
        }
    } catch (const std::exception& e) {
        progressEnd();
        if (_logger) {
            _logger->error("========================================");
            _logger->error("RENDER FAILED: {}", e.what());
            _logger->error("========================================");
        }
        throw;
    }
}

void BasePlugin::renderAsync(const OFX::RenderArguments &args)
{
    auto renderStartTime = std::chrono::steady_clock::now();
    int frame = static_cast<int>(args.time);

    if (_logger) {
        _logger->info("========================================");
        _logger->info("renderAsync() START - Frame {}", frame);
    }

    // STEP 1: Check cache first (instant return if available)
    auto cacheCheckStart = std::chrono::steady_clock::now();
    std::string cachedPath = constructExpectedOutputPath(frame);

    // Check in-memory cache first (instant!)
    bool cachedInMemory = false;
    {
        std::lock_guard<std::mutex> lock(_cacheMutex);
        cachedInMemory = (_cacheFileExists.find(cachedPath) != _cacheFileExists.end());
    }

    bool fileExists = false;
    if (cachedInMemory) {
        // Fast path: we know the file exists from previous check
        fileExists = true;
        if (_logger) {
            _logger->debug("Frame {}: In-memory cache HIT for {}", frame, cachedPath);
        }
    } else {
        // Slow path: check filesystem (only once per file)
        std::ifstream cacheTest(cachedPath);
        fileExists = cacheTest.good();
        if (fileExists) {
            cacheTest.close();
            // Remember this file exists (avoid future slow checks)
            std::lock_guard<std::mutex> lock(_cacheMutex);
            _cacheFileExists.insert(cachedPath);
        }
    }

    auto cacheCheckDuration = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - cacheCheckStart);

    // Cache OFF: discard any existing output so the SaveEXR node (which refuses
    // to overwrite) can write a fresh frame, then fall through to re-render.
    const bool useCache = !_enableCache || _enableCache->getValue();
    if (fileExists && !useCache) {
        if (_logger) {
            _logger->info("Frame {}: Cache disabled — removing stale output and re-rendering: {}",
                         frame, cachedPath);
        }
        std::error_code ec;
        std::filesystem::remove(cachedPath, ec);
        {
            std::lock_guard<std::mutex> lock(_cacheMutex);
            _cacheFileExists.erase(cachedPath);
        }
        fileExists = false;
    }

    if (fileExists) {
        if (_logger) {
            _logger->info("Frame {}: Cache HIT (check took {} ms, cached_in_mem: {})",
                         frame, cacheCheckDuration.count(), cachedInMemory);
            _logger->info("  Path: {}", cachedPath);
        }

        try {
            auto loadStart = std::chrono::steady_clock::now();
            loadCachedResult(args, cachedPath);
            auto loadDuration = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - loadStart);

            auto totalDuration = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - renderStartTime);

            if (_logger) {
                _logger->info("Frame {}: Loaded from cache (load: {} ms, total: {} ms)",
                             frame, loadDuration.count(), totalDuration.count());
                _logger->info("========================================");
            }
            return;  // ✓ INSTANT, non-blocking
        } catch (const std::exception& e) {
            std::string errorMsg = e.what();
            bool isDimensionMismatch = (errorMsg.find("dimensions") != std::string::npos &&
                                       errorMsg.find("do not match") != std::string::npos);

            if (_logger) {
                if (isDimensionMismatch) {
                    _logger->warn("Frame {}: Cached file has wrong dimensions - will invalidate and re-render", frame);
                    _logger->info("  Error details: {}", errorMsg);
                } else {
                    _logger->error("Frame {}: Failed to load cached result: {}", frame, errorMsg);
                }
            }

            // Remove from memory cache
            {
                std::lock_guard<std::mutex> lock(_cacheMutex);
                _cacheFileExists.erase(cachedPath);
            }

            // If dimension mismatch, rename the wrong-resolution file so it doesn't interfere
            if (isDimensionMismatch) {
                try {
                    std::string renamedPath = cachedPath + ".wrong_resolution";
                    if (_logger) {
                        _logger->info("  Renaming wrong-resolution cached file to: {}", renamedPath);
                    }
                    std::filesystem::rename(cachedPath, renamedPath);
                } catch (const std::exception& renameError) {
                    if (_logger) {
                        _logger->warn("  Could not rename cached file: {}", renameError.what());
                    }
                }
            }

            // Fall through to re-render
        }
    } else {
        if (_logger) {
            _logger->info("Frame {}: Cache MISS (check took {} ms)", frame, cacheCheckDuration.count());
        }
    }

    // Ensure ComfyUI client and job manager exist before any job-submission path.
    // This must happen here (not lazily inside STEP 3) so that the sequence plugin
    // guard below can see a valid _jobManager on the very first render call.
    if (!_comfyClient) {
        if (_logger) _logger->info("Frame {}: Creating ComfyUI client (early init)", frame);
        std::string address;
        _serverAddress->getValue(address);
        int port = _serverPort->getValue();
        std::string serverUrl = address + ":" + std::to_string(port);
        _comfyClient.reset(new Client(serverUrl));
    }
    if (!_jobManager && _comfyClient) {
        if (_logger) _logger->info("Frame {}: Initializing AsyncJobManager (early init)", frame);
        _jobManager.reset(new AsyncJobManager(_comfyClient.get(), _logger));
        _jobManager->setCompletionCallback([this](int completedFrame, bool success) {
            this->onJobComplete(completedFrame, success);
        });
        _jobManager->setStatusUpdateCallback([this]() {
            this->updateJobStatusDisplay();
        });
        if (_logger) _logger->info("AsyncJobManager initialized successfully");
    }
    // Apply the user's "Timeout (s)" on every render entry so per-instance
    // adjustments take effect immediately (no plugin reinit required).
    if (_jobManager && _timeout) {
        _jobManager->setMaxJobDurationSec(_timeout->getValue());
    }

    // STEP 2 (sequence plugins only): manage the single sequence-wide job
    if (isSequencePlugin() && _jobManager) {

        // 2a. Active job guard — build the output prefix key for this configuration
        std::string mountPath, project, workflowName, version;
        mountPath = getLocalMountPath();
        project = getTrimmedStringParam(_projectName);
        workflowName = getTrimmedStringParam(_workflowName);
        version = getTrimmedStringParam(_outputVersion);
        std::string outputPrefix = mountPath + "/out/" + project + "/" + workflowName + "/" + version + "/" + getEffectiveBasename();

        if (_pendingSequenceOutputPrefix == outputPrefix) {
            // A job was previously submitted for this exact output path.
            // Check whether it is still active.
            JobStatus seqStatus = _jobManager->getJobStatus(_sequenceStartFrame);
            bool active = (seqStatus == JobStatus::QUEUED     ||
                           seqStatus == JobStatus::PROCESSING);
            if (active) {
                if (_logger) _logger->info("Frame {}: Sequence job active (startFrame={}) — returning placeholder", frame, _sequenceStartFrame);
                returnPlaceholder(args, frame);
                return;
            }
            // Job ended (completed successfully or failed) but output file missing for
            // this frame → either failure or file-not-yet-flushed.  Clear pending and
            // fall through to re-submit.
            if (_logger) _logger->info("Frame {}: Sequence job ended without output — clearing pending, will resubmit", frame);
            _pendingSequenceOutputPrefix.clear();
        }

        // 2b. No active job for this output prefix.
        // Sequence plugins require the user to press "Collect & Submit" to start processing.
        // The render thread only shows a placeholder — it never calls fetchImage() here.
        if (_logger) _logger->info("Frame {}: No sequence job active — waiting for Collect & Submit", frame);
        returnPlaceholder(args, frame);
        return;
    }

    // STEP 2: Check if job already pending
    if (_jobManager && _jobManager->isJobPending(frame)) {
        JobStatus status = _jobManager->getJobStatus(frame);
        if (_logger) {
            _logger->info("Frame {}: Job already pending (status: {})",
                         frame, jobStatusToString(status));
        }

        // Return placeholder immediately
        returnPlaceholder(args, frame);
        return;  // ✓ NON-BLOCKING
    }

    // STEP 3: Submit new async job
    if (_logger) _logger->info("Frame {}: Cache MISS - submitting async job", frame);

    try {
        // Validate project name
        std::string projectName;
        projectName = getTrimmedStringParam(_projectName);

        if (projectName.empty()) {
            if (_logger) _logger->warn("Frame {}: Project name empty, returning passthrough", frame);
            returnPlaceholder(args, frame);
            return;
        }

        // Get mount path info for directory creation
        std::string mountPath, workflowName, version;
        mountPath = getLocalMountPath();
        workflowName = getTrimmedStringParam(_workflowName);
        version = getTrimmedStringParam(_outputVersion);

        // Pre-create output directory on CLIENT side (will sync to SERVER via network mount)
        // This MUST happen before async submission to ensure directories exist
        std::string outputDir = mountPath + "/out/" + projectName + "/" + workflowName + "/" + version;

        try {
            // Check if mount path exists
            struct stat mountStat;
            if (stat(mountPath.c_str(), &mountStat) != 0 || !S_ISDIR(mountStat.st_mode)) {
                std::string errorMsg = "Shared mount path does not exist or is not accessible: " + mountPath;
                if (_logger) _logger->error("Frame {}: {}", frame, errorMsg);
                returnPlaceholder(args, frame);
                return;
            }

            // Create base /out directory
            std::string baseOutDir = mountPath + "/out";
            if (!ImageIO::createDirectoryRecursive(baseOutDir)) {
                std::string errorMsg = "Failed to create base output directory: " + baseOutDir;
                if (_logger) _logger->error("Frame {}: {}", frame, errorMsg);
                returnPlaceholder(args, frame);
                return;
            }

            // Create full output directory path
            if (!ImageIO::createDirectoryRecursive(outputDir)) {
                std::string errorMsg = "Failed to create output directory: " + outputDir;
                if (_logger) _logger->error("Frame {}: {}", frame, errorMsg);
                returnPlaceholder(args, frame);
                return;
            }

            // Verify directory was created successfully
            struct stat st;
            if (stat(outputDir.c_str(), &st) != 0 || !S_ISDIR(st.st_mode)) {
                std::string errorMsg = "Directory creation succeeded but verification failed: " + outputDir;
                if (_logger) _logger->error("Frame {}: {}", frame, errorMsg);
                returnPlaceholder(args, frame);
                return;
            }

            if (_logger) {
                _logger->info("Frame {}: Successfully created output directory: {}", frame, outputDir);
            }
        } catch (const std::exception& e) {
            std::string errorMsg = std::string("Failed to create output directory: ") + e.what();
            if (_logger) _logger->error("Frame {}: {}", frame, errorMsg);
            returnPlaceholder(args, frame);
            return;
        }

        // Fetch source images (fast, in-memory operation)
        // For generator workflows (0 inputs), source may not be connected - that's OK
        bool hasSourceConnected = _srcClip && _srcClip->isConnected();

        if (!hasSourceConnected) {
            if (_logger) _logger->info("Frame {}: No source clip connected - running as generator workflow", frame);
        }

        auto fetchStart = std::chrono::steady_clock::now();

        // Helper lambda to convert OFX image to ImageData
        auto convertClipToImageData = [this](OFX::Clip* clip, double time) -> ImageData {
            std::unique_ptr<OFX::Image> img(clip->fetchImage(time));
            if (!img.get()) {
                throw std::runtime_error("Failed to fetch image from clip");
            }

            OfxRectI bounds = img->getBounds();
            int width = bounds.x2 - bounds.x1;
            int height = bounds.y2 - bounds.y1;
            int rowBytes = img->getRowBytes();
            int pixelComponents = img->getPixelComponentCount();
            OFX::BitDepthEnum bitDepth = img->getPixelDepth();

            int bitDepthInt = 32;
            if (bitDepth == OFX::eBitDepthUByte) bitDepthInt = 8;
            else if (bitDepth == OFX::eBitDepthUShort) bitDepthInt = 16;

            return ImageIO::fromOFXBuffer(
                img->getPixelData(), width, height, rowBytes, pixelComponents, bitDepthInt,
                shouldFlipYForOFX()
            );
        };

        // Collect all connected input images
        std::map<std::string, ImageData> imageDataMap;
        std::map<std::string, std::string> inputPathMap;
        std::string basename = getEffectiveBasename();

        // Helper to construct input path with suffix
        auto makeInputPath = [&](const std::string& suffix) {
            std::ostringstream ss;
            ss << mountPath << "/in/projects/"
               << projectName << "/"
               << workflowName << "/"
               << version << "/"
               << basename << suffix << "."
               << std::setw(4) << std::setfill('0') << frame << ".exr";
            return ss.str();
        };

        try {
            // Primary input (InputA / Source) - only if connected
            // Path: {basename}.{frame}.exr (no suffix)
            if (hasSourceConnected) {
                imageDataMap["InputA"] = convertClipToImageData(_srcClip, args.time);
                inputPathMap["InputA"] = makeInputPath("");
                if (_logger) _logger->info("Frame {}: InputA (Source) → {} [no suffix]",
                                         frame, inputPathMap["InputA"]);
            }

            // Secondary input (InputB / Source2) - optional
            // Path: {basename}_B.{frame}.exr (suffix prevents collision)
            if (_src2Clip && _src2Clip->isConnected()) {
                imageDataMap["InputB"] = convertClipToImageData(_src2Clip, args.time);
                inputPathMap["InputB"] = makeInputPath("_B");
                if (_logger) _logger->info("Frame {}: InputB (Source2) → {} [suffix: _B]",
                                         frame, inputPathMap["InputB"]);
            }

            // Tertiary input (InputC / Source3) - optional
            // Path: {basename}_C.{frame}.exr (suffix prevents collision)
            if (_src3Clip && _src3Clip->isConnected()) {
                imageDataMap["InputC"] = convertClipToImageData(_src3Clip, args.time);
                inputPathMap["InputC"] = makeInputPath("_C");
                if (_logger) _logger->info("Frame {}: InputC (Source3) → {} [suffix: _C]",
                                         frame, inputPathMap["InputC"]);
            }
        } catch (const std::exception& e) {
            if (_logger) _logger->error("Frame {}: Failed to fetch source image: {}", frame, e.what());
            returnPlaceholder(args, frame);
            return;
        }

        // Log input count and confirm no conflicts
        if (imageDataMap.empty()) {
            if (_logger) _logger->info("Frame {}: Generator workflow - no input images", frame);
        } else if (imageDataMap.size() > 1) {
            if (_logger) _logger->info("Frame {}: {} inputs with conflict-free naming (suffixes: _B, _C)",
                                     frame, imageDataMap.size());
        }

        auto fetchDuration = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - fetchStart);

        auto convertStart = std::chrono::steady_clock::now();
        auto convertDuration = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - convertStart);

        if (_logger) {
            _logger->info("Frame {}: Fetched {} input image(s) in {} ms",
                         frame, imageDataMap.size(), fetchDuration.count());
        }

        // Ensure ComfyUI client exists
        if (!_comfyClient) {
            if (_logger) _logger->info("Frame {}: Creating ComfyUI client", frame);

            std::string address;
            _serverAddress->getValue(address);
            int port = _serverPort->getValue();
            std::string serverUrl = address + ":" + std::to_string(port);
            _comfyClient.reset(new Client(serverUrl));

            // Re-initialize job manager with new client
            _jobManager.reset(new AsyncJobManager(_comfyClient.get(), _logger));
            _jobManager->setCompletionCallback([this](int completedFrame, bool success) {
                this->onJobComplete(completedFrame, success);
            });
            _jobManager->setStatusUpdateCallback([this]() {
                this->updateJobStatusDisplay();
            });
        }
        // Push the live user timeout into the (possibly fresh) manager.
        if (_jobManager && _timeout) {
            _jobManager->setMaxJobDurationSec(_timeout->getValue());
        }

        // Submit job asynchronously (TRULY NON-BLOCKING!)
        // The slow parts (writing EXR, building workflow, submitting to ComfyUI) happen in background
        if (_jobManager) {
            auto submitStart = std::chrono::steady_clock::now();

            _jobManager->submitJobAsync(frame, imageDataMap, inputPathMap, cachedPath, this);

            auto submitDuration = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - submitStart);

            auto totalDuration = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - renderStartTime);

            if (_logger) {
                _logger->info("Frame {}: submitJobAsync() returned in {} ms (non-blocking!)", frame, submitDuration.count());
                _logger->info("Frame {}: renderAsync() TOTAL TIME: {} ms", frame, totalDuration.count());
                _logger->info("  Breakdown: cache={} ms, fetch={} ms, convert={} ms, submitAsync={} ms",
                             cacheCheckDuration.count(), fetchDuration.count(),
                             convertDuration.count(), submitDuration.count());
                _logger->info("  (File write, workflow build, and ComfyUI submission happening in background)");
            }

            updateJobStatusDisplay();
        }

    } catch (const std::exception& e) {
        if (_logger) _logger->error("Frame {}: Failed to submit async job: {}", frame, e.what());
    }

    // Return placeholder immediately
    returnPlaceholder(args, frame);  // ✓ NON-BLOCKING
}

void BasePlugin::returnPlaceholder(const OFX::RenderArguments &args, int frame)
{
    // Always use checkerboard pattern (mode 1) - parameter is hidden
    int placeholderMode = 1;
    // _placeholderMode->getValue(placeholderMode);  // No longer reading from parameter

    if (_logger) _logger->debug("Frame {}: Returning placeholder (mode: {})", frame, placeholderMode);

    std::unique_ptr<OFX::Image> dst(_dstClip->fetchImage(args.time));
    if (!dst.get()) {
        if (_logger) _logger->error("Frame {}: Failed to fetch destination image", frame);
        return;
    }

    switch (placeholderMode) {
        case 0:  // Source passthrough
            if (_srcClip && _srcClip->isConnected()) {
                std::unique_ptr<OFX::Image> src(_srcClip->fetchImage(args.time));
                if (src.get()) {
                    copyPixelData(src.get(), dst.get());
                }
            }
            break;

        case 1:  // Checkerboard pattern
            renderCheckerboard(dst.get());
            break;

        case 2:  // Gray frame
            renderSolidColor(dst.get(), 0.5, 0.5, 0.5);
            break;

        case 3:  // Last valid frame
            {
                int lastValidFrame = findLastValidFrame(args.time);
                if (lastValidFrame >= 0) {
                    std::string lastValidPath = constructExpectedOutputPath(lastValidFrame);
                    try {
                        loadCachedResult(args, lastValidPath);
                        return;
                    } catch (...) {
                        // Fall back to passthrough
                    }
                }

                // Fallback to source passthrough
                if (_srcClip && _srcClip->isConnected()) {
                    std::unique_ptr<OFX::Image> src(_srcClip->fetchImage(args.time));
                    if (src.get()) {
                        copyPixelData(src.get(), dst.get());
                    }
                }
            }
            break;

        default:
            // Default to passthrough
            if (_srcClip && _srcClip->isConnected()) {
                std::unique_ptr<OFX::Image> src(_srcClip->fetchImage(args.time));
                if (src.get()) {
                    copyPixelData(src.get(), dst.get());
                }
            }
            break;
    }
}

void BasePlugin::loadCachedResult(const OFX::RenderArguments &args, const std::string& cachedPath)
{
    std::unique_ptr<OFX::Image> dst(_dstClip->fetchImage(args.time));
    if (!dst.get()) {
        throw std::runtime_error("Failed to fetch destination image");
    }

    int frame = static_cast<int>(args.time);

    // Get expected dimensions from the INPUT EXR that was sent to ComfyUI
    // This is critical when processing resized images - we need to validate against
    // the actual input dimensions, not the original RoD
    std::string inputPath = constructInputPath(frame);
    int expectedWidth = 0;
    int expectedHeight = 0;

    try {
        ImageData inputImageData = ImageIO::readEXR(inputPath);
        expectedWidth = inputImageData.width;
        expectedHeight = inputImageData.height;

        if (_logger) {
            _logger->debug("Frame {}: Input dimensions from {}x{} (from {})",
                          frame, expectedWidth, expectedHeight, inputPath);
        }
    } catch (const std::exception& e) {
        // If we can't read the input file, fall back to RoD or project size
        // This handles generator workflows and edge cases where input may have been deleted
        if (_logger) {
            _logger->debug("Frame {}: Could not read input EXR: {}", frame, e.what());
        }

        if (_srcClip && _srcClip->isConnected()) {
            OfxRectD rod = _srcClip->getRegionOfDefinition(args.time);
            expectedWidth = static_cast<int>(rod.x2 - rod.x1);
            expectedHeight = static_cast<int>(rod.y2 - rod.y1);
            if (_logger) {
                _logger->debug("Frame {}: Using source RoD for expected dimensions: {}x{}", frame, expectedWidth, expectedHeight);
            }
        } else {
            // Generator workflow - use project size or default
            try {
                double projectW = getPropertySet().propGetDouble(kOfxImageEffectPropProjectSize, 0, false);
                double projectH = getPropertySet().propGetDouble(kOfxImageEffectPropProjectSize, 1, false);
                expectedWidth = static_cast<int>(projectW);
                expectedHeight = static_cast<int>(projectH);
                if (_logger) {
                    _logger->debug("Frame {}: Generator workflow - using project size: {}x{}", frame, expectedWidth, expectedHeight);
                }
            } catch (...) {
                // Fall back to output image dimensions (will be validated later)
                expectedWidth = 0;
                expectedHeight = 0;
                if (_logger) {
                    _logger->debug("Frame {}: Generator workflow - will use output dimensions", frame);
                }
            }
        }
    }

    if (_logger) {
        OfxRectI renderWindow = dst->getBounds();
        _logger->debug("Frame {}: Expected dimensions={}x{}, Render window=({},{} to {},{})",
                      frame, expectedWidth, expectedHeight,
                      renderWindow.x1, renderWindow.y1,
                      renderWindow.x2, renderWindow.y2);
    }

    // Read cached output EXR file
    ImageData outputImageData = ImageIO::readEXR(cachedPath);

    // Surface "model produced empty output" to the user. ComfyUI considers a
    // run successful even when the inference produced an all-zero mask (e.g.
    // SAM-family open-vocabulary detectors return zero objects when the input
    // resolution is too small for the prompted subject — typical when the
    // host timeline is at proxy res). Without this signal the plugin shows
    // pure black with no explanation and the user can't tell whether the
    // plugin is broken or the model just found nothing.
    if (ImageIO::isImageCenterEmpty(outputImageData)) {
        if (_logger) {
            _logger->warn("Frame {}: Output EXR loaded successfully but center region is empty "
                          "(mean RGB approximately 0). ComfyUI ran without errors but the model "
                          "returned no content. Common causes: (a) SAM/matte prompt did not match "
                          "anything at the current input resolution — try raising the timeline "
                          "resolution or revising the prompt; (b) detection threshold is too high; "
                          "(c) the subject is out of frame. EXR path: {}",
                          frame, cachedPath);
        }
        try { if (_jobStatus)      _jobStatus->setValue("Empty output - model found nothing"); } catch (...) {}
        try { if (_jobStatusColor) _jobStatusColor->setValue(1.0, 0.5, 0.0); } catch (...) {}  // orange warning
    }

    // Cache output dimensions for dynamic RoD (allows canvas to resize to match output)
    {
        std::lock_guard<std::mutex> lock(_cacheMutex);
        _cacheDimensions[frame] = {outputImageData.width, outputImageData.height};
    }

    // NOTE: We do NOT validate that output dimensions match input dimensions
    // because ComfyUI workflows can include resize operations.
    // For example, upscaling workflows will produce larger outputs than inputs.
    // We only validate that the output can be rendered to the requested render window.

    if (_logger) {
        _logger->debug("Frame {}: Cached output dimensions={}x{}, input dimensions={}x{}",
                      frame, outputImageData.width, outputImageData.height,
                      expectedWidth, expectedHeight);

        if (outputImageData.width != expectedWidth || outputImageData.height != expectedHeight) {
            _logger->info("Frame {}: Output resolution differs from input (workflow may include resize operation)",
                         frame);
        }
    }

    // Get render window bounds - we may only need to render a portion
    OfxRectI renderWindow = dst->getBounds();
    int renderWidth = renderWindow.x2 - renderWindow.x1;
    int renderHeight = renderWindow.y2 - renderWindow.y1;

    // Check if output dimensions match the render window
    // Note: We compare against OUTPUT dimensions, not INPUT dimensions,
    // because workflows can resize images
    bool isFullFrameRender = (renderWindow.x1 == 0 && renderWindow.y1 == 0 &&
                             renderWidth == outputImageData.width &&
                             renderHeight == outputImageData.height);

    if (_logger) {
        _logger->debug("Frame {}: Cached image={}x{}, Render request={}x{}, Full frame={}",
                      static_cast<int>(args.time),
                      outputImageData.width, outputImageData.height,
                      renderWidth, renderHeight,
                      isFullFrameRender);
    }

    if (isFullFrameRender && _logger) {
        _logger->info("Frame {}: loadCachedResult branch = FULL-FRAME ({}x{})",
                      frame, outputImageData.width, outputImageData.height);
    }

    // Handle resolution mismatch (e.g., when workflow includes resize)
    ImageData regionData = outputImageData;

    // If render window doesn't match output dimensions, we need to handle it
    if (!isFullFrameRender) {
        // Check if this is a sub-region render or a resolution mismatch
        bool isSubRegion = (renderWidth <= outputImageData.width &&
                           renderHeight <= outputImageData.height &&
                           renderWindow.x1 >= 0 && renderWindow.y1 >= 0 &&
                           renderWindow.x2 <= outputImageData.width &&
                           renderWindow.y2 <= outputImageData.height);

        if (_logger) {
            _logger->info("Frame {}: loadCachedResult branch = {} | output={}x{} render=({},{})-({},{}) ({}x{})",
                          frame, isSubRegion ? "SUB-REGION" : "RESOLUTION-MISMATCH",
                          outputImageData.width, outputImageData.height,
                          renderWindow.x1, renderWindow.y1, renderWindow.x2, renderWindow.y2,
                          renderWidth, renderHeight);
        }

        if (isSubRegion) {
            // Extract the render window region from the full cached image.
            // regionData is built in top-left (EXR) layout — the final
            // toOFXBuffer(..., shouldFlipYForOFX()) call below is the single
            // place that handles host-specific OFX buffer orientation. Doing a
            // Y-flip here too would double-flip on hosts where the flag is true.
            // The renderWindow is in OFX (bottom-up) coords; we map it to image
            // (top-down) row range and iterate ascending so the result is top-left.
            regionData.width = renderWidth;
            regionData.height = renderHeight;

            std::vector<float> extractedPixels;
            extractedPixels.reserve(renderWidth * renderHeight * outputImageData.channels);

            int firstImageRow = outputImageData.height - renderWindow.y2;
            int lastImageRow  = outputImageData.height - renderWindow.y1;  // exclusive
            for (int imgRow = firstImageRow; imgRow < lastImageRow; ++imgRow) {
                for (int x = renderWindow.x1; x < renderWindow.x2; ++x) {
                    int srcIdx = (imgRow * outputImageData.width + x) * outputImageData.channels;
                    for (int c = 0; c < outputImageData.channels; ++c) {
                        extractedPixels.push_back(outputImageData.pixels[srcIdx + c]);
                    }
                }
            }

            regionData.pixels = std::move(extractedPixels);

            if (_logger) {
                _logger->debug("Frame {}: Extracted sub-region ({},{}) to ({},{}) from cached image",
                              static_cast<int>(args.time),
                              renderWindow.x1, renderWindow.y1,
                              renderWindow.x2, renderWindow.y2);
            }
        } else {
            // Resolution mismatch - output from workflow has different size than render window
            // This can happen when the workflow includes resize operations
            if (_logger) {
                _logger->info("Frame {}: Output resolution ({}x{}) differs from render window ({}x{}). "
                             "Handling resolution mismatch...",
                             frame, outputImageData.width, outputImageData.height,
                             renderWidth, renderHeight);
            }

            // Strategy: crop or pad to fit the render window in top-left (EXR) layout.
            // Y-orientation for the OFX dst buffer is handled exclusively by the
            // toOFXBuffer(..., shouldFlipYForOFX()) call below — flipping here too
            // would double-flip on hosts where the flag is true (Resolve, Flame, Nuke).
            regionData.width = renderWidth;
            regionData.height = renderHeight;

            std::vector<float> resizedPixels(renderWidth * renderHeight * outputImageData.channels, 0.0f);

            // Calculate copy dimensions (intersection of output and render window)
            int copyWidth = std::min(outputImageData.width, renderWidth);
            int copyHeight = std::min(outputImageData.height, renderHeight);

            // Center the output in the render window if sizes differ
            int offsetX = (renderWidth - outputImageData.width) / 2;
            int offsetY = (renderHeight - outputImageData.height) / 2;
            offsetX = std::max(0, offsetX);
            offsetY = std::max(0, offsetY);

            // Copy pixels from output (top-left) to centered position in render window (top-left).
            // No Y-flip here — toOFXBuffer below applies the host-appropriate flip.
            for (int y = 0; y < copyHeight; ++y) {
                for (int x = 0; x < copyWidth; ++x) {
                    int srcIdx = (y * outputImageData.width + x) * outputImageData.channels;
                    int dstY = y + offsetY;
                    int dstX = x + offsetX;
                    int dstIdx = (dstY * renderWidth + dstX) * outputImageData.channels;

                    if (dstIdx >= 0 && dstIdx + outputImageData.channels <= static_cast<int>(resizedPixels.size())) {
                        for (int c = 0; c < outputImageData.channels; ++c) {
                            resizedPixels[dstIdx + c] = outputImageData.pixels[srcIdx + c];
                        }
                    }
                }
            }

            regionData.pixels = std::move(resizedPixels);

            if (_logger) {
                _logger->info("Frame {}: Fitted output ({}x{}) into render window ({}x{}) at offset ({},{}) — top-left layout, OFX flip deferred to toOFXBuffer",
                             frame, copyWidth, copyHeight, renderWidth, renderHeight, offsetX, offsetY);
            }
        }
    }

    // Convert to OFX buffer
    int dstPixelComponents = dst->getPixelComponentCount();
    OFX::BitDepthEnum dstBitDepth = dst->getPixelDepth();
    int dstBitDepthInt = 32;
    if (dstBitDepth == OFX::eBitDepthUByte) dstBitDepthInt = 8;
    else if (dstBitDepth == OFX::eBitDepthUShort) dstBitDepthInt = 16;

    // Verify dimensions match before copying to OFX buffer
    if (regionData.width != renderWidth || regionData.height != renderHeight) {
        std::ostringstream error;
        error << "Internal error: Region data dimensions (" << regionData.width << "x" << regionData.height
              << ") do not match render window (" << renderWidth << "x" << renderHeight << ")";
        if (_logger) _logger->error("Frame {}: {}", frame, error.str());
        throw std::runtime_error(error.str());
    }

    // Diagnostic: sample regionData corners (top-left layout) before OFX write.
    // Pair this with readEXR's corner log + the post-write OFX corner log below
    // to triangulate any orientation issue end-to-end.
    if (_logger) {
        auto sampleR = [&](int x, int y) {
            int i = (y * regionData.width + x) * regionData.channels;
            return std::string("[") +
                   std::to_string(regionData.pixels[i + 0]) + "," +
                   std::to_string(regionData.pixels[i + 1]) + "," +
                   std::to_string(regionData.pixels[i + 2]) + "," +
                   std::to_string(regionData.channels >= 4 ? regionData.pixels[i + 3] : 1.0f) + "]";
        };
        _logger->info("Frame {}: regionData corners (top-left layout, before toOFXBuffer): "
                      "TL={} TR={} BL={} BR={}",
                      frame,
                      sampleR(0, 0),
                      sampleR(regionData.width - 1, 0),
                      sampleR(0, regionData.height - 1),
                      sampleR(regionData.width - 1, regionData.height - 1));
    }

    bool flipY = shouldFlipYForOFX();
    ImageIO::toOFXBuffer(regionData, dst->getPixelData(), dst->getRowBytes(),
                         dstPixelComponents, dstBitDepthInt, flipY);

    // Diagnostic: sample dst corners (in OFX buffer-memory layout — NOT image coords).
    // Reading the float buffer directly assumes 32-bit float; only sample for that
    // case to avoid misinterpreting 8/16-bit pixels.
    if (_logger && dstBitDepthInt == 32) {
        const float* dstF = static_cast<const float*>(dst->getPixelData());
        if (dstF) {
            int rb = dst->getRowBytes();
            int floatsPerRow = rb / sizeof(float);  // = renderWidth * dstPixelComponents (typically)
            auto sampleD = [&](int x, int y) {
                long base = static_cast<long>(y) * floatsPerRow + static_cast<long>(x) * dstPixelComponents;
                return std::string("[") +
                       std::to_string(dstF[base + 0]) + "," +
                       (dstPixelComponents > 1 ? std::to_string(dstF[base + 1]) : "_") + "," +
                       (dstPixelComponents > 2 ? std::to_string(dstF[base + 2]) : "_") + "," +
                       (dstPixelComponents > 3 ? std::to_string(dstF[base + 3]) : "_") + "]";
            };
            _logger->info("Frame {}: dst corners (OFX buffer memory order, after toOFXBuffer flipY={}): "
                          "row0_x0={} row0_xMax={} rowMax_x0={} rowMax_xMax={}",
                          frame, flipY,
                          sampleD(0, 0),
                          sampleD(renderWidth - 1, 0),
                          sampleD(0, renderHeight - 1),
                          sampleD(renderWidth - 1, renderHeight - 1));
            _logger->info("  Reading: row0 = OFX y=0 = {} of displayed image",
                          flipY ? "BOTTOM (host expects bottom-left)" : "TOP (host expects top-left)");
        }
    }
}

void BasePlugin::onJobComplete(int frame, bool success)
{
    if (_logger) {
        _logger->info("========================================");
        _logger->info("Job completion callback: Frame {} - {}", frame, success ? "SUCCESS" : "FAILED");
        _logger->info("========================================");
    }

    if (!success) {
        if (_jobManager) {
            std::string error = _jobManager->getJobError(frame);
            if (_logger) _logger->error("Frame {} error: {}", frame, error);
        }
        return;
    }

    // SUCCESS - Add output file(s) to in-memory cache for fast future lookups.
    // For sequence plugins, seed every frame in [_sequenceStartFrame.._sequenceEndFrame]
    // so subsequent render() calls hit the in-memory cache directly without a filesystem stat.
    int seedStart = isSequencePlugin() ? _sequenceStartFrame : frame;
    int seedEnd   = isSequencePlugin() ? _sequenceEndFrame   : frame;
    {
        std::lock_guard<std::mutex> lock(_cacheMutex);
        for (int t = seedStart; t <= seedEnd; ++t) {
            _cacheFileExists.insert(constructExpectedOutputPath(t));
        }
    }
    if (_logger) {
        if (isSequencePlugin()) {
            _logger->info("Sequence job complete: seeded in-memory cache for frames {}-{}", seedStart, seedEnd);
        } else {
            _logger->info("Frame {}: Added to in-memory cache: {}", frame, constructExpectedOutputPath(frame));
        }
    }

    // SUCCESS - Invalidate host cache to trigger re-render for all affected frames.
    // For sequence plugins, fire the refresh trigger at every frame so the host
    // knows to re-render the full sequence (not just startFrame).
    try {
        for (int t = seedStart; t <= seedEnd; ++t) {
            double currentValue = 0.0;
            _refreshTrigger->getValueAtTime(t, currentValue);
            _refreshTrigger->setValueAtTime(t, currentValue + 0.001);
        }
        if (_logger) {
            _logger->info("Cache invalidation triggered for frames {}-{}", seedStart, seedEnd);
        }
    } catch (const std::exception& e) {
        if (_logger) {
            _logger->error("Frame {}: Failed to invalidate cache: {}", frame, e.what());
        }
    }

    // Update job status display
    updateJobStatusDisplay();
}

void BasePlugin::updateJobStatusDisplay()
{
    if (!_jobManager || !_jobStatus || !_jobStatusColor) {
        return;
    }

    try {
        auto allJobs  = _jobManager->getAllJobs();
        auto failedFrames = _jobManager->getFailedFrames();
        int  pendingCount = _jobManager->getPendingJobCount();

        std::string statusText;
        double r = 0.5, g = 0.5, b = 0.5;  // gray = idle

        // Helper: extract the short human-readable exception message from a ComfyUI
        // error blob (which is a long JSON string).
        auto extractError = [](const std::string& raw) -> std::string {
            const std::string key = "\"exception_message\":\"";
            auto pos = raw.find(key);
            if (pos != std::string::npos) {
                pos += key.size();
                auto end = raw.find('"', pos);
                std::string msg = (end != std::string::npos) ? raw.substr(pos, end - pos)
                                                              : raw.substr(pos, 120);
                // trim trailing \n
                while (!msg.empty() && (msg.back() == '\n' || msg.back() == '\r')) msg.pop_back();
                return msg;
            }
            return raw.substr(0, 100);
        };

        // ---- Build status based on plugin type ----
        bool foundActive = false;

        if (isSequencePlugin()) {
            // ================================================================
            // SEQUENCE PLUGIN — one job, multi-phase (write → submit → poll)
            // ================================================================
            for (const auto& [frame, job] : allJobs) {
                if (job.status == JobStatus::FAILED)    continue;
                if (job.status == JobStatus::COMPLETED) continue;
                if (job.status == JobStatus::CANCELLED) continue;

                foundActive = true;
                int nFrames = (_sequenceEndFrame >= _sequenceStartFrame)
                              ? (_sequenceEndFrame - _sequenceStartFrame + 1) : 1;

                if (job.submissionStatus == SubmissionStatus::PENDING_WRITE) {
                    r = 1.0; g = 0.55; b = 0.0;  // orange
                    statusText = "Writing " + std::to_string(nFrames) + " frame(s) to disk...";
                } else if (job.submissionStatus == SubmissionStatus::PENDING_SUBMIT) {
                    r = 1.0; g = 0.75; b = 0.0;  // amber
                    statusText = "Submitting workflow to ComfyUI...";
                } else {
                    auto elapsed = static_cast<int>(
                        std::chrono::duration_cast<std::chrono::seconds>(
                            std::chrono::steady_clock::now() - job.submittedTime).count());
                    r = 1.0; g = 0.9; b = 0.1;  // yellow
                    statusText = "ComfyUI processing " + std::to_string(nFrames) +
                                 " frame(s) — " + std::to_string(elapsed) + "s" +
                                 " (poll " + std::to_string(job.pollCount) + ")";
                }
                break;  // only one sequence job at a time
            }

            // Idle / done for sequence
            if (!foundActive && failedFrames.empty()) {
                int completedCount = 0;
                for (const auto& [frame, job] : allJobs)
                    if (job.status == JobStatus::COMPLETED) ++completedCount;
                if (completedCount > 0) {
                    r = 0.0; g = 0.8; b = 0.2;  // green
                    int nFrames = (_sequenceEndFrame >= _sequenceStartFrame)
                                  ? (_sequenceEndFrame - _sequenceStartFrame + 1)
                                  : completedCount;
                    statusText = std::to_string(nFrames) + " frame(s) done";
                } else {
                    r = 0.5; g = 0.5; b = 0.5;
                    statusText = "Ready";
                }
            }

        } else {
            // ================================================================
            // FRAME-BASED PLUGIN — multiple independent frames in flight
            // ================================================================

            // Gather active (pending) jobs, sorted by frame number
            struct ActiveInfo {
                int frame;
                int elapsed;   // seconds since submission
                int pollCount;
            };
            std::vector<ActiveInfo> active;
            int completedCount = 0;

            for (const auto& [frame, job] : allJobs) {
                if (job.status == JobStatus::FAILED || job.status == JobStatus::CANCELLED) continue;
                if (job.status == JobStatus::COMPLETED) { ++completedCount; continue; }
                // QUEUED or PROCESSING
                auto elapsed = static_cast<int>(
                    std::chrono::duration_cast<std::chrono::seconds>(
                        std::chrono::steady_clock::now() - job.submittedTime).count());
                active.push_back({frame, elapsed, job.pollCount});
            }

            if (!active.empty()) {
                foundActive = true;
                r = 1.0; g = 0.9; b = 0.1;  // yellow

                // Show details for up to 3 frames; summarise the rest
                std::string frameList;
                int shown = static_cast<int>(std::min(active.size(), size_t(3)));
                for (int i = 0; i < shown; ++i) {
                    if (i > 0) frameList += "  |  ";
                    frameList += "fr." + std::to_string(active[i].frame) +
                                 " " + std::to_string(active[i].elapsed) + "s" +
                                 " p" + std::to_string(active[i].pollCount);
                }
                int extra = static_cast<int>(active.size()) - shown;
                if (extra > 0) frameList += "  (+" + std::to_string(extra) + " more)";

                statusText = "Processing: " + frameList;

            } else if (!failedFrames.empty()) {
                // handled in the shared failure block below

            } else if (completedCount > 0) {
                r = 0.0; g = 0.8; b = 0.2;  // green
                statusText = std::to_string(completedCount) + " frame(s) done";
            } else {
                r = 0.5; g = 0.5; b = 0.5;
                statusText = "Ready";
            }
        }

        // ---- Failures override everything (both plugin types) ----
        if (!failedFrames.empty()) {
            r = 1.0; g = 0.0; b = 0.0;
            std::string errMsg;
            for (const auto& [frame, job] : allJobs) {
                if (job.status == JobStatus::FAILED) {
                    errMsg = extractError(job.errorMessage);
                    // Prefix with frame number for frame-based plugins
                    if (!isSequencePlugin())
                        errMsg = "fr." + std::to_string(job.frame) + ": " + errMsg;
                    break;
                }
            }
            statusText = "Failed — " + errMsg;
            foundActive = false;
        }

        // Log only when status changes
        static std::string lastStatusText;
        if (_logger && statusText != lastStatusText) {
            _logger->info("Job status: {}", statusText);
            lastStatusText = statusText;
        }

        try { _jobStatus->setValue(statusText); }
        catch (const std::exception& e) {
            if (_logger) _logger->error("Failed to update job status text: {}", e.what());
        }
        try { _jobStatusColor->setValue(r, g, b); }
        catch (const std::exception& e) {
            if (_logger) _logger->error("Failed to update job status color: {}", e.what());
        }

    } catch (const std::exception& e) {
        if (_logger) _logger->error("Failed to update job status: {}", e.what());
    }
}

void BasePlugin::renderCheckerboard(OFX::Image* dst)
{
    if (!dst) return;

    OfxRectI bounds = dst->getBounds();
    int width = bounds.x2 - bounds.x1;
    int height = bounds.y2 - bounds.y1;
    int rowBytes = dst->getRowBytes();
    int pixelComponents = dst->getPixelComponentCount();
    OFX::BitDepthEnum bitDepth = dst->getPixelDepth();

    // Calculate bytes per pixel component
    int bytesPerComponent = 1;  // default for UByte
    if (bitDepth == OFX::eBitDepthUShort) {
        bytesPerComponent = 2;
    } else if (bitDepth == OFX::eBitDepthFloat) {
        bytesPerComponent = 4;
    }

    unsigned char* pixels = static_cast<unsigned char*>(dst->getPixelData());
    if (!pixels) return;

    // Checkerboard pattern (32x32 squares for better visibility)
    int squareSize = 32;

    for (int y = 0; y < height; ++y) {
        unsigned char* row = pixels + y * rowBytes;
        for (int x = 0; x < width; ++x) {
            bool dark = ((x / squareSize) + (y / squareSize)) % 2 == 0;
            float value = dark ? 0.4f : 0.6f;

            unsigned char* pixel = row + x * pixelComponents * bytesPerComponent;

            if (bitDepth == OFX::eBitDepthUByte) {
                unsigned char byteValue = static_cast<unsigned char>(value * 255);
                // Set RGB/color channels
                for (int c = 0; c < std::min(3, pixelComponents); ++c) {
                    pixel[c] = byteValue;
                }
                // Set alpha to fully opaque if RGBA
                if (pixelComponents == 4) {
                    pixel[3] = 255;
                }
            } else if (bitDepth == OFX::eBitDepthUShort) {
                unsigned short* shortPixel = reinterpret_cast<unsigned short*>(pixel);
                unsigned short shortValue = static_cast<unsigned short>(value * 65535);
                // Set RGB/color channels
                for (int c = 0; c < std::min(3, pixelComponents); ++c) {
                    shortPixel[c] = shortValue;
                }
                // Set alpha to fully opaque if RGBA
                if (pixelComponents == 4) {
                    shortPixel[3] = 65535;
                }
            } else {  // float
                float* floatPixel = reinterpret_cast<float*>(pixel);
                // Set RGB/color channels
                for (int c = 0; c < std::min(3, pixelComponents); ++c) {
                    floatPixel[c] = value;
                }
                // Set alpha to fully opaque if RGBA
                if (pixelComponents == 4) {
                    floatPixel[3] = 1.0f;
                }
            }
        }
    }
}

void BasePlugin::renderSolidColor(OFX::Image* dst, double r, double g, double b)
{
    if (!dst) return;

    OfxRectI bounds = dst->getBounds();
    int width = bounds.x2 - bounds.x1;
    int height = bounds.y2 - bounds.y1;
    int rowBytes = dst->getRowBytes();
    int pixelComponents = dst->getPixelComponentCount();
    OFX::BitDepthEnum bitDepth = dst->getPixelDepth();

    // Calculate bytes per pixel component
    int bytesPerComponent = 1;  // default for UByte
    if (bitDepth == OFX::eBitDepthUShort) {
        bytesPerComponent = 2;
    } else if (bitDepth == OFX::eBitDepthFloat) {
        bytesPerComponent = 4;
    }

    unsigned char* pixels = static_cast<unsigned char*>(dst->getPixelData());

    for (int y = 0; y < height; ++y) {
        unsigned char* row = pixels + y * rowBytes;
        for (int x = 0; x < width; ++x) {
            unsigned char* pixel = row + x * pixelComponents * bytesPerComponent;

            if (bitDepth == OFX::eBitDepthUByte) {
                pixel[0] = static_cast<unsigned char>(r * 255);
                if (pixelComponents > 1) pixel[1] = static_cast<unsigned char>(g * 255);
                if (pixelComponents > 2) pixel[2] = static_cast<unsigned char>(b * 255);
                if (pixelComponents > 3) pixel[3] = 255;  // Alpha = 1.0
            } else if (bitDepth == OFX::eBitDepthUShort) {
                unsigned short* shortPixel = reinterpret_cast<unsigned short*>(pixel);
                shortPixel[0] = static_cast<unsigned short>(r * 65535);
                if (pixelComponents > 1) shortPixel[1] = static_cast<unsigned short>(g * 65535);
                if (pixelComponents > 2) shortPixel[2] = static_cast<unsigned short>(b * 65535);
                if (pixelComponents > 3) shortPixel[3] = 65535;
            } else {  // float
                float* floatPixel = reinterpret_cast<float*>(pixel);
                floatPixel[0] = static_cast<float>(r);
                if (pixelComponents > 1) floatPixel[1] = static_cast<float>(g);
                if (pixelComponents > 2) floatPixel[2] = static_cast<float>(b);
                if (pixelComponents > 3) floatPixel[3] = 1.0f;
            }
        }
    }
}

int BasePlugin::findLastValidFrame(double currentTime)
{
    int currentFrame = static_cast<int>(currentTime);

    // Search backwards for the nearest completed frame
    for (int f = currentFrame - 1; f >= currentFrame - 100 && f >= 0; --f) {
        std::string path = constructExpectedOutputPath(f);
        std::ifstream testFile(path);
        if (testFile.good()) {
            testFile.close();
            return f;
        }
    }

    return -1;  // No valid frame found
}

bool BasePlugin::shouldFlipYForOFX() const
{
    // User parameter: 0=Auto, 1=Always flip, 2=Never flip.
    // Auto: hostname heuristic — known top-left pixel-buffer hosts skip the flip;
    // everything else flips. We do NOT use kOfxImageEffectHostPropNativeOrigin here:
    // per the OFX 1.4 spec that property is "only a UI hint" for overlay drawing
    // and "has no impact on pixel processing" — origins for UI and for pixels are
    // independent. The hostname table below is empirically calibrated against
    // actual on-disk EXR orientation.
    //
    // Note on Resolve vs Fusion: DaVinci Resolve hosts two distinct OFX endpoints
    // in the same process — "DaVinciResolve" (Edit/Color page) ships pixels in
    // OFX-spec bottom-left memory order, while "com.blackmagicdesign.Fusion"
    // (Fusion page) ships top-left. Treat them separately.
    int mode = 0;
    if (_flipYMode) {
        try { _flipYMode->getValue(mode); } catch (...) { mode = 0; }
    }
    if (mode == 1) return true;
    if (mode == 2) return false;

    bool result = true;
    const char* reason = "unknown host (default to OFX-spec bottom-left)";
    OFX::ImageEffectHostDescription* hd = OFX::getImageEffectHostDescription();
    if (!hd) {
        result = true;
        reason = "host description unavailable";
    } else {
        const std::string& name = hd->hostName;
        if      (name.find("Fusion")   != std::string::npos) { result = false; reason = "Fusion (top-left)"; }
        else if (name.find("Premiere") != std::string::npos) { result = false; reason = "Premiere (top-left)"; }
        else                                                  { result = true;  reason = name.c_str(); }
    }

    // Log the decision exactly once per instance so the .log shows what was chosen
    // without flooding with a per-frame line.
    if (!_flipDecisionLogged) {
        _flipDecisionLogged = true;
        if (_logger) {
            _logger->info("shouldFlipYForOFX(): decision = {} (reason: {})",
                          result ? "FLIP (host buffer is bottom-left)" : "NO FLIP (host buffer is top-left)",
                          reason);
        }
    }
    return result;
}

// ============================================================================
// Parameter Description
// ============================================================================

void BasePlugin::describeCommonParameters(OFX::ImageEffectDescriptor &desc,
                                          OFX::ContextEnum /*context*/,
                                          OFX::PageParamDescriptor *page,
                                          OFX::PageParamDescriptor * /*unused2*/,
                                          OFX::PageParamDescriptor * /*unused3*/,
                                          const json* configDefaults,
                                          bool skipGroupHeaders,
                                          bool isSequencePlugin)
{
    // Log config loading status
    auto logger = spdlog::get("comfyui_plugin");
    if (logger) {
        if (configDefaults && !configDefaults->empty()) {
            logger->info("=== Using config defaults from JSON ===");
            logger->info("Config contents: {}", configDefaults->dump(2));
        } else {
            logger->warn("=== No config defaults available, using hardcoded values ===");
        }
    }

    // Single page with groups for Flame compatibility

    // ==== PROJECT GROUP ====
    OFX::GroupParamDescriptor *projectGroup = desc.defineGroupParam("projectGroup");
    projectGroup->setLabel("Project");
    projectGroup->setOpen(true);
    if (!skipGroupHeaders) page->addChild(*projectGroup);

    // Project name (REQUIRED parameter)
    OFX::StringParamDescriptor *project = desc.defineStringParam("projectName");
    project->setLabel("Project Name (REQUIRED)");
    project->setHint("REQUIRED: Project name for organizing files (in/<PROJECT>/<WORKFLOW>/). Plugin will not process if empty.");
    project->setDefault("");  // No default - user must set this
    project->setEvaluateOnChange(true);  // Trigger updates when changed
    project->setParent(*projectGroup);
    page->addChild(*project);

    // Visual status indicator - RED when empty, BLACK when set
    OFX::RGBParamDescriptor *projectIndicator = desc.defineRGBParam("projectNameIndicator");
    projectIndicator->setLabel("Project Status");
    projectIndicator->setHint("RED = Project name required. BLACK = Project name is set.");
    projectIndicator->setDefault(1.0, 0.0, 0.0);  // Start with RED (warning)
    projectIndicator->setAnimates(false);  // Not animatable
    projectIndicator->setEvaluateOnChange(false);  // Don't trigger renders
    projectIndicator->setParent(*projectGroup);
    page->addChild(*projectIndicator);

    OFX::StringParamDescriptor *workflow = desc.defineStringParam("workflowName");
    workflow->setLabel("Workflow Name");
    workflow->setHint("Workflow/effect name for file organization (e.g., 'segmentation', 'upscale')");
    // Use config default if available, otherwise fallback to generic placeholder
    // Specialized plugins should override this with their effect name
    std::string workflowDefault = "effect";
    if (configDefaults && configDefaults->contains("project") && (*configDefaults)["project"].contains("workflowName")) {
        workflowDefault = (*configDefaults)["project"]["workflowName"].get<std::string>();
        if (logger) {
            logger->info("workflowName: Using config value: '{}'", workflowDefault);
        }
    } else {
        if (logger) {
            logger->info("workflowName: Using hardcoded default: '{}'", workflowDefault);
        }
    }
    workflow->setDefault(workflowDefault.c_str());
    workflow->setParent(*projectGroup);
    page->addChild(*workflow);

    OFX::StringParamDescriptor *version = desc.defineStringParam("outputVersion");
    version->setLabel("Output Version");
    version->setHint("Version identifier for output files (e.g., 'v001', 'v002')");
    // Use config default if available, otherwise fallback to hardcoded default
    std::string versionDefault = "v001";
    if (configDefaults && configDefaults->contains("project") && (*configDefaults)["project"].contains("outputVersion")) {
        versionDefault = (*configDefaults)["project"]["outputVersion"].get<std::string>();
    }
    version->setDefault(versionDefault.c_str());
    version->setParent(*projectGroup);
    page->addChild(*version);

    OFX::StringParamDescriptor *workflowFile = desc.defineStringParam("workflowFilePath");
    workflowFile->setLabel("Workflow File");
    workflowFile->setHint("Path to workflow JSON file. Use 'resources/workflow/<name>.json' for the bundled workflow, or an absolute path for a custom workflow. Leave empty to use the plugin's built-in hardcoded workflow.");
    workflowFile->setStringType(OFX::eStringTypeFilePath);
    // Neutral fallback: the per-plugin defaults-project.json supplies the correct
    // bundled workflow path via config. If config can't be found, default to empty
    // (use the built-in hardcoded workflow) rather than a bogus path that would
    // log "could not resolve workflow path" every job.
    std::string workflowFileDefault = "";
    if (configDefaults && configDefaults->contains("project") && (*configDefaults)["project"].contains("workflowFile")) {
        workflowFileDefault = (*configDefaults)["project"]["workflowFile"].get<std::string>();
    }
    workflowFile->setDefault(workflowFileDefault.c_str());
    workflowFile->setParent(*projectGroup);
    page->addChild(*workflowFile);

    // ==== PROCESSING GROUP ====
    OFX::GroupParamDescriptor *processingGroup = desc.defineGroupParam("processingGroup");
    processingGroup->setLabel("ComfyUI Processing");
    processingGroup->setOpen(true);
    if (!skipGroupHeaders) page->addChild(*processingGroup);

    // Sequence plugins: one-shot button — collect all frames then submit the batch.
    // Frame-based plugins: toggle — enables automatic per-frame submission during render.
    // Both controls live under the same "ComfyUI Processing" group so their names
    // don't need to repeat "ComfyUI".
    if (isSequencePlugin) {
        OFX::PushButtonParamDescriptor *collectBtn = desc.definePushButtonParam("collectAndSubmit");
        collectBtn->setLabel("Collect & Process");
        collectBtn->setHint("Fetch all frames from the timeline and submit the full sequence to ComfyUI for processing.");
        collectBtn->setParent(*processingGroup);
        page->addChild(*collectBtn);
    } else {
        OFX::BooleanParamDescriptor *enableProcessing = desc.defineBooleanParam("enableProcessing");
        enableProcessing->setLabel("Enable Processing");
        enableProcessing->setHint("Enable ComfyUI processing. When ON, each rendered frame is automatically submitted to ComfyUI. When OFF, input passes through unchanged. Keep OFF during initial setup to avoid blocking the UI.");
        bool enableProcessingDefault = false;
        if (configDefaults && configDefaults->contains("controls") && (*configDefaults)["controls"].contains("enableProcessing")) {
            enableProcessingDefault = (*configDefaults)["controls"]["enableProcessing"].get<bool>();
        }
        enableProcessing->setDefault(enableProcessingDefault);
        enableProcessing->setAnimates(false);
        enableProcessing->setParent(*processingGroup);
        page->addChild(*enableProcessing);
    }

    // Async rendering mode (hidden - always non-blocking)
    OFX::ChoiceParamDescriptor *asyncMode = desc.defineChoiceParam("asyncMode");
    asyncMode->setLabel("Rendering Mode");
    asyncMode->setHint("Blocking: Traditional behavior (UI freezes, waits for result). "
                       "Non-Blocking: Progressive rendering (returns placeholder immediately, updates when ready)");
    asyncMode->appendOption("Blocking (Wait for Result)");
    asyncMode->appendOption("Non-Blocking (Progressive)");
    // Use config default if available, otherwise fallback to hardcoded default
    int asyncModeDefault = 1;
    if (configDefaults && configDefaults->contains("controls") && (*configDefaults)["controls"].contains("asyncMode")) {
        asyncModeDefault = (*configDefaults)["controls"]["asyncMode"].get<int>();
    }
    asyncMode->setDefault(asyncModeDefault);
    asyncMode->setAnimates(false);
    asyncMode->setIsSecret(true);  // Hidden from UI
    asyncMode->setParent(*processingGroup);
    // page->addChild(*asyncMode);  // Removed from UI

    // EXR Y-flip override — OFX hosts disagree on pixel-buffer orientation.
    // If your output displays upside-down or input EXRs land flipped on disk,
    // change this from Auto to the opposite mode. After changing, delete the
    // cached input EXR folder so the plugin re-writes with the new orientation.
    OFX::ChoiceParamDescriptor *flipYMode = desc.defineChoiceParam("flipYMode");
    flipYMode->setLabel("Flip Y for EXR");
    flipYMode->setHint("Y-axis handling for EXR I/O. Auto: detect by host name "
                       "(Fusion/Premiere skip flip; Resolve/Flame/Nuke/others flip per OFX spec). "
                       "Always: force flip. Never: skip flip. "
                       "Toggle if input EXRs on disk are upside-down or output displays flipped. "
                       "Delete cached input EXRs after changing.");
    flipYMode->appendOption("Auto (detect by host)");
    flipYMode->appendOption("Always Flip");
    flipYMode->appendOption("Never Flip");
    flipYMode->setDefault(0);
    flipYMode->setAnimates(false);
    flipYMode->setParent(*processingGroup);

    // Placeholder display mode (hidden - always checkerboard)
    OFX::ChoiceParamDescriptor *placeholderMode = desc.defineChoiceParam("placeholderMode");
    placeholderMode->setLabel("Placeholder Display");
    placeholderMode->setHint("What to show while ComfyUI is processing in non-blocking mode");
    placeholderMode->appendOption("Source Passthrough");
    placeholderMode->appendOption("Checkerboard Pattern");
    placeholderMode->appendOption("Gray Frame");
    placeholderMode->appendOption("Last Valid Result");
    // Use config default if available, otherwise fallback to hardcoded default
    int placeholderModeDefault = 1;
    if (configDefaults && configDefaults->contains("controls") && (*configDefaults)["controls"].contains("placeholderMode")) {
        placeholderModeDefault = (*configDefaults)["controls"]["placeholderMode"].get<int>();
    }
    placeholderMode->setDefault(placeholderModeDefault);
    placeholderMode->setAnimates(false);
    placeholderMode->setIsSecret(true);  // Hidden from UI
    placeholderMode->setParent(*processingGroup);
    // page->addChild(*placeholderMode);  // Removed from UI

    // Job status display (read-only)
    OFX::StringParamDescriptor *jobStatus = desc.defineStringParam("jobStatus");
    jobStatus->setLabel("Status");
    jobStatus->setHint("Current processing status — updates automatically as jobs progress.");
    jobStatus->setStringType(OFX::eStringTypeLabel);  // Read-only label
    jobStatus->setDefault("Ready");
    jobStatus->setEnabled(false);  // Read-only
    jobStatus->setParent(*processingGroup);
    page->addChild(*jobStatus);

    // Job status color indicator (visual feedback)
    OFX::RGBParamDescriptor *jobStatusColor = desc.defineRGBParam("jobStatusColor");
    jobStatusColor->setLabel("Status Color");
    jobStatusColor->setHint(
        "Visual status indicator (updates automatically):\n"
        "• Cyan:   Collecting frames from timeline\n"
        "• Orange: Writing input EXRs to disk\n"
        "• Amber:  Submitting workflow to ComfyUI\n"
        "• Yellow: ComfyUI processing\n"
        "• Green:  All frames ready\n"
        "• Red:    Error / job failed\n"
        "• Gray:   Idle / no jobs"
    );
    jobStatusColor->setDefault(0.5, 0.5, 0.5);  // Gray by default
    jobStatusColor->setAnimates(false);  // Not animatable (updates programmatically)
    jobStatusColor->setEvaluateOnChange(false);  // Don't trigger renders when color changes
    // NOTE: NOT using setEnabled(false) - that might block programmatic updates in some hosts
    jobStatusColor->setParent(*processingGroup);
    page->addChild(*jobStatusColor);

    // Hidden refresh trigger for cache invalidation
    OFX::DoubleParamDescriptor *refreshTrigger = desc.defineDoubleParam("refreshTrigger");
    refreshTrigger->setLabel("Refresh Trigger");
    refreshTrigger->setIsSecret(true);  // Hidden from UI
    refreshTrigger->setAnimates(true);  // Can be keyframed per-frame
    refreshTrigger->setDefault(0.0);
    refreshTrigger->setParent(*processingGroup);
    page->addChild(*refreshTrigger);

    OFX::BooleanParamDescriptor *enableCache = desc.defineBooleanParam("enableCache");
    enableCache->setLabel("Enable Cache");
    enableCache->setHint("Use ComfyUI's caching system to speed up repeated renders");
    // Use config default if available, otherwise fallback to hardcoded default
    bool enableCacheDefault = true;
    if (configDefaults && configDefaults->contains("controls") && (*configDefaults)["controls"].contains("enableCache")) {
        enableCacheDefault = (*configDefaults)["controls"]["enableCache"].get<bool>();
    }
    enableCache->setDefault(enableCacheDefault);
    enableCache->setParent(*processingGroup);
    page->addChild(*enableCache);

    OFX::IntParamDescriptor *timeout = desc.defineIntParam("timeout");
    timeout->setLabel("Timeout (s)");
    timeout->setHint("Maximum time to wait for ComfyUI processing");
    // Use config default if available, otherwise fallback to hardcoded default
    int timeoutDefault = 300;
    if (configDefaults && configDefaults->contains("controls") && (*configDefaults)["controls"].contains("timeout")) {
        timeoutDefault = (*configDefaults)["controls"]["timeout"].get<int>();
    }
    timeout->setDefault(timeoutDefault);
    timeout->setRange(10, 3600);
    timeout->setDisplayRange(30, 600);
    timeout->setParent(*processingGroup);
    page->addChild(*timeout);

    // ==== SERVER GROUP ====
    OFX::GroupParamDescriptor *serverGroup = desc.defineGroupParam("serverGroup");
    serverGroup->setLabel("Server");
    serverGroup->setOpen(false);  // Collapsed by default
    if (!skipGroupHeaders) page->addChild(*serverGroup);

    OFX::StringParamDescriptor *serverAddr = desc.defineStringParam("serverAddress");
    serverAddr->setLabel("Server Address");
    serverAddr->setHint("Hostname or IP address of ComfyUI server");
    // Use config default if available, otherwise fallback to hardcoded default
    std::string serverAddressDefault = "localhost";
    if (configDefaults && configDefaults->contains("server") && (*configDefaults)["server"].contains("serverAddress")) {
        serverAddressDefault = (*configDefaults)["server"]["serverAddress"].get<std::string>();
        if (logger) {
            logger->info("serverAddress: Using config value: '{}'", serverAddressDefault);
        }
    } else {
        if (logger) {
            logger->info("serverAddress: Using hardcoded default: '{}'", serverAddressDefault);
        }
    }
    serverAddr->setDefault(serverAddressDefault.c_str());
    serverAddr->setParent(*serverGroup);
    page->addChild(*serverAddr);

    OFX::IntParamDescriptor *serverPort = desc.defineIntParam("serverPort");
    serverPort->setLabel("Port");
    serverPort->setHint("ComfyUI server port number");
    // Use config default if available, otherwise fallback to hardcoded default
    int serverPortDefault = 8188;
    if (configDefaults && configDefaults->contains("server") && (*configDefaults)["server"].contains("serverPort")) {
        serverPortDefault = (*configDefaults)["server"]["serverPort"].get<int>();
    }
    serverPort->setDefault(serverPortDefault);
    serverPort->setRange(1, 65535);
    serverPort->setDisplayRange(8000, 9000);
    serverPort->setParent(*serverGroup);
    page->addChild(*serverPort);

    // ==== STORAGE MOUNTS GROUP ====
    // The shared-storage mount paths live in their own group, separate from the
    // ComfyUI server connection above — they describe where the shared storage
    // is mounted on each OS, not the ComfyUI endpoint.
    OFX::GroupParamDescriptor *mountGroup = desc.defineGroupParam("mountGroup");
    mountGroup->setLabel("Storage Mounts");
    mountGroup->setOpen(false);  // Collapsed by default
    if (!skipGroupHeaders) page->addChild(*mountGroup);

    // ---- Shared-storage mounts (two views of the same storage) ------------
    // The plugin and the ComfyUI server are different machines that both touch
    // the EXRs on the same shared storage, mounted at different paths. The
    // "local" mount is this host's view (local EXR I/O); the "server" mount is
    // the ComfyUI box's view (written into the workflow sent to ComfyUI).
    //
    // The local default is the current OS's native view of the share. config's
    // "storage" block may carry per-OS defaults (localMountPath.{macos,windows,
    // linux}); we pick the entry for the platform this bundle was built for.
#if defined(_WIN32)
    const char* kLocalOsKey = "windows";
    std::string localMountDefault = "\\\\HOSTNAME\\share";
#elif defined(__APPLE__)
    const char* kLocalOsKey = "macos";
    std::string localMountDefault = "/Volumes/comfyui-share";
#else
    const char* kLocalOsKey = "linux";
    std::string localMountDefault = "/mnt/comfyui-share";
#endif
    std::string serverMountDefault = "\\\\HOSTNAME\\share";
    if (configDefaults && configDefaults->contains("storage")) {
        const json& storage = (*configDefaults)["storage"];
        if (storage.contains("localMountPath") && storage["localMountPath"].is_object() &&
            storage["localMountPath"].contains(kLocalOsKey)) {
            localMountDefault = storage["localMountPath"][kLocalOsKey].get<std::string>();
        }
        if (storage.contains("serverMountPath")) {
            serverMountDefault = storage["serverMountPath"].get<std::string>();
        }
    }

    OFX::StringParamDescriptor *localMount = desc.defineStringParam("localMountPath");
    localMount->setLabel("Local Storage Mount");
    localMount->setHint("The shared storage as mounted on THIS host — where the plugin reads and writes EXRs "
                        "locally (e.g., /Volumes/comfyui-share on macOS, /mnt/comfyui-share on Linux, "
                        "\\\\server\\share on Windows). Leave blank if this host reaches the storage at the same "
                        "path as the ComfyUI server.");
    localMount->setStringType(OFX::eStringTypeDirectoryPath);
    localMount->setDefault(localMountDefault.c_str());
    localMount->setParent(*mountGroup);
    page->addChild(*localMount);

    OFX::StringParamDescriptor *serverMount = desc.defineStringParam("serverMountPath");
    serverMount->setLabel("ComfyUI Server Mount");
    serverMount->setHint("The same shared storage as mounted on the ComfyUI server (UNC: \\\\server\\share). "
                         "This is the path written into the workflow sent to ComfyUI — the server reads inputs "
                         "and writes outputs through it.");
    serverMount->setDefault(serverMountDefault.c_str());
    serverMount->setParent(*mountGroup);
    page->addChild(*serverMount);
}

} // namespace ComfyUI
