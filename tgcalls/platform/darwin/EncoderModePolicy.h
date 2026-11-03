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
