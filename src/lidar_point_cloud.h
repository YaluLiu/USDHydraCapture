#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

struct LidarPointSample {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
    double intensity = 0.0;
    uint32_t flags = 0;
    bool valid = false;
    bool hit = false;
};

bool ReadLidarPointCloudCsv(
    const std::filesystem::path& path,
    std::vector<LidarPointSample>* points,
    std::string* error);
