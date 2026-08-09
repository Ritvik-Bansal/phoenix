#pragma once

// BLE dual role on a single connection:
//   peripheral — advertises and serves the custom Phoenix service (TX/RX)
//   client     — consumes ANCS and CTS from the connected iPhone
// All events funnel into the shared phoenix::Device under one mutex; the
// main loop calls service() to flush device output back over the air.

#include <phoenix/device.h>

// The Adafruit core is FreeRTOS-based; callbacks run on the callback task
// while loop() runs on its own task, so Device access is serialized with a
// FreeRTOS mutex.
#include <Arduino.h>

namespace ble_glue {

void begin(phoenix::Device* device, SemaphoreHandle_t deviceMutex);

// Drain device outbox to RX notifications (MTU-sized chunks) and execute
// pending ANCS actions. Call every loop iteration.
void service();

bool connected();

}  // namespace ble_glue
