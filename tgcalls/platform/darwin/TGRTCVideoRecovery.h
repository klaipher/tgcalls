#ifndef TGRTCVideoRecovery_h
#define TGRTCVideoRecovery_h

#include <cstdint>
#include <deque>
#include <limits>

namespace tgcalls {
namespace videorecovery {

class ResetLimiter {
public:
    explicit ResetLimiter(int maxResets = 3, int64_t windowMs = 10000) :
    _maxResets(maxResets),
    _windowMs(windowMs) {
    }

    bool tryReset(int64_t nowMs) {
        while (!_resets.empty() && nowMs - _resets.front() >= _windowMs) {
            _resets.pop_front();
        }
        if ((int)_resets.size() >= _maxResets) {
            return false;
        }
        _resets.push_back(nowMs);
        return true;
    }

    void clear() {
        _resets.clear();
    }

private:
    int _maxResets;
    int64_t _windowMs;
    std::deque<int64_t> _resets;
};

inline uint32_t bitrateBpsFromKbps(uint32_t kbps) {
    constexpr uint32_t maxKbps = std::numeric_limits<uint32_t>::max() / 1000;
    return kbps > maxKbps ? std::numeric_limits<uint32_t>::max() : kbps * 1000;
}

inline bool shouldApplyBitrate(uint32_t current, uint32_t next, uint32_t deadBandPercent = 5) {
    if (current == next) {
        return false;
    }
    if (current == 0 || next == 0) {
        return true;
    }
    uint64_t difference = current > next ? current - next : next - current;
    return difference * 100 >= (uint64_t)current * deadBandPercent;
}

}
}

#endif
