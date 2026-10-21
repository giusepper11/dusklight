#pragma once

#include "performance_capture.hpp"

#include <filesystem>
#include <string>
#include <utility>
#include <vector>
#include <sys/types.h>

namespace dusk::perf {
using Metadata = std::vector<std::pair<std::string, std::string>>;

std::optional<std::string> serialize_csv(const Capture& capture, const Metadata& metadata);
struct ExportResult {
    std::filesystem::path path;
    std::string error;
};
// Optional fault-injection seam; production uses the real POSIX operations.
struct ExportIo {
    ssize_t (*write)(int, const void*, size_t) = nullptr;
    int (*close)(int) = nullptr;
};
// UI thread only, AFTER capture ends. Uses exclusive creation, never overwrite.
ExportResult export_csv(const Capture& capture, const Metadata& metadata,
                        const std::filesystem::path& directory, const std::string& stem, ExportIo io = {});
} // namespace dusk::perf
