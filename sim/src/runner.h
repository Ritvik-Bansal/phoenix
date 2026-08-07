#pragma once

// Drives one phoenix::Device through a scenario, capturing every distinct
// display frame with its tick, triggering event, and brightness. Protocol
// events travel as real encoded frames fed in MTU-sized chunks — the sim
// exercises the exact byte path the radio would.

#include <cstdint>
#include <string>
#include <vector>

#include "phoenix/device.h"
#include "scenario.h"

namespace sim {

struct CapturedFrame {
  uint32_t tick = 0;
  std::string ascii;      // 40 lines of 72 chars (framebuffer dump)
  uint8_t brightness = 255;
  std::string label;      // triggering event ("" for animation frames)
  bool key = false;       // event-triggered: shown in the strip
};

struct ScenarioResult {
  std::string fileStem;
  std::string name;
  std::string desc;
  uint32_t totalTicks = 0;
  std::vector<CapturedFrame> frames;
};

ScenarioResult runScenario(const Scenario& scenario);

}  // namespace sim
