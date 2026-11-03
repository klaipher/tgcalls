#include "CallSystemPolicy.h"

#include <cstdio>
#include <limits>

using namespace tgcalls::systempolicy;

static int failures = 0;
static int checks = 0;

#define CHECK(cond) do { checks++; if (!(cond)) { failures++; std::printf("FAIL line %d: %s\n", __LINE__, #cond); } } while (0)

int main() {
    CHECK(RenderSchedule::nextDelayMs(false, -1.0) == 20);
    CHECK(RenderSchedule::nextDelayMs(false, 0.001) == 20);
    CHECK(RenderSchedule::nextDelayMs(true, -1.0) == 8);
    CHECK(RenderSchedule::nextDelayMs(true, std::numeric_limits<double>::quiet_NaN()) == 8);
    CHECK(RenderSchedule::nextDelayMs(true, std::numeric_limits<double>::infinity()) == 8);
    CHECK(RenderSchedule::nextDelayMs(true, 0.0) == 8);
    CHECK(RenderSchedule::nextDelayMs(true, 0.003) == 8);
    CHECK(RenderSchedule::nextDelayMs(true, 0.0121) == 13);
    CHECK(RenderSchedule::nextDelayMs(true, 0.020) == 20);
    CHECK(RenderSchedule::nextDelayMs(true, 0.0333) == 20);
    CHECK(RenderSchedule::nextDelayMs(true, 5.0) == 20);

    // Fake-clock walk of a 30 fps stream: ticks per second must be well below the old 120 Hz.
    {
        double now = 0.0;
        const double interval = 1.0 / 30.0;
        int ticks = 0;
        double nextFrame = interval;
        while (now < 10.0) {
            if (now >= nextFrame) {
                nextFrame += interval;
            }
            const int delay = RenderSchedule::nextDelayMs(true, nextFrame - now);
            now += delay / 1000.0;
            ticks++;
        }
        CHECK(ticks < 10 * 80);
        CHECK(ticks > 10 * 30);
    }

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

    CHECK(kStatsIntervalMs == 2000);
    CHECK(kBroadcastConnectedDelayMs == 500);

    std::printf("%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
