#include "image_output.h"

#include "pxr/pxr.h"

#include "pxr/base/gf/half.h"
#include "pxr/imaging/hd/renderBuffer.h"
#include "pxr/imaging/hd/types.h"
#include "pxr/imaging/hio/image.h"
#include "pxr/imaging/hio/types.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

PXR_NAMESPACE_USING_DIRECTIVE

namespace {

class RenderBufferMapGuard {
public:
    explicit RenderBufferMapGuard(HdRenderBuffer* buffer)
        : _buffer(buffer)
        , _mapped(_buffer ? _buffer->Map() : nullptr) {
    }

    ~RenderBufferMapGuard() {
        if (_buffer && _mapped) {
            _buffer->Unmap();
        }
    }

    RenderBufferMapGuard(const RenderBufferMapGuard&) = delete;
    RenderBufferMapGuard& operator=(const RenderBufferMapGuard&) = delete;

    void* Get() const {
        return _mapped;
    }

private:
    HdRenderBuffer* _buffer;
    void* _mapped;
};

std::string ToLowerAscii(std::string text) {
    std::transform(
        text.begin(),
        text.end(),
        text.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

bool EndsWith(const std::string& text, const std::string& suffix) {
    if (text.size() < suffix.size()) {
        return false;
    }
    return text.compare(text.size() - suffix.size(), suffix.size(), suffix) == 0;
}

float ClampUnit(float v) {
    return std::max(0.0f, std::min(1.0f, v));
}

uint8_t ToByte(float v) {
    const float clamped = ClampUnit(v);
    return static_cast<uint8_t>(clamped * 255.0f + 0.5f);
}

float NormalizeFinite(float v, float fallback = 0.0f) {
    if (!std::isfinite(v)) {
        return fallback;
    }
    return v;
}

void EncodeIdToRGB(uint32_t id, uint8_t* outR, uint8_t* outG, uint8_t* outB) {
    if (!outR || !outG || !outB) {
        return;
    }
    if (id == 0u) {
        *outR = 0u;
        *outG = 0u;
        *outB = 0u;
        return;
    }
    // Keep ID visualization deterministic across runs while spreading nearby IDs.
    *outR = static_cast<uint8_t>((id * 1315423911u) & 0xFFu);
    *outG = static_cast<uint8_t>((id * 2654435761u) & 0xFFu);
    *outB = static_cast<uint8_t>((id * 374761393u) & 0xFFu);
}

bool ConvertRenderBufferToRGBA8(
    HdRenderBuffer* renderBuffer,
    std::vector<uint8_t>* outPixels,
    int* outWidth,
    int* outHeight) {
    if (!renderBuffer || !outPixels || !outWidth || !outHeight) {
        return false;
    }

    renderBuffer->Resolve();
    RenderBufferMapGuard mapped(renderBuffer);
    if (!mapped.Get()) {
        std::cerr << "Failed to map render buffer\n";
        return false;
    }

    const int width = static_cast<int>(renderBuffer->GetWidth());
    const int height = static_cast<int>(renderBuffer->GetHeight());
    if (width <= 0 || height <= 0) {
        std::cerr << "Invalid render buffer size: " << width << "x" << height << "\n";
        return false;
    }

    outPixels->assign(static_cast<size_t>(width) * static_cast<size_t>(height) * 4u, 255u);
    *outWidth = width;
    *outHeight = height;

    const HdFormat format = renderBuffer->GetFormat();
    auto writePixel = [&](int x, int y, float r, float g, float b, float a) {
        const size_t dst = (static_cast<size_t>(y) * static_cast<size_t>(width) +
            static_cast<size_t>(x)) * 4u;
        (*outPixels)[dst + 0] = ToByte(r);
        (*outPixels)[dst + 1] = ToByte(g);
        (*outPixels)[dst + 2] = ToByte(b);
        (*outPixels)[dst + 3] = ToByte(a);
    };

    switch (format) {
    case HdFormatUNorm8: {
        const auto* src = static_cast<const uint8_t*>(mapped.Get());
        for (int y = 0; y < height; ++y) {
            const int srcY = height - 1 - y;
            for (int x = 0; x < width; ++x) {
                const size_t srcIndex =
                    static_cast<size_t>(srcY) * static_cast<size_t>(width) +
                    static_cast<size_t>(x);
                const float value = static_cast<float>(src[srcIndex]) / 255.0f;
                writePixel(x, y, value, value, value, 1.0f);
            }
        }
        return true;
    }
    case HdFormatUNorm8Vec4: {
        const auto* src = static_cast<const uint8_t*>(mapped.Get());
        for (int y = 0; y < height; ++y) {
            const int srcY = height - 1 - y;
            for (int x = 0; x < width; ++x) {
                const size_t srcIndex =
                    (static_cast<size_t>(srcY) * static_cast<size_t>(width) +
                        static_cast<size_t>(x)) * 4u;
                const size_t dstIndex =
                    (static_cast<size_t>(y) * static_cast<size_t>(width) +
                        static_cast<size_t>(x)) * 4u;
                (*outPixels)[dstIndex + 0] = src[srcIndex + 0];
                (*outPixels)[dstIndex + 1] = src[srcIndex + 1];
                (*outPixels)[dstIndex + 2] = src[srcIndex + 2];
                (*outPixels)[dstIndex + 3] = src[srcIndex + 3];
            }
        }
        return true;
    }
    case HdFormatUNorm8Vec3: {
        const auto* src = static_cast<const uint8_t*>(mapped.Get());
        for (int y = 0; y < height; ++y) {
            const int srcY = height - 1 - y;
            for (int x = 0; x < width; ++x) {
                const size_t srcIndex =
                    (static_cast<size_t>(srcY) * static_cast<size_t>(width) +
                        static_cast<size_t>(x)) * 3u;
                const size_t dstIndex =
                    (static_cast<size_t>(y) * static_cast<size_t>(width) +
                        static_cast<size_t>(x)) * 4u;
                (*outPixels)[dstIndex + 0] = src[srcIndex + 0];
                (*outPixels)[dstIndex + 1] = src[srcIndex + 1];
                (*outPixels)[dstIndex + 2] = src[srcIndex + 2];
                (*outPixels)[dstIndex + 3] = 255u;
            }
        }
        return true;
    }
    case HdFormatFloat32Vec4: {
        const auto* src = static_cast<const float*>(mapped.Get());
        for (int y = 0; y < height; ++y) {
            const int srcY = height - 1 - y;
            for (int x = 0; x < width; ++x) {
                const size_t srcIndex =
                    (static_cast<size_t>(srcY) * static_cast<size_t>(width) +
                        static_cast<size_t>(x)) * 4u;
                writePixel(x, y,
                    src[srcIndex + 0],
                    src[srcIndex + 1],
                    src[srcIndex + 2],
                    src[srcIndex + 3]);
            }
        }
        return true;
    }
    case HdFormatFloat32: {
        const auto* src = static_cast<const float*>(mapped.Get());
        for (int y = 0; y < height; ++y) {
            const int srcY = height - 1 - y;
            for (int x = 0; x < width; ++x) {
                const size_t srcIndex =
                    static_cast<size_t>(srcY) * static_cast<size_t>(width) +
                    static_cast<size_t>(x);
                const float value = NormalizeFinite(src[srcIndex]);
                writePixel(x, y, value, value, value, 1.0f);
            }
        }
        return true;
    }
    case HdFormatFloat32Vec3: {
        const auto* src = static_cast<const float*>(mapped.Get());
        for (int y = 0; y < height; ++y) {
            const int srcY = height - 1 - y;
            for (int x = 0; x < width; ++x) {
                const size_t srcIndex =
                    (static_cast<size_t>(srcY) * static_cast<size_t>(width) +
                        static_cast<size_t>(x)) * 3u;
                writePixel(x, y,
                    src[srcIndex + 0],
                    src[srcIndex + 1],
                    src[srcIndex + 2],
                    1.0f);
            }
        }
        return true;
    }
    case HdFormatFloat16: {
        const auto* src = static_cast<const GfHalf*>(mapped.Get());
        for (int y = 0; y < height; ++y) {
            const int srcY = height - 1 - y;
            for (int x = 0; x < width; ++x) {
                const size_t srcIndex =
                    static_cast<size_t>(srcY) * static_cast<size_t>(width) +
                    static_cast<size_t>(x);
                const float value = NormalizeFinite(static_cast<float>(src[srcIndex]));
                writePixel(x, y, value, value, value, 1.0f);
            }
        }
        return true;
    }
    case HdFormatFloat16Vec4: {
        const auto* src = static_cast<const GfHalf*>(mapped.Get());
        for (int y = 0; y < height; ++y) {
            const int srcY = height - 1 - y;
            for (int x = 0; x < width; ++x) {
                const size_t srcIndex =
                    (static_cast<size_t>(srcY) * static_cast<size_t>(width) +
                        static_cast<size_t>(x)) * 4u;
                writePixel(x, y,
                    static_cast<float>(src[srcIndex + 0]),
                    static_cast<float>(src[srcIndex + 1]),
                    static_cast<float>(src[srcIndex + 2]),
                    static_cast<float>(src[srcIndex + 3]));
            }
        }
        return true;
    }
    case HdFormatFloat16Vec3: {
        const auto* src = static_cast<const GfHalf*>(mapped.Get());
        for (int y = 0; y < height; ++y) {
            const int srcY = height - 1 - y;
            for (int x = 0; x < width; ++x) {
                const size_t srcIndex =
                    (static_cast<size_t>(srcY) * static_cast<size_t>(width) +
                        static_cast<size_t>(x)) * 3u;
                writePixel(x, y,
                    static_cast<float>(src[srcIndex + 0]),
                    static_cast<float>(src[srcIndex + 1]),
                    static_cast<float>(src[srcIndex + 2]),
                    1.0f);
            }
        }
        return true;
    }
    case HdFormatInt32: {
        const auto* src = static_cast<const int32_t*>(mapped.Get());
        for (int y = 0; y < height; ++y) {
            const int srcY = height - 1 - y;
            for (int x = 0; x < width; ++x) {
                const size_t srcIndex =
                    static_cast<size_t>(srcY) * static_cast<size_t>(width) +
                    static_cast<size_t>(x);
                const uint32_t id = static_cast<uint32_t>(src[srcIndex]);
                uint8_t r = 0u;
                uint8_t g = 0u;
                uint8_t b = 0u;
                EncodeIdToRGB(id, &r, &g, &b);
                writePixel(
                    x,
                    y,
                    static_cast<float>(r) / 255.0f,
                    static_cast<float>(g) / 255.0f,
                    static_cast<float>(b) / 255.0f,
                    1.0f);
            }
        }
        return true;
    }
    case HdFormatFloat32UInt8: {
        const auto* src = static_cast<const HdDepthStencilType*>(mapped.Get());
        for (int y = 0; y < height; ++y) {
            const int srcY = height - 1 - y;
            for (int x = 0; x < width; ++x) {
                const size_t srcIndex =
                    static_cast<size_t>(srcY) * static_cast<size_t>(width) +
                    static_cast<size_t>(x);
                const float value = NormalizeFinite(src[srcIndex].first);
                writePixel(x, y, value, value, value, 1.0f);
            }
        }
        return true;
    }
    default:
        std::cerr << "Unsupported AOV format: " << static_cast<int>(format)
                  << ". Expected scalar or RGB(A) variants of UNorm8/Float16/Float32, "
                  << "or Int32/depth-stencil IDs.\n";
        return false;
    }
}

bool WritePPM(
    const std::string& outputPath,
    int width,
    int height,
    const std::vector<uint8_t>& rgbaPixels) {
    std::ofstream ofs(outputPath, std::ios::binary);
    if (!ofs) {
        return false;
    }

    ofs << "P6\n" << width << " " << height << "\n255\n";
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const size_t idx =
                (static_cast<size_t>(y) * static_cast<size_t>(width) +
                    static_cast<size_t>(x)) * 4u;
            ofs.put(static_cast<char>(rgbaPixels[idx + 0]));
            ofs.put(static_cast<char>(rgbaPixels[idx + 1]));
            ofs.put(static_cast<char>(rgbaPixels[idx + 2]));
        }
    }
    return ofs.good();
}

bool WriteImage(
    const std::string& outputPath,
    int width,
    int height,
    const std::vector<uint8_t>& rgbaPixels) {
    const std::string lowerOutputPath = ToLowerAscii(outputPath);
    if (EndsWith(lowerOutputPath, ".ppm")) {
        return WritePPM(outputPath, width, height, rgbaPixels);
    }

    HioImageSharedPtr image = HioImage::OpenForWriting(outputPath);
    if (image) {
        HioImage::StorageSpec spec;
        spec.width = width;
        spec.height = height;
        spec.depth = 1;
        spec.format = HioFormatUNorm8Vec4;
        spec.flipped = false;
        spec.data = const_cast<uint8_t*>(rgbaPixels.data());
        if (image->Write(spec)) {
            return true;
        }
        std::cerr << "Hio failed to write " << outputPath << "\n";
        return false;
    }

    std::cerr << "No Hio writer for " << outputPath << "\n";
    return false;
}

}  // namespace

bool WriteRenderBufferImage(
    HdRenderBuffer* renderBuffer,
    const std::string& outputPath) {
    std::vector<uint8_t> rgbaPixels;
    int outWidth = 0;
    int outHeight = 0;
    if (!ConvertRenderBufferToRGBA8(renderBuffer, &rgbaPixels, &outWidth, &outHeight)) {
        return false;
    }
    return WriteImage(outputPath, outWidth, outHeight, rgbaPixels);
}
