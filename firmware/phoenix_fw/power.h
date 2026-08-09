#pragma once

#include <Arduino.h>

// Idle tracking and deep sleep. See power.cpp for the power budget
// reasoning; sloppy sleep handling here is the difference between days and
// hours of standby.
namespace power {

void noteActivity();          // any user or radio activity
uint32_t msSinceActivity();

// SYSTEM OFF: ~5 µA, wake only by the configured button (or reset). Never
// returns.
[[noreturn]] void deepSleep();

}  // namespace power
