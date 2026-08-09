// Project Phoenix firmware — Seeed XIAO nRF52840 + 0.42" SSD1306 72x40.
//
// The interesting logic (rendering, layout, screens, ANCS model, protocol)
// lives in the portable core (firmware/lib/PhoenixCore, synced from core/ by
// build.sh) and is unit-tested on the desktop. This sketch is deliberately
// thin glue: BLE dual role in ble_glue, panel push in display, GPIO in
// buttons, power policy in power.
//
// Include order matters: phoenix headers (which pull in the C++ standard
// library) must come before Arduino/bluefruit headers because the Arduino
// core defines min/max macros.

#include <PhoenixCore.h>

#include <bluefruit.h>

#include "battery_adc.h"
#include "ble_glue.h"
#include "buttons.h"
#include "display.h"
#include "fw_config.h"
#include "power.h"

phoenix::Device device;
SemaphoreHandle_t devMutex;

phoenix::FrameBuffer frame;
phoenix::FrameBuffer lastPushed;
bool everPushed = false;
uint8_t lastBrightness = 255;

uint32_t nextTickMs = 0;
uint32_t nextBatteryReadMs = 0;

void setup() {
  Serial.begin(115200);  // no wait-for-host: the glasses boot standalone

  devMutex = xSemaphoreCreateMutex();

  buttons::begin();
  battery_adc::begin();
  display::begin();
  ble_glue::begin(&device, devMutex);

  {
    xSemaphoreTake(devMutex, portMAX_DELAY);
    device.setBatteryMillivolts(battery_adc::readMillivolts());
    xSemaphoreGive(devMutex);
  }

  power::noteActivity();
  nextTickMs = millis();
  nextBatteryReadMs = millis() + 5000;

  Serial.println("Phoenix HUD up");
}

void loop() {
  const uint32_t now = millis();

  // Buttons: fast path, independent of the render tick.
  buttons::Event ev;
  while (buttons::poll(ev)) {
    xSemaphoreTake(devMutex, portMAX_DELAY);
    device.pressButton(static_cast<phoenix::Button>(ev.index + 1),
                       ev.longPress);
    xSemaphoreGive(devMutex);
    power::noteActivity();
  }

  // Battery sampling every 30 s; reporting policy lives in the core.
  if (static_cast<int32_t>(now - nextBatteryReadMs) >= 0) {
    nextBatteryReadMs = now + 30000;
    const int mv = battery_adc::readMillivolts();
    xSemaphoreTake(devMutex, portMAX_DELAY);
    device.setBatteryMillivolts(mv);
    xSemaphoreGive(devMutex);
  }

  // The 10 Hz core tick: advance state, render, push on change.
  if (static_cast<int32_t>(now - nextTickMs) >= 0) {
    nextTickMs += kTickMs;
    if (static_cast<int32_t>(now - nextTickMs) > 1000) {
      nextTickMs = now + kTickMs;  // resync after a long stall
    }

    bool hasActiveScreen;
    bool sleepRequested;
    uint8_t brightness;
    {
      xSemaphoreTake(devMutex, portMAX_DELAY);
      device.tick();
      device.render(frame);
      hasActiveScreen = device.screens().active() != nullptr;
      sleepRequested = device.takeSleepRequest();
      brightness = device.brightness();
      xSemaphoreGive(devMutex);
    }

    if (sleepRequested) {
      Serial.println("long-press: SYSTEM OFF");
      Serial.flush();
      power::deepSleep();  // never returns
    }

    // Display power: a live screen (or recent activity) keeps the panel on;
    // otherwise it is the single biggest battery drain and goes dark.
    const bool wantOn =
        hasActiveScreen || power::msSinceActivity() < kDisplayIdleOffMs;
    if (wantOn != display::isOn()) {
      display::setPowerSave(!wantOn);
      if (wantOn) everPushed = false;  // force a refresh on wake
    }

    if (display::isOn()) {
      if (brightness != lastBrightness) {
        display::setBrightness(brightness);
        lastBrightness = brightness;
      }
      if (!everPushed || frame != lastPushed) {
        display::push(frame);
        lastPushed = frame;
        everPushed = true;
      }
    }
  }

  // Flush device output over BLE (RX notifications, ANCS actions).
  ble_glue::service();

  // Nobody attached and nobody touching us: shut the chip down entirely.
  if (!ble_glue::connected() &&
      power::msSinceActivity() > kDeepSleepDisconnectedMs) {
    Serial.println("idle & disconnected: SYSTEM OFF");
    Serial.flush();
    power::deepSleep();
  }

  // FreeRTOS tickless idle turns this into real sleep, not a busy spin.
  delay(5);
}
