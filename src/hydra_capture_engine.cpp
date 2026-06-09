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
#include "pxr/base/vt/value.h"
#include "pxr/imaging/cameraUtil/conformWindow.h"
#include "pxr/imaging/cameraUtil/framing.h"
#include "pxr/imaging/glf/simpleLight.h"
#include "pxr/imaging/glf/simpleMaterial.h"
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

UsdTimeCode GetDefaultRenderTimeCode() {
    return UsdTimeCode(1.0);
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

GfFrustum MakeDefaultCameraFrustum(
    const UsdStageRefPtr& stage,
    int width,
    int height,
    const UsdTimeCode& timeCode) {
    const float aspectRatio =
        height > 0 ? static_cast<float>(width) / static_cast<float>(height) : 1.0f;

    GfRange3d range;
    if (stage) {
        UsdGeomBBoxCache bboxCache(
            timeCode,
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

    return camera.GetFrustum();
}

void SetCameraState(UsdImagingGLEngine* engine, const GfFrustum& frustum) {
    engine->SetCameraState(
        frustum.ComputeViewMatrix(),
        frustum.ComputeProjectionMatrix());
}

RenderCameraState MakeRenderCameraState(
    const GfFrustum& frustum,
    int width,
    int height) {
    RenderCameraState state;
    state.viewMatrix = frustum.ComputeViewMatrix();
    state.projectionMatrix = frustum.ComputeProjectionMatrix();
    state.width = width;
    state.height = height;
    state.valid = width > 0 && height > 0;
    return state;
}

void SetCameraLightState(
    UsdImagingGLEngine* engine,
    const GfFrustum& frustum,
    bool cameraLightEnabled) {
    const GfVec4f kSceneAmbient(0.01f, 0.01f, 0.01f, 1.0f);
    const GfVec4f kMaterialAmbient(0.2f, 0.2f, 0.2f, 1.0f);
    const GfVec4f kMaterialSpecular(0.1f, 0.1f, 0.1f, 1.0f);
    const float kMaterialShininess = 32.0f;

    GlfSimpleLightVector lights;
    if (cameraLightEnabled) {
        const GfVec3d cameraPosition = frustum.GetPosition();
        GlfSimpleLight cameraLight(GfVec4f(
            static_cast<float>(cameraPosition[0]),
            static_cast<float>(cameraPosition[1]),
            static_cast<float>(cameraPosition[2]),
            1.0f));
        cameraLight.SetTransform(frustum.ComputeViewInverse());
        cameraLight.SetAmbient(kSceneAmbient);
        lights.push_back(cameraLight);
    }

    GlfSimpleMaterial material;
    material.SetAmbient(kMaterialAmbient);
    material.SetSpecular(kMaterialSpecular);
    material.SetShininess(kMaterialShininess);
    engine->SetLightingState(lights, material, kSceneAmbient);
}

UsdImagingGLRenderParams MakeDefaultRenderParams(const UsdTimeCode& timeCode) {
    UsdImagingGLRenderParams renderParams;
    renderParams.frame = timeCode;
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
    engine_.reset();
    DestroyGLContext(context_.get());
}

bool HydraCaptureEngine::Initialize(std::string* error) {
    if (engine_) {
        return true;
    }

    if (!MakeGLContextCurrent(context_.get(), error)) {
        return false;
    }

    // UsdImagingGLEngine initializes Hgi during construction, so the GL
    // context must already be current before this point.
    engine_ = std::make_unique<UsdImagingGLEngine>();
    engine_->SetEnablePresentation(false);
    return true;
}

bool HydraCaptureEngine::SetRendererPlugin(
    const std::string& rendererPlugin,
    std::string* error) {
    const TfToken plugin(rendererPlugin);
    if (!engine_->SetRendererPlugin(plugin)) {
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
    return ApplyRendererSettings(engine_.get(), settings);
}

bool HydraCaptureEngine::SetAovs(
    const std::vector<std::string>& aovs,
    std::string* error) {
    TfTokenVector aovNames;
    aovNames.reserve(aovs.size());
    for (const std::string& aov : aovs) {
        aovNames.emplace_back(aov);
    }

    if (!engine_->SetRendererAovs(aovNames)) {
        std::ostringstream out;
        out << "Failed to set renderer AOVs: " << JoinTokens(aovNames)
            << "\nAvailable AOVs: " << JoinTokens(engine_->GetRendererAovs());
        SetError(error, out.str());
        return false;
    }
    return true;
}

bool HydraCaptureEngine::ConfigureCamera(
    const UsdStageRefPtr& stage,
    const std::string& requestedCameraPath,
    bool cameraLightEnabled,
    int width,
    int height,
    std::string* status,
    std::string* error) {
    cameraState_ = RenderCameraState();
    SdfPath cameraPath;
    UsdGeomCamera sceneCamera;
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
        sceneCamera = UsdGeomCamera(cameraPrim);
        useSceneCamera = true;
    } else {
        cameraPath = FindFirstCameraPath(stage);
        if (!cameraPath.IsEmpty()) {
            sceneCamera = UsdGeomCamera(stage->GetPrimAtPath(cameraPath));
            useSceneCamera = true;
        }
    }

    const UsdTimeCode renderTime = GetDefaultRenderTimeCode();
    GfFrustum cameraFrustum;
    if (useSceneCamera) {
        engine_->SetCameraPath(cameraPath);
        cameraFrustum = sceneCamera.GetCamera(renderTime).GetFrustum();
        SetError(status, "Using camera: " + cameraPath.GetString());
    } else {
        cameraFrustum = MakeDefaultCameraFrustum(stage, width, height, renderTime);
        SetError(status, "Using generated default camera.");
    }
    const double aspectRatio =
        height > 0 ? static_cast<double>(width) / static_cast<double>(height) : 1.0;
    CameraUtilConformWindow(&cameraFrustum, CameraUtilMatchHorizontally, aspectRatio);
    cameraState_ = MakeRenderCameraState(cameraFrustum, width, height);
    if (!useSceneCamera) {
        SetCameraState(engine_.get(), cameraFrustum);
    }
    SetCameraLightState(engine_.get(), cameraFrustum, cameraLightEnabled);
    return true;
}

const RenderCameraState& HydraCaptureEngine::GetCameraState() const {
    return cameraState_;
}

void HydraCaptureEngine::ConfigureViewport(int width, int height) {
    engine_->SetRenderBufferSize(GfVec2i(width, height));
    engine_->SetFraming(
        CameraUtilFraming(GfRect2i(GfVec2i(0, 0), width, height)));
    engine_->SetOverrideWindowPolicy(
        std::make_optional(CameraUtilMatchHorizontally));
}

HydraRenderResult HydraCaptureEngine::Render(
    const UsdStageRefPtr& stage,
    int maxIterations) {
    const UsdImagingGLRenderParams renderParams =
        MakeDefaultRenderParams(GetDefaultRenderTimeCode());
    HydraRenderResult result;
    do {
        engine_->Render(stage->GetPseudoRoot(), renderParams);
        ++result.sampleCount;
    } while (!engine_->IsConverged() && result.sampleCount < maxIterations);
    result.converged = engine_->IsConverged();
    return result;
}

HdRenderBuffer* HydraCaptureEngine::GetAovRenderBuffer(const std::string& aov) {
    return engine_->GetAovRenderBuffer(TfToken(aov));
}

bool HydraCaptureEngine::InvokeRendererCommand(
    const TfToken& command,
    const HdCommandArgs& args,
    std::string* error) {
    if (!engine_) {
        SetError(error, "Cannot invoke renderer command before engine initialization.");
        return false;
    }

    if (!engine_->InvokeRendererCommand(command, args)) {
        std::ostringstream out;
        out << "Failed to invoke renderer command: " << command.GetString();
        const HdCommandArgs::const_iterator filePathIt = args.find("filePath");
        if (filePathIt != args.end()) {
            out << " (filePath=";
            if (filePathIt->second.IsHolding<std::string>()) {
                out << filePathIt->second.UncheckedGet<std::string>();
            } else {
                out << filePathIt->second.GetTypeName();
            }
            out << ")";
        }
        SetError(error, out.str());
        return false;
    }
    return true;
}

std::string HydraCaptureEngine::AvailableAovs() const {
    return JoinTokens(engine_->GetRendererAovs());
}
