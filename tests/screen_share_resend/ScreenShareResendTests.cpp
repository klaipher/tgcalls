#include "platform/darwin/ScreenShareResendPolicy.h"

#include <cstdint>
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

constexpr int64_t kMs = 1000000;

// ScreenCaptureKit only delivers a frame when the screen changed. A still screen therefore sends nothing,
// and a viewer who joins, or a keyframe request after packet loss, gets no picture until something moves.
void TestNothingToResendBeforeTheFirstFrame() {
    StillScreenRepeater repeater;
    int64_t timestamp = 0;
    CHECK_TRUE(!repeater.takeResend(0, &timestamp));
    CHECK_TRUE(!repeater.takeResend(10000 * kMs, &timestamp));
}

void TestStillScreenIsResentEveryInterval() {
    StillScreenRepeater repeater;
    int64_t timestamp = 0;
    repeater.noteFrameSent(1000 * kMs, 1000 * kMs);
    CHECK_TRUE(!repeater.takeResend(1000 * kMs, &timestamp));
    CHECK_TRUE(!repeater.takeResend(1000 * kMs + StillScreenRepeater::kIntervalNs - 1, &timestamp));
    CHECK_TRUE(repeater.takeResend(1000 * kMs + StillScreenRepeater::kIntervalNs, &timestamp));
    int resends = 1;
    int64_t now = 1000 * kMs + StillScreenRepeater::kIntervalNs;
    for (int i = 0; i < 20; i++) {
        now += StillScreenRepeater::kIntervalNs;
        if (repeater.takeResend(now, &timestamp)) {
            resends++;
        }
    }
    CHECK_TRUE(resends == 21);
}

void TestIntervalIsAtMostTwoHundredMilliseconds() {
    CHECK_TRUE(StillScreenRepeater::kIntervalNs >= 50 * kMs);
    CHECK_TRUE(StillScreenRepeater::kIntervalNs <= 200 * kMs);
}

// A real frame already served the viewers; the repeat waits a full interval after it.
void TestARealFrameRestartsTheInterval() {
    StillScreenRepeater repeater;
    int64_t timestamp = 0;
    repeater.noteFrameSent(0, 0);
    repeater.noteFrameSent(90 * kMs, 90 * kMs);
    CHECK_TRUE(!repeater.takeResend(150 * kMs, &timestamp));
    CHECK_TRUE(repeater.takeResend(90 * kMs + StillScreenRepeater::kIntervalNs, &timestamp));
}

// The encoder's frame adapter and the timestamp aligner need strictly rising timestamps.
void TestResentTimestampsRiseStrictly() {
    StillScreenRepeater repeater;
    repeater.noteFrameSent(500 * kMs, 500 * kMs);
    int64_t last = 500 * kMs;
    int64_t now = 500 * kMs;
    for (int i = 0; i < 30; i++) {
        now += StillScreenRepeater::kIntervalNs;
        int64_t timestamp = 0;
        CHECK_TRUE(repeater.takeResend(now, &timestamp));
        CHECK_TRUE(timestamp > last);
        last = timestamp;
    }
}

// The repeat continues the frames' own clock: the last timestamp plus the time that passed, whatever the
// frames' clock reads relative to the caller's.
void TestResentTimestampContinuesTheFramesClock() {
    StillScreenRepeater repeater;
    const int64_t frameClock = 987654 * kMs;
    repeater.noteFrameSent(5000 * kMs, frameClock);
    int64_t first = 0;
    int64_t second = 0;
    CHECK_TRUE(repeater.takeResend(5000 * kMs + StillScreenRepeater::kIntervalNs, &first));
    CHECK_TRUE(first == frameClock + StillScreenRepeater::kIntervalNs);
    CHECK_TRUE(repeater.takeResend(5000 * kMs + 2 * StillScreenRepeater::kIntervalNs, &second));
    CHECK_TRUE(second == first + StillScreenRepeater::kIntervalNs);
}

// After the share stops nothing is repeated, and the old frame is not served to the next share.
void TestResetForgetsTheLastFrame() {
    StillScreenRepeater repeater;
    int64_t timestamp = 0;
    repeater.noteFrameSent(0, 0);
    repeater.reset();
    CHECK_TRUE(!repeater.hasFrame());
    CHECK_TRUE(!repeater.takeResend(10000 * kMs, &timestamp));
}

// A late wake-up produces one resend, not a burst that catches up on the missed ticks.
void TestLateWakeUpResendsOnce() {
    StillScreenRepeater repeater;
    int64_t timestamp = 0;
    repeater.noteFrameSent(0, 0);
    CHECK_TRUE(repeater.takeResend(1000 * kMs, &timestamp));
    CHECK_TRUE(!repeater.takeResend(1000 * kMs + 1, &timestamp));
    CHECK_TRUE(!repeater.takeResend(1000 * kMs + StillScreenRepeater::kIntervalNs - 1, &timestamp));
}

} // namespace

int main() {
    TestNothingToResendBeforeTheFirstFrame();
    TestStillScreenIsResentEveryInterval();
    TestIntervalIsAtMostTwoHundredMilliseconds();
    TestARealFrameRestartsTheInterval();
    TestResentTimestampsRiseStrictly();
    TestResentTimestampContinuesTheFramesClock();
    TestResetForgetsTheLastFrame();
    TestLateWakeUpResendsOnce();
    if (g_failures != 0) {
        std::printf("%d failure(s)\n", g_failures);
        return 1;
    }
    std::printf("ok\n");
    return 0;
}
