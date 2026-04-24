#pragma once

#include <iosfwd>
#include <string>
#include <vector>

struct Options {
    std::string rendererConfigPath;
    std::string usdPath;
    std::string outputDir;
    std::string cameraPath;
    std::vector<std::string> aovOverrides;
    int width = 1280;
    int height = 720;
    int maxIterations = 1;
};

bool ParseArgs(int argc, char** argv, Options* options, std::string* error);
void PrintUsage(std::ostream& out, const char* program);
