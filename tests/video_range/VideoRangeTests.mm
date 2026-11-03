#import <Foundation/Foundation.h>
#import <CoreMedia/CoreMedia.h>
#import <CoreVideo/CoreVideo.h>
#import <VideoToolbox/VideoToolbox.h>
#import "TGRTCScreenVideoRange.h"

static int failures = 0;

static void expect(BOOL condition, NSString *name) {
    fprintf(stderr, "%s: %s\n", condition ? "ok" : "FAIL", name.UTF8String);
    if (!condition) {
        failures++;
    }
}

static const int kSize = 128;

static CVPixelBufferRef makeI420Copy(uint8_t luma) {
    NSDictionary *attributes = @{ (id)kCVPixelBufferIOSurfacePropertiesKey: @{} };
    CVPixelBufferRef buffer = NULL;
    CVPixelBufferCreate(NULL, kSize, kSize, TGRTCI420CopyPixelFormat(), (__bridge CFDictionaryRef)attributes, &buffer);
    CVPixelBufferLockBaseAddress(buffer, 0);
    memset(CVPixelBufferGetBaseAddressOfPlane(buffer, 0), luma, CVPixelBufferGetBytesPerRowOfPlane(buffer, 0) * CVPixelBufferGetHeightOfPlane(buffer, 0));
    memset(CVPixelBufferGetBaseAddressOfPlane(buffer, 1), 128, CVPixelBufferGetBytesPerRowOfPlane(buffer, 1) * CVPixelBufferGetHeightOfPlane(buffer, 1));
    CVPixelBufferUnlockBaseAddress(buffer, 0);
    TGRTCTagLimitedRangeBT601(buffer);
    return buffer;
}

static CMSampleBufferRef encode(CVPixelBufferRef pixelBuffer, CMVideoCodecType codec) {
    __block CMSampleBufferRef result = NULL;
    VTCompressionSessionRef session = NULL;
    NSDictionary *spec = @{ (id)kVTVideoEncoderSpecification_EnableHardwareAcceleratedVideoEncoder: @YES };
    if (VTCompressionSessionCreate(NULL, kSize, kSize, codec, (__bridge CFDictionaryRef)spec, NULL, NULL, NULL, NULL, &session) != noErr) {
        return NULL;
    }
    VTSessionSetProperty(session, kVTCompressionPropertyKey_RealTime, kCFBooleanTrue);
    CFStringRef sessionMatrix = NULL;
    TGRTCUpdateSessionColorMatrix(session, pixelBuffer, &sessionMatrix);
    if (sessionMatrix != NULL) {
        CFRelease(sessionMatrix);
    }
    VTCompressionSessionEncodeFrameWithOutputHandler(session, pixelBuffer, CMTimeMake(0, 25), kCMTimeInvalid, NULL, NULL, ^(OSStatus status, VTEncodeInfoFlags flags, CMSampleBufferRef sampleBuffer) {
        if (status == noErr && sampleBuffer != NULL) {
            result = (CMSampleBufferRef)CFRetain(sampleBuffer);
        }
    });
    VTCompressionSessionCompleteFrames(session, kCMTimeInvalid);
    VTCompressionSessionInvalidate(session);
    CFRelease(session);
    return result;
}

static int decodedLuma(CMSampleBufferRef sampleBuffer) {
    CMFormatDescriptionRef format = CMSampleBufferGetFormatDescription(sampleBuffer);
    NSDictionary *output = @{ (id)kCVPixelBufferPixelFormatTypeKey: @(kCVPixelFormatType_32BGRA) };
    VTDecompressionSessionRef session = NULL;
    if (VTDecompressionSessionCreate(NULL, format, NULL, (__bridge CFDictionaryRef)output, NULL, &session) != noErr) {
        return -1;
    }
    __block int luma = -1;
    VTDecompressionSessionDecodeFrameWithOutputHandler(session, sampleBuffer, 0, NULL, ^(OSStatus status, VTDecodeInfoFlags infoFlags, CVImageBufferRef imageBuffer, CMTime presentationTimeStamp, CMTime presentationDuration) {
        if (status == noErr && imageBuffer != NULL) {
            CVPixelBufferLockBaseAddress(imageBuffer, kCVPixelBufferLock_ReadOnly);
            const uint8_t *pixels = (const uint8_t *)CVPixelBufferGetBaseAddress(imageBuffer);
            size_t row = CVPixelBufferGetBytesPerRow(imageBuffer);
            const uint8_t *center = pixels + row * (kSize / 2) + 4 * (kSize / 2);
            luma = (center[0] + center[1] + center[2]) / 3;
            CVPixelBufferUnlockBaseAddress(imageBuffer, kCVPixelBufferLock_ReadOnly);
        }
    });
    VTDecompressionSessionWaitForAsynchronousFrames(session);
    VTDecompressionSessionInvalidate(session);
    CFRelease(session);
    return luma;
}

int main() {
    @autoreleasepool {
        CVPixelBufferRef probe = makeI420Copy(16);
        expect(CVPixelBufferGetPixelFormatType(probe) == kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange, @"I420 frames (libyuv ARGBToI420, limited range) are copied into video-range 420v for the encoder, not 420f");
        CFTypeRef matrix = CVBufferCopyAttachment(probe, kCVImageBufferYCbCrMatrixKey, NULL);
        expect(matrix != NULL && CFEqual(matrix, kCVImageBufferYCbCrMatrix_ITU_R_601_4), @"the copy is tagged BT.601");
        if (matrix != NULL) {
            CFRelease(matrix);
        }
        CVPixelBufferRelease(probe);

        {
            VTCompressionSessionRef session = NULL;
            VTCompressionSessionCreate(NULL, kSize, kSize, kCMVideoCodecType_H264, NULL, NULL, NULL, NULL, NULL, &session);
            CFStringRef current = NULL;
            CVPixelBufferRef screen = makeI420Copy(16);
            CVPixelBufferRef camera = makeI420Copy(16);
            CVBufferSetAttachment(camera, kCVImageBufferYCbCrMatrixKey, kCVImageBufferYCbCrMatrix_ITU_R_709_2, kCVAttachmentMode_ShouldPropagate);
            BOOL first = TGRTCUpdateSessionColorMatrix(session, screen, &current);
            BOOL same = TGRTCUpdateSessionColorMatrix(session, screen, &current);
            BOOL switched = TGRTCUpdateSessionColorMatrix(session, camera, &current);
            CFTypeRef property = NULL;
            VTSessionCopyProperty(session, kVTCompressionPropertyKey_YCbCrMatrix, NULL, &property);
            expect(!first && !same && switched && current != NULL && CFEqual(current, kCVImageBufferYCbCrMatrix_ITU_R_709_2) && property != NULL && CFEqual(property, kCVImageBufferYCbCrMatrix_ITU_R_709_2), @"the session follows the frames' matrix and reports a switch (keyframe) only when it changes");
            if (property != NULL) {
                CFRelease(property);
            }
            if (current != NULL) {
                CFRelease(current);
            }
            CVPixelBufferRelease(screen);
            CVPixelBufferRelease(camera);
            VTCompressionSessionInvalidate(session);
            CFRelease(session);
        }

        const struct { CMVideoCodecType codec; const char *name; } codecs[] = { { kCMVideoCodecType_H264, "H.264" }, { kCMVideoCodecType_HEVC, "H.265" } };
        for (const auto &codec : codecs) {
            CVPixelBufferRef black = makeI420Copy(16);
            CVPixelBufferRef white = makeI420Copy(235);
            CMSampleBufferRef blackEncoded = encode(black, codec.codec);
            CMSampleBufferRef whiteEncoded = encode(white, codec.codec);
            if (blackEncoded == NULL || whiteEncoded == NULL) {
                expect(NO, [NSString stringWithFormat:@"%s encodes", codec.name]);
                continue;
            }
            CMFormatDescriptionRef format = CMSampleBufferGetFormatDescription(blackEncoded);
            CFTypeRef fullRange = CMFormatDescriptionGetExtension(format, kCMFormatDescriptionExtension_FullRangeVideo);
            CFTypeRef streamMatrix = CMFormatDescriptionGetExtension(format, kCMFormatDescriptionExtension_YCbCrMatrix);
            expect(fullRange == NULL || !CFBooleanGetValue((CFBooleanRef)fullRange), [NSString stringWithFormat:@"%s stream signals limited range (video_full_range_flag 0)", codec.name]);
            expect(streamMatrix != NULL && CFEqual(streamMatrix, kCMFormatDescriptionYCbCrMatrix_ITU_R_601_4), [NSString stringWithFormat:@"%s stream signals the BT.601 matrix", codec.name]);
            int blackLuma = decodedLuma(blackEncoded);
            int whiteLuma = decodedLuma(whiteEncoded);
            fprintf(stderr, "  %s decoded black %d white %d\n", codec.name, blackLuma, whiteLuma);
            expect(blackLuma >= 0 && blackLuma <= 3 && whiteLuma >= 252, [NSString stringWithFormat:@"%s: an Apple receiver decodes screen-share black as black and white as white", codec.name]);
            CFRelease(blackEncoded);
            CFRelease(whiteEncoded);
            CVPixelBufferRelease(black);
            CVPixelBufferRelease(white);
        }
    }
    return failures == 0 ? 0 : 1;
}
