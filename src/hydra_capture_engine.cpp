#include "hydra_capture_engine.h"

#include "renderer_settings.h"

#include "pxr/pxr.h"

#include "pxr/base/gf/camera.h"
#include "pxr/base/gf/frustum.h"
#include "pxr/base/gf/matrix4d.h"
#include "pxr/base/gf/range1f.h"
#include "pxr/base/gf/range3d.h"
#include "pxr/base/gf/rect2i.h"
#include "pxr/base/gf/vec2i.h"
#include "pxr/base/gf/vec3d.h"
#include "pxr/base/gf/vec4f.h"
#include "pxr/base/tf/token.h"
#include "pxr/imaging/cameraUtil/framing.h"
#include "pxr/imaging/glf/glContext.h"
#include "pxr/imaging/glf/testGLContext.h"
#include "pxr/usd/sdf/path.h"
#include "pxr/usd/usd/prim.h"
#include "pxr/usd/usd/primRange.h"
#include "pxr/usd/usdGeom/bboxCache.h"
#include "pxr/usd/usdGeom/camera.h"
#include "pxr/usd/usdGeom/imageable.h"
#include "pxr/usd/usdGeom/tokens.h"
#include "pxr/usdImaging/usdImagingGL/renderParams.h"

#include <algorithm>
#include <cmath>
#include <optional>
#include <sstream>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

PXR_NAMESPACE_USING_DIRECTIVE

struct HydraPlatformGLContext {
#ifdef _WIN32
    HWND hwnd = nullptr;
    HDC hdc = nullptr;
    HGLRC hglrc = nullptr;
#else
    GlfTestGLContextSharedPtr context;
#endif
};

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

void SetError(std::string* error, const std::string& message) {
    if (error) {
        *error = message;
    }
}

void DestroyGLContext(HydraPlatformGLContext* context) {
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

bool MakeGLContextCurrent(HydraPlatformGLContext* outContext, std::string* error) {
    if (!outContext) {
        SetError(error, "Missing GL context storage.");
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
            std::ostringstream out;
            out << "RegisterClassW failed with error " << GetLastError();
            SetError(error, out.str());
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
        std::ostringstream out;
        out << "CreateWindowExW failed with error " << GetLastError();
        SetError(error, out.str());
        DestroyGLContext(outContext);
        return false;
    }

    outContext->hdc = GetDC(outContext->hwnd);
    if (!outContext->hdc) {
        std::ostringstream out;
        out << "GetDC failed with error " << GetLastError();
        SetError(error, out.str());
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
        std::ostringstream out;
        out << "ChoosePixelFormat failed with error " << GetLastError();
        SetError(error, out.str());
        DestroyGLContext(outContext);
        return false;
    }

    if (SetPixelFormat(outContext->hdc, pixelFormat, &pfd) == 0) {
        std::ostringstream out;
        out << "SetPixelFormat failed with error " << GetLastError();
        SetError(error, out.str());
        DestroyGLContext(outContext);
        return false;
    }

    outContext->hglrc = wglCreateContext(outContext->hdc);
    if (!outContext->hglrc) {
        std::ostringstream out;
        out << "wglCreateContext failed with error " << GetLastError();
        SetError(error, out.str());
        DestroyGLContext(outContext);
        return false;
    }

    if (wglMakeCurrent(outContext->hdc, outContext->hglrc) == 0) {
        std::ostringstream out;
        out << "wglMakeCurrent failed with error " << GetLastError();
        SetError(error, out.str());
        DestroyGLContext(outContext);
        return false;
    }

    const GlfGLContextSharedPtr current = GlfGLContext::GetCurrentGLContext();
    if (!current || !current->IsValid()) {
        SetError(error, "Failed to acquire a valid OpenUSD GL context wrapper.");
        DestroyGLContext(outContext);
        return false;
    }

    GlfGLContext::MakeCurrent(current);
    return true;
#else
    GlfTestGLContext::RegisterGLContextCallbacks();
    outContext->context = GlfTestGLContext::Create(GlfTestGLContextSharedPtr());
    if (!outContext->context || !outContext->context->IsValid()) {
        SetError(error, "Failed to create a valid GL context.");
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

UsdImagingGLRenderParams MakeDefaultRenderParams() {
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
    return renderParams;
}

}  // namespace

HydraCaptureEngine::HydraCaptureEngine()
    : context_(std::make_unique<HydraPlatformGLContext>()) {}

HydraCaptureEngine::~HydraCaptureEngine() {
    DestroyGLContext(context_.get());
}

bool HydraCaptureEngine::Initialize(std::string* error) {
    if (!MakeGLContextCurrent(context_.get(), error)) {
        return false;
    }
    engine_.SetEnablePresentation(false);
    return true;
}

bool HydraCaptureEngine::SetRendererPlugin(
    const std::string& rendererPlugin,
    std::string* error) {
    const TfToken plugin(rendererPlugin);
    if (!engine_.SetRendererPlugin(plugin)) {
        std::ostringstream out;
        out << "Failed to set renderer plugin: " << plugin.GetString()
            << "\nAvailable renderer plugins: "
            << JoinTokens(UsdImagingGLEngine::GetRendererPlugins());
        SetError(error, out.str());
        return false;
    }
    return true;
}

bool HydraCaptureEngine::ApplySettings(
    const std::map<std::string, JsValue>& settings) {
    return ApplyRendererSettings(&engine_, settings);
}

bool HydraCaptureEngine::SetAovs(
    const std::vector<std::string>& aovs,
    std::string* error) {
    TfTokenVector aovNames;
    aovNames.reserve(aovs.size());
    for (const std::string& aov : aovs) {
        aovNames.emplace_back(aov);
    }

    if (!engine_.SetRendererAovs(aovNames)) {
        std::ostringstream out;
        out << "Failed to set renderer AOVs: " << JoinTokens(aovNames)
            << "\nAvailable AOVs: " << JoinTokens(engine_.GetRendererAovs());
        SetError(error, out.str());
        return false;
    }
    return true;
}

bool HydraCaptureEngine::ConfigureCamera(
    const UsdStageRefPtr& stage,
    const std::string& requestedCameraPath,
    int width,
    int height,
    std::string* status,
    std::string* error) {
    SdfPath cameraPath;
    bool useSceneCamera = false;
    if (!requestedCameraPath.empty()) {
        cameraPath = SdfPath(requestedCameraPath);
        if (!cameraPath.IsAbsolutePath()) {
            SetError(error, "Camera path must be absolute, got: " + requestedCameraPath);
            return false;
        }

        const UsdPrim cameraPrim = stage->GetPrimAtPath(cameraPath);
        if (!cameraPrim || !cameraPrim.IsA<UsdGeomCamera>()) {
            SetError(error, "Camera not found or not a UsdGeomCamera: " + requestedCameraPath);
            return false;
        }
        useSceneCamera = true;
    } else {
        cameraPath = FindFirstCameraPath(stage);
        if (!cameraPath.IsEmpty()) {
            useSceneCamera = true;
        }
    }

    if (useSceneCamera) {
        engine_.SetCameraPath(cameraPath);
        SetError(status, "Using camera: " + cameraPath.GetString());
    } else {
        SetDefaultCameraState(&engine_, stage, width, height);
        SetError(status, "Using generated default camera.");
    }
    return true;
}

void HydraCaptureEngine::ConfigureViewport(int width, int height) {
    engine_.SetRenderBufferSize(GfVec2i(width, height));
    engine_.SetFraming(
        CameraUtilFraming(GfRect2i(GfVec2i(0, 0), width, height)));
    engine_.SetOverrideWindowPolicy(
        std::make_optional(CameraUtilMatchHorizontally));
}

HydraRenderResult HydraCaptureEngine::Render(
    const UsdStageRefPtr& stage,
    int maxIterations) {
    const UsdImagingGLRenderParams renderParams = MakeDefaultRenderParams();
    HydraRenderResult result;
    do {
        engine_.Render(stage->GetPseudoRoot(), renderParams);
        ++result.sampleCount;
    } while (!engine_.IsConverged() && result.sampleCount < maxIterations);
    result.converged = engine_.IsConverged();
    return result;
}

HdRenderBuffer* HydraCaptureEngine::GetAovRenderBuffer(const std::string& aov) {
    return engine_.GetAovRenderBuffer(TfToken(aov));
}

std::string HydraCaptureEngine::AvailableAovs() const {
    return JoinTokens(engine_.GetRendererAovs());
}
