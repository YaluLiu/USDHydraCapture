#include "lidar_point_cloud.h"

#include <cerrno>
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <sstream>
#include <unordered_map>

namespace {

void SetError(std::string* error, const std::string& message) {
    if (error) {
        *error = message;
    }
}

std::string TrimAscii(std::string text) {
    const auto isSpace = [](unsigned char c) {
        return c == ' ' || c == '\t' || c == '\r' || c == '\n';
    };
    while (!text.empty() && isSpace(static_cast<unsigned char>(text.front()))) {
        text.erase(text.begin());
    }
    while (!text.empty() && isSpace(static_cast<unsigned char>(text.back()))) {
        text.pop_back();
    }
    return text;
}

bool ParseCsvLine(
    const std::string& line,
    std::vector<std::string>* fields,
    std::string* error) {
    if (!fields) {
        SetError(error, "Internal error: CSV fields output is null.");
        return false;
    }

    fields->clear();
    std::string field;
    bool quoted = false;
    for (size_t i = 0; i < line.size(); ++i) {
        const char c = line[i];
        if (quoted) {
            if (c == '"') {
                if (i + 1 < line.size() && line[i + 1] == '"') {
                    field.push_back('"');
                    ++i;
                } else {
                    quoted = false;
                }
            } else {
                field.push_back(c);
            }
            continue;
        }

        if (c == ',') {
            fields->push_back(field);
            field.clear();
        } else if (c == '"' && field.empty()) {
            quoted = true;
        } else {
            field.push_back(c);
        }
    }

    if (quoted) {
        SetError(error, "Unterminated quoted CSV field.");
        return false;
    }

    fields->push_back(field);
    return true;
}

std::unordered_map<std::string, size_t> BuildHeaderMap(
    const std::vector<std::string>& header) {
    std::unordered_map<std::string, size_t> result;
    for (size_t i = 0; i < header.size(); ++i) {
        result.emplace(TrimAscii(header[i]), i);
    }
    return result;
}

bool RequireColumn(
    const std::unordered_map<std::string, size_t>& columns,
    const std::string& name,
    size_t* index,
    std::string* error) {
    const auto it = columns.find(name);
    if (it == columns.end()) {
        SetError(error, "Missing required LiDAR CSV column: " + name);
        return false;
    }
    *index = it->second;
    return true;
}

bool ParseDoubleField(
    const std::string& text,
    const std::string& column,
    size_t lineNumber,
    double* value,
    std::string* error) {
    const std::string trimmed = TrimAscii(text);
    if (trimmed.empty()) {
        std::ostringstream out;
        out << "Empty numeric value in column '" << column << "' at line " << lineNumber;
        SetError(error, out.str());
        return false;
    }

    char* end = nullptr;
    errno = 0;
    const double parsed = std::strtod(trimmed.c_str(), &end);
    if (errno != 0 || end == trimmed.c_str() || *end != '\0' ||
        !std::isfinite(parsed)) {
        std::ostringstream out;
        out << "Invalid double value in column '" << column << "' at line "
            << lineNumber << ": " << text;
        SetError(error, out.str());
        return false;
    }
    *value = parsed;
    return true;
}

bool ParseUint32Field(
    const std::string& text,
    const std::string& column,
    size_t lineNumber,
    uint32_t* value,
    std::string* error) {
    const std::string trimmed = TrimAscii(text);
    uint64_t parsed = 0;
    const char* begin = trimmed.data();
    const char* end = begin + trimmed.size();
    const std::from_chars_result result = std::from_chars(begin, end, parsed);
    if (result.ec != std::errc() || result.ptr != end ||
        parsed > static_cast<uint64_t>(std::numeric_limits<uint32_t>::max())) {
        std::ostringstream out;
        out << "Invalid uint32 value in column '" << column << "' at line "
            << lineNumber << ": " << text;
        SetError(error, out.str());
        return false;
    }
    *value = static_cast<uint32_t>(parsed);
    return true;
}

bool ParseBoolIntField(
    const std::string& text,
    const std::string& column,
    size_t lineNumber,
    bool* value,
    std::string* error) {
    uint32_t parsed = 0;
    if (!ParseUint32Field(text, column, lineNumber, &parsed, error)) {
        return false;
    }
    if (parsed != 0u && parsed != 1u) {
        std::ostringstream out;
        out << "Invalid boolean integer in column '" << column << "' at line "
            << lineNumber << ": " << text;
        SetError(error, out.str());
        return false;
    }
    *value = parsed == 1u;
    return true;
}

}  // namespace

bool ReadLidarPointCloudCsv(
    const std::filesystem::path& path,
    std::vector<LidarPointSample>* points,
    std::string* error) {
    if (!points) {
        SetError(error, "Internal error: LiDAR point output is null.");
        return false;
    }

    std::ifstream input(path);
    if (!input) {
        SetError(error, "Failed to open LiDAR point cloud CSV: " + path.string());
        return false;
    }

    std::string line;
    if (!std::getline(input, line)) {
        SetError(error, "LiDAR point cloud CSV is empty: " + path.string());
        return false;
    }

    std::vector<std::string> header;
    std::string parseError;
    if (!ParseCsvLine(line, &header, &parseError)) {
        SetError(error, "Failed to parse LiDAR CSV header: " + parseError);
        return false;
    }
    const std::unordered_map<std::string, size_t> columns = BuildHeaderMap(header);

    size_t xColumn = 0;
    size_t yColumn = 0;
    size_t zColumn = 0;
    size_t intensityColumn = 0;
    size_t flagsColumn = 0;
    size_t validColumn = 0;
    size_t hitColumn = 0;
    if (!RequireColumn(columns, "x", &xColumn, error) ||
        !RequireColumn(columns, "y", &yColumn, error) ||
        !RequireColumn(columns, "z", &zColumn, error) ||
        !RequireColumn(columns, "intensity", &intensityColumn, error) ||
        !RequireColumn(columns, "flags", &flagsColumn, error) ||
        !RequireColumn(columns, "valid", &validColumn, error) ||
        !RequireColumn(columns, "hit", &hitColumn, error)) {
        return false;
    }

    std::vector<LidarPointSample> parsedPoints;
    std::vector<std::string> fields;
    size_t lineNumber = 1;
    while (std::getline(input, line)) {
        ++lineNumber;
        if (line.empty() || line == "\r") {
            continue;
        }

        parseError.clear();
        if (!ParseCsvLine(line, &fields, &parseError)) {
            std::ostringstream out;
            out << "Failed to parse LiDAR CSV line " << lineNumber << ": " << parseError;
            SetError(error, out.str());
            return false;
        }
        if (fields.size() != header.size()) {
            std::ostringstream out;
            out << "LiDAR CSV line " << lineNumber << " has " << fields.size()
                << " fields, expected " << header.size();
            SetError(error, out.str());
            return false;
        }

        LidarPointSample point;
        if (!ParseDoubleField(fields[xColumn], "x", lineNumber, &point.x, error) ||
            !ParseDoubleField(fields[yColumn], "y", lineNumber, &point.y, error) ||
            !ParseDoubleField(fields[zColumn], "z", lineNumber, &point.z, error) ||
            !ParseDoubleField(
                fields[intensityColumn], "intensity", lineNumber, &point.intensity, error) ||
            !ParseUint32Field(fields[flagsColumn], "flags", lineNumber, &point.flags, error) ||
            !ParseBoolIntField(fields[validColumn], "valid", lineNumber, &point.valid, error) ||
            !ParseBoolIntField(fields[hitColumn], "hit", lineNumber, &point.hit, error)) {
            return false;
        }

        if (point.valid && point.hit) {
            parsedPoints.push_back(point);
        }
    }

    if (!input.eof()) {
        SetError(error, "Failed while reading LiDAR point cloud CSV: " + path.string());
        return false;
    }

    *points = std::move(parsedPoints);
    return true;
}
