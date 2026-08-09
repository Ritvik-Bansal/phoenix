#include "power.h"

#include <bluefruit.h>
#include <nrf_soc.h>

#include "display.h"
#include "fw_config.h"

// Power reasoning
// ---------------
// The nRF52840 has three meaningfully different consumption regimes:
//
//   1. Active CPU + radio bursts: milliamps. Unavoidable while rendering
//      and talking BLE, so the loop only wakes at the 10 Hz tick (FreeRTOS
//      tickless idle sleeps the CPU between wakes via __WFE under the hood
//      of delay()).
//
//   2. SYSTEM ON idle with the SoftDevice keeping a bonded connection at a
//      slow interval and the OLED dark: tens of µA. This is the "in your
//      pocket but reachable" state — the phone can still push a
//      notification, which is the whole point of the product, so we stay
//      here as long as a phone is attached. The OLED is the hungriest part
//      (~10-20 mA lit), hence the aggressive display-off timer in the main
//      loop.
//
//   3. SYSTEM OFF: ~5 µA with RAM retention off and only GPIO sense armed.
//      The radio is dead — no ANCS, no reconnect — so this is reserved for
//      "nobody is using me": disconnected and untouched for
//      kDeepSleepDisconnectedMs, or an explicit long-press. Waking is a
//      full reboot (bond state persists in InternalFS, so the phone
//      re-attaches without re-pairing).
//
// At ~5 µA a 100 mAh pack idles for years; at SYSTEM ON with a live
// connection it's weeks; with the OLED lit it's hours. That ordering is why
// the display sleeps first, the link second, the chip last.

namespace power {
namespace {

volatile uint32_t lastActivityMs = 0;

}  // namespace

void noteActivity() { lastActivityMs = millis(); }

uint32_t msSinceActivity() { return millis() - lastActivityMs; }

void deepSleep() {
  display::setPowerSave(true);
  Bluefruit.Advertising.stop();

  // Arm button A as the wake source: pull-up + sense-low. In SYSTEM OFF the
  // GPIO SENSE machinery keeps running at nanoamp cost and a press resets
  // the chip into a normal boot.
  pinMode(kPinButtonA, INPUT_PULLUP_SENSE);

  // With the SoftDevice enabled, POWER register writes must go through it.
  const uint32_t err = sd_power_system_off();
  (void)err;
  // Only reached if the SoftDevice was not running: use the register.
  NRF_POWER->SYSTEMOFF = 1;
  for (;;) {
    __WFE();
  }
}

}  // namespace power
