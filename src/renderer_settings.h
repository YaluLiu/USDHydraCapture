#pragma once

#include "pxr/base/js/value.h"
#include "pxr/base/vt/value.h"
#include "pxr/usdImaging/usdImagingGL/engine.h"

#include <map>
#include <string>

pxr::VtValue ConvertJsonSettingValue(
    const pxr::JsValue& value,
    const pxr::VtValue* descriptorDefaultValue);

bool ApplyRendererSettings(
    pxr::UsdImagingGLEngine* engine,
    const std::map<std::string, pxr::JsValue>& settings);
