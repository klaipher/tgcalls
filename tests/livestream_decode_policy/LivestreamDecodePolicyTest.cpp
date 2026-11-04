#include "group/LivestreamDecodePolicy.h"

#include <cstdio>
#include <set>
#include <string>
#include <vector>

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

LivestreamDecodeStreamInfo h264_720p() {
    LivestreamDecodeStreamInfo info;
    info.codec = LivestreamDecodeCodec::H264;
    info.pixelFormat = LivestreamDecodePixelFormat::Yuv420P8;
    info.width = 1280;
    info.height = 720;
    return info;
}

// ---- eligibility ----

void TestEligibleStreams() {
    CHECK_TRUE(evaluateHardwareDecodeEligibility(h264_720p()).eligible);

    auto hevc = h264_720p();
    hevc.codec = LivestreamDecodeCodec::Hevc;
    CHECK_TRUE(evaluateHardwareDecodeEligibility(hevc).eligible);

    auto uhd = h264_720p();
    uhd.width = 4096;
    uhd.height = 2160;
    CHECK_TRUE(evaluateHardwareDecodeEligibility(uhd).eligible);
}

void TestIneligibleStreams() {
    auto other = h264_720p();
    other.codec = LivestreamDecodeCodec::Other;
    CHECK_TRUE(!evaluateHardwareDecodeEligibility(other).eligible);

    // 10-bit HEVC decodes to P010, which the 8-bit NV12 shader would draw as garbage.
    auto tenBit = h264_720p();
    tenBit.codec = LivestreamDecodeCodec::Hevc;
    tenBit.pixelFormat = LivestreamDecodePixelFormat::Other;
    CHECK_TRUE(!evaluateHardwareDecodeEligibility(tenBit).eligible);

    auto interlaced = h264_720p();
    interlaced.progressive = false;
    CHECK_TRUE(!evaluateHardwareDecodeEligibility(interlaced).eligible);

    auto huge = h264_720p();
    huge.width = 4097;
    CHECK_TRUE(!evaluateHardwareDecodeEligibility(huge).eligible);

    auto empty = h264_720p();
    empty.height = 0;
    CHECK_TRUE(!evaluateHardwareDecodeEligibility(empty).eligible);

    CHECK_TRUE(std::string(evaluateHardwareDecodeEligibility(other).reason) == "codec");
    CHECK_TRUE(std::string(evaluateHardwareDecodeEligibility(tenBit).reason) == "pixel-format");
}

// ---- session counter ----

void TestCounterCapsSessions() {
    HardwareDecodeSessionCounter counter(2);
    CHECK_TRUE(counter.tryAcquire());
    CHECK_TRUE(counter.tryAcquire());
    CHECK_TRUE(!counter.tryAcquire());
    CHECK_TRUE(counter.active() == 2);
    counter.release();
    CHECK_TRUE(counter.active() == 1);
    CHECK_TRUE(counter.tryAcquire());
}

void TestLeaseReleasesOnDestruction() {
    HardwareDecodeSessionCounter counter(1);
    {
        HardwareDecodeSessionLease lease = HardwareDecodeSessionLease::acquire(counter);
        CHECK_TRUE(lease.isValid());
        CHECK_TRUE(counter.active() == 1);
        HardwareDecodeSessionLease second = HardwareDecodeSessionLease::acquire(counter);
        CHECK_TRUE(!second.isValid());
        CHECK_TRUE(counter.active() == 1);

        // Moving a lease must not release or double count.
        HardwareDecodeSessionLease moved = std::move(lease);
        CHECK_TRUE(moved.isValid());
        CHECK_TRUE(!lease.isValid());
        CHECK_TRUE(counter.active() == 1);
    }
    CHECK_TRUE(counter.active() == 0);
}

// ---- decision ----

void TestSwitchOffIsAlwaysSoftware() {
    HardwareDecodeSessionCounter counter(2);
    auto decision = decideLivestreamDecodeMode(false, true, h264_720p(), false, counter);
    CHECK_TRUE(decision.mode == LivestreamDecodeMode::Software);
    CHECK_TRUE(decision.reason == "switch-off");
    CHECK_TRUE(counter.active() == 0);
}

void TestUnsupportedPlatformIsSoftware() {
    HardwareDecodeSessionCounter counter(2);
    auto decision = decideLivestreamDecodeMode(true, false, h264_720p(), false, counter);
    CHECK_TRUE(decision.mode == LivestreamDecodeMode::Software);
    CHECK_TRUE(decision.reason == "platform");
}

void TestEligibleStreamGetsHardwareAndALease() {
    HardwareDecodeSessionCounter counter(2);
    auto decision = decideLivestreamDecodeMode(true, true, h264_720p(), false, counter);
    CHECK_TRUE(decision.mode == LivestreamDecodeMode::Hardware);
    CHECK_TRUE(decision.reason == "ok");
    CHECK_TRUE(decision.lease.isValid());
    CHECK_TRUE(counter.active() == 1);
}

void TestIneligibleStreamTakesNoSession() {
    HardwareDecodeSessionCounter counter(2);
    auto info = h264_720p();
    info.progressive = false;
    auto decision = decideLivestreamDecodeMode(true, true, info, false, counter);
    CHECK_TRUE(decision.mode == LivestreamDecodeMode::Software);
    CHECK_TRUE(decision.reason == "ineligible:interlaced");
    CHECK_TRUE(counter.active() == 0);
}

void TestCapFallsBackToSoftwarePerStream() {
    HardwareDecodeSessionCounter counter(2);
    auto a = decideLivestreamDecodeMode(true, true, h264_720p(), false, counter);
    auto b = decideLivestreamDecodeMode(true, true, h264_720p(), false, counter);
    auto c = decideLivestreamDecodeMode(true, true, h264_720p(), false, counter);
    CHECK_TRUE(a.mode == LivestreamDecodeMode::Hardware);
    CHECK_TRUE(b.mode == LivestreamDecodeMode::Hardware);
    CHECK_TRUE(c.mode == LivestreamDecodeMode::Software);
    CHECK_TRUE(c.reason == "session-cap");
    // Dropping a stream frees its slot for the next one.
    a = LivestreamDecodeDecision();
    auto d = decideLivestreamDecodeMode(true, true, h264_720p(), false, counter);
    CHECK_TRUE(d.mode == LivestreamDecodeMode::Hardware);
}

void TestFailedStreamStaysSoftware() {
    HardwareDecodeSessionCounter counter(2);
    auto decision = decideLivestreamDecodeMode(true, true, h264_720p(), true, counter);
    CHECK_TRUE(decision.mode == LivestreamDecodeMode::Software);
    CHECK_TRUE(decision.reason == "hardware-failed-earlier");
    CHECK_TRUE(counter.active() == 0);
}

// ---- failure policy ----

void TestHardwareErrorFallsBackOnceThenEndsPart() {
    LivestreamDecodeHealth health;
    CHECK_TRUE(!health.hardwareFailed());

    // A software decoder error keeps today's behaviour: the part ends.
    CHECK_TRUE(health.onDecodeError(LivestreamDecodeMode::Software) == LivestreamDecodeErrorAction::EndPart);
    CHECK_TRUE(!health.hardwareFailed());

    // A hardware error switches that stream to software and drops the rest of the part.
    CHECK_TRUE(health.onDecodeError(LivestreamDecodeMode::Hardware) == LivestreamDecodeErrorAction::FallBackToSoftware);
    CHECK_TRUE(health.hardwareFailed());

    // A software error after the fallback ends the part again.
    CHECK_TRUE(health.onDecodeError(LivestreamDecodeMode::Software) == LivestreamDecodeErrorAction::EndPart);
}

// ---- pruning ----

void TestPruningKeepsActiveAndQueuedEndpoints() {
    std::set<std::string> existing = {"a", "b", "c", "d"};
    std::set<std::string> active = {"a"};
    std::set<std::string> queued = {"c"};
    auto pruned = endpointsToPrune(existing, active, queued);
    CHECK_TRUE((std::set<std::string>(pruned.begin(), pruned.end()) == std::set<std::string>{"b", "d"}));

    CHECK_TRUE(endpointsToPrune({}, active, queued).empty());
    CHECK_TRUE(endpointsToPrune(existing, existing, {}).empty());
}

// ---- runtime switches ----

void TestSwitchesDefaultOff() {
    CHECK_TRUE(!CallsRuntimeSwitches::livestreamHardwareDecoding());
    CallsRuntimeSwitches::setLivestreamHardwareDecoding(true);
    CHECK_TRUE(CallsRuntimeSwitches::livestreamHardwareDecoding());
    CallsRuntimeSwitches::setLivestreamHardwareDecoding(false);
    CHECK_TRUE(!CallsRuntimeSwitches::livestreamHardwareDecoding());
}

// Merged from the Mac decoder policy: small streams stay in software, the output buffer is validated,
// and the failed stage is remembered for the log.
void TestSmallStreamsStaySoftware() {
    auto small = h264_720p();
    small.width = 640;
    small.height = 360;
    CHECK_TRUE(evaluateHardwareDecodeEligibility(small).eligible);
    small.height = 359;
    CHECK_TRUE(!evaluateHardwareDecodeEligibility(small).eligible);
    small.width = 639;
    small.height = 360;
    CHECK_TRUE(!evaluateHardwareDecodeEligibility(small).eligible);
    auto portrait = h264_720p();
    portrait.width = 360;
    portrait.height = 640;
    CHECK_TRUE(evaluateHardwareDecodeEligibility(portrait).eligible);
}

void TestOutputBufferValidation() {
    CHECK_TRUE(isAcceptableOutputBuffer(2, 8, true));
    CHECK_TRUE(!isAcceptableOutputBuffer(3, 8, true));
    CHECK_TRUE(!isAcceptableOutputBuffer(2, 10, true));
    CHECK_TRUE(!isAcceptableOutputBuffer(2, 8, false));
}

void TestFailedStageIsRemembered() {
    LivestreamDecodeHealth health;
    CHECK_TRUE(health.onDecodeError(LivestreamDecodeMode::Software, LivestreamDecodeStage::Send) == LivestreamDecodeErrorAction::EndPart);
    CHECK_TRUE(!health.hardwareFailed());
    CHECK_TRUE(health.onDecodeError(LivestreamDecodeMode::Hardware, LivestreamDecodeStage::FrameConversion) == LivestreamDecodeErrorAction::FallBackToSoftware);
    CHECK_TRUE(health.hardwareFailed() && health.failedStage() == LivestreamDecodeStage::FrameConversion);
    health.onDecodeError(LivestreamDecodeMode::Hardware, LivestreamDecodeStage::Send);
    CHECK_TRUE(health.failedStage() == LivestreamDecodeStage::FrameConversion);
}

} // namespace

int main() {
    TestSmallStreamsStaySoftware();
    TestOutputBufferValidation();
    TestFailedStageIsRemembered();
    TestEligibleStreams();
    TestIneligibleStreams();
    TestCounterCapsSessions();
    TestLeaseReleasesOnDestruction();
    TestSwitchOffIsAlwaysSoftware();
    TestUnsupportedPlatformIsSoftware();
    TestEligibleStreamGetsHardwareAndALease();
    TestIneligibleStreamTakesNoSession();
    TestCapFallsBackToSoftwarePerStream();
    TestFailedStreamStaysSoftware();
    TestHardwareErrorFallsBackOnceThenEndsPart();
    TestPruningKeepsActiveAndQueuedEndpoints();
    TestSwitchesDefaultOff();

    if (g_failures != 0) {
        std::printf("%d failure(s)\n", g_failures);
        return 1;
    }
    std::printf("ok\n");
    return 0;
}
