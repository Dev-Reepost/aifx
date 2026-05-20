// Copyright OpenFX and contributors to the OpenFX project.
// SPDX-License-Identifier: BSD-3-Clause

#include "comfyui_image_io.h"
#include <iostream>
#include <cmath>
#include <vector>

#define COLOR_GREEN "\033[32m"
#define COLOR_RED "\033[31m"
#define COLOR_YELLOW "\033[33m"
#define COLOR_RESET "\033[0m"

void print_success(const std::string& msg) {
    std::cout << COLOR_GREEN << "✓ " << msg << COLOR_RESET << std::endl;
}

void print_failure(const std::string& msg) {
    std::cout << COLOR_RED << "✗ " << msg << COLOR_RESET << std::endl;
}

void print_info(const std::string& msg) {
    std::cout << COLOR_YELLOW << "  " << msg << COLOR_RESET << std::endl;
}

void print_warning(const std::string& msg) {
    std::cout << COLOR_YELLOW << "⚠ " << msg << COLOR_RESET << std::endl;
}

bool test_validate_exr_file(const std::string& filepath) {
    std::cout << "\n==== Validating EXR File ====" << std::endl;
    print_info("File: " + filepath);

    try {
        // Read the EXR file
        ComfyUI::ImageData image = ComfyUI::ImageIO::readEXR(filepath);

        print_success("Read EXR file successfully");
        print_info("Dimensions: " + std::to_string(image.width) + "x" + std::to_string(image.height));
        print_info("Channels: " + std::to_string(image.channels));
        print_info("Total pixels: " + std::to_string(image.pixels.size()));

        // Check if image is blank (all pixels are zero or near-zero)
        float sum = 0.0f;
        float max_val = 0.0f;
        float min_val = 1e10f;

        for (size_t i = 0; i < image.pixels.size(); ++i) {
            float val = std::abs(image.pixels[i]);
            sum += val;
            max_val = std::max(max_val, val);
            min_val = std::min(min_val, val);
        }

        float avg = sum / image.pixels.size();

        print_info("Pixel statistics:");
        print_info("  Average value: " + std::to_string(avg));
        print_info("  Maximum value: " + std::to_string(max_val));
        print_info("  Minimum value: " + std::to_string(min_val));

        // Sample first pixel
        if (image.pixels.size() >= 4) {
            std::cout << COLOR_YELLOW << "  First pixel (RGBA): ["
                      << image.pixels[0] << ", "
                      << image.pixels[1] << ", "
                      << image.pixels[2] << ", "
                      << (image.channels >= 4 ? image.pixels[3] : 1.0f)
                      << "]" << COLOR_RESET << std::endl;
        }

        // Sample middle pixel
        if (image.width > 0 && image.height > 0) {
            int mid_x = image.width / 2;
            int mid_y = image.height / 2;
            int mid_idx = (mid_y * image.width + mid_x) * image.channels;

            if (mid_idx + image.channels <= image.pixels.size()) {
                std::cout << COLOR_YELLOW << "  Middle pixel (" << mid_x << "," << mid_y << "): ["
                          << image.pixels[mid_idx + 0] << ", "
                          << image.pixels[mid_idx + 1] << ", "
                          << image.pixels[mid_idx + 2] << ", "
                          << (image.channels >= 4 ? image.pixels[mid_idx + 3] : 1.0f)
                          << "]" << COLOR_RESET << std::endl;
            }
        }

        // Check if blank
        if (avg < 0.001f && max_val < 0.01f) {
            print_warning("IMAGE APPEARS TO BE BLANK!");
            print_warning("All pixel values are near zero");
            return false;
        }

        print_success("Image contains non-zero pixel data");
        return true;

    } catch (const std::exception& e) {
        print_failure(std::string("Exception: ") + e.what());
        return false;
    }
}

bool test_create_realistic_image() {
    std::cout << "\n==== Creating Realistic Test Image ====" << std::endl;

    try {
        int width = 1920;
        int height = 1080;
        ComfyUI::ImageData image(width, height, 4); // RGBA

        // Create a test pattern with various colors and gradients
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                int idx = (y * width + x) * 4;

                // Create quadrants with different patterns
                if (x < width / 2 && y < height / 2) {
                    // Top-left: Red gradient
                    image.pixels[idx + 0] = static_cast<float>(x) / (width / 2);
                    image.pixels[idx + 1] = 0.0f;
                    image.pixels[idx + 2] = 0.0f;
                    image.pixels[idx + 3] = 1.0f;
                } else if (x >= width / 2 && y < height / 2) {
                    // Top-right: Green gradient
                    image.pixels[idx + 0] = 0.0f;
                    image.pixels[idx + 1] = static_cast<float>(x - width / 2) / (width / 2);
                    image.pixels[idx + 2] = 0.0f;
                    image.pixels[idx + 3] = 1.0f;
                } else if (x < width / 2 && y >= height / 2) {
                    // Bottom-left: Blue gradient
                    image.pixels[idx + 0] = 0.0f;
                    image.pixels[idx + 1] = 0.0f;
                    image.pixels[idx + 2] = static_cast<float>(y - height / 2) / (height / 2);
                    image.pixels[idx + 3] = 1.0f;
                } else {
                    // Bottom-right: White gradient
                    float val = (static_cast<float>(x - width / 2) / (width / 2) +
                                static_cast<float>(y - height / 2) / (height / 2)) / 2.0f;
                    image.pixels[idx + 0] = val;
                    image.pixels[idx + 1] = val;
                    image.pixels[idx + 2] = val;
                    image.pixels[idx + 3] = 1.0f;
                }
            }
        }

        std::string filename = "/tmp/test_realistic_1920x1080.exr";
        ComfyUI::ImageIO::writeEXR(filename, image);

        print_success("Created realistic test image");
        print_info("File: " + filename);
        print_info("Size: " + std::to_string(width) + "x" + std::to_string(height));

        // Validate the file we just wrote
        return test_validate_exr_file(filename);

    } catch (const std::exception& e) {
        print_failure(std::string("Exception: ") + e.what());
        return false;
    }
}

bool test_ofx_buffer_to_exr_roundtrip() {
    std::cout << "\n==== OFX Buffer → EXR → OFX Buffer Round-trip ====" << std::endl;

    try {
        int width = 640;
        int height = 480;
        int components = 4;

        // Create source OFX-style buffer with known pattern
        std::vector<float> srcBuffer(width * height * components);
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                int idx = (y * width + x) * 4;
                srcBuffer[idx + 0] = 0.75f; // R
                srcBuffer[idx + 1] = 0.50f; // G
                srcBuffer[idx + 2] = 0.25f; // B
                srcBuffer[idx + 3] = 1.00f; // A
            }
        }

        print_info("Created source buffer with pattern [0.75, 0.50, 0.25, 1.00]");

        // Convert from OFX buffer
        ComfyUI::ImageData imageData = ComfyUI::ImageIO::fromOFXBuffer(
            srcBuffer.data(),
            width,
            height,
            width * components * sizeof(float),
            components,
            32
        );

        print_success("Converted from OFX buffer");
        print_info("First pixel: [" +
                   std::to_string(imageData.pixels[0]) + ", " +
                   std::to_string(imageData.pixels[1]) + ", " +
                   std::to_string(imageData.pixels[2]) + ", " +
                   std::to_string(imageData.pixels[3]) + "]");

        // Write to EXR
        std::string filename = "/tmp/test_ofx_roundtrip.exr";
        ComfyUI::ImageIO::writeEXR(filename, imageData);
        print_success("Wrote to EXR: " + filename);

        // Read back from EXR
        ComfyUI::ImageData imageRead = ComfyUI::ImageIO::readEXR(filename);
        print_success("Read back from EXR");
        print_info("First pixel after read: [" +
                   std::to_string(imageRead.pixels[0]) + ", " +
                   std::to_string(imageRead.pixels[1]) + ", " +
                   std::to_string(imageRead.pixels[2]) + ", " +
                   std::to_string(imageRead.pixels[3]) + "]");

        // Verify pixel values (allowing for half-float precision loss)
        float tolerance = 0.005f; // Half-float has ~0.001 precision
        if (std::abs(imageRead.pixels[0] - 0.75f) > tolerance ||
            std::abs(imageRead.pixels[1] - 0.50f) > tolerance ||
            std::abs(imageRead.pixels[2] - 0.25f) > tolerance ||
            std::abs(imageRead.pixels[3] - 1.00f) > tolerance) {
            print_failure("Pixel values changed after round-trip");
            print_info("Expected: [0.75, 0.50, 0.25, 1.00]");
            print_info("Got: [" +
                       std::to_string(imageRead.pixels[0]) + ", " +
                       std::to_string(imageRead.pixels[1]) + ", " +
                       std::to_string(imageRead.pixels[2]) + ", " +
                       std::to_string(imageRead.pixels[3]) + "]");
            return false;
        }

        print_success("Round-trip verified - pixel values preserved");
        return true;

    } catch (const std::exception& e) {
        print_failure(std::string("Exception: ") + e.what());
        return false;
    }
}

int main(int argc, char* argv[]) {
    std::cout << "\n";
    std::cout << "╔══════════════════════════════════════════════════════════╗\n";
    std::cout << "║       ComfyUI OFX Plugin - EXR Validation Test          ║\n";
    std::cout << "╚══════════════════════════════════════════════════════════╝\n";

    int passed = 0;
    int failed = 0;

    // Run standard tests
    if (test_create_realistic_image()) passed++; else failed++;
    if (test_ofx_buffer_to_exr_roundtrip()) passed++; else failed++;

    // If user provided a file path, validate it
    if (argc > 1) {
        std::string filepath = argv[1];
        if (test_validate_exr_file(filepath)) passed++; else failed++;
    } else {
        print_info("\nTo validate a specific EXR file, run:");
        print_info("  " + std::string(argv[0]) + " <path-to-exr-file>");
    }

    // Summary
    std::cout << "\n";
    std::cout << "╔══════════════════════════════════════════════════════════╗\n";
    std::cout << "║                      Test Summary                        ║\n";
    std::cout << "╚══════════════════════════════════════════════════════════╝\n";
    std::cout << "  Tests passed: " << COLOR_GREEN << passed << COLOR_RESET << std::endl;
    std::cout << "  Tests failed: " << (failed > 0 ? COLOR_RED : COLOR_GREEN)
              << failed << COLOR_RESET << std::endl;

    if (failed == 0) {
        std::cout << "\n" << COLOR_GREEN << "✓ All tests passed!" << COLOR_RESET << std::endl;
        return 0;
    } else {
        std::cout << "\n" << COLOR_RED << "✗ Some tests failed" << COLOR_RESET << std::endl;
        return 1;
    }
}
