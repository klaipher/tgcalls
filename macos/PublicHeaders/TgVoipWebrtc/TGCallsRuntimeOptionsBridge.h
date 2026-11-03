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

@end

NS_ASSUME_NONNULL_END

#endif
