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

// --- Audio session mode ---

void TestModeKeptWhenSwitchOnAndCallModeActive() {
    // ManagedAudioSession picked videoChat for a video call; the engine's default is voiceChat.
    CHECK_TRUE(chooseAudioSessionMode("AVAudioSessionModeVoiceChat", "AVAudioSessionModeVideoChat", true) == "AVAudioSessionModeVideoChat");
}

void TestModeOverwrittenWhenSwitchOff() {
    CHECK_TRUE(chooseAudioSessionMode("AVAudioSessionModeVoiceChat", "AVAudioSessionModeVideoChat", false) == "AVAudioSessionModeVoiceChat");
}

void TestModeAppliedWhenCurrentIsNotACallMode() {
    CHECK_TRUE(chooseAudioSessionMode("AVAudioSessionModeVoiceChat", "AVAudioSessionModeDefault", true) == "AVAudioSessionModeVoiceChat");
    CHECK_TRUE(chooseAudioSessionMode("AVAudioSessionModeVoiceChat", "AVAudioSessionModeMeasurement", true) == "AVAudioSessionModeVoiceChat");
}

void TestModeNeverChangesAnExplicitNonDefaultRequest() {
    // Only the engine's own default (voiceChat) is replaceable; a caller asking for another mode gets it.
    CHECK_TRUE(chooseAudioSessionMode("AVAudioSessionModeVideoChat", "AVAudioSessionModeVoiceChat", true) == "AVAudioSessionModeVideoChat");
    CHECK_TRUE(chooseAudioSessionMode("AVAudioSessionModeDefault", "AVAudioSessionModeVideoChat", true) == "AVAudioSessionModeDefault");
}

void TestModeUnchangedWhenAlreadyEqual() {
    CHECK_TRUE(chooseAudioSessionMode("AVAudioSessionModeVoiceChat", "AVAudioSessionModeVoiceChat", true) == "AVAudioSessionModeVoiceChat");
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
    CHECK_TRUE(!CallsRuntimeSwitches::groupNarrowbandProcessing());
    CHECK_TRUE(!CallsRuntimeSwitches::keepSessionMode());
}

void TestAudioSwitchRoundTrip() {
    CallsRuntimeSwitches::setSystemNoiseSuppression(true);
    CallsRuntimeSwitches::setAdvancedDucking(true);
    CallsRuntimeSwitches::setGroupNarrowbandProcessing(true);
    CallsRuntimeSwitches::setKeepSessionMode(true);
    CHECK_TRUE(CallsRuntimeSwitches::systemNoiseSuppression());
    CHECK_TRUE(CallsRuntimeSwitches::advancedDucking());
    CHECK_TRUE(CallsRuntimeSwitches::groupNarrowbandProcessing());
    CHECK_TRUE(CallsRuntimeSwitches::keepSessionMode());
    CallsRuntimeSwitches::setSystemNoiseSuppression(false);
    CallsRuntimeSwitches::setAdvancedDucking(false);
    CallsRuntimeSwitches::setGroupNarrowbandProcessing(false);
    CallsRuntimeSwitches::setKeepSessionMode(false);
    CHECK_TRUE(!CallsRuntimeSwitches::systemNoiseSuppression());
    CHECK_TRUE(!CallsRuntimeSwitches::advancedDucking());
    CHECK_TRUE(!CallsRuntimeSwitches::groupNarrowbandProcessing());
    CHECK_TRUE(!CallsRuntimeSwitches::keepSessionMode());
}

} // namespace

int main() {
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
    TestModeKeptWhenSwitchOnAndCallModeActive();
    TestModeOverwrittenWhenSwitchOff();
    TestModeAppliedWhenCurrentIsNotACallMode();
    TestModeNeverChangesAnExplicitNonDefaultRequest();
    TestModeUnchangedWhenAlreadyEqual();
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
