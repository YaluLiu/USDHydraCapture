#pragma once

#include "renderer_config.h"

#include <filesystem>
#include <string>
#include <vector>

std::vector<std::string> ResolveFinalAovs(
    const std::vector<std::string>& cliAovs,
    const RendererConfig& rendererConfig);

std::string ResolveAovOutputExt(
    const std::string& aov,
    const RendererConfig& rendererConfig);

std::string SanitizeFileNameToken(const std::string& token);

std::filesystem::path BuildAovOutputPath(
    const std::filesystem::path& outputDir,
    const std::filesystem::path& usdPath,
    const std::string& aov,
    const std::string& outputExt);
