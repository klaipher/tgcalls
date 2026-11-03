#include "platform/darwin/CaptureFramePolicy.h"

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

const uint32_t k420f = 0x34323066;
const uint32_t k420v = 0x34323076;
const uint32_t kBGRA = 0x42475241;

// ---- frame plan ----

void TestPausedFramesAreDroppedBeforeAnyConversion() {
    CHECK_TRUE(planCameraFrame(true, false, false, k420f) == CaptureFramePlan::Drop);
    CHECK_TRUE(planCameraFrame(true, true, false, k420f) == CaptureFramePlan::Drop);
}

void TestSwitchOffKeepsTheI420Path() {
    CHECK_TRUE(planCameraFrame(false, false, false, k420f) == CaptureFramePlan::I420);
    CHECK_TRUE(planCameraFrame(false, false, false, k420v) == CaptureFramePlan::I420);
    CHECK_TRUE(planCameraFrame(false, false, false, kBGRA) == CaptureFramePlan::I420);
}

void TestNativeOnlyForBiPlanar420() {
    CHECK_TRUE(planCameraFrame(false, true, false, k420f) == CaptureFramePlan::Native);
    CHECK_TRUE(planCameraFrame(false, true, false, k420v) == CaptureFramePlan::Native);
    // Anything else keeps the I420 path, which is what it did before.
    CHECK_TRUE(planCameraFrame(false, true, false, kBGRA) == CaptureFramePlan::I420);
}

void TestLatchedFailureFallsBackToI420() {
    CHECK_TRUE(planCameraFrame(false, true, true, k420f) == CaptureFramePlan::I420);
}

// ---- buffer starvation monitor ----

void TestMonitorLatchesAfterRepeatedStarvation() {
    NativeCaptureHealth health;
    CHECK_TRUE(!health.failed());
    for (int i = 0; i < NativeCaptureHealth::kMaxStarvedFrames - 1; i++) {
        CHECK_TRUE(!health.onFrameDropped(true, 1000 + i * 10));
    }
    CHECK_TRUE(health.onFrameDropped(true, 1100));
    CHECK_TRUE(health.failed());
    // Stays failed.
    CHECK_TRUE(health.failed());
}

void TestMonitorIgnoresOtherDropReasons() {
    NativeCaptureHealth health;
    for (int i = 0; i < 100; i++) {
        CHECK_TRUE(!health.onFrameDropped(false, i * 10));
    }
    CHECK_TRUE(!health.failed());
}

void TestMonitorForgetsOldStarvation() {
    NativeCaptureHealth health;
    // Isolated drops, far apart, never add up.
    for (int i = 0; i < 50; i++) {
        CHECK_TRUE(!health.onFrameDropped(true, i * (NativeCaptureHealth::kWindowMs + 1)));
    }
    CHECK_TRUE(!health.failed());
}

// ---- fps ----

void TestCaptureFpsFollowsTheAdapterWhenSupported() {
    CHECK_TRUE(chooseCaptureFps(25, 30, 1.0, 30.0) == 25);
    CHECK_TRUE(chooseCaptureFps(25, 30, 15.0, 30.0) == 25);
}

void TestCaptureFpsFallsBackToTheCurrentRate() {
    // Range cannot go that low: keep what was configured.
    CHECK_TRUE(chooseCaptureFps(25, 30, 30.0, 30.0) == 30);
    CHECK_TRUE(chooseCaptureFps(25, 30, 1.0, 24.0) == 30);
    // A target above the current rate never raises it.
    CHECK_TRUE(chooseCaptureFps(60, 30, 1.0, 120.0) == 30);
}

} // namespace

int main() {
    TestPausedFramesAreDroppedBeforeAnyConversion();
    TestSwitchOffKeepsTheI420Path();
    TestNativeOnlyForBiPlanar420();
    TestLatchedFailureFallsBackToI420();
    TestMonitorLatchesAfterRepeatedStarvation();
    TestMonitorIgnoresOtherDropReasons();
    TestMonitorForgetsOldStarvation();
    TestCaptureFpsFollowsTheAdapterWhenSupported();
    TestCaptureFpsFallsBackToTheCurrentRate();

    if (g_failures != 0) {
        std::printf("%d failure(s)\n", g_failures);
        return 1;
    }
    std::printf("ok\n");
    return 0;
}
