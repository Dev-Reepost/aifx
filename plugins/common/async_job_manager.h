// Copyright OpenFX and contributors to the OpenFX project.
// SPDX-License-Identifier: BSD-3-Clause

#ifndef ASYNC_JOB_MANAGER_H
#define ASYNC_JOB_MANAGER_H

#include <string>
#include <map>
#include <vector>
#include <memory>
#include <mutex>
#include <thread>
#include <atomic>
#include <functional>
#include <chrono>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include "comfyui_image_io.h"

using json = nlohmann::json;

namespace ComfyUI {

// Forward declarations
class Client;
class BasePlugin;

/**
 * @brief Job execution status enum
 */
enum class JobStatus {
    QUEUED,         // Submitted to ComfyUI, waiting in queue
    PROCESSING,     // ComfyUI is actively processing this job
    COMPLETED,      // Job completed successfully, output file exists
    FAILED,         // Job failed due to error
    CANCELLED       // Job was cancelled by user
};

/**
 * @brief Convert JobStatus to string for logging
 */
inline const char* jobStatusToString(JobStatus status) {
    switch (status) {
        case JobStatus::QUEUED: return "QUEUED";
        case JobStatus::PROCESSING: return "PROCESSING";
        case JobStatus::COMPLETED: return "COMPLETED";
        case JobStatus::FAILED: return "FAILED";
        case JobStatus::CANCELLED: return "CANCELLED";
        default: return "UNKNOWN";
    }
}

/**
 * @brief Job submission status (for async submission)
 */
enum class SubmissionStatus {
    PENDING_WRITE,      // Image write in progress (background thread)
    PENDING_SUBMIT,     // Image written, workflow submission in progress
    SUBMITTED           // Successfully submitted to ComfyUI, now monitoring
};

/**
 * @brief Represents a single async rendering job
 */
struct AsyncJob {
    std::string promptId;                                   // ComfyUI prompt ID (returned from queuePrompt)
    int frame;                                              // Frame number being rendered
    std::string outputPath;                                 // Expected output file path
    std::chrono::steady_clock::time_point submittedTime;    // When job was submitted
    JobStatus status;                                       // Current job status
    std::string errorMessage;                               // Error message if failed
    int pollCount;                                          // Number of times we've polled for this job
    SubmissionStatus submissionStatus;                      // Track async submission progress

    AsyncJob()
        : frame(-1)
        , submittedTime(std::chrono::steady_clock::now())
        , status(JobStatus::QUEUED)
        , pollCount(0)
        , submissionStatus(SubmissionStatus::SUBMITTED)  // Legacy jobs are immediately submitted
    {}

    AsyncJob(const std::string& id, int f, const std::string& path)
        : promptId(id)
        , frame(f)
        , outputPath(path)
        , submittedTime(std::chrono::steady_clock::now())
        , status(JobStatus::QUEUED)
        , pollCount(0)
        , submissionStatus(SubmissionStatus::SUBMITTED)  // Legacy jobs are immediately submitted
    {}
};

/**
 * @brief Manages asynchronous ComfyUI job submission and monitoring
 *
 * This class provides non-blocking job execution by:
 * 1. Accepting job submissions without blocking the caller
 * 2. Running a background monitoring thread that polls ComfyUI for completion
 * 3. Notifying via callback when jobs complete
 * 4. Managing job lifecycle (cleanup of old jobs)
 *
 * Thread Safety:
 * - All public methods are thread-safe
 * - Internal job map protected by mutex
 * - Background thread uses proper synchronization
 *
 * Usage Example:
 * @code
 *   AsyncJobManager mgr(comfyClient, logger);
 *   mgr.setCompletionCallback([](int frame, bool success) {
 *       std::cout << "Frame " << frame << " completed: " << success << std::endl;
 *   });
 *
 *   if (!mgr.isJobPending(100)) {
 *       mgr.submitJob(100, workflowJson, "/path/to/output.exr");
 *   }
 *
 *   // Later...
 *   JobStatus status = mgr.getJobStatus(100);
 * @endcode
 */
class AsyncJobManager {
public:
    /**
     * @brief Callback function type for job completion notifications
     *
     * @param frame Frame number that completed
     * @param success True if job completed successfully, false if failed
     */
    using CompletionCallback = std::function<void(int frame, bool success)>;

    /**
     * @brief Callback function type for periodic status updates
     *
     * Called every monitoring iteration (adaptive: 0.5-5s depending on activity).
     * Useful for refreshing job status without waiting for job completion.
     */
    using StatusUpdateCallback = std::function<void()>;

    /**
     * @brief Constructor
     *
     * @param comfyClient Pointer to ComfyUI client (must outlive this object)
     * @param logger Pointer to spdlog logger (can be nullptr)
     */
    AsyncJobManager(Client* comfyClient, std::shared_ptr<spdlog::logger> logger);

    /**
     * @brief Destructor - stops monitoring thread and cleans up resources
     */
    ~AsyncJobManager();

    // Disable copy and move
    AsyncJobManager(const AsyncJobManager&) = delete;
    AsyncJobManager& operator=(const AsyncJobManager&) = delete;
    AsyncJobManager(AsyncJobManager&&) = delete;
    AsyncJobManager& operator=(AsyncJobManager&&) = delete;

    // ========================================================================
    // Job Submission API
    // ========================================================================

    /**
     * @brief Check if a job is pending for the given frame
     *
     * A job is considered "pending" if it's in QUEUED or PROCESSING state.
     *
     * @param frame Frame number to check
     * @return true if job exists and is pending, false otherwise
     */
    bool isJobPending(int frame) const;

    /**
     * @brief Submit a new async job to ComfyUI (LEGACY - BLOCKING)
     *
     * This method:
     * 1. Queues the workflow to ComfyUI server (non-blocking REST call)
     * 2. Stores job metadata in internal queue
     * 3. Returns immediately
     *
     * The background monitor thread will poll for completion.
     *
     * NOTE: The workflow must already contain the input file path,
     * which means writeInputImage() was called BEFORE this method,
     * causing blocking I/O.
     *
     * @param frame Frame number being rendered
     * @param workflow ComfyUI workflow JSON
     * @param outputPath Expected output file path
     * @throws std::runtime_error if submission to ComfyUI fails
     * @deprecated Use submitJobAsync() for truly non-blocking submission
     */
    void submitJob(int frame, const json& workflow, const std::string& outputPath);

    /**
     * @brief Submit a new async job to ComfyUI (TRULY NON-BLOCKING)
     *
     * This method accepts image data directly and performs ALL I/O in a background thread:
     * 1. Copies image data (cheap, in-memory)
     * 2. Returns immediately
     * 3. Background thread: writes input EXR file(s)
     * 4. Background thread: builds workflow with input paths
     * 5. Background thread: submits to ComfyUI
     * 6. Monitor thread: polls for completion
     *
     * This ensures renderAsync() returns INSTANTLY with no blocking I/O.
     *
     * @param frame Frame number being rendered
     * @param imageDataMap Map of input ID to source image data (e.g., "InputA" -> ImageData)
     *                     Will be copied to avoid lifetime issues
     * @param inputPathMap Map of input ID to file path (e.g., "InputA" -> "/path/to/input.exr")
     * @param outputPath Expected path where output file will be written
     * @param basePlugin Pointer to BasePlugin (for buildWorkflow callback)
     */
    void submitJobAsync(int frame,
                       const std::map<std::string, ImageData>& imageDataMap,
                       const std::map<std::string, std::string>& inputPathMap,
                       const std::string& outputPath,
                       BasePlugin* basePlugin);

    /**
     * @brief Submit a sequence job with pre-fetched frame data (NON-BLOCKING)
     *
     * All frames have already been fetched on the render thread (safe: fetchImage is
     * only ever called from within OFX render actions).  This method:
     *   1. Registers the job immediately (so the sequence guard sees it as active).
     *   2. Launches a background thread that writes EXRs and submits to ComfyUI.
     *
     * The background thread NEVER calls fetchImage() — it only does disk I/O and HTTP.
     *
     * @param startFrame      First frame of the sequence (used as job key)
     * @param frameData       Pre-fetched pixel data: frame number → ImageData
     *                        Frames already on disk should be omitted (cache check done
     *                        by the caller before fetching).
     * @param inputFolder     Folder path for input EXR files
     * @param basename        File basename (without frame number or extension)
     * @param inputPathMap    Map passed to buildWorkflow (e.g. "InputA" → folder)
     * @param firstOutputPath Expected path of the first output EXR (for polling)
     * @param basePlugin      Used for buildWorkflow() callback
     */
    void submitSequenceJobAsync(
        int startFrame,
        std::map<int, ImageData> frameData,
        const std::string& inputFolder,
        const std::string& basename,
        const std::map<std::string, std::string>& inputPathMap,
        const std::string& firstOutputPath,
        BasePlugin* basePlugin);

    /**
     * @brief Get current status of a job
     *
     * @param frame Frame number to check
     * @return JobStatus (COMPLETED if not found in queue)
     */
    JobStatus getJobStatus(int frame) const;

    /**
     * @brief Cancel a pending job
     *
     * Note: This only removes the job from our queue. It does NOT interrupt
     * execution on the ComfyUI server (that requires separate API call).
     *
     * @param frame Frame number to cancel
     * @return true if job was found and cancelled, false if not found
     */
    bool cancelJob(int frame);

    /**
     * @brief Cancel all pending jobs
     *
     * Removes all jobs from the queue and optionally interrupts ComfyUI server.
     *
     * @param interruptServer If true, also send interrupt command to ComfyUI
     */
    void cancelAllJobs(bool interruptServer = false);

    // ========================================================================
    // Callback Management
    // ========================================================================

    /**
     * @brief Set callback to be invoked when jobs complete
     *
     * Callback is invoked from the monitoring thread, so it must be thread-safe.
     * The callback should be fast and non-blocking.
     *
     * @param callback Function to call on completion (can be nullptr to disable)
     */
    void setCompletionCallback(CompletionCallback callback);

    /**
     * @brief Set callback to be invoked periodically for status updates
     *
     * Called every polling interval (adaptive: 0.5-5s) from monitoring thread.
     * Use this to refresh job status information.
     *
     * Callback must be thread-safe and fast (non-blocking).
     *
     * @param callback Function to call periodically (can be nullptr to disable)
     */
    void setStatusUpdateCallback(StatusUpdateCallback callback);

    // ========================================================================
    // Monitoring and Statistics
    // ========================================================================

    /**
     * @brief Get number of jobs currently pending (QUEUED or PROCESSING)
     *
     * @return Count of pending jobs
     */
    int getPendingJobCount() const;

    /**
     * @brief Get list of all pending frame numbers
     *
     * @return Vector of frame numbers with pending jobs
     */
    std::vector<int> getPendingFrames() const;

    /**
     * @brief Get list of failed frame numbers
     *
     * @return Vector of frame numbers with failed jobs
     */
    std::vector<int> getFailedFrames() const;

    /**
     * @brief Get detailed information about all jobs
     *
     * Useful for debugging or status reporting.
     *
     * @return Map of frame → AsyncJob for all tracked jobs
     */
    std::map<int, AsyncJob> getAllJobs() const;

    /**
     * @brief Get error message for a failed job
     *
     * @param frame Frame number
     * @return Error message string (empty if job not failed or not found)
     */
    std::string getJobError(int frame) const;

    // ========================================================================
    // Configuration
    // ========================================================================

    /**
     * @brief Set polling interval for background monitor thread
     *
     * NOTE: This sets BOTH fast and slow intervals to the same value.
     * Use setAdaptivePollingIntervals() for different active/idle rates.
     *
     * @param seconds Polling interval (default: 1.0 second)
     */
    void setPollingInterval(double seconds);

    /**
     * @brief Set adaptive polling intervals for active and idle states
     *
     * The monitor thread will automatically use:
     * - fastInterval when there are pending jobs (responsive updates)
     * - slowInterval when there are no pending jobs (reduce overhead)
     *
     * This provides responsive updates when work is happening while
     * minimizing CPU/network usage when idle.
     *
     * @param fastInterval Polling interval when jobs are active (default: 0.5s)
     * @param slowInterval Polling interval when idle (default: 5.0s)
     */
    void setAdaptivePollingIntervals(double fastInterval, double slowInterval);

    /**
     * @brief Set maximum age for completed jobs before cleanup
     *
     * Completed/failed jobs are automatically removed after this duration
     * to prevent unbounded memory growth.
     *
     * @param seconds Age in seconds (default: 300 = 5 minutes)
     */
    void setJobRetentionTime(double seconds);

    /**
     * @brief Set maximum number of poll attempts before marking job as failed
     *
     * If a job is polled this many times without completing, it's marked as failed.
     * This prevents jobs from staying in queue forever if ComfyUI crashes.
     *
     * @param maxPolls Maximum poll attempts (default: 600 = 10 minutes at 1s interval)
     */
    void setMaxPollAttempts(int maxPolls);

    /**
     * @brief Set the wall-clock deadline (in seconds since job submission)
     *
     * Independent of poll count — guarantees that a long-running but
     * progressing ComfyUI job (e.g. a slow diffusion upscaler) doesn't get
     * killed just because polling went fast. Mirrors the user-facing
     * "Timeout (s)" parameter from the plugin UI.
     *
     * @param seconds Max job duration (<= 0 disables the elapsed-time check)
     */
    void setMaxJobDurationSec(int seconds);

private:
    // ========================================================================
    // Internal Methods
    // ========================================================================

    /**
     * @brief Background monitoring thread function
     *
     * Continuously polls ComfyUI for job status updates until shutdown.
     */
    void monitorThreadFunc();

    /**
     * @brief Check if a specific job has completed
     *
     * Queries ComfyUI history API to check job status.
     * Updates job.status and job.errorMessage if applicable.
     *
     * @param job Job to check (modified in-place)
     * @return true if job completed (success or failure), false if still pending
     */
    bool checkJobCompletion(AsyncJob& job);

    /**
     * @brief Remove old completed/failed jobs to prevent memory growth
     *
     * Called periodically by monitoring thread.
     */
    void cleanupOldJobs();

    /**
     * @brief Verify output file exists for completed job
     *
     * @param path Output file path
     * @return true if file exists, false otherwise
     */
    bool verifyOutputFile(const std::string& path) const;

    // ========================================================================
    // Member Variables
    // ========================================================================

    // Core dependencies
    Client* _comfyClient;                          // ComfyUI REST client (non-owning)
    std::shared_ptr<spdlog::logger> _logger;       // Logger (can be nullptr)

    // Job tracking
    mutable std::mutex _jobsMutex;                 // Protects _jobs map
    std::map<int, AsyncJob> _jobs;                 // frame → job mapping

    // Background monitoring thread
    std::thread _monitorThread;                    // Background polling thread
    std::atomic<bool> _shutdown;                   // Signal to stop monitoring

    // Background sequence write thread (tracked so destructor can join it)
    std::thread _writeThread;                      // Background fetch+write+submit thread
    std::mutex _writeThreadMutex;                  // Protects _writeThread join/replace

    // Completion callback
    std::mutex _callbackMutex;                     // Protects callback
    CompletionCallback _completionCallback;        // User-provided completion handler

    // Status update callback (for periodic job status refresh)
    std::mutex _statusCallbackMutex;               // Protects status callback
    StatusUpdateCallback _statusUpdateCallback;    // User-provided status update handler

    // Configuration
    std::atomic<double> _pollingInterval;          // Seconds between polls (default: 1.0) - LEGACY
    std::atomic<double> _fastPollingInterval;      // Seconds between polls when jobs active (default: 0.5)
    std::atomic<double> _slowPollingInterval;      // Seconds between polls when idle (default: 5.0)
    std::atomic<double> _jobRetentionTime;         // Seconds to keep completed jobs (default: 300)
    std::atomic<int> _maxPollAttempts;             // Max polls before timeout (default: 600)
    std::atomic<int> _maxJobDurationSec;           // Wall-clock timeout in seconds (<=0 disables; default: -1)
};

} // namespace ComfyUI

#endif // ASYNC_JOB_MANAGER_H
