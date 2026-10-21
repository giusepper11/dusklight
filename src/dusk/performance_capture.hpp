#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace dusk::perf {

enum class MonitorMode : uint8_t { Off, Basic, Detailed };
constexpr MonitorMode effective_mode(MonitorMode mode, bool legacyFpsEnabled) {
    return mode == MonitorMode::Off && legacyFpsEnabled ? MonitorMode::Basic : mode;
}
constexpr bool collection_enabled(MonitorMode mode, bool captureActive) {
    return mode == MonitorMode::Detailed || captureActive;
}

struct PresentationEvent {
    uint64_t timestampNs;
    uint64_t presentationId;
    uint64_t epoch;
    uint32_t drawCalls;
    uint32_t mergedDrawCalls;
    uint32_t pipelinesCreated;
    uint32_t pipelinesQueued;
};

// Exactly one serialized presentation producer and one game/UI consumer.
// Release/acquire index transfer gives exclusive ownership of each plain slot;
// unread slots are NEVER overwritten. Neither operation waits or allocates.
class PresentationQueue {
public:
    static constexpr size_t Capacity = 8192;
    bool push(const PresentationEvent& event, uint64_t captureStartNs = 0, uint64_t captureDurationNs = 0) {
        const auto write = mWrite.load(std::memory_order_relaxed);
        if (write - mRead.load(std::memory_order_acquire) == Capacity) {
            mDropped.fetch_add(1, std::memory_order_relaxed);
            if (captureStartNs != 0 && event.timestampNs >= captureStartNs &&
                event.timestampNs - captureStartNs <= captureDurationNs) {
                if (mProducerDropEpoch != event.epoch) {
                    mProducerDropEpoch = event.epoch;
                    mProducerCaptureDrops = 0;
                }
                mCaptureDrops.store(++mProducerCaptureDrops, std::memory_order_release);
                mCaptureDropEpoch.store(event.epoch, std::memory_order_release);
            }
            return false;
        }
        mSlots[write % Capacity] = event;
        mWrite.store(write + 1, std::memory_order_release);
        return true;
    }
    bool pop(PresentationEvent& event) {
        const auto read = mRead.load(std::memory_order_relaxed);
        if (read == mWrite.load(std::memory_order_acquire)) {
            return false;
        }
        event = mSlots[read % Capacity];
        mRead.store(read + 1, std::memory_order_release);
        return true;
    }
    size_t available() const {
        return mWrite.load(std::memory_order_acquire) - mRead.load(std::memory_order_relaxed);
    }
    uint64_t dropped() const { return mDropped.load(std::memory_order_relaxed); }
    // Query ONLY the consumer's CURRENT epoch. The consumer publishes epochs,
    // so the producer cannot reset this count for a future epoch during a read.
    // Old in-flight publications carry old epochs and are ignored.
    uint64_t captureDropped(uint64_t currentEpoch) const {
        if (mCaptureDropEpoch.load(std::memory_order_acquire) != currentEpoch) {
            return 0;
        }
        return mCaptureDrops.load(std::memory_order_acquire);
    }

private:
    static_assert(std::atomic<size_t>::is_always_lock_free);
    static_assert(std::atomic<uint64_t>::is_always_lock_free);
    std::array<PresentationEvent, Capacity> mSlots{};
    alignas(64) std::atomic<size_t> mWrite{0};
    alignas(64) std::atomic<size_t> mRead{0};
    std::atomic<uint64_t> mDropped{0};
    uint64_t mProducerDropEpoch = 0; // Producer-owned, never read by consumer.
    uint64_t mProducerCaptureDrops = 0;
    std::atomic<uint64_t> mCaptureDropEpoch{0};
    std::atomic<uint64_t> mCaptureDrops{0};
};

struct Sample {
    uint64_t timestampNs;
    uint64_t intervalNs;
    uint64_t presentationId;
};

struct Summary {
    size_t count;
    double meanMs;
    double medianMs;
    double p95Ms;
    double p99Ms;
    double minMs;
    double maxMs;
    double averageFps;
};

// Percentiles use nearest rank: sorted[ceil(p * N) - 1]. The median is
// the middle value, or arithmetic mean of the two middle values for even N.
std::optional<Summary> summarize(std::span<const Sample> samples);

inline constexpr size_t SampleCapacity = 8192;
inline constexpr uint64_t DefaultDurationNs = 60000000000;

enum class StopReason { None, User, DurationLimit, Capacity, TransportOverflow, Interrupted };
const char* stop_reason_name(StopReason reason);

// Single consumer (game/UI thread) owns this object, including export.
class Capture {
public:
    bool start(uint64_t requestedStartNs, uint64_t durationNs = DefaultDurationNs);
    void record(uint64_t timestampNs, uint64_t presentationId);
    void stop(StopReason reason = StopReason::User);
    void transportOverflow(uint64_t dropped);
    bool active() const { return mActive; }
    bool complete() const { return mComplete; }
    bool truncated() const { return mTruncated; }
    bool valid() const {
        return mComplete && mSize != 0 && mInvalid == 0 && mDropped == 0 && !mTruncated;
    }
    std::span<const Sample> samples() const { return {mSamples.data(), mSize}; }
    uint64_t eventCount() const { return mEvents; }
    uint64_t invalidCount() const { return mInvalid; }
    uint64_t droppedCount() const { return mDropped; }
    uint64_t durationNs() const { return mFirstNs == 0 ? 0 : mLastNs - mFirstNs; }
    uint64_t requestedStartNs() const { return mRequestedStartNs; }
    uint64_t requestedDurationNs() const { return mRequestedDurationNs; }
    uint64_t firstTimestampNs() const { return mFirstNs; }
    StopReason stopReason() const { return mStopReason; }

private:
    std::array<Sample, SampleCapacity> mSamples{};
    size_t mSize = 0;
    uint64_t mRequestedStartNs = 0;
    uint64_t mRequestedDurationNs = DefaultDurationNs;
    uint64_t mFirstNs = 0;
    uint64_t mLastNs = 0;
    uint64_t mLastId = 0;
    uint64_t mEvents = 0;
    uint64_t mInvalid = 0;
    uint64_t mDropped = 0;
    bool mPreviousValid = false;
    bool mActive = false;
    bool mComplete = false;
    bool mTruncated = false;
    StopReason mStopReason = StopReason::None;
};

// The actual UI drain path, shared with synthetic/concurrent host regressions.
// A fixed queue prefix is processed; completion NEVER suppresses loss evidence.
void drain_presentations(Capture& capture, PresentationQueue& queue, uint64_t currentEpoch,
                         uint64_t streamStartNs, void (*onEvent)(const PresentationEvent&) = nullptr);

} // namespace dusk::perf
