#ifndef TGRTC_VIDEO_YUV_CONVERSION_H
#define TGRTC_VIDEO_YUV_CONVERSION_H

#import <Foundation/Foundation.h>
#import <CoreVideo/CoreVideo.h>

NS_ASSUME_NONNULL_BEGIN

typedef NS_ENUM(NSInteger, TGRTCYCbCrMatrix) {
    TGRTCYCbCrMatrixBT601 = 0,
    TGRTCYCbCrMatrixBT709 = 1,
    TGRTCYCbCrMatrixBT2020 = 2
};

typedef struct {
    float column0[4];
    float column1[4];
    float column2[4];
    float offset[4];
    float scale[4];
} TGRTCYUVConversion;

TGRTCYUVConversion TGRTCYUVConversionMake(TGRTCYCbCrMatrix matrix, BOOL fullRange);
TGRTCYUVConversion TGRTCYUVConversionDefault(void);
TGRTCYUVConversion TGRTCYUVConversionForPixelBuffer(CVPixelBufferRef pixelBuffer);

void TGRTCVideoFrameSetYUVColorSpace(id frame, TGRTCYCbCrMatrix matrix, BOOL fullRange);
TGRTCYUVConversion TGRTCYUVConversionForI420Frame(id frame);

NS_ASSUME_NONNULL_END

#endif
