#include "aov_output.h"
#include "options.h"
#include "renderer_config.h"
#include "renderer_settings.h"

#include "pxr/pxr.h"

#include "pxr/base/gf/half.h"
#include "pxr/base/gf/camera.h"
#include "pxr/base/gf/frustum.h"
#include "pxr/base/gf/range1f.h"
#include "pxr/base/gf/range3d.h"
#include "pxr/base/gf/rect2i.h"
#include "pxr/base/gf/vec2i.h"
#include "pxr/base/gf/vec3d.h"
#include "pxr/base/tf/token.h"
#include "pxr/imaging/cameraUtil/framing.h"
#include "pxr/imaging/glf/glContext.h"
#include "pxr/imaging/glf/testGLContext.h"
#include "pxr/imaging/hd/renderBuffer.h"
#include "pxr/imaging/hd/types.h"
#include "pxr/imaging/hio/image.h"
#include "pxr/imaging/hio/types.h"
#include "pxr/usd/sdf/path.h"
#include "pxr/usd/usd/prim.h"
#include "pxr/usd/usd/primRange.h"
#include "pxr/usd/usd/stage.h"
#include "pxr/usd/usdGeom/bboxCache.h"
#include "pxr/usd/usdGeom/camera.h"
#include "pxr/usd/usdGeom/imageable.h"
#include "pxr/usd/usdGeom/tokens.h"
#include "pxr/usdImaging/usdImagingGL/engine.h"
#include "pxr/usdImaging/usdImagingGL/renderParams.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

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

SdfPath FindFirstCameraPath(const UsdStageRefPtr& stage) {
    if (!stage) {
        return SdfPath();
    }

    for (const UsdPrim& prim : stage->Traverse()) {
        if (prim.IsA<UsdGeomCamera>()) {
            return prim.GetPath();
        }
    }
    return SdfPath();
}

std::string JoinTokens(const TfTokenVector& tokens) {
    std::string out;
    for (size_t i = 0; i < tokens.size(); ++i) {
        if (i != 0) {
            out += ", ";
        }
        out += tokens[i].GetString();
    }
    return out;
}

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

struct PlatformGLContext {
#ifdef _WIN32
    HWND hwnd = nullptr;
    HDC hdc = nullptr;
    HGLRC hglrc = nullptr;
#else
    GlfTestGLContextSharedPtr context;
#endif
};

void DestroyGLContext(PlatformGLContext* context) {
    if (!context) {
        return;
    }

#ifdef _WIN32
    if (context->hglrc) {
        GlfGLContext::DoneCurrent();
        wglDeleteContext(context->hglrc);
        context->hglrc = nullptr;
    }
    if (context->hdc && context->hwnd) {
        ReleaseDC(context->hwnd, context->hdc);
        context->hdc = nullptr;
    }
    if (context->hwnd) {
        DestroyWindow(context->hwnd);
        context->hwnd = nullptr;
    }
#else
    if (context->context) {
        GlfGLContext::DoneCurrent();
        context->context.reset();
    }
#endif
}

bool MakeGLContextCurrent(PlatformGLContext* outContext) {
    if (!outContext) {
        return false;
    }

#ifdef _WIN32
    constexpr wchar_t kWindowClassName[] = L"UsdHydraForAiHiddenGLWindow";
    const HINSTANCE instance = GetModuleHandleW(nullptr);

    WNDCLASSW wc;
    if (GetClassInfoW(instance, kWindowClassName, &wc) == 0) {
        WNDCLASSW newClass = {};
        newClass.style = CS_OWNDC;
        newClass.lpfnWndProc = DefWindowProcW;
        newClass.hInstance = instance;
        newClass.lpszClassName = kWindowClassName;
        if (RegisterClassW(&newClass) == 0) {
            std::cerr << "RegisterClassW failed with error " << GetLastError()
                      << "\n";
            return false;
        }
    }

    outContext->hwnd = CreateWindowExW(
        0,
        kWindowClassName,
        L"hydra_capture_hidden_gl",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        1,
        1,
        nullptr,
        nullptr,
        instance,
        nullptr);
    if (!outContext->hwnd) {
        std::cerr << "CreateWindowExW failed with error " << GetLastError()
                  << "\n";
        DestroyGLContext(outContext);
        return false;
    }

    outContext->hdc = GetDC(outContext->hwnd);
    if (!outContext->hdc) {
        std::cerr << "GetDC failed with error " << GetLastError() << "\n";
        DestroyGLContext(outContext);
        return false;
    }

    PIXELFORMATDESCRIPTOR pfd = {};
    pfd.nSize = sizeof(PIXELFORMATDESCRIPTOR);
    pfd.nVersion = 1;
    pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL;
    pfd.iPixelType = PFD_TYPE_RGBA;
    pfd.cColorBits = 24;
    pfd.cDepthBits = 24;
    pfd.cStencilBits = 8;
    pfd.iLayerType = PFD_MAIN_PLANE;

    const int pixelFormat = ChoosePixelFormat(outContext->hdc, &pfd);
    if (pixelFormat == 0) {
        std::cerr << "ChoosePixelFormat failed with error " << GetLastError()
                  << "\n";
        DestroyGLContext(outContext);
        return false;
    }

    if (SetPixelFormat(outContext->hdc, pixelFormat, &pfd) == 0) {
        std::cerr << "SetPixelFormat failed with error " << GetLastError()
                  << "\n";
        DestroyGLContext(outContext);
        return false;
    }

    outContext->hglrc = wglCreateContext(outContext->hdc);
    if (!outContext->hglrc) {
        std::cerr << "wglCreateContext failed with error " << GetLastError()
                  << "\n";
        DestroyGLContext(outContext);
        return false;
    }

    if (wglMakeCurrent(outContext->hdc, outContext->hglrc) == 0) {
        std::cerr << "wglMakeCurrent failed with error " << GetLastError()
                  << "\n";
        DestroyGLContext(outContext);
        return false;
    }

    const GlfGLContextSharedPtr current = GlfGLContext::GetCurrentGLContext();
    if (!current || !current->IsValid()) {
        std::cerr << "Failed to acquire a valid OpenUSD GL context wrapper.\n";
        DestroyGLContext(outContext);
        return false;
    }

    GlfGLContext::MakeCurrent(current);
    return true;
#else
    GlfTestGLContext::RegisterGLContextCallbacks();
    outContext->context = GlfTestGLContext::Create(GlfTestGLContextSharedPtr());
    if (!outContext->context || !outContext->context->IsValid()) {
        std::cerr << "Failed to create a valid GL context.\n";
        return false;
    }
    GlfGLContext::MakeCurrent(outContext->context);
    return true;
#endif
}

void SetDefaultCameraState(
    UsdImagingGLEngine* engine,
    const UsdStageRefPtr& stage,
    int width,
    int height) {
    const float aspectRatio =
        height > 0 ? static_cast<float>(width) / static_cast<float>(height) : 1.0f;

    GfRange3d range;
    if (stage) {
        UsdGeomBBoxCache bboxCache(
            UsdTimeCode::Default(),
            { UsdGeomTokens->default_, UsdGeomTokens->proxy, UsdGeomTokens->render });
        range = bboxCache.ComputeWorldBound(stage->GetPseudoRoot()).ComputeAlignedRange();
    }

    GfVec3d center(0.0, 0.0, 0.0);
    double radius = 1.0;
    if (!range.IsEmpty()) {
        center = range.GetMidpoint();
        const GfVec3d size = range.GetSize();
        radius = std::max({ size[0], size[1], size[2], 1.0 }) * 0.5;
    }

    constexpr double kPi = 3.14159265358979323846;
    const double distance = radius / std::tan(22.5 * kPi / 180.0) + radius;
    GfCamera camera;
    camera.SetPerspectiveFromAspectRatioAndFieldOfView(
        aspectRatio,
        45.0f,
        GfCamera::FOVVertical);
    camera.SetClippingRange(GfRange1f(0.1f, static_cast<float>(distance + radius * 4.0)));
    camera.SetTransform(
        GfMatrix4d(1.0).SetTranslate(center + GfVec3d(0.0, 0.0, distance)));

    const GfFrustum frustum = camera.GetFrustum();
    engine->SetCameraState(
        frustum.ComputeViewMatrix(),
        frustum.ComputeProjectionMatrix());
}

}  // namespace

int main(int argc, char** argv) {
    Options options;
    std::string error;
    if (!ParseArgs(argc, argv, &options, &error)) {
        if (!error.empty()) {
            std::cerr << error << "\n";
        }
        PrintUsage(std::cerr, argv[0]);
        return EXIT_FAILURE;
    }

    RendererConfig rendererConfig;
    if (!LoadRendererConfig(options.rendererConfigPath, &rendererConfig, &error)) {
        std::cerr << error << "\n";
        return EXIT_FAILURE;
    }
    const std::vector<std::string> finalAovs =
        ResolveFinalAovs(options.aovOverrides, rendererConfig);
    if (finalAovs.empty()) {
        std::cerr << "Final AOV list is empty.\n";
        return EXIT_FAILURE;
    }
    for (const std::string& aov : finalAovs) {
        if (SanitizeFileNameToken(aov).empty()) {
            std::cerr << "AOV name sanitizes to an empty file name: " << aov << "\n";
            return EXIT_FAILURE;
        }
    }

    const std::filesystem::path outputRoot(options.outputDir);
    const std::filesystem::path usdPath(options.usdPath);
    const std::filesystem::path outputUsdDir = outputRoot / usdPath.stem();
    std::error_code filesystemError;
    std::filesystem::create_directories(outputUsdDir, filesystemError);
    if (filesystemError) {
        std::cerr << "Failed to create output directory " << outputUsdDir
                  << ": " << filesystemError.message() << "\n";
        return EXIT_FAILURE;
    }

    PlatformGLContext glContext;
    if (!MakeGLContextCurrent(&glContext)) {
        return EXIT_FAILURE;
    }
    struct ContextCleanup {
        PlatformGLContext* context;
        ~ContextCleanup() {
            DestroyGLContext(context);
        }
    } contextCleanup { &glContext };

    const UsdStageRefPtr stage = UsdStage::Open(options.usdPath);
    if (!stage) {
        std::cerr << "Failed to open USD stage: " << options.usdPath << "\n";
        return EXIT_FAILURE;
    }

    SdfPath cameraPath;
    bool useSceneCamera = false;
    if (!options.cameraPath.empty()) {
        cameraPath = SdfPath(options.cameraPath);
        if (!cameraPath.IsAbsolutePath()) {
            std::cerr << "Camera path must be absolute, got: " << options.cameraPath << "\n";
            return EXIT_FAILURE;
        }
        const UsdPrim cameraPrim = stage->GetPrimAtPath(cameraPath);
        if (!cameraPrim || !cameraPrim.IsA<UsdGeomCamera>()) {
            std::cerr << "Camera not found or not a UsdGeomCamera: "
                      << options.cameraPath << "\n";
            return EXIT_FAILURE;
        }
        useSceneCamera = true;
    } else {
        cameraPath = FindFirstCameraPath(stage);
        if (!cameraPath.IsEmpty()) {
            useSceneCamera = true;
        }
    }

    UsdImagingGLEngine engine;
    const TfToken rendererPlugin(rendererConfig.rendererPlugin);
    if (!engine.SetRendererPlugin(rendererPlugin)) {
        std::cerr << "Failed to set renderer plugin: "
                  << rendererPlugin.GetString() << "\n";
        std::cerr << "Available renderer plugins: "
                  << JoinTokens(UsdImagingGLEngine::GetRendererPlugins())
                  << "\n";
        return EXIT_FAILURE;
    }

    if (!ApplyRendererSettings(&engine, rendererConfig.settings)) {
        return EXIT_FAILURE;
    }

    TfTokenVector aovNames;
    aovNames.reserve(finalAovs.size());
    for (const std::string& aov : finalAovs) {
        aovNames.emplace_back(aov);
    }
    if (!engine.SetRendererAovs(aovNames)) {
        std::cerr << "Failed to set renderer AOVs: "
                  << JoinTokens(aovNames) << "\n";
        std::cerr << "Available AOVs: "
                  << JoinTokens(engine.GetRendererAovs()) << "\n";
        return EXIT_FAILURE;
    }

    engine.SetEnablePresentation(false);
    if (useSceneCamera) {
        engine.SetCameraPath(cameraPath);
        std::cout << "Using camera: " << cameraPath.GetString() << "\n";
    } else {
        SetDefaultCameraState(&engine, stage, options.width, options.height);
        std::cout << "Using generated default camera.\n";
    }
    engine.SetRenderBufferSize(GfVec2i(options.width, options.height));
    engine.SetFraming(
        CameraUtilFraming(GfRect2i(GfVec2i(0, 0), options.width, options.height)));
    engine.SetOverrideWindowPolicy(
        std::make_optional(CameraUtilMatchHorizontally));

    UsdImagingGLRenderParams renderParams;
    renderParams.frame = UsdTimeCode::Default();
    renderParams.complexity = 1.0f;
    renderParams.drawMode = UsdImagingGLDrawMode::DRAW_SHADED_SMOOTH;
    renderParams.showProxy = true;
    renderParams.showRender = true;
    renderParams.enableLighting = true;
    renderParams.enableSceneLights = true;
    renderParams.enableSceneMaterials = true;
    renderParams.gammaCorrectColors = true;
    renderParams.colorCorrectionMode = TfToken("sRGB");
    renderParams.clearColor = GfVec4f(0.0f, 0.0f, 0.0f, 1.0f);

    std::cout << "Rendering " << options.usdPath << " at "
              << options.width << "x" << options.height << "\n";
    int sampleCount = 0;
    do {
        engine.Render(stage->GetPseudoRoot(), renderParams);
        ++sampleCount;
    } while (!engine.IsConverged() && sampleCount < options.maxIterations);

    std::cout << "Render iterations: " << sampleCount
              << (engine.IsConverged() ? " (converged)" : " (stopped by max iterations)")
              << "\n";

    std::vector<std::filesystem::path> savedPaths;
    for (const std::string& aov : finalAovs) {
        const TfToken aovName(aov);
        HdRenderBuffer* renderBuffer = engine.GetAovRenderBuffer(aovName);
        if (!renderBuffer) {
            std::cerr << "Failed to fetch render buffer for AOV: " << aov << "\n";
            std::cerr << "Available AOVs: "
                      << JoinTokens(engine.GetRendererAovs()) << "\n";
            return EXIT_FAILURE;
        }

        std::vector<uint8_t> rgbaPixels;
        int outWidth = 0;
        int outHeight = 0;
        if (!ConvertRenderBufferToRGBA8(renderBuffer, &rgbaPixels, &outWidth, &outHeight)) {
            std::cerr << "Failed to convert render buffer for AOV: " << aov << "\n";
            return EXIT_FAILURE;
        }

        const std::filesystem::path outputPath = BuildAovOutputPath(
            outputRoot,
            usdPath,
            aov,
            ResolveAovOutputExt(aov, rendererConfig));
        if (!WriteImage(outputPath.string(), outWidth, outHeight, rgbaPixels)) {
            std::cerr << "Failed to write AOV '" << aov
                      << "' to " << outputPath << "\n";
            return EXIT_FAILURE;
        }
        savedPaths.push_back(outputPath);
    }

    for (const std::filesystem::path& path : savedPaths) {
        std::cout << path.string() << "\n";
    }
    return EXIT_SUCCESS;
}
