#ifndef TGRTCPixelTransfer_h
#define TGRTCPixelTransfer_h

#import <Foundation/Foundation.h>
#import <CoreVideo/CoreVideo.h>

#include <cstdint>

namespace tgcalls {
namespace pixeltransfer {

struct CleanAperture {
    int width = 0;
    int height = 0;
    int horizontalOffset = 0;
    int verticalOffset = 0;
};

inline bool computeCleanAperture(int sourceWidth, int sourceHeight, int cropX, int cropY, int cropWidth, int cropHeight, CleanAperture &result) {
    if (sourceWidth <= 0 || sourceHeight <= 0 || cropWidth <= 0 || cropHeight <= 0 || cropX < 0 || cropY < 0) {
        return false;
    }
    if (cropX + cropWidth > sourceWidth || cropY + cropHeight > sourceHeight) {
        return false;
    }
    result.width = cropWidth;
    result.height = cropHeight;
    result.horizontalOffset = cropX + cropWidth / 2 - sourceWidth / 2;
    result.verticalOffset = cropY + cropHeight / 2 - sourceHeight / 2;
    return true;
}

}
}

@interface TGRTCPixelTransfer : NSObject

- (BOOL)cropAndScaleSource:(CVPixelBufferRef)source cropX:(int)cropX cropY:(int)cropY cropWidth:(int)cropWidth cropHeight:(int)cropHeight destination:(CVPixelBufferRef)destination;

- (void)invalidate;

@end

#endif
