#pragma once

#include <Arduino.h>

// Three tactile buttons, active low with internal pull-ups, software
// debounced. Short press fires on release; long press fires once at the
// threshold while still held (and the following release is swallowed).
namespace buttons {

struct Event {
  uint8_t index;   // 0 = A (dismiss), 1 = B (next/accept), 2 = C (aux/sleep)
  bool longPress;
};

void begin();
// Call often (every few ms). Returns queued events one at a time.
bool poll(Event& out);

}  // namespace buttons
