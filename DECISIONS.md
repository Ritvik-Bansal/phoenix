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
