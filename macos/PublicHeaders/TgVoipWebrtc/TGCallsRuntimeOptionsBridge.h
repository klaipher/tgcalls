#ifndef TGCallsRuntimeOptionsBridge_h
#define TGCallsRuntimeOptionsBridge_h

#import <Foundation/Foundation.h>

NS_ASSUME_NONNULL_BEGIN

@interface TGCallsRuntimeOptionsBridge : NSObject

+ (void)setLivestreamHardwareDecodeEnabled:(BOOL)enabled;
+ (void)setCameraFormatV2Enabled:(BOOL)enabled;
+ (void)setHardwareCropEnabled:(BOOL)enabled;
+ (void)setNativeDecodeFormatEnabled:(BOOL)enabled;
+ (void)setScreencastHardwareEncoderEnabled:(BOOL)enabled;
// Kill switch for H.264 low-latency rate control (default on). Applies to the next encoder session.
+ (void)setEncoderLowLatencyEnabled:(BOOL)enabled;
// Group calls keep the processed signal of an 8/16 kHz (Bluetooth) microphone (default on). Applies to the next group call.
+ (void)setGroupNarrowbandProcessingEnabled:(BOOL)enabled;

// Batch B audio. All four apply to the next call and never change what is negotiated.
// mode: 0 = both suppressors (default), 1 = WebRTC suppressor only, 2 = RNNoise only.
+ (void)setGroupNoiseMode:(NSInteger)mode;
// mode: 0 = analog AGC (default), 1 = digital AGC2 that leaves the system mic volume alone.
+ (void)setMicGainMode:(NSInteger)mode;
// 0 = encoder default; 5...10 sets the local Opus encoder complexity.
+ (void)setOpusComplexity:(NSInteger)complexity;
+ (void)setLowBitrateApmRateEnabled:(BOOL)enabled;
// Whether the macOS audio worker threads join the device's real-time workgroup.
+ (void)setAudioWorkgroupEnabled:(BOOL)enabled;

// Batch C. Local-only; nothing here changes what is negotiated or sent on the wire.
// Raises the tgcalls media, worker and network threads to user-initiated QoS (false restores the default).
+ (void)setThreadQoSEnabled:(BOOL)enabled;
// While true, livestream video parts are not decoded (audio keeps playing). Set when the stream window is hidden.
+ (void)setLivestreamVideoSuspended:(BOOL)suspended;
// Caps the outgoing camera video (not screen sharing). Zero means no limit. maxShortSide is in pixels, e.g. 540.
+ (void)setOutgoingCameraLimitWithMaxShortSide:(NSInteger)maxShortSide maxFps:(NSInteger)maxFps;

@end

NS_ASSUME_NONNULL_END

#endif
