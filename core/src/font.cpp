#include "phoenix/font.h"

namespace phoenix {

int drawChar(FrameBuffer& fb, const Font& font, int x, int y, char c, bool on) {
  const GlyphInfo& g = font.glyph(c);
  const int bytesPerRow = (g.width + 7) / 8;
  const uint8_t* rows = font.bitmap + g.offset;
  for (int row = 0; row < font.height; ++row) {
    for (int col = 0; col < g.width; ++col) {
      if (rows[row * bytesPerRow + col / 8] & (0x80u >> (col & 7))) {
        fb.setPixel(x + col, y + row, on);
      }
    }
  }
  return g.width + font.tracking;
}

int drawText(FrameBuffer& fb, const Font& font, int x, int y,
             const std::string& s, bool on) {
  int cx = x;
  for (char c : s) {
    // Fully off-screen to the right: nothing further can draw.
    if (cx >= kDisplayWidth) break;
    cx += drawChar(fb, font, cx, y, c, on);
  }
  return cx - x;
}

TextMetrics measureText(const Font& font, const std::string& s) {
  int w = 0;
  for (char c : s) {
    w += font.advance(c);
  }
  if (!s.empty()) w -= font.tracking;  // no tracking after the last glyph
  return TextMetrics{w, font.height};
}

}  // namespace phoenix
