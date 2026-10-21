#include "performance_csv.hpp"

#include <cerrno>
#include <cctype>
#include <fcntl.h>
#include <iomanip>
#include <locale>
#include <sstream>
#include <system_error>
#include <unistd.h>

namespace dusk::perf {
namespace {
std::string quote(const std::string& value) {
    if (value.find_first_of(",\"\r\n") == std::string::npos) {
        return value;
    }
    std::string result = "\"";
    for (const char c : value) {
        if (c == '"') {
            result += '"';
        }
        result += c;
    }
    return result + '"';
}
std::string number(double value) {
    std::ostringstream stream;
    stream.imbue(std::locale::classic());
    stream << std::setprecision(17) << value;
    return stream.str();
}
}

std::optional<std::string> serialize_csv(const Capture& capture, const Metadata& metadata) {
    if (capture.active() || !capture.complete()) {
        return std::nullopt;
    }
    std::ostringstream stream;
    stream.imbue(std::locale::classic());
    stream << "record_type,key,value,presentation_id,presentation_timestamp_ns,presentation_interval_ns\r\n";
    const auto row = [&](const char* type, const std::string& key, const std::string& value) {
        stream << type << ',' << quote(key) << ',' << quote(value) << ",,,\r\n";
    };
    row("metadata", "format_version", "1");
    row("metadata", "measurement", "host-observed successful Surface::Present return interval; not GPU time or scanout");
    row("metadata", "clock", "std::chrono::steady_clock; integer nanoseconds");
    for (const auto& [key, value] : metadata) {
        row("metadata", key, value);
    }
    row("metadata", "requested_start_monotonic_ns", std::to_string(capture.requestedStartNs()));
    row("metadata", "requested_duration_ns", std::to_string(capture.requestedDurationNs()));
    row("metadata", "window_boundary_policy", "whole intervals with both endpoints inside closed request window; boundary-crossing intervals excluded, not clipped or interpolated");
    row("metadata", "first_presentation_monotonic_ns", std::to_string(capture.firstTimestampNs()));
    row("metadata", "presentation_window_duration_ns", std::to_string(capture.durationNs()));
    row("metadata", "sample_count", std::to_string(capture.samples().size()));
    row("metadata", "presentation_event_count", std::to_string(capture.eventCount()));
    row("metadata", "invalid_event_or_gap_count", std::to_string(capture.invalidCount()));
    row("metadata", "transport_dropped_count", std::to_string(capture.droppedCount()));
    row("metadata", "data_valid", capture.valid() ? "true" : "false");
    row("metadata", "capture_truncated", capture.truncated() ? "true" : "false");
    row("metadata", "stop_reason", stop_reason_name(capture.stopReason()));
    row("metadata", "percentile_algorithm", "nearest rank: sorted[ceil(p*N)-1]; median averages middle pair for even N");
    row("metadata", "average_fps_algorithm", "valid_interval_count * 1e9 / sum(valid_interval_ns)");
    const auto summary = summarize(capture.samples());
    for (const auto& [key, value] : std::array<std::pair<const char*, double>, 7>{{
             {"mean_interval_ms", summary ? summary->meanMs : 0},
             {"median_interval_ms", summary ? summary->medianMs : 0},
             {"p95_interval_ms", summary ? summary->p95Ms : 0},
             {"p99_interval_ms", summary ? summary->p99Ms : 0},
             {"min_interval_ms", summary ? summary->minMs : 0},
             {"max_interval_ms", summary ? summary->maxMs : 0},
             {"sequence_average_fps", summary ? summary->averageFps : 0}}}) {
        row("summary", key, summary ? number(value) : "Unavailable");
    }
    uint64_t sumNs = 0;
    for (const auto& sample : capture.samples()) {
        sumNs += sample.intervalNs;
    }
    row("summary", "valid_interval_sum_ns", std::to_string(sumNs));
    size_t index = 0;
    for (const auto& sample : capture.samples()) {
        stream << "sample," << index++ << ",," << sample.presentationId << ',' << sample.timestampNs << ','
               << sample.intervalNs << "\r\n";
    }
    row("metadata", "export_complete", "true");
    return stream.str();
}

ExportResult export_csv(const Capture& capture, const Metadata& metadata,
                        const std::filesystem::path& directory, const std::string& stem, ExportIo io) {
    const auto csv = serialize_csv(capture, metadata);
    if (!csv) {
        return {{}, "Capture must finish before export"};
    }
    if (stem.empty() || stem.size() > 96) {
        return {{}, "Invalid benchmark filename"};
    }
    for (const unsigned char c : stem) {
        if (!std::isalnum(c) && c != '-' && c != '_') {
            return {{}, "Invalid benchmark filename"};
        }
    }
    std::error_code error;
    std::filesystem::create_directories(directory, error);
    if (error) {
        return {{}, error.message()};
    }
    for (unsigned attempt = 0; attempt < 10000; ++attempt) {
        const auto path = directory / (stem + "-" + std::to_string(attempt) + ".partial");
        const int file = ::open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0644);
        if (file < 0) {
            if (errno == EEXIST) {
                continue;
            }
            return {{}, std::error_code(errno, std::generic_category()).message()};
        }
        size_t written = 0;
        int writeError = 0;
        while (written < csv->size()) {
            const auto count = io.write ? io.write(file, csv->data() + written, csv->size() - written)
                                        : ::write(file, csv->data() + written, csv->size() - written);
            if (count < 0 && errno == EINTR) {
                continue;
            }
            if (count <= 0) {
                writeError = count < 0 ? errno : EIO;
                break;
            }
            written += static_cast<size_t>(count);
        }
        const int closeResult = io.close ? io.close(file) : ::close(file);
        if (closeResult != 0 && writeError == 0) {
            writeError = errno;
        }
        if (writeError != 0) {
            // Even a full payload/footer remains explicitly .partial if close
            // fails. It is NEVER published as a completed CSV.
            return {path, std::error_code(writeError, std::generic_category()).message()};
        }
        // Reserve a NEW capture namespace atomically. Never rename onto a
        // pre-existing benchmark/file. Inside our exclusively owned directory,
        // results.csv has never existed. This works on Switch FAT filesystems
        // without relying on unsupported hardlinks/Linux-only rename flags.
        for (unsigned captureIndex = 0; captureIndex < 10000; ++captureIndex) {
            const auto captureDir = directory / (stem + "-" + std::to_string(captureIndex));
            const bool created = std::filesystem::create_directory(captureDir, error);
            if (!created) {
                if (!error || error == std::errc::file_exists) {
                    error.clear();
                    continue;
                }
                return {path, error.message()};
            }
            const auto completePath = captureDir / "results.csv";
            std::filesystem::rename(path, completePath, error);
            if (error) {
                return {path, error.message()};
            }
            return {completePath, {}};
        }
        return {path, "Unique benchmark directory capacity reached; partial output retained"};
    }
    return {{}, "Unique benchmark filename capacity reached"};
}
} // namespace dusk::perf
