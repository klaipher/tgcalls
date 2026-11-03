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

@end

NS_ASSUME_NONNULL_END

#endif
