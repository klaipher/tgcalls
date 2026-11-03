#ifndef TGCALLS_CALLS_RUNTIME_SWITCHES_H
#define TGCALLS_CALLS_RUNTIME_SWITCHES_H

#include <atomic>

namespace tgcalls {

// Process-wide, local-only switches for behaviour changes that are not yet validated on a
// device. They never reach the wire: nothing here may change negotiation, signalling or the
// codec lists offered to a peer. The app sets them from its Debug Settings before a call
// starts (and again when the setting changes); every default is the behaviour tgcalls had
// before the switch existed.
class CallsRuntimeSwitches {
public:
    // Livestream (broadcast) video decoded with VideoToolbox instead of FFmpeg's software
    // decoders. Falls back to software per stream on any hardware error.
    static bool livestreamHardwareDecoding() {
        return flag(LivestreamHardwareDecoding).load(std::memory_order_relaxed);
    }
    static void setLivestreamHardwareDecoding(bool value) {
        flag(LivestreamHardwareDecoding).store(value, std::memory_order_relaxed);
    }

    // Camera frames travel from the capturer to the encoder as NV12 CVPixelBuffers instead of
    // being converted to I420 on the CPU first.
    static bool nativeCapturePath() {
        return flag(NativeCapturePath).load(std::memory_order_relaxed);
    }
    static void setNativeCapturePath(bool value) {
        flag(NativeCapturePath).store(value, std::memory_order_relaxed);
    }

    // VideoToolbox H.264 encoder in low-latency rate-control mode (iOS 14.5+).
    static bool encoderLowLatency() {
        return flag(EncoderLowLatency).load(std::memory_order_relaxed);
    }
    static void setEncoderLowLatency(bool value) {
        flag(EncoderLowLatency).store(value, std::memory_order_relaxed);
    }

private:
    enum Flag {
        LivestreamHardwareDecoding,
        NativeCapturePath,
        EncoderLowLatency,
        FlagCount
    };

    static std::atomic<bool> &flag(Flag which) {
        static std::atomic<bool> values[FlagCount] = {};
        return values[which];
    }
};

} // namespace tgcalls

#endif
