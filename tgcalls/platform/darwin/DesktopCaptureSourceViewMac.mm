//
//  DesktopCaptureSourceView.m
//  TgVoipWebrtc
//
//  Created by Mikhail Filimonov on 28.12.2020.
//  Copyright © 2020 Mikhail Filimonov. All rights reserved.
//
#import <Cocoa/Cocoa.h>
#import "DesktopCaptureSourceViewMac.h"
#import "platform/darwin/VideoMetalViewMac.h"
#import "platform/darwin/ScreenCaptureKitCapturer.h"
#import "platform/darwin/ScreenCaptureKitSources.h"
#import "tgcalls/desktop_capturer/DesktopCaptureSource.h"
#import "tgcalls/desktop_capturer/DesktopCaptureSourceHelper.h"
#import "tgcalls/desktop_capturer/DesktopCaptureSourceManager.h"
#import "platform/darwin/VideoMetalViewMac.h"



@interface DesktopCaptureSourceViewMetal : VideoMetalView
@end


@implementation DesktopCaptureSourceViewMetal

-(id)initWithHelper:(tgcalls::DesktopCaptureSourceHelper)helper {
    if (self = [super initWithFrame:CGRectZero]) {
        std::shared_ptr<rtc::VideoSinkInterface<webrtc::VideoFrame>> sink = [self getSink];
        helper.setOutput(sink);
        [self setVideoContentMode:kCAGravityResizeAspectFill];
    }
    return self;
}
@end


@implementation DesktopCaptureSourceDataMac
-(id)initWithSize:(CGSize)size fps:(double)fps captureMouse:(bool)captureMouse {
    if (self = [super init]) {
        self.aspectSize = size;
        self.fps = fps;
        self.captureMouse = captureMouse;
    }
    return self;
}

-(NSString *)cachedKey {
    return [[NSString alloc] initWithFormat:@"%@:%f:%d", NSStringFromSize(self.aspectSize), self.fps, self.captureMouse];
}
@end

@interface DesktopCaptureSourceMac ()
{
    absl::optional<tgcalls::DesktopCaptureSource> _source;
    BOOL _isWindow;
}
-(id)initWithSource:(tgcalls::DesktopCaptureSource)source isWindow:(BOOL)isWindow;
-(tgcalls::DesktopCaptureSource)getSource;

@end

@implementation DesktopCaptureSourceMac

-(tgcalls::DesktopCaptureSource)getSource {
    return _source.value();
}

-(NSString *)title {
    if (_isWindow) {
        const tgcalls::DesktopCaptureSource source = _source.value();
        return [[NSString alloc] initWithCString:source.title().c_str() encoding:NSUTF8StringEncoding];
    }
    else
        return [[NSString alloc] initWithFormat:@"Screen"];
}

-(long)uniqueId {
    return _source.value().uniqueId();
}
-(BOOL)isWindow {
    return _isWindow;
}
-(NSString *)uniqueKey {
    return [[NSString alloc] initWithFormat:@"%ld:%@", self.uniqueId, _isWindow ? @"Window" : @"Screen"];
}

-(NSString *)deviceIdKey {
    return [[NSString alloc] initWithFormat:@"desktop_capturer_%@_%ld", _isWindow ? @"window" : @"screen", self.uniqueId];
}

-(BOOL)isEqual:(id)object {
    return [[((DesktopCaptureSourceMac *)object) uniqueKey] isEqualToString:[self uniqueKey]];
}
- (BOOL)isEqualTo:(id)object {
    return [[((DesktopCaptureSourceMac *)object) uniqueKey] isEqualToString:[self uniqueKey]];
}

-(id)initWithSource:(tgcalls::DesktopCaptureSource)source isWindow:(BOOL)isWindow {
    if (self = [super init]) {
        _source = source;
        _isWindow = isWindow;
    }
    return self;
}

@end


@interface DesktopCaptureSourceScopeMac ()
-(tgcalls::DesktopCaptureSourceData)getData;
-(tgcalls::DesktopCaptureSource)getSource;
@end

@implementation DesktopCaptureSourceScopeMac

-(id)initWithSource:(DesktopCaptureSourceMac *)source data:(DesktopCaptureSourceDataMac *)data {
    if (self = [super init]) {
        _data = data;
        _source = source;
    }
    return self;
}

-(NSString *)cachedKey {
    return [[NSString alloc] initWithFormat:@"%@:%@", _source.uniqueKey, _data.cachedKey];
}

-(tgcalls::DesktopCaptureSourceData)getData {
    tgcalls::DesktopCaptureSourceData data{
        /*.aspectSize = */{ (int)_data.aspectSize.width, (int)_data.aspectSize.height},
        /*.fps = */_data.fps,
        /*.captureMouse = */_data.captureMouse,
    };
    return data;
}
-(tgcalls::DesktopCaptureSource)getSource {
    return [_source getSource];
}

@end

static BOOL DesktopCaptureUsesScreenCaptureKit() {
    if (@available(macOS 14.0, *)) {
        return [ScreenCaptureKitCapturer isEnabled];
    }
    return NO;
}

@implementation DesktopCaptureSourceManagerMac
{
    std::map<std::string, tgcalls::DesktopCaptureSourceHelper> _cached;
    std::unique_ptr<tgcalls::DesktopCaptureSourceManager> _manager;
    BOOL _isWindow;
    BOOL _usesScreenCaptureKit;
    NSMutableDictionary<NSString *, NSHashTable<NSView *> *> *_screenCaptureViews;
}

-(instancetype)init_s {
    if (self = [super init]) {
        _isWindow = NO;
        _usesScreenCaptureKit = DesktopCaptureUsesScreenCaptureKit();
        _screenCaptureViews = [[NSMutableDictionary alloc] init];
        if (!_usesScreenCaptureKit) {
            _manager = std::make_unique<tgcalls::DesktopCaptureSourceManager>(tgcalls::DesktopCaptureType::Screen);
        }
    }
    return self;
}
-(instancetype)init_w {
    if (self = [super init]) {
        _isWindow = YES;
        _usesScreenCaptureKit = DesktopCaptureUsesScreenCaptureKit();
        _screenCaptureViews = [[NSMutableDictionary alloc] init];
        if (!_usesScreenCaptureKit) {
            _manager = std::make_unique<tgcalls::DesktopCaptureSourceManager>(tgcalls::DesktopCaptureType::Window);
        }
    }
    return self;
}

-(NSArray<DesktopCaptureSourceMac *> *)screenCaptureKitList API_AVAILABLE(macos(14.0)) {
    TGScreenCaptureContentCache *cache = [TGScreenCaptureContentCache shared];
    NSArray<TGScreenCaptureListedSource *> *sources = _isWindow ? [cache windowsWaitingAtMost:0.5] : [cache displaysWaitingAtMost:0.5];
    NSMutableArray<DesktopCaptureSourceMac *> *macSources = [[NSMutableArray alloc] init];
    for (TGScreenCaptureListedSource *source in sources) {
        tgcalls::DesktopCaptureSource captureSource(source.uniqueId, std::string(source.title.UTF8String ?: ""), _isWindow);
        [macSources addObject:[[DesktopCaptureSourceMac alloc] initWithSource:captureSource isWindow:_isWindow]];
    }
    return macSources;
}

-(NSArray<DesktopCaptureSourceMac *> *)list {
    if (_usesScreenCaptureKit) {
        if (@available(macOS 14.0, *)) {
            return [self screenCaptureKitList];
        }
    }
    std::vector<tgcalls::DesktopCaptureSource> sources = _manager->sources();
    NSMutableArray<DesktopCaptureSourceMac *> *macSources = [[NSMutableArray alloc] init];
    for (auto i = sources.begin(); i != sources.end(); ++i) {
        [macSources addObject:[[DesktopCaptureSourceMac alloc] initWithSource:*i isWindow:_isWindow]];
    }
    return macSources;
}

-(NSView *)createForScope:(DesktopCaptureSourceScopeMac*)scope {
    if (_usesScreenCaptureKit) {
        if (@available(macOS 14.0, *)) {
            TGScreenCapturePreviewView *view = [[TGScreenCapturePreviewView alloc] initWithSourceId:scope.source.uniqueId isWindow:_isWindow boxPixelSize:scope.data.aspectSize fps:scope.data.fps showsCursor:scope.data.captureMouse];
            NSHashTable<NSView *> *views = _screenCaptureViews[scope.cachedKey];
            if (views == nil) {
                views = [NSHashTable weakObjectsHashTable];
                _screenCaptureViews[scope.cachedKey] = views;
            }
            [views addObject:view];
            return view;
        }
    }
    auto i = _cached.find(std::string([scope.cachedKey UTF8String]));
    if (i == _cached.end()) {
        i = _cached.emplace(
                            std::string([scope.cachedKey UTF8String]),
            tgcalls::DesktopCaptureSourceHelper([scope getSource], [scope getData])).first;
    }
    
    DesktopCaptureSourceViewMetal *view = [[DesktopCaptureSourceViewMetal alloc] initWithHelper:i->second];
    if (scope.data.captureMouse) {
        [view setVideoContentMode:kCAGravityResizeAspect];
    }
    return view;
}

-(void)start:(DesktopCaptureSourceScopeMac *)scope {
    if (_usesScreenCaptureKit) {
        if (@available(macOS 14.0, *)) {
            for (NSView *view in [_screenCaptureViews[scope.cachedKey] allObjects]) {
                [(TGScreenCapturePreviewView *)view start];
            }
        }
        return;
    }
    const auto i = _cached.find(std::string([scope.cachedKey UTF8String]));
    if (i != _cached.end()) {
        i->second.start();
    }
}

-(void)stop:(DesktopCaptureSourceScopeMac *)scope {
    if (_usesScreenCaptureKit) {
        if (@available(macOS 14.0, *)) {
            for (NSView *view in [_screenCaptureViews[scope.cachedKey] allObjects]) {
                [(TGScreenCapturePreviewView *)view stop];
            }
        }
        return;
    }
    const auto i = _cached.find(std::string([scope.cachedKey UTF8String]));
    if (i != _cached.end()) {
        i->second.stop();
    }
}

-(void)dealloc {
    if (@available(macOS 14.0, *)) {
        for (NSHashTable<NSView *> *views in _screenCaptureViews.allValues) {
            for (NSView *view in views.allObjects) {
                [(TGScreenCapturePreviewView *)view stop];
            }
        }
    }
    for (auto &[key, helper] : _cached) {
        helper.stop();
    }
    _manager.reset();
}

@end
