// Copyright OpenFX and contributors to the OpenFX project.
// SPDX-License-Identifier: BSD-3-Clause

#ifndef COMFYUI_CLIENT_H
#define COMFYUI_CLIENT_H

#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace ComfyUI {

/**
 * @brief Event types for WebSocket monitoring callbacks
 */
enum class EventType {
    Status,           // Queue status update
    Executing,        // Node execution update
    Progress,         // Progress update from a node
    ExecutionError,   // Execution error occurred
    ExecutionCached,  // Result retrieved from cache
    Completed         // Workflow completed successfully
};

/**
 * @brief Callback function for WebSocket events
 *
 * @param eventType Type of event received
 * @param data Event data (JSON format varies by event type)
 */
using EventCallback = std::function<void(EventType eventType, const json& data)>;

/**
 * @brief REST/WebSocket client for ComfyUI server communication
 *
 * Handles HTTP/WebSocket communication with ComfyUI server for:
 * - Submitting workflows
 * - Monitoring execution status via WebSocket
 * - Retrieving generated images
 * - Managing models
 */
class Client {
public:
    Client(const std::string& serverAddress);
    ~Client();

    // Connection management
    bool testConnection();
    void setServerAddress(const std::string& address);
    std::string getServerAddress() const;

    // Workflow execution
    std::string queuePrompt(const json& workflow, const std::string& clientId);
    json getHistory(const std::string& promptId);
    bool interruptExecution(const std::string& clientId);

    /**
     * @brief Monitor workflow execution via WebSocket
     *
     * Connects to ComfyUI WebSocket endpoint and monitors workflow execution.
     * Blocks until workflow completes, errors, or is interrupted.
     *
     * @param promptId Prompt ID from queuePrompt()
     * @param callback Called for each event (progress, status, errors)
     * @throws std::runtime_error if connection fails or execution errors
     */
    void monitorExecution(const std::string& promptId, EventCallback callback);

    // File I/O paths
    void setInputDirectory(const std::string& path);
    void setOutputDirectory(const std::string& path);
    std::string getInputDirectory() const;
    std::string getOutputDirectory() const;

    // Model management
    std::vector<std::string> findModels(const std::string& modelType);

    // Client ID
    std::string getClientId() const;

private:
    class Impl;
    std::unique_ptr<Impl> m_impl;

    std::string generateClientId();
};

} // namespace ComfyUI

#endif // COMFYUI_CLIENT_H
