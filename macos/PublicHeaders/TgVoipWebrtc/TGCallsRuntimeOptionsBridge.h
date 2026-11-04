#ifndef TGCallsRuntimeOptionsBridge_h
#define TGCallsRuntimeOptionsBridge_h

#import <Foundation/Foundation.h>

NS_ASSUME_NONNULL_BEGIN

// The macOS app's way into tgcalls::CallsRuntimeSwitches (the same switches, with the same names and defaults, the iOS app
// sets from its own bridge). Every setter is local only and never changes what is negotiated or sent on the wire. All
// switches default to off; the Debug Settings section "Streams & Calls" lists them with their descriptions.
@interface TGCallsRuntimeOptionsBridge : NSObject

// Applies from the next decoder / encoder / camera start.
+ (void)setLivestreamHardwareDecodingEnabled:(BOOL)enabled;
+ (void)setHardwareCropEnabled:(BOOL)enabled;
+ (void)setNativeDecodeFormatEnabled:(BOOL)enabled;
+ (void)setScreencastHardwareEncoderEnabled:(BOOL)enabled;
// Group calls: 0 = both suppressors (default), 1 = WebRTC suppressor only, 2 = RNNoise only. Applies to the next call.
+ (void)setGroupNoiseMode:(NSInteger)mode;
// The thermal video ladder switch. Applies live: the current level is applied again to every camera.
+ (void)setThermalVideoLadderEnabled:(BOOL)enabled;
// State the app reports. Level 0 nominal ... 3 critical (see CallSystemPolicy.h); only limits the camera while the ladder is on.
+ (void)setThermalLevel:(NSInteger)level;
// While true, livestream video parts are not decoded (audio keeps playing). Set when the stream window is hidden.
+ (void)setLivestreamVideoSuspended:(BOOL)suspended;

@end

NS_ASSUME_NONNULL_END

#endif
