#pragma once

#include <phoenix/framebuffer.h>

// SSD1306 0.42" 72x40 behind U8g2. The dedicated 72x40 constructor carries
// the panel's quirks (column offset 28, multiplex 0x27, display offset 0x0C)
// so none of that lives here.
namespace display {

void begin();
void push(const phoenix::FrameBuffer& fb);
void setBrightness(uint8_t level);   // SSD1306 contrast
void setPowerSave(bool off);         // true = panel off
bool isOn();

}  // namespace display
