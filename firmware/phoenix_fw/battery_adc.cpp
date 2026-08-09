#include "battery_adc.h"

#include <Arduino.h>

namespace battery_adc {
namespace {

// XIAO nRF52840: VBAT feeds a 1 MΩ / 510 kΩ divider gated by VBAT_ENABLE
// (drive LOW to connect). The divider presents VBAT * 510/1510 ≈ 0.338 * VBAT,
// well inside the 2.4 V internal reference range for a 4.2 V pack.
constexpr int kDividerNum = 1510;
constexpr int kDividerDen = 510;
constexpr int kAdcRefMv = 2400;
constexpr int kAdcMax = 4095;  // 12-bit

}  // namespace

void begin() {
  pinMode(VBAT_ENABLE, OUTPUT);
  digitalWrite(VBAT_ENABLE, LOW);        // enable the divider
  analogReference(AR_INTERNAL_2_4);      // 0.6 V ref, 1/4 gain
  analogReadResolution(12);
}

int readMillivolts() {
  // Average a few samples; the SAADC is noisy around the divider's high
  // output impedance.
  uint32_t acc = 0;
  constexpr int kSamples = 8;
  for (int i = 0; i < kSamples; ++i) {
    acc += analogRead(PIN_VBAT);
  }
  const uint32_t raw = acc / kSamples;
  const uint32_t pinMv = raw * kAdcRefMv / kAdcMax;
  return static_cast<int>(pinMv * kDividerNum / kDividerDen);
}

}  // namespace battery_adc
