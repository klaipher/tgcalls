/*
 *  Copyright 2017 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#import "TGRTCMTLI420Renderer.h"

#import <Metal/Metal.h>
#import <MetalKit/MetalKit.h>

#import "base/RTCI420Buffer.h"
#import "base/RTCLogging.h"
#import "base/RTCVideoFrame.h"
#import "base/RTCVideoFrameBuffer.h"

#import "TGRTCMTLRenderer+Private.h"
#import "TGRTCVideoYUVConversion.h"
#import "components/video_frame_buffer/RTCCVPixelBuffer.h"
#import <CoreVideo/CoreVideo.h>


@implementation TGRTCMTLI420Renderer {
  // Textures.
  id<MTLTexture> _yTexture;
  id<MTLTexture> _uTexture;
  id<MTLTexture> _vTexture;

  MTLTextureDescriptor *_descriptor;
  MTLTextureDescriptor *_chromaDescriptor;

  int _width;
  int _height;
  int _chromaWidth;
  int _chromaHeight;

  CVMetalTextureCacheRef _textureCache;
  CVMetalTextureRef _lumaTextureRef;
  CVMetalTextureRef _chromaTextureRef;
  BOOL _isBiPlanar;
  TGRTCYUVConversion _conversion;
}

- (void)releaseBiPlanarTextures {
  if (_lumaTextureRef) {
    CFRelease(_lumaTextureRef);
    _lumaTextureRef = NULL;
  }
  if (_chromaTextureRef) {
    CFRelease(_chromaTextureRef);
    _chromaTextureRef = NULL;
  }
}

- (void)dealloc {
  [self releaseBiPlanarTextures];
  if (_textureCache) {
    CFRelease(_textureCache);
    _textureCache = NULL;
  }
}

- (BOOL)setupBiPlanarTexturesForPixelBuffer:(CVPixelBufferRef)pixelBuffer device:(id<MTLDevice>)device {
  OSType pixelFormat = CVPixelBufferGetPixelFormatType(pixelBuffer);
  if (pixelFormat != kCVPixelFormatType_420YpCbCr8BiPlanarFullRange && pixelFormat != kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange) {
    return NO;
  }
  if ([self nv12PipelineState] == nil) {
    return NO;
  }
  if (!_textureCache) {
    if (CVMetalTextureCacheCreate(kCFAllocatorDefault, NULL, device, NULL, &_textureCache) != kCVReturnSuccess) {
      _textureCache = NULL;
      return NO;
    }
  }
  [self releaseBiPlanarTextures];
  size_t width = CVPixelBufferGetWidthOfPlane(pixelBuffer, 0);
  size_t height = CVPixelBufferGetHeightOfPlane(pixelBuffer, 0);
  size_t chromaWidth = CVPixelBufferGetWidthOfPlane(pixelBuffer, 1);
  size_t chromaHeight = CVPixelBufferGetHeightOfPlane(pixelBuffer, 1);
  if (CVMetalTextureCacheCreateTextureFromImage(kCFAllocatorDefault, _textureCache, pixelBuffer, NULL, MTLPixelFormatR8Unorm, width, height, 0, &_lumaTextureRef) != kCVReturnSuccess ||
      CVMetalTextureCacheCreateTextureFromImage(kCFAllocatorDefault, _textureCache, pixelBuffer, NULL, MTLPixelFormatRG8Unorm, chromaWidth, chromaHeight, 1, &_chromaTextureRef) != kCVReturnSuccess) {
    [self releaseBiPlanarTextures];
    return NO;
  }
  _yTexture = CVMetalTextureGetTexture(_lumaTextureRef);
  _uTexture = CVMetalTextureGetTexture(_chromaTextureRef);
  _vTexture = nil;
  _descriptor = nil;
  _chromaDescriptor = nil;
  return _yTexture != nil && _uTexture != nil;
}



- (void)getWidth:(nonnull int *)width
          height:(nonnull int *)height
         ofFrame:(nonnull RTC_OBJC_TYPE(RTCVideoFrame) *)frame {
  *width = frame.width;
  *height = frame.height;
}


- (BOOL)setupTexturesForFrame:(nonnull RTC_OBJC_TYPE(RTCVideoFrame) *)frame {
  if (![super setupTexturesForFrame:frame]) {
    return NO;
  }

  id<MTLDevice> device = [self currentMetalDevice];
  if (!device) {
    return NO;
  }

  if ([frame.buffer isKindOfClass:[RTC_OBJC_TYPE(RTCCVPixelBuffer) class]]) {
    RTC_OBJC_TYPE(RTCCVPixelBuffer) *pixelBuffer = (RTC_OBJC_TYPE(RTCCVPixelBuffer) *)frame.buffer;
    _conversion = TGRTCYUVConversionForPixelBuffer(pixelBuffer.pixelBuffer);
    if (![pixelBuffer requiresCropping] && [self setupBiPlanarTexturesForPixelBuffer:pixelBuffer.pixelBuffer device:device]) {
      _isBiPlanar = YES;
      return YES;
    }
  } else {
    _conversion = TGRTCYUVConversionForI420Frame(frame);
  }
  if (_isBiPlanar) {
    [self releaseBiPlanarTextures];
    _yTexture = nil;
    _uTexture = nil;
    _vTexture = nil;
    _isBiPlanar = NO;
  }

  id<RTC_OBJC_TYPE(RTCI420Buffer)> buffer = [frame.buffer toI420];

  // Luma (y) texture.
  if (!_descriptor || _width != frame.width || _height != frame.height) {
    _width = frame.width;
    _height = frame.height;
    _descriptor = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatR8Unorm
                                                                     width:_width
                                                                    height:_height
                                                                 mipmapped:NO];
    _descriptor.usage = MTLTextureUsageShaderRead;
    _yTexture = [device newTextureWithDescriptor:_descriptor];
  }

  // Chroma (u,v) textures
  [_yTexture replaceRegion:MTLRegionMake2D(0, 0, _width, _height)
               mipmapLevel:0
                 withBytes:buffer.dataY
               bytesPerRow:buffer.strideY];

  if (!_chromaDescriptor || _chromaWidth != frame.width / 2 || _chromaHeight != frame.height / 2) {
    _chromaWidth = frame.width / 2;
    _chromaHeight = frame.height / 2;
    _chromaDescriptor =
        [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatR8Unorm
                                                           width:_chromaWidth
                                                          height:_chromaHeight
                                                       mipmapped:NO];
    _chromaDescriptor.usage = MTLTextureUsageShaderRead;
    _uTexture = [device newTextureWithDescriptor:_chromaDescriptor];
    _vTexture = [device newTextureWithDescriptor:_chromaDescriptor];
  }

  [_uTexture replaceRegion:MTLRegionMake2D(0, 0, _chromaWidth, _chromaHeight)
               mipmapLevel:0
                 withBytes:buffer.dataU
               bytesPerRow:buffer.strideU];
  [_vTexture replaceRegion:MTLRegionMake2D(0, 0, _chromaWidth, _chromaHeight)
               mipmapLevel:0
                 withBytes:buffer.dataV
               bytesPerRow:buffer.strideV];

  return (_uTexture != nil) && (_yTexture != nil) && (_vTexture != nil);
}

- (void)uploadTexturesToRenderEncoder:(id<MTLRenderCommandEncoder>)renderEncoder {
  if (_isBiPlanar) {
    [renderEncoder setRenderPipelineState:[self nv12PipelineState]];
    [renderEncoder setFragmentTexture:_yTexture atIndex:0];
    [renderEncoder setFragmentTexture:_uTexture atIndex:1];
  } else {
    [renderEncoder setFragmentTexture:_yTexture atIndex:0];
    [renderEncoder setFragmentTexture:_uTexture atIndex:1];
    [renderEncoder setFragmentTexture:_vTexture atIndex:2];
  }
  TGRTCYUVConversion conversion = _conversion;
  [renderEncoder setFragmentBytes:&conversion length:sizeof(conversion) atIndex:0];
}

@end

