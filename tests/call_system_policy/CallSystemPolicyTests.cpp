#include "CallSystemPolicy.h"
#include "CallsRuntimeSwitches.h"

#include <algorithm>
#include <cstdio>

using namespace tgcalls::systempolicy;

static int failures = 0;
static int checks = 0;

#define CHECK(cond) do { checks++; if (!(cond)) { failures++; std::printf("FAIL line %d: %s\n", __LINE__, #cond); } } while (0)

int main() {
    OutgoingVideoSize full = limitOutgoingVideo(1280, 720, 25, 0, 0);
    CHECK((full == OutgoingVideoSize{1280, 720, 25}));
    CHECK((limitOutgoingVideo(1280, 720, 25, 540, 20) == OutgoingVideoSize{960, 540, 20}));
    CHECK((limitOutgoingVideo(1280, 720, 25, 360, 15) == OutgoingVideoSize{640, 360, 15}));
    CHECK((limitOutgoingVideo(720, 1280, 25, 540, 0) == OutgoingVideoSize{540, 960, 25}));
    CHECK((limitOutgoingVideo(640, 480, 25, 540, 30) == OutgoingVideoSize{640, 480, 25}));
    CHECK((limitOutgoingVideo(640, 360, 10, 0, 15) == OutgoingVideoSize{640, 360, 10}));
    CHECK((limitOutgoingVideo(0, 0, 25, 540, 15) == OutgoingVideoSize{0, 0, 15 > 0 ? 25 : 25}));
    const auto odd = limitOutgoingVideo(1000, 563, 25, 480, 0);
    CHECK(odd.width % 2 == 0 && odd.height % 2 == 0 && odd.height <= 480 && odd.width > 0);
    const auto tiny = limitOutgoingVideo(4000, 10, 25, 4, 0);
    CHECK(tiny.width >= 2 && tiny.height >= 2);

    CHECK(kStatsIntervalMs == 1000);
    CHECK(kGroupStatsIntervalMs == 2000);
    CHECK(kBroadcastConnectedDelayMs == 500);

    // Thermal video ladder (short-side cap): level 0 and 1 leave the format alone.
    for (int level = 0; level <= 1; level++) {
        CHECK((videoLadderFormat(level, 1280, 720, true) == OutgoingVideoSize{1280, 720, 25}));
    }
    CHECK((videoLadderFormat(2, 1280, 720, true) == OutgoingVideoSize{960, 540, 20}));
    CHECK((videoLadderFormat(3, 1280, 720, true) == OutgoingVideoSize{640, 360, 15}));
    CHECK((videoLadderFormat(2, 720, 1280, true) == OutgoingVideoSize{540, 960, 20}));
    CHECK((videoLadderFormat(3, 320, 240, true) == OutgoingVideoSize{320, 240, 15}));
    for (int level = -4; level <= 99; level++) {
        CHECK((videoLadderFormat(level, 1280, 720, false) == OutgoingVideoSize{1280, 720, 25}));
    }
    CHECK(videoLadderFormat(-4, 1280, 720, true).width == 1280);
    CHECK(videoLadderFormat(99, 1280, 720, true).width == 640);
    {
        int previousFps = 100;
        int previousWidth = 100000;
        for (int level = 0; level <= 3; level++) {
            const auto cap = videoLadderFormat(level, 1280, 720, true);
            CHECK(cap.fps <= previousFps && cap.width <= previousWidth);
            previousFps = cap.fps;
            previousWidth = cap.width;
        }
    }
    const auto portraitOdd = videoLadderFormat(2, 1000, 563, true);
    CHECK(portraitOdd.width % 2 == 0 && portraitOdd.height % 2 == 0 && std::min(portraitOdd.width, portraitOdd.height) <= 540);

    // The camera itself runs at the ladder's rate from level 2.
    CHECK(thermalCameraFpsCap(0, true) == 0 && thermalCameraFpsCap(1, true) == 0);
    CHECK(thermalCameraFpsCap(2, true) == 20 && thermalCameraFpsCap(3, true) == 15);
    for (int level = 0; level <= 3; level++) {
        CHECK(thermalCameraFpsCap(level, false) == 0);
    }

    // Engine thread quality of service: off leaves the threads alone, on promotes all three roles.
    for (auto role : {EngineThreadRole::Network, EngineThreadRole::Media, EngineThreadRole::Worker}) {
        CHECK(engineThreadQos(role, false) == EngineThreadQos::Unchanged);
        CHECK(engineThreadQos(role, true) == EngineThreadQos::UserInitiated);
    }

    // Remote video is dropped locally only when nobody can see it.
    CHECK(shouldDropRemoteVideo(true, false));
    CHECK(!shouldDropRemoteVideo(false, false));
    CHECK(!shouldDropRemoteVideo(true, true));
    CHECK(!shouldDropRemoteVideo(false, true));

    // The runtime switches and the state the apps report.
    CHECK(!tgcalls::CallsRuntimeSwitches::threadQoS() && !tgcalls::CallsRuntimeSwitches::thermalVideoLadder());
    CHECK(!tgcalls::CallsRuntimeSwitches::incomingVideoDropped() && !tgcalls::CallsRuntimeSwitches::livestreamVideoSuspended());
    CHECK(tgcalls::CallsRuntimeSwitches::thermalLevel() == 0);
    tgcalls::CallsRuntimeSwitches::setThreadQoS(true);
    tgcalls::CallsRuntimeSwitches::setThermalVideoLadder(true);
    tgcalls::CallsRuntimeSwitches::setThermalLevel(2);
    tgcalls::CallsRuntimeSwitches::setIncomingVideoDropped(true);
    tgcalls::CallsRuntimeSwitches::setLivestreamVideoSuspended(true);
    CHECK(tgcalls::CallsRuntimeSwitches::threadQoS() && tgcalls::CallsRuntimeSwitches::thermalVideoLadder());
    CHECK(tgcalls::CallsRuntimeSwitches::thermalLevel() == 2);
    CHECK(tgcalls::CallsRuntimeSwitches::incomingVideoDropped() && tgcalls::CallsRuntimeSwitches::livestreamVideoSuspended());
    tgcalls::CallsRuntimeSwitches::setThreadQoS(false);
    tgcalls::CallsRuntimeSwitches::setThermalVideoLadder(false);
    tgcalls::CallsRuntimeSwitches::setThermalLevel(0);
    tgcalls::CallsRuntimeSwitches::setIncomingVideoDropped(false);
    tgcalls::CallsRuntimeSwitches::setLivestreamVideoSuspended(false);
    CHECK(!tgcalls::CallsRuntimeSwitches::threadQoS() && tgcalls::CallsRuntimeSwitches::thermalLevel() == 0);

    std::printf("%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
