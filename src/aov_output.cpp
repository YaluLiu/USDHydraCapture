#include "aov_output.h"

#include <cctype>

std::vector<std::string> ResolveFinalAovs(
    const std::vector<std::string>& cliAovs,
    const RendererConfig& rendererConfig) {
    if (!cliAovs.empty()) {
        return cliAovs;
    }
    if (!rendererConfig.defaultAovs.empty()) {
        return rendererConfig.defaultAovs;
    }
    return { "color" };
}

std::string ResolveAovOutputExt(
    const std::string& aov,
    const RendererConfig& rendererConfig) {
    const auto it = rendererConfig.aovs.find(aov);
    if (it != rendererConfig.aovs.end() && !it->second.outputExt.empty()) {
        return it->second.outputExt;
    }
    if (aov == "color") {
        return ".png";
    }
    return ".ppm";
}

std::string SanitizeFileNameToken(const std::string& token) {
    std::string out;
    bool previousWasSeparator = false;
    for (unsigned char c : token) {
        const bool allowed =
            std::isalnum(c) || c == '_' || c == '-' || c == '.';
        if (allowed) {
            out.push_back(static_cast<char>(c));
            previousWasSeparator = false;
        } else if (!previousWasSeparator) {
            out.push_back('_');
            previousWasSeparator = true;
        }
    }
    return out;
}

std::filesystem::path BuildAovOutputPath(
    const std::filesystem::path& outputDir,
    const std::filesystem::path& usdPath,
    const std::string& aov,
    const std::string& outputExt) {
    return outputDir / usdPath.stem() / (SanitizeFileNameToken(aov) + outputExt);
}
