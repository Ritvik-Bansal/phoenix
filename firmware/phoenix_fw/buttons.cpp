#include "buttons.h"

#include "fw_config.h"

namespace buttons {
namespace {

const uint32_t kPins[3] = {kPinButtonA, kPinButtonB, kPinButtonC};

struct ButtonState {
  bool stable = false;       // debounced pressed state (true = down)
  bool lastRaw = false;
  uint32_t lastEdgeMs = 0;
  uint32_t downSinceMs = 0;
  bool longFired = false;
};

ButtonState state[3];

// Tiny event ring; overflow drops the oldest (button mashing is not data).
Event ring[8];
uint8_t head = 0, count = 0;

void push(uint8_t index, bool longPress) {
  if (count == 8) {
    head = (head + 1) % 8;
    --count;
  }
  ring[(head + count) % 8] = {index, longPress};
  ++count;
}

}  // namespace

void begin() {
  for (uint32_t pin : kPins) {
    pinMode(pin, INPUT_PULLUP);
  }
}

bool poll(Event& out) {
  const uint32_t now = millis();
  for (int i = 0; i < 3; ++i) {
    ButtonState& b = state[i];
    const bool raw = digitalRead(kPins[i]) == LOW;
    if (raw != b.lastRaw) {
      b.lastRaw = raw;
      b.lastEdgeMs = now;
    }
    if (raw != b.stable && now - b.lastEdgeMs >= kDebounceMs) {
      b.stable = raw;
      if (raw) {
        b.downSinceMs = now;
        b.longFired = false;
      } else if (!b.longFired) {
        push(static_cast<uint8_t>(i), false);  // short press on release
      }
    }
    if (b.stable && !b.longFired && now - b.downSinceMs >= kLongPressMs) {
      b.longFired = true;
      push(static_cast<uint8_t>(i), true);
    }
  }
  if (count == 0) return false;
  out = ring[head];
  head = (head + 1) % 8;
  --count;
  return true;
}

}  // namespace buttons
