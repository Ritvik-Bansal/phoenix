# Decisions

One line per judgement call: `[AREA] Chose X over Y because Z`.

- [REPO] Chose to root the repository at `Downloads/jarvis/phoenix/` (a new `phoenix/` directory inside the empty working directory) over making `jarvis/` itself the repo because the spec's layout diagram is explicitly rooted at `phoenix/`.
- [BUILD] Chose CMake minimum 3.20 over something older because the Definition of Done uses `ctest --test-dir`, which was added in 3.20.
- [TIME] Chose a global tick of 100 ms (10 Hz) over a faster tick because it is fast enough for readable marquee/pager motion, keeps golden tests small, and gives the firmware an easy low-power render cadence.
- [FONTS] Chose a proportional 5x7 body font (per-glyph widths) over fixed-pitch because on a 72 px line it buys 1–3 extra characters and the spec requires per-glyph advance anyway.
