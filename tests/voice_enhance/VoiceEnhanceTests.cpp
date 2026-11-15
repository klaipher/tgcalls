// Host test for voice_enhance (the shared headset / far-microphone voice chain). Runs against the APM stand-in in
// stub/, so it checks the core's own plumbing and RNNoise path, not WebRTC's noise suppression.
#include <voice_enhance/voice_enhance.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

static int failures = 0;
#define CHECK(cond, ...) do { if (!(cond)) { failures++; printf("FAIL %s:%d %s: ", __FILE__, __LINE__, #cond); printf(__VA_ARGS__); printf("\n"); } } while (0)

static std::vector<int16_t> makeSignal(int n) {
    std::vector<int16_t> s(n);
    unsigned seed = 12345;
    for (int i = 0; i < n; i++) {
        seed = seed * 1664525u + 1013904223u;
        double noise = ((int) (seed >> 16) - 32768) / 32768.0 * 0.05;
        double tone = 0.3 * sin(2 * M_PI * 440.0 * i / 48000.0) + 0.1 * sin(2 * M_PI * 2000.0 * i / 48000.0);
        s[i] = (int16_t) lrint((tone + noise) * 32767.0);
    }
    return s;
}

static void runRoute(int profile, int cls, int rnn, int drop, const char *name) {
    const int total = 48000;  // 1 s
    std::vector<int16_t> in = makeSignal(total), out;
    VoiceEnhancer *ve = ve_create(profile, cls, rnn, drop);
    CHECK(ve != nullptr, "%s create", name);
    if (!ve) return;
    int delay = ve_delay_samples(ve);
    int i = 0, chunk = 0;
    const int chunks[] = {480, 441, 1024, 7, 960, 333};  // odd sizes too: the core buffers into 10 ms blocks
    while (i < total) {
        int n = chunks[chunk++ % 6];
        if (i + n > total) n = total - i;
        std::vector<int16_t> buf(in.begin() + i, in.begin() + i + n);
        int r = ve_process(ve, buf.data(), n);
        CHECK(r >= 0 && r <= n, "%s process returned %d", name, r);
        if (r > 0) out.insert(out.end(), buf.begin(), buf.begin() + r);
        i += n;
    }
    if (drop && profile != VE_PROFILE_ANALYZE) {
        std::vector<int16_t> tail(delay + 4800);
        int f = ve_flush(ve, tail.data(), (int) tail.size());
        CHECK(f >= 0, "%s flush", name);
        out.insert(out.end(), tail.begin(), tail.begin() + f);
    }
    // int16 output cannot hold NaN/Inf, but the chain must not have fallen into bypass because of them.
    CHECK(ve_get_level(ve) != VE_LEVEL_BYPASS || profile == VE_PROFILE_ANALYZE, "%s fell to bypass (level %d)", name, ve_get_level(ve));
    if (profile == VE_PROFILE_ANALYZE) {
        CHECK(delay == 0, "%s analyze delay %d", name, delay);
        CHECK((int) out.size() == total && memcmp(out.data(), in.data(), total * 2) == 0, "%s analyze must not change audio", name);
        CHECK(ve_get_class(ve) >= VE_CLASS_UNKNOWN && ve_get_class(ve) <= VE_CLASS_SWB, "%s class", name);
    } else {
        CHECK(delay >= 480, "%s delay %d", name, delay);
        if (drop) {
            CHECK((int) out.size() == total, "%s drop_latency output %zu != %d", name, out.size(), total);
        } else {
            CHECK((int) out.size() <= total && (int) out.size() >= total - 480, "%s output %zu", name, out.size());
        }
        double e = 0;
        for (int16_t v : out) e += (double) v * v;
        CHECK(e > 0, "%s output is silent", name);
    }
    printf("ok %-28s delay=%d out=%zu level=%d class=%d\n", name, delay, out.size(), ve_get_level(ve), ve_get_class(ve));
    ve_destroy(ve);
}

int main() {
    struct { int profile, cls; const char *name; } routes[] = {
        {VE_PROFILE_HEADSET, VE_CLASS_NB, "headset-narrowband"},
        {VE_PROFILE_HEADSET, VE_CLASS_WB, "headset-wideband"},
        {VE_PROFILE_HEADSET, VE_CLASS_SWB, "headset-superwide"},
        {VE_PROFILE_PHONE_FAR, VE_CLASS_SWB, "phone-far"},
        {VE_PROFILE_ANALYZE, VE_CLASS_UNKNOWN, "analyze"},
    };
    for (auto &r : routes) {
        for (int rnn = 0; rnn < 2; rnn++) {
            for (int drop = 0; drop < 2; drop++) {
                char name[64];
                snprintf(name, sizeof name, "%s rnn=%d drop=%d", r.name, rnn, drop);
                runRoute(r.profile, r.cls, rnn, drop, name);
            }
        }
    }

    // Bad input: the API refuses instead of crashing; unknown profile creates nothing.
    CHECK(ve_create(0, 0, 0, 0) == nullptr && ve_create(99, 0, 0, 0) == nullptr, "bad profile");
    CHECK(ve_process(nullptr, nullptr, 10) == -1, "null handle");
    VoiceEnhancer *ve = ve_create(VE_PROFILE_HEADSET, VE_CLASS_WB, 0, 0);
    int16_t pcm[480] = {0};
    CHECK(ve_process(ve, nullptr, 480) == -1, "null buffer");
    CHECK(ve_process(ve, pcm, -1) == -1, "negative count");
    CHECK(ve_flush(ve, pcm, 480) == 0, "flush without drop_latency");
    CHECK(ve_delay_samples(nullptr) == 0 && ve_get_level(nullptr) == VE_LEVEL_BYPASS, "null getters");

    // Level control: only downgrades; bypass is pass-through shifted by the delay, and stays continuous.
    CHECK(ve_get_level(ve) == VE_LEVEL_FULL, "starts at full");
    ve_set_level(ve, 7);
    CHECK(ve_get_level(ve) == VE_LEVEL_FULL, "invalid level ignored");
    ve_set_level(ve, VE_LEVEL_REDUCED);
    CHECK(ve_get_level(ve) == VE_LEVEL_REDUCED, "reduced");
    ve_set_level(ve, VE_LEVEL_FULL);
    CHECK(ve_get_level(ve) == VE_LEVEL_REDUCED, "level never goes back up");
    ve_set_level(ve, VE_LEVEL_BYPASS);
    std::vector<int16_t> sig = makeSignal(48000), res;
    int delay = ve_delay_samples(ve);
    for (int i = 0; i + 480 <= 48000; i += 480) {
        int16_t b[480];
        memcpy(b, sig.data() + i, sizeof b);
        int r = ve_process(ve, b, 480);
        if (r > 0) res.insert(res.end(), b, b + r);
    }
    bool same = delay > 0 && (int) res.size() >= delay;
    for (size_t k = (size_t) delay + 480; same && k < res.size(); k++) if (res[k] != sig[k - delay]) same = false;
    CHECK(same, "bypass should be the input delayed by %d samples", delay);
    int us = ve_take_average_block_micros(ve);
    CHECK(us >= 0 && ve_take_average_block_micros(ve) == 0, "block timing resets after reading");
    ve_destroy(ve);

    // Re-creating after destroy gives a fresh chain (the library has no in-place reset).
    VoiceEnhancer *a = ve_create(VE_PROFILE_HEADSET, VE_CLASS_WB, 1, 1);
    VoiceEnhancer *b = ve_create(VE_PROFILE_HEADSET, VE_CLASS_WB, 1, 1);
    std::vector<int16_t> s1 = makeSignal(9600), s2 = s1;
    ve_process(a, s1.data(), 9600);
    ve_process(b, s2.data(), 9600);
    CHECK(memcmp(s1.data(), s2.data(), s1.size() * 2) == 0, "two fresh chains must give identical output");
    ve_destroy(a);
    ve_destroy(b);

    printf(failures ? "FAILED (%d)\n" : "all passed\n", failures);
    return failures ? 1 : 0;
}
