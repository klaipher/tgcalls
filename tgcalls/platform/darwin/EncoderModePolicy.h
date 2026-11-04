#ifndef TGCALLS_ENCODER_MODE_POLICY_H
#define TGCALLS_ENCODER_MODE_POLICY_H

#include <atomic>

namespace tgcalls {

struct EncoderMode {
    // VideoToolbox low-latency rate control (kVTVideoEncoderSpecification_EnableLowLatencyRateControl).
    bool lowLatency = false;
    // kVTCompressionPropertyKey_RealTime. Only ever set together with low-latency rate control; the
    // standard mode keeps the false WebRTC has always used here.
    bool realTime = false;
    // Stable token for the log line: "ok", "switch-off", "os" or "failed-earlier".
    const char *reason = "switch-off";
};

// Remembers, for the process, that a low-latency session could not be created or configured.
class EncoderLowLatencyLatch {
public:
    static bool failed() {
        return flag().load();
    }
    static void markFailed() {
        flag().store(true);
    }
    static void reset() {
        flag().store(false);
    }

private:
    static std::atomic<bool> &flag() {
        static std::atomic<bool> value{false};
        return value;
    }
};

enum class EncodeFailureKind {
    // kVTInvalidSessionErr: normal after the app returns from the background.
    SessionInvalid,
    // kVTVideoEncoderMalfunctionErr.
    Malfunction,
    // A failure reported by the output callback (status != noErr).
    AsyncEncodeError,
};

// Decides whether a failure of a session that runs in low-latency mode must end low-latency mode for the
// process (the next session is the standard one, today's behaviour) instead of rebuilding the same mode.
// A malfunction or an asynchronous encode error is fatal at once. An invalid session is expected after a
// background trip, so it only counts when it repeats without a single encoded frame in between.
class LowLatencyFailureTracker {
public:
    static constexpr int kMaxInvalidSessionsInARow = 3;

    bool shouldFallBack(bool usesLowLatency, EncodeFailureKind kind) {
        if (!usesLowLatency) {
            return false;
        }
        switch (kind) {
        case EncodeFailureKind::Malfunction:
        case EncodeFailureKind::AsyncEncodeError:
            return true;
        case EncodeFailureKind::SessionInvalid:
            return _invalidInARow.fetch_add(1) + 1 >= kMaxInvalidSessionsInARow;
        }
        return false;
    }

    void frameWasEncoded() {
        _invalidInARow.store(0);
    }

private:
    std::atomic<int> _invalidInARow{0};
};

inline EncoderMode chooseEncoderMode(bool switchEnabled, bool osSupportsLowLatency) {
    EncoderMode mode;
    if (!switchEnabled) {
        mode.reason = "switch-off";
        return mode;
    }
    if (!osSupportsLowLatency) {
        mode.reason = "os";
        return mode;
    }
    if (EncoderLowLatencyLatch::failed()) {
        mode.reason = "failed-earlier";
        return mode;
    }
    mode.lowLatency = true;
    mode.realTime = true;
    mode.reason = "ok";
    return mode;
}

} // namespace tgcalls

#endif
