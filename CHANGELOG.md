# tgcalls (Nanogram fork) changelog

Branch `nanogram-shared`, shared by Nanogram for iOS and Nanogram for macOS. Base: Telegram's iOS line `telegram-ios-dev` (2bc227de).

Newest first. Fork-only commits, grouped by day; entries for new work go on top under `## Unreleased`, then are
moved under their date when the app repositories pin them.

## Unreleased

- 1:1 screen share: the Mac marks `desktop_capturer_` sources as screen captures; `ScreenSharePolicy.h` maps the negotiated version to how the screen is sent (7/8/9/12/13 screencast channel, 10/11 video channel with screen-content settings, 2.7.7/5.0.0 bitrate floor); InstanceV2ReferenceImpl no longer drops a screen capture; every call logs its version and screen mode

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

