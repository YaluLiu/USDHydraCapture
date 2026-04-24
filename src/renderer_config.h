#pragma once

#include "pxr/base/js/value.h"

#include <filesystem>
#include <map>
#include <string>
#include <vector>

struct AovConfig {
    std::string outputExt;
};

struct RendererConfig {
    std::string name;
    std::string rendererPlugin;
    std::vector<std::string> defaultAovs;
    std::map<std::string, pxr::JsValue> settings;
    std::map<std::string, AovConfig> aovs;
};

bool LoadRendererConfig(
    const std::filesystem::path& path,
    RendererConfig* config,
    std::string* error);
