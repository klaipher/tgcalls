#ifndef TGCameraFormatSelection_h
#define TGCameraFormatSelection_h

#include <algorithm>
#include <cstdint>
#include <vector>

namespace tgcalls {
namespace cameraformat {

struct Candidate {
    int width = 0;
    int height = 0;
    double minFps = 0.0;
    double maxFps = 0.0;
    bool isHDR = false;
    bool isBinned = false;
    bool isFullRange = false;
};

constexpr double kTargetFps = 30.0;

// The engine sends camera video at 25 fps at most (VideoCaptureInterfaceImpl::adaptToOutgoingLimit),
// so capturing faster only produces frames that are dropped before encoding.
constexpr double kCaptureFps = 25.0;

inline bool supportsFps(Candidate const &candidate, double fps) {
    return candidate.minFps <= fps + 0.01 && candidate.maxFps >= fps - 0.01;
}

inline int sizeClass(Candidate const &candidate) {
    if (candidate.width >= 720 && candidate.height >= 720) {
        return 0;
    }
    if (candidate.width >= 640 && candidate.height >= 640) {
        return 1;
    }
    return 2;
}

inline bool isBetter(Candidate const &lhs, Candidate const &rhs) {
    if (lhs.isHDR != rhs.isHDR) {
        return !lhs.isHDR;
    }
    int lhsClass = sizeClass(lhs);
    int rhsClass = sizeClass(rhs);
    if (lhsClass != rhsClass) {
        return lhsClass < rhsClass;
    }
    if (lhs.width != rhs.width) {
        return lhsClass == 2 ? lhs.width > rhs.width : lhs.width < rhs.width;
    }
    if (lhs.height != rhs.height) {
        return lhsClass == 2 ? lhs.height > rhs.height : lhs.height < rhs.height;
    }
    bool lhsFps = supportsFps(lhs, kTargetFps);
    bool rhsFps = supportsFps(rhs, kTargetFps);
    if (lhsFps != rhsFps) {
        return lhsFps;
    }
    if (lhs.isBinned != rhs.isBinned) {
        return lhs.isBinned;
    }
    if (lhs.maxFps != rhs.maxFps) {
        return lhs.maxFps < rhs.maxFps;
    }
    if (lhs.isFullRange != rhs.isFullRange) {
        return !lhs.isFullRange;
    }
    return false;
}

inline int selectBest(std::vector<Candidate> const &candidates) {
    int best = -1;
    for (int i = 0; i < (int)candidates.size(); i++) {
        if (best == -1 || isBetter(candidates[i], candidates[best])) {
            best = i;
        }
    }
    return best;
}

inline double clampFps(double minFps, double maxFps, double target = kTargetFps) {
    if (maxFps <= 0.0) {
        return target;
    }
    return std::min(std::max(target, minFps), maxFps);
}

struct FrameRateRange {
    double minFps = 0.0;
    double maxFps = 0.0;
};

struct FrameRateChoice {
    enum class Kind {
        Target,
        FastestOfRange,
        SlowestOfRange
    };

    int rangeIndex = -1;
    Kind kind = Kind::Target;
};

inline FrameRateChoice chooseFrameRate(std::vector<FrameRateRange> const &ranges, double target = kTargetFps) {
    FrameRateChoice result;
    double bestDistance = 0.0;
    for (int i = 0; i < (int)ranges.size(); i++) {
        auto const &range = ranges[i];
        FrameRateChoice choice;
        choice.rangeIndex = i;
        double distance = 0.0;
        if (target < range.minFps - 0.01) {
            choice.kind = FrameRateChoice::Kind::SlowestOfRange;
            distance = range.minFps - target;
        } else if (target > range.maxFps + 0.01) {
            choice.kind = FrameRateChoice::Kind::FastestOfRange;
            distance = target - range.maxFps;
        }
        if (result.rangeIndex == -1 || distance < bestDistance) {
            result = choice;
            bestDistance = distance;
        }
    }
    return result;
}

struct CaptureFrameRateBounds {
    int rangeIndex = -1;
    // Fastest allowed rate (shortest frame duration).
    double fastestFps = 0.0;
    // Slowest allowed rate (longest frame duration): the range's own slowest, so the camera
    // can stretch exposure in low light instead of being locked to one rate.
    double slowestFps = 0.0;
};

inline CaptureFrameRateBounds captureFrameRateBounds(std::vector<FrameRateRange> const &ranges) {
    CaptureFrameRateBounds bounds;
    auto choice = chooseFrameRate(ranges, kCaptureFps);
    if (choice.rangeIndex < 0) {
        return bounds;
    }
    auto const &range = ranges[choice.rangeIndex];
    bounds.rangeIndex = choice.rangeIndex;
    bounds.slowestFps = range.minFps;
    switch (choice.kind) {
        case FrameRateChoice::Kind::Target:
            bounds.fastestFps = kCaptureFps;
            break;
        case FrameRateChoice::Kind::FastestOfRange:
            bounds.fastestFps = range.maxFps;
            break;
        case FrameRateChoice::Kind::SlowestOfRange:
            bounds.fastestFps = range.minFps;
            break;
    }
    return bounds;
}

}
}

#endif
