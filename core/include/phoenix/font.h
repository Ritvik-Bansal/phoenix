#pragma once

#include <cstdint>
#include <string>

#include "phoenix/framebuffer.h"

namespace phoenix {

// One entry per character in [firstChar, lastChar]. width == 0 marks a gap in
// a sparse font (e.g. the clock font covers only digits, ':' and '.'); the
// renderer substitutes the font's fallback glyph.
struct GlyphInfo {
  uint16_t offset;  // byte offset into Font::bitmap
  uint8_t width;    // glyph width in pixels, 0 = absent
};

struct TextMetrics {
  int width;   // exact ink+tracking width in px (no trailing tracking)
  int height;  // font height in px
};

// Bitmap layout: per glyph, row-major, MSB-first, each row padded to whole
// bytes — produced by tools/gen_fonts.py from the ASCII-art sources in
// tools/fonts/.
struct Font {
  uint8_t height;
  uint8_t firstChar;
  uint8_t lastChar;
  uint8_t tracking;      // px advance added after every glyph
  uint8_t fallbackChar;  // substituted for any char outside the font
  const GlyphInfo* glyphs;
  const uint8_t* bitmap;

  bool hasGlyph(char c) const {
    const uint8_t u = static_cast<uint8_t>(c);
    return u >= firstChar && u <= lastChar && glyphs[u - firstChar].width > 0;
  }
  const GlyphInfo& glyph(char c) const {
    const uint8_t u = hasGlyph(c) ? static_cast<uint8_t>(c) : fallbackChar;
    return glyphs[u - firstChar];
  }
  // Horizontal space a character occupies including tracking.
  int advance(char c) const { return glyph(c).width + tracking; }
};

// Draws one glyph with its top-left at (x, y); returns its advance.
int drawChar(FrameBuffer& fb, const Font& font, int x, int y, char c,
             bool on = true);

// Draws a single line of display text (already sanitized to the font's byte
// range — see text_sanitize.h); returns the drawn width in px.
int drawText(FrameBuffer& fb, const Font& font, int x, int y,
             const std::string& s, bool on = true);

TextMetrics measureText(const Font& font, const std::string& s);

}  // namespace phoenix
