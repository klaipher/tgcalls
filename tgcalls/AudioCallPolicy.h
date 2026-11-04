#ifndef TGCALLS_AUDIO_CALL_POLICY_H
#define TGCALLS_AUDIO_CALL_POLICY_H

#include <atomic>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <vector>

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
        return state().failed.load();
    }

    // Sets the latch and tells every registered observer, once per failure (the first call after a reset).
    static void markFailed() {
        std::vector<std::function<void()>> toTell;
        {
            std::lock_guard<std::mutex> lock(state().mutex);
            if (state().failed.exchange(true)) {
                return;
            }
            for (const auto &entry : state().observers) {
                toTell.push_back(entry.second);
            }
        }
        for (const auto &observer : toTell) {
            observer();
        }
    }

    static void reset() {
        state().failed.store(false);
    }

    // A call that dropped the software noise suppression relies on the system unit; this lets it take the
    // software stage back on the live channel when the unit fails later. Runs on the thread that reports
    // the failure (an audio-unit thread), so the observer must only post work. If the unit has failed
    // already, the observer is told at once. Returns an id for removeObserver.
    static int addObserver(std::function<void()> observer) {
        bool tellNow = false;
        int id = 0;
        {
            std::lock_guard<std::mutex> lock(state().mutex);
            id = ++state().nextId;
            state().observers[id] = observer;
            tellNow = state().failed.load();
        }
        if (tellNow) {
            observer();
        }
        return id;
    }

    static void removeObserver(int id) {
        std::lock_guard<std::mutex> lock(state().mutex);
        state().observers.erase(id);
    }

private:
    struct State {
        std::atomic<bool> failed{false};
        std::mutex mutex;
        std::map<int, std::function<void()>> observers;
        int nextId = 0;
    };

    static State &state() {
        static State value;
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
