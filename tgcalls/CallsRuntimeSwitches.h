#ifndef TGCALLS_CALLS_RUNTIME_SWITCHES_H
#define TGCALLS_CALLS_RUNTIME_SWITCHES_H

#include <atomic>

namespace tgcalls {

// Process-wide, local-only switches for behaviour changes that are not yet validated on a device. They never
// reach the wire: nothing here may change negotiation, signalling or the codec lists offered to a peer.
//
// The names and meaning are the UNIFIED ones shared with the iOS fork (telegram-ios-dev), where this class
// already exists with more switches. When the two tgcalls branches are merged the iOS file is the base and
// the Mac-only defaults below become platform defaults. Until then this Mac file only carries the switches
// the Mac fixes need; the older Mac switches stay in platform/darwin/TGCallsRuntimeOptions.h.
//
// Defaults are the behaviour of the platform before the switch existed: both of these were always on in the
// Mac build, so they default ON here (the iOS defaults are OFF).
class CallsRuntimeSwitches {
public:
    // VideoToolbox H.264 encoder in low-latency rate-control mode. Kill switch; also latched off for the
    // process by EncoderLowLatencyLatch when a low-latency session fails.
    static bool encoderLowLatency() {
        return flag(EncoderLowLatency).load(std::memory_order_relaxed);
    }
    static void setEncoderLowLatency(bool value) {
        flag(EncoderLowLatency).store(value, std::memory_order_relaxed);
    }

    // Group calls on 8/16 kHz capture (Bluetooth headsets) keep the signal that went through RNNoise and the
    // external-audio mix instead of discarding it.
    static bool groupNarrowbandProcessing() {
        return flag(GroupNarrowbandProcessing).load(std::memory_order_relaxed);
    }
    static void setGroupNarrowbandProcessing(bool value) {
        flag(GroupNarrowbandProcessing).store(value, std::memory_order_relaxed);
    }

private:
    enum Flag {
        EncoderLowLatency,
        GroupNarrowbandProcessing,
        FlagCount
    };

    static std::atomic<bool> &flag(Flag which) {
        static std::atomic<bool> values[FlagCount] = {{true}, {true}};
        return values[which];
    }
};

} // namespace tgcalls

#endif
