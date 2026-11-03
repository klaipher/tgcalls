#include <cstdio>
#include <memory>
#include <string>

#include "CallAudioPolicy.h"
#include "api/audio_codecs/audio_format.h"
#include "api/audio_codecs/audio_encoder_factory_template.h"
#include "api/audio_codecs/L16/audio_encoder_L16.h"
#include "api/audio_codecs/opus/audio_encoder_opus.h"
#include "modules/audio_processing/include/audio_processing.h"

static int failures = 0;

static void expect(bool condition, const char *name) {
    fprintf(stderr, "%s: %s\n", condition ? "ok" : "FAIL", name);
    if (!condition) {
        failures++;
    }
}

using namespace tgcalls::audiopolicy;

static void testHardwareMute() {
    // A8: macOS never toggles the device-level input mute; the software mute already silences the send path.
    expect(!shouldUseHardwareMicrophoneMute(), "macOS does not use the hardware microphone mute");
}

static void testNoiseModeParsing() {
    expect(groupNoiseModeFromInt(0) == GroupNoiseMode::Both, "0 is both");
    expect(groupNoiseModeFromInt(1) == GroupNoiseMode::ApmOnly, "1 is apm only");
    expect(groupNoiseModeFromInt(2) == GroupNoiseMode::RNNoiseOnly, "2 is rnnoise only");
    expect(groupNoiseModeFromInt(-1) == GroupNoiseMode::Both, "negative falls back to both");
    expect(groupNoiseModeFromInt(99) == GroupNoiseMode::Both, "unknown falls back to both");
}

static void testNoiseResolution() {
    // Both (today's behaviour): APM NS stays on, RNNoise follows the user toggle.
    auto both = resolveGroupNoise(GroupNoiseMode::Both, true, true);
    expect(both.apmNoiseSuppression && both.rnnoise, "both, user on: both run");
    both = resolveGroupNoise(GroupNoiseMode::Both, false, true);
    expect(both.apmNoiseSuppression && !both.rnnoise, "both, user off: APM only (a598e27 behaviour)");
    both = resolveGroupNoise(GroupNoiseMode::Both, true, false);
    expect(!both.apmNoiseSuppression && both.rnnoise, "both, APM not requested: stays off");

    auto apm = resolveGroupNoise(GroupNoiseMode::ApmOnly, true, true);
    expect(apm.apmNoiseSuppression && !apm.rnnoise, "apm only, user on: RNNoise skipped");
    apm = resolveGroupNoise(GroupNoiseMode::ApmOnly, false, true);
    expect(apm.apmNoiseSuppression && !apm.rnnoise, "apm only, user off: APM only");

    auto rn = resolveGroupNoise(GroupNoiseMode::RNNoiseOnly, true, true);
    expect(!rn.apmNoiseSuppression && rn.rnnoise, "rnnoise only, user on: APM NS off");
    rn = resolveGroupNoise(GroupNoiseMode::RNNoiseOnly, false, true);
    expect(rn.apmNoiseSuppression && !rn.rnnoise, "rnnoise only, user off: APM NS stays so audio is not left unsuppressed");
    rn = resolveGroupNoise(GroupNoiseMode::RNNoiseOnly, true, false);
    expect(!rn.apmNoiseSuppression && rn.rnnoise, "rnnoise only, APM not requested: stays off");
}

static void testMicGain() {
    expect(micGainModeFromInt(0) == MicGainMode::Legacy, "0 is legacy");
    expect(micGainModeFromInt(1) == MicGainMode::DigitalAgc2, "1 is digital AGC2");
    expect(micGainModeFromInt(7) == MicGainMode::Legacy, "unknown is legacy");
    expect(analogAgcEnabled(MicGainMode::Legacy), "legacy keeps AGC1 analog");
    expect(!analogAgcEnabled(MicGainMode::DigitalAgc2), "AGC2 mode turns AGC1 analog off");

    webrtc::AudioProcessing::Config config;
    const auto before = config;
    ApmPolicy policy;
    expect(!policy.changesConfig(), "default policy changes nothing");
    applyApmPolicy(config, policy);
    expect(config.gain_controller2.enabled == before.gain_controller2.enabled, "default policy leaves AGC2 alone");
    expect(config.pipeline.maximum_internal_processing_rate == before.pipeline.maximum_internal_processing_rate, "default policy leaves the rate alone");

    policy.micGain = MicGainMode::DigitalAgc2;
    expect(policy.changesConfig(), "AGC2 policy changes the config");
    applyApmPolicy(config, policy);
    expect(config.gain_controller2.enabled, "AGC2 enabled");
    expect(config.gain_controller2.adaptive_digital.enabled, "adaptive digital enabled");
    expect(!config.gain_controller2.input_volume_controller.enabled, "no analog input volume control, so the system mic slider does not move");
    expect(!config.gain_controller1.enabled, "AGC1 not enabled by the policy");
}

static void testApmRate() {
    expect(maximumInternalProcessingRate(false, 32) == 48000, "switch off keeps 48 kHz");
    expect(maximumInternalProcessingRate(true, 32) == 32000, "32 kbps with the switch on uses 32 kHz");
    expect(maximumInternalProcessingRate(true, 40) == 32000, "40 kbps is still low");
    expect(maximumInternalProcessingRate(true, 41) == 48000, "41 kbps keeps 48 kHz");
    expect(maximumInternalProcessingRate(true, 0) == 48000, "unknown bitrate keeps 48 kHz");

    webrtc::AudioProcessing::Config config;
    ApmPolicy policy;
    policy.lowBitrateRate = true;
    policy.opusBitrateKbps = 32;
    expect(policy.changesConfig(), "rate policy changes the config");
    applyApmPolicy(config, policy);
    expect(config.pipeline.maximum_internal_processing_rate == 32000, "config rate lowered");
    policy.opusBitrateKbps = 64;
    config = webrtc::AudioProcessing::Config();
    applyApmPolicy(config, policy);
    expect(config.pipeline.maximum_internal_processing_rate == 48000, "high bitrate keeps the rate");
}

static void testApmBuilderRoundTrip() {
    webrtc::AudioProcessing::Config config;
    ApmPolicy policy;
    policy.micGain = MicGainMode::DigitalAgc2;
    policy.lowBitrateRate = true;
    policy.opusBitrateKbps = 32;
    applyApmPolicy(config, policy);
    auto apm = webrtc::AudioProcessingBuilder().SetConfig(config).Create();
    expect(apm != nullptr, "APM builds with the policy config");
    const auto actual = apm->GetConfig();
    expect(actual.gain_controller2.enabled && actual.gain_controller2.adaptive_digital.enabled, "APM kept AGC2");
    expect(actual.pipeline.maximum_internal_processing_rate == 32000, "APM kept the 32 kHz rate");
}

static void testOpusComplexity() {
    expect(clampOpusComplexity(0) == 0, "0 means encoder default");
    expect(clampOpusComplexity(5) == 5, "5 allowed");
    expect(clampOpusComplexity(7) == 7, "7 allowed");
    expect(clampOpusComplexity(9) == 9, "9 allowed");
    expect(clampOpusComplexity(10) == 10, "10 allowed");
    expect(clampOpusComplexity(4) == 0, "below 5 is refused");
    expect(clampOpusComplexity(11) == 0, "above 10 is refused");
    expect(clampOpusComplexity(-3) == 0, "negative is refused");

    webrtc::SdpAudioFormat format("opus", 48000, 2, {{"minptime", "10"}, {"useinbandfec", "1"}, {"ptime", "60"}});
    const auto config = makeOpusConfig(format, 7);
    expect(config.has_value(), "opus config parses");
    if (config) {
        expect(config->complexity == 7 && config->low_rate_complexity == 7, "both complexities set");
        expect(config->IsOk(), "config stays valid");
        expect(config->fec_enabled, "negotiated FEC preserved");
        expect(config->frame_size_ms == 60, "negotiated ptime preserved");
        expect(config->sample_rate_hz == 48000, "sample rate preserved");
    }
    const auto untouched = makeOpusConfig(format, 0);
    const auto original = webrtc::AudioEncoderOpus::SdpToConfig(format);
    expect(untouched && original && untouched->complexity == original->complexity && untouched->low_rate_complexity == original->low_rate_complexity, "0 leaves the default complexities");
    webrtc::SdpAudioFormat notOpus("L16", 48000, 1);
    expect(!makeOpusConfig(notOpus, 7).has_value(), "non-opus format is not handled");
}

static void testOpusFactory() {
    auto inner = webrtc::CreateAudioEncoderFactory<webrtc::AudioEncoderOpus, webrtc::AudioEncoderL16>();
    expect(wrapOpusEncoderFactory(inner, 0).get() == inner.get(), "complexity 0 returns the original factory");
    auto wrapped = wrapOpusEncoderFactory(inner, 7);
    expect(wrapped.get() != inner.get(), "complexity 7 wraps the factory");
    expect(wrapped->GetSupportedEncoders().size() == inner->GetSupportedEncoders().size(), "same supported encoders");
    webrtc::SdpAudioFormat format("opus", 48000, 2, {{"minptime", "10"}, {"useinbandfec", "1"}});
    expect(wrapped->QueryAudioEncoder(format).has_value() == inner->QueryAudioEncoder(format).has_value(), "same query result");
    auto encoder = wrapped->MakeAudioEncoder(111, format, absl::nullopt);
    expect(encoder != nullptr, "wrapped factory makes an opus encoder");
    if (encoder) {
        expect(encoder->SampleRateHz() == 48000, "encoder is 48 kHz");
    }
    webrtc::SdpAudioFormat l16("L16", 48000, 1);
    auto other = wrapped->MakeAudioEncoder(112, l16, absl::nullopt);
    expect(other != nullptr, "other codecs pass through");
}

int main() {
    testHardwareMute();
    testNoiseModeParsing();
    testNoiseResolution();
    testMicGain();
    testApmRate();
    testApmBuilderRoundTrip();
    testOpusComplexity();
    testOpusFactory();
    fprintf(stderr, failures == 0 ? "all passed\n" : "%d FAILED\n", failures);
    return failures == 0 ? 0 : 1;
}
