#include "renderer_config.h"

#include "pxr/base/js/json.h"
#include "pxr/base/js/types.h"

#include <fstream>
#include <sstream>

PXR_NAMESPACE_USING_DIRECTIVE

namespace {

void SetError(std::string* error, const std::string& message) {
    if (error) {
        *error = message;
    }
}

std::string ConfigError(
    const std::filesystem::path& path,
    const std::string& key,
    const std::string& expected,
    const JsValue* actual = nullptr) {
    std::ostringstream out;
    out << path.string() << ": key '" << key << "' must be " << expected;
    if (actual) {
        out << ", got " << actual->GetTypeName();
    }
    return out.str();
}

const JsValue* FindMember(const JsObject& object, const std::string& key) {
    const auto it = object.find(key);
    if (it == object.end()) {
        return nullptr;
    }
    return &it->second;
}

bool ReadStringMember(
    const std::filesystem::path& path,
    const JsObject& object,
    const std::string& key,
    bool required,
    std::string* value,
    std::string* error) {
    const JsValue* member = FindMember(object, key);
    if (!member) {
        if (required) {
            SetError(error, ConfigError(path, key, "a string"));
            return false;
        }
        return true;
    }
    if (!member->IsString()) {
        SetError(error, ConfigError(path, key, "a string", member));
        return false;
    }
    *value = member->GetString();
    return true;
}

bool ReadStringArray(
    const std::filesystem::path& path,
    const JsValue& value,
    const std::string& key,
    std::vector<std::string>* out,
    std::string* error) {
    if (!value.IsArray()) {
        SetError(error, ConfigError(path, key, "an array of strings", &value));
        return false;
    }
    out->clear();
    const JsArray& array = value.GetJsArray();
    for (size_t i = 0; i < array.size(); ++i) {
        if (!array[i].IsString()) {
            SetError(
                error,
                ConfigError(path, key + "[" + std::to_string(i) + "]", "a string", &array[i]));
            return false;
        }
        out->push_back(array[i].GetString());
    }
    return true;
}

}  // namespace

bool LoadRendererConfig(
    const std::filesystem::path& path,
    RendererConfig* config,
    std::string* error) {
    if (!config) {
        SetError(error, "Internal error: renderer config output is null");
        return false;
    }
    *config = RendererConfig();

    std::ifstream input(path);
    if (!input) {
        SetError(error, path.string() + ": failed to open renderer config JSON");
        return false;
    }

    JsParseError parseError;
    const JsValue root = JsParseStream(input, &parseError);
    if (!root) {
        std::ostringstream out;
        out << path.string() << ": failed to parse JSON at line "
            << parseError.line << ", column " << parseError.column << ": "
            << parseError.reason;
        SetError(error, out.str());
        return false;
    }
    if (!root.IsObject()) {
        SetError(error, ConfigError(path, "$", "an object", &root));
        return false;
    }

    const JsObject& rootObject = root.GetJsObject();
    if (!ReadStringMember(path, rootObject, "name", false, &config->name, error)) {
        return false;
    }
    if (!ReadStringMember(
            path,
            rootObject,
            "renderer_plugin",
            true,
            &config->rendererPlugin,
            error)) {
        return false;
    }

    if (const JsValue* defaults = FindMember(rootObject, "defaults")) {
        if (!defaults->IsObject()) {
            SetError(error, ConfigError(path, "defaults", "an object", defaults));
            return false;
        }
        const JsObject& defaultsObject = defaults->GetJsObject();
        if (const JsValue* aovs = FindMember(defaultsObject, "aovs")) {
            if (!ReadStringArray(path, *aovs, "defaults.aovs", &config->defaultAovs, error)) {
                return false;
            }
        }
        if (const JsValue* settings = FindMember(defaultsObject, "settings")) {
            if (!settings->IsObject()) {
                SetError(error, ConfigError(path, "defaults.settings", "an object", settings));
                return false;
            }
            for (const auto& entry : settings->GetJsObject()) {
                config->settings[entry.first] = entry.second;
            }
        }
    }

    if (const JsValue* aovs = FindMember(rootObject, "aovs")) {
        if (!aovs->IsObject()) {
            SetError(error, ConfigError(path, "aovs", "an object", aovs));
            return false;
        }
        for (const auto& entry : aovs->GetJsObject()) {
            if (!entry.second.IsObject()) {
                SetError(error, ConfigError(path, "aovs." + entry.first, "an object", &entry.second));
                return false;
            }
            AovConfig aovConfig;
            if (!ReadStringMember(
                    path,
                    entry.second.GetJsObject(),
                    "output_ext",
                    false,
                    &aovConfig.outputExt,
                    error)) {
                return false;
            }
            config->aovs[entry.first] = aovConfig;
        }
    }

    return true;
}
