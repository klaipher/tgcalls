#include "CallPowerPolicy.h"
#include "CallsRuntimeSwitches.h"

#include <cstdio>

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

// Thermal video ladder. Level 0 and 1 are today's behaviour: no cap, 25 fps.
void TestNominalAndElevatedLeaveTheFormatAlone() {
    for (int level = 0; level <= 1; level++) {
        const auto cap = videoLadderFormat(level, 1280, 720, true);
        CHECK_TRUE(cap.width == 1280 && cap.height == 720 && cap.fps == 25);
    }
}

// The ladder used to lower only what the encoder is fed; the camera and its image processing kept
// running at the full rate, which is the main heat source, and the adapter dropped every third frame
// (30 to 20) unevenly. The camera itself is asked for the ladder's rate.
void TestCameraRunsAtTheLadderRateFromHigh() {
    CHECK_TRUE(thermalCameraFpsCap(0, true) == 0);
    CHECK_TRUE(thermalCameraFpsCap(1, true) == 0);
    CHECK_TRUE(thermalCameraFpsCap(2, true) == 20);
    CHECK_TRUE(thermalCameraFpsCap(3, true) == 15);
    CHECK_TRUE(thermalCameraFpsCap(3, true) == videoLadderFormat(3, 1280, 720, true).fps);
}

void TestCameraIsNotCappedWithTheSwitchOff() {
    for (int level = 0; level <= 3; level++) {
        CHECK_TRUE(thermalCameraFpsCap(level, false) == 0);
    }
}

void TestHighCapsTheLongSideAndFrameRate() {
    const auto cap = videoLadderFormat(2, 1280, 720, true);
    CHECK_TRUE(cap.width == 640 && cap.height == 360);
    CHECK_TRUE(cap.fps == 20);
}

void TestCriticalGoesFurtherDown() {
    const auto cap = videoLadderFormat(3, 1280, 720, true);
    CHECK_TRUE(cap.width == 480 && cap.height == 270);
    CHECK_TRUE(cap.fps == 15);
}

void TestPortraitKeepsItsAspectAndEvenSizes() {
    const auto cap = videoLadderFormat(2, 720, 1280, true);
    CHECK_TRUE(cap.width == 360 && cap.height == 640);
    const auto odd = videoLadderFormat(2, 1000, 563, true);
    CHECK_TRUE(odd.width % 2 == 0 && odd.height % 2 == 0);
    CHECK_TRUE(odd.width <= 640);
}

void TestSmallerSourcesAreNeverUpscaled() {
    const auto cap = videoLadderFormat(2, 320, 240, true);
    CHECK_TRUE(cap.width == 320 && cap.height == 240);
    CHECK_TRUE(cap.fps == 20);
}

void TestSwitchOffIsTodaysFormat() {
    for (int level = 0; level <= 3; level++) {
        const auto cap = videoLadderFormat(level, 1280, 720, false);
        CHECK_TRUE(cap.width == 1280 && cap.height == 720 && cap.fps == 25);
    }
}

void TestOutOfRangeLevelsAreClamped() {
    CHECK_TRUE(videoLadderFormat(-4, 1280, 720, true).width == 1280);
    CHECK_TRUE(videoLadderFormat(99, 1280, 720, true).width == 480);
}

void TestEveryLevelOnlyLowersTheFormat() {
    int previousFps = 100;
    int previousWidth = 100000;
    for (int level = 0; level <= 3; level++) {
        const auto cap = videoLadderFormat(level, 1280, 720, true);
        CHECK_TRUE(cap.fps <= previousFps);
        CHECK_TRUE(cap.width <= previousWidth);
        previousFps = cap.fps;
        previousWidth = cap.width;
    }
}

// Engine thread quality of service.
void TestQosOffLeavesThreadsAlone() {
    for (auto role : {EngineThreadRole::Network, EngineThreadRole::Media, EngineThreadRole::Worker}) {
        CHECK_TRUE(engineThreadQos(role, false) == EngineThreadQos::Unchanged);
    }
}

void TestQosOnPromotesTheCallThreads() {
    for (auto role : {EngineThreadRole::Network, EngineThreadRole::Media, EngineThreadRole::Worker}) {
        CHECK_TRUE(engineThreadQos(role, true) == EngineThreadQos::UserInitiated);
    }
}

// Remote video is dropped locally only when nobody can see it.
void TestRemoteVideoDropNeedsBackgroundAndNoPictureInPicture() {
    CHECK_TRUE(shouldDropRemoteVideo(true, false, true));
    CHECK_TRUE(!shouldDropRemoteVideo(false, false, true));
    CHECK_TRUE(!shouldDropRemoteVideo(true, true, true));
    CHECK_TRUE(!shouldDropRemoteVideo(false, true, true));
    CHECK_TRUE(!shouldDropRemoteVideo(true, false, false));
}

void TestNewSwitchesDefaultToToday() {
    CHECK_TRUE(!CallsRuntimeSwitches::threadQoS());
    CHECK_TRUE(!CallsRuntimeSwitches::thermalVideoLadder());
    CHECK_TRUE(!CallsRuntimeSwitches::backgroundVideoDrop());
    CHECK_TRUE(!CallsRuntimeSwitches::incomingVideoDropped());
    CHECK_TRUE(CallsRuntimeSwitches::thermalLevel() == 0);
}

void TestNewSwitchesRoundTrip() {
    CallsRuntimeSwitches::setThreadQoS(true);
    CallsRuntimeSwitches::setThermalVideoLadder(true);
    CallsRuntimeSwitches::setBackgroundVideoDrop(true);
    CallsRuntimeSwitches::setIncomingVideoDropped(true);
    CallsRuntimeSwitches::setThermalLevel(2);
    CHECK_TRUE(CallsRuntimeSwitches::threadQoS());
    CHECK_TRUE(CallsRuntimeSwitches::thermalVideoLadder());
    CHECK_TRUE(CallsRuntimeSwitches::backgroundVideoDrop());
    CHECK_TRUE(CallsRuntimeSwitches::incomingVideoDropped());
    CHECK_TRUE(CallsRuntimeSwitches::thermalLevel() == 2);
    CallsRuntimeSwitches::setThreadQoS(false);
    CallsRuntimeSwitches::setThermalVideoLadder(false);
    CallsRuntimeSwitches::setBackgroundVideoDrop(false);
    CallsRuntimeSwitches::setIncomingVideoDropped(false);
    CallsRuntimeSwitches::setThermalLevel(0);
    CHECK_TRUE(!CallsRuntimeSwitches::threadQoS());
    CHECK_TRUE(CallsRuntimeSwitches::thermalLevel() == 0);
}

} // namespace

int main() {
    TestCameraRunsAtTheLadderRateFromHigh();
    TestCameraIsNotCappedWithTheSwitchOff();
    TestNominalAndElevatedLeaveTheFormatAlone();
    TestHighCapsTheLongSideAndFrameRate();
    TestCriticalGoesFurtherDown();
    TestPortraitKeepsItsAspectAndEvenSizes();
    TestSmallerSourcesAreNeverUpscaled();
    TestSwitchOffIsTodaysFormat();
    TestOutOfRangeLevelsAreClamped();
    TestEveryLevelOnlyLowersTheFormat();
    TestQosOffLeavesThreadsAlone();
    TestQosOnPromotesTheCallThreads();
    TestRemoteVideoDropNeedsBackgroundAndNoPictureInPicture();
    TestNewSwitchesDefaultToToday();
    TestNewSwitchesRoundTrip();
    if (g_failures == 0) {
        std::printf("ok\n");
    }
    return g_failures == 0 ? 0 : 1;
}
