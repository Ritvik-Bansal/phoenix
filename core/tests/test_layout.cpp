#include <string>
#include <vector>

#include "phoenix/font_data.h"
#include "phoenix/layout.h"
#include "test_framework.h"

using namespace phoenix;

namespace {
int w(const std::string& s) { return measureText(kFontBody, s).width; }
}  // namespace

TEST(wrap_boundary_widths) {
  // Exactly wide enough for both words on one line.
  const int both = w("alpha beta");
  CHECK_EQ(wrapText(kFontBody, "alpha beta", both).size(), size_t(1));
  // One pixel narrower: must split.
  const auto lines = wrapText(kFontBody, "alpha beta", both - 1);
  CHECK_EQ(lines.size(), size_t(2));
  CHECK_EQ(lines[0], "alpha");
  CHECK_EQ(lines[1], "beta");
  // Exactly the first word's width.
  const auto tight = wrapText(kFontBody, "alpha beta", w("alpha"));
  CHECK_EQ(tight.size(), size_t(2));
  CHECK_EQ(tight[0], "alpha");
}

TEST(wrap_every_line_fits) {
  const std::string text =
      "Meet me at the coffee shop on Market Street at noon tomorrow";
  for (int width : {20, 30, 40, 60, 72}) {
    std::string rejoined;
    for (const auto& line : wrapText(kFontBody, text, width)) {
      CHECK(w(line) <= width);
      for (char c : line) {
        if (c != ' ') rejoined.push_back(c);
      }
    }
    // No characters lost or invented (spaces collapse, letters survive).
    std::string original;
    for (char c : text) {
      if (c != ' ') original.push_back(c);
    }
    CHECK_EQ(rejoined, original);
  }
}

TEST(wrap_hard_breaks_overlong_word) {
  const std::string word = "Pneumonoultramicroscopicsilicovolcanoconiosis";
  const auto lines = wrapText(kFontBody, word, 30);
  CHECK(lines.size() >= 2);
  std::string joined;
  for (const auto& line : lines) {
    CHECK(!line.empty());
    CHECK(w(line) <= 30);
    joined += line;
  }
  CHECK_EQ(joined, word);
  // Greedy: each line takes the longest prefix that fits.
  for (size_t i = 0; i + 1 < lines.size(); ++i) {
    CHECK(w(lines[i] + lines[i + 1][0]) > 30);
  }
}

TEST(wrap_newlines_and_spaces) {
  const auto lines = wrapText(kFontBody, "a\n\nb", 72);
  CHECK_EQ(lines.size(), size_t(3));
  CHECK_EQ(lines[0], "a");
  CHECK_EQ(lines[1], "");
  CHECK_EQ(lines[2], "b");

  const auto collapsed = wrapText(kFontBody, "a    b", 72);
  CHECK_EQ(collapsed.size(), size_t(1));
  CHECK_EQ(collapsed[0], "a b");

  const auto trimmed = wrapText(kFontBody, "  hi  ", 72);
  CHECK_EQ(trimmed.size(), size_t(1));
  CHECK_EQ(trimmed[0], "hi");

  CHECK(wrapText(kFontBody, "", 72).empty());
}

TEST(wrap_pathological_narrow_width) {
  // Width narrower than the glyph: one char per line, still terminates.
  const auto lines = wrapText(kFontBody, "WWW", 2);
  CHECK_EQ(lines.size(), size_t(3));
  for (const auto& line : lines) CHECK_EQ(line, "W");
}

TEST(truncate_with_ellipsis) {
  CHECK_EQ(truncateWithEllipsis(kFontBody, "short", 72), "short");
  const std::string longText = "This subject line is far too long";
  const std::string cut = truncateWithEllipsis(kFontBody, longText, 60);
  CHECK(w(cut) <= 60);
  CHECK_EQ(cut.substr(cut.size() - 3), "...");
  CHECK_EQ(cut.substr(0, 4), longText.substr(0, 4));
  // Exact fit is not truncated.
  CHECK_EQ(truncateWithEllipsis(kFontBody, "abc", w("abc")), "abc");
  // One px short of exact fit is.
  const std::string nearly = truncateWithEllipsis(kFontBody, "abcdef", w("abcdef") - 1);
  CHECK(nearly != "abcdef");
  CHECK(w(nearly) <= w("abcdef") - 1);
  // Degenerate widths: bare dots, never wider than the budget.
  CHECK_EQ(truncateWithEllipsis(kFontBody, "hello world", w("...")), "...");
  CHECK(w(truncateWithEllipsis(kFontBody, "hello", 3)) <= 3);
  CHECK_EQ(truncateWithEllipsis(kFontBody, "hello", 0), "");
}

TEST(marquee_static_when_it_fits) {
  const Marquee m(50, 60);
  CHECK(m.isStatic());
  CHECK_EQ(m.cycleTicks(), 0);
  for (uint32_t t : {0u, 1u, 100u, 100000u}) CHECK_EQ(m.offsetAt(t), 0);
}

TEST(marquee_full_cycle) {
  MarqueeConfig cfg;
  cfg.pxPerTick = 1;
  cfg.startPauseTicks = 3;
  cfg.gapPx = 12;
  const Marquee m(100, 60, cfg);  // dist = 112
  CHECK(!m.isStatic());
  CHECK_EQ(m.wrapAdvancePx(), 112);
  CHECK_EQ(m.cycleTicks(), 3 + 112);
  // Pause holds at 0.
  CHECK_EQ(m.offsetAt(0), 0);
  CHECK_EQ(m.offsetAt(2), 0);
  // Scroll advances 1 px/tick.
  CHECK_EQ(m.offsetAt(3), 1);
  CHECK_EQ(m.offsetAt(4), 2);
  CHECK_EQ(m.offsetAt(114), 112);  // end of travel == wrap point
  // Next tick is a fresh cycle.
  CHECK_EQ(m.offsetAt(115), 0);
  CHECK_EQ(m.offsetAt(115 + 3), 1);
  // Deterministic across cycles.
  for (uint32_t t = 0; t < 115; ++t) {
    CHECK_EQ(m.offsetAt(t), m.offsetAt(t + 115));
    CHECK(m.offsetAt(t) >= 0);
    CHECK(m.offsetAt(t) <= 112);
  }
}

TEST(marquee_speed_not_dividing_distance) {
  MarqueeConfig cfg;
  cfg.pxPerTick = 3;
  cfg.startPauseTicks = 0;
  cfg.gapPx = 0;
  const Marquee m(10, 5, cfg);  // dist 10, 3 px/tick -> 4 scroll ticks
  CHECK_EQ(m.cycleTicks(), 4);
  CHECK_EQ(m.offsetAt(0), 3);
  CHECK_EQ(m.offsetAt(2), 9);
  CHECK_EQ(m.offsetAt(3), 10);  // clamped to the wrap point
  CHECK_EQ(m.offsetAt(4), 3);   // wrapped
}

TEST(marquee_draw_clips_to_viewport) {
  MarqueeConfig cfg;
  cfg.startPauseTicks = 0;
  const std::string text = "WWWWWWWWWWWWWW";  // far wider than the view
  const int textW = w(text);
  const Marquee m(textW, 30, cfg);
  FrameBuffer fb;
  fb.setPixel(9, 3);   // sentinel left of the viewport
  fb.setPixel(41, 3);  // sentinel right of the viewport
  drawMarqueeText(fb, kFontBody, 10, 0, 30, text, m, 5);
  for (int y = 0; y < 12; ++y) {
    for (int x = 0; x < 72; ++x) {
      if (x >= 10 && x < 40) continue;   // inside viewport: anything goes
      const bool sentinel = (x == 9 || x == 41) && y == 3;
      CHECK_EQ(fb.getPixel(x, y), sentinel);
    }
  }
  // Something was actually drawn inside the viewport.
  int lit = 0;
  for (int y = 0; y < 8; ++y)
    for (int x = 10; x < 40; ++x)
      if (fb.getPixel(x, y)) ++lit;
  CHECK(lit > 10);
}

TEST(pager_page_math) {
  PagerConfig cfg;
  cfg.linesPerPage = 4;
  cfg.ticksPerPage = 25;
  const VerticalPager p(10, cfg);  // 3 pages: 4 + 4 + 2 lines
  CHECK_EQ(p.pageCount(), 3);
  CHECK_EQ(p.firstLine(0), 0);
  CHECK_EQ(p.firstLine(2), 8);
  CHECK_EQ(p.lineCountOn(0), 4);
  CHECK_EQ(p.lineCountOn(2), 2);
  CHECK_EQ(p.pageAt(0), 0);
  CHECK_EQ(p.pageAt(24), 0);
  CHECK_EQ(p.pageAt(25), 1);
  CHECK_EQ(p.pageAt(74), 2);
  CHECK_EQ(p.pageAt(1000), 2);  // clamps, no wrap
  CHECK(!p.finishedAt(74));
  CHECK(p.finishedAt(75));
  CHECK_EQ(p.nextBoundaryAfter(0), uint32_t(25));
  CHECK_EQ(p.nextBoundaryAfter(25), uint32_t(50));
  CHECK_EQ(p.nextBoundaryAfter(26), uint32_t(50));
}

TEST(pager_degenerate_cases) {
  const VerticalPager empty(0);
  CHECK_EQ(empty.pageCount(), 1);
  CHECK_EQ(empty.lineCountOn(0), 0);

  PagerConfig manual;
  manual.ticksPerPage = 0;
  const VerticalPager p(9, manual);
  CHECK_EQ(p.pageAt(100000), 0);  // no auto-advance
  CHECK(!p.finishedAt(100000));

  PagerConfig one;
  one.linesPerPage = 4;
  one.ticksPerPage = 25;
  const VerticalPager single(3, one);
  CHECK_EQ(single.pageCount(), 1);
  CHECK(single.finishedAt(25));
}

TESTFW_MAIN
