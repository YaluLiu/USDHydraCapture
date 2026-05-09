#pragma once

#include "pxr/base/js/value.h"
#include "pxr/imaging/hd/renderBuffer.h"
#include "pxr/usd/usd/stage.h"
#include "pxr/usdImaging/usdImagingGL/engine.h"

#include <map>
#include <memory>
#include <string>
#include <vector>

struct HydraPlatformGLContext;

struct HydraRenderResult {
    int sampleCount = 0;
    bool converged = false;
};

class HydraCaptureEngine {
public:
    HydraCaptureEngine();
    ~HydraCaptureEngine();

    HydraCaptureEngine(const HydraCaptureEngine&) = delete;
    HydraCaptureEngine& operator=(const HydraCaptureEngine&) = delete;

    bool Initialize(std::string* error);
    bool SetRendererPlugin(const std::string& rendererPlugin, std::string* error);
    bool ApplySettings(const std::map<std::string, pxr::JsValue>& settings);
    bool SetAovs(const std::vector<std::string>& aovs, std::string* error);
    bool ConfigureCamera(
        const pxr::UsdStageRefPtr& stage,
        const std::string& requestedCameraPath,
        int width,
        int height,
        std::string* status,
        std::string* error);
    void ConfigureViewport(int width, int height);
    HydraRenderResult Render(const pxr::UsdStageRefPtr& stage, int maxIterations);
    pxr::HdRenderBuffer* GetAovRenderBuffer(const std::string& aov);
    std::string AvailableAovs() const;

private:
    std::unique_ptr<HydraPlatformGLContext> context_;
    std::unique_ptr<pxr::UsdImagingGLEngine> engine_;
};
