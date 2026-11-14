# How this tgcalls differs from Telegram's

Upstream: https://github.com/TelegramMessenger/tgcalls. Base: branch `telegram-ios-dev` at 2bc227de (the commit the
Telegram-iOS import pins). Ours: branch `nanogram-shared` (35 commits, 94 files, +4093/-718 at 2026-11-05). One
implementation for iOS and macOS: the former `nanogram-mac` branch is merged into it (ee0216b). History of changes:
[CHANGELOG.md](CHANGELOG.md). Format: docs/fork-records.md in klaipher/nanogram.

First version, assembled from commit subjects and the iOS repository's `docs/subsystems/tgcalls/`. Entries marked
(check) were not re-verified against the code.

### macOS platform layer
- What: ScreenCaptureKit screen sharing (GPU scale and convert, `SCShareableContent` picker), VideoToolbox decoders and
  H.265 encoder recovery, low-latency VideoToolbox rate control for call video, Metal renderer converts YCbCr with the
  frame's matrix and range (NV12 without a CPU copy), stable 720p30 camera format started off the main thread.
- Where: `tgcalls/platform/darwin/`, `macos/`, `Package.swift` (SwiftPM package for the Mac app).
- Why: the Mac app has no platform layer upstream; battery and latency on Apple Silicon.
- Upstream: no (Apple-only code), though the policies below are generic.
- Sync: host tests in `tests/` and the Bazel policy tests (`TgVoipWebrtc/BUILD` in klaipher/nanogram); `Package.swift` must keep tools, docs and the wasm core out of the target.

### Call policies and runtime switches
- What: `CallsRuntimeSwitches` (one switch class), `CallPowerPolicy`, `CallSystemPolicy`, `AudioCallPolicy`: quieter timers,
  deadline-based livestream render loop, hidden-livestream decode suspend, local outgoing camera cap, group noise mode,
  digital AGC2, Opus complexity, Bluetooth narrowband handling, H.264 low-latency with runtime failure latch.
- Where: `tgcalls/CallsRuntimeSwitches.h`, `tgcalls/Call*Policy.h`, `tgcalls/group/`, tests `tgcalls/CallPowerPolicyTest.cpp`.
- Upstream: partly. Sync: the iOS and Mac Debug Settings expose the switches (Streams & Calls).

### Logging and diagnostics
- What: no in-memory WebRTC log without a log path, no stderr in release, INFO only in DEBUG, call log keeps the newest half at its size cap, 64-bit timestamps in `StateLogRecord`.
- Where: `tgcalls/LogSinkImpl.cpp`, `tgcalls/platform/`.
- Upstream: yes.

### Build against desktop-app tg_owt
- What: macOS builds against `klaipher/tg_owt` (desktop-app's tg_owt, no TGCALLS SEAM patches); the iOS build uses the vendored `klaipher/webrtc`.
- Sync: seams must exist on both sides; see `NANOGRAM-CHANGES.md` in klaipher/webrtc and klaipher/tg_owt.

### Legacy v1 calls
- What: arm64 uses the hardware H.264 encoder (`supportsEncoding(H264)` no longer inverted); x86_64 keeps the upstream H.264-only offer (pending the owner).
- Where: `tgcalls/legacy`/`platform/darwin` (check).

### 1:1 screen share by protocol version

- Which path runs: `CallSessionManagerContext` (TelegramCore, CallSessionManager.swift) reverses the version list before choosing, so Mac, iOS and tdesktop calls negotiate 9.0.0 (InstanceV2Impl, separate screencast channel). 10.0.0/11.0.0 (reference impl) is a rare fallback. The user-visible change is therefore the Mac passing `isScreenCapture = true`: a Mac screen share moves from the camera video channel to InstanceV2Impl's separate screencast channel (`is_screencast`, MAINTAIN_RESOLUTION, no explicit max bitrate because `adjustBitratePreferences` caps only the outgoing video channel), the path iOS already uses for its own screen share. Expected log line: `Call: negotiated version 9.0.0, screen share mode separate-screencast-channel`.
- Thermal ladder: `videoLadderFormat`/`thermalCameraFpsCap` take `isScreen`; a screen share keeps its size and drops frame rate only (level 2: 15 fps, level 3: 10 fps), with the ladder switch on. Only the frame-rate request to the video source applies to screen captures; the Darwin camera-rate cap (`setMaximumFrameRate`) is camera-only, so capture itself is not slowed. Host test `tests/call_system_policy`.

- `tgcalls/ScreenSharePolicy.h` (host test `tests/screen_share_policy`, Bazel `screen_share_policy_test`): version to screen mode. `macos/Sources/OngoingCallThreadLocalContext.mm` passes `isScreenCapture` for `desktop_capturer_` ids in `Create` and `switchToDevice`.
- `InstanceV2ReferenceImpl::setVideoCapture` (10.0.0/11.0.0): a screen capture used to fall into an empty branch and nothing was sent. It now uses the single video transceiver with content hint `kDetailed` (sets `is_screencast`), `MAINTAIN_RESOLUTION` and a 2 Mbps ceiling (camera: 1200 Kibit/s, `1200 * 1024` bps, as before).
- `InstanceV2Impl` (7/8/9/12/13) keeps its separate screencast channel; the peer treats any incoming video content as its incoming video, so it shows the screen. 14.0.0 (CompatImpl) still drops screen captures and 18/19 are untouched: not registered on the Mac; iOS behaves as before.
- Each of InstanceImpl, InstanceV2Impl and InstanceV2ReferenceImpl writes `Call: negotiated version X, screen share mode Y` to the call log when created.

### Sync with Telegram 13.2 (tgcalls c1de0882)
- What: Telegram's 13.2 engine changes, taken without their unpublished base: SCTP timer overrides read and range-checked by every SCTP-signaling engine; reflector candidate-IP resolution on by default; `removeOutgoingChannel` index fix; CompatImpl (14.0.0) honours `network_signaling_nosctp` and mutes through the sender's `encodings[0].active`.
- Why: calls with Android 14.0.0 (always nosctp) and with relays; no behaviour beyond what Telegram shipped.
- Where: `tgcalls/v2/SignalingSctpConnection.*`, `CustomParameters.h`, `ContentNegotiation.cpp`, `InstanceV2CompatImpl.cpp`, `InstanceV2Impl.cpp`, `InstanceV2ReferenceImpl.cpp`, `NativeNetworkingImpl.cpp`, `v2wasm/CallCoreHost.cpp`; host tests `v2/SignalingSctpOptionsTest.cpp`, `v2/ContentNegotiationTest.cpp` (superproject targets `signaling_sctp_options_test`, `content_negotiation_test`).
- Upstream: yes for the picked commits (they are Telegram's); the CompatImpl part is our re-implementation of c839645, not public.
- Sync: when Telegram publishes c839645 or the 13.2 development tree, diff `InstanceV2CompatImpl.cpp` against it and drop ours; the two test mains must stay excluded in `Package.swift`.

### One engine for Android too
- What: the Android line is merged in. `makeVideoSource(thread, thread, screencapture)`; `Descriptor` carries both the single and the two-level audio-level callbacks; WebRTC's `OnRenegotiationNeeded` waits for a quiet stable state like capture changes; the Android platform layer (`tgcalls/platform/android/`) lives here unchanged; the video sink no longer replays its last frame to a late sink; video descriptions with empty `ssrcGroups` are ignored.
- Where: `tgcalls/Instance.h`, `Manager.cpp`, `MediaManager.*`, `platform/*/`, `v2/InstanceV2*Impl.cpp`, `group/GroupInstanceCustomImpl.cpp`.
- Why: one source for the three apps, so a call fix lands once.
- Upstream: no.
- Sync: per-file resolutions in docs/NANOGRAM-MERGE.md. Android CMake stays `-std=c++17`.
