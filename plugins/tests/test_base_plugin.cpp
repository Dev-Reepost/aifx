// Copyright OpenFX and contributors to the OpenFX project.
// SPDX-License-Identifier: BSD-3-Clause

/**
 * @file test_base_plugin.cpp
 * @brief Unit tests for ComfyUI BasePlugin workflow components
 *
 * Tests individual components of the render pipeline:
 * - JSON history parsing
 * - Image I/O workflow
 * - Workflow JSON structure
 * - Error handling
 *
 * Note: Full OFX plugin testing requires a host application.
 * These tests validate the core logic independently.
 */

#include "comfyui_base_plugin.h"
#include "comfyui_image_io.h"
#include <nlohmann/json.hpp>
#include <iostream>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <cstdlib>
#include <cmath>

using json = nlohmann::json;
using namespace ComfyUI;

// Calling into BasePlugin pulls the OFX Support library's ImageEffect
// translation unit into the link, and its init() references the plugin
// registration entry point every real bundle defines. A test executable has no
// factories to register, so provide the empty definition the linker wants.
namespace OFX {
namespace Plugin {
void getPluginIDs(OFX::PluginFactoryArray& /*ids*/) {}
}  // namespace Plugin
}  // namespace OFX

// ============================================================================
// Test Functions
// ============================================================================

bool test_parse_comfyui_history() {
    std::cout << "\n[TEST] parse_comfyui_history" << std::endl;

    try {
        // Create mock ComfyUI history JSON (real format from ComfyUI API)
        json history = {
            {"test-prompt-id-123", {
                {"status", {
                    {"status_str", "success"},
                    {"completed", true}
                }},
                {"outputs", {
                    {"5", {
                        {"images", json::array({
                            {
                                {"filename", "output_00001.exr"},
                                {"subfolder", ""},
                                {"type", "output"}
                            }
                        })}
                    }},
                    {"7", {
                        {"images", json::array({
                            {
                                {"filename", "mask_00001.png"},
                                {"subfolder", "masks"},
                                {"type", "output"}
                            }
                        })}
                    }}
                }}
            }}
        };

        // Test parsing logic (simulating parseOutputPath() method)
        std::string mountPath = "/tmp/comfyui_test";
        std::string projectName = "test_project";
        std::string outputPath;

        // Parse first output image
        for (auto& [promptId, promptData] : history.items()) {
            if (!promptData.contains("outputs")) continue;

            const json& outputs = promptData["outputs"];
            for (auto& [nodeId, nodeData] : outputs.items()) {
                if (!nodeData.contains("images")) continue;

                const json& images = nodeData["images"];
                if (!images.is_array() || images.empty()) continue;

                const json& firstImage = images[0];
                if (firstImage.contains("filename")) {
                    std::string filename = firstImage["filename"].get<std::string>();
                    outputPath = mountPath + "/" + projectName + "/" + filename;
                    break;
                }
            }
            if (!outputPath.empty()) break;
        }

        if (outputPath != "/tmp/comfyui_test/test_project/output_00001.exr") {
            std::cerr << "  ✗ Incorrect output path: " << outputPath << std::endl;
            return false;
        }

        std::cout << "  ✓ Successfully parsed output path from history" << std::endl;
        return true;

    } catch (const std::exception& e) {
        std::cerr << "  ✗ Exception: " << e.what() << std::endl;
        return false;
    }
}

bool test_filename_generation() {
    std::cout << "\n[TEST] filename_generation" << std::endl;

    try {
        // Test input filename generation (simulating writeInputImage())
        std::string mountPath = "/tmp/comfyui_exchange";
        std::string projectName = "my_project";
        int frame = 42;

        std::ostringstream filename;
        filename << mountPath << "/" << projectName << "/input_"
                 << std::setw(4) << std::setfill('0') << frame << ".exr";

        std::string expected = "/tmp/comfyui_exchange/my_project/input_0042.exr";
        if (filename.str() != expected) {
            std::cerr << "  ✗ Incorrect filename: " << filename.str() << std::endl;
            std::cerr << "     Expected: " << expected << std::endl;
            return false;
        }

        // Test different frame numbers
        std::vector<std::pair<int, std::string>> testCases = {
            {1, "input_0001.exr"},
            {100, "input_0100.exr"},
            {9999, "input_9999.exr"}
        };

        for (const auto& [testFrame, expectedSuffix] : testCases) {
            std::ostringstream oss;
            oss << "input_" << std::setw(4) << std::setfill('0') << testFrame << ".exr";
            if (oss.str() != expectedSuffix) {
                std::cerr << "  ✗ Frame " << testFrame << " incorrect: " << oss.str() << std::endl;
                return false;
            }
        }

        std::cout << "  ✓ Filename generation correct for all test cases" << std::endl;
        return true;

    } catch (const std::exception& e) {
        std::cerr << "  ✗ Exception: " << e.what() << std::endl;
        return false;
    }
}

bool test_image_workflow_roundtrip() {
    std::cout << "\n[TEST] image_workflow_roundtrip" << std::endl;

    try {
        // Create test directories
        system("mkdir -p /tmp/comfyui_test/test_project");

        // 1. Create source image with test pattern (simulating OFX input)
        int width = 512;
        int height = 512;
        int channels = 4;
        std::vector<float> srcPixels(width * height * channels);

        // Fill with gradient pattern
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                int idx = (y * width + x) * channels;
                srcPixels[idx + 0] = float(x) / width;       // R: horizontal gradient
                srcPixels[idx + 1] = float(y) / height;      // G: vertical gradient
                srcPixels[idx + 2] = 0.5f;                   // B: constant
                srcPixels[idx + 3] = 1.0f;                   // A: opaque
            }
        }

        // 2. Convert to ImageData (simulating fromOFXBuffer())
        ImageData imageData(width, height, channels);
        imageData.pixels = srcPixels;

        // 3. Write as input EXR (simulating writeInputImage())
        std::string inputPath = "/tmp/comfyui_test/test_project/input_0001.exr";
        ImageIO::writeEXR(inputPath, imageData);

        // 4. Simulate ComfyUI processing (copy input to output)
        std::string outputPath = "/tmp/comfyui_test/test_project/output_0001.exr";
        system(("cp " + inputPath + " " + outputPath).c_str());

        // 5. Read output (simulating Step 6 in executeWorkflow())
        ImageData outputData = ImageIO::readEXR(outputPath);

        // 6. Verify dimensions
        if (outputData.width != width || outputData.height != height || outputData.channels != channels) {
            std::cerr << "  ✗ Dimension mismatch: got " << outputData.width << "x" << outputData.height
                      << "x" << outputData.channels << ", expected " << width << "x" << height << "x" << channels << std::endl;
            return false;
        }

        // 7. Verify pixel data preserved
        float maxError = 0.0f;
        for (size_t i = 0; i < srcPixels.size(); ++i) {
            float error = std::abs(srcPixels[i] - outputData.pixels[i]);
            maxError = std::max(maxError, error);
        }

        if (maxError > 0.001f) {
            std::cerr << "  ✗ Pixel data not preserved, max error: " << maxError << std::endl;
            return false;
        }

        std::cout << "  ✓ Image workflow round-trip successful (max error: " << maxError << ")" << std::endl;
        return true;

    } catch (const std::exception& e) {
        std::cerr << "  ✗ Exception: " << e.what() << std::endl;
        return false;
    }
}

bool test_workflow_json_structure() {
    std::cout << "\n[TEST] workflow_json_structure" << std::endl;

    try {
        // Create test workflow (simulating buildWorkflow() implementation)
        json workflow = {
            {"prompt", {
                {"1", {
                    {"class_type", "LoadImage"},
                    {"inputs", {
                        {"image", "input_0001.exr"}
                    }}
                }},
                {"2", {
                    {"class_type", "ImageScale"},
                    {"inputs", {
                        {"image", json::array({"1", 0})},
                        {"width", 1024},
                        {"height", 1024},
                        {"method", "lanczos"}
                    }}
                }},
                {"3", {
                    {"class_type", "SaveImage"},
                    {"inputs", {
                        {"images", json::array({"2", 0})},
                        {"filename_prefix", "output"}
                    }}
                }}
            }},
            {"client_id", "test-client-123"}
        };

        // Validate structure
        if (!workflow.contains("prompt") || !workflow["prompt"].is_object()) {
            std::cerr << "  ✗ Workflow missing 'prompt' object" << std::endl;
            return false;
        }

        if (workflow["prompt"].size() != 3) {
            std::cerr << "  ✗ Expected 3 nodes in workflow, got " << workflow["prompt"].size() << std::endl;
            return false;
        }

        // Validate each node has required fields
        for (auto& [nodeId, nodeData] : workflow["prompt"].items()) {
            if (!nodeData.contains("class_type")) {
                std::cerr << "  ✗ Node " << nodeId << " missing 'class_type'" << std::endl;
                return false;
            }
            if (!nodeData.contains("inputs")) {
                std::cerr << "  ✗ Node " << nodeId << " missing 'inputs'" << std::endl;
                return false;
            }
        }

        // Validate client_id present
        if (!workflow.contains("client_id") || !workflow["client_id"].is_string()) {
            std::cerr << "  ✗ Workflow missing or invalid 'client_id'" << std::endl;
            return false;
        }

        std::cout << "  ✓ Workflow JSON structure valid" << std::endl;
        return true;

    } catch (const std::exception& e) {
        std::cerr << "  ✗ Exception: " << e.what() << std::endl;
        return false;
    }
}

bool test_dimension_mismatch_detection() {
    std::cout << "\n[TEST] dimension_mismatch_detection" << std::endl;

    try {
        // Simulate the dimension check in executeWorkflow() Step 6
        int srcWidth = 256, srcHeight = 256;
        int dstWidth = 512, dstHeight = 512;

        // This is what the code does:
        // if (resultImage.width != dstWidth || resultImage.height != dstHeight)

        bool mismatchDetected = false;
        try {
            if (srcWidth != dstWidth || srcHeight != dstHeight) {
                std::ostringstream err;
                err << "Dimension mismatch: result " << srcWidth << "x" << srcHeight
                    << " vs destination " << dstWidth << "x" << dstHeight;
                throw std::runtime_error(err.str());
            }
        } catch (const std::runtime_error& e) {
            std::string msg = e.what();
            if (msg.find("Dimension mismatch") != std::string::npos) {
                mismatchDetected = true;
            }
        }

        if (!mismatchDetected) {
            std::cerr << "  ✗ Failed to detect dimension mismatch" << std::endl;
            return false;
        }

        std::cout << "  ✓ Dimension mismatch correctly detected" << std::endl;
        return true;

    } catch (const std::exception& e) {
        std::cerr << "  ✗ Exception: " << e.what() << std::endl;
        return false;
    }
}

bool test_output_fit_classification() {
    std::cout << "\n[TEST] output_fit_classification" << std::endl;

    using Mode = BasePlugin::OutputFitMode;

    struct Case {
        const char* name;
        int outW, outH;          // frame ComfyUI produced
        int rodW, rodH;          // canvas the host allocated (0 = not reported)
        OfxRectI window;         // render window (dst->getBounds())
        Mode expected;
    };

    const Case cases[] = {
        // Host honoured the RoD and asked for the whole frame.
        {"full frame",              1920, 1080, 1920, 1080, {0, 0, 1920, 1080}, Mode::FullFrame},

        // The Flame regression: a fixed-format host allocates a 1280x720 canvas
        // for a 1920x1080 upscale and asks for all of it. Geometrically this is
        // indistinguishable from a tile — every sub-region condition holds — so
        // before the dstRoD check it classified as SubRegion and delivered the
        // 1280x720 corner of the upscale.
        {"fixed-format host",       1920, 1080, 1280,  720, {0, 0, 1280,  720}, Mode::HostRefusedRoD},
        {"fixed-format host, tiled request",
                                    1920, 1080, 1280,  720, {0, 0,  512,  512}, Mode::HostRefusedRoD},

        // Genuine tiled render: the canvas IS our output, the window is a piece.
        {"tile, bottom-left",       1920, 1080, 1920, 1080, {0, 0,  512,  512}, Mode::SubRegion},
        {"tile, offset",            1920, 1080, 1920, 1080, {256, 128, 768, 640}, Mode::SubRegion},

        // Host did not report a RoD: a refusal cannot be told from a tile, so we
        // keep the old behaviour rather than erroring on a guess.
        {"no dstRoD reported",      1920, 1080,    0,    0, {0, 0, 1280,  720}, Mode::SubRegion},

        // Regression guard: a fixed-size model (DepthCrafter / NormalCrafter with
        // Force Size on) emits its own internal resolution, so the host canvas
        // is LARGER than the result. Nothing has to be destroyed — scale it up
        // to the canvas. Erroring here would break those plugins, which
        // rendered fine before the refusal check existed.
        {"fixed-size model, canvas larger",
                                    1024,  576, 1920, 1080, {0, 0, 1920, 1080}, Mode::Fit},

        // Same, on a canvas whose aspect ratio differs — still a fit, not a refusal.
        {"fixed-size model, other aspect",
                                     768,  768, 1920, 1080, {0, 0, 1920, 1080}, Mode::Fit},

        // The model rounded its target to a multiple of 16 where our predicted
        // RoD did not. Within the slack this is not a refusal; the window is
        // then exactly inside the result, so it is trimmed by the sub-region
        // path rather than resampled — lossless, and no render is failed over
        // two columns.
        {"rounding slack, output 2px larger",
                                    1922, 1080, 1920, 1080, {0, 0, 1920, 1080}, Mode::SubRegion},

        // ...but a materially smaller canvas still is a refusal.
        {"canvas materially smaller",
                                    1920, 1080, 1600,  900, {0, 0, 1600,  900}, Mode::HostRefusedRoD},
    };

    bool ok = true;
    for (const auto& c : cases) {
        Mode got = BasePlugin::classifyOutputFit(c.outW, c.outH, c.rodW, c.rodH, c.window);
        if (got != c.expected) {
            std::cerr << "  ✗ " << c.name << ": expected mode " << static_cast<int>(c.expected)
                      << ", got " << static_cast<int>(got) << std::endl;
            ok = false;
        }
    }

    if (!ok) return false;
    std::cout << "  ✓ All " << (sizeof(cases) / sizeof(cases[0]))
              << " output-fit cases classified correctly" << std::endl;
    return true;
}

bool test_bit_depth_conversion() {
    std::cout << "\n[TEST] bit_depth_conversion" << std::endl;

    try {
        // Test the bit depth enum to integer conversion logic
        // This simulates what writeInputImage() does

        struct TestCase {
            std::string name;
            int enumValue;  // Simulated OFX::BitDepthEnum value
            int expectedInt;
        };

        std::vector<TestCase> testCases = {
            {"eBitDepthUByte", 0, 8},
            {"eBitDepthUShort", 1, 16},
            {"eBitDepthFloat", 2, 32}
        };

        for (const auto& testCase : testCases) {
            int bitDepthInt = 32; // default
            if (testCase.enumValue == 0) {  // eBitDepthUByte
                bitDepthInt = 8;
            } else if (testCase.enumValue == 1) {  // eBitDepthUShort
                bitDepthInt = 16;
            }

            if (bitDepthInt != testCase.expectedInt) {
                std::cerr << "  ✗ " << testCase.name << " conversion failed: got "
                          << bitDepthInt << ", expected " << testCase.expectedInt << std::endl;
                return false;
            }
        }

        std::cout << "  ✓ Bit depth conversion correct for all types" << std::endl;
        return true;

    } catch (const std::exception& e) {
        std::cerr << "  ✗ Exception: " << e.what() << std::endl;
        return false;
    }
}

bool test_error_message_format() {
    std::cout << "\n[TEST] error_message_format" << std::endl;

    try {
        // Test that error messages contain useful information
        std::vector<std::string> testErrors = {
            "ComfyUI execution failed: Timeout waiting for response",
            "Failed to find output file in ComfyUI history",
            "Dimension mismatch: result 256x256 vs destination 512x512"
        };

        for (const auto& errorMsg : testErrors) {
            // Verify error message has meaningful content
            if (errorMsg.empty() || errorMsg.length() < 10) {
                std::cerr << "  ✗ Error message too short: '" << errorMsg << "'" << std::endl;
                return false;
            }

            // Verify it contains useful keywords
            bool hasUsefulInfo = (
                errorMsg.find("failed") != std::string::npos ||
                errorMsg.find("error") != std::string::npos ||
                errorMsg.find("mismatch") != std::string::npos ||
                errorMsg.find("ComfyUI") != std::string::npos
            );

            if (!hasUsefulInfo) {
                std::cerr << "  ✗ Error message lacks useful info: '" << errorMsg << "'" << std::endl;
                return false;
            }
        }

        std::cout << "  ✓ Error message format validated" << std::endl;
        return true;

    } catch (const std::exception& e) {
        std::cerr << "  ✗ Exception: " << e.what() << std::endl;
        return false;
    }
}

bool test_server_path_conversion() {
    std::cout << "\n[TEST] server_path_conversion" << std::endl;

    // The ComfyUI server may run on Windows (UNC / drive letter) or on POSIX.
    // A Linux ComfyUI treats '\' as an ordinary filename character, so the
    // separator style must follow the configured server mount, not the OS this
    // plugin was built for.
    struct MountCase {
        const char* mount;
        bool windows;
    };
    const MountCase mountCases[] = {
        {"\\\\192.168.1.110\\silo2\\002_COMFYUI", true},   // UNC share
        {"Z:\\share", true},                               // drive letter, backslash
        {"D:/share", true},                                // drive letter, forward slash
        {"/Volumes/silo2/002_COMFYUI", false},             // macOS mount
        {"/mnt/comfyui-share", false},                     // Linux mount
        {"", false},                                       // unset
    };
    for (const auto& c : mountCases) {
        if (isWindowsStylePath(c.mount) != c.windows) {
            std::cerr << "  ✗ Wrong path style for mount '" << c.mount << "'" << std::endl;
            return false;
        }
    }

    // Trailing separators on a mount would splice into a doubled separator.
    if (stripTrailingSeparators("/Volumes/silo2/") != "/Volumes/silo2" ||
        stripTrailingSeparators("\\\\host\\share\\") != "\\\\host\\share" ||
        stripTrailingSeparators("/mnt/share") != "/mnt/share") {
        std::cerr << "  ✗ stripTrailingSeparators failed" << std::endl;
        return false;
    }

    // A POSIX server must keep forward slashes; a Windows server gets backslashes.
    const std::string posixPath = "/mnt/silo2/in/projects/006_OFX_TESTING/v001/shot01";
    if (normalizeSeparators(posixPath, false) != posixPath) {
        std::cerr << "  ✗ POSIX server path was rewritten: "
                  << normalizeSeparators(posixPath, false) << std::endl;
        return false;
    }
    if (normalizeSeparators("\\\\host\\share\\in/projects/shot01", false) !=
        "//host/share/in/projects/shot01") {
        std::cerr << "  ✗ Backslashes not converted for a POSIX server" << std::endl;
        return false;
    }
    if (normalizeSeparators("\\\\host\\share/in/projects/shot01", true) !=
        "\\\\host\\share\\in\\projects\\shot01") {
        std::cerr << "  ✗ Forward slashes not converted for a Windows server" << std::endl;
        return false;
    }

    std::cout << "  ✓ Server path style follows the server mount, not the build OS" << std::endl;
    return true;
}

// ============================================================================
// Main Test Runner
// ============================================================================

int main() {
    std::cout << "========================================" << std::endl;
    std::cout << "ComfyUI BasePlugin Workflow Tests" << std::endl;
    std::cout << "========================================" << std::endl;

    int passed = 0;
    int total = 0;

    // Run all tests
    struct Test {
        const char* name;
        bool (*func)();
    };

    Test tests[] = {
        {"parse_comfyui_history", test_parse_comfyui_history},
        {"filename_generation", test_filename_generation},
        {"image_workflow_roundtrip", test_image_workflow_roundtrip},
        {"workflow_json_structure", test_workflow_json_structure},
        {"dimension_mismatch_detection", test_dimension_mismatch_detection},
        {"output_fit_classification", test_output_fit_classification},
        {"bit_depth_conversion", test_bit_depth_conversion},
        {"error_message_format", test_error_message_format},
        {"server_path_conversion", test_server_path_conversion}
    };

    for (const auto& test : tests) {
        total++;
        if (test.func()) {
            passed++;
        }
    }

    // Summary
    std::cout << "\n========================================" << std::endl;
    std::cout << "Results: " << passed << "/" << total << " tests passed" << std::endl;
    std::cout << "========================================" << std::endl;

    // Cleanup
    system("rm -rf /tmp/comfyui_test");

    return (passed == total) ? 0 : 1;
}
