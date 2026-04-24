#include "options.h"

#include <cstdlib>
#include <iostream>

namespace {

bool ParseInt(const std::string& text, int* value) {
    if (!value) {
        return false;
    }
    try {
        size_t consumed = 0;
        const int parsed = std::stoi(text, &consumed);
        if (consumed != text.size()) {
            return false;
        }
        *value = parsed;
        return true;
    } catch (...) {
        return false;
    }
}

void SetError(std::string* error, const std::string& message) {
    if (error) {
        *error = message;
    }
}

}  // namespace

void PrintUsage(std::ostream& out, const char* program) {
    out
        << "Usage:\n  " << program
        << " --renderer-config <plugin.json> --usd <scene.usd> "
           "--output-dir <dir> [options]\n\n"
        << "Options:\n"
        << "  --camera <SdfPath>       Camera prim path, e.g. /World/Camera\n"
        << "  --width <int>            Render width (default: 1280)\n"
        << "  --height <int>           Render height (default: 720)\n"
        << "  --aov <token>            AOV to output; may be repeated\n"
        << "  --max-iterations <int>   Max render iterations (default: 1)\n";
}

bool ParseArgs(int argc, char** argv, Options* options, std::string* error) {
    if (!options) {
        SetError(error, "Internal error: options output is null");
        return false;
    }

    *options = Options();
    for (int i = 1; i < argc; ++i) {
        const std::string arg(argv[i]);
        auto requireValue = [&](const std::string& flag, std::string* value) {
            if (i + 1 >= argc) {
                SetError(error, "Missing value for " + flag);
                return false;
            }
            *value = argv[++i];
            if (value->empty()) {
                SetError(error, "Empty value for " + flag);
                return false;
            }
            return true;
        };

        std::string value;
        if (arg == "--renderer-config") {
            if (!requireValue(arg, &options->rendererConfigPath)) {
                return false;
            }
        } else if (arg == "--usd") {
            if (!requireValue(arg, &options->usdPath)) {
                return false;
            }
        } else if (arg == "--output-dir") {
            if (!requireValue(arg, &options->outputDir)) {
                return false;
            }
        } else if (arg == "--camera") {
            if (!requireValue(arg, &options->cameraPath)) {
                return false;
            }
        } else if (arg == "--aov") {
            if (!requireValue(arg, &value)) {
                return false;
            }
            options->aovOverrides.push_back(value);
        } else if (arg == "--width") {
            if (!requireValue(arg, &value)) {
                return false;
            }
            if (!ParseInt(value, &options->width) || options->width <= 0) {
                SetError(error, "Invalid --width: expected positive integer, got " + value);
                return false;
            }
        } else if (arg == "--height") {
            if (!requireValue(arg, &value)) {
                return false;
            }
            if (!ParseInt(value, &options->height) || options->height <= 0) {
                SetError(error, "Invalid --height: expected positive integer, got " + value);
                return false;
            }
        } else if (arg == "--max-iterations") {
            if (!requireValue(arg, &value)) {
                return false;
            }
            if (!ParseInt(value, &options->maxIterations) || options->maxIterations <= 0) {
                SetError(
                    error,
                    "Invalid --max-iterations: expected positive integer, got " + value);
                return false;
            }
        } else {
            SetError(error, "Unknown argument: " + arg);
            return false;
        }
    }

    std::vector<std::string> missing;
    if (options->rendererConfigPath.empty()) {
        missing.push_back("--renderer-config");
    }
    if (options->usdPath.empty()) {
        missing.push_back("--usd");
    }
    if (options->outputDir.empty()) {
        missing.push_back("--output-dir");
    }
    if (!missing.empty()) {
        std::string message = "Missing required argument";
        if (missing.size() > 1) {
            message += "s";
        }
        message += ": ";
        for (size_t i = 0; i < missing.size(); ++i) {
            if (i != 0) {
                message += ", ";
            }
            message += missing[i];
        }
        SetError(error, message);
        return false;
    }

    return true;
}
