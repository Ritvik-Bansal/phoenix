#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace phoenix {

// Display geometry: 0.42" SSD1306, 72x40, 1bpp.
inline constexpr int kDisplayWidth = 72;
inline constexpr int kDisplayHeight = 40;
inline constexpr int kDisplayPages = (kDisplayHeight + 7) / 8;  // 5 pages of 8 rows

// A small 1bpp raster (icons, arrows, splash art).
// Layout: row-major, MSB-first within each byte, each row padded to a whole
// number of bytes. Produced by tools/gen_fonts.py from ASCII-art sources.
struct Sprite {
  uint8_t width;
  uint8_t height;
  const uint8_t* data;
};

// Fixed 72x40 1bpp framebuffer packed in SSD1306 page order:
// byte index = page * width + x, where page = y / 8; bit (y % 8) of that byte
// is the pixel, LSB = topmost row of the page. This matches the SSD1306's
// GDDRAM layout (and U8g2's full-buffer layout for this panel), so the
// firmware can push the raw buffer without any repacking.
class FrameBuffer {
 public:
  static constexpr size_t kBufferSize =
      static_cast<size_t>(kDisplayPages) * kDisplayWidth;

  FrameBuffer() { clear(); }

  void clear(bool on = false);

  // All drawing is clipped to the display bounds; out-of-range coordinates are
  // safely ignored.
  void setPixel(int x, int y, bool on = true);
  bool getPixel(int x, int y) const;

  void fillRect(int x, int y, int w, int h, bool on = true);
  void drawRect(int x, int y, int w, int h, bool on = true);
  void drawLine(int x0, int y0, int x1, int y1, bool on = true);

  // Draws the sprite's set bits at (x, y); clear bits leave the buffer alone,
  // so sprites compose over existing content.
  void blit(const Sprite& sprite, int x, int y, bool on = true);

  // Raw page-order buffer, ready for an SSD1306 data write.
  const uint8_t* pages() const { return buf_; }
  uint8_t* pages() { return buf_; }

  bool operator==(const FrameBuffer& other) const;
  bool operator!=(const FrameBuffer& other) const { return !(*this == other); }

  // ASCII-art dump (kDisplayHeight lines, '\n'-terminated), used by the golden
  // tests and the simulator's debug output.
  std::string toAscii(char onCh = '#', char offCh = '.') const;

 private:
  uint8_t buf_[kBufferSize];
};

}  // namespace phoenix
