#include "group/StreamRenderPolicy.h"
#include <limits>

#include <cstdio>
#include <map>
#include <memory>
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

// The render timer used to repost itself every 8 ms forever. The delay now depends on what the
// tick has to serve.
// Video follows the next frame's presentation time, within [8 ms, 20 ms]; unknown means the floor.
void TestVideoTickFollowsTheNextFrame() {
    const auto ring = kStreamAudioRingCapacityMs;
    CHECK_TRUE(streamRenderDelayMs(StreamRenderActivity::Video, ring) == 8);
    CHECK_TRUE(streamRenderDelayMs(StreamRenderActivity::Video, ring, -1.0) == 8);
    CHECK_TRUE(streamRenderDelayMs(StreamRenderActivity::Video, ring, std::numeric_limits<double>::quiet_NaN()) == 8);
    CHECK_TRUE(streamRenderDelayMs(StreamRenderActivity::Video, ring, std::numeric_limits<double>::infinity()) == 8);
    CHECK_TRUE(streamRenderDelayMs(StreamRenderActivity::Video, ring, 0.0) == 8);
    CHECK_TRUE(streamRenderDelayMs(StreamRenderActivity::Video, ring, 0.003) == 8);
    CHECK_TRUE(streamRenderDelayMs(StreamRenderActivity::Video, ring, 0.0121) == 13);
    CHECK_TRUE(streamRenderDelayMs(StreamRenderActivity::Video, ring, 0.020) == 20);
    CHECK_TRUE(streamRenderDelayMs(StreamRenderActivity::Video, ring, 5.0) == 20);
    // The frame time is ignored when nothing plays video.
    CHECK_TRUE(streamRenderDelayMs(StreamRenderActivity::AudioOnly, ring, 0.003) == 40);
    CHECK_TRUE(streamRenderDelayMs(StreamRenderActivity::Idle, ring, 0.003) == 20);
    // A small ring bounds every delay to half of it.
    CHECK_TRUE(streamRenderDelayMs(StreamRenderActivity::Video, 24, 5.0) == 12);
}

// Fake-clock walk of a 30 fps stream: ticks per second stay far below the old 120 Hz, and above the frame rate.
void TestThirtyFpsWalk() {
    double now = 0.0;
    const double interval = 1.0 / 30.0;
    int ticks = 0;
    double nextFrame = interval;
    while (now < 10.0) {
        if (now >= nextFrame) {
            nextFrame += interval;
        }
        const int delay = streamRenderDelayMs(StreamRenderActivity::Video, kStreamAudioRingCapacityMs, nextFrame - now);
        now += delay / 1000.0;
        ticks++;
    }
    CHECK_TRUE(ticks < 10 * 80);
    CHECK_TRUE(ticks > 10 * 30);
}

void TestAudioOnlyTicksSlowerThanVideo() {
    const int audio = streamRenderDelayMs(StreamRenderActivity::AudioOnly, kStreamAudioRingCapacityMs);
    const int video = streamRenderDelayMs(StreamRenderActivity::Video, kStreamAudioRingCapacityMs);
    CHECK_TRUE(audio > video);
}

// The audio ring buffer holds 400 ms. No tick delay may let a full ring drain below half before
// the next refill.
void TestTickNeverLetsTheAudioRingDrainBelowHalf() {
    const StreamRenderActivity all[] = {StreamRenderActivity::Idle, StreamRenderActivity::AudioOnly, StreamRenderActivity::Video};
    for (auto activity : all) {
        for (int capacity : {400, 200, 100, 40}) {
            const int delay = streamRenderDelayMs(activity, capacity);
            CHECK_TRUE(delay <= capacity / 2);
            CHECK_TRUE(delay >= 1);
        }
    }
}

void TestIdleStillPollsForNewSegments() {
    const int delay = streamRenderDelayMs(StreamRenderActivity::Idle, kStreamAudioRingCapacityMs);
    CHECK_TRUE(delay > 0 && delay <= 50);
}

void TestActivityFollowsWhatPlays() {
    CHECK_TRUE(streamRenderActivity(false, false) == StreamRenderActivity::Idle);
    CHECK_TRUE(streamRenderActivity(true, false) == StreamRenderActivity::AudioOnly);
    CHECK_TRUE(streamRenderActivity(false, true) == StreamRenderActivity::Video);
    CHECK_TRUE(streamRenderActivity(true, true) == StreamRenderActivity::Video);
}

struct Sink {};

void TestSinkListsDropExpiredSinksAndCountLiveOnes() {
    std::map<std::string, std::vector<std::weak_ptr<Sink>>> sinks;
    auto a = std::make_shared<Sink>();
    {
        auto b = std::make_shared<Sink>();
        sinks["x"] = {a, b};
        CHECK_TRUE(liveStreamSinkCount(sinks, "x") == 2);
    }
    CHECK_TRUE(liveStreamSinkCount(sinks, "x") == 1);
    CHECK_TRUE(liveStreamSinkCount(sinks, "missing") == 0);
    pruneExpiredStreamSinks(sinks);
    CHECK_TRUE(sinks["x"].size() == 1);
    a.reset();
    pruneExpiredStreamSinks(sinks);
    CHECK_TRUE(sinks.find("x") == sinks.end());
}

// Frames are decoded only for a stream somebody shows.
void TestDecodeOnlyWhenWatched() {
    CHECK_TRUE(shouldDecodeStreamVideo(1));
    CHECK_TRUE(shouldDecodeStreamVideo(3));
    CHECK_TRUE(!shouldDecodeStreamVideo(0));
}

// A tile that was hidden and is shown again mid-part has to decode every frame between where the part
// stopped and "now". Done in one render tick that stalls the media thread (and the audio refill with it),
// so a tick decodes a bounded number of frames and catches up over the following ticks.
void TestCatchUpIsBoundedPerTick() {
    int decoded = 0;
    while (mayDecodeAnotherStreamFrame(decoded)) {
        decoded++;
        CHECK_TRUE(decoded <= kMaxStreamFramesDecodedPerTick);
    }
    CHECK_TRUE(decoded == kMaxStreamFramesDecodedPerTick);
    // Normal playback needs one or two frames per tick, far below the bound.
    CHECK_TRUE(kMaxStreamFramesDecodedPerTick >= 4);
    CHECK_TRUE(mayDecodeAnotherStreamFrame(0));
    CHECK_TRUE(mayDecodeAnotherStreamFrame(2));
}

// A part skipped while nobody watched leaves the decoder with references from before the gap; it is
// flushed once, before it decodes again.
void TestSkippedPartFlushesTheDecoderOnce() {
    SkippedPartFlag flag;
    CHECK_TRUE(!flag.consumeFlushNeeded());
    flag.noteSkipped();
    flag.noteSkipped();
    CHECK_TRUE(flag.consumeFlushNeeded());
    CHECK_TRUE(!flag.consumeFlushNeeded());
    flag.noteSkipped();
    CHECK_TRUE(flag.consumeFlushNeeded());
}

// A tile hidden and shown again inside one part still has that part's references in the decoder. Flushing
// there would make the decoder drop every frame up to the next keyframe (FFmpeg outputs nothing until it
// recovers), so the rest of the part would be read in one call with nothing shown. The flush waits for the
// next part to start (parts start on a keyframe).
void TestSkippedPartFlushWaitsForThePartStart() {
    SkippedPartFlag flag;
    CHECK_TRUE(!flag.consumeFlushNeededAtPartStart(true));
    flag.noteSkipped();
    CHECK_TRUE(!flag.consumeFlushNeededAtPartStart(false));
    CHECK_TRUE(!flag.consumeFlushNeededAtPartStart(false));
    CHECK_TRUE(flag.consumeFlushNeededAtPartStart(true));
    CHECK_TRUE(!flag.consumeFlushNeededAtPartStart(true));
    flag.noteSkipped();
    CHECK_TRUE(flag.consumeFlushNeededAtPartStart(true));
}

} // namespace

int main() {
    TestCatchUpIsBoundedPerTick();
    TestSkippedPartFlushesTheDecoderOnce();
    TestSkippedPartFlushWaitsForThePartStart();
    TestVideoTickFollowsTheNextFrame();
    TestThirtyFpsWalk();
    TestAudioOnlyTicksSlowerThanVideo();
    TestTickNeverLetsTheAudioRingDrainBelowHalf();
    TestIdleStillPollsForNewSegments();
    TestActivityFollowsWhatPlays();
    TestSinkListsDropExpiredSinksAndCountLiveOnes();
    TestDecodeOnlyWhenWatched();
    if (g_failures == 0) {
        std::printf("ok\n");
    }
    return g_failures == 0 ? 0 : 1;
}
