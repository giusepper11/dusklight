#include "performance_capture.hpp"

#include <algorithm>
#include <vector>

namespace dusk::perf {

std::optional<Summary> summarize(std::span<const Sample> samples) {
    if (samples.empty()) {
        return std::nullopt;
    }
    std::vector<uint64_t> sorted;
    sorted.reserve(samples.size());
    long double sum = 0;
    for (const auto& sample : samples) {
        if (sample.intervalNs == 0) {
            return std::nullopt;
        }
        sorted.push_back(sample.intervalNs);
        sum += sample.intervalNs;
    }
    std::sort(sorted.begin(), sorted.end());
    const size_t n = sorted.size();
    const double median = n % 2 ? static_cast<double>(sorted[n / 2])
                                  : (static_cast<double>(sorted[n / 2 - 1]) + sorted[n / 2]) / 2.0;
    return Summary{n, static_cast<double>(sum / n / 1000000.0L), median / 1000000.0,
                   sorted[(95 * n + 99) / 100 - 1] / 1000000.0,
                   sorted[(99 * n + 99) / 100 - 1] / 1000000.0,
                   sorted.front() / 1000000.0, sorted.back() / 1000000.0,
                   static_cast<double>(n * 1000000000.0L / sum)};
}

bool Capture::start(uint64_t requestedStartNs, uint64_t durationNs) {
    if (mActive) {
        return false;
    }
    mRequestedStartNs = requestedStartNs;
    mRequestedDurationNs = durationNs;
    mSize = 0;
    mFirstNs = mLastNs = mLastId = mEvents = mInvalid = mDropped = 0;
    mPreviousValid = mComplete = mTruncated = false;
    mActive = true;
    mStopReason = StopReason::None;
    return true;
}

void Capture::record(uint64_t timestampNs, uint64_t presentationId) {
    if (!mActive) {
        return;
    }
    // Closed request window: only WHOLE intervals with both endpoints inside
    // it are retained. A crossing interval is censored, never clipped.
    if (timestampNs >= mRequestedStartNs && timestampNs - mRequestedStartNs > mRequestedDurationNs) {
        stop(StopReason::DurationLimit);
        return;
    }
    ++mEvents;
    if (timestampNs == 0 || timestampNs < mRequestedStartNs ||
        (mLastNs != 0 && (timestampNs <= mLastNs || presentationId <= mLastId))) {
        ++mInvalid;
        mPreviousValid = false;
        return;
    }
    if (mFirstNs == 0) {
        mFirstNs = timestampNs;
    }
    if (mPreviousValid) {
        if (presentationId == mLastId + 1) {
            mSamples[mSize++] = {timestampNs, timestampNs - mLastNs, presentationId};
        } else {
            // Never bridge missing presentation events with a synthetic interval.
            ++mInvalid;
        }
    }
    mLastNs = timestampNs;
    mLastId = presentationId;
    mPreviousValid = true;
    if (mSize == SampleCapacity) {
        mTruncated = true;
        stop(StopReason::Capacity);
    } else if (timestampNs - mRequestedStartNs >= mRequestedDurationNs) {
        stop(StopReason::DurationLimit);
    }
}

void Capture::stop(StopReason reason) {
    if (mActive) {
        mTruncated |= reason == StopReason::Interrupted;
        mActive = false;
        mComplete = true;
        mStopReason = reason;
    }
}

void Capture::transportOverflow(uint64_t dropped) {
    if ((mActive || mComplete) && dropped > mDropped) {
        mDropped = dropped; // Cumulative count for this capture epoch/window.
        mTruncated = true;
        stop(StopReason::TransportOverflow);
    }
}

void drain_presentations(Capture& capture, PresentationQueue& queue, uint64_t currentEpoch,
                         uint64_t streamStartNs, void (*onEvent)(const PresentationEvent&)) {
    const bool wasActive = capture.active();
    const auto available = queue.available();
    PresentationEvent event{};
    for (size_t i = 0; i < available && queue.pop(event); ++i) {
        if (event.epoch != currentEpoch || (event.timestampNs != 0 && event.timestampNs < streamStartNs)) {
            continue;
        }
        if (onEvent) {
            onEvent(event);
        }
        capture.record(event.timestampNs, event.presentationId);
    }
    if (wasActive) {
        capture.transportOverflow(queue.captureDropped(currentEpoch));
    }
}

const char* stop_reason_name(StopReason reason) {
    switch (reason) {
    case StopReason::None: return "none";
    case StopReason::User: return "user_stop";
    case StopReason::DurationLimit: return "duration_limit";
    case StopReason::Capacity: return "sample_capacity_reached";
    case StopReason::TransportOverflow: return "presentation_queue_overflow";
    case StopReason::Interrupted: return "interrupted";
    }
    return "unavailable";
}

} // namespace dusk::perf
