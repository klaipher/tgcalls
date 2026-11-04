#ifndef TGCALLS_CALL_AUDIO_POLICY_H
#define TGCALLS_CALL_AUDIO_POLICY_H

// Local audio policy shared by 1:1 and group calls. Nothing here changes what is
// negotiated or sent on the wire: Opus stays 48 kHz fullband mono with the same
// ptime and FEC. Everything only decides how the local capture chain and the local
// Opus encoder spend CPU. All switches default to today's behaviour.

#include <atomic>
#include <memory>
#include <optional>
#include <vector>

#include "absl/strings/match.h"

#ifdef __APPLE__
#include <TargetConditionals.h>
#endif

#include "api/audio_codecs/audio_encoder_factory.h"
#include "api/audio_codecs/audio_format.h"
#include "api/audio_codecs/opus/audio_encoder_opus.h"
#include "api/audio_codecs/opus/audio_encoder_opus_config.h"
#include "modules/audio_processing/include/audio_processing.h"
#include "api/make_ref_counted.h"

namespace tgcalls {
namespace audiopolicy {

// A8. On macOS the input device's own mute control is not toggled: the software mute
// (SetAudioSend / Enable) already silences the stream, and a hardware mute is global to
// the device, so a crash or a USB headset could leave the microphone muted for every app.
inline bool shouldUseHardwareMicrophoneMute() {
#if defined(__APPLE__) && TARGET_OS_OSX
    return false;
#else
    return true;
#endif
}

// A3. Group calls can run WebRTC's noise suppressor, RNNoise or both.
enum class GroupNoiseMode : int {
    Both = 0,
    ApmOnly = 1,
    RNNoiseOnly = 2
};

inline GroupNoiseMode groupNoiseModeFromInt(int value) {
    switch (value) {
        case 1: return GroupNoiseMode::ApmOnly;
        case 2: return GroupNoiseMode::RNNoiseOnly;
        default: return GroupNoiseMode::Both;
    }
}

struct GroupNoiseResolution {
    bool apmNoiseSuppression = false;
    bool rnnoise = false;
};

// `userEnabled` is the in-call noise suppression toggle. `apmRequested` is what the call
// would use without this policy. With the toggle off the APM suppressor is never removed,
// so turning suppression off never leaves a noisy mic worse than today.
inline GroupNoiseResolution resolveGroupNoise(GroupNoiseMode mode, bool userEnabled, bool apmRequested) {
    GroupNoiseResolution result;
    switch (mode) {
        case GroupNoiseMode::Both:
            result.apmNoiseSuppression = apmRequested;
            result.rnnoise = userEnabled;
            break;
        case GroupNoiseMode::ApmOnly:
            result.apmNoiseSuppression = apmRequested;
            result.rnnoise = false;
            break;
        case GroupNoiseMode::RNNoiseOnly:
            result.apmNoiseSuppression = apmRequested && !userEnabled;
            result.rnnoise = userEnabled;
            break;
    }
    return result;
}

// A5. Legacy: AGC1 adaptive-analog, which moves the system microphone slider.
// DigitalAgc2: AGC2 adaptive digital gain only; the system volume is left alone.
enum class MicGainMode : int {
    Legacy = 0,
    DigitalAgc2 = 1
};

inline MicGainMode micGainModeFromInt(int value) {
    return value == 1 ? MicGainMode::DigitalAgc2 : MicGainMode::Legacy;
}

inline bool analogAgcEnabled(MicGainMode mode) {
    return mode == MicGainMode::Legacy;
}

// A6. Opus voice in mono encodes fullband (up to 20 kHz) from about 14 kbps, so the 16-20 kHz
// band reaches the listener at the 32 kbps both 1:1 and group calls use. Only at 12 kbps or
// less does nothing above 16 kHz survive encoding; only then can the capture chain run at
// 32 kHz internally instead of 48 kHz without a quality loss.
constexpr int kLowBitrateRateMaxKbps = 12;

inline int maximumInternalProcessingRate(bool lowBitrateRateEnabled, int opusBitrateKbps) {
    if (lowBitrateRateEnabled && opusBitrateKbps > 0 && opusBitrateKbps <= kLowBitrateRateMaxKbps) {
        return 32000;
    }
    return 48000;
}

struct ApmPolicy {
    MicGainMode micGain = MicGainMode::Legacy;
    bool lowBitrateRate = false;
    int opusBitrateKbps = 32;

    bool changesConfig() const {
        return micGain != MicGainMode::Legacy || maximumInternalProcessingRate(lowBitrateRate, opusBitrateKbps) != 48000;
    }
};

inline void applyApmPolicy(webrtc::AudioProcessing::Config &config, const ApmPolicy &policy) {
    if (policy.micGain == MicGainMode::DigitalAgc2) {
        config.gain_controller2.enabled = true;
        config.gain_controller2.adaptive_digital.enabled = true;
        config.gain_controller2.input_volume_controller.enabled = false;
    }
    const int rate = maximumInternalProcessingRate(policy.lowBitrateRate, policy.opusBitrateKbps);
    if (rate != 48000) {
        config.pipeline.maximum_internal_processing_rate = rate;
    }
}

// A7a. Opus encoder complexity. Local only; the decoder and the SDP are unaffected.
inline int clampOpusComplexity(int value) {
    return (value >= 5 && value <= 10) ? value : 0;
}

inline std::optional<webrtc::AudioEncoderOpusConfig> makeOpusConfig(const webrtc::SdpAudioFormat &format, int complexity) {
    auto parsed = webrtc::AudioEncoderOpus::SdpToConfig(format);
    if (!parsed) {
        return std::nullopt;
    }
    webrtc::AudioEncoderOpusConfig config = *parsed;
    const int clamped = clampOpusComplexity(complexity);
    if (clamped != 0) {
        // Only the normal complexity. low_rate_complexity applies below ~12.5 kbps (bad networks),
        // where lowering it would cost voice quality when it is already tight.
        config.complexity = clamped;
    }
    return config;
}

class OpusComplexityEncoderFactory : public webrtc::AudioEncoderFactory {
public:
    OpusComplexityEncoderFactory(rtc::scoped_refptr<webrtc::AudioEncoderFactory> inner, int complexity) :
    _inner(std::move(inner)),
    _complexity(complexity) {
    }

    std::vector<webrtc::AudioCodecSpec> GetSupportedEncoders() override {
        return _inner->GetSupportedEncoders();
    }

    absl::optional<webrtc::AudioCodecInfo> QueryAudioEncoder(const webrtc::SdpAudioFormat &format) override {
        return _inner->QueryAudioEncoder(format);
    }

    std::unique_ptr<webrtc::AudioEncoder> MakeAudioEncoder(int payloadType, const webrtc::SdpAudioFormat &format, absl::optional<webrtc::AudioCodecPairId> codecPairId) override {
        if (absl::EqualsIgnoreCase(format.name, "opus")) {
            const auto config = makeOpusConfig(format, _complexity);
            if (config && config->IsOk()) {
                auto encoder = webrtc::AudioEncoderOpus::MakeAudioEncoder(*config, payloadType, codecPairId);
                if (encoder) {
                    return encoder;
                }
            }
        }
        // Any problem falls back to the untouched factory.
        return _inner->MakeAudioEncoder(payloadType, format, codecPairId);
    }

private:
    rtc::scoped_refptr<webrtc::AudioEncoderFactory> _inner;
    int _complexity = 0;
};

inline rtc::scoped_refptr<webrtc::AudioEncoderFactory> wrapOpusEncoderFactory(rtc::scoped_refptr<webrtc::AudioEncoderFactory> inner, int complexity) {
    const int clamped = clampOpusComplexity(complexity);
    if (clamped == 0) {
        return inner;
    }
    return rtc::make_ref_counted<OpusComplexityEncoderFactory>(std::move(inner), clamped);
}

// Switch values, pushed from the app's Debug settings (TGCallsRuntimeOptionsBridge).
// Read when a call starts; they never change what is negotiated.
struct RuntimeAudioOptions {
    static std::atomic<int> &groupNoiseMode() {
        static std::atomic<int> value{0};
        return value;
    }

    static std::atomic<int> &micGainMode() {
        static std::atomic<int> value{0};
        return value;
    }

    static std::atomic<int> &opusComplexity() {
        static std::atomic<int> value{0};
        return value;
    }

    static std::atomic<bool> &lowBitrateApmRate() {
        static std::atomic<bool> value{false};
        return value;
    }
};

inline GroupNoiseMode currentGroupNoiseMode() {
    return groupNoiseModeFromInt(RuntimeAudioOptions::groupNoiseMode().load());
}

inline MicGainMode currentMicGainMode() {
    return micGainModeFromInt(RuntimeAudioOptions::micGainMode().load());
}

inline ApmPolicy currentApmPolicy(int opusBitrateKbps) {
    ApmPolicy policy;
    policy.micGain = currentMicGainMode();
    policy.lowBitrateRate = RuntimeAudioOptions::lowBitrateApmRate().load();
    policy.opusBitrateKbps = opusBitrateKbps;
    return policy;
}

}
}

#endif
