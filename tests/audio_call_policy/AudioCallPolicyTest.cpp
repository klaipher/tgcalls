#include "AudioCallPolicy.h"
#include "CallsRuntimeSwitches.h"

#include <cstdio>
#include <string>

namespace {

int g_failures = 0;

#define CHECK_TRUE(cond)                                                  \
    do {                                                                  \
        if (!(cond)) {                                                    \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);   \
            g_failures++;                                                 \
        }                                                                 \
    } while (0)

using namespace tgcalls;

// --- Capture processing: who suppresses noise, the system or the software APM ---

void TestSwitchOffKeepsSoftwareSuppression() {
    auto choice = chooseCaptureProcessing(false, true, true, false);
    CHECK_TRUE(choice.softwareNoiseSuppression);
    CHECK_TRUE(!choice.disableHighpassFilter);
    CHECK_TRUE(std::string(choice.reason) == "switch-off");
}

void TestNonIosKeepsSoftwareSuppression() {
    auto choice = chooseCaptureProcessing(true, false, true, false);
    CHECK_TRUE(choice.softwareNoiseSuppression);
    CHECK_TRUE(!choice.disableHighpassFilter);
    CHECK_TRUE(std::string(choice.reason) == "platform");
}

void TestNoVoiceProcessingKeepsSoftwareSuppression() {
    // Group listeners and streams run on RemoteIO, bypass mode has no system DSP.
    auto choice = chooseCaptureProcessing(true, true, false, false);
    CHECK_TRUE(choice.softwareNoiseSuppression);
    CHECK_TRUE(!choice.disableHighpassFilter);
    CHECK_TRUE(std::string(choice.reason) == "no-system-voice-processing");
}

void TestEarlierVoiceProcessingFailureKeepsSoftwareSuppression() {
    auto choice = chooseCaptureProcessing(true, true, true, true);
    CHECK_TRUE(choice.softwareNoiseSuppression);
    CHECK_TRUE(!choice.disableHighpassFilter);
    CHECK_TRUE(std::string(choice.reason) == "failed-earlier");
}

void TestSystemSuppressionDropsSoftwareNoiseSuppressionAndHighpass() {
    auto choice = chooseCaptureProcessing(true, true, true, false);
    CHECK_TRUE(!choice.softwareNoiseSuppression);
    CHECK_TRUE(choice.disableHighpassFilter);
    CHECK_TRUE(std::string(choice.reason) == "system");
}

void TestVoiceProcessingLatch() {
    SystemVoiceProcessingLatch::reset();
    CHECK_TRUE(!SystemVoiceProcessingLatch::failed());
    SystemVoiceProcessingLatch::markFailed();
    CHECK_TRUE(SystemVoiceProcessingLatch::failed());
    SystemVoiceProcessingLatch::reset();
    CHECK_TRUE(!SystemVoiceProcessingLatch::failed());
}

// --- A voice-processing failure after the channel was created ---

// The channel options are fixed before the audio unit starts. When the unit then fails, the call that
// already dropped the software noise suppression has to get it back, not only the calls after it.
void TestObserverIsToldOnTheFirstFailureOnly() {
    SystemVoiceProcessingLatch::reset();
    int told = 0;
    int id = SystemVoiceProcessingLatch::addObserver([&]() { told++; });
    CHECK_TRUE(told == 0);
    SystemVoiceProcessingLatch::markFailed();
    CHECK_TRUE(told == 1);
    SystemVoiceProcessingLatch::markFailed();
    CHECK_TRUE(told == 1);
    SystemVoiceProcessingLatch::removeObserver(id);
    SystemVoiceProcessingLatch::reset();
}

void TestRemovedObserverIsNotTold() {
    SystemVoiceProcessingLatch::reset();
    int told = 0;
    int id = SystemVoiceProcessingLatch::addObserver([&]() { told++; });
    SystemVoiceProcessingLatch::removeObserver(id);
    SystemVoiceProcessingLatch::markFailed();
    CHECK_TRUE(told == 0);
    SystemVoiceProcessingLatch::reset();
}

// A failure that lands between choosing the options and registering must not be lost.
void TestObserverAddedAfterAFailureIsToldAtOnce() {
    SystemVoiceProcessingLatch::reset();
    SystemVoiceProcessingLatch::markFailed();
    int told = 0;
    int id = SystemVoiceProcessingLatch::addObserver([&]() { told++; });
    CHECK_TRUE(told == 1);
    SystemVoiceProcessingLatch::removeObserver(id);
    SystemVoiceProcessingLatch::reset();
}

void TestObserverMayRemoveItselfWhileBeingTold() {
    SystemVoiceProcessingLatch::reset();
    int told = 0;
    int id = 0;
    id = SystemVoiceProcessingLatch::addObserver([&]() {
        told++;
        SystemVoiceProcessingLatch::removeObserver(id);
    });
    SystemVoiceProcessingLatch::markFailed();
    CHECK_TRUE(told == 1);
    SystemVoiceProcessingLatch::reset();
}

// --- Other-audio ducking ---

void TestDuckingAppliedOnIos17WithSwitch() {
    auto plan = chooseOtherAudioDucking(true, true);
    CHECK_TRUE(plan.apply);
    CHECK_TRUE(plan.advanced);
    CHECK_TRUE(plan.level == OtherAudioDuckingLevel::Min);
    CHECK_TRUE(std::string(plan.reason) == "ok");
}

void TestDuckingNotAppliedWithSwitchOff() {
    auto plan = chooseOtherAudioDucking(false, true);
    CHECK_TRUE(!plan.apply);
    CHECK_TRUE(std::string(plan.reason) == "switch-off");
}

void TestDuckingNotAppliedBeforeIos17() {
    auto plan = chooseOtherAudioDucking(true, false);
    CHECK_TRUE(!plan.apply);
    CHECK_TRUE(std::string(plan.reason) == "os");
}

void TestDuckingLevelValuesMatchAppleHeader() {
    // AUVoiceIOOtherAudioDuckingLevel: Default 0, Min 10, Mid 20, Max 30.
    CHECK_TRUE(static_cast<int>(OtherAudioDuckingLevel::Default) == 0);
    CHECK_TRUE(static_cast<int>(OtherAudioDuckingLevel::Min) == 10);
    CHECK_TRUE(static_cast<int>(OtherAudioDuckingLevel::Mid) == 20);
    CHECK_TRUE(static_cast<int>(OtherAudioDuckingLevel::Max) == 30);
}

// --- Process-wide switches: defaults and round trip ---

void TestAudioSwitchDefaults() {
    // A fresh process: every audio change is off until it has been compared on a device.
    CHECK_TRUE(!CallsRuntimeSwitches::systemNoiseSuppression());
    CHECK_TRUE(!CallsRuntimeSwitches::advancedDucking());
    CHECK_TRUE(CallsRuntimeSwitches::groupNoiseMode() == 0);
}

void TestAudioSwitchRoundTrip() {
    CallsRuntimeSwitches::setSystemNoiseSuppression(true);
    CallsRuntimeSwitches::setAdvancedDucking(true);
    CallsRuntimeSwitches::setGroupNoiseMode(2);
    CHECK_TRUE(CallsRuntimeSwitches::systemNoiseSuppression());
    CHECK_TRUE(CallsRuntimeSwitches::advancedDucking());
    CHECK_TRUE(CallsRuntimeSwitches::groupNoiseMode() == 2);
    CHECK_TRUE(audiopolicy::currentGroupNoiseMode() == audiopolicy::GroupNoiseMode::RNNoiseOnly);
    CallsRuntimeSwitches::setSystemNoiseSuppression(false);
    CallsRuntimeSwitches::setAdvancedDucking(false);
    CallsRuntimeSwitches::setGroupNoiseMode(0);
    CHECK_TRUE(!CallsRuntimeSwitches::systemNoiseSuppression());
    CHECK_TRUE(!CallsRuntimeSwitches::advancedDucking());
    CHECK_TRUE(CallsRuntimeSwitches::groupNoiseMode() == 0);
}

// --- Group noise mode (both apps) and microphone gain ---

void TestNoiseModeParsing() {
    using namespace audiopolicy;
    CHECK_TRUE(groupNoiseModeFromInt(0) == GroupNoiseMode::Both);
    CHECK_TRUE(groupNoiseModeFromInt(1) == GroupNoiseMode::ApmOnly);
    CHECK_TRUE(groupNoiseModeFromInt(2) == GroupNoiseMode::RNNoiseOnly);
    CHECK_TRUE(groupNoiseModeFromInt(-1) == GroupNoiseMode::Both);
    CHECK_TRUE(groupNoiseModeFromInt(99) == GroupNoiseMode::Both);
}

void TestNoiseResolution() {
    using namespace audiopolicy;
    auto both = resolveGroupNoise(GroupNoiseMode::Both, true, true);
    CHECK_TRUE(both.apmNoiseSuppression && both.rnnoise);
    both = resolveGroupNoise(GroupNoiseMode::Both, false, true);
    CHECK_TRUE(both.apmNoiseSuppression && !both.rnnoise);
    both = resolveGroupNoise(GroupNoiseMode::Both, true, false);
    CHECK_TRUE(!both.apmNoiseSuppression && both.rnnoise);
    auto apm = resolveGroupNoise(GroupNoiseMode::ApmOnly, true, true);
    CHECK_TRUE(apm.apmNoiseSuppression && !apm.rnnoise);
    auto rn = resolveGroupNoise(GroupNoiseMode::RNNoiseOnly, true, true);
    CHECK_TRUE(!rn.apmNoiseSuppression && rn.rnnoise);
    // Turning suppression off never leaves the microphone without the WebRTC suppressor.
    rn = resolveGroupNoise(GroupNoiseMode::RNNoiseOnly, false, true);
    CHECK_TRUE(rn.apmNoiseSuppression && !rn.rnnoise);
    rn = resolveGroupNoise(GroupNoiseMode::RNNoiseOnly, true, false);
    CHECK_TRUE(!rn.apmNoiseSuppression && rn.rnnoise);
}

void TestMicGainAndHardwareMuteFollowThePlatform() {
#if defined(__APPLE__) && TARGET_OS_OSX
    CHECK_TRUE(audiopolicy::usesDigitalMicGain());
    CHECK_TRUE(!audiopolicy::analogAgcEnabled());
    CHECK_TRUE(!audiopolicy::shouldUseHardwareMicrophoneMute());
#else
    CHECK_TRUE(!audiopolicy::usesDigitalMicGain());
    CHECK_TRUE(audiopolicy::analogAgcEnabled());
    CHECK_TRUE(audiopolicy::shouldUseHardwareMicrophoneMute());
#endif
}

} // namespace

int main() {
    TestNoiseModeParsing();
    TestNoiseResolution();
    TestMicGainAndHardwareMuteFollowThePlatform();
    TestAudioSwitchDefaults();
    TestAudioSwitchRoundTrip();
    TestSwitchOffKeepsSoftwareSuppression();
    TestNonIosKeepsSoftwareSuppression();
    TestNoVoiceProcessingKeepsSoftwareSuppression();
    TestEarlierVoiceProcessingFailureKeepsSoftwareSuppression();
    TestSystemSuppressionDropsSoftwareNoiseSuppressionAndHighpass();
    TestVoiceProcessingLatch();
    TestObserverIsToldOnTheFirstFailureOnly();
    TestRemovedObserverIsNotTold();
    TestObserverAddedAfterAFailureIsToldAtOnce();
    TestObserverMayRemoveItselfWhileBeingTold();
    TestDuckingAppliedOnIos17WithSwitch();
    TestDuckingNotAppliedWithSwitchOff();
    TestDuckingNotAppliedBeforeIos17();
    TestDuckingLevelValuesMatchAppleHeader();
    if (g_failures == 0) {
        std::printf("ok\n");
        return 0;
    }
    std::printf("%d failures\n", g_failures);
    return 1;
}
