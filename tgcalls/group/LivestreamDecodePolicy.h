#ifndef TGCALLS_LIVESTREAM_DECODE_POLICY_H
#define TGCALLS_LIVESTREAM_DECODE_POLICY_H

#include "CallsRuntimeSwitches.h"

#include <atomic>
#include <set>
#include <string>
#include <utility>
#include <vector>

// Decisions behind livestream hardware decoding, kept free of FFmpeg and platform types so they
// are covered by a host test. VideoStreamingPart maps AVCodecParameters into
// LivestreamDecodeStreamInfo and DarwinFFMpeg.mm owns the VideoToolbox plumbing.

namespace tgcalls {

enum class LivestreamDecodeCodec {
    Other,
    H264,
    Hevc
};

enum class LivestreamDecodePixelFormat {
    Other,
    // 8-bit planar 4:2:0 (yuv420p / yuvj420p): decodes to NV12 pixel buffers, which is all the
    // renderers draw.
    Yuv420P8
};

struct LivestreamDecodeStreamInfo {
    LivestreamDecodeCodec codec = LivestreamDecodeCodec::Other;
    LivestreamDecodePixelFormat pixelFormat = LivestreamDecodePixelFormat::Other;
    int width = 0;
    int height = 0;
    bool progressive = true;
};

constexpr int kLivestreamDecodeMinWidth = 640;
constexpr int kLivestreamDecodeMinHeight = 360;
constexpr int kLivestreamDecodeMaxDimension = 4096;

// The renderers draw 8-bit two-plane 4:2:0 pixel buffers only; anything else a hardware decoder
// might hand back is refused and the stream continues in software.
inline bool isAcceptableOutputBuffer(int planeCount, int bitsPerComponent, bool chroma420) {
    return planeCount == 2 && bitsPerComponent == 8 && chroma420;
}

struct HardwareDecodeEligibility {
    bool eligible = false;
    // Stable token for logs: "ok", "codec", "pixel-format", "interlaced" or "size".
    const char *reason = "ok";
};

inline HardwareDecodeEligibility evaluateHardwareDecodeEligibility(LivestreamDecodeStreamInfo const &info) {
    if (info.codec != LivestreamDecodeCodec::H264 && info.codec != LivestreamDecodeCodec::Hevc) {
        return { false, "codec" };
    }
    // 10-bit streams (P010) are not drawn correctly by the 8-bit NV12 shader.
    if (info.pixelFormat != LivestreamDecodePixelFormat::Yuv420P8) {
        return { false, "pixel-format" };
    }
    if (!info.progressive) {
        return { false, "interlaced" };
    }
    if (info.width <= 0 || info.height <= 0 || info.width > kLivestreamDecodeMaxDimension || info.height > kLivestreamDecodeMaxDimension) {
        return { false, "size" };
    }
    // Small streams decode fast in software; a hardware session is a scarce system resource.
    const int longSide = info.width > info.height ? info.width : info.height;
    const int shortSide = info.width > info.height ? info.height : info.width;
    if (longSide < kLivestreamDecodeMinWidth || shortSide < kLivestreamDecodeMinHeight) {
        return { false, "size" };
    }
    return { true, "ok" };
}

// Hardware decode sessions are a small system-wide resource; a few simultaneous streams are
// the most we claim, the rest decode in software.
class HardwareDecodeSessionCounter {
public:
    explicit HardwareDecodeSessionCounter(int capacity) : _capacity(capacity) {
    }

    bool tryAcquire() {
        int current = _active.load();
        while (current < _capacity) {
            if (_active.compare_exchange_weak(current, current + 1)) {
                return true;
            }
        }
        return false;
    }

    void release() {
        int current = _active.load();
        while (current > 0) {
            if (_active.compare_exchange_weak(current, current - 1)) {
                return;
            }
        }
    }

    int active() const {
        return _active.load();
    }

    int capacity() const {
        return _capacity;
    }

    static HardwareDecodeSessionCounter &shared() {
        static HardwareDecodeSessionCounter value(2);
        return value;
    }

private:
    int _capacity;
    std::atomic<int> _active{0};
};

// Holds one slot of a counter until destroyed.
class HardwareDecodeSessionLease {
public:
    HardwareDecodeSessionLease() = default;

    static HardwareDecodeSessionLease acquire(HardwareDecodeSessionCounter &counter) {
        HardwareDecodeSessionLease lease;
        if (counter.tryAcquire()) {
            lease._counter = &counter;
        }
        return lease;
    }

    HardwareDecodeSessionLease(HardwareDecodeSessionLease &&other) noexcept : _counter(other._counter) {
        other._counter = nullptr;
    }

    HardwareDecodeSessionLease &operator=(HardwareDecodeSessionLease &&other) noexcept {
        if (this != &other) {
            reset();
            _counter = other._counter;
            other._counter = nullptr;
        }
        return *this;
    }

    HardwareDecodeSessionLease(HardwareDecodeSessionLease const &) = delete;
    HardwareDecodeSessionLease &operator=(HardwareDecodeSessionLease const &) = delete;

    ~HardwareDecodeSessionLease() {
        reset();
    }

    bool isValid() const {
        return _counter != nullptr;
    }

private:
    void reset() {
        if (_counter) {
            _counter->release();
            _counter = nullptr;
        }
    }

    HardwareDecodeSessionCounter *_counter = nullptr;
};

enum class LivestreamDecodeMode {
    Software,
    Hardware
};

struct LivestreamDecodeDecision {
    LivestreamDecodeMode mode = LivestreamDecodeMode::Software;
    // For the log line: "ok", "switch-off", "platform", "hardware-failed-earlier",
    // "session-cap" or "ineligible:<reason>".
    std::string reason = "switch-off";
    // Valid exactly when mode is Hardware; the decoder keeps it for as long as it lives.
    HardwareDecodeSessionLease lease;
};

inline LivestreamDecodeDecision decideLivestreamDecodeMode(
    bool switchEnabled,
    bool platformSupportsHardware,
    LivestreamDecodeStreamInfo const &info,
    bool streamFailedBefore,
    HardwareDecodeSessionCounter &counter
) {
    LivestreamDecodeDecision decision;
    if (!switchEnabled) {
        decision.reason = "switch-off";
        return decision;
    }
    if (!platformSupportsHardware) {
        decision.reason = "platform";
        return decision;
    }
    if (streamFailedBefore) {
        decision.reason = "hardware-failed-earlier";
        return decision;
    }
    auto eligibility = evaluateHardwareDecodeEligibility(info);
    if (!eligibility.eligible) {
        decision.reason = std::string("ineligible:") + eligibility.reason;
        return decision;
    }
    auto lease = HardwareDecodeSessionLease::acquire(counter);
    if (!lease.isValid()) {
        decision.reason = "session-cap";
        return decision;
    }
    decision.mode = LivestreamDecodeMode::Hardware;
    decision.reason = "ok";
    decision.lease = std::move(lease);
    return decision;
}

// Where a hardware decode failed; only used for the log line.
enum class LivestreamDecodeStage {
    Open,
    Send,
    Receive,
    FrameConversion
};

enum class LivestreamDecodeErrorAction {
    // Today's behaviour: the rest of the part is dropped, the next part starts clean.
    EndPart,
    // The hardware session failed: drop the rest of this part, decode the stream in software
    // from the next part (a part starts on a keyframe).
    FallBackToSoftware
};

// Per-stream: remembers that hardware decoding failed so the stream never retries it.
class LivestreamDecodeHealth {
public:
    bool hardwareFailed() const {
        return _hardwareFailed;
    }

    LivestreamDecodeStage failedStage() const {
        return _failedStage;
    }

    LivestreamDecodeErrorAction onDecodeError(LivestreamDecodeMode mode, LivestreamDecodeStage stage = LivestreamDecodeStage::Receive) {
        if (mode == LivestreamDecodeMode::Hardware) {
            if (!_hardwareFailed) {
                _failedStage = stage;
            }
            _hardwareFailed = true;
            return LivestreamDecodeErrorAction::FallBackToSoftware;
        }
        return LivestreamDecodeErrorAction::EndPart;
    }

private:
    bool _hardwareFailed = false;
    LivestreamDecodeStage _failedStage = LivestreamDecodeStage::Open;
};

// Endpoints whose decoder state can be released: not shown now and not referenced by a
// segment that is still queued.
inline std::vector<std::string> endpointsToPrune(
    std::set<std::string> const &existing,
    std::set<std::string> const &active,
    std::set<std::string> const &queued
) {
    std::vector<std::string> result;
    for (auto const &endpoint : existing) {
        if (active.find(endpoint) == active.end() && queued.find(endpoint) == queued.end()) {
            result.push_back(endpoint);
        }
    }
    return result;
}

} // namespace tgcalls

#endif
