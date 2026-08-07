#include "phoenix/framebuffer.h"

#include <cstring>

namespace phoenix {

void FrameBuffer::clear(bool on) {
  std::memset(buf_, on ? 0xFF : 0x00, kBufferSize);
}

void FrameBuffer::setPixel(int x, int y, bool on) {
  if (x < 0 || x >= kDisplayWidth || y < 0 || y >= kDisplayHeight) return;
  const size_t idx = static_cast<size_t>(y >> 3) * kDisplayWidth + x;
  const uint8_t bit = static_cast<uint8_t>(1u << (y & 7));
  if (on) {
    buf_[idx] |= bit;
  } else {
    buf_[idx] &= static_cast<uint8_t>(~bit);
  }
}

bool FrameBuffer::getPixel(int x, int y) const {
  if (x < 0 || x >= kDisplayWidth || y < 0 || y >= kDisplayHeight) return false;
  const size_t idx = static_cast<size_t>(y >> 3) * kDisplayWidth + x;
  return (buf_[idx] >> (y & 7)) & 1u;
}

void FrameBuffer::fillRect(int x, int y, int w, int h, bool on) {
  if (w <= 0 || h <= 0) return;
  int x0 = x < 0 ? 0 : x;
  int y0 = y < 0 ? 0 : y;
  int x1 = x + w;
  int y1 = y + h;
  if (x1 > kDisplayWidth) x1 = kDisplayWidth;
  if (y1 > kDisplayHeight) y1 = kDisplayHeight;
  for (int yy = y0; yy < y1; ++yy) {
    for (int xx = x0; xx < x1; ++xx) {
      setPixel(xx, yy, on);
    }
  }
}

void FrameBuffer::drawRect(int x, int y, int w, int h, bool on) {
  if (w <= 0 || h <= 0) return;
  drawLine(x, y, x + w - 1, y, on);
  drawLine(x, y + h - 1, x + w - 1, y + h - 1, on);
  drawLine(x, y, x, y + h - 1, on);
  drawLine(x + w - 1, y, x + w - 1, y + h - 1, on);
}

void FrameBuffer::drawLine(int x0, int y0, int x1, int y1, bool on) {
  // Bresenham. Endpoints may be anywhere; setPixel clips.
  int dx = x1 > x0 ? x1 - x0 : x0 - x1;
  int dy = y1 > y0 ? y1 - y0 : y0 - y1;
  int sx = x0 < x1 ? 1 : -1;
  int sy = y0 < y1 ? 1 : -1;
  int err = dx - dy;
  for (;;) {
    setPixel(x0, y0, on);
    if (x0 == x1 && y0 == y1) break;
    int e2 = err * 2;
    if (e2 > -dy) {
      err -= dy;
      x0 += sx;
    }
    if (e2 < dx) {
      err += dx;
      y0 += sy;
    }
  }
}

void FrameBuffer::blit(const Sprite& sprite, int x, int y, bool on) {
  if (sprite.data == nullptr) return;
  const int bytesPerRow = (sprite.width + 7) / 8;
  for (int row = 0; row < sprite.height; ++row) {
    for (int col = 0; col < sprite.width; ++col) {
      const uint8_t byte = sprite.data[row * bytesPerRow + col / 8];
      if (byte & (0x80u >> (col & 7))) {
        setPixel(x + col, y + row, on);
      }
    }
  }
}

bool FrameBuffer::operator==(const FrameBuffer& other) const {
  return std::memcmp(buf_, other.buf_, kBufferSize) == 0;
}

std::string FrameBuffer::toAscii(char onCh, char offCh) const {
  std::string out;
  out.reserve(static_cast<size_t>(kDisplayHeight) * (kDisplayWidth + 1));
  for (int y = 0; y < kDisplayHeight; ++y) {
    for (int x = 0; x < kDisplayWidth; ++x) {
      out.push_back(getPixel(x, y) ? onCh : offCh);
    }
    out.push_back('\n');
  }
  return out;
}

}  // namespace phoenix
