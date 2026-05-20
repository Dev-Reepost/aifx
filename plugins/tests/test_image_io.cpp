// Copyright OpenFX and contributors to the OpenFX project.
// SPDX-License-Identifier: BSD-3-Clause

#include "comfyui_image_io.h"
#include <iostream>
#include <cmath>

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

bool test_create_and_write_exr() {
    std::cout << "\n==== Test 1: Create and Write EXR ====" << std::endl;

    try {
        // Create a simple gradient image
        int width = 256;
        int height = 256;
        ComfyUI::ImageData image(width, height, 4); // RGBA

        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                int idx = (y * width + x) * 4;
                image.pixels[idx + 0] = static_cast<float>(x) / width;        // R
                image.pixels[idx + 1] = static_cast<float>(y) / height;       // G
                image.pixels[idx + 2] = 0.5f;                                  // B
                image.pixels[idx + 3] = 1.0f;                                  // A
            }
        }

        // Write to file
        std::string filename = "/tmp/test_gradient.exr";
        ComfyUI::ImageIO::writeEXR(filename, image);

        print_success("Created and wrote gradient EXR file");
        print_info("File: " + filename);
        print_info("Size: " + std::to_string(width) + "x" + std::to_string(height));

        return true;
    } catch (const std::exception& e) {
        print_failure(std::string("Exception: ") + e.what());
        return false;
    }
}

bool test_read_exr() {
    std::cout << "\n==== Test 2: Read EXR ====" << std::endl;

    try {
        std::string filename = "/tmp/test_gradient.exr";

        // Read back the file we just wrote
        ComfyUI::ImageData image = ComfyUI::ImageIO::readEXR(filename);

        print_success("Read EXR file successfully");
        print_info("Dimensions: " + std::to_string(image.width) + "x" + std::to_string(image.height));
        print_info("Channels: " + std::to_string(image.channels));
        print_info("Pixels: " + std::to_string(image.pixels.size()));

        // Verify dimensions
        if (image.width != 256 || image.height != 256) {
            print_failure("Incorrect dimensions");
            return false;
        }

        // Verify channel count
        if (image.channels != 4) {
            print_failure("Incorrect channel count");
            return false;
        }

        // Verify a few pixel values (approximate due to half-float conversion)
        int idx0 = 0; // Top-left corner
        if (std::abs(image.pixels[idx0 + 0] - 0.0f) > 0.01f ||
            std::abs(image.pixels[idx0 + 1] - 0.0f) > 0.01f) {
            print_failure("Incorrect pixel values at (0,0)");
            return false;
        }

        int idx_mid = ((128 * 256) + 128) * 4; // Middle
        if (std::abs(image.pixels[idx_mid + 0] - 0.5f) > 0.01f ||
            std::abs(image.pixels[idx_mid + 1] - 0.5f) > 0.01f) {
            print_failure("Incorrect pixel values at (128,128)");
            return false;
        }

        print_success("Pixel values verified");
        return true;
    } catch (const std::exception& e) {
        print_failure(std::string("Exception: ") + e.what());
        return false;
    }
}

bool test_ofx_buffer_conversion() {
    std::cout << "\n==== Test 3: OFX Buffer Conversion ====" << std::endl;

    try {
        // Create a simple test image
        int width = 16;
        int height = 16;
        int components = 4;

        // Create float buffer (32-bit)
        std::vector<float> srcBuffer(width * height * components);
        for (int i = 0; i < width * height; ++i) {
            srcBuffer[i * 4 + 0] = 1.0f;  // R
            srcBuffer[i * 4 + 1] = 0.5f;  // G
            srcBuffer[i * 4 + 2] = 0.25f; // B
            srcBuffer[i * 4 + 3] = 1.0f;  // A
        }

        // Convert from OFX buffer
        ComfyUI::ImageData image = ComfyUI::ImageIO::fromOFXBuffer(
            srcBuffer.data(),
            width,
            height,
            width * components * sizeof(float), // rowBytes
            components,
            32 // 32-bit float
        );

        print_success("Converted from OFX buffer");
        print_info("Image: " + std::to_string(image.width) + "x" + std::to_string(image.height));

        // Verify
        if (std::abs(image.pixels[0] - 1.0f) > 0.001f ||
            std::abs(image.pixels[1] - 0.5f) > 0.001f ||
            std::abs(image.pixels[2] - 0.25f) > 0.001f) {
            print_failure("Incorrect pixel values after conversion");
            return false;
        }

        // Convert back to OFX buffer
        std::vector<float> dstBuffer(width * height * components);
        ComfyUI::ImageIO::toOFXBuffer(
            image,
            dstBuffer.data(),
            width * components * sizeof(float),
            components,
            32
        );

        print_success("Converted to OFX buffer");

        // Verify round-trip
        if (std::abs(dstBuffer[0] - 1.0f) > 0.001f ||
            std::abs(dstBuffer[1] - 0.5f) > 0.001f ||
            std::abs(dstBuffer[2] - 0.25f) > 0.001f) {
            print_failure("Incorrect pixel values after round-trip");
            return false;
        }

        print_success("Round-trip conversion verified");
        return true;
    } catch (const std::exception& e) {
        print_failure(std::string("Exception: ") + e.what());
        return false;
    }
}

bool test_8bit_conversion() {
    std::cout << "\n==== Test 4: 8-bit Conversion ====" << std::endl;

    try {
        int width = 4;
        int height = 4;
        int components = 3; // RGB

        // Create 8-bit buffer
        std::vector<uint8_t> srcBuffer(width * height * components);
        for (int i = 0; i < width * height; ++i) {
            srcBuffer[i * 3 + 0] = 255; // R
            srcBuffer[i * 3 + 1] = 128; // G
            srcBuffer[i * 3 + 2] = 0;   // B
        }

        // Convert from 8-bit
        ComfyUI::ImageData image = ComfyUI::ImageIO::fromOFXBuffer(
            srcBuffer.data(),
            width,
            height,
            width * components, // rowBytes
            components,
            8 // 8-bit
        );

        print_success("Converted from 8-bit buffer");

        // Verify
        if (std::abs(image.pixels[0] - 1.0f) > 0.01f ||
            std::abs(image.pixels[1] - (128.0f/255.0f)) > 0.01f ||
            std::abs(image.pixels[2] - 0.0f) > 0.01f) {
            print_failure("Incorrect 8-bit conversion");
            return false;
        }

        print_success("8-bit values verified");
        return true;
    } catch (const std::exception& e) {
        print_failure(std::string("Exception: ") + e.what());
        return false;
    }
}

int main() {
    std::cout << "\n";
    std::cout << "╔══════════════════════════════════════════════════════════╗\n";
    std::cout << "║       ComfyUI OFX Plugin - Image I/O Test Suite         ║\n";
    std::cout << "╚══════════════════════════════════════════════════════════╝\n";

    int passed = 0;
    int failed = 0;

    if (test_create_and_write_exr()) passed++; else failed++;
    if (test_read_exr()) passed++; else failed++;
    if (test_ofx_buffer_conversion()) passed++; else failed++;
    if (test_8bit_conversion()) passed++; else failed++;

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
