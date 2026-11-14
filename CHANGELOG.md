# tgcalls (Nanogram fork) changelog

Branch `nanogram-shared`, shared by Nanogram for iOS and Nanogram for macOS. Base: Telegram's iOS line `telegram-ios-dev` (2bc227de).

Newest first. Fork-only commits, grouped by day; entries for new work go on top under `## Unreleased`, then are
moved under their date when the app repositories pin them.

## Unreleased

- Merge of Android's `nanogram-android` (545f5ae) into this branch: one call engine for iOS, macOS and Android. `makeVideoSource` takes `bool screencapture` on every platform; `Descriptor` has both `audioLevelUpdated(float)` and `audioLevelsUpdated(float, float)` (`MakeAudioLevelsCallback`); `OnRenegotiationNeeded` goes through `requestRenegotiation()` (Android 03d7b86, ea0f24e); the Android platform layer is carried unchanged. Details and per-file resolutions: docs/NANOGRAM-MERGE.md
- Android's behaviour changes in shared files are guarded with `WEBRTC_ANDROID` so iOS/macOS are unchanged: no last-frame replay in `VideoSinkImpl`, empty `ssrcGroups` ignored, `InstanceV2Impl` resets outgoing video/screencast channels on capture change or video off, `_currentUncroppedSink` is a `shared_ptr` (a `weak_ptr` expired on camera flip)
- `tools/build_wasm_core.sh` (reference core wasm with the iOS genrule flags, then embedded source) and `tools/check_android_cmake.sh` (tgcalls sources missing from Android's CMake; today only the three host tests)
- `tools/wasm_to_source.py` (embeds the wasm reference core as a C++ array), from the iOS repository

- Telegram for macOS 13.2's broadcast playback through the Mac call audio device (Telegram's `38dcea1`, the tgcalls commit telegram-macos ec221d85 pins): `macos/Sources/BroadcastMedia/*` (the livestream part reader and video decoder the Mac's `GroupCallBroadcast*` player uses) and `OngoingCallThreadLocalContext.mm`'s `broadcastAudioCore`; in shared code `GroupInstanceDescriptor.onPlayoutFrame`, an optional playout callback, empty by default, so iOS is unchanged. Cherry-picked with `-x`.

Sync with Telegram 13.2 (tgcalls c1de0882 on development, not public). Source of each change: the public `dev/android` cherry-picks of Telegram's own commits where they exist (`git cherry-pick -x`), a re-implementation where not.

- Every SCTP-signaling engine (11.0.0, 14.0.0, 18/19, as 12/13 before) reads `network_sctp_t1_init_ms` / `_t1_cookie_ms` / `_max_backoff_ms` through `SignalingSctpConnection::Options::fromCustomParameters`; each connection logs its values (af52b5d, picked from dev/android 12ab3b3 = development 8d5e7e0)
- The SCTP timer values are range-checked (1-60000 ms, numbers beyond int read as absent), `SignalingSctpConnection` takes `Options` with no default, host test `SignalingSctpOptionsTest.cpp` (1d574df, dev/android d96660e = development c5ef736)
- `network_reflector_resolve_remote_candidate_ip` defaults to on in every engine that reads it (`getCustomParameterBoolDefaultTrue`); only an explicit `false` turns it off (59a614e, dev/android 545f5ae = development c1de0882)
- ContentNegotiation: `removeOutgoingChannel` walks `_outgoingChannelDescriptions`; a camera turned off before the peer answered no longer returns as a second video channel; host test `ContentNegotiationTest.cpp` from the public look-alike tmacos 10dc9ae (1e99bc9; Telegram's 44e528e)
- InstanceV2CompatImpl (14.0.0): `network_signaling_nosctp` selects `ExternalSignalingConnection`; mute sets the audio sender's `encodings[0].active` and re-applies it after every negotiation step (51dbd05; Telegram's c839645, re-implemented)
- Not taken then (taken by the Android merge, see Unreleased): 11.0.0 `OnRenegotiationNeeded` routed through `requestRenegotiation` (03cd2c5); CompatImpl 7.0.0/8.0.0/9.0.0 framing (unreachable: the app registers InstanceV2Impl first for those versions); `Package.swift` "native SwiftPM WebRTC" (d69ed2f; different Mac integration)

- 1:1 screen share (3862bdc, corrected in the fix-pass commit): calls negotiate 9.0.0 (the app reverses the version list), so the visible change is InstanceV2Impl's separate screencast channel for Mac screen shares; the 10/11 PeerConnection path is a rare fallback. The Mac marks `desktop_capturer_` sources as screen captures; `ScreenSharePolicy.h` maps the negotiated version to how the screen is sent (7/8/9/12/13 screencast channel, 10/11 video channel with screen-content settings, 2.7.7/5.0.0 bitrate floor); InstanceV2ReferenceImpl no longer drops a screen capture; every call logs its version and screen mode (expected on a normal call: `negotiated version 9.0.0, screen share mode separate-screencast-channel`). Thermal video ladder (when on): a screen share keeps its size and drops frame rate only (15 fps from level 2, 10 at level 3); before, `isScreenCapture` switched the ladder off entirely, now that the Mac sets the flag it would have disabled protection for Mac screen shares

## 2026-11-04

- Livestream: a suspended (hidden-window) stream no longer holds the render tick at the 8 ms video floor (54220d6)
- Legacy v1 calls keep the upstream codec offer: H.264 only on x86_64 (pending the owner) (270a17d)
- Calls: thread priorities, the videoChat audio session mode and the macOS audio workgroup are removed (owner decision) (aa1549b)
- Bazel policy tests read the single-sourced test mains; tests added for the H.264 rule (3b15762)
- Call log: at the size cap keep the newest half behind a marker instead of dropping the rest of the call (05b5cf8)
- Legacy v1 calls on arm64 use the hardware H.264 encoder: supportsEncoding(H264) is no longer inverted (47d12fc)
- Host tests follow the merged policies: Mac video_support drops the deleted decode helpers (covered by livestream_decode_policy), concurrent session cap and switch defaults added (db02c26)
- Group audio: the audio-input flag is iOS-only in the Mac build (7452db5)
- Merge nanogram-mac-integration into the iOS tgcalls fork: one implementation per concept (ee0216b)
- Livestream: the skipped-part decoder flush waits for the next part instead of hitting a part resumed in its middle (3e7eaab)
- Livestream: resuming a hidden stream flushes the decoder after skipped parts and decodes at most 6 frames per tick (54d5896)
- H.264 low-latency (Q6): runtime failure latch, standard session after a failure, encoderLowLatency kill switch (7b8c62b)
- Group calls: Bluetooth (8/16 kHz) capture resamples every frame while the round trip is on; groupNarrowbandProcessing switch (f40c7fc)
- Screen share: a still screen keeps reaching the encoder (the last frame is sent again every 100 ms) (5541910)
- Review fixes: camera capture capped at the engine's 25 fps without a fixed frame duration, 32 kHz voice processing only at 12 kbps or less, Opus complexity leaves low-rate complexity alone (26a893e)
- Batch C: quieter call timers, deadline-based livestream render loop, hidden-livestream decode suspend, local outgoing camera cap and thread QoS options (642c43f)
- Batch B audio: group noise mode, digital AGC2, Opus complexity and APM rate switches, no hardware mic mute on macOS (27d6476)
- V3: libavutil/pixdesc.h needs C linkage in VideoStreamingPart.cpp (34da1d0)
- V4: Mac camera picks a stable 720p 30 fps format, locks capture to 30 fps and starts off the main thread (calls.cameraFormatV2) (e5eb430)
- V3: livestream video can decode with VideoToolbox behind tgcalls.livestreamHardwareDecode, with a sticky software fallback (e004829)
- Host tests for batch A video helpers, including a real VTPixelTransferSession crop (16f13be)
- Batch A video: VideoToolbox decoders and the H.265 encoder recover from stale sessions on the Mac, H.265 starts at its real bitrate, frame planes have their real length (8594360)

## 2026-11-03

- R2-29: group calls skip RNNoise with noise suppression off; non-48 kHz capture keeps its processed audio (a598e27)
- R2-30: release builds send nothing to WebRTC's debug log, so INFO lines are not formatted without a sink (013adcb)
- H28 follow-up: the main thread never waits for SCShareableContent (93693e5)
- Merge nanogram-mac-runtime into nanogram-mac-integration (c0ac4d9)
- H28: the screen-share picker lists sources from SCShareableContent and draws its tiles with SCScreenshotManager (df6280d)
- Q5: screen shares stay limited-range BT.601 and say so end to end (e9a1405)
- Q14: the Mac Metal renderer converts YCbCr with the frame's matrix and range, and renders NV12 frames without a CPU toI420 copy (6f098d2)
- macOS: build against desktop-app tg_owt, which has no TGCALLS SEAM patches (6e0caba)
- Q6: Mac call video encoders use VideoToolbox low-latency rate control and real-time mode, with a fallback (9ab1773)
- H28: screen sharing captures with ScreenCaptureKit, scaled and converted on the GPU (e7b02f1)
- M18: keep the host log-sink test out of the TgVoipWebrtc target (9cb2fb2)
- macOS package: keep tools, docs and the wasm core out of the SwiftPM target (bbe5473)
- M18: no in-memory WebRTC log without a log path; stderr and INFO only in DEBUG (469020f)

