#import <TargetConditionals.h>
#if TARGET_OS_OSX

#ifndef TGCALLS_SCREEN_CAPTURE_KIT_CONFIGURATION_H
#define TGCALLS_SCREEN_CAPTURE_KIT_CONFIGURATION_H

#import <Foundation/Foundation.h>
#import <CoreMedia/CoreMedia.h>
#import <ScreenCaptureKit/ScreenCaptureKit.h>

NS_ASSUME_NONNULL_BEGIN

CGSize TGScreenCaptureOutputSize(CGSize sourcePixelSize, CGSize boundingSize);
SCStreamConfiguration *TGScreenCaptureStreamConfiguration(CGSize outputSize, double fps, BOOL showsCursor) API_AVAILABLE(macos(14.0));
BOOL TGScreenCaptureSampleBufferIsComplete(CMSampleBufferRef sampleBuffer) API_AVAILABLE(macos(14.0));
void TGScreenCaptureTagFrame(CVPixelBufferRef pixelBuffer) API_AVAILABLE(macos(12.0));

@interface TGScreenCaptureWindowCandidate : NSObject
@property (nonatomic, readonly) uint32_t windowID;
@property (nonatomic, readonly) NSInteger layer;
@property (nonatomic, readonly) BOOL onScreen;
@property (nonatomic, readonly, nullable) NSString *title;
@property (nonatomic, readonly, nullable) NSString *ownerName;
@property (nonatomic, readonly) pid_t ownerPid;
- (instancetype)initWithWindowID:(uint32_t)windowID layer:(NSInteger)layer onScreen:(BOOL)onScreen title:(nullable NSString *)title ownerName:(nullable NSString *)ownerName ownerPid:(pid_t)ownerPid;
@end

@interface TGScreenCaptureListedSource : NSObject
@property (nonatomic, readonly) long long uniqueId;
@property (nonatomic, readonly) NSString *title;
- (instancetype)initWithUniqueId:(long long)uniqueId title:(NSString *)title;
@end

NSArray<TGScreenCaptureListedSource *> *TGScreenCaptureListedWindows(NSArray<TGScreenCaptureWindowCandidate *> *windows);
CGSize TGScreenCaptureFillSize(CGSize sourcePixelSize, CGSize boxPixelSize);
BOOL TGScreenCapturePreviewUsesScreenshots(double fps);
NSTimeInterval TGScreenCaptureScreenshotInterval(double fps);
SCStreamConfiguration *TGScreenCaptureThumbnailConfiguration(CGSize sourcePixelSize, CGSize boxPixelSize, BOOL showsCursor) API_AVAILABLE(macos(14.0));
SCStreamConfiguration *TGScreenCapturePreviewConfiguration(CGSize sourcePixelSize, CGSize boxPixelSize, double fps, BOOL showsCursor) API_AVAILABLE(macos(14.0));

NS_ASSUME_NONNULL_END

#endif

#endif // TARGET_OS_OSX
