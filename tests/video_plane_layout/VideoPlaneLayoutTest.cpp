#include "VideoPlaneLayout.h"

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

// The chroma planes of 4:2:0 video have half the rows of the luma plane, rounded up. The
// frame wrappers handed to the UI used the luma height for them, so the NSData they exposed
// claimed twice the bytes the buffer holds.
void TestChromaRowsAreHalfTheLumaRowsRoundedUp() {
    CHECK_TRUE(chromaPlaneRows(720) == 360);
    CHECK_TRUE(chromaPlaneRows(721) == 361);
    CHECK_TRUE(chromaPlaneRows(1) == 1);
    CHECK_TRUE(chromaPlaneRows(0) == 0);
}

void TestPlaneByteLengthsMatchTheAllocation() {
    // 1280x720: Y is 720 rows, U, V and the interleaved UV plane are 360 rows.
    CHECK_TRUE(videoPlaneByteLength(1280, 720) == 921600);
    CHECK_TRUE(videoPlaneByteLength(640, chromaPlaneRows(720)) == 230400);
    CHECK_TRUE(videoPlaneByteLength(1280, chromaPlaneRows(720)) == 460800);
    // Odd height: the last chroma row is still allocated.
    CHECK_TRUE(videoPlaneByteLength(64, chromaPlaneRows(5)) == 64 * 3);
}

void TestNegativeInputsGiveZero() {
    CHECK_TRUE(videoPlaneByteLength(-1, 10) == 0);
    CHECK_TRUE(videoPlaneByteLength(10, -1) == 0);
    CHECK_TRUE(chromaPlaneRows(-3) == 0);
}

} // namespace

int main() {
    TestChromaRowsAreHalfTheLumaRowsRoundedUp();
    TestPlaneByteLengthsMatchTheAllocation();
    TestNegativeInputsGiveZero();

    if (g_failures != 0) {
        std::printf("%d failure(s)\n", g_failures);
        return 1;
    }
    std::printf("ok\n");
    return 0;
}
