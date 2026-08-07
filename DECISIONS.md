# Decisions

One line per judgement call: `[AREA] Chose X over Y because Z`.

- [REPO] Chose to root the repository at `Downloads/jarvis/phoenix/` (a new `phoenix/` directory inside the empty working directory) over making `jarvis/` itself the repo because the spec's layout diagram is explicitly rooted at `phoenix/`.
- [BUILD] Chose CMake minimum 3.20 over something older because the Definition of Done uses `ctest --test-dir`, which was added in 3.20.
- [TIME] Chose a global tick of 100 ms (10 Hz) over a faster tick because it is fast enough for readable marquee/pager motion, keeps golden tests small, and gives the firmware an easy low-power render cadence.
- [FONTS] Chose a proportional 5x7 body font (per-glyph widths) over fixed-pitch because on a 72 px line it buys 1–3 extra characters and the spec requires per-glyph advance anyway.
- [CODEC] Chose CRC-16/CCITT-FALSE over CRC-8 because 2 bytes per frame is cheap and it catches all single/double-bit errors at these frame sizes; check value 0x29B1 is locked by a test so the doc and code cannot drift.
- [CODEC] Chose resync-by-one-byte after a bad frame (drop only the start byte, rescan) over skipping the whole claimed frame because a real frame may begin inside garbage that faked a header; the corruption sweep test proves subsequent frames always recover.
- [CODEC] Chose a 4096-byte decoder buffer cap with oldest-byte drop over unbounded buffering because this parses radio input on a microcontroller; the cap is ~8x the largest legal frame.
- [TESTS] Chose separate `build-asan/` sanitizer runs over enabling ASan in the default build because the user-facing build should stay vanilla; the fuzz suite runs green under ASan+UBSan.
- [ANCS] Chose to suppress screens for `PreExisting`-flagged notifications (stored but not popped) over showing everything because iOS replays the entire notification backlog on every reconnect and flooding the HUD with stale items would make reconnects unusable.
- [ANCS] Chose to show silent-flagged notifications normally over suppressing them because a glanceable HUD is the point of the device; silent only means the phone didn't chime.
- [SCREENS] Chose restart-timers-on-resume for preempted screens over accumulating shown-time because after a call interrupts a notification the wearer needs the full display window again, and it keeps the state machine memoryless.
- [SCREENS] Chose B-on-last-page = dismiss for notifications (and wrap-to-start for assistant replies) because notification triage is linear while assistant replies get re-read.
- [SCREENS] Chose idle screen = Clock when connected / Status when not, with button C summoning Status as a timed overlay, so StatusScreen has one implementation for both roles.
- [DEVICE] Chose to drop all phone-fed content (ANCS store, nav, assistant) on disconnect because it is stale the moment the link dies and iOS replays pre-existing notifications on reconnect anyway.
- [DEVICE] Chose battery reports on connect + every 60 s + on ≥5% change over a fixed period only, to keep BLE traffic minimal while the phone still tracks the pack closely.
- [SIM] Chose a plain-text scenario DSL over JSON because the core stays zero-dependency (no JSON parser to vendor) and line-oriented commands with comments read like a screenplay.
- [SIM] Chose to feed protocol scenario events through real encoded frames in 20-byte chunks over calling device methods directly, so every simulator run also exercises the wire codec and reassembly path.
- [SIM] Chose frames as run-length SVG path data reused by both the film strip and the player (strip built by inline JS) over fully static markup, because playback needs the frame data in JS anyway and duplicating hundreds of frames as static SVG would double the file.
- [SIM] Chose IntersectionObserver-paused players and a single small glow filter after live testing showed ten simultaneously animating filtered SVGs strain the renderer.
- [SIM] Chose to render SET_BRIGHTNESS as panel glow opacity in the player because a 1bpp framebuffer has no gray levels — dimming is a property of the panel, not the pixels.
