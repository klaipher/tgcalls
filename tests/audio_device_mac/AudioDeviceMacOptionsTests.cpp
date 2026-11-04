#include <cstdio>
#include <type_traits>

#include "modules/audio_device/mac/audio_device_mac_options.h"

static int failures = 0;

static void expect(bool condition, const char *name) {
    fprintf(stderr, "%s: %s\n", condition ? "ok" : "FAIL", name);
    if (!condition) {
        failures++;
    }
}

using webrtc::AudioDeviceMacOptions;

// The audio workgroup join was an experiment the owner removed: the capture and render threads keep the
// scheduling tg_owt gives them.
template <typename T, typename = void>
struct HasWorkgroupSwitch : std::false_type {};
template <typename T>
struct HasWorkgroupSwitch<T, std::void_t<decltype(T::joinDeviceWorkgroup())>> : std::true_type {};
static_assert(!HasWorkgroupSwitch<AudioDeviceMacOptions>::value, "the audio workgroup switch is removed");

int main() {
    expect(!AudioDeviceMacOptions::pollKeyState().load(), "key-state polling is off by default");
    fprintf(stderr, failures == 0 ? "all passed\n" : "%d FAILED\n", failures);
    return failures == 0 ? 0 : 1;
}
