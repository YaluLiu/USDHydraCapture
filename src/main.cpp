#include "aov_output.h"
#include "hydra_capture_engine.h"
#include "image_output.h"
#include "options.h"
#include "renderer_config.h"

#include "pxr/pxr.h"

#include "pxr/usd/usd/stage.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

PXR_NAMESPACE_USING_DIRECTIVE

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
