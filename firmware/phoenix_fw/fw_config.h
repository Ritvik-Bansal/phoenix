#pragma once

// Hardware map and timing constants for the Phoenix HUD on the Seeed XIAO
// nRF52840. The 0.42" SSD1306 hangs off the I2C pins (D4=SDA, D5=SCL on the
// XIAO edge); three tactile buttons go to D1/D2/D3 against GND.

#include <Arduino.h>

// Buttons (active low, internal pull-ups).
constexpr uint32_t kPinButtonA = 1;  // dismiss / decline
constexpr uint32_t kPinButtonB = 2;  // next page / accept
constexpr uint32_t kPinButtonC = 3;  // status overlay; long-press = sleep

constexpr uint32_t kDebounceMs = 30;
constexpr uint32_t kLongPressMs = 700;

// Core tick cadence — must match the core's assumptions (10 Hz).
constexpr uint32_t kTickMs = 100;

// Power policy, see power.cpp for the reasoning.
constexpr uint32_t kDisplayIdleOffMs = 30000;      // panel off after 30 s idle
constexpr uint32_t kDeepSleepDisconnectedMs = 300000;  // SYSTEM OFF after 5 min alone

// BLE identity.
constexpr const char* kDeviceName = "Phoenix HUD";
