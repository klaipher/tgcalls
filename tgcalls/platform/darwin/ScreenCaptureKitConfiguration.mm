#import <TargetConditionals.h>
#if TARGET_OS_OSX

#import "ScreenCaptureKitConfiguration.h"

#include <cmath>

CGSize TGScreenCaptureOutputSize(CGSize sourcePixelSize, CGSize boundingSize) {
    if (sourcePixelSize.width <= 0.0 || sourcePixelSize.height <= 0.0) {
        return CGSizeMake(2.0, 2.0);
    }
    CGFloat scale = MIN(1.0, MIN(boundingSize.width / sourcePixelSize.width, boundingSize.height / sourcePixelSize.height));
    CGFloat width = MAX(2.0, std::floor(sourcePixelSize.width * scale / 2.0) * 2.0);
    CGFloat height = MAX(2.0, std::floor(sourcePixelSize.height * scale / 2.0) * 2.0);
    return CGSizeMake(width, height);
}

SCStreamConfiguration *TGScreenCaptureStreamConfiguration(CGSize outputSize, double fps, BOOL showsCursor) {
    SCStreamConfiguration *configuration = [[SCStreamConfiguration alloc] init];
    configuration.width = (size_t)outputSize.width;
    configuration.height = (size_t)outputSize.height;
    configuration.pixelFormat = kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange;
    configuration.colorMatrix = kCGDisplayStreamYCbCrMatrix_ITU_R_601_4;
    configuration.colorSpaceName = kCGColorSpaceSRGB;
    configuration.minimumFrameInterval = CMTimeMake(1, (int32_t)MAX(1.0, std::round(fps)));
    configuration.queueDepth = 4;
    configuration.showsCursor = showsCursor;
    configuration.scalesToFit = YES;
    return configuration;
}

BOOL TGScreenCaptureSampleBufferIsComplete(CMSampleBufferRef sampleBuffer) {
    if (!CMSampleBufferIsValid(sampleBuffer) || CMSampleBufferGetImageBuffer(sampleBuffer) == NULL) {
        return NO;
    }
    CFArrayRef attachments = CMSampleBufferGetSampleAttachmentsArray(sampleBuffer, false);
    if (attachments == NULL || CFArrayGetCount(attachments) == 0) {
        return NO;
    }
    NSDictionary *info = (__bridge NSDictionary *)CFArrayGetValueAtIndex(attachments, 0);
    NSNumber *status = info[SCStreamFrameInfoStatus];
    return status != nil && status.integerValue == SCFrameStatusComplete;
}

void TGScreenCaptureTagFrame(CVPixelBufferRef pixelBuffer) {
    CFTypeRef matrix = CVBufferCopyAttachment(pixelBuffer, kCVImageBufferYCbCrMatrixKey, NULL);
    if (matrix != NULL) {
        CFRelease(matrix);
        return;
    }
    CVBufferSetAttachment(pixelBuffer, kCVImageBufferYCbCrMatrixKey, kCVImageBufferYCbCrMatrix_ITU_R_601_4, kCVAttachmentMode_ShouldPropagate);
}

@implementation TGScreenCaptureWindowCandidate

- (instancetype)initWithWindowID:(uint32_t)windowID layer:(NSInteger)layer onScreen:(BOOL)onScreen title:(NSString *)title ownerName:(NSString *)ownerName ownerPid:(pid_t)ownerPid {
    self = [super init];
    if (self != nil) {
        _windowID = windowID;
        _layer = layer;
        _onScreen = onScreen;
        _title = [title copy];
        _ownerName = [ownerName copy];
        _ownerPid = ownerPid;
    }
    return self;
}

@end

@implementation TGScreenCaptureListedSource

- (instancetype)initWithUniqueId:(long long)uniqueId title:(NSString *)title {
    self = [super init];
    if (self != nil) {
        _uniqueId = uniqueId;
        _title = [title copy];
    }
    return self;
}

@end

NSArray<TGScreenCaptureListedSource *> *TGScreenCaptureListedWindows(NSArray<TGScreenCaptureWindowCandidate *> *windows) {
    NSMutableArray<TGScreenCaptureWindowCandidate *> *eligible = [[NSMutableArray alloc] init];
    NSMutableSet<NSNumber *> *pidsWithTitles = [[NSMutableSet alloc] init];
    for (TGScreenCaptureWindowCandidate *window in windows) {
        if (window.windowID == 0 || window.layer != 0 || !window.onScreen) {
            continue;
        }
        if ([window.title isEqualToString:@"StatusIndicator"] && [window.ownerName isEqualToString:@"Window Server"]) {
            continue;
        }
        [eligible addObject:window];
        if (window.title.length != 0) {
            [pidsWithTitles addObject:@(window.ownerPid)];
        }
    }
    NSMutableArray<TGScreenCaptureListedSource *> *result = [[NSMutableArray alloc] init];
    NSMutableSet<NSNumber *> *pidsWithUntitled = [[NSMutableSet alloc] init];
    for (TGScreenCaptureWindowCandidate *window in eligible) {
        if (window.title.length != 0) {
            [result addObject:[[TGScreenCaptureListedSource alloc] initWithUniqueId:window.windowID title:window.title]];
        } else if (window.ownerName.length != 0 && ![pidsWithTitles containsObject:@(window.ownerPid)] && ![pidsWithUntitled containsObject:@(window.ownerPid)]) {
            [pidsWithUntitled addObject:@(window.ownerPid)];
            [result addObject:[[TGScreenCaptureListedSource alloc] initWithUniqueId:window.windowID title:window.ownerName]];
        }
    }
    return result;
}

CGSize TGScreenCaptureFillSize(CGSize sourcePixelSize, CGSize boxPixelSize) {
    if (sourcePixelSize.width <= 0.0 || sourcePixelSize.height <= 0.0) {
        return CGSizeMake(2.0, 2.0);
    }
    CGFloat scale = MIN(1.0, MAX(boxPixelSize.width / sourcePixelSize.width, boxPixelSize.height / sourcePixelSize.height));
    CGFloat width = MAX(2.0, std::floor(sourcePixelSize.width * scale / 2.0) * 2.0);
    CGFloat height = MAX(2.0, std::floor(sourcePixelSize.height * scale / 2.0) * 2.0);
    return CGSizeMake(width, height);
}

BOOL TGScreenCapturePreviewUsesScreenshots(double fps) {
    return fps <= 2.0;
}

NSTimeInterval TGScreenCaptureScreenshotInterval(double fps) {
    if (fps <= 0.0) {
        return 2.0;
    }
    return MAX(0.5, 1.0 / fps);
}

SCStreamConfiguration *TGScreenCaptureThumbnailConfiguration(CGSize sourcePixelSize, CGSize boxPixelSize, BOOL showsCursor) {
    CGSize size = TGScreenCaptureFillSize(sourcePixelSize, boxPixelSize);
    SCStreamConfiguration *configuration = [[SCStreamConfiguration alloc] init];
    configuration.width = (size_t)size.width;
    configuration.height = (size_t)size.height;
    configuration.pixelFormat = kCVPixelFormatType_32BGRA;
    configuration.colorSpaceName = kCGColorSpaceSRGB;
    configuration.showsCursor = showsCursor;
    configuration.scalesToFit = YES;
    return configuration;
}

SCStreamConfiguration *TGScreenCapturePreviewConfiguration(CGSize sourcePixelSize, CGSize boxPixelSize, double fps, BOOL showsCursor) {
    CGSize size = TGScreenCaptureOutputSize(sourcePixelSize, boxPixelSize);
    SCStreamConfiguration *configuration = [[SCStreamConfiguration alloc] init];
    configuration.width = (size_t)size.width;
    configuration.height = (size_t)size.height;
    configuration.pixelFormat = kCVPixelFormatType_32BGRA;
    configuration.colorSpaceName = kCGColorSpaceSRGB;
    configuration.minimumFrameInterval = CMTimeMake(1, (int32_t)MAX(1.0, std::round(fps)));
    configuration.queueDepth = 3;
    configuration.showsCursor = showsCursor;
    configuration.scalesToFit = YES;
    return configuration;
}

#endif // TARGET_OS_OSX
