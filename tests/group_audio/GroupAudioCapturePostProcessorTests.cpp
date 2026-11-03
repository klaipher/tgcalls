#include "group/GroupAudioCapturePostProcessor.h"

#include "modules/audio_processing/audio_buffer.h"

#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <new>
#include <vector>

static std::atomic<int> allocations{0};
static int denoiseCalls = 0;

void *operator new(size_t size) {
    allocations++;
    void *pointer = std::malloc(size ? size : 1);
    if (!pointer) {
        throw std::bad_alloc();
    }
    return pointer;
}

void operator delete(void *pointer) noexcept {
    std::free(pointer);
}

void operator delete(void *pointer, size_t) noexcept {
    std::free(pointer);
}

struct DenoiseState {
    int unused = 0;
};

extern "C" {

int rnnoise_get_frame_size() {
    return 480;
}

DenoiseState *rnnoise_create(void *) {
    return new DenoiseState();
}

void rnnoise_destroy(DenoiseState *state) {
    delete state;
}

float rnnoise_process_frame(DenoiseState *, float *out, const float *in) {
    denoiseCalls++;
    for (int i = 0; i < 480; i++) {
        out[i] = in[i] * 0.5f;
    }
    return 1.0f;
}

}

using tgcalls::AudioCapturePostProcessor;
using tgcalls::GroupLevelValue;
using tgcalls::NoiseSuppressionConfiguration;

static int failures = 0;

static void expect(bool condition, const char *name) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", name);
        failures++;
    } else {
        std::fprintf(stderr, "ok: %s\n", name);
    }
}

struct Harness {
    explicit Harness(int sampleRate, bool noiseSuppression, std::vector<float> *external = nullptr, webrtc::Mutex *mutex = nullptr) :
    configuration(std::make_shared<NoiseSuppressionConfiguration>(noiseSuppression)),
    processor(std::make_unique<AudioCapturePostProcessor>([this](GroupLevelValue const &value) {
        updates++;
        lastVoice = value.voice;
    }, configuration, external, mutex)),
    buffer(sampleRate, 1, sampleRate, 1, sampleRate, 1) {
        static_cast<webrtc::CustomProcessing *>(processor.get())->Initialize(sampleRate, 1);
    }

    void fill(float amplitude, int frame) {
        float *samples = buffer.channels()[0];
        for (size_t i = 0; i < buffer.num_frames(); i++) {
            samples[i] = amplitude * std::sin(0.05f * (float)(frame * buffer.num_frames() + i));
        }
    }

    float peak() {
        float result = 0.0f;
        const float *samples = buffer.channels_const()[0];
        for (size_t i = 0; i < buffer.num_frames(); i++) {
            result = std::max(result, std::fabs(samples[i]));
        }
        return result;
    }

    void process() {
        static_cast<webrtc::CustomProcessing *>(processor.get())->Process(&buffer);
    }

    std::shared_ptr<NoiseSuppressionConfiguration> configuration;
    std::unique_ptr<AudioCapturePostProcessor> processor;
    webrtc::AudioBuffer buffer;
    int updates = 0;
    bool lastVoice = false;
};

static void noiseSuppressionOffSkipsRnnoise() {
    Harness harness(48000, false);
    denoiseCalls = 0;
    for (int frame = 0; frame < 100; frame++) {
        harness.fill(8000.0f, frame);
        harness.process();
    }
    expect(denoiseCalls == 0, "noise suppression off never runs RNNoise");
    expect(harness.updates > 0, "noise suppression off still reports levels");
}

static void noiseSuppressionOffDetectsSpeechFromPeaks() {
    Harness speech(48000, false);
    for (int frame = 0; frame < 40; frame++) {
        speech.fill(50.0f, frame);
        speech.process();
    }
    expect(!speech.lastVoice, "noise suppression off reports a quiet signal as silence");
    bool sawVoice = false;
    for (int frame = 40; frame < 60; frame++) {
        speech.fill(8000.0f, frame);
        speech.process();
        sawVoice = sawVoice || speech.lastVoice;
    }
    expect(sawVoice, "noise suppression off reports a loud burst over a quiet floor as speech");

    Harness tone(48000, false);
    for (int frame = 0; frame < 300; frame++) {
        tone.fill(1500.0f, frame);
        tone.process();
    }
    expect(!tone.lastVoice, "noise suppression off treats a steady loud floor as noise");
}

static void peakSpeechDetectorFollowsTheNoiseFloor() {
    tgcalls::PeakSpeechDetector detector;
    expect(!detector.update(500.0f), "a frame below the minimum threshold is not speech");
    expect(detector.update(1200.0f), "a frame above the minimum threshold over a quiet floor is speech");
    tgcalls::PeakSpeechDetector noisy;
    for (int i = 0; i < 100; i++) {
        noisy.update(1500.0f);
    }
    expect(!noisy.update(2000.0f), "a frame less than twice a loud floor is not speech");
    expect(noisy.update(3500.0f), "a frame more than twice a loud floor is speech");
}

static void noiseSuppressionOnDenoisesAt48kHz() {
    Harness harness(48000, true);
    denoiseCalls = 0;
    harness.fill(8000.0f, 0);
    float before = harness.peak();
    harness.process();
    expect(denoiseCalls == 1, "noise suppression on runs RNNoise once per frame");
    expect(std::fabs(harness.peak() - before * 0.5f) < 1.0f, "the denoised 48 kHz frame replaces the capture buffer");
}

static void noiseSuppressionOnDenoisesAt16kHz() {
    Harness harness(16000, true);
    denoiseCalls = 0;
    float before = 0.0f;
    for (int frame = 0; frame < 50; frame++) {
        harness.fill(8000.0f, frame);
        before = harness.peak();
        harness.process();
    }
    expect(denoiseCalls == 50, "noise suppression on runs RNNoise on every 16 kHz frame");
    expect(harness.peak() < before * 0.7f && harness.peak() > before * 0.3f, "the denoised 16 kHz frame reaches the capture buffer");
}

static void externalSamplesReachTheCaptureBufferAt16kHz() {
    std::vector<float> external;
    webrtc::Mutex mutex;
    Harness harness(16000, false, &external, &mutex);
    for (int frame = 0; frame < 50; frame++) {
        {
            webrtc::MutexLock lock(&mutex);
            external.assign(480, 4000.0f);
        }
        harness.fill(0.0f, frame);
        harness.process();
    }
    expect(harness.peak() > 2000.0f, "mixed external samples reach the 16 kHz capture buffer");
}

static void processingAllocatesNothingPerFrame() {
    for (int sampleRate : {16000, 48000}) {
        for (bool noiseSuppression : {false, true}) {
            Harness harness(sampleRate, noiseSuppression);
            for (int frame = 0; frame < 10; frame++) {
                harness.fill(8000.0f, frame);
                harness.process();
            }
            int before = allocations.load();
            for (int frame = 10; frame < 1010; frame++) {
                harness.fill(8000.0f, frame);
                harness.process();
            }
            char name[128];
            std::snprintf(name, sizeof(name), "1,000 frames at %d Hz with noise suppression %s allocate nothing", sampleRate, noiseSuppression ? "on" : "off");
            expect(allocations.load() == before, name);
        }
    }
}

int main() {
    noiseSuppressionOffSkipsRnnoise();
    noiseSuppressionOffDetectsSpeechFromPeaks();
    peakSpeechDetectorFollowsTheNoiseFloor();
    noiseSuppressionOnDenoisesAt48kHz();
    noiseSuppressionOnDenoisesAt16kHz();
    externalSamplesReachTheCaptureBufferAt16kHz();
    processingAllocatesNothingPerFrame();
    return failures == 0 ? 0 : 1;
}
