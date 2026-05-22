// Copyright OpenFX and contributors to the OpenFX project.
// SPDX-License-Identifier: BSD-3-Clause

#include "comfyui_image_io.h"
#define TINYEXR_IMPLEMENTATION
#include <tinyexr.h>
#include <spdlog/spdlog.h>
#include <cstring>
#include <algorithm>
#include <cstdint>
#include <errno.h>

#ifdef _WIN32
    #include <direct.h>
    #include <sys/stat.h>
    #include <sys/types.h>
    #define mkdir(path, mode) _mkdir(path)
    #define S_ISDIR(m) (((m) & _S_IFMT) == _S_IFDIR)
#else
    #include <sys/stat.h>
    #include <sys/types.h>
#endif

namespace ComfyUI {
namespace ImageIO {

// Helper function to create directory recursively
bool createDirectoryRecursive(const std::string& path) {
    if (path.empty()) return true;

    // Check if directory already exists
    struct stat st;
    if (stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode)) {
        return true; // Already exists
    }

    // Find parent directory
    size_t pos = path.find_last_of("/\\");
    if (pos != std::string::npos) {
        std::string parent = path.substr(0, pos);
        if (!createDirectoryRecursive(parent)) {
            return false;
        }
    }

    // Create this directory
    if (mkdir(path.c_str(), 0755) != 0 && errno != EEXIST) {
        return false;
    }

    return true;
}

// Helper function to get directory from file path
static std::string getDirectory(const std::string& filepath) {
    size_t pos = filepath.find_last_of("/\\");
    if (pos != std::string::npos) {
        return filepath.substr(0, pos);
    }
    return "";
}

ImageData readEXR(const std::string& filename) {
    // Use TinyEXR's flexible header+image API instead of LoadEXR().
    // LoadEXR() only handles RGBA-compatible files and reports
    // "Invalid/Corrupted data" on perfectly valid RGB-only EXRs (e.g. depth maps
    // emitted by ComfyUI's HQ-Image-Save SaveEXR node, which writes B/G/R with
    // no alpha). We accept any channel layout and assemble a 4-channel
    // (RGBA) ImageData for downstream consistency:
    //   - R/G/B/A channels matched by name (case-insensitive); missing R/G/B
    //     fall back to luminance Y (or to R replicated) for grayscale outputs;
    //     missing A defaults to 1.0 (opaque).
    EXRVersion version;
    int ret = ParseEXRVersionFromFile(&version, filename.c_str());
    if (ret != TINYEXR_SUCCESS) {
        throw std::runtime_error("Failed to parse EXR version: " + filename);
    }

    EXRHeader header;
    InitEXRHeader(&header);
    const char* err = nullptr;
    ret = ParseEXRHeaderFromFile(&header, &version, filename.c_str(), &err);
    if (ret != TINYEXR_SUCCESS) {
        std::string msg = "Failed to parse EXR header: " + filename;
        if (err) { msg += " - "; msg += err; FreeEXRErrorMessage(err); }
        FreeEXRHeader(&header);
        throw std::runtime_error(msg);
    }

    // Force float decoding; the file may be HALF on disk.
    for (int i = 0; i < header.num_channels; ++i) {
        if (header.pixel_types[i] == TINYEXR_PIXELTYPE_HALF) {
            header.requested_pixel_types[i] = TINYEXR_PIXELTYPE_FLOAT;
        }
    }

    EXRImage exrImage;
    InitEXRImage(&exrImage);
    ret = LoadEXRImageFromFile(&exrImage, &header, filename.c_str(), &err);
    if (ret != TINYEXR_SUCCESS) {
        std::string msg = "Failed to load EXR pixels: " + filename;
        if (err) { msg += " - "; msg += err; FreeEXRErrorMessage(err); }
        FreeEXRHeader(&header);
        throw std::runtime_error(msg);
    }

    int width  = exrImage.width;
    int height = exrImage.height;

    int rIdx = -1, gIdx = -1, bIdx = -1, aIdx = -1, yIdx = -1;
    auto logger = spdlog::get("comfyui_plugin");
    std::string channelSummary;
    for (int i = 0; i < header.num_channels; ++i) {
        const char* name = header.channels[i].name;
        const char* typeStr = "?";
        switch (header.pixel_types[i]) {
            case TINYEXR_PIXELTYPE_UINT:  typeStr = "UINT";  break;
            case TINYEXR_PIXELTYPE_HALF:  typeStr = "HALF→FLOAT"; break;
            case TINYEXR_PIXELTYPE_FLOAT: typeStr = "FLOAT"; break;
        }
        if (!channelSummary.empty()) channelSummary += ", ";
        channelSummary += std::string(name) + ":" + typeStr;
        if      (name[0] == 'R' || name[0] == 'r') rIdx = i;
        else if (name[0] == 'G' || name[0] == 'g') gIdx = i;
        else if (name[0] == 'B' || name[0] == 'b') bIdx = i;
        else if (name[0] == 'A' || name[0] == 'a') aIdx = i;
        else if (name[0] == 'Y' || name[0] == 'y') yIdx = i;
    }
    if (logger) {
        logger->info("readEXR: '{}' — {}x{}, {} channel(s): [{}]",
                     filename, width, height, header.num_channels, channelSummary);
        logger->info("  channel mapping: R={}, G={}, B={}, A={}, Y={} (synth A=1.0 if absent)",
                     rIdx, gIdx, bIdx, aIdx, yIdx);
    }

    auto channelPtr = [&](int idx) -> const float* {
        if (idx < 0) return nullptr;
        return reinterpret_cast<const float*>(exrImage.images[idx]);
    };
    const float* rChan = channelPtr(rIdx);
    const float* gChan = channelPtr(gIdx);
    const float* bChan = channelPtr(bIdx);
    const float* aChan = channelPtr(aIdx);
    const float* yChan = channelPtr(yIdx);

    // Grayscale fallbacks: single Y → replicate to RGB; single R → replicate to G,B.
    if (!rChan && !gChan && !bChan && yChan) { rChan = gChan = bChan = yChan; }
    if (rChan && !gChan) gChan = rChan;
    if (rChan && !bChan) bChan = rChan;

    ImageData image(width, height, 4);
    const long npix = static_cast<long>(width) * static_cast<long>(height);
    for (long i = 0; i < npix; ++i) {
        image.pixels[i * 4 + 0] = rChan ? rChan[i] : 0.0f;
        image.pixels[i * 4 + 1] = gChan ? gChan[i] : 0.0f;
        image.pixels[i * 4 + 2] = bChan ? bChan[i] : 0.0f;
        image.pixels[i * 4 + 3] = aChan ? aChan[i] : 1.0f;
    }

    if (logger) {
        // Sample top-left, top-right, bottom-left, bottom-right corners (RGBA).
        // Useful for sanity-checking orientation and decode correctness.
        auto sample = [&](long x, long y) {
            long i = (y * width + x) * 4;
            return std::string("[") +
                   std::to_string(image.pixels[i + 0]) + "," +
                   std::to_string(image.pixels[i + 1]) + "," +
                   std::to_string(image.pixels[i + 2]) + "," +
                   std::to_string(image.pixels[i + 3]) + "]";
        };
        logger->info("  corners (top-left layout): TL={} TR={} BL={} BR={}",
                     sample(0, 0),
                     sample(width - 1, 0),
                     sample(0, height - 1),
                     sample(width - 1, height - 1));
    }

    FreeEXRImage(&exrImage);
    FreeEXRHeader(&header);

    return image;
}

void writeEXR(const std::string& filename, const ImageData& image) {
    if (image.width <= 0 || image.height <= 0) {
        throw std::runtime_error("Invalid image dimensions");
    }

    if (image.channels != 3 && image.channels != 4) {
        throw std::runtime_error("Only RGB and RGBA images supported");
    }

    // Create parent directory if it doesn't exist
    std::string dir = getDirectory(filename);
    if (!dir.empty() && !createDirectoryRecursive(dir)) {
        throw std::runtime_error("Failed to create directory: " + dir);
    }

    // TinyEXR expects non-interleaved channel data
    std::vector<float> r(image.width * image.height);
    std::vector<float> g(image.width * image.height);
    std::vector<float> b(image.width * image.height);
    std::vector<float> a(image.width * image.height);

    // De-interleave channels
    for (int i = 0; i < image.width * image.height; ++i) {
        r[i] = image.pixels[i * image.channels + 0];
        g[i] = image.pixels[i * image.channels + 1];
        b[i] = image.pixels[i * image.channels + 2];
        if (image.channels == 4) {
            a[i] = image.pixels[i * image.channels + 3];
        } else {
            a[i] = 1.0f; // Opaque
        }
    }

    // Setup EXR image structure
    EXRHeader header;
    InitEXRHeader(&header);

    EXRImage exr_image;
    InitEXRImage(&exr_image);

    exr_image.num_channels = 4;

    // Channels MUST be supplied in alphabetical order (A, B, G, R) — the
    // EXR file format stores channels alphabetically and TinyEXR reorders
    // the *name* list to comply, but it does NOT reshuffle the matching
    // data-pointer array. Giving the names in B,G,R,A order with data
    // pointers b,g,r,a paired them by index, but after the alphabetic sort
    // index 0 became "A" with our b[] data, index 3 became "R" with our
    // a[] data (alpha = 1.0 opaque), producing an input EXR whose "R"
    // channel was a constant 1.0 (red-saturated) for every pixel — exactly
    // the cast SeedVR2 then propagated into its upscaled output.
    std::vector<float*> image_ptr(4);
    image_ptr[0] = &a[0]; // A
    image_ptr[1] = &b[0]; // B
    image_ptr[2] = &g[0]; // G
    image_ptr[3] = &r[0]; // R

    exr_image.images = (unsigned char**)image_ptr.data();
    exr_image.width = image.width;
    exr_image.height = image.height;

    header.num_channels = 4;
    header.channels = (EXRChannelInfo*)malloc(sizeof(EXRChannelInfo) * 4);

    strncpy(header.channels[0].name, "A", 255);
    strncpy(header.channels[1].name, "B", 255);
    strncpy(header.channels[2].name, "G", 255);
    strncpy(header.channels[3].name, "R", 255);

    header.pixel_types = (int*)malloc(sizeof(int) * 4);
    header.requested_pixel_types = (int*)malloc(sizeof(int) * 4);
    for (int i = 0; i < 4; i++) {
        header.pixel_types[i] = TINYEXR_PIXELTYPE_FLOAT;
        header.requested_pixel_types[i] = TINYEXR_PIXELTYPE_HALF; // Save as 16-bit float
    }

    // Write file
    const char* err = nullptr;
    int ret = SaveEXRImageToFile(&exr_image, &header, filename.c_str(), &err);

    // Cleanup
    free(header.channels);
    free(header.pixel_types);
    free(header.requested_pixel_types);

    if (ret != TINYEXR_SUCCESS) {
        std::string error_msg = "Failed to write EXR file: " + filename;
        if (err) {
            error_msg += " - " + std::string(err);
            FreeEXRErrorMessage(err);
        }
        throw std::runtime_error(error_msg);
    }
}

void computeResizeDims(int srcWidth, int srcHeight, int targetShortSide,
                        int& outWidth, int& outHeight)
{
    if (targetShortSide <= 0) {
        outWidth  = srcWidth;
        outHeight = srcHeight;
        return;
    }
    int shorterSide  = std::min(srcWidth, srcHeight);
    double scale     = static_cast<double>(targetShortSide) / shorterSide;
    // Round to nearest even number (VAE requires even dimensions)
    outWidth  = (static_cast<int>(std::round(srcWidth  * scale)) + 1) & ~1;
    outHeight = (static_cast<int>(std::round(srcHeight * scale)) + 1) & ~1;
}

ImageData resize(const ImageData& src, int targetWidth, int targetHeight)
{
    if (src.width == targetWidth && src.height == targetHeight) {
        return src;  // No-op
    }
    if (targetWidth <= 0 || targetHeight <= 0) {
        throw std::runtime_error("resize: invalid target dimensions");
    }

    ImageData dst(targetWidth, targetHeight, src.channels);
    const int ch = src.channels;

    double scaleX = static_cast<double>(src.width)  / targetWidth;
    double scaleY = static_cast<double>(src.height) / targetHeight;

    for (int dstY = 0; dstY < targetHeight; ++dstY) {
        double srcYf = (dstY + 0.5) * scaleY - 0.5;
        int y0 = static_cast<int>(std::floor(srcYf));
        int y1 = y0 + 1;
        double fy = srcYf - y0;
        y0 = std::max(0, std::min(y0, src.height - 1));
        y1 = std::max(0, std::min(y1, src.height - 1));

        for (int dstX = 0; dstX < targetWidth; ++dstX) {
            double srcXf = (dstX + 0.5) * scaleX - 0.5;
            int x0 = static_cast<int>(std::floor(srcXf));
            int x1 = x0 + 1;
            double fx = srcXf - x0;
            x0 = std::max(0, std::min(x0, src.width - 1));
            x1 = std::max(0, std::min(x1, src.width - 1));

            const float* p00 = &src.pixels[(y0 * src.width + x0) * ch];
            const float* p01 = &src.pixels[(y0 * src.width + x1) * ch];
            const float* p10 = &src.pixels[(y1 * src.width + x0) * ch];
            const float* p11 = &src.pixels[(y1 * src.width + x1) * ch];
            float* out = &dst.pixels[(dstY * targetWidth + dstX) * ch];

            double w00 = (1.0 - fx) * (1.0 - fy);
            double w01 = fx          * (1.0 - fy);
            double w10 = (1.0 - fx) * fy;
            double w11 = fx          * fy;

            for (int c = 0; c < ch; ++c) {
                out[c] = static_cast<float>(
                    w00 * p00[c] + w01 * p01[c] +
                    w10 * p10[c] + w11 * p11[c]);
            }
        }
    }
    return dst;
}

bool isImageCenterEmpty(const ImageData& img, float threshold)
{
    if (img.pixels.empty() || img.width <= 0 || img.height <= 0 || img.channels < 1) {
        return true;
    }

    // Sample a 16×16 grid in the center half of the image. Borders are skipped
    // because masks legitimately have black edges even when detection worked —
    // we want to catch the case where the *content area* is also zero, which
    // means the model produced nothing.
    const int gridSize = 16;
    const int x0 = img.width  / 4;
    const int x1 = (img.width  * 3) / 4;
    const int y0 = img.height / 4;
    const int y1 = (img.height * 3) / 4;

    // If the image is too small to have a meaningful "center half" (e.g. tiny
    // proxy renders), fall back to sampling the whole image. Otherwise empty
    // tests on small previews would always trigger.
    int sx0 = (x1 - x0 > gridSize) ? x0 : 0;
    int sx1 = (x1 - x0 > gridSize) ? x1 : img.width;
    int sy0 = (y1 - y0 > gridSize) ? y0 : 0;
    int sy1 = (y1 - y0 > gridSize) ? y1 : img.height;

    const int chToSum = std::min(3, img.channels);
    double sum = 0.0;
    int    samples = 0;

    for (int gy = 0; gy < gridSize; ++gy) {
        const int y = sy0 + (gy * (sy1 - sy0)) / gridSize;
        for (int gx = 0; gx < gridSize; ++gx) {
            const int x = sx0 + (gx * (sx1 - sx0)) / gridSize;
            const size_t idx = (static_cast<size_t>(y) * img.width + x) * img.channels;
            if (idx + chToSum > img.pixels.size()) continue;
            for (int c = 0; c < chToSum; ++c) {
                sum += std::abs(img.pixels[idx + c]);
            }
            samples += chToSum;
        }
    }

    if (samples == 0) return true;
    const double mean = sum / samples;
    return mean < static_cast<double>(threshold);
}

ImageData fromOFXBuffer(
    const void* srcPixels,
    int width,
    int height,
    int rowBytes,
    int pixelComponents,
    int bitDepth,
    bool flipY
) {
    if (!srcPixels) {
        throw std::runtime_error("Source pixel buffer is null!");
    }

    ImageData image(width, height, pixelComponents);

    const uint8_t* src8 = static_cast<const uint8_t*>(srcPixels);

    // OFX spec: origin at bottom-left (Y up). EXR: origin at top-left (Y down).
    // Hosts that report nativeOrigin = TopLeft (DaVinci Resolve, etc.) already
    // hand us top-left pixels — do not flip in that case.
    for (int y = 0; y < height; ++y) {
        int srcY = flipY ? (height - 1 - y) : y;
        const uint8_t* srcRow = src8 + srcY * rowBytes;

        for (int x = 0; x < width; ++x) {
            int dstIdx = (y * width + x) * pixelComponents;

            if (bitDepth == 8) {
                // 8-bit unsigned
                const uint8_t* pixel = srcRow + x * pixelComponents;
                for (int c = 0; c < pixelComponents; ++c) {
                    image.pixels[dstIdx + c] = pixel[c] / 255.0f;
                }
            }
            else if (bitDepth == 16) {
                // 16-bit unsigned
                const uint16_t* pixel = reinterpret_cast<const uint16_t*>(srcRow + x * pixelComponents * 2);
                for (int c = 0; c < pixelComponents; ++c) {
                    image.pixels[dstIdx + c] = pixel[c] / 65535.0f;
                }
            }
            else if (bitDepth == 32) {
                // 32-bit float
                const float* pixel = reinterpret_cast<const float*>(srcRow + x * pixelComponents * 4);
                for (int c = 0; c < pixelComponents; ++c) {
                    image.pixels[dstIdx + c] = pixel[c];
                }
            }
            else {
                throw std::runtime_error("Unsupported bit depth: " + std::to_string(bitDepth));
            }
        }
    }

    return image;
}

void toOFXBuffer(
    const ImageData& image,
    void* dstPixels,
    int rowBytes,
    int pixelComponents,
    int bitDepth,
    bool flipY
) {
    // Defensive parameter validation (prevents crashes from invalid input)
    if (!dstPixels) {
        throw std::invalid_argument("toOFXBuffer: dstPixels is NULL - cannot write to invalid buffer");
    }

    if (image.pixels.empty()) {
        throw std::invalid_argument("toOFXBuffer: source image pixel data is empty");
    }

    if (image.width <= 0 || image.height <= 0) {
        throw std::invalid_argument("toOFXBuffer: invalid image dimensions: " +
                                   std::to_string(image.width) + "x" + std::to_string(image.height));
    }

    if (pixelComponents < 1 || pixelComponents > 4) {
        throw std::invalid_argument("toOFXBuffer: invalid pixel components: " +
                                   std::to_string(pixelComponents));
    }

    if (bitDepth != 8 && bitDepth != 16 && bitDepth != 32) {
        throw std::invalid_argument("toOFXBuffer: unsupported bit depth: " +
                                   std::to_string(bitDepth));
    }

    int bytesPerPixel = pixelComponents * (bitDepth / 8);
    int minRowBytes = image.width * bytesPerPixel;

    if (rowBytes < minRowBytes) {
        throw std::invalid_argument("toOFXBuffer: rowBytes (" + std::to_string(rowBytes) +
                                   ") is smaller than minimum required (" + std::to_string(minRowBytes) + ")");
    }

    size_t expectedPixelCount = static_cast<size_t>(image.width) * image.height * image.channels;
    if (image.pixels.size() < expectedPixelCount) {
        throw std::invalid_argument("toOFXBuffer: source image pixel array is too small - expected " +
                                   std::to_string(expectedPixelCount) + " floats, got " +
                                   std::to_string(image.pixels.size()));
    }

    uint8_t* dst8 = static_cast<uint8_t*>(dstPixels);

    // EXR: origin at top-left (Y down). OFX spec: origin at bottom-left (Y up).
    // Hosts that report nativeOrigin = TopLeft (DaVinci Resolve, etc.) expect
    // top-left pixels — do not flip in that case.
    for (int y = 0; y < image.height; ++y) {
        int dstY = flipY ? (image.height - 1 - y) : y;
        uint8_t* dstRow = dst8 + dstY * rowBytes;

        for (int x = 0; x < image.width; ++x) {
            int srcIdx = (y * image.width + x) * image.channels;

            if (bitDepth == 8) {
                // 8-bit unsigned
                uint8_t* pixel = dstRow + x * pixelComponents;
                for (int c = 0; c < std::min(pixelComponents, image.channels); ++c) {
                    float val = std::max(0.0f, std::min(1.0f, image.pixels[srcIdx + c]));
                    pixel[c] = static_cast<uint8_t>(val * 255.0f + 0.5f);
                }
            }
            else if (bitDepth == 16) {
                // 16-bit unsigned
                uint16_t* pixel = reinterpret_cast<uint16_t*>(dstRow + x * pixelComponents * 2);
                for (int c = 0; c < std::min(pixelComponents, image.channels); ++c) {
                    float val = std::max(0.0f, std::min(1.0f, image.pixels[srcIdx + c]));
                    pixel[c] = static_cast<uint16_t>(val * 65535.0f + 0.5f);
                }
            }
            else if (bitDepth == 32) {
                // 32-bit float
                float* pixel = reinterpret_cast<float*>(dstRow + x * pixelComponents * 4);
                for (int c = 0; c < std::min(pixelComponents, image.channels); ++c) {
                    pixel[c] = image.pixels[srcIdx + c];
                }
            }
            else {
                throw std::runtime_error("Unsupported bit depth: " + std::to_string(bitDepth));
            }

            // Fill remaining channels with default values (e.g., alpha = 1.0)
            if (pixelComponents > image.channels) {
                if (bitDepth == 8) {
                    uint8_t* pixel = dstRow + x * pixelComponents;
                    for (int c = image.channels; c < pixelComponents; ++c) {
                        pixel[c] = (c == 3) ? 255 : 0; // Alpha = 1.0, others = 0.0
                    }
                }
                else if (bitDepth == 16) {
                    uint16_t* pixel = reinterpret_cast<uint16_t*>(dstRow + x * pixelComponents * 2);
                    for (int c = image.channels; c < pixelComponents; ++c) {
                        pixel[c] = (c == 3) ? 65535 : 0;
                    }
                }
                else if (bitDepth == 32) {
                    float* pixel = reinterpret_cast<float*>(dstRow + x * pixelComponents * 4);
                    for (int c = image.channels; c < pixelComponents; ++c) {
                        pixel[c] = (c == 3) ? 1.0f : 0.0f;
                    }
                }
            }
        }
    }
}

} // namespace ImageIO
} // namespace ComfyUI
