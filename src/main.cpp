#include "aov_output.h"
#include "hydra_capture_engine.h"
#include "image_output.h"
#include "lidar_overlay.h"
#include "lidar_point_cloud.h"
#include "options.h"
#include "renderer_config.h"

#include "pxr/pxr.h"

#include "pxr/base/tf/token.h"
#include "pxr/base/vt/value.h"
#include "pxr/usd/usd/stage.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

PXR_NAMESPACE_USING_DIRECTIVE

namespace {

bool HasAov(const std::vector<std::string>& aovs, const std::string& target) {
    return std::find(aovs.begin(), aovs.end(), target) != aovs.end();
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

    HydraCaptureEngine hydraEngine;
    if (!hydraEngine.Initialize(&error)) {
        std::cerr << error << "\n";
        return EXIT_FAILURE;
    }

    const UsdStageRefPtr stage = UsdStage::Open(options.usdPath);
    if (!stage) {
        std::cerr << "Failed to open USD stage: " << options.usdPath << "\n";
        return EXIT_FAILURE;
    }

    if (!hydraEngine.SetRendererPlugin(rendererConfig.rendererPlugin, &error)) {
        std::cerr << error << "\n";
        return EXIT_FAILURE;
    }

    if (!hydraEngine.ApplySettings(rendererConfig.settings)) {
        return EXIT_FAILURE;
    }

    if (!hydraEngine.SetAovs(finalAovs, &error)) {
        std::cerr << error << "\n";
        return EXIT_FAILURE;
    }

    std::string cameraStatus;
    if (!hydraEngine.ConfigureCamera(
            stage,
            options.cameraPath,
            options.cameraLightEnabled,
            options.width,
            options.height,
            &cameraStatus,
            &error)) {
        std::cerr << error << "\n";
        return EXIT_FAILURE;
    }
    std::cout << cameraStatus << "\n";
    hydraEngine.ConfigureViewport(options.width, options.height);

    std::cout << "Rendering " << options.usdPath << " at "
              << options.width << "x" << options.height << "\n";
    const HydraRenderResult renderResult =
        hydraEngine.Render(stage, options.maxIterations);

    std::cout << "Render iterations: " << renderResult.sampleCount
              << (renderResult.converged ? " (converged)" : " (stopped by max iterations)")
              << "\n";

    const std::string overlayCsvPath =
        !options.lidarPointCloudPath.empty()
            ? options.lidarPointCloudPath
            : options.exportLidarPointCloudPath;
    const bool overlayRequested = !overlayCsvPath.empty();
    const bool colorAovRequested = HasAov(finalAovs, "color");
    std::vector<LidarPointSample> lidarPoints;
    bool overlayEnabled = false;
    if (overlayRequested && !colorAovRequested) {
        std::cout << "LiDAR overlay skipped because final AOV list does not include color.\n";
    } else if (overlayRequested) {
        if (!options.exportLidarPointCloudPath.empty()) {
            HdCommandArgs args;
            args["filePath"] = VtValue(options.exportLidarPointCloudPath);
            if (!hydraEngine.InvokeRendererCommand(
                    TfToken("exportLidarPointCloud"),
                    args,
                    &error)) {
                std::cerr << error << "\n";
                return EXIT_FAILURE;
            }
        }
        if (!ReadLidarPointCloudCsv(overlayCsvPath, &lidarPoints, &error)) {
            std::cerr << error << "\n";
            return EXIT_FAILURE;
        }
        overlayEnabled = true;
        std::cout << "Read LiDAR point cloud CSV: " << overlayCsvPath << "\n";
        std::cout << "LiDAR valid hit points: " << lidarPoints.size() << "\n";
    }

    std::vector<std::filesystem::path> savedPaths;
    for (const std::string& aov : finalAovs) {
        HdRenderBuffer* renderBuffer = hydraEngine.GetAovRenderBuffer(aov);
        if (!renderBuffer) {
            std::cerr << "Failed to fetch render buffer for AOV: " << aov << "\n";
            std::cerr << "Available AOVs: " << hydraEngine.AvailableAovs() << "\n";
            return EXIT_FAILURE;
        }

        const std::filesystem::path outputPath = BuildAovOutputPath(
            outputRoot,
            usdPath,
            aov,
            ResolveAovOutputExt(aov, rendererConfig));
        if (overlayEnabled && aov == "color") {
            Rgba8Image image;
            if (!ConvertRenderBufferToRGBA8(renderBuffer, &image)) {
                std::cerr << "Failed to convert color AOV to RGBA8 for LiDAR overlay.\n";
                return EXIT_FAILURE;
            }
            LidarOverlayOptions overlayOptions;
            overlayOptions.pointRadiusPixels = options.lidarOverlayPointRadius;
            const size_t drawnPoints = DrawLidarPointOverlay(
                hydraEngine.GetCameraState(),
                lidarPoints,
                overlayOptions,
                &image);
            std::cout << "LiDAR overlay drawn points: " << drawnPoints
                      << " of " << lidarPoints.size() << "\n";
            std::cout << "LiDAR overlay color output: " << outputPath.string() << "\n";
            if (!WriteRGBA8Image(image, outputPath.string())) {
                std::cerr << "Failed to write AOV '" << aov
                          << "' to " << outputPath << "\n";
                return EXIT_FAILURE;
            }
        } else if (!WriteRenderBufferImage(renderBuffer, outputPath.string())) {
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
