#ifndef TGCALLS_SCREEN_SHARE_POLICY_H
#define TGCALLS_SCREEN_SHARE_POLICY_H

// Pure policy for a 1:1 call that sends the screen as its outgoing video (a capturer created with
// isScreenCapture = true). What the flag does depends on the negotiated protocol version, because each
// implementation reads it differently. Header only so host tests can include it without WebRTC. Nothing here is
// negotiated or sent on the wire: it names how each implementation encodes the screen and builds the one log line
// a call writes.
//
// Which version is negotiated: the app side (CallSessionManagerContext in TelegramCore) reverses the version list
// before selecting, so Mac, iOS and tdesktop calls normally end up on 9.0.0 (InstanceV2Impl, separate screencast
// channel). 10.0.0/11.0.0 (the PeerConnection implementation) only run when the peer offers nothing newer in that
// order, which is rare.

#include <string>

namespace tgcalls {
namespace screenshare {

enum class ScreenMode {
    // 2.7.7, 5.0.0 (MediaManager): one video stream; the flag only raises the bitrate floor and start to 700 kbps.
    BitrateFloorOnVideo,
    // 7, 8, 9, 12, 13 (InstanceV2Impl): a separate screencast channel (screencast content type, maintain
    // resolution). The peer shows it as its incoming video and takes the screencast media state.
    SeparateScreencastChannel,
    // 10, 11 (InstanceV2ReferenceImpl): the single video transceiver carries the screen with screen-content
    // settings (detailed content hint, maintain resolution, higher bitrate ceiling).
    ScreenSettingsOnVideoChannel,
    // Any other version (14.0.0 compat drops screen captures; 18/19 and unknown versions are not handled): the
    // screen is not sent. Nothing enforces this at the call site; the value documents it and names the log line.
    NotSupported,
};

inline ScreenMode chooseScreenMode(const std::string &version) {
    if (version == "2.7.7" || version == "5.0.0") {
        return ScreenMode::BitrateFloorOnVideo;
    }
    if (version == "7.0.0" || version == "8.0.0" || version == "9.0.0" || version == "12.0.0" || version == "13.0.0") {
        return ScreenMode::SeparateScreencastChannel;
    }
    if (version == "10.0.0" || version == "11.0.0") {
        return ScreenMode::ScreenSettingsOnVideoChannel;
    }
    return ScreenMode::NotSupported;
}

// The implementation class a version is registered by (what InstanceImpl/InstanceV2Impl/InstanceV2ReferenceImpl
// return from their getVersions), or "none".
inline const char *implementationFor(const std::string &version) {
    if (version == "2.7.7" || version == "5.0.0") {
        return "InstanceImpl";
    }
    if (version == "7.0.0" || version == "8.0.0" || version == "9.0.0" || version == "12.0.0" || version == "13.0.0") {
        return "InstanceV2Impl";
    }
    if (version == "10.0.0" || version == "11.0.0") {
        return "InstanceV2ReferenceImpl";
    }
    return "none";
}

inline bool screenIsSent(ScreenMode mode) {
    return mode != ScreenMode::NotSupported;
}

inline const char *screenModeName(ScreenMode mode) {
    switch (mode) {
        case ScreenMode::BitrateFloorOnVideo:
            return "bitrate-floor-on-video";
        case ScreenMode::SeparateScreencastChannel:
            return "separate-screencast-channel";
        case ScreenMode::ScreenSettingsOnVideoChannel:
            return "screen-settings-on-video-channel";
        case ScreenMode::NotSupported:
            break;
    }
    return "not-supported";
}

// The single line each call writes to its log when the instance is created.
inline std::string callLogLine(const std::string &version) {
    return "Call: negotiated version " + (version.empty() ? std::string("(none)") : version)
        + ", screen share mode " + screenModeName(chooseScreenMode(version));
}

// Screen-content settings of the PeerConnection implementation (ScreenSettingsOnVideoChannel).
constexpr int kScreenMaxBitrateBps = 2000 * 1000;

} // namespace screenshare
} // namespace tgcalls

#endif
