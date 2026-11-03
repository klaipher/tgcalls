#ifndef TGLivestreamDecode_h
#define TGLivestreamDecode_h

#include <atomic>

namespace tgcalls {
namespace livestreamdecode {

enum class Codec {
    Unknown,
    H264,
    HEVC
};

struct StreamInfo {
    Codec codec = Codec::Unknown;
    int width = 0;
    int height = 0;
    bool progressive = true;
    int bitDepth = 8;
    bool chroma420 = true;
};

constexpr int kMinHeight = 360;
constexpr int kMinWidth = 640;
constexpr int kMaxDimension = 4096;
constexpr int kMaxSessions = 2;

inline bool isEligible(StreamInfo const &info) {
    if (info.codec != Codec::H264 && info.codec != Codec::HEVC) {
        return false;
    }
    if (!info.progressive || info.bitDepth != 8 || !info.chroma420) {
        return false;
    }
    int longSide = info.width > info.height ? info.width : info.height;
    int shortSide = info.width > info.height ? info.height : info.width;
    if (longSide < kMinWidth || shortSide < kMinHeight) {
        return false;
    }
    if (longSide > kMaxDimension) {
        return false;
    }
    return true;
}

class SessionCounter {
public:
    explicit SessionCounter(int limit = kMaxSessions) :
    _limit(limit) {
    }

    bool tryAcquire() {
        int current = _count.load();
        while (current < _limit) {
            if (_count.compare_exchange_weak(current, current + 1)) {
                return true;
            }
        }
        return false;
    }

    void release() {
        int current = _count.load();
        while (current > 0) {
            if (_count.compare_exchange_weak(current, current - 1)) {
                return;
            }
        }
    }

    int count() const {
        return _count.load();
    }

private:
    int _limit;
    std::atomic<int> _count{0};
};

inline SessionCounter &sharedSessionCounter() {
    static SessionCounter counter;
    return counter;
}

inline bool isAcceptableOutputBuffer(int planeCount, int bitsPerComponent, bool chroma420) {
    return planeCount == 2 && bitsPerComponent == 8 && chroma420;
}

}
}

#endif
