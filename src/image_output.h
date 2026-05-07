#pragma once

#include "pxr/pxr.h"

#include <string>

PXR_NAMESPACE_OPEN_SCOPE
class HdRenderBuffer;
PXR_NAMESPACE_CLOSE_SCOPE

bool WriteRenderBufferImage(
    pxr::HdRenderBuffer* renderBuffer,
    const std::string& outputPath);
