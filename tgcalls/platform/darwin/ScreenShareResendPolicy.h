#ifndef TGCALLS_SCREEN_SHARE_RESEND_POLICY_H
#define TGCALLS_SCREEN_SHARE_RESEND_POLICY_H

#include <algorithm>
#include <cstdint>

// Decisions of the ScreenCaptureKit capturer, free of ScreenCaptureKit so a host test covers them.

namespace tgcalls {

// ScreenCaptureKit delivers a frame only when the screen changed. A still screen sends nothing, so a viewer
// who joins, or a keyframe request after packet loss, gets no picture until something moves. The capturer
// keeps its last frame and hands it to the encoder again whenever nothing real was sent for an interval.
// The conversion cost lies in capturing, not in sending, so a repeat is cheap. Local only: nothing here
// changes what is negotiated or the frame format; a repeat is an ordinary frame with a later timestamp.
//
// nowNs is one monotonic clock reading in nanoseconds; frame timestamps are in whatever clock the frames carry.
class StillScreenRepeater {
public:
    // Wire-neutral: a repeated frame passes through the same adapter and encoder as a real one, at most
    // 10 per second (the stream is capped at 25 fps).
    static constexpr int64_t kIntervalNs = 100 * 1000 * 1000;

    // A real frame was handed to the encoder.
    void noteFrameSent(int64_t nowNs, int64_t frameTimestampNs) {
        _hasFrame = true;
        _lastSentNs = nowNs;
        _lastTimestampNs = frameTimestampNs;
    }

    // True when the last frame should be sent again now. Then *timestampNs is the timestamp for the repeat: the
    // last timestamp advanced by the time that passed since it was sent, so it stays in the frames' own clock
    // domain and rises strictly (the frame adapter and the timestamp aligner downstream need that).
    bool takeResend(int64_t nowNs, int64_t *timestampNs) {
        if (!_hasFrame || nowNs - _lastSentNs < kIntervalNs) {
            return false;
        }
        const int64_t timestamp = _lastTimestampNs + std::max(nowNs - _lastSentNs, kMinStepNs);
        _lastSentNs = nowNs;
        _lastTimestampNs = timestamp;
        *timestampNs = timestamp;
        return true;
    }

    bool hasFrame() const {
        return _hasFrame;
    }

    // The share stopped: the old picture must not reach the next one.
    void reset() {
        _hasFrame = false;
        _lastSentNs = 0;
        _lastTimestampNs = 0;
    }

private:
    static constexpr int64_t kMinStepNs = 1000 * 1000;

    bool _hasFrame = false;
    int64_t _lastSentNs = 0;
    int64_t _lastTimestampNs = 0;
};

} // namespace tgcalls

#endif
