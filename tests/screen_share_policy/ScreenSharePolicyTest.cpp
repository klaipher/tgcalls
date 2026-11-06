#include "ScreenSharePolicy.h"

#include <cstdio>
#include <cstring>
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

using namespace tgcalls::screenshare;

void TestLegacyVersionsRaiseTheBitrateFloorOnly() {
    CHECK_TRUE(chooseScreenMode("2.7.7") == ScreenMode::BitrateFloorOnVideo);
    CHECK_TRUE(chooseScreenMode("5.0.0") == ScreenMode::BitrateFloorOnVideo);
}

void TestV2VersionsUseTheScreencastChannel() {
    for (const char *version : {"7.0.0", "8.0.0", "9.0.0", "12.0.0", "13.0.0"}) {
        CHECK_TRUE(chooseScreenMode(version) == ScreenMode::SeparateScreencastChannel);
    }
}

void TestReferenceVersionsCarryTheScreenOnTheVideoChannel() {
    CHECK_TRUE(chooseScreenMode("10.0.0") == ScreenMode::ScreenSettingsOnVideoChannel);
    CHECK_TRUE(chooseScreenMode("11.0.0") == ScreenMode::ScreenSettingsOnVideoChannel);
}

void TestEveryVersionTheMacRegistersSendsTheScreen() {
    for (const char *version : {"2.7.7", "5.0.0", "7.0.0", "8.0.0", "9.0.0", "10.0.0", "11.0.0", "12.0.0", "13.0.0"}) {
        CHECK_TRUE(screenIsSent(chooseScreenMode(version)));
    }
}

void TestOtherVersionsAreNotSupported() {
    for (const char *version : {"14.0.0", "18.0.0", "19.0.0", "", "unknown", "6.0.0", "2.4.4", "13.0", " 13.0.0"}) {
        CHECK_TRUE(chooseScreenMode(version) == ScreenMode::NotSupported);
        CHECK_TRUE(!screenIsSent(chooseScreenMode(version)));
    }
}

void TestModeNamesAreDistinct() {
    const ScreenMode modes[] = {
        ScreenMode::BitrateFloorOnVideo,
        ScreenMode::SeparateScreencastChannel,
        ScreenMode::ScreenSettingsOnVideoChannel,
        ScreenMode::NotSupported,
    };
    for (int i = 0; i < 4; i++) {
        for (int j = i + 1; j < 4; j++) {
            CHECK_TRUE(std::strcmp(screenModeName(modes[i]), screenModeName(modes[j])) != 0);
        }
    }
}

void TestLogLine() {
    CHECK_TRUE(callLogLine("10.0.0") == "Call: negotiated version 10.0.0, screen share mode screen-settings-on-video-channel");
    CHECK_TRUE(callLogLine("9.0.0") == "Call: negotiated version 9.0.0, screen share mode separate-screencast-channel");
    CHECK_TRUE(callLogLine("5.0.0") == "Call: negotiated version 5.0.0, screen share mode bitrate-floor-on-video");
    CHECK_TRUE(callLogLine("14.0.0") == "Call: negotiated version 14.0.0, screen share mode not-supported");
    CHECK_TRUE(callLogLine("") == "Call: negotiated version (none), screen share mode not-supported");
}

void TestScreenBitrateCeilingIsAboveTheCameraCeiling() {
    CHECK_TRUE(kScreenMaxBitrateBps > 1200 * 1024);
}

} // namespace

int main() {
    TestLegacyVersionsRaiseTheBitrateFloorOnly();
    TestV2VersionsUseTheScreencastChannel();
    TestReferenceVersionsCarryTheScreenOnTheVideoChannel();
    TestEveryVersionTheMacRegistersSendsTheScreen();
    TestOtherVersionsAreNotSupported();
    TestModeNamesAreDistinct();
    TestLogLine();
    TestScreenBitrateCeilingIsAboveTheCameraCeiling();
    if (g_failures != 0) {
        std::printf("%d failure(s)\n", g_failures);
        return 1;
    }
    std::printf("screen_share_policy: all tests passed\n");
    return 0;
}
