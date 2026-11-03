#import <Foundation/Foundation.h>
#import <CoreVideo/CoreVideo.h>

#include <thread>
#include <vector>

#include "TGRTCVideoRecovery.h"
#include "TGRTCVideoGeometry.h"
#include "TGLivestreamDecode.h"
#include "TGCameraFormatSelection.h"
#include "TGCallsRuntimeOptions.h"
#include "VideoDecodeFallback.h"
#import "TGRTCPixelTransfer.h"

static int failures = 0;

static void expect(bool condition, const char *name) {
    fprintf(stderr, "%s: %s\n", condition ? "ok" : "FAIL", name);
    if (!condition) {
        failures++;
    }
}

static void testResetLimiter() {
    using tgcalls::videorecovery::ResetLimiter;
    ResetLimiter limiter(3, 10000);
    expect(limiter.tryReset(0), "limiter allows first reset");
    expect(limiter.tryReset(1000), "limiter allows second reset");
    expect(limiter.tryReset(2000), "limiter allows third reset");
    expect(!limiter.tryReset(3000), "limiter blocks the fourth reset inside the window");
    expect(!limiter.tryReset(9999), "limiter still blocks just before the window ends");
    expect(limiter.tryReset(10000), "limiter allows again when the first reset leaves the window");
    expect(!limiter.tryReset(10001), "limiter counts the new reset");
    limiter.clear();
    expect(limiter.tryReset(10002), "limiter allows after clear");
    ResetLimiter zero(0, 1000);
    expect(!zero.tryReset(0), "limiter with zero budget never resets");
}

static void testBitrate() {
    using namespace tgcalls::videorecovery;
    expect(bitrateBpsFromKbps(0) == 0, "0 kbps is 0 bps");
    expect(bitrateBpsFromKbps(300) == 300000, "300 kbps is 300000 bps");
    expect(bitrateBpsFromKbps(2500) == 2500000, "2500 kbps is 2500000 bps");
    expect(bitrateBpsFromKbps(UINT32_MAX) == UINT32_MAX, "huge kbps saturates");
    expect(bitrateBpsFromKbps(4294967) == 4294967000u, "largest exact kbps converts");
    expect(bitrateBpsFromKbps(4294968) == UINT32_MAX, "first overflowing kbps saturates");
}

static void testDeadBand() {
    using namespace tgcalls::videorecovery;
    expect(!shouldApplyBitrate(1000000, 1000000), "equal bitrate is not applied");
    expect(!shouldApplyBitrate(1000000, 1030000), "3 percent change stays inside the dead band");
    expect(!shouldApplyBitrate(1000000, 970000), "3 percent drop stays inside the dead band");
    expect(shouldApplyBitrate(1000000, 1050000), "5 percent rise is applied");
    expect(shouldApplyBitrate(1000000, 900000), "10 percent drop is applied");
    expect(shouldApplyBitrate(0, 300000), "first bitrate is always applied");
    expect(shouldApplyBitrate(300000, 0), "bitrate to zero is applied");
    expect(!shouldApplyBitrate(0, 0), "zero to zero is not applied");
}

static void testGeometry() {
    using namespace tgcalls::videogeometry;
    expect(chromaHeight(720) == 360, "even height halves");
    expect(chromaHeight(721) == 361, "odd height rounds up");
    expect(chromaHeight(0) == 0, "zero height");
    expect(chromaHeight(-4) == 0, "negative height");
    expect(planeLength(1280, 360) == 460800, "chroma plane is stride times half the height");
    expect(planeLength(1280, 360) < planeLength(1280, 720), "chroma plane is shorter than the old height-based length");
    expect(planeLength(0, 360) == 0, "zero stride");
    expect(planeLength(100, -1) == 0, "negative rows");
    expect(planeLength(65536, 65536) == 4294967296ull, "no 32-bit overflow");
}

static void testLivestreamEligibility() {
    using namespace tgcalls::livestreamdecode;
    StreamInfo info;
    info.codec = Codec::H264;
    info.width = 1280;
    info.height = 720;
    expect(isEligible(info), "720p H.264 is eligible");
    info.codec = Codec::HEVC;
    expect(isEligible(info), "720p 8-bit HEVC is eligible");
    info.codec = Codec::Unknown;
    expect(!isEligible(info), "unknown codec is not eligible");
    info.codec = Codec::H264;
    info.width = 426;
    info.height = 240;
    expect(!isEligible(info), "240p is not eligible");
    info.width = 640;
    info.height = 360;
    expect(isEligible(info), "640x360 is the smallest eligible size");
    info.width = 360;
    info.height = 640;
    expect(isEligible(info), "portrait 360x640 is eligible");
    info.width = 8192;
    info.height = 4320;
    expect(!isEligible(info), "8K is not eligible");
    info.width = 4096;
    info.height = 2160;
    expect(isEligible(info), "4096 wide is eligible");
    info.width = 1920;
    info.height = 1080;
    info.progressive = false;
    expect(!isEligible(info), "interlaced is not eligible");
    info.progressive = true;
    info.bitDepth = 10;
    expect(!isEligible(info), "10-bit is not eligible");
    info.bitDepth = 8;
    info.chroma420 = false;
    expect(!isEligible(info), "4:2:2 is not eligible");
}

static void testSessionCounter() {
    using namespace tgcalls::livestreamdecode;
    SessionCounter counter(2);
    expect(counter.tryAcquire(), "first session acquired");
    expect(counter.tryAcquire(), "second session acquired");
    expect(!counter.tryAcquire(), "third session refused");
    counter.release();
    expect(counter.count() == 1, "release lowers the count");
    expect(counter.tryAcquire(), "session available after release");
    counter.release();
    counter.release();
    counter.release();
    expect(counter.count() == 0, "release never goes below zero");

    SessionCounter contended(2);
    std::atomic<int> acquired{0};
    std::vector<std::thread> threads;
    for (int i = 0; i < 16; i++) {
        threads.emplace_back([&]() {
            if (contended.tryAcquire()) {
                acquired++;
            }
        });
    }
    for (auto &thread : threads) {
        thread.join();
    }
    expect(acquired.load() == 2 && contended.count() == 2, "concurrent acquisition never exceeds the limit");
}

static void testFallback() {
    using namespace tgcalls;
    VideoDecodeFallback fallback;
    expect(!fallback.isSoftwareOnly(), "hardware decode is allowed at first");
    expect(fallback.noteHardwareFailure(VideoDecodeStage::Receive), "first failure switches to software");
    expect(fallback.isSoftwareOnly(), "software only after a failure");
    expect(!fallback.noteHardwareFailure(VideoDecodeStage::Send), "second failure reports nothing new");
    expect(fallback.failedStage() == VideoDecodeStage::Receive, "the first failing stage is kept");
}

static void testOutputBufferValidation() {
    using namespace tgcalls::livestreamdecode;
    expect(isAcceptableOutputBuffer(2, 8, true), "8-bit two-plane 4:2:0 is accepted");
    expect(!isAcceptableOutputBuffer(2, 10, true), "10-bit is rejected");
    expect(!isAcceptableOutputBuffer(3, 8, true), "three planes are rejected");
    expect(!isAcceptableOutputBuffer(1, 8, true), "single plane is rejected");
    expect(!isAcceptableOutputBuffer(2, 8, false), "4:2:2 is rejected");
}

static tgcalls::cameraformat::Candidate camera(int width, int height, double minFps, double maxFps, bool hdr = false, bool full = false) {
    tgcalls::cameraformat::Candidate candidate;
    candidate.width = width;
    candidate.height = height;
    candidate.minFps = minFps;
    candidate.maxFps = maxFps;
    candidate.isHDR = hdr;
    candidate.isFullRange = full;
    return candidate;
}

static void testCameraSelection() {
    using namespace tgcalls::cameraformat;
    expect(selectBest({}) == -1, "no formats gives no selection");

    std::vector<Candidate> studio = {
        camera(3840, 2160, 1, 30),
        camera(1920, 1080, 1, 60),
        camera(1280, 720, 1, 60),
        camera(1280, 720, 1, 30),
        camera(640, 480, 1, 30)
    };
    expect(selectBest(studio) == 3, "720p 30 fps wins over 720p 60 fps and larger formats");

    std::vector<Candidate> hdr = {
        camera(1280, 720, 1, 30, true),
        camera(1920, 1080, 1, 30)
    };
    expect(selectBest(hdr) == 1, "SDR 1080p wins over HDR 720p");

    std::vector<Candidate> range = {
        camera(1280, 720, 1, 30, false, true),
        camera(1280, 720, 1, 30, false, false)
    };
    expect(selectBest(range) == 1, "video range wins over full range at the same size");

    std::vector<Candidate> order1 = { camera(1280, 720, 1, 30), camera(1920, 1080, 1, 30) };
    std::vector<Candidate> order2 = { camera(1920, 1080, 1, 30), camera(1280, 720, 1, 30) };
    expect(selectBest(order1) == 0 && selectBest(order2) == 1, "selection does not depend on input order");

    std::vector<Candidate> small = { camera(320, 240, 1, 30), camera(640, 480, 1, 30), camera(176, 144, 1, 30) };
    expect(selectBest(small) == 1, "with only small formats the largest is chosen");

    std::vector<Candidate> mid = { camera(640, 480, 1, 30), camera(1280, 720, 1, 30) };
    expect(selectBest(mid) == 1, "720p wins over 640x480");

    std::vector<Candidate> fixed60 = { camera(1280, 720, 60, 60), camera(1280, 720, 1, 30) };
    expect(selectBest(fixed60) == 1, "a format that can run at 30 fps wins");

    std::vector<Candidate> equal = { camera(1280, 720, 1, 30), camera(1280, 720, 1, 30) };
    expect(selectBest(equal) == 0, "identical formats keep the first");
}

static void testFrameRateChoice() {
    using namespace tgcalls::cameraformat;
    auto a = chooseFrameRate({ { 1, 30 } });
    expect(a.rangeIndex == 0 && a.kind == FrameRateChoice::Kind::Target, "30 fps inside 1-30");
    auto b = chooseFrameRate({ { 1, 60 } });
    expect(b.rangeIndex == 0 && b.kind == FrameRateChoice::Kind::Target, "30 fps inside 1-60");
    auto c = chooseFrameRate({ { 60, 120 } });
    expect(c.kind == FrameRateChoice::Kind::SlowestOfRange, "range above 30 uses its slowest rate");
    auto d = chooseFrameRate({ { 1, 15 } });
    expect(d.kind == FrameRateChoice::Kind::FastestOfRange, "range below 30 uses its fastest rate");
    auto e = chooseFrameRate({ { 1, 15 }, { 24, 60 } });
    expect(e.rangeIndex == 1 && e.kind == FrameRateChoice::Kind::Target, "the range that contains 30 fps is chosen");
    auto f = chooseFrameRate({});
    expect(f.rangeIndex == -1, "no ranges gives no choice");
    auto g = chooseFrameRate({ { 30.0, 30.000030 } });
    expect(g.kind == FrameRateChoice::Kind::Target, "NTSC-style 30 fps range is accepted");
    expect(clampFps(1, 15) == 15 && clampFps(60, 120) == 60 && clampFps(1, 60) == 30, "clampFps clamps into the range");
}

static void testCleanAperture() {
    using namespace tgcalls::pixeltransfer;
    CleanAperture aperture;
    expect(computeCleanAperture(1920, 1080, 0, 0, 1920, 1080, aperture) && aperture.horizontalOffset == 0 && aperture.verticalOffset == 0, "full frame has no offset");
    expect(computeCleanAperture(1920, 1080, 420, 0, 1080, 1080, aperture) && aperture.width == 1080 && aperture.horizontalOffset == 0, "centred square crop has zero offset");
    expect(computeCleanAperture(100, 100, 50, 0, 50, 100, aperture) && aperture.horizontalOffset == 25, "right half is offset to the right");
    expect(computeCleanAperture(100, 100, 0, 0, 50, 100, aperture) && aperture.horizontalOffset == -25, "left half is offset to the left");
    expect(computeCleanAperture(100, 100, 0, 50, 100, 50, aperture) && aperture.verticalOffset == 25, "bottom half is offset down");
    expect(!computeCleanAperture(100, 100, 60, 0, 50, 100, aperture), "crop outside the source is rejected");
    expect(!computeCleanAperture(100, 100, 0, 0, 0, 100, aperture), "empty crop is rejected");
    expect(!computeCleanAperture(100, 100, -1, 0, 50, 100, aperture), "negative origin is rejected");
}

static CVPixelBufferRef makeNV12(int width, int height, uint8_t leftLuma, uint8_t rightLuma) {
    NSDictionary *attributes = @{ (id)kCVPixelBufferIOSurfacePropertiesKey: @{} };
    CVPixelBufferRef buffer = NULL;
    CVPixelBufferCreate(NULL, width, height, kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange, (__bridge CFDictionaryRef)attributes, &buffer);
    CVPixelBufferLockBaseAddress(buffer, 0);
    uint8_t *y = (uint8_t *)CVPixelBufferGetBaseAddressOfPlane(buffer, 0);
    size_t stride = CVPixelBufferGetBytesPerRowOfPlane(buffer, 0);
    for (int row = 0; row < height; row++) {
        for (int column = 0; column < width; column++) {
            y[row * stride + column] = column < width / 2 ? leftLuma : rightLuma;
        }
    }
    memset(CVPixelBufferGetBaseAddressOfPlane(buffer, 1), 128, CVPixelBufferGetBytesPerRowOfPlane(buffer, 1) * CVPixelBufferGetHeightOfPlane(buffer, 1));
    CVPixelBufferUnlockBaseAddress(buffer, 0);
    return buffer;
}

static int averageLuma(CVPixelBufferRef buffer) {
    CVPixelBufferLockBaseAddress(buffer, kCVPixelBufferLock_ReadOnly);
    uint8_t *y = (uint8_t *)CVPixelBufferGetBaseAddressOfPlane(buffer, 0);
    size_t stride = CVPixelBufferGetBytesPerRowOfPlane(buffer, 0);
    size_t width = CVPixelBufferGetWidthOfPlane(buffer, 0);
    size_t height = CVPixelBufferGetHeightOfPlane(buffer, 0);
    long sum = 0;
    for (size_t row = 0; row < height; row++) {
        for (size_t column = 0; column < width; column++) {
            sum += y[row * stride + column];
        }
    }
    CVPixelBufferUnlockBaseAddress(buffer, kCVPixelBufferLock_ReadOnly);
    return (int)(sum / (long)(width * height));
}

static void testPixelTransfer() {
    CVPixelBufferRef source = makeNV12(256, 128, 40, 200);
    CVPixelBufferRef destination = makeNV12(128, 64, 0, 0);
    TGRTCPixelTransfer *transfer = [[TGRTCPixelTransfer alloc] init];

    BOOL ok = [transfer cropAndScaleSource:source cropX:128 cropY:0 cropWidth:128 cropHeight:128 destination:destination];
    expect(ok, "hardware crop of the right half succeeds");
    int right = averageLuma(destination);
    expect(right > 180 && right < 215, "right-half crop contains only the bright half");

    ok = [transfer cropAndScaleSource:source cropX:0 cropY:0 cropWidth:128 cropHeight:128 destination:destination];
    int left = averageLuma(destination);
    expect(ok && left > 25 && left < 60, "left-half crop contains only the dark half");

    ok = [transfer cropAndScaleSource:source cropX:0 cropY:0 cropWidth:256 cropHeight:128 destination:destination];
    int both = averageLuma(destination);
    expect(ok && both > 100 && both < 140, "full-frame transfer contains both halves");

    expect(![transfer cropAndScaleSource:source cropX:200 cropY:0 cropWidth:128 cropHeight:128 destination:destination], "crop outside the source fails so the caller can fall back");

    [transfer invalidate];
    ok = [transfer cropAndScaleSource:source cropX:128 cropY:0 cropWidth:128 cropHeight:128 destination:destination];
    expect(ok, "the session is recreated after invalidate");

    CVPixelBufferRelease(source);
    CVPixelBufferRelease(destination);
}

static void testRuntimeOptionDefaults() {
    using tgcalls::RuntimeOptions;
    expect(!RuntimeOptions::livestreamHardwareDecode().load(), "livestream hardware decode defaults off");
    expect(RuntimeOptions::cameraFormatV2().load(), "camera format v2 defaults on");
    expect(!RuntimeOptions::hardwareCrop().load(), "hardware crop defaults off");
    expect(!RuntimeOptions::nativeDecodeFormat().load(), "native decode format defaults off");
    expect(!RuntimeOptions::screencastHardwareEncoder().load(), "screencast hardware encoder defaults off");
}

int main() {
    @autoreleasepool {
        testResetLimiter();
        testBitrate();
        testDeadBand();
        testGeometry();
        testLivestreamEligibility();
        testSessionCounter();
        testFallback();
        testOutputBufferValidation();
        testCameraSelection();
        testFrameRateChoice();
        testCleanAperture();
        testPixelTransfer();
        testRuntimeOptionDefaults();
    }
    if (failures > 0) {
        fprintf(stderr, "%d failures\n", failures);
        return 1;
    }
    fprintf(stderr, "all passed\n");
    return 0;
}
