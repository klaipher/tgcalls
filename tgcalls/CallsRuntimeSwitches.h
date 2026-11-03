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

    // Audio. Software noise suppression and the high-pass filter are dropped from the outgoing
    // audio options when the iOS voice-processing unit already does that work. Default off.
    static bool systemNoiseSuppression() {
        return flag(SystemNoiseSuppression).load(std::memory_order_relaxed);
    }
    static void setSystemNoiseSuppression(bool value) {
        flag(SystemNoiseSuppression).store(value, std::memory_order_relaxed);
    }

    // The voice-processing unit ducks other audio only while speech is active, and by the smallest
    // amount (iOS 17+). Local audio-unit configuration. Default on.
    static bool advancedDucking() {
        return !flag(AdvancedDuckingDisabled).load(std::memory_order_relaxed);
    }
    static void setAdvancedDucking(bool value) {
        flag(AdvancedDuckingDisabled).store(!value, std::memory_order_relaxed);
    }

    // The v2 1:1 Opus encoder starts with a 1% expected-loss floor, as the group and v1 engines
    // already do. Encoder-local; the negotiated FEC parameter is untouched. Default on.
    static bool opusFirstLossFloor() {
        return !flag(OpusFirstLossFloorDisabled).load(std::memory_order_relaxed);
    }
    static void setOpusFirstLossFloor(bool value) {
        flag(OpusFirstLossFloorDisabled).store(!value, std::memory_order_relaxed);
    }

    // Starting the audio device no longer replaces the videoChat session mode of a video call with
    // voiceChat. Default off.
    static bool keepSessionMode() {
        return flag(KeepSessionMode).load(std::memory_order_relaxed);
    }
    static void setKeepSessionMode(bool value) {
        flag(KeepSessionMode).store(value, std::memory_order_relaxed);
    }

    // Engine threads (network, media, worker) run at user-initiated quality of service. Read when
    // the threads are created, so a change applies from the next app launch. Default off.
    static bool threadQoS() {
        return flag(ThreadQoS).load(std::memory_order_relaxed);
    }
    static void setThreadQoS(bool value) {
        flag(ThreadQoS).store(value, std::memory_order_relaxed);
    }

    // The app's thermal level (0 nominal ... 3 critical) lowers the size and frame rate of the
    // outgoing video through the local output-format request. Default off.
    static bool thermalVideoLadder() {
        return flag(ThermalVideoLadder).load(std::memory_order_relaxed);
    }
    static void setThermalVideoLadder(bool value) {
        flag(ThermalVideoLadder).store(value, std::memory_order_relaxed);
    }
    static int thermalLevel() {
        return thermalLevelValue().load(std::memory_order_relaxed);
    }
    static void setThermalLevel(int value) {
        thermalLevelValue().store(value, std::memory_order_relaxed);
    }

    // Incoming 1:1 video frames are dropped before they reach the app while it is in the background
    // without Picture in Picture. Default off.
    static bool backgroundVideoDrop() {
        return flag(BackgroundVideoDrop).load(std::memory_order_relaxed);
    }
    static void setBackgroundVideoDrop(bool value) {
        flag(BackgroundVideoDrop).store(value, std::memory_order_relaxed);
    }
    // Runtime state set by the app (see shouldDropRemoteVideo); only read while the switch is on.
    static bool incomingVideoDropped() {
        return flag(IncomingVideoDropped).load(std::memory_order_relaxed);
    }
    static void setIncomingVideoDropped(bool value) {
        flag(IncomingVideoDropped).store(value, std::memory_order_relaxed);
    }

private:
    static std::atomic<int> &thermalLevelValue() {
        static std::atomic<int> value{0};
        return value;
    }

    // Every flag is false at process start; the ones that default to "on" are stored inverted.
    enum Flag {
        LivestreamHardwareDecoding,
        NativeCapturePath,
        EncoderLowLatency,
        SystemNoiseSuppression,
        AdvancedDuckingDisabled,
        OpusFirstLossFloorDisabled,
        KeepSessionMode,
        ThreadQoS,
        ThermalVideoLadder,
        BackgroundVideoDrop,
        IncomingVideoDropped,
        FlagCount
    };

    static std::atomic<bool> &flag(Flag which) {
        static std::atomic<bool> values[FlagCount] = {};
        return values[which];
    }
};

} // namespace tgcalls

#endif
