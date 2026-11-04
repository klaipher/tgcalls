#include "platform/darwin/DarwinCodecSupport.h"
#include <cstdio>
using namespace tgcalls;
static int g = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); g++; } } while (0)

int main() {
    DarwinEncodingFacts iosArm; iosArm.isIos = true; iosArm.hevcPresetAvailable = true; iosArm.h265Compiled = true;
    DarwinEncodingFacts macArm; macArm.h265Compiled = true;
    DarwinEncodingFacts macX86; macX86.isX86_64 = true; macX86.h265Compiled = true;
    DarwinEncodingFacts iosSim; iosSim.isIos = true; iosSim.isX86_64 = true;
    for (const auto &f : {iosArm, macArm, macX86, iosSim}) {
        CHECK(darwinSupportsEncoding("VP8", f));    // interop floor stays
        CHECK(darwinSupportsEncoding("VP9", f));
        CHECK(!darwinSupportsEncoding("AV1", f));
        CHECK(!darwinSupportsEncoding("", f));
    }
    // H.264 keeps the upstream rule (offered to a v1 peer only on x86_64): what a legacy call offers is interop,
    // and flipping it waits for the owner's decision (Pending owner, phase 1 review).
    CHECK(!darwinSupportsEncoding("H264", iosArm));
    CHECK(!darwinSupportsEncoding("H264", macArm));
    CHECK(darwinSupportsEncoding("H264", macX86));
    CHECK(darwinSupportsEncoding("H264", iosSim));
    CHECK(darwinSupportsEncoding("H265", iosArm));
    CHECK(darwinSupportsEncoding("H265", macArm));
    CHECK(!darwinSupportsEncoding("H265", macX86));
    CHECK(!darwinSupportsEncoding("H265", iosSim));
    DarwinEncodingFacts noHevc = iosArm; noHevc.hevcPresetAvailable = false;
    CHECK(!darwinSupportsEncoding("H265", noHevc));
    DarwinEncodingFacts off = macArm; off.h265Compiled = false;
    CHECK(!darwinSupportsEncoding("H265", off));
    if (g) { std::printf("%d failure(s)\n", g); return 1; }
    std::printf("DarwinCodecSupportTests OK\n");
    return 0;
}
