#include "lidar_overlay.h"

#include "pxr/base/gf/vec4d.h"

#include <algorithm>
#include <cmath>

PXR_NAMESPACE_USING_DIRECTIVE

namespace {

bool HasValidImageStorage(const Rgba8Image& image) {
    return image.width > 0 && image.height > 0 &&
        image.pixels.size() ==
            static_cast<size_t>(image.width) * static_cast<size_t>(image.height) * 4u;
}

bool WriteOverlaySquare(
    int centerX,
    int centerY,
    int radius,
    const LidarOverlayOptions& options,
    Rgba8Image* image) {
    bool wrotePixel = false;
    const int minX = std::max(0, centerX - radius);
    const int maxX = std::min(image->width - 1, centerX + radius);
    const int minY = std::max(0, centerY - radius);
    const int maxY = std::min(image->height - 1, centerY + radius);
    for (int y = minY; y <= maxY; ++y) {
        for (int x = minX; x <= maxX; ++x) {
            const size_t pixel =
                (static_cast<size_t>(y) * static_cast<size_t>(image->width) +
                    static_cast<size_t>(x)) * 4u;
            image->pixels[pixel + 0] = options.r;
            image->pixels[pixel + 1] = options.g;
            image->pixels[pixel + 2] = options.b;
            image->pixels[pixel + 3] = options.a;
            wrotePixel = true;
        }
    }
    return wrotePixel;
}

}  // namespace

size_t DrawLidarPointOverlay(
    const RenderCameraState& camera,
    const std::vector<LidarPointSample>& points,
    const LidarOverlayOptions& options,
    Rgba8Image* image) {
    if (!camera.valid || camera.width <= 0 || camera.height <= 0 ||
        !image || !HasValidImageStorage(*image) ||
        camera.width != image->width || camera.height != image->height) {
        return 0;
    }

    const int radius = std::max(0, options.pointRadiusPixels);
    size_t drawnPoints = 0;
    for (const LidarPointSample& point : points) {
        if (!std::isfinite(point.x) || !std::isfinite(point.y) ||
            !std::isfinite(point.z)) {
            continue;
        }
        const GfVec4d clip =
            GfVec4d(point.x, point.y, point.z, 1.0) *
            camera.viewMatrix *
            camera.projectionMatrix;
        if (!std::isfinite(clip[0]) || !std::isfinite(clip[1]) ||
            !std::isfinite(clip[2]) || !std::isfinite(clip[3]) ||
            clip[3] <= 0.0) {
            continue;
        }

        const double ndcX = clip[0] / clip[3];
        const double ndcY = clip[1] / clip[3];
        if (!std::isfinite(ndcX) || !std::isfinite(ndcY) ||
            ndcX < -1.0 || ndcX > 1.0 || ndcY < -1.0 || ndcY > 1.0) {
            continue;
        }

        const double pixelX =
            (ndcX * 0.5 + 0.5) * static_cast<double>(camera.width - 1);
        const double pixelY =
            (1.0 - (ndcY * 0.5 + 0.5)) * static_cast<double>(camera.height - 1);
        if (!std::isfinite(pixelX) || !std::isfinite(pixelY)) {
            continue;
        }
        const int centerX = static_cast<int>(std::lround(pixelX));
        const int centerY = static_cast<int>(std::lround(pixelY));
        if (WriteOverlaySquare(centerX, centerY, radius, options, image)) {
            ++drawnPoints;
        }
    }
    return drawnPoints;
}
