#include "phoenix/layout.h"

namespace phoenix {
namespace {

// width(a + b) == width(a) + tracking + width(b) for nonempty a, b.
int joinedWidth(const Font& font, int leftW, int rightW) {
  return leftW + font.tracking + rightW;
}

// Longest prefix of `word` measuring <= widthPx, but at least one char so
// pathological widths still make progress.
size_t hardBreakPrefix(const Font& font, const std::string& word, int widthPx) {
  int w = 0;
  for (size_t i = 0; i < word.size(); ++i) {
    const int glyphW = font.glyph(word[i]).width;
    const int cand = i == 0 ? glyphW : joinedWidth(font, w, glyphW);
    if (cand > widthPx && i > 0) return i;
    w = cand;
  }
  return word.size();
}

}  // namespace

std::vector<std::string> wrapText(const Font& font, const std::string& text,
                                  int widthPx) {
  std::vector<std::string> lines;
  if (text.empty()) return lines;

  size_t paraStart = 0;
  while (paraStart <= text.size()) {
    size_t paraEnd = text.find('\n', paraStart);
    const bool lastPara = paraEnd == std::string::npos;
    const std::string para =
        text.substr(paraStart, lastPara ? std::string::npos : paraEnd - paraStart);

    std::string cur;
    int curW = 0;
    bool emitted = false;
    size_t i = 0;
    while (i < para.size()) {
      if (para[i] == ' ') {  // collapse space runs at break points
        ++i;
        continue;
      }
      size_t j = para.find(' ', i);
      if (j == std::string::npos) j = para.size();
      std::string word = para.substr(i, j - i);
      i = j;

      while (!word.empty()) {
        const int wordW = measureText(font, word).width;
        if (cur.empty()) {
          if (wordW <= widthPx) {
            cur = word;
            curW = wordW;
            word.clear();
          } else {
            const size_t cut = hardBreakPrefix(font, word, widthPx);
            lines.push_back(word.substr(0, cut));
            emitted = true;
            word.erase(0, cut);
          }
        } else {
          const int spaceW = font.glyph(' ').width;
          const int candW =
              joinedWidth(font, joinedWidth(font, curW, spaceW), wordW);
          if (candW <= widthPx) {
            cur += ' ';
            cur += word;
            curW = candW;
            word.clear();
          } else {
            lines.push_back(cur);
            emitted = true;
            cur.clear();
            curW = 0;
          }
        }
      }
    }
    if (!cur.empty() || !emitted) lines.push_back(cur);
    if (lastPara) break;
    paraStart = paraEnd + 1;
  }
  return lines;
}

std::string truncateWithEllipsis(const Font& font, const std::string& text,
                                 int widthPx) {
  if (measureText(font, text).width <= widthPx) return text;

  static const std::string kEllipsis = "...";
  const int ellW = measureText(font, kEllipsis).width;

  // Longest prefix such that prefix + "..." fits.
  int w = 0;
  size_t best = 0;
  for (size_t i = 0; i < text.size(); ++i) {
    const int glyphW = font.glyph(text[i]).width;
    w = i == 0 ? glyphW : joinedWidth(font, w, glyphW);
    if (joinedWidth(font, w, ellW) <= widthPx) best = i + 1;
  }
  if (best > 0) return text.substr(0, best) + kEllipsis;

  // Not even one char + ellipsis: as many bare dots as fit.
  std::string dots = kEllipsis;
  while (!dots.empty() && measureText(font, dots).width > widthPx) {
    dots.pop_back();
  }
  return dots;
}

Marquee::Marquee(int textWidthPx, int viewWidthPx, MarqueeConfig cfg)
    : textW_(textWidthPx), viewW_(viewWidthPx), cfg_(cfg) {
  if (cfg_.pxPerTick < 1) cfg_.pxPerTick = 1;
  if (cfg_.startPauseTicks < 0) cfg_.startPauseTicks = 0;
  if (cfg_.gapPx < 0) cfg_.gapPx = 0;
}

int Marquee::cycleTicks() const {
  if (isStatic()) return 0;
  const int dist = wrapAdvancePx();
  const int scrollTicks = (dist + cfg_.pxPerTick - 1) / cfg_.pxPerTick;
  return cfg_.startPauseTicks + scrollTicks;
}

int Marquee::offsetAt(uint32_t tick) const {
  if (isStatic()) return 0;
  const int cycle = cycleTicks();
  const int p = static_cast<int>(tick % static_cast<uint32_t>(cycle));
  if (p < cfg_.startPauseTicks) return 0;
  const int o = (p - cfg_.startPauseTicks + 1) * cfg_.pxPerTick;
  const int dist = wrapAdvancePx();
  return o > dist ? dist : o;
}

void drawMarqueeText(FrameBuffer& fb, const Font& font, int x, int y,
                     int viewWidthPx, const std::string& text, const Marquee& m,
                     uint32_t tick, bool on) {
  const int clipX0 = x;
  const int clipX1 = x + viewWidthPx;
  if (m.isStatic()) {
    drawTextClipped(fb, font, x, y, text, clipX0, clipX1, on);
    return;
  }
  const int off = m.offsetAt(tick);
  drawTextClipped(fb, font, x - off, y, text, clipX0, clipX1, on);
  // Second copy trails gapPx behind so the loop is seamless once the first
  // copy scrolls out.
  drawTextClipped(fb, font, x - off + m.wrapAdvancePx(), y, text, clipX0,
                  clipX1, on);
}

VerticalPager::VerticalPager(int lineCount, PagerConfig cfg)
    : lines_(lineCount < 0 ? 0 : lineCount), cfg_(cfg) {
  if (cfg_.linesPerPage < 1) cfg_.linesPerPage = 1;
  if (cfg_.ticksPerPage < 0) cfg_.ticksPerPage = 0;
}

int VerticalPager::pageCount() const {
  if (lines_ == 0) return 1;
  return (lines_ + cfg_.linesPerPage - 1) / cfg_.linesPerPage;
}

int VerticalPager::pageAt(uint32_t tick) const {
  if (cfg_.ticksPerPage == 0) return 0;
  const uint32_t p = tick / static_cast<uint32_t>(cfg_.ticksPerPage);
  const int last = pageCount() - 1;
  return p > static_cast<uint32_t>(last) ? last : static_cast<int>(p);
}

int VerticalPager::firstLine(int page) const {
  return page * cfg_.linesPerPage;
}

int VerticalPager::lineCountOn(int page) const {
  const int first = firstLine(page);
  if (first >= lines_) return 0;
  const int rest = lines_ - first;
  return rest < cfg_.linesPerPage ? rest : cfg_.linesPerPage;
}

bool VerticalPager::finishedAt(uint32_t tick) const {
  if (cfg_.ticksPerPage == 0) return false;
  return tick >= static_cast<uint32_t>(pageCount()) *
                     static_cast<uint32_t>(cfg_.ticksPerPage);
}

uint32_t VerticalPager::nextBoundaryAfter(uint32_t tick) const {
  if (cfg_.ticksPerPage == 0) return tick + 1;
  const uint32_t period = static_cast<uint32_t>(cfg_.ticksPerPage);
  return (tick / period + 1) * period;
}

}  // namespace phoenix
