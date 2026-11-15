#include "voice_enhance.h"

#include <algorithm>
#include <cmath>
#include <math.h>
#include <string.h>
#include <time.h>
#include <vector>


#include "modules/audio_processing/include/audio_processing.h"

extern "C" {
#include <rnnoise.h>
}

namespace {

constexpr int kRate = 48000;
constexpr int kBlock = 480;      // 10 ms, the frame size of WebRTC APM and RNNoise
constexpr int kFft = 512;
constexpr int kHop = 256;
constexpr int kBins = kFft / 2 + 1;
constexpr int kReverbDelayHops = 9;  // about 48 ms: where the late tail starts
constexpr double kPi = 3.14159265358979323846;

// Same time-of-day-free clock the rest of the app uses for timing.
int64_t nowMicros() {
    timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t) ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
}

struct Fifo {
    std::vector<float> data;
    size_t head = 0;

    size_t size() const { return data.size() - head; }

    void push(const float *src, size_t n) {
        data.insert(data.end(), src, src + n);
    }

    void pushZeros(size_t n) {
        data.insert(data.end(), n, 0.0f);
    }

    void pop(float *dst, size_t n) {
        memcpy(dst, data.data() + head, n * sizeof(float));
        head += n;
        if (head >= 8192 && head * 2 >= data.size()) {
            data.erase(data.begin(), data.begin() + head);
            head = 0;
        }
    }
};

struct Biquad {
    float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;

    float run(float x) {
        float y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }

    void set(double nb0, double nb1, double nb2, double a0, double na1, double na2) {
        b0 = (float) (nb0 / a0);
        b1 = (float) (nb1 / a0);
        b2 = (float) (nb2 / a0);
        a1 = (float) (na1 / a0);
        a2 = (float) (na2 / a0);
    }

    void highpass(double f0, double q) {
        double w = 2 * kPi * f0 / kRate, c = cos(w), alpha = sin(w) / (2 * q);
        set((1 + c) / 2, -(1 + c), (1 + c) / 2, 1 + alpha, -2 * c, 1 - alpha);
    }

    void lowpass(double f0, double q) {
        double w = 2 * kPi * f0 / kRate, c = cos(w), alpha = sin(w) / (2 * q);
        set((1 - c) / 2, 1 - c, (1 - c) / 2, 1 + alpha, -2 * c, 1 - alpha);
    }

    void bandpass(double f0, double q) {
        double w = 2 * kPi * f0 / kRate, c = cos(w), alpha = sin(w) / (2 * q);
        set(alpha, 0, -alpha, 1 + alpha, -2 * c, 1 - alpha);
    }

    void peaking(double f0, double gainDb, double q) {
        double A = pow(10.0, gainDb / 40.0);
        double w = 2 * kPi * f0 / kRate, c = cos(w), alpha = sin(w) / (2 * q);
        set(1 + alpha * A, -2 * c, 1 - alpha * A, 1 + alpha / A, -2 * c, 1 - alpha / A);
    }

    void lowShelf(double f0, double gainDb) {
        double A = pow(10.0, gainDb / 40.0);
        double w = 2 * kPi * f0 / kRate, c = cos(w), s = sin(w);
        double alpha = s / 2 * sqrt(2.0);  // shelf slope 1
        double t = 2 * sqrt(A) * alpha;
        set(A * ((A + 1) - (A - 1) * c + t), 2 * A * ((A - 1) - (A + 1) * c), A * ((A + 1) - (A - 1) * c - t),
            (A + 1) + (A - 1) * c + t, -2 * ((A - 1) + (A + 1) * c), (A + 1) + (A - 1) * c - t);
    }
};

// Corrective EQ per bandwidth class: the one table to tune after listening tests.
struct EqBand {
    enum Type { LowShelf, Peak } type;
    double freq;
    double gainDb;
    double q;
};

const EqBand kEqNarrow[] = {
        {EqBand::LowShelf, 200, 4.0, 0},
        {EqBand::Peak, 500, -2.0, 1.0},
        {EqBand::Peak, 2800, 3.0, 0.9},
};
const EqBand kEqWide[] = {
        {EqBand::LowShelf, 180, 3.0, 0},
        {EqBand::Peak, 450, -2.0, 1.0},
        {EqBand::Peak, 3500, 2.0, 0.9},
};

// Radix-2 complex FFT.
struct Fft {
    float cosT[kFft / 2], sinT[kFft / 2];
    uint16_t rev[kFft];

    Fft() {
        for (int i = 0; i < kFft / 2; i++) {
            cosT[i] = (float) cos(2 * kPi * i / kFft);
            sinT[i] = (float) -sin(2 * kPi * i / kFft);
        }
        for (int i = 0; i < kFft; i++) {
            int r = 0;
            for (int b = 0, v = i; b < 9; b++, v >>= 1) r = (r << 1) | (v & 1);
            rev[i] = (uint16_t) r;
        }
    }

    // inverse: conjugate trick, scaled by 1/N.
    void run(float *re, float *im, bool inverse) const {
        if (inverse) {
            for (int i = 0; i < kFft; i++) im[i] = -im[i];
        }
        for (int i = 0; i < kFft; i++) {
            int j = rev[i];
            if (j > i) {
                std::swap(re[i], re[j]);
                std::swap(im[i], im[j]);
            }
        }
        for (int len = 2; len <= kFft; len <<= 1) {
            int half = len / 2, step = kFft / len;
            for (int i = 0; i < kFft; i += len) {
                for (int k = 0; k < half; k++) {
                    float wr = cosT[k * step], wi = sinT[k * step];
                    int a = i + k, b = a + half;
                    float tr = re[b] * wr - im[b] * wi;
                    float ti = re[b] * wi + im[b] * wr;
                    re[b] = re[a] - tr;
                    im[b] = im[a] - ti;
                    re[a] += tr;
                    im[a] += ti;
                }
            }
        }
        if (inverse) {
            float s = 1.0f / kFft;
            for (int i = 0; i < kFft; i++) {
                re[i] *= s;
                im[i] = -im[i] * s;
            }
        }
    }
};

const Fft &fft() {
    static const Fft instance;
    return instance;
}

// Function-local statics are initialised thread-safely: the voice and round chains can start at the same time.
struct WindowTables {
    float sqrtHann[kFft];
    float hann[kFft];
    WindowTables() {
        for (int i = 0; i < kFft; i++) {
            sqrtHann[i] = (float) sqrt(0.5 * (1.0 - cos(2 * kPi * i / kFft)));
            hann[i] = (float) (0.5 * (1.0 - cos(2 * kPi * i / kFft)));
        }
    }
};

const WindowTables &windowTables() {
    static const WindowTables instance;
    return instance;
}

const float *sqrtHann() {
    return windowTables().sqrtHann;
}

const float *hann() {
    return windowTables().hann;
}

inline int16_t toInt16(float v) {
    if (v > 32767.0f) v = 32767.0f;
    if (v < -32768.0f) v = -32768.0f;
    return (int16_t) lrintf(v);
}

// Statistical late-reverberation suppressor (Lebart-style power subtraction) on a 512/256 STFT.
class Dereverb {
public:
    Dereverb(float tailSeconds, float floorDb) {
        float delaySeconds = kReverbDelayHops * (float) kHop / kRate;
        decay_ = expf(-2.0f * 3.0f * logf(10.0f) * delaySeconds / tailSeconds);
        floor_ = powf(10.0f, floorDb / 20.0f);
        history_.assign((size_t) (kReverbDelayHops + 1) * kBins, 0.0f);
        for (int i = 0; i < kBins; i++) gain_[i] = 1.0f;
        memset(prev_, 0, sizeof(prev_));
        memset(ola_, 0, sizeof(ola_));
        out_.pushZeros(kHop);
    }

    // Output is delayed by 2 * kHop samples relative to the input: the STFT puts out the reconstruction of the
    // previous hop, and out_ starts with one hop of zeros.
    void process(const float *in, float *out, int n, bool active) {
        in_.push(in, (size_t) n);
        while (in_.size() >= (size_t) kHop) {
            float hopIn[kHop], hopOut[kHop];
            in_.pop(hopIn, kHop);
            if (active) {
                hop(hopIn, hopOut);
            } else {
                memcpy(hopOut, prev_, sizeof(hopOut));
                memcpy(prev_, hopIn, sizeof(prev_));
                memset(ola_, 0, sizeof(ola_));
            }
            out_.push(hopOut, kHop);
        }
        out_.pop(out, (size_t) n);
    }

private:
    void hop(const float *hopIn, float *hopOut) {
        const float *w = sqrtHann();
        float re[kFft], im[kFft];
        for (int i = 0; i < kHop; i++) {
            re[i] = prev_[i] * w[i];
            re[kHop + i] = hopIn[i] * w[kHop + i];
        }
        memset(im, 0, sizeof(im));
        memcpy(prev_, hopIn, sizeof(prev_));
        fft().run(re, im, false);

        float power[kBins];
        for (int k = 0; k < kBins; k++) power[k] = re[k] * re[k] + im[k] * im[k];
        float smooth[kBins];
        for (int k = 0; k < kBins; k++) {
            float a = power[k > 0 ? k - 1 : 0], c = power[k + 1 < kBins ? k + 1 : kBins - 1];
            smooth[k] = 0.5f * power[k] + 0.25f * (a + c);
        }
        const float *old = &history_[(size_t) historyPos_ * kBins];
        for (int k = 0; k < kBins; k++) {
            float reverb = decay_ * old[k];
            float g = smooth[k] > 1e-3f ? sqrtf(fmaxf(smooth[k] - reverb, 0.0f) / smooth[k]) : 1.0f;
            g = fmaxf(g, floor_);
            gain_[k] = 0.5f * gain_[k] + 0.5f * g;
        }
        memcpy(&history_[(size_t) historyPos_ * kBins], smooth, sizeof(smooth));
        historyPos_ = (historyPos_ + 1) % (kReverbDelayHops + 1);

        for (int k = 0; k < kBins; k++) {
            re[k] *= gain_[k];
            im[k] *= gain_[k];
            if (k > 0 && k < kFft / 2) {
                re[kFft - k] = re[k];
                im[kFft - k] = -im[k];
            }
        }
        fft().run(re, im, true);
        for (int i = 0; i < kFft; i++) re[i] *= w[i];
        for (int i = 0; i < kHop; i++) {
            hopOut[i] = ola_[i] + re[i];
            ola_[i] = re[kHop + i];
        }
    }

    float decay_ = 0.3f, floor_ = 0.35f;
    float gain_[kBins];
    float prev_[kHop];
    float ola_[kHop];
    std::vector<float> history_;
    int historyPos_ = 0;
    Fifo in_, out_;
};

// WebRTC APM used as a capture-only block: either noise suppression or AGC2 with the limiter.
class ApmStage {
public:
    using ApmPtr = decltype(webrtc::AudioProcessingBuilder().Create());

    bool init(const webrtc::AudioProcessing::Config &config) {
        apm_ = webrtc::AudioProcessingBuilder().Create();
        if (!apm_) return false;
        apm_->ApplyConfig(config);
        return true;
    }

    void apply(const webrtc::AudioProcessing::Config &config) {
        if (apm_) apm_->ApplyConfig(config);
    }

    // Float samples (-1..1): no int16 clamp before the AGC2 limiter sees the boosted signal.
    bool process(float *block) {
        float in[kBlock], out[kBlock];
        for (int i = 0; i < kBlock; i++) in[i] = block[i] * (1.0f / 32768.0f);
        const float *src[1] = {in};
        float *dst[1] = {out};
        webrtc::StreamConfig stream(kRate, 1);
        if (apm_->ProcessStream(src, stream, stream, dst) != 0) return false;
        for (int i = 0; i < kBlock; i++) block[i] = out[i] * 32768.0f;
        return true;
    }

private:
    ApmPtr apm_;
};

webrtc::AudioProcessing::Config noiseConfig(webrtc::AudioProcessing::Config::NoiseSuppression::Level level) {
    webrtc::AudioProcessing::Config config;
    config.noise_suppression.enabled = true;
    config.noise_suppression.level = level;
    return config;
}

webrtc::AudioProcessing::Config gainConfig(float maxGainDb, float initialGainDb) {
    webrtc::AudioProcessing::Config config;
    config.gain_controller2.enabled = true;
    config.gain_controller2.adaptive_digital.enabled = true;
    config.gain_controller2.adaptive_digital.headroom_db = 5.0f;
    config.gain_controller2.adaptive_digital.max_gain_db = maxGainDb;
    config.gain_controller2.adaptive_digital.initial_gain_db = initialGainDb;
    config.gain_controller2.adaptive_digital.max_gain_change_db_per_second = 6.0f;
    config.gain_controller2.adaptive_digital.max_output_noise_level_dbfs = -50.0f;
    return config;
}

}  // namespace

struct VoiceEnhancer {
    int profile = 0;
    bool useRnnoise = false;
    bool dropLatency = false;
    int level = VE_LEVEL_FULL;
    int cls = VE_CLASS_UNKNOWN;

    int chainLatency = 0;  // samples inside the stages (RNNoise block, dereverb hop)
    int skip = 0;          // start-up delay still to drop

    // Top-level queues.
    Fifo inq, outq, rawq;
    std::vector<float> analysisq;

    // Stages.
    Biquad highpass;
    Biquad eq[3];
    bool eqActive = false;
    ApmStage noise;
    DenoiseState *rnn = nullptr;
    float rnnWet = 1.0f;
    float dryPrev[kBlock];
    Dereverb *dereverb = nullptr;
    ApmStage gain;

    // Bandwidth extension for narrowband input.
    Biquad bweBand, bweHp1, bweHp2, bweLp, bweTilt;
    float bweBandPower = 0, bweExcitationPower = 0, bweMix = 0;

    // Bandwidth classification.
    float history[kFft];
    int historyFill = 0;
    double bandLow = 0, bandMid = 0, bandHigh = 0;
    int speechBlocks = 0;
    int evalCount = 0;
    int pendingClass = VE_CLASS_UNKNOWN;
    float noiseFloor = 1000.0f;
    float rnnVad = 0.0f;

    // Time budget.
    int64_t usSum = 0;
    int blocks = 0;
    int64_t windowUs = 0;
    int windowBlocks = 0;
    int slowWindows = 0;

    ~VoiceEnhancer() {
        if (rnn) rnnoise_destroy(rnn);
        delete dereverb;
    }

    bool init(int initialClass) {
        cls = initialClass;
        highpass.highpass(profile == VE_PROFILE_PHONE_FAR || cls == VE_CLASS_SWB ? 80.0 : 100.0, 0.7071);
        memset(dryPrev, 0, sizeof(dryPrev));
        memset(history, 0, sizeof(history));
        if (profile == VE_PROFILE_ANALYZE) return true;

        if (useRnnoise) {
            rnn = rnnoise_create(nullptr);
            if (!rnn) return false;
            rnnWet = profile == VE_PROFILE_PHONE_FAR || cls == VE_CLASS_SWB ? 1.0f : 0.7f;
            chainLatency += kBlock;
        } else if (!noise.init(noiseConfig(noiseLevel()))) {
            return false;
        }
        if (profile == VE_PROFILE_PHONE_FAR) {
            dereverb = new Dereverb(0.5f, -9.0f);
            chainLatency += 2 * kHop;
            if (!gain.init(gainConfig(30.0f, 10.0f))) return false;
        } else {
            if (!gain.init(gainConfig(18.0f, 6.0f))) return false;
            configureForClass();
        }
        rawq.pushZeros((size_t) chainLatency);
        outq.pushZeros(kBlock);
        skip = dropLatency ? chainLatency + kBlock : 0;
        return true;
    }

    webrtc::AudioProcessing::Config::NoiseSuppression::Level noiseLevel() const {
        using Level = webrtc::AudioProcessing::Config::NoiseSuppression;
        if (profile == VE_PROFILE_PHONE_FAR) return Level::kHigh;
        return cls == VE_CLASS_NB ? Level::kLow : Level::kModerate;
    }

    void configureForClass() {
        if (profile != VE_PROFILE_HEADSET) return;
        // Super-wideband and wider carries real speech detail: RNNoise output only, no dry mix.
        if (rnn) rnnWet = cls == VE_CLASS_SWB ? 1.0f : 0.7f;
        eqActive = cls == VE_CLASS_NB || cls == VE_CLASS_WB;
        if (eqActive) {
            const EqBand *table = cls == VE_CLASS_NB ? kEqNarrow : kEqWide;
            for (int i = 0; i < 3; i++) {
                if (table[i].type == EqBand::LowShelf) {
                    eq[i].lowShelf(table[i].freq, table[i].gainDb);
                } else {
                    eq[i].peaking(table[i].freq, table[i].gainDb, table[i].q);
                }
            }
        }
        if (cls == VE_CLASS_NB) {
            bweBand.bandpass(3000.0, 1.5);
            bweHp1.highpass(4000.0, 0.7071);
            bweHp2.highpass(4000.0, 0.7071);
            bweLp.lowpass(8000.0, 0.7071);
            bweTilt.lowpass(5000.0, 0.7071);
        }
        if (!rnn) noise.apply(noiseConfig(noiseLevel()));
    }

    // Measures how wide the incoming speech is; the first answer is taken at once, later changes need two matches.
    void classify(const float *block) {
        memmove(history, history + kBlock, (kFft - kBlock) * sizeof(float));
        memcpy(history + kFft - kBlock, block, kBlock * sizeof(float));
        if (historyFill < kFft) {
            historyFill += kBlock;
            return;
        }
        float sum = 0;
        for (int i = 0; i < kBlock; i++) sum += block[i] * block[i];
        float rms = sqrtf(sum / kBlock);
        noiseFloor = rms < noiseFloor ? rms : noiseFloor * 1.0005f;
        if (!(rms > 4.0f * noiseFloor && rms > 120.0f)) return;

        const float *w = hann();
        float re[kFft], im[kFft];
        for (int i = 0; i < kFft; i++) {
            re[i] = history[i] * w[i];
            im[i] = 0;
        }
        fft().run(re, im, false);
        for (int k = 4; k <= 36; k++) bandLow += re[k] * re[k] + im[k] * im[k];
        for (int k = 39; k <= 74; k++) bandMid += re[k] * re[k] + im[k] * im[k];
        for (int k = 81; k <= 128; k++) bandHigh += re[k] * re[k] + im[k] * im[k];
        speechBlocks++;
        int threshold = evalCount == 0 ? 100 : 200;
        if (speechBlocks < threshold) return;

        double low = bandLow + 1e-9;
        int candidate = bandMid < low * 3.16e-4 ? VE_CLASS_NB : (bandHigh < low * 3.16e-4 ? VE_CLASS_WB : VE_CLASS_SWB);
        bandLow = bandMid = bandHigh = 0;
        speechBlocks = 0;
        bool first = evalCount == 0;
        evalCount++;
        if (candidate != cls && (first || candidate == pendingClass)) {
            cls = candidate;
            if (profile == VE_PROFILE_HEADSET) configureForClass();
        }
        pendingClass = candidate;
    }

    // Per-sample narrowband to wideband extension: harmonics from the rectified 2-4 kHz band.
    void extendBandwidth(float *x, float zcr, bool speech) {
        float target = !speech ? 0.0f : (zcr > 0.07f ? 0.126f : 0.063f);
        for (int i = 0; i < kBlock; i++) {
            float band = bweBand.run(x[i]);
            float rect = fabsf(band);
            float e = bweTilt.run(bweLp.run(bweHp2.run(bweHp1.run(rect))));
            bweBandPower += 0.002f * (band * band - bweBandPower);
            bweExcitationPower += 0.002f * (e * e - bweExcitationPower);
            bweMix += 0.002f * (target - bweMix);
            float norm = sqrtf(bweBandPower / (bweExcitationPower + 1e-3f));
            x[i] += bweMix * norm * e;
        }
    }

    bool runBlock(float *x) {
        for (int i = 0; i < kBlock; i++) x[i] = highpass.run(x[i]);

        if (rnn) {
            float out[kBlock], filtered[kBlock];
            memcpy(filtered, x, sizeof(filtered));
            rnnVad = rnnoise_process_frame(rnn, out, x);
            for (int i = 0; i < kBlock; i++) {
                x[i] = rnnWet * out[i] + (1.0f - rnnWet) * dryPrev[i];
            }
            // The dry copy follows RNNoise's one-block delay.
            memcpy(dryPrev, filtered, sizeof(filtered));
        } else if (!noise.process(x)) {
            return false;
        }

        float sum = 0;
        int crossings = 0;
        for (int i = 0; i < kBlock; i++) {
            sum += x[i] * x[i];
            if (i > 0 && ((x[i] >= 0) != (x[i - 1] >= 0))) crossings++;
        }
        float rms = sqrtf(sum / kBlock);
        bool speech = rnn ? rnnVad > 0.6f : rms > 80.0f;

        if (dereverb) {
            float out[kBlock];
            dereverb->process(x, out, kBlock, level >= VE_LEVEL_FULL);
            memcpy(x, out, sizeof(out));
        }
        if (eqActive) {
            for (int i = 0; i < kBlock; i++) {
                x[i] = eq[2].run(eq[1].run(eq[0].run(x[i])));
            }
        }
        if (profile == VE_PROFILE_HEADSET && cls == VE_CLASS_NB && level >= VE_LEVEL_FULL) {
            extendBandwidth(x, (float) crossings / kBlock, speech);
        }
        return gain.process(x);
    }

    int processBlocks(int16_t *io, int n) {
        for (int i = 0; i < n; i++) inq.data.push_back((float) io[i]);
        while (inq.size() >= (size_t) kBlock) {
            float block[kBlock], original[kBlock], delayed[kBlock];
            inq.pop(block, kBlock);
            memcpy(original, block, sizeof(block));
            rawq.push(original, kBlock);
            rawq.pop(delayed, kBlock);

            if (profile == VE_PROFILE_HEADSET) classify(original);
            bool ok = level != VE_LEVEL_BYPASS;
            int64_t started = nowMicros();
            if (ok) ok = runBlock(block);
            if (ok) {
                // Tests the bits: the library builds with -ffast-math, which folds std::isfinite to true.
                for (int i = 0; i < kBlock; i++) {
                    uint32_t bits;
                    memcpy(&bits, &block[i], sizeof(bits));
                    if ((bits & 0x7f800000u) == 0x7f800000u) {
                        ok = false;
                        break;
                    }
                }
            }
            if (level != VE_LEVEL_BYPASS) {
                int64_t elapsed = nowMicros() - started;
                usSum += elapsed;
                blocks++;
                windowUs += elapsed;
                if (++windowBlocks == 100) {
                    // 5 ms per 20 ms frame is 2.5 ms per block; the window is the last 100 blocks, whatever the reports take.
                    if (windowUs / windowBlocks > 2500) {
                        if (++slowWindows == 1) level = VE_LEVEL_REDUCED;
                        else level = VE_LEVEL_BYPASS;
                    } else {
                        slowWindows = 0;
                    }
                    windowUs = 0;
                    windowBlocks = 0;
                }
            }
            if (!ok) {
                level = VE_LEVEL_BYPASS;
                memcpy(block, delayed, sizeof(block));
            }
            outq.push(block, kBlock);
        }
        if (outq.size() < (size_t) n) return -1;
        std::vector<float> out((size_t) n);
        outq.pop(out.data(), (size_t) n);
        int drop = std::min(skip, n);
        skip -= drop;
        int written = n - drop;
        for (int i = 0; i < written; i++) io[i] = toInt16(out[(size_t) (drop + i)]);
        return written;
    }

    void analyze(const int16_t *io, int n) {
        for (int i = 0; i < n; i++) analysisq.push_back((float) io[i]);
        size_t used = 0;
        while (analysisq.size() - used >= (size_t) kBlock) {
            classify(analysisq.data() + used);
            used += kBlock;
        }
        analysisq.erase(analysisq.begin(), analysisq.begin() + used);
    }
};

extern "C" {

VoiceEnhancer *ve_create(int profile, int initial_class, int use_rnnoise, int drop_latency) {
    if (profile != VE_PROFILE_HEADSET && profile != VE_PROFILE_PHONE_FAR && profile != VE_PROFILE_ANALYZE) {
        return nullptr;
    }
    VoiceEnhancer *ve = new VoiceEnhancer();
    ve->profile = profile;
    ve->useRnnoise = use_rnnoise != 0;
    ve->dropLatency = drop_latency != 0;
    if (!ve->init(initial_class)) {
        delete ve;
        return nullptr;
    }
    return ve;
}

void ve_destroy(VoiceEnhancer *ve) {
    delete ve;
}

int ve_process(VoiceEnhancer *ve, int16_t *io, int n) {
    if (!ve || !io || n < 0) return -1;
    if (ve->profile == VE_PROFILE_ANALYZE) {
        ve->analyze(io, n);
        return n;
    }
    return ve->processBlocks(io, n);
}

int ve_flush(VoiceEnhancer *ve, int16_t *out, int cap) {
    if (!ve || !out || cap < 0 || ve->profile == VE_PROFILE_ANALYZE || !ve->dropLatency) return 0;
    // Zeros push the remaining speech out of the chain; the leftover is the tail of those zeros.
    int want = std::min(cap, ve->chainLatency + kBlock);
    int pending = (int) (ve->inq.size() % kBlock);
    int feed = want + (pending ? kBlock - pending : 0);
    std::vector<int16_t> zeros((size_t) feed, 0);
    int produced = ve->processBlocks(zeros.data(), feed);
    if (produced < 0) return 0;
    produced = std::min(produced, want);
    memcpy(out, zeros.data(), (size_t) produced * sizeof(int16_t));
    return produced;
}

int ve_delay_samples(const VoiceEnhancer *ve) {
    if (!ve || ve->profile == VE_PROFILE_ANALYZE) return 0;
    return ve->chainLatency + kBlock;
}

int ve_get_class(const VoiceEnhancer *ve) {
    return ve ? ve->cls : VE_CLASS_UNKNOWN;
}

int ve_get_level(const VoiceEnhancer *ve) {
    return ve ? ve->level : VE_LEVEL_BYPASS;
}

void ve_set_level(VoiceEnhancer *ve, int level) {
    if (ve && level >= VE_LEVEL_BYPASS && level <= VE_LEVEL_FULL && level < ve->level) {
        ve->level = level;
    }
}

int ve_take_average_block_micros(VoiceEnhancer *ve) {
    if (!ve || ve->blocks == 0) return 0;
    int average = (int) (ve->usSum / ve->blocks);
    ve->usSum = 0;
    ve->blocks = 0;
    return average;
}

}  // extern "C"

