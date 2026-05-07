#include "aov_output.h"
#include "image_output.h"
#include "options.h"
#include "renderer_config.h"
#include "renderer_settings.h"

#include "pxr/pxr.h"

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
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
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

        const std::filesystem::path outputPath = BuildAovOutputPath(
            outputRoot,
            usdPath,
            aov,
            ResolveAovOutputExt(aov, rendererConfig));
        if (!WriteRenderBufferImage(renderBuffer, outputPath.string())) {
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
