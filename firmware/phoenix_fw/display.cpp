#include "display.h"

#include <U8g2lib.h>
#include <Wire.h>

#include <cstring>

namespace display {
namespace {

// Full-buffer variant: we blast the core's framebuffer straight into U8g2's
// buffer, so the panel gets exactly the pixels the simulator shows.
U8G2_SSD1306_72X40_ER_F_HW_I2C u8g2(U8G2_R0, /*reset=*/U8X8_PIN_NONE);

bool poweredOn = true;

}  // namespace

void begin() {
  u8g2.begin();
  u8g2.setContrast(255);
  poweredOn = true;
}

void push(const phoenix::FrameBuffer& fb) {
  // Both sides use SSD1306 page order (byte = 8-row column strip, LSB on
  // top), so when the buffer geometry matches we can memcpy. If a future
  // U8g2 changes its internal layout the pixel loop keeps us correct.
  const size_t u8g2BufferSize = static_cast<size_t>(u8g2.getBufferTileWidth()) *
                                8u * u8g2.getBufferTileHeight();
  if (u8g2BufferSize == phoenix::FrameBuffer::kBufferSize) {
    std::memcpy(u8g2.getBufferPtr(), fb.pages(),
                phoenix::FrameBuffer::kBufferSize);
  } else {
    u8g2.clearBuffer();
    for (int y = 0; y < phoenix::kDisplayHeight; ++y) {
      for (int x = 0; x < phoenix::kDisplayWidth; ++x) {
        if (fb.getPixel(x, y)) u8g2.drawPixel(x, y);
      }
    }
  }
  u8g2.sendBuffer();
}

void setBrightness(uint8_t level) { u8g2.setContrast(level); }

void setPowerSave(bool off) {
  u8g2.setPowerSave(off ? 1 : 0);
  poweredOn = !off;
}

bool isOn() { return poweredOn; }

}  // namespace display
