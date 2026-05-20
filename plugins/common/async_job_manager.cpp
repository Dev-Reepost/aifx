// Copyright OpenFX and contributors to the OpenFX project.
// SPDX-License-Identifier: BSD-3-Clause

#include "async_job_manager.h"
#include "comfyui_client.h"
#include "comfyui_base_plugin.h"
#include <fstream>
#include <algorithm>

namespace ComfyUI {

// ============================================================================
// Constructor / Destructor
// ============================================================================

AsyncJobManager::AsyncJobManager(Client* comfyClient, std::shared_ptr<spdlog::logger> logger)
    : _comfyClient(comfyClient)
    , _logger(logger)
    , _shutdown(false)
    , _completionCallback(nullptr)
    , _pollingInterval(2.0)        // Poll every 2 seconds (legacy fallback)
    , _fastPollingInterval(0.5)    // Fast: 0.5s when jobs active (responsive)
    , _slowPollingInterval(5.0)    // Slow: 5s when idle (reduced overhead)
    , _jobRetentionTime(300.0)     // Keep completed jobs for 5 minutes
    , _maxPollAttempts(300)        // 10 minutes max (300 * 2s)
{
    if (_logger) {
        _logger->info("========================================");
        _logger->info("AsyncJobManager: Initializing with ADAPTIVE polling");
        _logger->info("  Fast polling (active): {} seconds", _fastPollingInterval.load());
        _logger->info("  Slow polling (idle):   {} seconds", _slowPollingInterval.load());
        _logger->info("  Max poll attempts: {}", _maxPollAttempts.load());
        _logger->info("  Job retention time: {} seconds", _jobRetentionTime.load());
        _logger->info("========================================");
    }

    // Start background monitoring thread
    _monitorThread = std::thread(&AsyncJobManager::monitorThreadFunc, this);

    if (_logger) _logger->info("AsyncJobManager: Background monitor thread started (thread_id: {})",
                               std::hash<std::thread::id>{}(_monitorThread.get_id()));
}

AsyncJobManager::~AsyncJobManager()
{
    if (_logger) _logger->info("AsyncJobManager: Shutting down");

    // Signal shutdown — the write thread checks this flag between frames
    _shutdown = true;

    // Join the write thread FIRST: it holds raw OFX clip pointers that
    // become invalid as soon as the host tears down the plugin instance.
    // Joining here ensures fetchImage() is never called on a dead clip.
    {
        std::lock_guard<std::mutex> lock(_writeThreadMutex);
        if (_writeThread.joinable()) {
            if (_logger) _logger->info("AsyncJobManager: Waiting for write thread to finish");
            _writeThread.join();
        }
    }

    // Then wait for the monitoring thread to finish
    if (_monitorThread.joinable()) {
        if (_logger) _logger->info("AsyncJobManager: Waiting for monitor thread to finish");
        _monitorThread.join();
    }

    if (_logger) _logger->info("AsyncJobManager: Shutdown complete");
}

// ============================================================================
// Job Submission API
// ============================================================================

bool AsyncJobManager::isJobPending(int frame) const
{
    std::lock_guard<std::mutex> lock(_jobsMutex);

    auto it = _jobs.find(frame);
    if (it == _jobs.end()) {
        return false;  // No job found
    }

    const AsyncJob& job = it->second;
    return (job.status == JobStatus::QUEUED || job.status == JobStatus::PROCESSING);
}

void AsyncJobManager::submitJob(int frame, const json& workflow, const std::string& outputPath)
{
    auto startTime = std::chrono::steady_clock::now();

    if (_logger) {
        _logger->info("========================================");
        _logger->info("AsyncJobManager::submitJob() - Frame {}", frame);
        _logger->info("  Output path: {}", outputPath);
        _logger->info("  Workflow nodes: {}", workflow.size());
    }

    // Submit to ComfyUI (may throw exception)
    auto queueStartTime = std::chrono::steady_clock::now();
    if (_logger) _logger->info("  Calling ComfyUI queuePrompt()...");
    std::string promptId = _comfyClient->queuePrompt(workflow, "");

    auto queueEndTime = std::chrono::steady_clock::now();
    auto queueDuration = std::chrono::duration_cast<std::chrono::milliseconds>(queueEndTime - queueStartTime);

    if (_logger) {
        _logger->info("  ComfyUI accepted job - promptId: {}", promptId);
        _logger->info("  queuePrompt() took: {} ms", queueDuration.count());
    }

    // Add to job queue
    {
        std::lock_guard<std::mutex> lock(_jobsMutex);

        AsyncJob job(promptId, frame, outputPath);
        job.status = JobStatus::QUEUED;

        _jobs[frame] = job;

        if (_logger) {
            _logger->info("  Frame {} added to job queue", frame);
            _logger->info("  Total jobs in queue: {}", _jobs.size());

            auto totalDuration = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - startTime);
            _logger->info("  submitJob() total time: {} ms", totalDuration.count());
            _logger->info("========================================");
        }
    }
}

void AsyncJobManager::submitJobAsync(int frame,
                                     const std::map<std::string, ImageData>& imageDataMap,
                                     const std::map<std::string, std::string>& inputPathMap,
                                     const std::string& outputPath,
                                     BasePlugin* basePlugin)
{
    auto startTime = std::chrono::steady_clock::now();

    if (_logger) {
        _logger->info("========================================");
        _logger->info("AsyncJobManager::submitJobAsync() - Frame {} (TRULY NON-BLOCKING)", frame);
        _logger->info("  Output path: {}", outputPath);
        _logger->info("  Input images: {}", imageDataMap.size());
        for (const auto& [inputId, imageData] : imageDataMap) {
            _logger->info("    {}: {}x{}, {} channels",
                         inputId, imageData.width, imageData.height, imageData.channels);
        }
    }

    // Create placeholder job immediately (marks frame as pending)
    {
        std::lock_guard<std::mutex> lock(_jobsMutex);

        AsyncJob job;
        job.frame = frame;
        job.outputPath = outputPath;
        job.status = JobStatus::QUEUED;
        job.submissionStatus = SubmissionStatus::PENDING_WRITE;
        job.submittedTime = std::chrono::steady_clock::now();

        _jobs[frame] = job;

        if (_logger) {
            _logger->info("  Frame {} marked as PENDING_WRITE in job queue", frame);
        }
    }

    // Launch background thread to handle I/O and submission
    // Note: We detach the thread because we don't need to wait for it
    std::thread([this, frame, imageDataMap, inputPathMap, outputPath, basePlugin, startTime]() {
        try {
            auto writeStartTime = std::chrono::steady_clock::now();

            if (_logger) {
                _logger->info("  [BG Thread] Frame {}: Writing {} input image(s)...", frame, imageDataMap.size());
            }

            // Write all input EXR files (THIS IS THE SLOW PART - now in background!)
            for (const auto& [inputId, imageData] : imageDataMap) {
                auto it = inputPathMap.find(inputId);
                if (it != inputPathMap.end()) {
                    const std::string& inputPath = it->second;
                    if (_logger) {
                        _logger->info("    [BG Thread] Writing {}: {}", inputId, inputPath);
                    }
                    ImageIO::writeEXR(inputPath, imageData);
                }
            }

            auto writeDuration = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - writeStartTime);

            if (_logger) {
                _logger->info("  [BG Thread] Frame {}: {} input image(s) written in {} ms",
                             frame, imageDataMap.size(), writeDuration.count());
            }

            // Update status to PENDING_SUBMIT
            {
                std::lock_guard<std::mutex> lock(_jobsMutex);
                auto it = _jobs.find(frame);
                if (it != _jobs.end()) {
                    it->second.submissionStatus = SubmissionStatus::PENDING_SUBMIT;
                }
            }

            // Build workflow (need to call back to BasePlugin)
            auto workflowStartTime = std::chrono::steady_clock::now();

            json workflow = basePlugin->buildWorkflow(frame, inputPathMap);

            auto workflowDuration = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - workflowStartTime);

            if (_logger) {
                _logger->info("  [BG Thread] Frame {}: Workflow built in {} ms", frame, workflowDuration.count());
            }

            // Submit to ComfyUI
            auto submitStartTime = std::chrono::steady_clock::now();

            std::string promptId = _comfyClient->queuePrompt(workflow, "");

            auto submitDuration = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - submitStartTime);

            // Update job with promptId and mark as SUBMITTED
            {
                std::lock_guard<std::mutex> lock(_jobsMutex);
                auto it = _jobs.find(frame);
                if (it != _jobs.end()) {
                    it->second.promptId = promptId;
                    it->second.submissionStatus = SubmissionStatus::SUBMITTED;

                    auto totalDuration = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now() - startTime);

                    if (_logger) {
                        _logger->info("  [BG Thread] Frame {}: Submitted to ComfyUI (promptId: {})", frame, promptId);
                        _logger->info("  [BG Thread] Frame {}: Total async submission: {} ms (write={}, workflow={}, submit={})",
                                     frame, totalDuration.count(), writeDuration.count(),
                                     workflowDuration.count(), submitDuration.count());
                        _logger->info("========================================");
                    }
                }
            }

        } catch (const std::exception& e) {
            // Mark job as failed
            std::lock_guard<std::mutex> lock(_jobsMutex);
            auto it = _jobs.find(frame);
            if (it != _jobs.end()) {
                it->second.status = JobStatus::FAILED;
                it->second.errorMessage = std::string("Async submission failed: ") + e.what();

                if (_logger) {
                    _logger->error("  [BG Thread] Frame {}: Submission failed: {}", frame, e.what());
                    _logger->info("========================================");
                }
            }
        }
    }).detach();

    auto returnDuration = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - startTime);

    if (_logger) {
        _logger->info("  submitJobAsync() returned in {} ms (non-blocking!)", returnDuration.count());
    }
}

void AsyncJobManager::submitSequenceJobAsync(
    int startFrame,
    std::map<int, ImageData> frameData,
    const std::string& inputFolder,
    const std::string& basename,
    const std::map<std::string, std::string>& inputPathMap,
    const std::string& firstOutputPath,
    BasePlugin* basePlugin)
{
    auto startTime = std::chrono::steady_clock::now();

    if (_logger) {
        _logger->info("========================================");
        _logger->info("AsyncJobManager::submitSequenceJobAsync() - startFrame {} ({} frames pre-fetched)",
                      startFrame, frameData.size());
        _logger->info("  First output path: {}", firstOutputPath);
    }

    // Register the job immediately so the sequence guard sees it as active
    {
        std::lock_guard<std::mutex> lock(_jobsMutex);
        AsyncJob job;
        job.frame = startFrame;
        job.outputPath = firstOutputPath;
        job.status = JobStatus::QUEUED;
        job.submissionStatus = SubmissionStatus::PENDING_WRITE;
        job.submittedTime = std::chrono::steady_clock::now();
        _jobs[startFrame] = job;
        if (_logger) _logger->info("  Frame {} marked as PENDING_WRITE in job queue", startFrame);
    }

    // Join any previous write thread before starting a new one.
    {
        std::lock_guard<std::mutex> lock(_writeThreadMutex);
        if (_writeThread.joinable()) {
            _writeThread.join();
        }
        // Move frame data into thread (no OFX objects — only plain pixel buffers).
        // The background thread NEVER calls fetchImage() or touches any OFX clips.
        _writeThread = std::thread([this, startFrame,
                     frameData = std::move(frameData),
                     inputFolder, basename,
                     inputPathMap, firstOutputPath, basePlugin, startTime]() mutable {
            try {
                if (_shutdown.load()) {
                    if (_logger) _logger->info("  [BG Thread] Shutdown before write — aborting");
                    return;
                }

                // ----------------------------------------------------------------
                // Write pre-fetched frames to the network share as EXR files.
                // frameData only contains frames NOT already on disk (caller checked).
                // ----------------------------------------------------------------
                auto writeStart = std::chrono::steady_clock::now();
                if (_logger) _logger->info("  [BG Thread] Writing {} input EXR(s)...", frameData.size());

                for (auto& kv : frameData) {
                    if (_shutdown.load()) {
                        if (_logger) _logger->info("  [BG Thread] Shutdown during write — aborting");
                        return;
                    }
                    int t = kv.first;
                    std::ostringstream ss;
                    ss << inputFolder << "/" << basename << "."
                       << std::setfill('0') << std::setw(4) << t << ".exr";
                    ImageIO::writeEXR(ss.str(), kv.second);
                    kv.second = ImageData{};  // free immediately after write
                    if (_logger) _logger->debug("  [BG Thread] Frame {}: EXR written", t);
                }
                frameData.clear();

                if (_shutdown.load()) {
                    if (_logger) _logger->info("  [BG Thread] Shutdown after write — aborting submission");
                    return;
                }

                auto writeDuration = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - writeStart);
                if (_logger) _logger->info("  [BG Thread] All EXRs written in {} ms", writeDuration.count());

                // ----------------------------------------------------------------
                // Build workflow JSON + submit to ComfyUI
                // ----------------------------------------------------------------
                {
                    std::lock_guard<std::mutex> lock(_jobsMutex);
                    auto it = _jobs.find(startFrame);
                    if (it != _jobs.end())
                        it->second.submissionStatus = SubmissionStatus::PENDING_SUBMIT;
                }

                auto workflowStart = std::chrono::steady_clock::now();
                json workflow = basePlugin->buildWorkflow(startFrame, inputPathMap);
                auto workflowDuration = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - workflowStart);
                if (_logger) _logger->info("  [BG Thread] Workflow built in {} ms", workflowDuration.count());

                auto submitStart = std::chrono::steady_clock::now();
                std::string promptId = _comfyClient->queuePrompt(workflow, "");
                auto submitDuration = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - submitStart);

                {
                    std::lock_guard<std::mutex> lock(_jobsMutex);
                    auto it = _jobs.find(startFrame);
                    if (it != _jobs.end()) {
                        it->second.promptId = promptId;
                        it->second.submissionStatus = SubmissionStatus::SUBMITTED;
                    }
                }

                auto totalDuration = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - startTime);
                if (_logger) {
                    _logger->info("  [BG Thread] Submitted (promptId: {})", promptId);
                    _logger->info("  [BG Thread] Total: {} ms (write={}, workflow={}, submit={})",
                                 totalDuration.count(), writeDuration.count(),
                                 workflowDuration.count(), submitDuration.count());
                    _logger->info("========================================");
                }

            } catch (const std::exception& e) {
                std::lock_guard<std::mutex> lock(_jobsMutex);
                auto it = _jobs.find(startFrame);
                if (it != _jobs.end()) {
                    it->second.status = JobStatus::FAILED;
                    it->second.errorMessage = std::string("Sequence submission failed: ") + e.what();
                }
                if (_logger) _logger->error("  [BG Thread] Sequence submission failed: {}", e.what());
            }
        });
    }

    auto returnDuration = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - startTime);
    if (_logger) _logger->info("  submitSequenceJobAsync() returned in {} ms", returnDuration.count());
}

JobStatus AsyncJobManager::getJobStatus(int frame) const
{
    std::lock_guard<std::mutex> lock(_jobsMutex);

    auto it = _jobs.find(frame);
    if (it == _jobs.end()) {
        return JobStatus::COMPLETED;  // Not in queue = completed (or never existed)
    }

    return it->second.status;
}

bool AsyncJobManager::cancelJob(int frame)
{
    std::lock_guard<std::mutex> lock(_jobsMutex);

    auto it = _jobs.find(frame);
    if (it == _jobs.end()) {
        if (_logger) _logger->warn("AsyncJobManager: Cannot cancel frame {} - not found", frame);
        return false;
    }

    it->second.status = JobStatus::CANCELLED;

    if (_logger) _logger->info("AsyncJobManager: Frame {} cancelled", frame);
    return true;
}

void AsyncJobManager::cancelAllJobs(bool interruptServer)
{
    if (_logger) _logger->info("AsyncJobManager: Cancelling all jobs (interrupt={})", interruptServer);

    // Mark all jobs as cancelled
    {
        std::lock_guard<std::mutex> lock(_jobsMutex);

        for (auto& [frame, job] : _jobs) {
            if (job.status == JobStatus::QUEUED || job.status == JobStatus::PROCESSING) {
                job.status = JobStatus::CANCELLED;
            }
        }
    }

    // Optionally interrupt ComfyUI server
    if (interruptServer && _comfyClient) {
        try {
            std::string clientId = _comfyClient->getClientId();
            _comfyClient->interruptExecution(clientId);
            if (_logger) _logger->info("AsyncJobManager: Sent interrupt to ComfyUI server");
        } catch (const std::exception& e) {
            if (_logger) _logger->error("AsyncJobManager: Failed to interrupt server: {}", e.what());
        }
    }

    if (_logger) _logger->info("AsyncJobManager: All jobs cancelled");
}

// ============================================================================
// Callback Management
// ============================================================================

void AsyncJobManager::setCompletionCallback(CompletionCallback callback)
{
    std::lock_guard<std::mutex> lock(_callbackMutex);
    _completionCallback = callback;

    if (_logger) {
        if (callback) {
            _logger->info("AsyncJobManager: Completion callback registered");
        } else {
            _logger->info("AsyncJobManager: Completion callback cleared");
        }
    }
}

void AsyncJobManager::setStatusUpdateCallback(StatusUpdateCallback callback)
{
    std::lock_guard<std::mutex> lock(_statusCallbackMutex);
    _statusUpdateCallback = callback;

    if (_logger) {
        if (callback) {
            _logger->info("AsyncJobManager: Status update callback registered (for periodic UI updates)");
        } else {
            _logger->info("AsyncJobManager: Status update callback cleared");
        }
    }
}

// ============================================================================
// Monitoring and Statistics
// ============================================================================

int AsyncJobManager::getPendingJobCount() const
{
    std::lock_guard<std::mutex> lock(_jobsMutex);

    int count = 0;
    for (const auto& [frame, job] : _jobs) {
        if (job.status == JobStatus::QUEUED || job.status == JobStatus::PROCESSING) {
            count++;
        }
    }

    return count;
}

std::vector<int> AsyncJobManager::getPendingFrames() const
{
    std::lock_guard<std::mutex> lock(_jobsMutex);

    std::vector<int> frames;
    for (const auto& [frame, job] : _jobs) {
        if (job.status == JobStatus::QUEUED || job.status == JobStatus::PROCESSING) {
            frames.push_back(frame);
        }
    }

    // Sort for consistent ordering
    std::sort(frames.begin(), frames.end());

    return frames;
}

std::vector<int> AsyncJobManager::getFailedFrames() const
{
    std::lock_guard<std::mutex> lock(_jobsMutex);

    std::vector<int> frames;
    for (const auto& [frame, job] : _jobs) {
        if (job.status == JobStatus::FAILED) {
            frames.push_back(frame);
        }
    }

    // Sort for consistent ordering
    std::sort(frames.begin(), frames.end());

    return frames;
}

std::map<int, AsyncJob> AsyncJobManager::getAllJobs() const
{
    std::lock_guard<std::mutex> lock(_jobsMutex);
    return _jobs;  // Return copy
}

std::string AsyncJobManager::getJobError(int frame) const
{
    std::lock_guard<std::mutex> lock(_jobsMutex);

    auto it = _jobs.find(frame);
    if (it == _jobs.end()) {
        return "";
    }

    return it->second.errorMessage;
}

// ============================================================================
// Configuration
// ============================================================================

void AsyncJobManager::setPollingInterval(double seconds)
{
    // Set both fast and slow intervals to the same value (legacy behavior)
    _pollingInterval = seconds;
    _fastPollingInterval = seconds;
    _slowPollingInterval = seconds;
    if (_logger) _logger->info("AsyncJobManager: Polling interval set to {} seconds (both fast and slow)", seconds);
}

void AsyncJobManager::setAdaptivePollingIntervals(double fastInterval, double slowInterval)
{
    _fastPollingInterval = fastInterval;
    _slowPollingInterval = slowInterval;
    if (_logger) {
        _logger->info("AsyncJobManager: Adaptive polling intervals set");
        _logger->info("  Fast (active): {} seconds", fastInterval);
        _logger->info("  Slow (idle):   {} seconds", slowInterval);
    }
}

void AsyncJobManager::setJobRetentionTime(double seconds)
{
    _jobRetentionTime = seconds;
    if (_logger) _logger->info("AsyncJobManager: Job retention time set to {} seconds", seconds);
}

void AsyncJobManager::setMaxPollAttempts(int maxPolls)
{
    _maxPollAttempts = maxPolls;
    if (_logger) _logger->info("AsyncJobManager: Max poll attempts set to {}", maxPolls);
}

// ============================================================================
// Internal Methods - Background Monitoring
// ============================================================================

void AsyncJobManager::monitorThreadFunc()
{
    if (_logger) {
        _logger->info("AsyncJobManager Monitor: Thread started with ADAPTIVE polling");
        _logger->info("  Fast interval (active): {} seconds", _fastPollingInterval.load());
        _logger->info("  Slow interval (idle):   {} seconds", _slowPollingInterval.load());
    }

    int iterationCount = 0;
    bool wasActive = false;  // Track state transitions for logging

    while (!_shutdown) {
        // Declare jobsToCheck outside try block so it's available for adaptive polling decision
        std::vector<std::pair<int, AsyncJob>> jobsToCheck;

        try {
            auto loopStartTime = std::chrono::steady_clock::now();
            ++iterationCount;

            // Get copy of jobs to check (minimize lock time)
            {
                std::lock_guard<std::mutex> lock(_jobsMutex);

                for (auto& [frame, job] : _jobs) {
                    if (job.status == JobStatus::QUEUED || job.status == JobStatus::PROCESSING) {
                        jobsToCheck.push_back({frame, job});
                    }
                }
            }

            // Log only every 10 iterations to reduce spam (every 20 seconds)
            if (_logger && !jobsToCheck.empty() && (iterationCount % 10 == 0)) {
                _logger->debug("Monitor iteration #{}: Checking {} pending job(s)",
                              iterationCount, jobsToCheck.size());
            }

            // Check each pending job (outside lock)
            for (auto& [frame, job] : jobsToCheck) {
                if (_shutdown) break;

                // Skip jobs that haven't been submitted to ComfyUI yet (async submission in progress)
                if (job.submissionStatus != SubmissionStatus::SUBMITTED) {
                    // Only log once when first encountered, then stay silent
                    if (_logger && job.pollCount == 0) {
                        _logger->info("  Frame {}: Waiting for async submission (status: {})",
                                      frame,
                                      job.submissionStatus == SubmissionStatus::PENDING_WRITE ? "PENDING_WRITE" : "PENDING_SUBMIT");
                    }
                    continue;
                }

                // Skip jobs without a promptId (shouldn't happen, but be defensive)
                if (job.promptId.empty()) {
                    if (_logger) {
                        _logger->warn("  Frame {}: Skipping - no promptId (submission may have failed)", frame);
                    }
                    continue;
                }

                auto checkStartTime = std::chrono::steady_clock::now();

                // Only log status checks on first poll and every 10th poll to reduce spam
                if (_logger && (job.pollCount == 0 || job.pollCount % 10 == 0)) {
                    _logger->info("  Frame {}: Checking status (promptId: {}, pollCount: {})",
                                  frame, job.promptId, job.pollCount);
                }

                bool completed = checkJobCompletion(job);

                auto checkDuration = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - checkStartTime);

                // Only log slow checks or completed status
                if (_logger && (completed || checkDuration.count() > 500)) {
                    _logger->info("  Frame {}: Check took {} ms (completed: {})",
                                  frame, checkDuration.count(), completed);
                }

                if (completed) {
                    // Update job in map
                    {
                        std::lock_guard<std::mutex> lock(_jobsMutex);
                        _jobs[frame] = job;  // Update with new status
                    }

                    // Invoke callback (outside lock)
                    bool success = (job.status == JobStatus::COMPLETED);
                    if (_logger) {
                        _logger->info("AsyncJobManager Monitor: Frame {} {} (status: {})",
                                     frame,
                                     success ? "COMPLETED" : "FAILED",
                                     jobStatusToString(job.status));
                    }

                    // Invoke completion callback if registered
                    {
                        std::lock_guard<std::mutex> callbackLock(_callbackMutex);
                        if (_completionCallback) {
                            try {
                                _completionCallback(frame, success);
                            } catch (const std::exception& e) {
                                if (_logger) {
                                    _logger->error("AsyncJobManager Monitor: Callback exception: {}",
                                                  e.what());
                                }
                            }
                        }
                    }
                } else {
                    // Still pending - update poll count
                    std::lock_guard<std::mutex> lock(_jobsMutex);
                    _jobs[frame].pollCount = job.pollCount;
                    _jobs[frame].status = job.status;
                }
            }

            // Cleanup old jobs periodically
            cleanupOldJobs();

            // Invoke status update callback for periodic UI refresh
            {
                std::lock_guard<std::mutex> lock(_statusCallbackMutex);
                if (_statusUpdateCallback) {
                    try {
                        _statusUpdateCallback();
                    } catch (const std::exception& e) {
                        if (_logger) {
                            _logger->error("AsyncJobManager Monitor: Status update callback exception: {}", e.what());
                        }
                    }
                }
            }

        } catch (const std::exception& e) {
            if (_logger) {
                _logger->error("AsyncJobManager Monitor: Exception in monitor loop: {}", e.what());
            }
        }

        // Sleep before next poll - ADAPTIVE based on job activity
        if (!_shutdown) {
            // Determine polling interval based on job activity
            bool hasActiveJobs = !jobsToCheck.empty();
            double interval = hasActiveJobs ? _fastPollingInterval.load() : _slowPollingInterval.load();

            // Log state transitions (active ↔ idle)
            if (_logger && hasActiveJobs != wasActive) {
                if (hasActiveJobs) {
                    _logger->info("Monitor: Switching to FAST polling ({} s) - jobs are active", interval);
                } else {
                    _logger->info("Monitor: Switching to SLOW polling ({} s) - no jobs pending", interval);
                }
                wasActive = hasActiveJobs;
            }

            auto sleepDuration = std::chrono::duration<double>(interval);
            std::this_thread::sleep_for(sleepDuration);
        }
    }

    if (_logger) _logger->info("AsyncJobManager Monitor: Thread exiting");
}

bool AsyncJobManager::checkJobCompletion(AsyncJob& job)
{
    job.pollCount++;

    // Check for timeout (too many poll attempts)
    if (job.pollCount > _maxPollAttempts.load()) {
        if (_logger) {
            _logger->error("AsyncJobManager: Frame {} timed out after {} polls",
                          job.frame, job.pollCount);
        }
        job.status = JobStatus::FAILED;
        job.errorMessage = "Job timed out after " + std::to_string(job.pollCount) + " poll attempts";
        return true;  // Completed (as failed)
    }

    try {
        // Query ComfyUI history
        auto historyStartTime = std::chrono::steady_clock::now();

        if (_logger && job.pollCount == 1) {
            _logger->debug("Frame {}: First poll - calling getHistory({})",
                          job.frame, job.promptId);
        }

        json history = _comfyClient->getHistory(job.promptId);

        auto historyDuration = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - historyStartTime);

        if (_logger && historyDuration.count() > 100) {
            _logger->warn("Frame {}: getHistory() took {} ms (slow)",
                         job.frame, historyDuration.count());
        }

        // Check if our prompt ID is in the history
        if (!history.contains(job.promptId)) {
            // Not in history yet - still queued/processing
            if (_logger) {
                if (job.pollCount == 1) {
                    _logger->info("Frame {}: Not in history yet (just submitted)",
                                 job.frame);
                } else if (job.pollCount % 10 == 0) {
                    _logger->info("Frame {}: Still not in history (poll {})",
                                 job.frame, job.pollCount);
                } else {
                    _logger->debug("Frame {}: Not in history (poll {})",
                                  job.frame, job.pollCount);
                }
            }
            return false;  // Still pending
        }

        // Found in history - parse response
        if (_logger && job.pollCount == 1) {
            _logger->info("Frame {}: Found in history!", job.frame);
        }

        const json& promptData = history[job.promptId];

        // Check for errors
        if (promptData.contains("status")) {
            const json& status = promptData["status"];

            if (status.contains("status_str")) {
                std::string statusStr = status["status_str"].get<std::string>();

                if (statusStr == "success") {
                    // Success - verify output file exists
                    if (verifyOutputFile(job.outputPath)) {
                        job.status = JobStatus::COMPLETED;
                        if (_logger) {
                            _logger->info("AsyncJobManager: Frame {} completed successfully",
                                         job.frame);
                        }
                        return true;
                    } else {
                        // Success reported but file missing - log full history for diagnosis
                        job.status = JobStatus::FAILED;
                        job.errorMessage = "Output file not found: " + job.outputPath;
                        if (_logger) {
                            _logger->error("AsyncJobManager: Frame {} - output file missing: {}",
                                          job.frame, job.outputPath);
                            _logger->error("AsyncJobManager: ComfyUI history response (for diagnosis): {}",
                                          history.dump(2));
                        }
                        return true;
                    }
                } else if (statusStr == "error") {
                    // Error during execution
                    job.status = JobStatus::FAILED;
                    if (status.contains("messages")) {
                        job.errorMessage = status["messages"].dump();
                    } else {
                        job.errorMessage = "ComfyUI execution error";
                    }
                    if (_logger) {
                        _logger->error("AsyncJobManager: Frame {} failed: {}",
                                      job.frame, job.errorMessage);
                    }
                    return true;
                }
            }

            // Check completed flag
            if (status.contains("completed") && status["completed"].get<bool>()) {
                // Mark as processing → will check output file on next poll
                if (job.status == JobStatus::QUEUED) {
                    job.status = JobStatus::PROCESSING;
                }
            }
        }

        // Check for outputs (indicates completion)
        if (promptData.contains("outputs") && !promptData["outputs"].is_null()) {
            int outputCount = promptData["outputs"].size();

            if (outputCount > 0) {
                // Has outputs - verify file exists
                if (verifyOutputFile(job.outputPath)) {
                    job.status = JobStatus::COMPLETED;
                    if (_logger) {
                        _logger->info("AsyncJobManager: Frame {} completed (outputs detected)",
                                     job.frame);
                    }
                    return true;
                } else {
                    // Outputs exist but file not found yet - might be cached, check again
                    if (_logger) {
                        _logger->debug("AsyncJobManager: Frame {} has outputs but file not found yet",
                                      job.frame);
                    }
                    return false;  // Wait for file to appear
                }
            }
        }

        // Still processing
        return false;

    } catch (const std::exception& e) {
        // Network error or JSON parse error
        if (_logger) {
            _logger->warn("AsyncJobManager: Error checking frame {}: {}",
                         job.frame, e.what());
        }
        // Don't mark as failed yet - might be temporary network issue
        return false;
    }
}

void AsyncJobManager::cleanupOldJobs()
{
    std::lock_guard<std::mutex> lock(_jobsMutex);

    auto now = std::chrono::steady_clock::now();
    double retentionTime = _jobRetentionTime.load();

    // Find jobs to remove
    std::vector<int> framesToRemove;

    for (const auto& [frame, job] : _jobs) {
        // Only cleanup completed, failed, or cancelled jobs
        if (job.status == JobStatus::COMPLETED ||
            job.status == JobStatus::FAILED ||
            job.status == JobStatus::CANCELLED) {

            auto age = std::chrono::duration<double>(now - job.submittedTime).count();

            if (age > retentionTime) {
                framesToRemove.push_back(frame);
            }
        }
    }

    // Remove old jobs
    for (int frame : framesToRemove) {
        if (_logger) {
            _logger->debug("AsyncJobManager: Cleaning up old job for frame {}", frame);
        }
        _jobs.erase(frame);
    }

    if (!framesToRemove.empty() && _logger) {
        _logger->info("AsyncJobManager: Cleaned up {} old jobs (remaining: {})",
                     framesToRemove.size(), _jobs.size());
    }
}

bool AsyncJobManager::verifyOutputFile(const std::string& path) const
{
    std::ifstream file(path);
    bool exists = file.good();
    file.close();
    return exists;
}

} // namespace ComfyUI
