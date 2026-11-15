#ifndef NANOGRAM_VOICE_ENHANCE_H
#define NANOGRAM_VOICE_ENHANCE_H

/*
 * Voice enhancement for recordings (the "Voice enhancement" switch in Nanogram features).
 *
 * Plain mono 48 kHz int16 in, int16 out, no Android types, so a host harness can run it.
 * Profiles:
 *   HEADSET    Bluetooth microphone (HFP/mSBC/CVSD or LE Audio): high-pass, noise suppression, corrective EQ,
 *              bandwidth extension for narrowband input, AGC2 and limiter.
 *   PHONE_FAR  Phone microphone far from the mouth: high-pass, stronger noise suppression, dereverberation, AGC2
 *              with more gain, limiter.
 *   ANALYZE    No change to the audio; only measures the bandwidth class (narrowband / wideband / wider) for logs.
 *
 * SHARED by iOS, macOS and Android (one source, tgcalls fork, directory voice_enhance/). Nothing here knows a platform:
 * the host app feeds mono 48 kHz int16 PCM from its recorder. Per platform:
 *   Android  JNI glue jni/voice_enhance_jni.cc (AudioVoiceEnhancer) calls these functions.
 *   iOS/Mac  not wired yet. Call ve_create when a voice message / round video recording starts (profile HEADSET for a
 *            Bluetooth route, PHONE_FAR for the built-in mic, ANALYZE to only measure), ve_process on every capture
 *            buffer, ve_flush at the end (drop_latency=1), ve_destroy after. Build voice_enhance.cc with WebRTC's
 *            audio_processing.h and RNNoise (rnnoise.h) on the include path and link the same WebRTC and RNNoise as
 *            tgcalls; do not put tests/voice_enhance/stub on the include path.
 * Host test: tests/voice_enhance/run.sh.
 */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
    VE_PROFILE_HEADSET = 1,
    VE_PROFILE_PHONE_FAR = 2,
    VE_PROFILE_ANALYZE = 3,
};

enum {
    VE_CLASS_UNKNOWN = 0,
    VE_CLASS_NB = 1,   // about 4 kHz (CVSD)
    VE_CLASS_WB = 2,   // about 8 kHz (mSBC)
    VE_CLASS_SWB = 3,  // wider (LE Audio, built-in microphone)
};

enum {
    VE_LEVEL_BYPASS = 0,   // pass-through; the delay stays the same so the stream is continuous
    VE_LEVEL_REDUCED = 1,  // no bandwidth extension, no dereverberation
    VE_LEVEL_FULL = 2,
};

typedef struct VoiceEnhancer VoiceEnhancer;

/*
 * use_rnnoise: use RNNoise instead of WebRTC noise suppression (the user's existing choice).
 * drop_latency: ve_process removes the chain delay from the start of the stream (and ve_flush returns the tail),
 *   so the output has exactly as many samples as the input. Without it the output is delayed by
 *   ve_delay_samples and the caller shifts timestamps.
 * Returns NULL if the profile is unknown or something could not be created.
 */
VoiceEnhancer *ve_create(int profile, int initial_class, int use_rnnoise, int drop_latency);
void ve_destroy(VoiceEnhancer *ve);

/*
 * Processes n samples in place. Returns the number of output samples written at the start of io (n, or fewer while
 * the start-up delay is being dropped), or -1 on failure.
 */
int ve_process(VoiceEnhancer *ve, int16_t *io, int n);

/* Only with drop_latency: writes the samples still inside the chain. Returns the count (at most cap). */
int ve_flush(VoiceEnhancer *ve, int16_t *out, int cap);

int ve_delay_samples(const VoiceEnhancer *ve);
int ve_get_class(const VoiceEnhancer *ve);
int ve_get_level(const VoiceEnhancer *ve);
void ve_set_level(VoiceEnhancer *ve, int level);
/* Average processing time per 10 ms block since the last call; 0 if no block ran. Resets the counters. */
int ve_take_average_block_micros(VoiceEnhancer *ve);

#ifdef __cplusplus
}
#endif

#endif
