#include "aov_output.h"
#include "options.h"
#include "renderer_config.h"
#include "renderer_settings.h"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <pxr/base/tf/token.h>
#include <pxr/base/vt/value.h>
#include <string>
#include <vector>

namespace {

void TestParseRequiredAndRepeatedAovs() {
    const char* argv[] = {
        "hydra_capture",
        "--renderer-config",
        "config/plugins/hdStorm/plugin.json",
        "--usd",
        "/tmp/scene.usd",
        "--output-dir",
        "/tmp/out",
        "--camera",
        "/World/Camera",
        "--width",
        "64",
        "--height",
        "32",
        "--max-iterations",
        "3",
        "--aov",
        "color",
        "--aov",
        "depth",
    };

    Options options;
    std::string error;
    assert(ParseArgs(static_cast<int>(std::size(argv)), const_cast<char**>(argv), &options, &error));
    assert(options.rendererConfigPath == "config/plugins/hdStorm/plugin.json");
    assert(options.usdPath == "/tmp/scene.usd");
    assert(options.outputDir == "/tmp/out");
    assert(options.cameraPath == "/World/Camera");
    assert(options.width == 64);
    assert(options.height == 32);
    assert(options.maxIterations == 3);
    assert((options.aovOverrides == std::vector<std::string> { "color", "depth" }));
}

void TestParseRejectsOldPositionalAndRendererPlugin() {
    const char* positional[] = { "hydra_capture", "scene.usd", "out.png" };
    Options options;
    std::string error;
    assert(!ParseArgs(static_cast<int>(std::size(positional)), const_cast<char**>(positional), &options, &error));
    assert(error.find("Unknown argument") != std::string::npos ||
        error.find("Missing required") != std::string::npos);

    const char* rendererPlugin[] = {
        "hydra_capture",
        "--renderer-config",
        "plugin.json",
        "--usd",
        "scene.usd",
        "--output-dir",
        "out",
        "--renderer-plugin",
        "HdStormRendererPlugin",
    };
    error.clear();
    assert(!ParseArgs(
        static_cast<int>(std::size(rendererPlugin)),
        const_cast<char**>(rendererPlugin),
        &options,
        &error));
    assert(error.find("--renderer-plugin") != std::string::npos);
}

void TestRendererConfigJson() {
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "hydra_capture_config_test.json";
    {
        std::ofstream out(path);
        out << R"json({
  "name": "testRenderer",
  "renderer_plugin": "HdTestRendererPlugin",
  "defaults": {
    "aovs": ["color", "lidar:pointCloud"],
    "settings": {
      "enableSceneMaterials": true,
      "samples": 4,
      "scale": 0.5,
      "mode": "tokenValue"
    }
  },
  "aovs": {
    "color": { "output_ext": ".png" },
    "lidar:pointCloud": { "output_ext": ".exr" }
  }
})json";
    }

    RendererConfig config;
    std::string error;
    assert(LoadRendererConfig(path, &config, &error));
    assert(config.name == "testRenderer");
    assert(config.rendererPlugin == "HdTestRendererPlugin");
    assert((config.defaultAovs == std::vector<std::string> { "color", "lidar:pointCloud" }));
    assert(config.settings.at("enableSceneMaterials").GetBool());
    assert(config.settings.at("samples").GetInt() == 4);
    assert(config.settings.at("scale").GetReal() == 0.5);
    assert(config.settings.at("mode").GetString() == "tokenValue");
    assert(config.aovs.at("color").outputExt == ".png");
    assert(config.aovs.at("lidar:pointCloud").outputExt == ".exr");
}

void TestRendererConfigErrorsNamePathKeyAndType() {
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "hydra_capture_bad_config_test.json";
    {
        std::ofstream out(path);
        out << R"json({ "renderer_plugin": 7 })json";
    }

    RendererConfig config;
    std::string error;
    assert(!LoadRendererConfig(path, &config, &error));
    assert(error.find(path.string()) != std::string::npos);
    assert(error.find("renderer_plugin") != std::string::npos);
    assert(error.find("string") != std::string::npos);
}

void TestAovDefaultsExtensionsAndPaths() {
    RendererConfig config;
    config.defaultAovs = { "color", "depth", "primId", "lidar:pointCloud" };
    config.aovs["color"].outputExt = ".png";
    config.aovs["lidar:pointCloud"].outputExt = ".exr";

    assert((ResolveFinalAovs({}, config) ==
        std::vector<std::string> { "color", "depth", "primId", "lidar:pointCloud" }));
    assert((ResolveFinalAovs({ "depth" }, config) == std::vector<std::string> { "depth" }));
    assert(ResolveAovOutputExt("color", config) == ".png");
    assert(ResolveAovOutputExt("depth", config) == ".ppm");
    assert(ResolveAovOutputExt("lidar:pointCloud", config) == ".exr");
    assert(SanitizeFileNameToken("lidar:pointCloud") == "lidar_pointCloud");
    assert(SanitizeFileNameToken("a / b\\\\c") == "a_b_c");
    assert(SanitizeFileNameToken("::") == "_");

    const auto path = BuildAovOutputPath("/tmp/hydra_capture", "/assets/redbox_right.usd", "lidar:pointCloud", ".exr");
    assert(path == std::filesystem::path("/tmp/hydra_capture/redbox_right/lidar_pointCloud.exr"));
}

void TestRendererSettingConversionUsesDescriptorTypes() {
    const pxr::VtValue floatDefault(1.0f);
    const pxr::VtValue tokenDefault(pxr::TfToken("default"));

    const pxr::VtValue boolValue = ConvertJsonSettingValue(pxr::JsValue(true), nullptr);
    assert(boolValue.IsHolding<bool>());
    assert(boolValue.UncheckedGet<bool>());

    const pxr::VtValue intValue = ConvertJsonSettingValue(pxr::JsValue(4), nullptr);
    assert(intValue.IsHolding<int>());
    assert(intValue.UncheckedGet<int>() == 4);

    const pxr::VtValue floatValue = ConvertJsonSettingValue(pxr::JsValue(0.5), &floatDefault);
    assert(floatValue.IsHolding<float>());
    assert(floatValue.UncheckedGet<float>() == 0.5f);

    const pxr::VtValue tokenValue = ConvertJsonSettingValue(pxr::JsValue("tokenValue"), &tokenDefault);
    assert(tokenValue.IsHolding<pxr::TfToken>());
    assert(tokenValue.UncheckedGet<pxr::TfToken>() == pxr::TfToken("tokenValue"));
}

}  // namespace

int main() {
    TestParseRequiredAndRepeatedAovs();
    TestParseRejectsOldPositionalAndRendererPlugin();
    TestRendererConfigJson();
    TestRendererConfigErrorsNamePathKeyAndType();
    TestAovDefaultsExtensionsAndPaths();
    TestRendererSettingConversionUsesDescriptorTypes();
    return 0;
}
