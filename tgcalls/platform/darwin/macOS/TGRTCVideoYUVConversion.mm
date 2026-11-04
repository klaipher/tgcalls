#import "TGRTCVideoYUVConversion.h"

#import <objc/runtime.h>

static char TGRTCVideoFrameYUVColorSpaceKey;

TGRTCYUVConversion TGRTCYUVConversionMake(TGRTCYCbCrMatrix matrix, BOOL fullRange) {
    float kr = 0.299f;
    float kb = 0.114f;
    switch (matrix) {
        case TGRTCYCbCrMatrixBT709:
            kr = 0.2126f;
            kb = 0.0722f;
            break;
        case TGRTCYCbCrMatrixBT2020:
            kr = 0.2627f;
            kb = 0.0593f;
            break;
        default:
            break;
    }
    float kg = 1.0f - kr - kb;
    TGRTCYUVConversion conversion = {
        { 1.0f, 1.0f, 1.0f, 0.0f },
        { 0.0f, -2.0f * kb * (1.0f - kb) / kg, 2.0f * (1.0f - kb), 0.0f },
        { 2.0f * (1.0f - kr), -2.0f * kr * (1.0f - kr) / kg, 0.0f, 0.0f },
        { fullRange ? 0.0f : 16.0f / 255.0f, 0.5f, 0.5f, 0.0f },
        { fullRange ? 1.0f : 255.0f / 219.0f, fullRange ? 1.0f : 255.0f / 224.0f, fullRange ? 1.0f : 255.0f / 224.0f, 0.0f }
    };
    return conversion;
}

TGRTCYUVConversion TGRTCYUVConversionDefault(void) {
    return TGRTCYUVConversionMake(TGRTCYCbCrMatrixBT601, YES);
}

TGRTCYUVConversion TGRTCYUVConversionForPixelBuffer(CVPixelBufferRef pixelBuffer) {
    BOOL fullRange = YES;
    switch (CVPixelBufferGetPixelFormatType(pixelBuffer)) {
        case kCVPixelFormatType_32BGRA:
        case kCVPixelFormatType_32ARGB:
            return TGRTCYUVConversionMake(TGRTCYCbCrMatrixBT601, NO);
        case kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange:
        case kCVPixelFormatType_420YpCbCr10BiPlanarVideoRange:
        case kCVPixelFormatType_422YpCbCr8BiPlanarVideoRange:
            fullRange = NO;
            break;
        default:
            break;
    }
    TGRTCYCbCrMatrix matrix = TGRTCYCbCrMatrixBT601;
    CFTypeRef tag = NULL;
    if (@available(macOS 12.0, *)) {
        tag = CVBufferCopyAttachment(pixelBuffer, kCVImageBufferYCbCrMatrixKey, NULL);
    } else {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
        tag = CVBufferGetAttachment(pixelBuffer, kCVImageBufferYCbCrMatrixKey, NULL);
        if (tag != NULL) {
            CFRetain(tag);
        }
#pragma clang diagnostic pop
    }
    if (tag != NULL) {
        if (CFGetTypeID(tag) == CFStringGetTypeID()) {
            if (CFEqual(tag, kCVImageBufferYCbCrMatrix_ITU_R_709_2)) {
                matrix = TGRTCYCbCrMatrixBT709;
            } else if (CFEqual(tag, kCVImageBufferYCbCrMatrix_ITU_R_2020)) {
                matrix = TGRTCYCbCrMatrixBT2020;
            }
        }
        CFRelease(tag);
    }
    return TGRTCYUVConversionMake(matrix, fullRange);
}

void TGRTCVideoFrameSetYUVColorSpace(id frame, TGRTCYCbCrMatrix matrix, BOOL fullRange) {
    objc_setAssociatedObject(frame, &TGRTCVideoFrameYUVColorSpaceKey, @[ @(matrix), @(fullRange) ], OBJC_ASSOCIATION_RETAIN_NONATOMIC);
}

TGRTCYUVConversion TGRTCYUVConversionForI420Frame(id frame) {
    NSArray<NSNumber *> *colorSpace = objc_getAssociatedObject(frame, &TGRTCVideoFrameYUVColorSpaceKey);
    if (colorSpace.count != 2) {
        return TGRTCYUVConversionDefault();
    }
    return TGRTCYUVConversionMake((TGRTCYCbCrMatrix)colorSpace[0].integerValue, colorSpace[1].boolValue);
}
