#pragma once

#include "hydra_capture_engine.h"
#include "image_output.h"
#include "lidar_point_cloud.h"

#include <cstddef>
#include <cstdint>
#include <vector>

struct LidarOverlayOptions {
    int pointRadiusPixels = 2;
    uint8_t r = 0;
    uint8_t g = 255;
    uint8_t b = 255;
    uint8_t a = 255;
};

size_t DrawLidarPointOverlay(
    const RenderCameraState& camera,
    const std::vector<LidarPointSample>& points,
    const LidarOverlayOptions& options,
    Rgba8Image* image);
