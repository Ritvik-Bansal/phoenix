#pragma once

// Battery voltage via the XIAO nRF52840's on-board divider. The LiPo
// voltage-to-percent conversion lives in the portable core
// (phoenix/battery.h) where it is unit-tested; this file only produces
// honest millivolts.
namespace battery_adc {

void begin();
int readMillivolts();

}  // namespace battery_adc
