// Copyright OpenFX and contributors to the OpenFX project.
// SPDX-License-Identifier: BSD-3-Clause

#ifndef COMFYUI_IMAGE_IO_H
#define COMFYUI_IMAGE_IO_H

#include <string>
#include <vector>
#include <stdexcept>

namespace ComfyUI {

/**
 * @brief Simple image data structure for EXR files
 */
struct ImageData {
    int width;
    int height;
    int channels;  // 3 for RGB, 4 for RGBA
    std::vector<float> pixels;  // Interleaved float data (RGBRGBRGB... or RGBARGBARGBA...)

    ImageData() : width(0), height(0), channels(0) {}
    ImageData(int w, int h, int c) : width(w), height(h), channels(c) {
        pixels.resize(w * h * c);
    }
};

/**
 * @brief Image I/O utilities for ComfyUI integration
 *
 * Handles reading and writing EXR files using TinyEXR.
 * Provides conversion between OFX image buffers and EXR files.
 */
namespace ImageIO {

/**
 * @brief Create directory recursively
 *
 * Creates all parent directories as needed (like mkdir -p).
 *
 * @param path Directory path to create
 * @return true if directory exists or was created successfully, false otherwise
 */
bool createDirectoryRecursive(const std::string& path);

/**
 * @brief Read an EXR file
 *
 * @param filename Path to EXR file
 * @return ImageData structure with pixel data
 * @throws std::runtime_error if file cannot be read
 */
ImageData readEXR(const std::string& filename);

/**
 * @brief Write an EXR file
 *
 * @param filename Path to output EXR file
 * @param image Image data to write
 * @throws std::runtime_error if file cannot be written
 */
void writeEXR(const std::string& filename, const ImageData& image);

/**
 * @brief Convert OFX image buffer to ImageData
 *
 * Handles various OFX pixel formats and converts to float RGB/RGBA.
 *
 * @param srcPixels Source pixel buffer
 * @param width Image width
 * @param height Image height
 * @param rowBytes Bytes per row (may include padding)
 * @param pixelComponents Number of components (3=RGB, 4=RGBA)
 * @param bitDepth Bit depth per component (8, 16, or 32)
 * @param flipY If true, flip Y axis (OFX bottom-left → EXR top-left).
 *              Pass false when the host already provides top-left-origin pixels
 *              (e.g. DaVinci Resolve). Default true matches OFX spec.
 * @return ImageData with float pixels
 */
ImageData fromOFXBuffer(
    const void* srcPixels,
    int width,
    int height,
    int rowBytes,
    int pixelComponents,
    int bitDepth,
    bool flipY = true
);

/**
 * @brief Convert ImageData to OFX image buffer
 *
 * @param image Source image data
 * @param dstPixels Destination pixel buffer
 * @param rowBytes Bytes per row (may include padding)
 * @param pixelComponents Number of components in destination
 * @param bitDepth Bit depth per component (8, 16, or 32)
 * @param flipY If true, flip Y axis (EXR top-left → OFX bottom-left).
 *              Pass false when the host expects top-left-origin pixels
 *              (e.g. DaVinci Resolve). Default true matches OFX spec.
 */
void toOFXBuffer(
    const ImageData& image,
    void* dstPixels,
    int rowBytes,
    int pixelComponents,
    int bitDepth,
    bool flipY = true
);

/**
 * @brief Bilinear resize of an ImageData to a new resolution
 *
 * @param src Source image
 * @param targetWidth Target width in pixels
 * @param targetHeight Target height in pixels
 * @return New ImageData at the requested resolution
 */
ImageData resize(const ImageData& src, int targetWidth, int targetHeight);

/**
 * @brief Detect whether an image is effectively empty (all RGB ≈ 0).
 *
 * Samples a coarse grid in the center region of the image (avoiding the
 * outer 25% on each side where mask/matte content is often legitimately
 * zero even when detection succeeded). Returns true if the mean of the
 * absolute RGB values across those samples is below `threshold`.
 *
 * Used to surface the failure mode "ComfyUI ran successfully but produced
 * an empty mask" — typical for SAM-family plugins when the input
 * resolution is too small for the open-vocabulary detector to find the
 * prompted subject. Without this signal the plugin shows pure black to
 * the user and they have no way to tell the model is at fault rather
 * than the plugin.
 *
 * @param img Image to test
 * @param threshold Mean absolute RGB value below which the image is
 *                  considered empty. Default 1e-4 is tight enough to
 *                  ignore float noise but loose enough to forgive a
 *                  single non-zero pixel in an otherwise empty mask.
 * @return true if the center region of the image is effectively all-zero.
 */
bool isImageCenterEmpty(const ImageData& img, float threshold = 1e-4f);

/**
 * @brief Compute target dimensions to resize an image so its shorter side equals targetShortSide,
 *        preserving aspect ratio.
 *
 * @param srcWidth  Source width
 * @param srcHeight Source height
 * @param targetShortSide Desired length of the shorter side in pixels
 * @param outWidth  [out] Computed target width (rounded to nearest even number)
 * @param outHeight [out] Computed target height (rounded to nearest even number)
 */
void computeResizeDims(int srcWidth, int srcHeight, int targetShortSide,
                        int& outWidth, int& outHeight);

} // namespace ImageIO
} // namespace ComfyUI

#endif // COMFYUI_IMAGE_IO_H
