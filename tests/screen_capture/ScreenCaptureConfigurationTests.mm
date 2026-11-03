#import <Foundation/Foundation.h>
#import <CoreVideo/CoreVideo.h>
#import "ScreenCaptureKitConfiguration.h"
#import "ScreenCaptureKitSources.h"

static int failures = 0;

static void expect(BOOL condition, NSString *name) {
    fprintf(stderr, "%s: %s\n", condition ? "ok" : "FAIL", name.UTF8String);
    if (!condition) {
        failures++;
    }
}

static CMSampleBufferRef makeSampleBuffer(SCFrameStatus status, BOOL attachStatus) {
    CVPixelBufferRef pixelBuffer = NULL;
    CVPixelBufferCreate(kCFAllocatorDefault, 64, 64, kCVPixelFormatType_420YpCbCr8BiPlanarFullRange, NULL, &pixelBuffer);
    CMVideoFormatDescriptionRef format = NULL;
    CMVideoFormatDescriptionCreateForImageBuffer(kCFAllocatorDefault, pixelBuffer, &format);
    CMSampleTimingInfo timing = { kCMTimeInvalid, CMTimeMake(1, 25), kCMTimeInvalid };
    CMSampleBufferRef sampleBuffer = NULL;
    CMSampleBufferCreateReadyWithImageBuffer(kCFAllocatorDefault, pixelBuffer, format, &timing, &sampleBuffer);
    if (attachStatus) {
        CFArrayRef attachments = CMSampleBufferGetSampleAttachmentsArray(sampleBuffer, true);
        CFMutableDictionaryRef dictionary = (CFMutableDictionaryRef)CFArrayGetValueAtIndex(attachments, 0);
        CFDictionarySetValue(dictionary, (__bridge CFStringRef)SCStreamFrameInfoStatus, (__bridge CFNumberRef)@(status));
    }
    CFRelease(format);
    CVPixelBufferRelease(pixelBuffer);
    return sampleBuffer;
}

int main() {
    @autoreleasepool {
        CGSize retina = TGScreenCaptureOutputSize(CGSizeMake(5120, 2880), CGSizeMake(1920, 1080));
        expect(retina.width == 1920 && retina.height == 1080, @"a 5K display is captured at the 1920x1080 encode size, not at full resolution");
        CGSize tall = TGScreenCaptureOutputSize(CGSizeMake(1000, 2000), CGSizeMake(1920, 1080));
        expect(tall.height <= 1080 && tall.width <= 1920 && fabs(tall.width / tall.height - 0.5) < 0.01, @"a tall window keeps its aspect ratio inside the box");
        CGSize small = TGScreenCaptureOutputSize(CGSizeMake(641, 481), CGSizeMake(1920, 1080));
        expect(small.width <= 641 && small.height <= 481 && ((int)small.width % 2) == 0 && ((int)small.height % 2) == 0, @"a small window is never upscaled and has even dimensions");

        SCStreamConfiguration *configuration = TGScreenCaptureStreamConfiguration(CGSizeMake(1920, 1080), 25.0, YES);
        expect(configuration.width == 1920 && configuration.height == 1080, @"the stream scales on the GPU to the output size");
        expect(configuration.pixelFormat == kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange, @"limited-range 420v like the legacy libyuv ARGBToI420 path (Q5 owner decision)");
        expect(configuration.colorMatrix != NULL && CFEqual(configuration.colorMatrix, kCGDisplayStreamYCbCrMatrix_ITU_R_601_4), @"BT.601 matrix (Q5)");
        expect(configuration.colorSpaceName != NULL && CFEqual(configuration.colorSpaceName, kCGColorSpaceSRGB), @"sRGB colour space (Q5)");
        expect(CMTimeCompare(configuration.minimumFrameInterval, CMTimeMake(1, 25)) == 0, @"at most 25 frames per second");
        expect(configuration.queueDepth >= 3 && configuration.queueDepth <= 5, @"a short frame queue");
        expect(configuration.showsCursor, @"the cursor is drawn by the system");

        CMSampleBufferRef complete = makeSampleBuffer(SCFrameStatusComplete, YES);
        CMSampleBufferRef idle = makeSampleBuffer(SCFrameStatusIdle, YES);
        CMSampleBufferRef untagged = makeSampleBuffer(SCFrameStatusComplete, NO);
        expect(TGScreenCaptureSampleBufferIsComplete(complete), @"complete frames are sent");
        expect(!TGScreenCaptureSampleBufferIsComplete(idle), @"idle frames (nothing changed) are dropped");
        expect(!TGScreenCaptureSampleBufferIsComplete(untagged), @"frames without a status are dropped");
        CVPixelBufferRef untaggedFrame = NULL;
        CVPixelBufferCreate(kCFAllocatorDefault, 64, 64, kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange, NULL, &untaggedFrame);
        TGScreenCaptureTagFrame(untaggedFrame);
        CFTypeRef matrix = CVBufferCopyAttachment(untaggedFrame, kCVImageBufferYCbCrMatrixKey, NULL);
        expect(matrix != NULL && CFEqual(matrix, kCVImageBufferYCbCrMatrix_ITU_R_601_4), @"a captured frame without a matrix tag is tagged BT.601 before it reaches the encoder (Q5)");
        if (matrix != NULL) {
            CFRelease(matrix);
        }
        CVPixelBufferRef taggedFrame = NULL;
        CVPixelBufferCreate(kCFAllocatorDefault, 64, 64, kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange, NULL, &taggedFrame);
        CVBufferSetAttachment(taggedFrame, kCVImageBufferYCbCrMatrixKey, kCVImageBufferYCbCrMatrix_ITU_R_709_2, kCVAttachmentMode_ShouldPropagate);
        TGScreenCaptureTagFrame(taggedFrame);
        matrix = CVBufferCopyAttachment(taggedFrame, kCVImageBufferYCbCrMatrixKey, NULL);
        expect(matrix != NULL && CFEqual(matrix, kCVImageBufferYCbCrMatrix_ITU_R_709_2), @"a tag set by ScreenCaptureKit is kept");
        if (matrix != NULL) {
            CFRelease(matrix);
        }
        CVPixelBufferRelease(untaggedFrame);
        CVPixelBufferRelease(taggedFrame);

        NSArray<TGScreenCaptureWindowCandidate *> *candidates = @[
            [[TGScreenCaptureWindowCandidate alloc] initWithWindowID:10 layer:0 onScreen:YES title:@"Report.pages" ownerName:@"Pages" ownerPid:100],
            [[TGScreenCaptureWindowCandidate alloc] initWithWindowID:11 layer:25 onScreen:YES title:@"Dock" ownerName:@"Dock" ownerPid:101],
            [[TGScreenCaptureWindowCandidate alloc] initWithWindowID:12 layer:0 onScreen:NO title:@"Minimized" ownerName:@"Notes" ownerPid:102],
            [[TGScreenCaptureWindowCandidate alloc] initWithWindowID:13 layer:0 onScreen:YES title:@"" ownerName:@"Pages" ownerPid:100],
            [[TGScreenCaptureWindowCandidate alloc] initWithWindowID:14 layer:0 onScreen:YES title:nil ownerName:@"Calculator" ownerPid:103],
            [[TGScreenCaptureWindowCandidate alloc] initWithWindowID:15 layer:0 onScreen:YES title:nil ownerName:@"Calculator" ownerPid:103],
            [[TGScreenCaptureWindowCandidate alloc] initWithWindowID:16 layer:0 onScreen:YES title:@"StatusIndicator" ownerName:@"Window Server" ownerPid:104],
            [[TGScreenCaptureWindowCandidate alloc] initWithWindowID:17 layer:0 onScreen:YES title:nil ownerName:nil ownerPid:105],
        ];
        NSArray<TGScreenCaptureListedSource *> *listed = TGScreenCaptureListedWindows(candidates);
        NSMutableArray<NSString *> *summary = [[NSMutableArray alloc] init];
        for (TGScreenCaptureListedSource *source in listed) {
            [summary addObject:[NSString stringWithFormat:@"%lld:%@", source.uniqueId, source.title]];
        }
        expect([summary isEqualToArray:@[ @"10:Report.pages", @"14:Calculator" ]], [NSString stringWithFormat:@"the window list keeps webrtc's rules on SCShareableContent (layer 0, on screen, untitled only as the app's one window named after the app, no status indicator): %@", summary]);

        CGSize thumbnail = TGScreenCaptureFillSize(CGSizeMake(5120, 2880), CGSizeMake(270, 180));
        expect(thumbnail.width == 320 && thumbnail.height == 180, @"a thumbnail is captured just large enough to fill its tile (5K display → 320x180), not at full resolution");
        CGSize tiny = TGScreenCaptureFillSize(CGSizeMake(200, 100), CGSizeMake(270, 180));
        expect(tiny.width == 200 && tiny.height == 100, @"a small window is never upscaled");
        expect(TGScreenCapturePreviewUsesScreenshots(0.5) && !TGScreenCapturePreviewUsesScreenshots(24), @"picker tiles (0.5 fps) are screenshots, the large preview (24 fps) is a stream");
        expect(TGScreenCaptureScreenshotInterval(0.5) == 2.0, @"tiles refresh every 2 s, as before");
        SCStreamConfiguration *thumbnailConfiguration = TGScreenCaptureThumbnailConfiguration(CGSizeMake(5120, 2880), CGSizeMake(270, 180), NO);
        expect(thumbnailConfiguration.width == 320 && thumbnailConfiguration.height == 180 && thumbnailConfiguration.pixelFormat == kCVPixelFormatType_32BGRA && !thumbnailConfiguration.showsCursor, @"the screenshot is BGRA at the tile size, without the cursor");
        SCStreamConfiguration *previewConfiguration = TGScreenCapturePreviewConfiguration(CGSizeMake(5120, 2880), CGSizeMake(1000, 700), 24.0, YES);
        expect(previewConfiguration.width <= 1000 && previewConfiguration.height <= 700 && previewConfiguration.width >= 998 && previewConfiguration.pixelFormat == kCVPixelFormatType_32BGRA && CMTimeCompare(previewConfiguration.minimumFrameInterval, CMTimeMake(1, 24)) == 0 && previewConfiguration.showsCursor, @"the large preview streams BGRA fitted into its box at 24 fps with the cursor");

        if (CGPreflightScreenCaptureAccess()) {
            NSArray<TGScreenCaptureListedSource *> *displays = [[TGScreenCaptureContentCache shared] displaysWaitingAtMost:5.0];
            expect(displays.count > 0 && [displays.firstObject.title isEqualToString:@"Screen"], @"SCShareableContent lists the displays (live)");
        } else {
            fprintf(stderr, "skip: SCShareableContent listing (no Screen Recording permission for the test runner)\n");
        }

        CFRelease(complete);
        CFRelease(idle);
        CFRelease(untagged);
    }
    return failures == 0 ? 0 : 1;
}
