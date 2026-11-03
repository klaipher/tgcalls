#ifndef TGCALLS_AUDIO_CALL_POLICY_H
#define TGCALLS_AUDIO_CALL_POLICY_H

#include <atomic>
#include <string>

namespace tgcalls {

#if defined(WEBRTC_IOS)
constexpr bool kAudioCallPolicyIsIosBuild = true;
#else
constexpr bool kAudioCallPolicyIsIosBuild = false;
#endif

// Decisions behind the local audio changes. Everything here is about this device only: nothing
// reaches the wire, no codec, SDP or signalling value depends on it. Pure code (no WebRTC, no
// Apple headers) so the decisions are tested on the host.

// --- Who suppresses noise on the microphone signal --------------------------------------------

struct CaptureProcessingChoice {
    // cricket::AudioOptions::noise_suppression of the outgoing audio channel.
    bool softwareNoiseSuppression = true;
    // True: set AudioOptions::highpass_filter = false. False: leave it unset (WebRTC's default).
    bool disableHighpassFilter = false;
    // Stable token for the log line.
    const char *reason = "switch-off";
};

// Remembers, for the process, that the system voice-processing unit could not be used (it failed
// to initialise, or bypass was requested). Calls created afterwards keep the software processing.
class SystemVoiceProcessingLatch {
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

// On iOS the call's audio unit is VoiceProcessingIO, which already runs echo cancellation, noise
// suppression and gain control in the system's DSP. WebRTC's own noise suppression (and its
// high-pass filter) then process an already cleaned signal a second time. When the switch is on
// and the system unit is expected to be active, the software stage is dropped; in every other
// case (RemoteIO, bypass, an earlier failure, macOS) today's options stay.
inline CaptureProcessingChoice chooseCaptureProcessing(bool switchOn, bool isIos, bool systemVoiceProcessingExpected, bool systemVoiceProcessingFailedEarlier) {
    CaptureProcessingChoice choice;
    if (!switchOn) {
        choice.reason = "switch-off";
    } else if (!isIos) {
        choice.reason = "platform";
    } else if (!systemVoiceProcessingExpected) {
        choice.reason = "no-system-voice-processing";
    } else if (systemVoiceProcessingFailedEarlier) {
        choice.reason = "failed-earlier";
    } else {
        choice.softwareNoiseSuppression = false;
        choice.disableHighpassFilter = true;
        choice.reason = "system";
    }
    return choice;
}

// --- Opus first-loss floor --------------------------------------------------------------------

// WebRTC's Opus encoder starts with an expected packet loss of 0 and only learns the real figure
// from receiver reports, so the first seconds of a call on a lossy link have no FEC redundancy.
// The field trial gives it a 1% floor. Encoder-local: the in-band FEC parameter that is
// negotiated stays as it is. The v1 and group engines already enable it.
inline std::string withOpusFirstLossFloor(std::string trials, bool enabled) {
    static const char *const kTrial = "WebRTC-Audio-OpusMinPacketLossRate/Enabled-1/";
    if (!enabled || trials.find(kTrial) != std::string::npos) {
        return trials;
    }
    if (!trials.empty() && trials.back() != '/') {
        trials += '/';
    }
    trials += kTrial;
    return trials;
}

// --- Audio session mode -----------------------------------------------------------------------

// The engine's audio-device start applies its own session configuration (mode voiceChat), which
// replaces the mode the app's session manager chose for the call (videoChat for a video call).
// With the switch on, the engine's default voiceChat no longer replaces a call mode that is
// already set. A mode the caller asked for explicitly is always applied.
inline std::string chooseAudioSessionMode(const std::string &configured, const std::string &current, bool keepCurrentCallMode) {
    static const char *const kVoiceChat = "AVAudioSessionModeVoiceChat";
    static const char *const kVideoChat = "AVAudioSessionModeVideoChat";
    if (keepCurrentCallMode && configured == kVoiceChat && current == kVideoChat) {
        return current;
    }
    return configured;
}

// --- Other-audio ducking of the voice-processing unit -----------------------------------------

// Values of AUVoiceIOOtherAudioDuckingLevel.
enum class OtherAudioDuckingLevel : int {
    Default = 0,
    Min = 10,
    Mid = 20,
    Max = 30
};

struct OtherAudioDuckingPlan {
    bool apply = false;
    bool advanced = false;
    OtherAudioDuckingLevel level = OtherAudioDuckingLevel::Default;
    const char *reason = "switch-off";
};

// iOS 17 lets the voice-processing unit duck other audio only while someone is speaking, and by a
// chosen amount, instead of ducking it for the whole call. Music or a video playing next to a call
// then stays at a normal level between sentences.
inline OtherAudioDuckingPlan chooseOtherAudioDucking(bool switchOn, bool osSupportsAdvancedDucking) {
    OtherAudioDuckingPlan plan;
    if (!switchOn) {
        plan.reason = "switch-off";
    } else if (!osSupportsAdvancedDucking) {
        plan.reason = "os";
    } else {
        plan.apply = true;
        plan.advanced = true;
        plan.level = OtherAudioDuckingLevel::Min;
        plan.reason = "ok";
    }
    return plan;
}

} // namespace tgcalls

#endif
