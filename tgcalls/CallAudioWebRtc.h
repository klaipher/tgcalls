#ifndef TGCALLS_CALL_AUDIO_WEBRTC_H
#define TGCALLS_CALL_AUDIO_WEBRTC_H

// The part of the audio policy that needs WebRTC types. The pure decisions are in AudioCallPolicy.h so
// the host tests of the iOS Bazel build do not link libtg_owt.

#include "AudioCallPolicy.h"

#include "modules/audio_processing/include/audio_processing.h"

namespace tgcalls {
namespace audiopolicy {

// macOS: AGC2 adaptive digital gain, no input volume controller, so the system microphone slider is never
// moved. Returns true when the config was changed (macOS only; a no-op on iOS).
inline bool applyDigitalMicGain(webrtc::AudioProcessing::Config &config) {
    if (!usesDigitalMicGain()) {
        return false;
    }
    config.gain_controller2.enabled = true;
    config.gain_controller2.adaptive_digital.enabled = true;
    config.gain_controller2.input_volume_controller.enabled = false;
    return true;
}

} // namespace audiopolicy
} // namespace tgcalls

#endif
