#include "platform/darwin/EncoderModePolicy.h"

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

void TestSwitchOffIsTodaysMode() {
    EncoderLowLatencyLatch::reset();
    auto mode = chooseEncoderMode(false, true);
    CHECK_TRUE(!mode.lowLatency);
    CHECK_TRUE(!mode.realTime);
    CHECK_TRUE(std::string(mode.reason) == "switch-off");
}

void TestUnsupportedOsIsTodaysMode() {
    EncoderLowLatencyLatch::reset();
    auto mode = chooseEncoderMode(true, false);
    CHECK_TRUE(!mode.lowLatency);
    CHECK_TRUE(!mode.realTime);
    CHECK_TRUE(std::string(mode.reason) == "os");
}

void TestSwitchOnAndSupportedUsesLowLatencyWithRealTime() {
    EncoderLowLatencyLatch::reset();
    auto mode = chooseEncoderMode(true, true);
    CHECK_TRUE(mode.lowLatency);
    CHECK_TRUE(mode.realTime);
    CHECK_TRUE(std::string(mode.reason) == "ok");
}

// A failure to create or configure a low-latency session turns it off for every later session
// of the process, so a device that cannot do it pays for one failed attempt, not one per resize.
void TestFailureLatchesToTodaysMode() {
    EncoderLowLatencyLatch::reset();
    CHECK_TRUE(!EncoderLowLatencyLatch::failed());
    EncoderLowLatencyLatch::markFailed();
    CHECK_TRUE(EncoderLowLatencyLatch::failed());
    auto mode = chooseEncoderMode(true, true);
    CHECK_TRUE(!mode.lowLatency);
    CHECK_TRUE(!mode.realTime);
    CHECK_TRUE(std::string(mode.reason) == "failed-earlier");
    EncoderLowLatencyLatch::reset();
}

} // namespace

int main() {
    TestSwitchOffIsTodaysMode();
    TestUnsupportedOsIsTodaysMode();
    TestSwitchOnAndSupportedUsesLowLatencyWithRealTime();
    TestFailureLatchesToTodaysMode();

    if (g_failures != 0) {
        std::printf("%d failure(s)\n", g_failures);
        return 1;
    }
    std::printf("ok\n");
    return 0;
}
