#include <cstdio>

#include "CallAudioWebRtc.h"
#include "modules/audio_processing/include/audio_processing.h"

static int failures = 0;

static void expect(bool condition, const char *name) {
    fprintf(stderr, "%s: %s\n", condition ? "ok" : "FAIL", name);
    if (!condition) {
        failures++;
    }
}

using namespace tgcalls::audiopolicy;

// The pure decisions (noise modes, hardware mute, which platform uses the digital gain) are covered by
// tgcalls/AudioCallPolicyTest.cpp, which both apps run. This test links the real libtg_owt.

static void testDigitalMicGain() {
    webrtc::AudioProcessing::Config config;
    const auto before = config;
    const bool changed = applyDigitalMicGain(config);
    expect(changed == usesDigitalMicGain(), "the config is changed exactly where the digital gain is used");
#if defined(__APPLE__) && TARGET_OS_OSX
    expect(config.gain_controller2.enabled, "AGC2 enabled");
    expect(config.gain_controller2.adaptive_digital.enabled, "adaptive digital enabled");
    expect(!config.gain_controller2.input_volume_controller.enabled, "no analog input volume control, so the system mic slider does not move");
    expect(!config.gain_controller1.enabled, "AGC1 not enabled by the policy");
    auto apm = webrtc::AudioProcessingBuilder().SetConfig(config).Create();
    expect(apm != nullptr, "APM builds with the policy config");
    expect(apm && apm->GetConfig().gain_controller2.enabled && apm->GetConfig().gain_controller2.adaptive_digital.enabled, "APM kept AGC2");
#else
    expect(config.gain_controller2.enabled == before.gain_controller2.enabled, "iOS leaves AGC2 alone");
#endif
    (void)before;
}

int main() {
    testDigitalMicGain();
    fprintf(stderr, failures == 0 ? "all passed\n" : "%d FAILED\n", failures);
    return failures == 0 ? 0 : 1;
}
