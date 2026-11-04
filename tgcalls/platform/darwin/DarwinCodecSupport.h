#ifndef TGCALLS_DARWIN_CODEC_SUPPORT_H
#define TGCALLS_DARWIN_CODEC_SUPPORT_H

#include <string>

namespace tgcalls {

// Which codecs this Apple device offers to a v1 (legacy) peer as encoders.
// Codec names are the SDP names (cricket::kH264CodecName and so on).
struct DarwinEncodingFacts {
    bool isIos = false;
    bool isX86_64 = false;
    // iOS only: the system lists the HEVC export preset.
    bool hevcPresetAvailable = false;
    // The build has H.265 compiled in (WEBRTC_DISABLE_H265 not defined).
    bool h265Compiled = false;
};

// H.264 is always offered: the encoder factory always lists it, and the
// call path (SimulcastVideoEncoderFactory) creates the VideoToolbox hardware
// encoder on Apple Silicon. The old check returned NO on arm64 (it was
// written in 2021 for early M1 Macs, when H.265 was preferred there), which
// left old peers with software VP8/VP9 only. VP8 stays in the list.
inline bool darwinSupportsEncoding(const std::string &codecName, const DarwinEncodingFacts &facts) {
    if (codecName == "H265") {
        if (!facts.h265Compiled) {
            return false;
        }
        return facts.isIos ? facts.hevcPresetAvailable : !facts.isX86_64;
    }
    if (codecName == "H264" || codecName == "VP8" || codecName == "VP9") {
        return true;
    }
    return false;
}

} // namespace tgcalls

#endif
