# Project Phoenix — Build Plan

Monocular clip-on HUD for eyeglasses: a portable C++17 rendering/protocol core, a
desktop HTML simulator (the primary deliverable), arduino-cli firmware for the
Seeed XIAO nRF52840 driving a 72x40 SSD1306, and a SwiftUI iOS companion app for
the voice-assistant path. Notifications and time never touch the app — they come
from ANCS/CTS straight over BLE to the firmware.

Legend: `[ ]` pending · `[x]` done. Every phase ends with a mandatory gate;
work never proceeds past a red gate.

---

## Phase 1 — Repo skeleton & build scaffolding
- [x] 1.1 `phoenix/` repo, `git init`, `.gitignore` (build dirs, sim output, copied firmware lib, secrets, Xcode noise)
- [x] 1.2 `PLAN.md` (this file) and `DECISIONS.md` started
- [x] 1.3 Repository layout: `core/ sim/ firmware/ ios/ tools/` (directories land with their phases)
- [x] 1.4 Root `CMakeLists.txt` (C++17, `-Wall -Wextra`, `enable_testing`) + `core/` library target with real version constants (FrameBuffer implemented early so the gate compiles real code)
- [x] 1.5 **Gate:** `cmake -B build && cmake --build build` succeeds
- [x] 1.6 Commit

## Phase 2 — PROTOCOL.md (finalized before any dependent code)
- [x] 2.1 Generate real 128-bit UUIDs (service, TX, RX) with `uuidgen`; document the base-UUID scheme
- [x] 2.2 GATT model: TX (phone→glasses, write-without-response), RX (glasses→phone, notify), MTU/chunking rules (iOS decides MTU; framing survives arbitrary fragmentation)
- [x] 2.3 Frame format: start byte, type, seq, flags, u16 LE payload length, payload, CRC-16 (exact polynomial, init, coverage, byte order all documented, worked example computed and verified)
- [x] 2.4 Message types: `ASSISTANT_TEXT`, `ASSISTANT_STREAM_CHUNK`, `NAV_UPDATE` (maneuver enum, distance, street), `CLEAR`, `SET_BRIGHTNESS`, `PING`, `ACK`, `BUTTON_EVENT`, `BATTERY_STATUS` — payload schema for each
- [x] 2.5 UTF-8 policy + documented fallback for unsupported codepoints (emoji must not crash the display)
- [x] 2.6 Decoder robustness rules: resync, length caps, CRC failure handling
- [x] 2.7 Commit

## Phase 3 — Core framebuffer, fonts, font generator
- [x] 3.1 `FrameBuffer` 72x40, 1bpp, SSD1306 page order; `clear/setPixel/getPixel/fillRect/drawRect/drawLine/blit`, raw page accessor
- [x] 3.2 Font engine: per-glyph advance, `measureText` → `TextMetrics`, `drawText`
- [x] 3.3 `tools/gen_fonts.py` + readable ASCII-art font sources (no unexplained hex blobs): 5x7 proportional body font (full printable ASCII + fallback glyph), ~10x16 numeric clock font, 7x7 category icons, 16x16 nav arrows & splash art
- [x] 3.4 UTF-8 sanitizer: decode hostile UTF-8, transliterate common punctuation/Latin-1, map everything unsupported to the fallback glyph
- [x] 3.5 Hand-rolled zero-dependency test harness; golden-frame infrastructure (ASCII-art goldens committed as files, side-by-side diff print on failure, `PHOENIX_BLESS=1` authoring flow)
- [x] 3.6 **Gate:** golden-frame tests pass (`ctest`, 3/3 suites green)
- [x] 3.7 Commit

## Phase 4 — Core layout engine
- [x] 4.1 `wrapText` (word boundaries, hard char-break fallback for over-long words, explicit `\n`)
- [x] 4.2 `truncateWithEllipsis`
- [x] 4.3 `Marquee`: px/tick, start pause, wrap gap — pure function of tick count (+ `drawMarqueeText` with viewport clipping via new `drawTextClipped`)
- [x] 4.4 `VerticalPager`: page splitting, auto-advance on tick, `nextBoundaryAfter` for manual next-page
- [x] 4.5 Unit tests: boundary widths, over-long words, full marquee cycle, viewport clip, page math
- [x] 4.6 **Gate:** layout unit tests pass (4/4 suites green)
- [x] 4.7 Commit

## Phase 5 — Core protocol codec + fuzz suite
- [x] 5.1 Encoder (frame build + CRC) and typed payload builders
- [x] 5.2 `StreamDecoder`: byte-stream state machine, resync on garbage/bad CRC/oversize length, bounded buffering
- [x] 5.3 Typed payload parsers (bounds-checked, `optional` results)
- [x] 5.4 Fuzz suite (deterministic PRNG): truncated frames (every prefix), bad lengths, per-byte corruption sweep, garbage interleave, arbitrary fragmentation invariance, mutated streams, decoder-wedge probe
- [x] 5.5 ASan/UBSan verification run in `build-asan/` (no OOB reads)
- [x] 5.6 **Gate:** all codec/fuzz tests pass, sanitizers clean (5/5 suites in both builds)
- [x] 5.7 Commit

## Phase 6 — Core ANCS model, screens, screen manager
- [x] 6.1 ANCS domain model: category/event/flag enums (full documented set), `AncsNotification`, `AncsStore` handling added/modified/removed
- [x] 6.2 Category → icon/treatment mapping (pre-existing backlog stays quiet; modified-merge keeps unchanged fields)
- [x] 6.3 Screens: `SplashScreen`, `ClockScreen`, `NotificationScreen`, `IncomingCallScreen`, `AssistantScreen` (with `2/3` page indicator), `NavScreen`, `StatusScreen` (+ deterministic clock via `DateTime`, LiPo battery curve)
- [x] 6.4 `ScreenManager`: priority queue (call > nav > assistant > notification > idle clock), per-type timed dismissal, button dismissal, formal teardown-before-enter state machine with transition log
- [x] 6.5 `Device` facade: BLE bytes in → decoder → dispatch; ANCS events in; buttons; outbound frame queue (button events, battery, ACKs); tick-driven deterministic clock; firmware and sim both wrap exactly this object
- [x] 6.6 Tests: priority preemption & resume, transition ordering, ANCS add/modify/remove (dismissed on phone → cleared on display), 9 reviewed screen golden frames, 42 device/screen/ancs cases
- [x] 6.7 **Gate:** priority, transition, and ANCS event tests pass (8/8 suites, plain + ASan)
- [x] 6.8 Commit

## Phase 7 — Simulator (primary deliverable)
- [x] 7.1 Scenario DSL (plain text, zero-dep parser) + `ScenarioRunner` driving the same `Device` facade the firmware uses (TX frames fed to the decoder in 20-byte radio-sized chunks)
- [x] 7.2 Scenarios (10): boot/idle clock · short notification · over-long notification (marquee) · incoming call with actions · notification removed while on screen · multi-page assistant reply · navigation sequence · rapid-burst priority test · disconnect/rebond cycle · brightness & battery
- [x] 7.3 HTML writer: one self-contained `sim/out/index.html` (~1.4 MB, zero external assets), frames as inline run-length SVG paths, 8x scale, dark bezel + phosphor glow, per-frame tick & event labels
- [x] 7.4 Playback: vanilla inline JS per scenario — play/pause/step/seek/speed at real-time tick rate (100 ms/tick), brightness as panel glow, sticky event labels, offscreen players idle
- [x] 7.5 Visual check in a live browser: splash/clock/call/nav/status all verified on screen
- [x] 7.6 **Gate:** `./build/sim/phoenix_sim --all` writes `sim/out/index.html` — 629 captured frames across all 10 scenarios
- [x] 7.7 Commit

## Phase 8 — Firmware (XIAO nRF52840, arduino-cli)
- [x] 8.1 `firmware/build.sh`: installs Seeed board index + core + U8g2, syncs `core/` into `firmware/lib/PhoenixCore` (single source of truth, umbrella header for arduino-cli detection), compiles `Seeeduino:nrf52:xiaonRF52840` with `-std=gnu++17`
- [x] 8.2 Display driver: U8g2 dedicated 72x40 SSD1306 constructor (`U8G2_SSD1306_72X40_ER_F_HW_I2C`), core framebuffer memcpy'd straight into U8g2's page buffer (geometry-checked, pixel-loop fallback)
- [x] 8.3 BLE dual role on one connection: peripheral advertising Phoenix service (+ ANCS solicitation) + GATT client `BLEAncs`/`BLEClientCts`; pairing requested on connect, bonds persist in InternalFS
- [x] 8.4 ANCS callbacks (attrs + app display name fetched) → core model; TX writes → core decoder; CTS reads/adjust → core clock; core outbox → MTU-chunked RX notifications
- [x] 8.5 ANCS notification actions wired to buttons (B accepts, A declines via `performAction`)
- [x] 8.6 Three debounced buttons: dismiss, next-page, aux/long-press sleep (30 ms debounce, 700 ms long)
- [x] 8.7 Power management: display-off idle → SYSTEM ON idle → SYSTEM OFF with GPIO-sense wake; full power budget reasoning in power.cpp comments
- [x] 8.8 Battery ADC (XIAO divider, averaged, 2.4 V internal ref) → millivolts → core LiPo curve
- [x] 8.9 **Gate:** `./firmware/build.sh` compiles clean (168 KB flash / 20%, 16.5 KB RAM / 6%)
- [x] 8.10 Commit

## Phase 9 — iOS companion app
- [x] 9.1 Xcode project (xcodegen spec + committed `.xcodeproj`, shared scheme `Phoenix`), SwiftUI, iOS 17 target
- [x] 9.2 `PhoenixTransport` protocol; `BleTransport` (CoreBluetooth central: scan by service UUID, connect, MTU-sized chunked TX writes, subscribe RX, state preservation/restoration with `willRestoreState`, `bluetooth-central` background mode) and `FakeTransport` (full virtual glasses; entire app usable in the Simulator)
- [x] 9.3 Reconnection with exponential backoff + full jitter (pure, injectable RNG, unit-tested)
- [x] 9.4 Swift protocol codec + stream decoder, validated byte-for-byte against C++-core-generated fixtures; hostile-input tests mirror the C++ fuzz suite
- [x] 9.5 Speech-to-text: `SFSpeechRecognizer`, on-device preference, `AVAudioSession` Bluetooth-headset routing, in-app push-to-talk button + text input fallback
- [x] 9.6 Streaming LLM client (OpenAI-compatible SSE); key from Keychain or gitignored xcconfig; offline echo mode when unconfigured
- [x] 9.7 Virtual glasses view: 72x40 SwiftUI Canvas driven by the same generated font/sprite bytes as the firmware
- [x] 9.8 Debug view: raw frames both directions, hex + decoded
- [x] 9.9 XCTest: codec + fuzz, backoff, transport abstraction, renderer/wrap parity, app-model pipeline (39 cases)
- [x] 9.10 **Gate:** `xcodebuild -scheme Phoenix -destination 'platform=iOS Simulator,name=iPhone 15' build test` succeeds (39/39); app also launched and screenshotted running on the Simulator
- [x] 9.11 Commit

## Phase 10 — Documentation & final verification
- [x] 10.1 `README.md`: exact copy-paste commands first, architecture overview, layout, testing notes, font/scenario workflow, known limitations (Simulator BLE, force-quit background kill, 7-day free provisioning, accessory-side MTU/interval limits, firmware verified by compilation only, ASCII-only glyph set)
- [x] 10.2 `DECISIONS.md` complete (30 entries); `PLAN.md` fully checked off
- [x] 10.3 Full clean-room run of the entire Definition of Done command list — build trees, sim output, and Xcode DerivedData deleted first:
      · `cmake -B build && cmake --build build && ctest` → 8/8 suites pass
      · `./build/sim/phoenix_sim --all` → 10 scenarios, 629 frames, 1.4 MB `sim/out/index.html`
      · `./firmware/build.sh` → clean compile, 20% flash / 6% RAM
      · `xcodebuild … name=iPhone 15 build test` → BUILD + TEST SUCCEEDED, 39/39
- [x] 10.4 Final commit

---

## Result

| Component | Verification | Status |
|---|---|---|
| `core/` | 8 ctest suites (~100 cases) incl. 12 golden frames, protocol fuzz; clean under ASan/UBSan | green |
| `sim/` | 10 scenarios → self-contained HTML player, reviewed in a live browser | green |
| `firmware/` | `arduino-cli` compile for `Seeeduino:nrf52:xiaonRF52840` (no hardware to flash) | green |
| `ios/` | `xcodebuild` Simulator build + 39 XCTest cases; app launched and screenshotted | green |
