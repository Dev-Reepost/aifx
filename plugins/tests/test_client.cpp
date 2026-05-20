// Copyright OpenFX and contributors to the OpenFX project.
// SPDX-License-Identifier: BSD-3-Clause

/**
 * Test suite for ComfyUI REST client
 *
 * Usage:
 *   1. Start ComfyUI server: python main.py
 *   2. Run tests: ./test_comfyui_client
 *
 * Tests require a running ComfyUI server on localhost:8188
 */

#include "comfyui_client.h"
#include <iostream>
#include <cassert>
#include <thread>
#include <chrono>

// ANSI color codes for pretty output
#define COLOR_GREEN "\033[32m"
#define COLOR_RED "\033[31m"
#define COLOR_YELLOW "\033[33m"
#define COLOR_BLUE "\033[34m"
#define COLOR_RESET "\033[0m"

// Test result tracking
int tests_run = 0;
int tests_passed = 0;
int tests_failed = 0;

// Global server address (can be overridden via command line)
std::string g_server_address = "localhost:8188";

void print_test_header(const std::string& test_name) {
    std::cout << "\n" << COLOR_BLUE << "==== " << test_name << " ====" << COLOR_RESET << std::endl;
    tests_run++;
}

void print_success(const std::string& message) {
    std::cout << COLOR_GREEN << "✓ " << message << COLOR_RESET << std::endl;
}

void print_failure(const std::string& message) {
    std::cout << COLOR_RED << "✗ " << message << COLOR_RESET << std::endl;
    tests_failed++;
}

void print_info(const std::string& message) {
    std::cout << COLOR_YELLOW << "  " << message << COLOR_RESET << std::endl;
}

bool test_result(bool condition, const std::string& test_description) {
    if (condition) {
        print_success(test_description);
        tests_passed++;
        return true;
    } else {
        print_failure(test_description);
        return false;
    }
}

// ============================================================================
// Test 1: Client Construction
// ============================================================================
bool test_client_construction() {
    print_test_header("Test 1: Client Construction");

    try {
        // Test with default port
        ComfyUI::Client client1("localhost");
        print_info("Client created with hostname only");
        test_result(!client1.getClientId().empty(), "Client ID generated");
        test_result(client1.getServerAddress() == "localhost:8188", "Default port 8188 used");

        // Test with explicit port
        ComfyUI::Client client2("localhost:8080");
        test_result(client2.getServerAddress() == "localhost:8080", "Custom port 8080 used");

        // Test client ID uniqueness
        test_result(client1.getClientId() != client2.getClientId(), "Client IDs are unique");

        print_info("Client ID format: " + client1.getClientId());

        return true;
    } catch (const std::exception& e) {
        print_failure(std::string("Exception: ") + e.what());
        return false;
    }
}

// ============================================================================
// Test 2: Server Connection
// ============================================================================
bool test_server_connection() {
    print_test_header("Test 2: Server Connection");

    try {
        ComfyUI::Client client(g_server_address);

        print_info("Attempting to connect to ComfyUI server...");
        bool connected = client.testConnection();

        if (connected) {
            print_success("Connected to ComfyUI server at " + g_server_address);
            return true;
        } else {
            print_failure("Could not connect to ComfyUI server");
            print_info("Make sure ComfyUI is running at " + g_server_address);
            return false;
        }
    } catch (const std::exception& e) {
        print_failure(std::string("Exception: ") + e.what());
        return false;
    }
}

// ============================================================================
// Test 3: Invalid Server Connection
// ============================================================================
bool test_invalid_connection() {
    print_test_header("Test 3: Invalid Server Connection");

    try {
        // Try to connect to invalid port
        ComfyUI::Client client("localhost:9999");

        print_info("Attempting to connect to invalid port...");
        bool connected = client.testConnection();

        test_result(!connected, "Correctly failed to connect to invalid port");
        return true;
    } catch (const std::exception& e) {
        print_failure(std::string("Unexpected exception: ") + e.what());
        return false;
    }
}

// ============================================================================
// Test 4: Queue Simple Workflow
// ============================================================================
bool test_queue_workflow() {
    print_test_header("Test 4: Queue Simple Workflow");

    try {
        ComfyUI::Client client(g_server_address);

        // First verify connection
        if (!client.testConnection()) {
            print_failure("Server not available, skipping workflow test");
            return false;
        }

        // Create a minimal valid workflow with output
        // This workflow loads an image and saves it (no-op but valid)
        // Using "Alice_Paul_1915.jpg" which exists on the server
        json workflow = {
            {"1", {
                {"class_type", "LoadImage"},
                {"inputs", {
                    {"image", "Alice_Paul_1915.jpg"}
                }}
            }},
            {"2", {
                {"class_type", "SaveImage"},
                {"inputs", {
                    {"images", json::array({"1", 0})},
                    {"filename_prefix", "test_output"}
                }}
            }}
        };

        print_info("Submitting test workflow...");
        std::string promptId = client.queuePrompt(workflow, client.getClientId());

        test_result(!promptId.empty(), "Workflow queued successfully");
        print_info("Prompt ID: " + promptId);

        // Give server time to process (simple load+save should be fast)
        print_info("Waiting for workflow to complete...");
        std::this_thread::sleep_for(std::chrono::milliseconds(2000));

        // Try to get history
        print_info("Fetching workflow history...");
        json history = client.getHistory(promptId);

        // History might still be empty if workflow is still running,
        // but the API call should succeed (we got valid JSON back)
        bool history_retrieved = !history.is_null();
        test_result(history_retrieved, "History API call successful");

        if (!history.empty()) {
            print_info("Workflow completed and found in history");
        } else {
            print_info("Note: Workflow may still be processing");
        }

        return true;
    } catch (const std::exception& e) {
        print_failure(std::string("Exception: ") + e.what());
        print_info("Note: This test requires ComfyUI server to be running");
        return false;
    }
}

// ============================================================================
// Test 5: Get History
// ============================================================================
bool test_get_history() {
    print_test_header("Test 5: Get History");

    try {
        ComfyUI::Client client(g_server_address);

        if (!client.testConnection()) {
            print_failure("Server not available, skipping history test");
            return false;
        }

        // Try to get history for non-existent prompt
        print_info("Fetching history for non-existent prompt...");
        json history = client.getHistory("nonexistent-prompt-id-12345");

        // Should return empty object, not throw exception
        test_result(history.empty(), "Empty history returned for invalid prompt ID");

        return true;
    } catch (const std::exception& e) {
        print_failure(std::string("Exception: ") + e.what());
        return false;
    }
}

// ============================================================================
// Test 6: Interrupt Execution
// ============================================================================
bool test_interrupt_execution() {
    print_test_header("Test 6: Interrupt Execution");

    try {
        ComfyUI::Client client(g_server_address);

        if (!client.testConnection()) {
            print_failure("Server not available, skipping interrupt test");
            return false;
        }

        print_info("Sending interrupt signal...");
        bool result = client.interruptExecution(client.getClientId());

        // Interrupt should succeed even if nothing is running
        test_result(result, "Interrupt signal sent successfully");

        return true;
    } catch (const std::exception& e) {
        print_failure(std::string("Exception: ") + e.what());
        return false;
    }
}

// ============================================================================
// Test 7: Directory Configuration
// ============================================================================
bool test_directory_configuration() {
    print_test_header("Test 7: Directory Configuration");

    try {
        ComfyUI::Client client("localhost:8188");

        // Test input directory
        client.setInputDirectory("/path/to/input");
        test_result(client.getInputDirectory() == "/path/to/input",
                   "Input directory set correctly");

        // Test output directory
        client.setOutputDirectory("/path/to/output");
        test_result(client.getOutputDirectory() == "/path/to/output",
                   "Output directory set correctly");

        return true;
    } catch (const std::exception& e) {
        print_failure(std::string("Exception: ") + e.what());
        return false;
    }
}

// ============================================================================
// Test 8: Server Address Change
// ============================================================================
bool test_server_address_change() {
    print_test_header("Test 8: Server Address Change");

    try {
        ComfyUI::Client client("localhost:8188");

        // Change server address
        client.setServerAddress("127.0.0.1:8080");
        test_result(client.getServerAddress() == "127.0.0.1:8080",
                   "Server address changed correctly");

        // Change back
        client.setServerAddress("localhost");
        test_result(client.getServerAddress() == "localhost:8188",
                   "Default port applied correctly");

        return true;
    } catch (const std::exception& e) {
        print_failure(std::string("Exception: ") + e.what());
        return false;
    }
}

// ============================================================================
// Test 9: Model Discovery (Stub)
// ============================================================================
bool test_model_discovery() {
    print_test_header("Test 9: Model Discovery");

    try {
        ComfyUI::Client client(g_server_address);

        print_info("Testing model discovery...");
        auto models = client.findModels("sam");

        // Currently returns empty (not implemented)
        print_info("Model discovery: " + std::to_string(models.size()) + " models found");
        print_info("Note: Model discovery not yet implemented");

        return true;
    } catch (const std::exception& e) {
        print_failure(std::string("Exception: ") + e.what());
        return false;
    }
}

// ============================================================================
// Test 10: WebSocket Monitoring
// ============================================================================
bool test_websocket_monitoring() {
    print_test_header("Test 10: WebSocket Monitoring");

    try {
        ComfyUI::Client client(g_server_address);

        if (!client.testConnection()) {
            print_failure("Server not available, skipping WebSocket test");
            return false;
        }

        // Create a simple workflow
        json workflow = {
            {"1", {
                {"class_type", "LoadImage"},
                {"inputs", {
                    {"image", "Alice_Paul_1915.jpg"}
                }}
            }},
            {"2", {
                {"class_type", "SaveImage"},
                {"inputs", {
                    {"images", json::array({"1", 0})},
                    {"filename_prefix", "test_websocket"}
                }}
            }}
        };

        print_info("Queueing workflow for WebSocket monitoring...");
        std::string promptId = client.queuePrompt(workflow, client.getClientId());

        test_result(!promptId.empty(), "Workflow queued successfully");
        print_info("Prompt ID: " + promptId);

        // Track events
        int status_count = 0;
        int executing_count = 0;
        int progress_count = 0;
        bool completed = false;
        std::string last_node;

        print_info("Monitoring execution via WebSocket...");

        // Monitor with callback
        client.monitorExecution(promptId,
            [&](ComfyUI::EventType eventType, const json& data) {
                switch (eventType) {
                    case ComfyUI::EventType::Status:
                        status_count++;
                        break;
                    case ComfyUI::EventType::Executing:
                        executing_count++;
                        if (data.contains("node") && !data["node"].is_null()) {
                            last_node = data["node"].get<std::string>();
                            print_info("  Executing node: " + last_node);
                        }
                        break;
                    case ComfyUI::EventType::Progress:
                        progress_count++;
                        break;
                    case ComfyUI::EventType::Completed:
                        completed = true;
                        print_info("  Workflow completed!");
                        break;
                    case ComfyUI::EventType::ExecutionError:
                        print_failure("  Execution error: " + data.dump());
                        break;
                    case ComfyUI::EventType::ExecutionCached:
                        print_info("  Result retrieved from cache");
                        break;
                }
            });

        // Verify we received events
        test_result(completed, "Workflow completed via WebSocket");
        test_result(status_count > 0 || executing_count > 0, "Received WebSocket events");

        print_info("Events received: " + std::to_string(status_count) + " status, " +
                   std::to_string(executing_count) + " executing, " +
                   std::to_string(progress_count) + " progress");

        return true;
    } catch (const std::exception& e) {
        print_failure(std::string("Exception: ") + e.what());
        print_info("Note: This test requires ComfyUI server to be running");
        return false;
    }
}

// ============================================================================
// Main Test Runner
// ============================================================================

int main(int argc, char** argv) {
    std::cout << "\n";
    std::cout << "╔══════════════════════════════════════════════════════════╗\n";
    std::cout << "║       ComfyUI OFX Plugin - REST Client Test Suite       ║\n";
    std::cout << "╚══════════════════════════════════════════════════════════╝\n";

    bool server_required = false;

    // Parse command line arguments
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--require-server") {
            server_required = true;
        } else if (arg == "--server" && i + 1 < argc) {
            g_server_address = argv[++i];
            print_info("Using server: " + g_server_address);
        } else if (arg == "--help" || arg == "-h") {
            std::cout << "\nUsage: " << argv[0] << " [OPTIONS]\n";
            std::cout << "\nOptions:\n";
            std::cout << "  --server ADDRESS    ComfyUI server address (default: localhost:8188)\n";
            std::cout << "  --require-server    Fail if server is not available\n";
            std::cout << "  --help, -h          Show this help message\n";
            std::cout << "\nExamples:\n";
            std::cout << "  " << argv[0] << "\n";
            std::cout << "  " << argv[0] << " --server 192.168.1.211:8188\n";
            std::cout << "  " << argv[0] << " --server 192.168.1.211:8188 --require-server\n\n";
            return 0;
        }
    }

    if (!server_required && g_server_address == "localhost:8188") {
        print_info("Run with --require-server to fail if ComfyUI is not running");
        print_info("Run with --server ADDRESS to use a different server");
    }

    // Run all tests
    test_client_construction();

    bool server_available = test_server_connection();

    test_invalid_connection();

    if (server_available || !server_required) {
        test_queue_workflow();
        test_get_history();
        test_interrupt_execution();
    }

    test_directory_configuration();
    test_server_address_change();
    test_model_discovery();

    if (server_available || !server_required) {
        test_websocket_monitoring();
    }

    // Print summary
    std::cout << "\n";
    std::cout << "╔══════════════════════════════════════════════════════════╗\n";
    std::cout << "║                      Test Summary                        ║\n";
    std::cout << "╚══════════════════════════════════════════════════════════╝\n";
    std::cout << "  Tests run:    " << tests_run << "\n";
    std::cout << "  Tests passed: " << COLOR_GREEN << tests_passed << COLOR_RESET << "\n";
    std::cout << "  Tests failed: " << (tests_failed > 0 ? COLOR_RED : COLOR_GREEN)
              << tests_failed << COLOR_RESET << "\n";

    if (tests_failed == 0) {
        std::cout << "\n" << COLOR_GREEN << "✓ All tests passed!" << COLOR_RESET << "\n\n";
        return 0;
    } else {
        std::cout << "\n" << COLOR_RED << "✗ Some tests failed" << COLOR_RESET << "\n\n";
        return 1;
    }
}
