#pragma once

#include "performance_capture.hpp"
#include <string>

namespace dusk::perf {
// All functions except Aurora's callback are called only on the game/UI thread.
MonitorMode mode();
void update();
void shutdown();
bool start_benchmark();
void stop_benchmark();
bool capture_active();
bool has_results();
void export_results();
std::string detailed_text(float originalFps);
std::string benchmark_summary();
} // namespace dusk::perf
