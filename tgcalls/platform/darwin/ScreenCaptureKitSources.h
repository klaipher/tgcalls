#import <TargetConditionals.h>
#if TARGET_OS_OSX

#ifndef TGCALLS_SCREEN_CAPTURE_KIT_SOURCES_H
#define TGCALLS_SCREEN_CAPTURE_KIT_SOURCES_H

#import <AppKit/AppKit.h>
#import <ScreenCaptureKit/ScreenCaptureKit.h>

#import "ScreenCaptureKitConfiguration.h"

NS_ASSUME_NONNULL_BEGIN

API_AVAILABLE(macos(14.0))
@interface TGScreenCaptureContentCache : NSObject

+ (instancetype)shared;
- (instancetype)initWithFetcher:(void (^)(void (^completion)(SCShareableContent * _Nullable content)))fetcher;
- (void)refreshWithCompletion:(dispatch_block_t)completion;
- (nullable SCShareableContent *)contentWaitingAtMost:(NSTimeInterval)timeout;
- (NSArray<TGScreenCaptureListedSource *> *)displaysWaitingAtMost:(NSTimeInterval)timeout;
- (NSArray<TGScreenCaptureListedSource *> *)windowsWaitingAtMost:(NSTimeInterval)timeout;

@end

API_AVAILABLE(macos(14.0))
@interface TGScreenCapturePreviewView : NSView

- (instancetype)initWithSourceId:(long long)sourceId isWindow:(BOOL)isWindow boxPixelSize:(CGSize)boxPixelSize fps:(double)fps showsCursor:(BOOL)showsCursor;
- (void)start;
- (void)stop;

@end

NS_ASSUME_NONNULL_END

#endif

#endif // TARGET_OS_OSX
