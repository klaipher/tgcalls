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

// The upstream rule, unchanged: H.264 is offered only on x86_64 (so arm64 devices offer a v1 peer
// H.265/VP8/VP9). What a legacy call offers is interop; offering H.264 on arm64 (the hardware encoder)
// waits for the owner's decision.
inline bool darwinSupportsEncoding(const std::string &codecName, const DarwinEncodingFacts &facts) {
    if (codecName == "H265") {
        if (!facts.h265Compiled) {
            return false;
        }
        return facts.isIos ? facts.hevcPresetAvailable : !facts.isX86_64;
    }
    if (codecName == "H264") {
        return facts.isX86_64;
    }
    if (codecName == "VP8" || codecName == "VP9") {
        return true;
    }
    return false;
}

} // namespace tgcalls

#endif
