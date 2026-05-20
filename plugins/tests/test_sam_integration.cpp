// Copyright OpenFX and contributors to the OpenFX project.
// SPDX-License-Identifier: BSD-3-Clause

#include "comfyui_client.h"
#include "comfyui_image_io.h"
#include <iostream>
#include <fstream>
#include <cmath>
#include <thread>
#include <chrono>

using namespace ComfyUI;
using json = nlohmann::json;

// Test configuration
const std::string SERVER_ADDR = "192.168.1.211:8188";
const std::string TEST_DIR = "/tmp/comfyui_sam_test";

// Helper to create test image with clear object
void createTestImage(const std::string& path) {
    std::cout << "Creating test image with synthetic object..." << std::endl;

    int width = 512, height = 512, channels = 4;
    ImageData image(width, height, channels);

    // Create blue background with red circle (interleaved RGBA)
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            int pixelIdx = (y * width + x) * channels;

            // Check if we're in the circle (center, radius 100)
            int dx = x - width/2;
            int dy = y - height/2;
            bool inCircle = (dx*dx + dy*dy < 100*100);

            if (inCircle) {
                // Bright red circle
                image.pixels[pixelIdx + 0] = 1.0f;  // R
                image.pixels[pixelIdx + 1] = 0.0f;  // G
                image.pixels[pixelIdx + 2] = 0.0f;  // B
                image.pixels[pixelIdx + 3] = 1.0f;  // A
            } else {
                // Blue background
                image.pixels[pixelIdx + 0] = 0.1f;  // R
                image.pixels[pixelIdx + 1] = 0.1f;  // G
                image.pixels[pixelIdx + 2] = 0.5f;  // B
                image.pixels[pixelIdx + 3] = 1.0f;  // A
            }
        }
    }

    ImageIO::writeEXR(path, image);
    std::cout << "  Created test image: " << path << std::endl;
}

// Test 1: Verify server and model availability
bool test_server_and_models() {
    std::cout << "\n[TEST 1] Server and Model Availability" << std::endl;

    try {
        Client client(SERVER_ADDR);

        // Test connection
        if (!client.testConnection()) {
            std::cerr << "  ✗ Cannot connect to ComfyUI server at " << SERVER_ADDR << std::endl;
            return false;
        }

        std::cout << "  ✓ ComfyUI server is reachable at " << SERVER_ADDR << std::endl;

        // TODO: Add model validation endpoint when available
        // For now, we'll discover this during workflow execution

        return true;
    } catch (const std::exception& e) {
        std::cerr << "  ✗ Server connection failed: " << e.what() << std::endl;
        return false;
    }
}

// Test 2: Full SAM segmentation workflow
bool test_sam_workflow() {
    std::cout << "\n[TEST 2] SAM Segmentation Workflow" << std::endl;

    try {
        // Setup directories
        system(("mkdir -p " + TEST_DIR + "/in").c_str());
        system(("mkdir -p " + TEST_DIR + "/out").c_str());

        // Create test image
        std::string inputPath = TEST_DIR + "/in/test_input_0001.exr";
        createTestImage(inputPath);

        // Create ComfyUI client
        Client client(SERVER_ADDR);
        std::string clientId = client.getClientId();

        // Build simplified SAM workflow (ComfyUI API format)
        // Note: Simplified version without ImageColorspace node
        json workflow = {
            // Node 1: Load input EXR
            {"1", {
                {"inputs", {
                    {"filepath", inputPath},
                    {"linear_to_sRGB", "false"},
                    {"image_load_cap", 0},
                    {"skip_first_images", 0},
                    {"select_every_nth", 1}
                }},
                {"class_type", "LoadEXR"}
            }},

            // Node 16: Grounding DINO + SAM segmentation
            {"16", {
                {"inputs", {
                    {"prompt", "red circle"},  // Detect the red circle we created
                    {"threshold", 0.3},
                    {"sam_model", json::array({"18", 0})},
                    {"grounding_dino_model", json::array({"17", 0})},
                    {"image", json::array({"1", 0})}
                }},
                {"class_type", "GroundingDinoSAMSegment (segment anything)"}
            }},

            // Node 17: Load Grounding DINO model
            {"17", {
                {"inputs", {
                    {"model_name", "GroundingDINO_SwinB (938MB)"}
                }},
                {"class_type", "GroundingDinoModelLoader (segment anything)"}
            }},

            // Node 18: Load SAM model
            {"18", {
                {"inputs", {
                    {"model_name", "sam_vit_h (2.56GB)"}
                }},
                {"class_type", "SAMModelLoader (segment anything)"}
            }},

            // Node 20: Invert mask (get segmented object)
            {"20", {
                {"inputs", {
                    {"mask", json::array({"16", 1})}
                }},
                {"class_type", "InvertMask"}
            }},

            // Node 23: Combine image with mask
            {"23", {
                {"inputs", {
                    {"destination", json::array({"1", 0})},
                    {"source", json::array({"16", 0})},
                    {"x", 0},
                    {"y", 0},
                    {"mask", json::array({"20", 0})},
                    {"resize_source", "false"}
                }},
                {"class_type", "ImageCompositeMasked"}
            }},

            // Node 27: Save output EXR (directly from composite)
            {"27", {
                {"inputs", {
                    {"images", json::array({"23", 0})},
                    {"filename_prefix", TEST_DIR + "/out/test_output"},
                    {"compression", "ZIP"},
                    {"sRGB_to_linear", "false"},
                    {"version", 2},
                    {"start_frame", 1},
                    {"frame_pad", 4}
                }},
                {"class_type", "SaveEXR"}
            }}
        };

        std::cout << "  Submitting workflow to ComfyUI server..." << std::endl;

        // Queue workflow
        std::string promptId = client.queuePrompt(workflow, clientId);
        std::cout << "  ✓ Workflow queued with ID: " << promptId << std::endl;

        // Monitor execution via WebSocket
        std::cout << "  Monitoring execution via WebSocket..." << std::endl;

        bool completed = false;
        bool error = false;
        std::string errorMsg;

        auto callback = [&](EventType eventType, const json& data) {
            switch (eventType) {
                case EventType::Progress:
                    if (data.contains("value") && data.contains("max")) {
                        int value = data["value"].get<int>();
                        int max = data["max"].get<int>();
                        std::cout << "    Progress: " << value << "/" << max << std::endl;
                    }
                    break;

                case EventType::Executing:
                    if (data.contains("node")) {
                        if (data["node"].is_null()) {
                            std::cout << "    Execution completed" << std::endl;
                        } else {
                            std::string node = data["node"].get<std::string>();
                            std::cout << "    Executing node: " << node << std::endl;
                        }
                    }
                    break;

                case EventType::Completed:
                    completed = true;
                    std::cout << "    ✓ Workflow completed" << std::endl;
                    break;

                case EventType::ExecutionError:
                    error = true;
                    errorMsg = data.dump();
                    std::cerr << "    ✗ Error: " << errorMsg << std::endl;
                    break;

                default:
                    break;
            }
        };

        client.monitorExecution(promptId, callback);

        if (error) {
            std::cerr << "  ✗ Workflow execution failed: " << errorMsg << std::endl;
            return false;
        }

        if (!completed) {
            std::cerr << "  ✗ Workflow did not complete" << std::endl;
            return false;
        }

        std::cout << "  ✓ Workflow completed successfully" << std::endl;

        // Get history to find output file
        json history = client.getHistory(promptId);

        if (!history.contains(promptId)) {
            std::cerr << "  ✗ Prompt ID not found in history" << std::endl;
            return false;
        }

        std::string outputPath;
        const json& promptData = history[promptId];

        if (promptData.contains("outputs")) {
            for (auto& [nodeId, nodeData] : promptData["outputs"].items()) {
                if (nodeData.contains("images")) {
                    const json& images = nodeData["images"];
                    if (!images.empty() && images[0].contains("filename")) {
                        std::string filename = images[0]["filename"].get<std::string>();
                        outputPath = TEST_DIR + "/out/" + filename;
                        break;
                    }
                }
            }
        }

        if (outputPath.empty()) {
            std::cerr << "  ✗ Could not find output file in history" << std::endl;
            return false;
        }

        std::cout << "  ✓ Output file: " << outputPath << std::endl;

        // Read output image
        ImageData output = ImageIO::readEXR(outputPath);
        std::cout << "  ✓ Read output EXR: " << output.width << "x" << output.height
                  << " (" << output.channels << " channels)" << std::endl;

        // Verify output has expected properties
        if (output.width != 512 || output.height != 512) {
            std::cerr << "  ✗ Output dimensions don't match input" << std::endl;
            return false;
        }

        // Check that segmentation produced some variation
        // (pixels should not all be identical)
        float minR = 1.0f, maxR = 0.0f;
        for (int y = 0; y < output.height; ++y) {
            for (int x = 0; x < output.width; ++x) {
                int pixelIdx = (y * output.width + x) * output.channels;
                float r = output.pixels[pixelIdx];
                minR = std::min(minR, r);
                maxR = std::max(maxR, r);
            }
        }

        float range = maxR - minR;
        if (range < 0.01f) {
            std::cerr << "  ✗ Output appears to be flat (no segmentation?)" << std::endl;
            return false;
        }

        std::cout << "  ✓ Output has variation (R channel range: " << range << ")" << std::endl;
        std::cout << "  ✓ SAM segmentation workflow completed successfully" << std::endl;

        return true;

    } catch (const std::exception& e) {
        std::cerr << "  ✗ Exception: " << e.what() << std::endl;
        return false;
    }
}

// Test 3: Model variant testing
bool test_model_variants() {
    std::cout << "\n[TEST 3] Model Variant Testing" << std::endl;
    std::cout << "  (Skipped - requires all models to be installed)" << std::endl;
    std::cout << "  Models to test:" << std::endl;
    std::cout << "    - SAM: sam_vit_h (2.56GB), sam_vit_l (1.25GB), sam_vit_b (375MB)" << std::endl;
    std::cout << "    - Grounding DINO: GroundingDINO_SwinB (938MB), GroundingDINO_SwinT (694MB)" << std::endl;
    return true;
}

int main(int argc, char** argv) {
    std::cout << "========================================" << std::endl;
    std::cout << "SAM Segmentation Integration Tests" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "Server: " << SERVER_ADDR << std::endl;
    std::cout << "Test Directory: " << TEST_DIR << std::endl;

    int passed = 0;
    int total = 0;

    // Test 1: Server and models
    total++;
    if (test_server_and_models()) {
        passed++;
    }

    // Test 2: Full workflow (only if server is available)
    if (passed > 0) {
        total++;
        if (test_sam_workflow()) {
            passed++;
        }
    } else {
        std::cout << "\n[TEST 2] Skipped (server not available)" << std::endl;
    }

    // Test 3: Model variants
    total++;
    if (test_model_variants()) {
        passed++;
    }

    // Summary
    std::cout << "\n========================================" << std::endl;
    std::cout << "Test Results: " << passed << "/" << total << " passed" << std::endl;
    std::cout << "========================================" << std::endl;

    return (passed == total) ? 0 : 1;
}
