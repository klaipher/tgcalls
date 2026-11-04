#include "platform/darwin/TGRTCVideoRecovery.h"

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

using namespace tgcalls::videorecovery;

void TestLimiterAllowsThreeResetsPerWindow() {
    ResetLimiter limiter(3, 10000);
    CHECK_TRUE(limiter.tryReset(0));
    CHECK_TRUE(limiter.tryReset(1000));
    CHECK_TRUE(limiter.tryReset(2000));
    CHECK_TRUE(!limiter.tryReset(3000));
    CHECK_TRUE(!limiter.tryReset(9999));
}

void TestLimiterAllowsAgainWhenTheOldestLeavesTheWindow() {
    ResetLimiter limiter(3, 10000);
    CHECK_TRUE(limiter.tryReset(0));
    CHECK_TRUE(limiter.tryReset(1000));
    CHECK_TRUE(limiter.tryReset(2000));
    CHECK_TRUE(limiter.tryReset(10000));
    CHECK_TRUE(!limiter.tryReset(10001));
}

void TestDefaultsAreThreePerTenSeconds() {
    ResetLimiter limiter;
    CHECK_TRUE(limiter.tryReset(0));
    CHECK_TRUE(limiter.tryReset(1));
    CHECK_TRUE(limiter.tryReset(2));
    CHECK_TRUE(!limiter.tryReset(3));
    CHECK_TRUE(limiter.tryReset(10000));
}

void TestRefusedResetsAreNotCounted() {
    ResetLimiter limiter(1, 1000);
    CHECK_TRUE(limiter.tryReset(0));
    CHECK_TRUE(!limiter.tryReset(500));
    CHECK_TRUE(!limiter.tryReset(999));
    CHECK_TRUE(limiter.tryReset(1000));
}

void TestClearAndZeroBudget() {
    ResetLimiter limiter(1, 1000);
    CHECK_TRUE(limiter.tryReset(0));
    CHECK_TRUE(!limiter.tryReset(1));
    limiter.clear();
    CHECK_TRUE(limiter.tryReset(2));
    ResetLimiter none(0, 1000);
    CHECK_TRUE(!none.tryReset(0));
}

void TestKbpsToBps() {
    CHECK_TRUE(bitrateBpsFromKbps(0) == 0);
    CHECK_TRUE(bitrateBpsFromKbps(300) == 300000);
    CHECK_TRUE(bitrateBpsFromKbps(2500) == 2500000);
    CHECK_TRUE(bitrateBpsFromKbps(4294967) == 4294967000u);
    CHECK_TRUE(bitrateBpsFromKbps(4294968) == UINT32_MAX);
    CHECK_TRUE(bitrateBpsFromKbps(UINT32_MAX) == UINT32_MAX);
}

void TestBitrateDeadBand() {
    CHECK_TRUE(!shouldApplyBitrate(1000000, 1000000));
    CHECK_TRUE(!shouldApplyBitrate(1000000, 1030000));
    CHECK_TRUE(shouldApplyBitrate(1000000, 1050000));
    CHECK_TRUE(shouldApplyBitrate(0, 300000));
    CHECK_TRUE(!shouldApplyBitrate(0, 0));
}

}  // namespace

int main() {
    TestLimiterAllowsThreeResetsPerWindow();
    TestLimiterAllowsAgainWhenTheOldestLeavesTheWindow();
    TestDefaultsAreThreePerTenSeconds();
    TestRefusedResetsAreNotCounted();
    TestClearAndZeroBudget();
    TestKbpsToBps();
    TestBitrateDeadBand();
    if (g_failures != 0) {
        std::printf("%d failure(s)\n", g_failures);
        return 1;
    }
    std::printf("ok\n");
    return 0;
}
