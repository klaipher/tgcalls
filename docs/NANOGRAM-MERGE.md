# Merge of nanogram-android into nanogram-shared

One call engine for iOS, macOS and Android. This merge brings the Android line (`origin/nanogram-android`, 545f5ae) into
the shared line (`origin/nanogram-shared`, 9bfb76f); merge base 1c236c0. Rule: shared wins, Android wins only for
`tgcalls/platform/android/**` and the `OnRenegotiationNeeded` routing (Android 03d7b86, public cherry-pick ea0f24e).

## Textual conflicts (8 files)

| File | Resolution |
|---|---|
| `CLAUDE.md` | Android text for both hunks: the renegotiation note and the `android-late-cam` flake note describe code that is now in. |
| `Package.swift` | Shared: it excludes `CLAUDE.md`, `Dockerfile`, `docs`, `tools`, `tgcalls/v2wasm` and the host tests from the target, a superset of Android's list. |
| `tgcalls/group/GroupAudioCapturePostProcessor.cpp` / `.h` (add/add) | Shared: peak speech detector, `PushSincResampler` processing-rate path. Android's older `Process(originalBuffer)` form dropped. |
| `tgcalls/group/GroupInstanceCustomImpl.cpp` | Shared: `CallAudioWebRtc.h` include and the digital-mic-gain `AudioProcessing::Config` block. |
| `tgcalls/group/GroupInstanceReferenceImpl.cpp` | Shared: log sink only with a log path. Android's added `SetLogToStderr` / `AddLogToStream` lines duplicated lines shared already has, so they are dropped. |
| `tgcalls/v2/InstanceV2CompatImpl.cpp` | Shared: external signaling connection branch (`network_signaling_nosctp`) and `applyOutgoingAudioMuteState()`. |
| `tgcalls/v2/InstanceV2ReferenceImpl.cpp` | Shared: `TGCALLS_WEBRTC_WITHOUT_TELEGRAM_SEAMS` guards (include and mtproto fallback). Android: the two renegotiation comment hunks (the code change itself merged cleanly: `OnRenegotiationNeeded` calls `requestRenegotiation()`). |

## Silent API differences fixed in the same merge

| Difference | Result |
|---|---|
| `PlatformInterface::makeVideoSource` | Three arguments `(signalingThread, workerThread, bool screencapture)` everywhere: `PlatformInterface.h`, Darwin, Desktop (tdesktop), Fake, Android; `VideoCaptureInterfaceObject` passes `isScreenCapture`. The iOS/Mac bridge does not override it. |
| `Descriptor::audioLevelUpdated(float)` (iOS, Mac) vs `audioLevelsUpdated(float, float)` (Android) | Descriptor keeps both. `MakeAudioLevelsCallback` (Instance.h) merges them into the engines' `(own, effective)` form; `Manager`, `InstanceV2Impl`, `InstanceV2CompatImpl` and `InstanceV2ReferenceImpl` use it. If both are set both are called. The 1:1 AudioSinkImpl now reports `(0, level)`. |
| `EncryptionKey::value` | Stays `shared_ptr<const array>`; Android's JNI passes a non-const `shared_ptr`, which converts implicitly. |
| `_currentUncroppedSink` | `std::shared_ptr` member and unconditional `setUncroppedOutput(_currentUncroppedSink)` under `WEBRTC_ANDROID` (the JNI layer passes a temporary sink and the capturer holds the only other reference, so a `weak_ptr` would expire in `switchToDevice` and the self-view would freeze after a camera flip); shared's `weak_ptr` + `.lock()` everywhere else. |
| `std::weak_ptr{ _videoCaptureGuard }` | Explicit `std::weak_ptr<bool>{ ... }` (Android's form, valid in C++17 and C++20). |

## Android changes carried unchanged (they merged without conflict)

`tgcalls/platform/android/**` is byte-identical to `origin/nanogram-android`. In shared files Android adds:
three behaviours live in shared files, all now under `#ifdef WEBRTC_ANDROID` so iOS and macOS keep shared behaviour:
`VideoSinkImpl` keeps only the last frame size (no stored `VideoFrame`, no replay of the last frame to a sink added late;
Apple keeps the replay), empty or incomplete `ssrcGroups` of a video description are ignored, and `InstanceV2Impl`
resets the outgoing video and screencast channels when capture changes or video turns off (Apple only detaches the
capture). Promoting the `ssrcGroups` guard or the channel reset to all platforms needs an owner decision and an Apple
runtime check of camera/screen-share switching. `requestRenegotiation()` also handles WebRTC's own
`OnRenegotiationNeeded` on every platform.

Proof: with every `WEBRTC_ANDROID` block removed, `InstanceV2Impl.cpp`, `GroupInstanceCustomImpl.cpp` and
`VideoCaptureInterfaceImpl.{h,cpp}` differ from 9bfb76f only by the signature, audio-level, `makeVideoSource` and
renegotiation hunks.

W4.4 emulator plan: add an Android camera flip check (front/back, then confirm the local self-view keeps updating).

## Platform call sites read (no changes needed beyond the above)

- iOS bridge `ios/Sources/OngoingCallThreadLocalContext.mm`: sets `.audioLevelUpdated`, constructs `EncryptionKey` from a const array; compiles against the kept fields.
- Mac `macos/Sources/OngoingCallThreadLocalContext.mm`: same.
- Android `TMessagesProj/jni/voip/org_telegram_messenger_voip_Instance.cpp`: sets `.audioLevelsUpdated(float, float)` and a non-const key; both still accepted.

## C++ standard

The merged tree adds no C++20-only library or language use over the Android line (grep for `starts_with`, `contains`, `span`,
`<=>`, `requires`, `std::format`, ranges found nothing in `tgcalls/` outside Apple/desktop/wasm paths). Android's CMake stays
at `-std=c++17`. Designated initializers in the JNI file are a clang extension in C++17 and already built. Not
compiled here: whoever builds Android must confirm.

## Also added

`tools/wasm_to_source.py` from klaipher/nanogram (was `submodules/TgVoipWebrtc/tools/` at iOS 466d8350; now this fork's `tools/`), the prerequisite of the
single reference-core embedding step.
