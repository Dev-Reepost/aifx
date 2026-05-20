// Copyright OpenFX and contributors to the OpenFX project.
// SPDX-License-Identifier: BSD-3-Clause

#include "comfyui_client.h"
#include <httplib.h>
#include <ixwebsocket/IXWebSocket.h>
#include <random>
#include <sstream>
#include <iomanip>
#include <stdexcept>
#include <iostream>
#include <thread>
#include <condition_variable>
#include <mutex>

namespace ComfyUI {

// PIMPL implementation
class Client::Impl {
public:
    std::string hostname;
    int port;
    std::string inputDir;
    std::string outputDir;
    std::string clientId;

    Impl(const std::string& addr) {
        parseServerAddress(addr);
    }

    void parseServerAddress(const std::string& addr) {
        // Parse "hostname:port" format
        size_t colonPos = addr.find(':');
        if (colonPos != std::string::npos) {
            hostname = addr.substr(0, colonPos);
            port = std::stoi(addr.substr(colonPos + 1));
        } else {
            hostname = addr;
            port = 8188; // Default ComfyUI port
        }
    }

    std::string getServerAddress() const {
        return hostname + ":" + std::to_string(port);
    }
};

Client::Client(const std::string& serverAddress)
    : m_impl(std::make_unique<Impl>(serverAddress))
{
    m_impl->clientId = generateClientId();
}

Client::~Client() = default;

bool Client::testConnection() {
    try {
        httplib::Client client(m_impl->hostname, m_impl->port);
        client.set_connection_timeout(5, 0); // 5 seconds

        // Try to GET /system_stats or root endpoint
        auto res = client.Get("/");

        if (res && (res->status == 200 || res->status == 404)) {
            // 404 is OK - server responded, just no root handler
            return true;
        }
        return false;
    } catch (const std::exception& e) {
        std::cerr << "ComfyUI connection test failed: " << e.what() << std::endl;
        return false;
    }
}

void Client::setServerAddress(const std::string& address) {
    m_impl->parseServerAddress(address);
}

std::string Client::getServerAddress() const {
    return m_impl->getServerAddress();
}

std::string Client::queuePrompt(const json& workflow, const std::string& clientId) {
    auto startTime = std::chrono::steady_clock::now();

    try {
        // Create HTTP client
        auto clientStart = std::chrono::steady_clock::now();
        httplib::Client client(m_impl->hostname, m_impl->port);
        client.set_connection_timeout(10, 0); // 10 seconds

        auto clientDuration = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - clientStart);

        // Build request payload matching ComfyUI API format
        auto buildStart = std::chrono::steady_clock::now();
        json payload = {{"prompt", workflow}};
        if (!clientId.empty()) {
            payload["client_id"] = clientId;
        }

        std::string body = payload.dump();

        auto buildDuration = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - buildStart);

        // POST to /prompt endpoint
        auto postStart = std::chrono::steady_clock::now();
        std::cerr << "[ComfyUI::Client] queuePrompt() -> POST to " << m_impl->getServerAddress()
                  << "/prompt (payload size: " << body.length() << " bytes)" << std::endl;

        auto res = client.Post("/prompt", body, "application/json");

        auto postDuration = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - postStart);

        std::cerr << "[ComfyUI::Client] POST took " << postDuration.count() << " ms" << std::endl;

        if (!res) {
            std::cerr << "[ComfyUI::Client] ERROR: Failed to connect" << std::endl;
            throw std::runtime_error("Failed to connect to ComfyUI server at " +
                                   m_impl->getServerAddress());
        }

        std::cerr << "[ComfyUI::Client] Response status: " << res->status << std::endl;

        if (res->status != 200) {
            std::cerr << "[ComfyUI::Client] ERROR: Server returned " << res->status << std::endl;
            throw std::runtime_error("ComfyUI server returned error: " +
                                   std::to_string(res->status) + " - " + res->body);
        }

        // Parse response to get prompt_id
        auto parseStart = std::chrono::steady_clock::now();
        json response = json::parse(res->body);
        auto parseDuration = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - parseStart);

        auto totalDuration = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - startTime);

        if (response.contains("prompt_id")) {
            std::string promptId = response["prompt_id"].get<std::string>();
            std::cerr << "[ComfyUI::Client] SUCCESS: promptId=" << promptId << std::endl;
            std::cerr << "[ComfyUI::Client] queuePrompt() TOTAL: " << totalDuration.count() << " ms "
                      << "(client=" << clientDuration.count() << "ms, build=" << buildDuration.count()
                      << "ms, POST=" << postDuration.count() << "ms, parse=" << parseDuration.count()
                      << "ms)" << std::endl;
            return promptId;
        } else if (response.contains("error")) {
            throw std::runtime_error("ComfyUI error: " +
                                   response["error"].dump());
        }

        throw std::runtime_error("Unexpected response from ComfyUI server");

    } catch (const json::exception& e) {
        throw std::runtime_error(std::string("JSON error: ") + e.what());
    } catch (const std::exception& e) {
        throw std::runtime_error(std::string("ComfyUI queue error: ") + e.what());
    }
}

json Client::getHistory(const std::string& promptId) {
    auto startTime = std::chrono::steady_clock::now();

    try {
        httplib::Client client(m_impl->hostname, m_impl->port);
        client.set_connection_timeout(10, 0);

        // GET /history/{prompt_id}
        std::string endpoint = "/history/" + promptId;

        auto getStart = std::chrono::steady_clock::now();
        auto res = client.Get(endpoint.c_str());
        auto getDuration = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - getStart);

        if (getDuration.count() > 50) {
            std::cerr << "[ComfyUI::Client] getHistory(" << promptId << ") GET took "
                      << getDuration.count() << " ms" << std::endl;
        }

        if (!res) {
            std::cerr << "[ComfyUI::Client] getHistory() ERROR: Failed to connect" << std::endl;
            throw std::runtime_error("Failed to get history from ComfyUI server");
        }

        if (res->status != 200) {
            std::cerr << "[ComfyUI::Client] getHistory() ERROR: Status " << res->status << std::endl;
            throw std::runtime_error("ComfyUI history request failed: " +
                                   std::to_string(res->status));
        }

        // Parse and return FULL history response
        // Response format: {prompt_id: {outputs: {...}, status: {...}, prompt: {...}}}
        auto parseStart = std::chrono::steady_clock::now();
        json history = json::parse(res->body);
        auto parseDuration = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - parseStart);

        auto totalDuration = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - startTime);

        if (totalDuration.count() > 100 || getDuration.count() > 50) {
            std::cerr << "[ComfyUI::Client] getHistory() TOTAL: " << totalDuration.count() << " ms "
                      << "(GET=" << getDuration.count() << "ms, parse=" << parseDuration.count()
                      << "ms)" << std::endl;
        }

        // Return the full history object so caller can check if promptId exists
        return history;

    } catch (const json::exception& e) {
        throw std::runtime_error(std::string("JSON parse error: ") + e.what());
    } catch (const std::exception& e) {
        throw std::runtime_error(std::string("Get history error: ") + e.what());
    }
}

bool Client::interruptExecution(const std::string& clientId) {
    try {
        httplib::Client client(m_impl->hostname, m_impl->port);
        client.set_connection_timeout(5, 0);

        // POST to /interrupt
        json payload = {{"client_id", clientId}};
        std::string body = payload.dump();

        auto res = client.Post("/interrupt", body, "application/json");

        return (res && res->status == 200);

    } catch (const std::exception& e) {
        std::cerr << "Interrupt execution failed: " << e.what() << std::endl;
        return false;
    }
}

void Client::setInputDirectory(const std::string& path) {
    m_impl->inputDir = path;
}

void Client::setOutputDirectory(const std::string& path) {
    m_impl->outputDir = path;
}

std::string Client::getInputDirectory() const {
    return m_impl->inputDir;
}

std::string Client::getOutputDirectory() const {
    return m_impl->outputDir;
}

std::vector<std::string> Client::findModels(const std::string& modelType) {
    // Model discovery would require filesystem access or ComfyUI API endpoint
    // For now, return empty - will be populated from UI or config
    // TODO: Implement via ComfyUI /object_info endpoint or filesystem scan
    return {};
}

std::string Client::getClientId() const {
    return m_impl->clientId;
}

std::string Client::generateClientId() {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, 15);

    std::stringstream ss;
    ss << "ofx_client_";
    for (int i = 0; i < 16; ++i) {
        ss << std::hex << dis(gen);
    }
    return ss.str();
}

void Client::monitorExecution(const std::string& promptId, EventCallback callback) {
    try {
        // Track completion state
        bool completed = false;
        bool error_occurred = false;
        std::string error_message;
        std::mutex mtx;
        std::condition_variable cv;

        // Create ixwebsocket client
        ix::WebSocket webSocket;

        // Build WebSocket URL: ws://hostname:port/ws?clientId=xxx
        std::stringstream url;
        url << "ws://" << m_impl->hostname << ":" << m_impl->port
            << "/ws?clientId=" << m_impl->clientId;

        webSocket.setUrl(url.str());

        // Set message handler
        webSocket.setOnMessageCallback([&](const ix::WebSocketMessagePtr& msg) {
            if (msg->type == ix::WebSocketMessageType::Message) {
                try {
                    json message = json::parse(msg->str);
                    std::string type = message["type"];

                    if (type == "status") {
                        // Queue status update
                        if (callback) {
                            callback(EventType::Status, message["data"]);
                        }
                    }
                    else if (type == "executing") {
                        // Node execution update
                        json data = message["data"];
                        std::string node = data.contains("node") && !data["node"].is_null()
                            ? data["node"].get<std::string>()
                            : "";

                        if (node.empty() && data["prompt_id"] == promptId) {
                            // Execution completed (node is null)
                            if (callback) {
                                callback(EventType::Completed, data);
                            }
                            std::lock_guard<std::mutex> lock(mtx);
                            completed = true;
                            cv.notify_all();
                        } else if (!node.empty()) {
                            // Node executing
                            if (callback) {
                                callback(EventType::Executing, data);
                            }
                        }
                    }
                    else if (type == "progress") {
                        // Progress update
                        if (callback) {
                            callback(EventType::Progress, message["data"]);
                        }
                    }
                    else if (type == "execution_error") {
                        // Execution error
                        if (callback) {
                            callback(EventType::ExecutionError, message["data"]);
                        }
                        std::lock_guard<std::mutex> lock(mtx);
                        error_occurred = true;
                        error_message = message["data"].dump();
                        cv.notify_all();
                    }
                    else if (type == "execution_cached") {
                        // Result from cache
                        if (callback) {
                            callback(EventType::ExecutionCached, message["data"]);
                        }
                    }
                } catch (const std::exception& e) {
                    std::cerr << "WebSocket message parsing error: " << e.what() << std::endl;
                }
            }
            else if (msg->type == ix::WebSocketMessageType::Error) {
                std::lock_guard<std::mutex> lock(mtx);
                error_occurred = true;
                error_message = "WebSocket error: " + msg->errorInfo.reason;
                cv.notify_all();
            }
            else if (msg->type == ix::WebSocketMessageType::Close) {
                // Connection closed
                if (!completed && !error_occurred) {
                    std::lock_guard<std::mutex> lock(mtx);
                    error_occurred = true;
                    error_message = "WebSocket closed unexpectedly";
                    cv.notify_all();
                }
            }
        });

        // Start the WebSocket connection
        webSocket.start();

        // Wait for connection to be ready
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        // Wait for completion or error
        std::unique_lock<std::mutex> lock(mtx);
        cv.wait(lock, [&]{ return completed || error_occurred; });

        // Close connection
        webSocket.stop();

        // Check for errors
        if (error_occurred) {
            throw std::runtime_error("ComfyUI execution error: " + error_message);
        }

    } catch (const std::exception& e) {
        throw std::runtime_error(std::string("WebSocket monitoring error: ") + e.what());
    }
}

} // namespace ComfyUI
