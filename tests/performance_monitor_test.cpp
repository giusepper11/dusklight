#include "dusk/performance_capture.hpp"

#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <thread>

using namespace dusk::perf;

void require(bool ok, const char* description) {
    if (!ok) {
        std::cerr << "FAIL: " << description << '\n';
        std::exit(1);
    }
}

void near(double actual, double expected, const char* description) {
    require(std::abs(actual - expected) < 0.000001, description);
}

int main() {
    const std::array<Sample, 3> stable{{{20000000, 10000000, 2},
                                         {30000000, 10000000, 3},
                                         {40000000, 10000000, 4}}};
    const auto summary = summarize(stable);
    require(summary.has_value(), "stable intervals have a summary");
    require(summary->count == 3, "three intervals, not four presentation events");
    near(summary->meanMs, 10.0, "stable mean interval");
    near(summary->medianMs, 10.0, "stable median interval");
    near(summary->p95Ms, 10.0, "stable P95 interval");
    near(summary->p99Ms, 10.0, "stable P99 interval");
    near(summary->minMs, 10.0, "stable minimum interval");
    near(summary->maxMs, 10.0, "stable maximum interval");
    near(summary->averageFps, 100.0, "FPS from measured interval sequence");
    std::cout << "PASS: stable presentation interval statistics\n";
    const std::array<Sample, 4> alternating{{{0, 10000000, 0}, {0, 30000000, 0},
                                             {0, 10000000, 0}, {0, 30000000, 0}}};
    const auto mixed = summarize(alternating);
    near(mixed->meanMs, 20.0, "alternating mean");
    near(mixed->medianMs, 20.0, "even median averages middle values");
    near(mixed->p95Ms, 30.0, "alternating P95");
    near(mixed->averageFps, 50.0, "FPS is not mean of instantaneous FPS");
    std::array<Sample, 100> ranked{};
    for (size_t i = 0; i < ranked.size(); ++i) {
        ranked[i].intervalNs = (i + 1) * 1000000;
    }
    const auto ranks = summarize(ranked);
    near(ranks->medianMs, 50.5, "median of 1 through 100");
    near(ranks->p95Ms, 95.0, "nearest-rank P95");
    near(ranks->p99Ms, 99.0, "nearest-rank P99");
    for (auto& sample : ranked) {
        sample.intervalNs = 10000000;
    }
    ranked.back().intervalNs = 1000000000;
    const auto hitch = summarize(ranked);
    near(hitch->p99Ms, 10.0, "one hitch among 100 is above P99");
    near(hitch->maxMs, 1000.0, "hitch is retained in maximum");
    near(hitch->averageFps, 100000.0 / 1990.0, "hitch contributes to sequence FPS");
    require(!summarize({}), "empty intervals are unavailable");
    const std::array<Sample, 1> invalid{{{0, 0, 0}}};
    require(!summarize(invalid), "zero intervals cannot produce fabricated statistics");
    std::cout << "PASS: alternating, hitch, percentiles and empty/invalid statistics\n";
    Capture capture;
    require(capture.start(1), "start a capture");
    require(capture.active(), "capture becomes active");
    capture.record(10000001, 1);
    require(capture.samples().empty(), "first event establishes baseline only");
    capture.record(20000001, 2);
    capture.record(30000001, 3);
    capture.stop();
    require(capture.complete() && !capture.active(), "manual stop finalizes capture");
    require(capture.samples().size() == 2, "two raw intervals for three events");
    require(capture.eventCount() == 3, "event count retained separately");
    require(capture.durationNs() == 20000000, "duration covers observed presentation window");
    require(capture.valid(), "complete uninterrupted sequence is valid");
    require(capture.samples()[0].timestampNs == 20000001 &&
                capture.samples()[1].timestampNs == 30000001, "samples remain chronological");
    require(capture.start(100), "restart a completed capture");
    require(!capture.start(200), "start does not discard an active capture");
    capture.stop();
    require(capture.samples().empty() && capture.eventCount() == 0, "restart clears prior results");
    require(!capture.valid(), "empty capture is not valid measurement evidence");
    require(capture.start(1), "start single-event capture");
    capture.record(10000001, 1);
    capture.stop();
    require(capture.samples().empty() && !capture.valid(), "single event has no interval");
    require(capture.start(1, 30000000), "start bounded-duration capture");
    capture.record(10000001, 1);
    capture.record(20000001, 2);
    capture.record(30000001, 3);
    require(!capture.active() && capture.stopReason() == StopReason::DurationLimit,
            "capture ends at the first event completing the requested duration");
    std::cout << "PASS: capture start/stop/restart, raw order and duration\n";
    require(capture.start(1), "start invalid-timestamp capture");
    capture.record(1000, 1);
    capture.record(2000, 2);
    capture.record(0, 3);
    capture.record(2000, 4);
    capture.record(1500, 5);
    capture.record(3000, 6);
    capture.record(4000, 7);
    capture.record(5000, 9);
    capture.record(6000, 10);
    capture.stop(StopReason::Interrupted);
    require(capture.invalidCount() == 4, "zero, duplicate, backwards and missing event rejected");
    require(capture.samples().size() == 3, "invalid timestamps do not bridge gaps");
    require(capture.samples()[2].presentationId == 10 && capture.samples()[2].intervalNs == 1000,
            "valid sequence resumes after discontinuity without interpolation");
    require(!capture.valid() && capture.stopReason() == StopReason::Interrupted,
            "interrupted capture records reason and invalidity");
    require(capture.start(1, std::numeric_limits<uint64_t>::max()), "start capacity test");
    for (size_t i = 0; i <= SampleCapacity + 3; ++i) {
        capture.record(i + 1, i + 1);
    }
    require(capture.samples().size() == SampleCapacity && capture.truncated(), "bounded sample capacity");
    require(capture.stopReason() == StopReason::Capacity, "capacity has explicit stop reason");
    require(capture.eventCount() == SampleCapacity + 1, "no overwrite after capacity stop");
    require(capture.start(1), "start overflow test");
    capture.record(1000, 1);
    capture.record(2000, 2);
    capture.transportOverflow(7);
    require(capture.truncated() && !capture.active() && !capture.valid() && capture.droppedCount() == 7,
            "transport loss stops capture and invalidates complete-evidence claim");
    std::cout << "PASS: invalid timestamps, gaps, interruption, storage and transport truncation\n";
    require(!collection_enabled(MonitorMode::Off, false), "Off disables continuous collection");
    require(!collection_enabled(MonitorMode::Basic, false), "Basic only uses original FPS counter");
    require(collection_enabled(MonitorMode::Detailed, false), "Detailed enables recent samples");
    require(collection_enabled(MonitorMode::Off, true), "explicit benchmark works with panel Off");
    require(effective_mode(MonitorMode::Off, true) == MonitorMode::Basic, "legacy FPS setting preserved");
    require(effective_mode(MonitorMode::Off, false) == MonitorMode::Off, "default Off stays Off");
    PresentationQueue queue;
    PresentationEvent event{};
    for (size_t i = 0; i < PresentationQueue::Capacity; ++i) {
        require(queue.push({i + 1, i + 1, 9, 11, 12, 13, 14}), "queue accepts its capacity");
    }
    require(!queue.push({}), "full queue never overwrites unread events");
    require(queue.dropped() == 1, "queue loss is explicit");
    for (size_t i = 0; i < PresentationQueue::Capacity; ++i) {
        require(queue.pop(event) && event.presentationId == i + 1 && event.epoch == 9,
                "queue raw event order and generation preserved");
    }
    require(!queue.pop(event), "empty queue does not invent events");
    constexpr uint64_t ConcurrentEvents = 200000;
    std::thread producer([&] {
        for (uint64_t i = 1; i <= ConcurrentEvents; ++i) {
            const PresentationEvent published{i * 1000, i, 42, static_cast<uint32_t>(i % 32), 4, 5, 6};
            while (!queue.push(published, 1, ConcurrentEvents * 1000)) {
                std::this_thread::yield();
            }
        }
    });
    uint64_t observedDrops = 0;
    for (uint64_t i = 1; i <= ConcurrentEvents;) {
        const auto drops = queue.captureDropped(42);
        require(drops >= observedDrops, "current-epoch concurrent loss counts do not tear or regress");
        observedDrops = drops;
        if (queue.pop(event)) {
            require(event.presentationId == i && event.timestampNs == i * 1000 && event.epoch == 42 &&
                        event.drawCalls == i % 32 && event.pipelinesCreated == 5,
                    "concurrent snapshots are ordered and not torn");
            ++i;
        } else {
            std::this_thread::yield();
        }
    }
    producer.join();
    std::cout << "PASS: Off/Basic semantics and bounded concurrent event publication (200000 events)\n";
    Capture regular;
    Capture delayed;
    PresentationQueue regularQueue;
    PresentationQueue delayedQueue;
    regular.start(1);
    delayed.start(1);
    const std::array<uint64_t, 3> endpoints{1000000001, 2000000001, 120000000001};
    uint64_t id = 0;
    for (const auto timestamp : endpoints) {
        const PresentationEvent published{timestamp, ++id, 77, 0, 0, 0, 0};
        regularQueue.push(published, 1, DefaultDurationNs);
        delayedQueue.push(published, 1, DefaultDurationNs);
        drain_presentations(regular, regularQueue, 77, 1);
    }
    drain_presentations(delayed, delayedQueue, 77, 1);
    require(!regular.active() && !delayed.active(), "post-window event finalizes both schedules");
    require(regular.samples().size() == 1 && delayed.samples().size() == 1 &&
                regular.eventCount() == 2 && delayed.eventCount() == 2,
            "same presentation stream gives same population regardless of UI drain schedule");
    require(delayed.durationNs() == 1000000000 && delayed.samples()[0].intervalNs == 1000000000,
            "hitch crossing deadline is excluded, never clipped or interpolated");
    Capture boundary;
    boundary.start(1, 100);
    boundary.record(1, 1);
    boundary.record(101, 2);
    require(!boundary.active() && boundary.samples().size() == 1,
            "endpoint exactly at deadline belongs to closed capture window");

    Capture filled;
    PresentationQueue overflowQueue;
    filled.start(1, 1000000);
    filled.record(1, 1);
    filled.record(2, 2);
    for (size_t i = 0; i < PresentationQueue::Capacity + 7; ++i) {
        overflowQueue.push({i + 3, i + 3, 88, 0, 0, 0, 0}, 1, 1000000);
    }
    drain_presentations(filled, overflowQueue, 88, 1);
    require(filled.stopReason() == StopReason::Capacity && filled.droppedCount() == 7 && !filled.valid(),
            "capacity completion while draining preserves already-observed capture queue losses");

    Capture ended;
    PresentationQueue postWindowQueue;
    ended.start(1, 10);
    for (size_t i = 0; i < PresentationQueue::Capacity + 7; ++i) {
        postWindowQueue.push({i + 1, i + 1, 99, 0, 0, 0, 0}, 1, 10);
    }
    drain_presentations(ended, postWindowQueue, 99, 1);
    require(ended.valid() && ended.droppedCount() == 0 && postWindowQueue.dropped() == 7,
            "drops entirely after deadline do not invalidate a complete capture window");
    std::cout << "PASS: integrated drain scheduling, deadline censoring and window-attributed queue loss\n";
}
