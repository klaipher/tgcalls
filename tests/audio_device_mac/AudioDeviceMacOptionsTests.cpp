#include <cstdio>

#include "modules/audio_device/mac/audio_device_mac_options.h"

static int failures = 0;

static void expect(bool condition, const char *name) {
    fprintf(stderr, "%s: %s\n", condition ? "ok" : "FAIL", name);
    if (!condition) {
        failures++;
    }
}

using webrtc::AudioDeviceMacOptions;
using webrtc::WorkgroupMembership;

int main() {
    expect(!AudioDeviceMacOptions::pollKeyState().load(), "key-state polling is off by default");
    expect(!AudioDeviceMacOptions::joinDeviceWorkgroup().load(), "workgroup join is off by default");

    {
        int joins = 0, leaves = 0;
        {
            WorkgroupMembership membership(false, [&] { joins++; return true; }, [&] { leaves++; });
            expect(!membership.joined(), "disabled: not joined");
        }
        expect(joins == 0 && leaves == 0, "disabled: join and leave never called");
    }
    {
        int joins = 0, leaves = 0;
        {
            WorkgroupMembership membership(true, [&] { joins++; return true; }, [&] { leaves++; });
            expect(membership.joined(), "enabled: joined");
            expect(joins == 1 && leaves == 0, "enabled: one join, no leave while alive");
        }
        expect(joins == 1 && leaves == 1, "enabled: left exactly once on destruction");
    }
    {
        int joins = 0, leaves = 0;
        {
            WorkgroupMembership membership(true, [&] { joins++; return false; }, [&] { leaves++; });
            expect(!membership.joined(), "failed join: not joined");
        }
        expect(joins == 1 && leaves == 0, "failed join: no leave");
    }
    {
        int leaves = 0;
        {
            WorkgroupMembership membership(true, nullptr, [&] { leaves++; });
            expect(!membership.joined(), "missing join function: not joined");
        }
        expect(leaves == 0, "missing join function: no leave");
    }
    fprintf(stderr, failures == 0 ? "all passed\n" : "%d FAILED\n", failures);
    return failures == 0 ? 0 : 1;
}
