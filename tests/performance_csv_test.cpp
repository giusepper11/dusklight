#include "dusk/performance_csv.hpp"

#include <chrono>
#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <unistd.h>

using namespace dusk::perf;

void require(bool value, const char* description) {
    if (!value) {
        std::cerr << "FAIL: " << description << '\n';
        std::exit(1);
    }
}
std::string read(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}

ssize_t short_write(int file, const void* bytes, size_t count) {
    return ::write(file, bytes, std::min(count, size_t{3}));
}
int writeCalls = 0;
ssize_t failed_write(int file, const void* bytes, size_t count) {
    if (writeCalls++ == 0) {
        return short_write(file, bytes, count);
    }
    errno = ENOSPC;
    return -1;
}
int failed_close(int file) {
    ::close(file);
    errno = EIO;
    return -1;
}

int main(int argc, char** argv) {
    require(argc == 2, "explicit workspace-local output root required");
    const auto dir = std::filesystem::path(argv[1]) /
                     ("run-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    Capture capture;
    Metadata metadata{{"build_id", "synthetic-host-fixture"},
                      {"scene_label", "Synthetic, \"test\"\nfixture"}};
    capture.start(1);
    require(!serialize_csv(capture, metadata), "no export while capturing");
    const auto active = export_csv(capture, metadata, dir, "benchmark");
    require(!active.error.empty() && !std::filesystem::exists(dir), "active capture does not touch filesystem");
    capture.record(1000, 1);
    capture.record(2000, 2);
    capture.record(4500, 3);
    capture.stop();
    const auto csv = serialize_csv(capture, metadata);
    require(csv.has_value(), "completed captures serialize");
    require(csv->find("sample,0,,2,2000,1000\r\nsample,1,,3,4500,2500\r\n") != std::string::npos,
            "raw timestamps, intervals and sample order are exact");
    require(csv->find("\"Synthetic, \"\"test\"\"\nfixture\"") != std::string::npos,
            "metadata comma, quote and newline escaping");
    require(csv->find("metadata,sample_count,2,,,") != std::string::npos &&
                csv->find("metadata,presentation_event_count,3,,,") != std::string::npos &&
                csv->find("metadata,data_valid,true,,,") != std::string::npos,
            "separate event/sample counts and validity metadata");
    require(csv->ends_with("metadata,export_complete,true,,,\r\n"), "completion footer detects partial files");
    auto first = export_csv(capture, metadata, dir, "benchmark");
    require(first.error.empty() && read(first.path) == *csv, "CSV bytes reach actual host file");
    auto second = export_csv(capture, metadata, dir, "benchmark");
    require(second.error.empty() && second.path != first.path && read(first.path) == *csv,
            "unique names never overwrite prior benchmark");
    auto unsafe = export_csv(capture, metadata, dir, "../unsafe");
    require(!unsafe.error.empty(), "filename stem cannot escape benchmark directory");
    auto failed = export_csv(capture, metadata, first.path / "not-a-directory", "benchmark");
    require(!failed.error.empty(), "filesystem write errors reported rather than success");
    const auto shortResult = export_csv(capture, metadata, dir, "short-write", {.write = short_write});
    require(shortResult.error.empty() && read(shortResult.path) == *csv, "short writes preserve every CSV byte");
    const auto writeFailure = export_csv(capture, metadata, dir, "write-error", {.write = failed_write});
    require(!writeFailure.error.empty() && writeFailure.path.extension() == ".partial" &&
                std::filesystem::exists(writeFailure.path), "write failure retains only explicitly partial output");
    const auto closeFailure = export_csv(capture, metadata, dir, "close-error", {.close = failed_close});
    require(!closeFailure.error.empty() && closeFailure.path.extension() == ".partial" &&
                read(closeFailure.path) == *csv,
            "close failure cannot publish complete CSV even when every byte and footer were written");
    require(!std::filesystem::exists(dir / "close-error-0" / "results.csv"),
            "final CSV path absent after close failure");
    capture.start(1);
    capture.stop();
    const auto empty = serialize_csv(capture, metadata);
    require(empty->find("metadata,data_valid,false,,,") != std::string::npos &&
                empty->find("summary,median_interval_ms,Unavailable,,,") != std::string::npos,
            "empty captures export unavailable metrics, not zeros");
    std::cout << "PASS: actual CSV serialization, chronological samples, metadata, exclusive export and errors\n";
    std::cout << "Synthetic fixtures: " << dir << '\n';
}
