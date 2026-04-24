#include "renderer_settings.h"

#include "pxr/base/tf/token.h"

#include <iostream>

PXR_NAMESPACE_USING_DIRECTIVE

VtValue ConvertJsonSettingValue(
    const JsValue& value,
    const VtValue* descriptorDefaultValue) {
    if (value.IsBool()) {
        return VtValue(value.GetBool());
    }
    if (value.IsInt()) {
        if (descriptorDefaultValue && descriptorDefaultValue->IsHolding<float>()) {
            return VtValue(static_cast<float>(value.GetInt()));
        }
        if (descriptorDefaultValue && descriptorDefaultValue->IsHolding<double>()) {
            return VtValue(static_cast<double>(value.GetInt()));
        }
        return VtValue(value.GetInt());
    }
    if (value.IsReal()) {
        if (descriptorDefaultValue && descriptorDefaultValue->IsHolding<float>()) {
            return VtValue(static_cast<float>(value.GetReal()));
        }
        return VtValue(value.GetReal());
    }
    if (value.IsString()) {
        if (descriptorDefaultValue && descriptorDefaultValue->IsHolding<TfToken>()) {
            return VtValue(TfToken(value.GetString()));
        }
        return VtValue(value.GetString());
    }
    return VtValue();
}

bool ApplyRendererSettings(
    UsdImagingGLEngine* engine,
    const std::map<std::string, JsValue>& settings) {
    if (!engine) {
        return false;
    }

    const UsdImagingGLRendererSettingsList descriptors =
        engine->GetRendererSettingsList();
    for (const auto& setting : settings) {
        const TfToken key(setting.first);
        const VtValue* defaultValue = nullptr;
        for (const UsdImagingGLRendererSetting& descriptor : descriptors) {
            if (descriptor.key == key) {
                defaultValue = &descriptor.defValue;
                break;
            }
        }

        VtValue converted = ConvertJsonSettingValue(setting.second, defaultValue);
        if (converted.IsEmpty()) {
            std::cerr << "Unsupported renderer setting type for key '"
                      << setting.first << "': " << setting.second.GetTypeName()
                      << "\n";
            return false;
        }
        engine->SetRendererSetting(key, converted);
    }

    return true;
}
