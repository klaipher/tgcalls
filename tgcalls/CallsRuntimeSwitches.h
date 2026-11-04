#ifndef TGCALLS_CALLS_RUNTIME_SWITCHES_H
#define TGCALLS_CALLS_RUNTIME_SWITCHES_H

#include <atomic>

namespace tgcalls {

// Process-wide, local-only switches for behaviour changes that still need a measurement on a device.
// They never reach the wire: nothing here may change negotiation, signalling or the codec lists
// offered to a peer. Both apps (iOS and macOS) set the same switches from their Debug Settings
// section "Streams & Calls" before a call starts (and again when a setting changes).
//
// Switch policy: a change that is a bug fix or a low-risk win has NO switch here (it is always on and
// guards itself with an internal latch or fallback). What remains is either risky or needs an energy /
// quality measurement. Every default below is OFF, which is the behaviour tgcalls had before the switch
// existed. Each switch carries a "remove after measuring" note: once the device measurement says it is
// safe and useful, delete the switch and make the behaviour unconditional.
//
// Removed by the owner as experiments without a clear benefit (the behaviour from before stays): thread
// priorities (QoS), the 32 kHz processing rate, the Opus complexity, the macOS audio workgroup and keeping
// the videoChat audio session mode. Do not bring them back as switches.
//
// Runtime STATE the app reports (thermal level, incoming video dropped, livestream suspended) is kept here
// too, but it is not a user-facing switch.
class CallsRuntimeSwitches {
public:
    // Livestream (broadcast) video decoded with VideoToolbox instead of FFmpeg's software decoders.
    // Falls back to software per stream on any hardware error.
    // Remove after measuring: battery and CPU of a long livestream on a device, hardware vs software.
    static bool livestreamHardwareDecoding() {
        return flag(LivestreamHardwareDecoding).load(std::memory_order_relaxed);
    }
    static void setLivestreamHardwareDecoding(bool value) {
        flag(LivestreamHardwareDecoding).store(value, std::memory_order_relaxed);
    }

    // iOS: camera frames travel from the capturer to the encoder as NV12 CVPixelBuffers instead of being
    // converted to I420 on the CPU first.
    // Remove after measuring: CPU and picture quality of a video call on an iPhone.
    static bool nativeCapturePath() {
        return flag(NativeCapturePath).load(std::memory_order_relaxed);
    }
    static void setNativeCapturePath(bool value) {
        flag(NativeCapturePath).store(value, std::memory_order_relaxed);
    }

    // Audio, iOS. Software noise suppression and the high-pass filter are dropped from the outgoing audio
    // options when the voice-processing unit already does that work.
    // Remove after measuring: A/B of voice quality with the system unit alone.
    static bool systemNoiseSuppression() {
        return flag(SystemNoiseSuppression).load(std::memory_order_relaxed);
    }
    static void setSystemNoiseSuppression(bool value) {
        flag(SystemNoiseSuppression).store(value, std::memory_order_relaxed);
    }

    // iOS 17+: the voice-processing unit ducks other audio only while speech is active, and by the smallest
    // amount. With other audio playing near full level the echo canceller has no reference for it, so it may
    // reach the peer.
    // Remove after measuring: A/B with music playing next to a call.
    static bool advancedDucking() {
        return flag(AdvancedDucking).load(std::memory_order_relaxed);
    }
    static void setAdvancedDucking(bool value) {
        flag(AdvancedDucking).store(value, std::memory_order_relaxed);
    }

    // The thermal level (0 nominal ... 3 critical) set by the app lowers the size and frame rate of the
    // outgoing camera video (the UI part of the thermal / Low Power response is always on in the apps).
    // Remove after measuring: a thermally throttled call on a device.
    static bool thermalVideoLadder() {
        return flag(ThermalVideoLadder).load(std::memory_order_relaxed);
    }
    static void setThermalVideoLadder(bool value) {
        flag(ThermalVideoLadder).store(value, std::memory_order_relaxed);
    }

    // macOS: the camera frame is cropped and scaled with a VTPixelTransferSession instead of on the CPU.
    // Remove after measuring: CPU of a 720p call and the picture after the crop.
    static bool hardwareCrop() {
        return flag(HardwareCrop).load(std::memory_order_relaxed);
    }
    static void setHardwareCrop(bool value) {
        flag(HardwareCrop).store(value, std::memory_order_relaxed);
    }

    // macOS: VideoToolbox decoders output the stream's own video-range NV12 (and Metal) instead of forcing
    // full range with OpenGL compatibility. The iOS shaders assume full range, so it is not exposed on iOS.
    // Remove after measuring: colours of H.264 and H.265 calls against the sender's picture.
    static bool nativeDecodeFormat() {
        return flag(NativeDecodeFormat).load(std::memory_order_relaxed);
    }
    static void setNativeDecodeFormat(bool value) {
        flag(NativeDecodeFormat).store(value, std::memory_order_relaxed);
    }

    // macOS: screen sharing in a group call encodes with the hardware H.264 encoder instead of OpenH264.
    // Remove after measuring: CPU and sharpness of text while sharing a screen.
    static bool screencastHardwareEncoder() {
        return flag(ScreencastHardwareEncoder).load(std::memory_order_relaxed);
    }
    static void setScreencastHardwareEncoder(bool value) {
        flag(ScreencastHardwareEncoder).store(value, std::memory_order_relaxed);
    }

    // Group calls: which noise suppressors run (0 both, 1 WebRTC only, 2 RNNoise only, see AudioCallPolicy.h).
    // Default 0 = both, the behaviour before the switch.
    // Remove after measuring: CPU and voice quality of the three modes in a busy group call.
    static int groupNoiseMode() {
        return intValue(GroupNoiseMode).load(std::memory_order_relaxed);
    }
    static void setGroupNoiseMode(int value) {
        intValue(GroupNoiseMode).store(value, std::memory_order_relaxed);
    }

    // --- State reported by the app (not switches) -------------------------------------------------------

    // 0 nominal, 1 fair, 2 serious, 3 critical. Only read while thermalVideoLadder() is on.
    static int thermalLevel() {
        return intValue(ThermalLevel).load(std::memory_order_relaxed);
    }
    static void setThermalLevel(int value) {
        intValue(ThermalLevel).store(value, std::memory_order_relaxed);
    }

    // The app drops incoming 1:1 video frames before they reach the UI while it is in the background
    // without Picture in Picture (always on; this is the state it reports, see shouldDropRemoteVideo).
    static bool incomingVideoDropped() {
        return flag(IncomingVideoDropped).load(std::memory_order_relaxed);
    }
    static void setIncomingVideoDropped(bool value) {
        flag(IncomingVideoDropped).store(value, std::memory_order_relaxed);
    }

    // The livestream is not on screen (window hidden, tile off screen): video is not decoded, audio goes on.
    // Always honoured; the app only reports the state.
    static bool livestreamVideoSuspended() {
        return flag(LivestreamVideoSuspended).load(std::memory_order_relaxed);
    }
    static void setLivestreamVideoSuspended(bool value) {
        flag(LivestreamVideoSuspended).store(value, std::memory_order_relaxed);
    }

private:
    enum Flag {
        LivestreamHardwareDecoding,
        NativeCapturePath,
        SystemNoiseSuppression,
        AdvancedDucking,
        ThermalVideoLadder,
        HardwareCrop,
        NativeDecodeFormat,
        ScreencastHardwareEncoder,
        IncomingVideoDropped,
        LivestreamVideoSuspended,
        FlagCount
    };

    enum IntValue {
        GroupNoiseMode,
        ThermalLevel,
        IntValueCount
    };

    // Every value is false / 0 at process start, which is the default of every switch.
    static std::atomic<bool> &flag(Flag which) {
        static std::atomic<bool> values[FlagCount] = {};
        return values[which];
    }

    static std::atomic<int> &intValue(IntValue which) {
        static std::atomic<int> values[IntValueCount] = {};
        return values[which];
    }
};

} // namespace tgcalls

#endif
