#include "performance_monitor.hpp"

#include "performance_build_identity.hpp"
#include "performance_csv.hpp"
#include "dusk/main.h"
#include "dusk/settings.h"
#include "dusk/ui/ui.hpp"

#include <aurora/aurora.h>
#include <aurora/gfx.h>
#include <aurora/lib/window.hpp>
#include <fmt/format.h>
#include <nlohmann/json.hpp>
#ifdef __SWITCH__
#include <switch.h>
#endif

#include <algorithm>
#include <chrono>
#include <ctime>
#include <fstream>

namespace dusk::perf {
namespace {
// Fixed, process-lifetime callback target: unregister never frees a slot or target
// while an already-loaded callback is in flight. Aurora owns worker shutdown.
PresentationQueue gQueue;
std::atomic<uint64_t> gPublishedEpoch{0};
std::atomic<uint64_t> gPublishedWindowStartNs{0};
std::atomic<uint64_t> gPublishedWindowDurationNs{0};
uint64_t gEpoch = 0;
uint64_t gStreamStartNs = 0;

Capture gCapture;
Metadata gMetadata;
Metadata gStartConditions;
bool gConditionsChanged = false;
bool gFinalized = false;
std::optional<Summary> gCaptureSummary;
std::array<Sample, 256> gRecent{};
size_t gRecentCount = 0;
size_t gRecentWrite = 0;
uint64_t gRecentLastInterval = 0;
std::optional<PresentationEvent> gPrevious;
std::optional<PresentationEvent> gLatest;

uint64_t now_ns() {
    const auto value = std::chrono::duration_cast<std::chrono::nanoseconds>(
                           std::chrono::steady_clock::now().time_since_epoch()).count();
    return value > 0 ? static_cast<uint64_t>(value) : 0;
}

void on_present(const AuroraPresentEvent* event) {
    const auto epoch = gPublishedEpoch.load(std::memory_order_acquire);
    if (epoch != 0) {
        // Exactly one producer; no allocation, file I/O, sorting, waits or locks.
        gQueue.push({event->timestampNs, event->presentationId, epoch, event->drawCalls,
                     event->mergedDrawCalls, event->pipelinesCreated, event->pipelinesQueued},
                    gPublishedWindowStartNs.load(std::memory_order_relaxed),
                    gPublishedWindowDurationNs.load(std::memory_order_relaxed));
    }
}

void clear_recent() {
    gRecentCount = gRecentWrite = 0;
    gRecentLastInterval = 0;
    gPrevious.reset();
    gLatest.reset();
}

void start_stream(bool benchmark = false) {
    // Discard a bounded old prefix; an old in-flight producer can still publish,
    // so all readers ALSO check the epoch and monotonic start boundary.
    PresentationEvent stale{};
    const auto available = gQueue.available();
    for (size_t i = 0; i < available; ++i) {
        gQueue.pop(stale);
    }
    clear_recent();
    ++gEpoch;
    gStreamStartNs = now_ns();
    // Bounds are atomic because an old-epoch callback may still read them.
    // Release-publishing the new epoch guarantees its producer sees new bounds;
    // any old producer observing new bounds still tags its losses with OLD epoch.
    gPublishedWindowStartNs.store(benchmark ? gStreamStartNs : 0, std::memory_order_relaxed);
    gPublishedWindowDurationNs.store(benchmark ? DefaultDurationNs : 0, std::memory_order_relaxed);
    gPublishedEpoch.store(gEpoch, std::memory_order_release);
    aurora_set_present_callback(on_present);
}

void stop_stream() {
    gPublishedEpoch.store(0, std::memory_order_release);
    aurora_set_present_callback(nullptr);
    clear_recent();
}

std::string mode_name(MonitorMode value) {
    switch (value) {
    case MonitorMode::Off: return "Off";
    case MonitorMode::Basic: return "Basic";
    case MonitorMode::Detailed: return "Detailed";
    }
    return "Unavailable";
}

Metadata conditions() {
    const auto& settings = getSettings();
    const auto size = aurora::window::get_window_size();
    std::string displayMode = "Unavailable";
#ifdef __SWITCH__
    displayMode = appletGetOperationMode() == AppletOperationMode_Console ? "Docked" : "Handheld";
#endif
    return {{"display_mode", displayMode},
            {"reported_gx_framebuffer", fmt::format("{}x{}", size.fb_width, size.fb_height)},
            {"reported_output_framebuffer", fmt::format("{}x{}", size.native_fb_width, size.native_fb_height)},
            {"resolution_provenance", "Aurora window configuration report; not GPU readback"},
            {"configured_fps_cap", std::to_string(settings.video.maxFrameRate.getValue())},
            {"configured_vsync", settings.video.enableVsync.getValue() ? "true" : "false"},
            {"configured_frame_interpolation", std::to_string(static_cast<int>(settings.game.enableFrameInterpolation.getValue()))},
            {"configured_internal_resolution_scale", std::to_string(settings.game.internalResolutionScale.getValue())},
            {"monitor_mode", mode_name(mode())}};
}

std::string associated_manifest_hash() {
    // NRO self-hashing is deliberately absent. This is an associated manifest,
    // generated AFTER a host verified the artifact, never a circular ROMFS hash.
    try {
        std::ifstream file(ConfigPath / "performance-monitor-build.json", std::ios::binary);
        std::array<char, 16385> buffer{};
        file.read(buffer.data(), buffer.size());
        const auto size = file.gcount();
        if (size <= 0 || size > 16384) {
            return {};
        }
        const auto json = nlohmann::json::parse(buffer.data(), buffer.data() + size, nullptr, false);
        if (!json.is_object() || json.value("build_id", "") != build::Id ||
            json.value("game_source_revision", "") != build::GameRevision ||
            json.value("aurora_source_revision", "") != build::AuroraRevision) {
            return {};
        }
        const auto hash = json.value("nro_sha256", std::string{});
        if (hash.size() != 64 || hash.find_first_not_of("0123456789abcdefABCDEF") != std::string::npos) {
            return {};
        }
        return hash;
    } catch (const std::exception&) {
        return {};
    }
}

void finalize() {
    if (!gCapture.complete() || gFinalized) {
        return;
    }
    gCaptureSummary = summarize(gCapture.samples());
    const auto endConditions = conditions();
    gConditionsChanged |= endConditions != gStartConditions;
    for (const auto& [key, value] : endConditions) {
        gMetadata.emplace_back(key + "_end", value);
    }
    gMetadata.emplace_back("conditions_changed_during_capture", gConditionsChanged ? "true" : "false");
    gFinalized = true;
    ui::push_toast({.title = "Benchmark capture finished",
                    .content = fmt::format("{} intervals retained. Export Results saves the raw CSV.",
                                           gCapture.samples().size()),
                    .duration = std::chrono::seconds{5}});
}

void record_recent(const PresentationEvent& event) {
    if (gPrevious && event.timestampNs > gPrevious->timestampNs &&
        event.presentationId == gPrevious->presentationId + 1) {
        gRecentLastInterval = event.timestampNs - gPrevious->timestampNs;
        gRecent[gRecentWrite] = {event.timestampNs, gRecentLastInterval, event.presentationId};
        gRecentWrite = (gRecentWrite + 1) % gRecent.size();
        gRecentCount = std::min(gRecentCount + 1, gRecent.size());
    } else {
        gRecentCount = gRecentWrite = 0;
        gRecentLastInterval = 0;
    }
    if (event.timestampNs != 0 && (!gPrevious || event.timestampNs > gPrevious->timestampNs)) {
        gPrevious = event;
        gLatest = event;
    } else {
        gPrevious.reset();
        gLatest.reset();
    }
}
}

MonitorMode mode() {
    auto value = getSettings().video.performanceMonitor.getValue();
    if (value != MonitorMode::Off && value != MonitorMode::Basic && value != MonitorMode::Detailed) {
        value = MonitorMode::Off;
    }
    return effective_mode(value, getSettings().video.enableFpsOverlay.getValue());
}

void update() {
    const bool enabled = collection_enabled(mode(), gCapture.active());
    if (!enabled) {
        if (gPublishedEpoch.load(std::memory_order_acquire) != 0) {
            stop_stream();
        }
        return; // Off/Basic + no benchmark: no new timestamps, queue work or sorting.
    }
    if (gPublishedEpoch.load(std::memory_order_acquire) == 0) {
        start_stream();
    }
    drain_presentations(gCapture, gQueue, gEpoch, gStreamStartNs,
                        mode() == MonitorMode::Detailed ? record_recent : nullptr);
    if (gCapture.active()) {
        gConditionsChanged |= conditions() != gStartConditions;
    }
    finalize();
    if (!collection_enabled(mode(), gCapture.active())) {
        stop_stream();
    }
}

bool start_benchmark() {
    if (gCapture.active() || !IsGameLaunched) {
        return false;
    }
    gCaptureSummary.reset();
    gConditionsChanged = gFinalized = false;
    gMetadata = {{"build_id", build::Id}, {"game_source_revision", build::GameRevision},
                 {"aurora_source_revision", build::AuroraRevision},
                 {"source_dirty_at_configure", build::SourceDirty ? "true" : "false"},
                 {"compiler", build::Compiler}, {"toolchain_image", build::Toolchain},
                 {"graphics_backend", aurora_get_backend() == BACKEND_VULKAN ? build::VulkanBackend : "Unavailable (not Vulkan)"},

                 {"wall_clock_calibrated", "false"},
                 {"scene_label", getSettings().video.benchmarkSceneLabel.getValue().substr(0, 160)},
                 {"scene_label_origin", "manual user selection; no location detector"},
                 {"cpu_clock_hz", "Unavailable"}, {"gpu_clock_hz", "Unavailable"},
                 {"memory_clock_hz", "Unavailable"}, {"gpu_time", "Unavailable (not calibrated)"},
                 {"cpu_render_time", "Unavailable (not instrumented)"},
                 {"sample_capacity", std::to_string(SampleCapacity)},
                 {"presentation_queue_capacity", std::to_string(PresentationQueue::Capacity)},
                 {"capture_scope", "all successful presentation returns, including menus and transitions"},
                 {"transport_drop_scope", "current capture epoch; lost endpoints inside the closed monotonic request window"}};
    const auto hash = associated_manifest_hash();
    gMetadata.emplace_back("nro_sha256", hash.empty() ? "Unavailable" : hash);
    gMetadata.emplace_back("nro_sha256_origin", hash.empty()
        ? "Unavailable: install matching host-verified performance-monitor-build.json in active data root"
        : "externally supplied identity-matched manifest; running NRO bytes not verified");
    gStartConditions = conditions();
    gMetadata.insert(gMetadata.end(), gStartConditions.begin(), gStartConditions.end());
    // Prepare metadata (including manifest disk read) BEFORE capture starts.
    const auto wall = std::chrono::system_clock::now().time_since_epoch();
    const auto wallMs = std::chrono::duration_cast<std::chrono::milliseconds>(wall).count();
    gMetadata.emplace_back("capture_start_utc_unix_ms", std::to_string(wallMs));
    start_stream(true);
    gCapture.start(gStreamStartNs);
    return true;
}

void stop_benchmark() {
    update(); // Consume the bounded prefix already published before manual stop.
    gCapture.stop();
    finalize();
    if (!collection_enabled(mode(), false)) {
        stop_stream();
    }
}

void shutdown() {
    update();
    gCapture.stop(StopReason::Interrupted);
    finalize();
    stop_stream();
}

bool capture_active() { return gCapture.active(); }
bool has_results() { return gCapture.complete() && gFinalized; }

void export_results() {
    if (!has_results() || gCapture.active()) {
        return;
    }
    // Only this UI-side action does file I/O. No automatic export or worker I/O.
    const auto result = export_csv(gCapture, gMetadata, ConfigPath / "benchmarks",
                                   "benchmark-" + std::string(build::Id).substr(0, 12));
    ui::push_toast({.type = result.error.empty() ? "success" : "warning",
                   .title = result.error.empty() ? "Benchmark CSV exported" : "Benchmark export failed",
                   .content = result.error.empty() ? result.path.string() : result.error,
                   .duration = std::chrono::seconds{8}});
}

std::string benchmark_summary() {
    if (gCapture.active()) {
        return fmt::format("Capturing: {} valid intervals. Default limit: 60 s.\n"
                           "Close menus to return to the scene; menu/transition intervals are included. "
                           "Finalization waits for the first presentation at/after the window end, or Stop.",
                           gCapture.samples().size());
    }
    if (!has_results()) {
        return "No benchmark captured. Start Benchmark records up to 60 seconds.\n"
               "Raw samples stay in memory until Export Results. Starting again replaces the in-memory capture.";
    }
    std::string text = fmt::format("Capture: {} intervals / {} presentation events; {:.3f} s observed window.\n"
                                  "Stop: {} | valid: {} | truncated: {} | invalid/gaps: {} | queue drops: {}.\n",
                                  gCapture.samples().size(), gCapture.eventCount(), gCapture.durationNs() / 1e9,
                                  stop_reason_name(gCapture.stopReason()), gCapture.valid() ? "yes" : "no",
                                  gCapture.truncated() ? "yes" : "no", gCapture.invalidCount(), gCapture.droppedCount());
    if (gCaptureSummary) {
        const auto& s = *gCaptureSummary;
        text += fmt::format("Presentation intervals (ms): mean {:.3f}, median {:.3f}, P95 {:.3f}, P99 {:.3f}, "
                            "min {:.3f}, max {:.3f}.\nSequence average FPS: {:.3f}.",
                            s.meanMs, s.medianMs, s.p95Ms, s.p99Ms, s.minMs, s.maxMs, s.averageFps);
    } else {
        text += "Presentation interval statistics: Unavailable (no valid intervals).";
    }
    if (gConditionsChanged) {
        text += "\nWarning: recorded conditions changed during capture.";
    }
    return text;
}

std::string detailed_text(float originalFps) {
    std::string text = fmt::format("{:.0f} FPS (existing counter)\nPresentation intervals (last <=256):\n", originalFps);
    const auto now = now_ns();
    const bool recent = gLatest && now >= gLatest->timestampNs && now - gLatest->timestampNs < 2000000000;
    const auto summary = recent ? summarize({gRecent.data(), gRecentCount}) : std::nullopt;
    if (summary) {
        text += fmt::format("Latest {:.3f} ms | Median {:.3f} ms\nP95 {:.3f} ms | P99 {:.3f} ms | N={}\n",
                            gRecentLastInterval / 1e6, summary->medianMs, summary->p95Ms, summary->p99Ms, summary->count);
    } else {
        text += "Latest / Median / P95 / P99: Unavailable\n";
    }
    if (recent) {
        text += fmt::format("Draw calls (worker sample): {} | merged {}\n"
                            "Pipelines (async): created {} | queued {}\n",
                            gLatest->drawCalls, gLatest->mergedDrawCalls,
                            gLatest->pipelinesCreated, gLatest->pipelinesQueued);
    } else {
        text += "Draw calls / pipeline counters: Unavailable\n";
    }
    text += "GPU time: Unavailable (not calibrated)\n";
    text += gCapture.active() ? fmt::format("Benchmark: capturing ({} intervals)", gCapture.samples().size())
                             : "Benchmark: idle";
    return text;
}
} // namespace dusk::perf
