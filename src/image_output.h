#pragma once

#include "pxr/pxr.h"

#include <cstdint>
#include <string>
#include <vector>

PXR_NAMESPACE_OPEN_SCOPE
class HdRenderBuffer;
PXR_NAMESPACE_CLOSE_SCOPE

struct Rgba8Image {
    int width = 0;
    int height = 0;
    std::vector<uint8_t> pixels;
};

bool ConvertRenderBufferToRGBA8(
    pxr::HdRenderBuffer* renderBuffer,
    Rgba8Image* image);

bool WriteRGBA8Image(
    const Rgba8Image& image,
    const std::string& outputPath);

bool WriteRenderBufferImage(
    pxr::HdRenderBuffer* renderBuffer,
    const std::string& outputPath);
