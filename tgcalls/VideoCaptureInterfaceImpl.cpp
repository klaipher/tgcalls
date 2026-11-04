#include "VideoCaptureInterfaceImpl.h"

#include "VideoCapturerInterface.h"
#include "Manager.h"
#include "MediaManager.h"
#include "platform/PlatformInterface.h"
#include "StaticThreads.h"
#include "CallSystemPolicy.h"

#include <mutex>
#include <vector>
#include <algorithm>

#ifdef __APPLE__
#include "platform/darwin/TGCallsRuntimeOptions.h"
#endif

namespace tgcalls {

namespace {

std::mutex &captureRegistryMutex() {
    static std::mutex value;
    return value;
}

std::vector<VideoCaptureInterfaceImpl *> &captureRegistry() {
    static std::vector<VideoCaptureInterfaceImpl *> value;
    return value;
}

void currentOutgoingLimit(int &maxShortSide, int &maxFps) {
#ifdef __APPLE__
    maxShortSide = RuntimeOptions::outgoingCameraMaxShortSide().load();
    maxFps = RuntimeOptions::outgoingCameraMaxFps().load();
#else
    maxShortSide = 0;
    maxFps = 0;
#endif
}

}

VideoCaptureInterfaceObject::VideoCaptureInterfaceObject(std::string deviceId, bool isScreenCapture, std::shared_ptr<PlatformContext> platformContext, Threads &threads)
: _videoSource(PlatformInterface::SharedInstance()->makeVideoSource(threads.getMediaThread(), threads.getWorkerThread())) {
	_platformContext = platformContext;

	switchToDevice(deviceId, isScreenCapture);
}

VideoCaptureInterfaceObject::~VideoCaptureInterfaceObject() {
	if (_videoCapturer) {
		_videoCapturer->setUncroppedOutput(nullptr);
	}
}

webrtc::scoped_refptr<webrtc::VideoTrackSourceInterface> VideoCaptureInterfaceObject::source() {
	return _videoSource;
}

int VideoCaptureInterfaceObject::getRotation() {
    if (_videoCapturer) {
        return _videoCapturer->getRotation();
    } else {
        return 0;
    }
}

bool VideoCaptureInterfaceObject::isScreenCapture() {
    return _isScreenCapture;
}

void VideoCaptureInterfaceObject::switchToDevice(std::string deviceId, bool isScreenCapture) {
    if (_videoCapturer) {
		_videoCapturer->setUncroppedOutput(nullptr);
    }
    _isScreenCapture = isScreenCapture;
	if (_videoSource) {
        //this should outlive the capturer
        _videoCapturer = nullptr;
		_videoCapturer = PlatformInterface::SharedInstance()->makeVideoCapturer(_videoSource, deviceId, [this](VideoState state) {
			if (this->_stateUpdated) {
				this->_stateUpdated(state);
			}
            if (this->_onIsActiveUpdated) {
                switch (state) {
                    case VideoState::Active: {
                        this->_onIsActiveUpdated(true);
                        break;
                    }
                    default: {
                        this->_onIsActiveUpdated(false);
                        break;
                    }
                }
            }
        }, [this](PlatformCaptureInfo info) {
            if (this->_shouldBeAdaptedToReceiverAspectRate != info.shouldBeAdaptedToReceiverAspectRate) {
                this->_shouldBeAdaptedToReceiverAspectRate = info.shouldBeAdaptedToReceiverAspectRate;
            }
            if (this->_rotationUpdated) {
                this->_rotationUpdated(info.rotation);
            }
            this->updateAspectRateAdaptation();
        }, _platformContext, _videoCapturerResolution);
	}
	if (_videoCapturer) {
		if (_preferredAspectRatio > 0) {
			_videoCapturer->setPreferredCaptureAspectRatio(_preferredAspectRatio);
		}
		if (const auto currentUncroppedSink = _currentUncroppedSink.lock()) {
			_videoCapturer->setUncroppedOutput(currentUncroppedSink);
		}
        if (_onFatalError) {
            _videoCapturer->setOnFatalError(_onFatalError);
        }
        if (_onPause) {
            _videoCapturer->setOnPause(_onPause);
        }
		_videoCapturer->setState(_state);
	}
}

void VideoCaptureInterfaceObject::withNativeImplementation(std::function<void(void *)> completion) {
    if (_videoCapturer) {
        _videoCapturer->withNativeImplementation(completion);
    } else {
        completion(nullptr);
    }
}

void VideoCaptureInterfaceObject::setState(VideoState state) {
	if (_state != state) {
		_state = state;
		if (_videoCapturer) {
			_videoCapturer->setState(state);
		}
	}
}

void VideoCaptureInterfaceObject::setPreferredAspectRatio(float aspectRatio) {
    _preferredAspectRatio = aspectRatio;
    updateAspectRateAdaptation();
}

void VideoCaptureInterfaceObject::updateAspectRateAdaptation() {
    if (_videoCapturer) {
        if (_videoCapturerResolution.first != 0 && _videoCapturerResolution.second != 0) {
            if (_preferredAspectRatio > 0.01 && _shouldBeAdaptedToReceiverAspectRate) {
                float originalWidth = (float)_videoCapturerResolution.first;
                float originalHeight = (float)_videoCapturerResolution.second;

                float aspectRatio = _preferredAspectRatio;

                float width = (originalWidth > aspectRatio * originalHeight)
                    ? int(std::round(aspectRatio * originalHeight))
                    : originalWidth;
                float height = (originalWidth > aspectRatio * originalHeight)
                    ? originalHeight
                    : int(std::round(originalHeight / aspectRatio));

                adaptToOutgoingLimit((int)width, (int)height);
            } else {
                adaptToOutgoingLimit(_videoCapturerResolution.first, _videoCapturerResolution.second);
            }
        }
    }
}

void VideoCaptureInterfaceObject::adaptToOutgoingLimit(int width, int height) {
    int maxShortSide = 0;
    int maxFps = 0;
    if (!_isScreenCapture) {
        currentOutgoingLimit(maxShortSide, maxFps);
    }
    const auto size = systempolicy::limitOutgoingVideo(width, height, systempolicy::kOutgoingCameraFps, maxShortSide, maxFps);
    if (size.width != width || size.height != height || size.fps != systempolicy::kOutgoingCameraFps) {
        RTC_LOG(LS_INFO) << "Outgoing camera video limited to " << size.width << "x" << size.height << " at " << size.fps << " fps";
    }
    PlatformInterface::SharedInstance()->adaptVideoSource(_videoSource, size.width, size.height, size.fps);
}

void VideoCaptureInterfaceObject::reapplyOutgoingLimit() {
    updateAspectRateAdaptation();
}

void VideoCaptureInterfaceObject::setOutput(std::shared_ptr<rtc::VideoSinkInterface<webrtc::VideoFrame>> sink) {
	if (_videoCapturer) {
		_videoCapturer->setUncroppedOutput(sink);
	}
	_currentUncroppedSink = sink;
}

void VideoCaptureInterfaceObject::setOnFatalError(std::function<void()> error) {
    if (_videoCapturer) {
        _videoCapturer->setOnFatalError(error);
    }
    _onFatalError = error;
}
void VideoCaptureInterfaceObject::setOnPause(std::function<void(bool)> pause) {
    if (_videoCapturer) {
        _videoCapturer->setOnPause(pause);
    }
    _onPause = pause;
}

void VideoCaptureInterfaceObject::setOnIsActiveUpdated(std::function<void(bool)> onIsActiveUpdated) {
    _onIsActiveUpdated = onIsActiveUpdated;
}

void VideoCaptureInterfaceObject::setStateUpdated(std::function<void(VideoState)> stateUpdated) {
	_stateUpdated = stateUpdated;
}

void VideoCaptureInterfaceObject::setRotationUpdated(std::function<void(int)> rotationUpdated) {
    _rotationUpdated = rotationUpdated;
}

VideoCaptureInterfaceImpl::VideoCaptureInterfaceImpl(std::string deviceId, bool isScreenCapture, std::shared_ptr<PlatformContext> platformContext, std::shared_ptr<Threads> threads) :
_impl(threads->getMediaThread(), [deviceId, isScreenCapture, platformContext, threads]() {
	return std::make_shared<VideoCaptureInterfaceObject>(deviceId, isScreenCapture, platformContext, *threads);
}) {
    std::unique_lock<std::mutex> lock(captureRegistryMutex());
    captureRegistry().push_back(this);
}

VideoCaptureInterfaceImpl::~VideoCaptureInterfaceImpl() {
    std::unique_lock<std::mutex> lock(captureRegistryMutex());
    auto &registry = captureRegistry();
    registry.erase(std::remove(registry.begin(), registry.end(), this), registry.end());
}

void VideoCaptureInterfaceImpl::reapplyOutgoingLimits() {
    std::unique_lock<std::mutex> lock(captureRegistryMutex());
    for (auto *capture : captureRegistry()) {
        capture->_impl.perform([](VideoCaptureInterfaceObject *impl) {
            impl->reapplyOutgoingLimit();
        });
    }
}

void VideoCaptureInterfaceImpl::switchToDevice(std::string deviceId, bool isScreenCapture) {
	_impl.perform([deviceId, isScreenCapture](VideoCaptureInterfaceObject *impl) {
		impl->switchToDevice(deviceId, isScreenCapture);
	});
}

void VideoCaptureInterfaceImpl::withNativeImplementation(std::function<void(void *)> completion) {
    _impl.perform([completion](VideoCaptureInterfaceObject *impl) {
        impl->withNativeImplementation(completion);
    });
}

void VideoCaptureInterfaceImpl::setState(VideoState state) {
	_impl.perform([state](VideoCaptureInterfaceObject *impl) {
		impl->setState(state);
	});
}

void VideoCaptureInterfaceImpl::setPreferredAspectRatio(float aspectRatio) {
    _impl.perform([aspectRatio](VideoCaptureInterfaceObject *impl) {
        impl->setPreferredAspectRatio(aspectRatio);
    });
}
void VideoCaptureInterfaceImpl::setOnFatalError(std::function<void()> error) {
    _impl.perform([error](VideoCaptureInterfaceObject *impl) {
        impl->setOnFatalError(error);
    });
}
void VideoCaptureInterfaceImpl::setOnPause(std::function<void(bool)> pause) {
    _impl.perform([pause](VideoCaptureInterfaceObject *impl) {
        impl->setOnPause(pause);
    });
}

void VideoCaptureInterfaceImpl::setOnIsActiveUpdated(std::function<void(bool)> onIsActiveUpdated) {
    _impl.perform([onIsActiveUpdated](VideoCaptureInterfaceObject *impl) {
        impl->setOnIsActiveUpdated(onIsActiveUpdated);
    });
}

void VideoCaptureInterfaceImpl::setOutput(std::shared_ptr<rtc::VideoSinkInterface<webrtc::VideoFrame>> sink) {
	_impl.perform([sink](VideoCaptureInterfaceObject *impl) {
		impl->setOutput(sink);
	});
}

ThreadLocalObject<VideoCaptureInterfaceObject> *VideoCaptureInterfaceImpl::object() {
	return &_impl;
}

std::function<webrtc::scoped_refptr<webrtc::VideoTrackSourceInterface>()> videoCaptureToGetVideoSource(std::shared_ptr<VideoCaptureInterface> videoCapture) {
	return [videoCapture]() -> webrtc::scoped_refptr<webrtc::VideoTrackSourceInterface> {
		if (!videoCapture) {
			return nullptr;
		}
		VideoCaptureInterfaceObject *object = static_cast<VideoCaptureInterfaceImpl *>(videoCapture.get())->object()->getSyncAssumingSameThread();
		return object ? object->source() : nullptr;
	};
}

} // namespace tgcalls
