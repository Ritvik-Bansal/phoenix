#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "phoenix/font.h"

namespace phoenix {

// Word-wraps display text (already sanitized, see text_sanitize.h) into lines
// no wider than widthPx.
//   - breaks on spaces; runs of spaces collapse at break points
//   - a word wider than widthPx hard-breaks mid-word (never an infinite loop,
//     even when widthPx is narrower than a single glyph: one char per line)
//   - '\n' forces a line break; "a\n\nb" yields an empty middle line
//   - returns {} for empty input; lines never carry surrounding spaces
std::vector<std::string> wrapText(const Font& font, const std::string& text,
                                  int widthPx);

// Single line: unchanged if it fits, otherwise cut so that text + "..." fits
// in widthPx. Degenerates to bare dots (possibly none) when even one char
// plus the ellipsis cannot fit. Result always measures <= widthPx.
std::string truncateWithEllipsis(const Font& font, const std::string& text,
                                 int widthPx);

struct MarqueeConfig {
  int pxPerTick = 1;        // scroll speed, px per tick (10 ticks = 1 s)
  int startPauseTicks = 8;  // hold at rest at the start of every cycle
  int gapPx = 12;           // blank gap between text end and its wrapped copy
};

// Deterministic horizontal scroller for one over-wide line: a pure function
// of the tick count, therefore directly testable. The text scrolls left by
// pxPerTick each tick after an initial pause; a second copy follows gapPx
// behind so the loop is seamless.
class Marquee {
 public:
  Marquee() = default;
  Marquee(int textWidthPx, int viewWidthPx, MarqueeConfig cfg = {});

  bool isStatic() const { return textW_ <= viewW_; }
  // textWidth + gap: scroll distance of one full loop; where copy #2 draws.
  int wrapAdvancePx() const { return textW_ + cfg_.gapPx; }
  // Full period in ticks; 0 when static.
  int cycleTicks() const;
  // Scroll offset in px at a tick. 0 while static/paused; capped at
  // wrapAdvancePx() (which renders identically to 0).
  int offsetAt(uint32_t tick) const;

 private:
  int textW_ = 0;
  int viewW_ = 0;
  MarqueeConfig cfg_{};
};

// Renders a marqueed line at (x, y), clipped to [x, x + viewWidthPx).
void drawMarqueeText(FrameBuffer& fb, const Font& font, int x, int y,
                     int viewWidthPx, const std::string& text, const Marquee& m,
                     uint32_t tick, bool on = true);

struct PagerConfig {
  int linesPerPage = 4;
  int ticksPerPage = 25;  // auto-advance period; 0 = no auto-advance
};

// Splits over-tall text (a wrapped line count) into pages and resolves the
// visible page for a tick. Pure; screens that support a manual "next page"
// button jump their local tick to the next page boundary.
class VerticalPager {
 public:
  VerticalPager() = default;
  explicit VerticalPager(int lineCount, PagerConfig cfg = {});

  int pageCount() const;
  int pageAt(uint32_t tick) const;  // clamps to the last page, no wrap-around
  int firstLine(int page) const;
  int lineCountOn(int page) const;
  // True once every page has had its ticksPerPage on screen.
  bool finishedAt(uint32_t tick) const;
  // Smallest tick T > tick at which pageAt advances (for manual next-page).
  uint32_t nextBoundaryAfter(uint32_t tick) const;

 private:
  int lines_ = 0;
  PagerConfig cfg_{};
};

}  // namespace phoenix
