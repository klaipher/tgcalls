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

- `tgcalls/ScreenSharePolicy.h` (host test `tests/screen_share_policy`, Bazel `screen_share_policy_test`): version to screen mode. `macos/Sources/OngoingCallThreadLocalContext.mm` passes `isScreenCapture` for `desktop_capturer_` ids in `Create` and `switchToDevice`.
- `InstanceV2ReferenceImpl::setVideoCapture` (10.0.0/11.0.0): a screen capture used to fall into an empty branch and nothing was sent. It now uses the single video transceiver with content hint `kDetailed` (sets `is_screencast`), `MAINTAIN_RESOLUTION` and a 2 Mbps ceiling (camera: 1200 KiB/s as before).
- `InstanceV2Impl` (7/8/9/12/13) keeps its separate screencast channel; the peer treats any incoming video content as its incoming video, so it shows the screen. 14.0.0 (CompatImpl) still drops screen captures and 18/19 are untouched: not registered on the Mac; iOS behaves as before.
- Each of InstanceImpl, InstanceV2Impl and InstanceV2ReferenceImpl writes `Call: negotiated version X, screen share mode Y` to the call log when created.
