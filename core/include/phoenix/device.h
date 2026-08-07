#pragma once

// The whole glasses brain, hardware-free. Firmware and simulator both drive
// exactly this object:
//
//   inputs   feedBle()        bytes written to the Phoenix TX characteristic
//            onAncsEvent()    translated BLEAncs callbacks / scenario events
//            setTime()        CTS reads / scenario events
//            pressButton()    debounced GPIO / scenario events
//            setConnected(), setBonded(), setBatteryMillivolts()
//            tick()           10 Hz
//
//   outputs  render()             the frame to push to the panel
//            takeOutbox()         wire bytes to notify on the RX characteristic
//            takeAncsActions()    ANCS perform-action requests (call accept/…)
//            takeSleepRequest()   long-press sleep intent
//            brightness()         SSD1306 contrast to apply

#include <cstdint>
#include <string>
#include <vector>

#include "phoenix/ancs.h"
#include "phoenix/battery.h"
#include "phoenix/protocol.h"
#include "phoenix/screen_manager.h"
#include "phoenix/screens.h"
#include "phoenix/version.h"

namespace phoenix {

class Device {
 public:
  struct AncsActionRequest {
    uint32_t uid = 0;
    bool positive = false;
  };

  Device();

  // ---- inputs ----
  void tick();
  void feedBle(const uint8_t* data, size_t n);
  void onAncsEvent(const ancs::AncsEvent& e);
  void pressButton(Button b, bool longPress);
  void setConnected(bool connected);
  void setBonded(bool bonded);
  void setTime(const DateTime& t);
  void setBatteryMillivolts(int mv);

  // ---- outputs ----
  void render(FrameBuffer& fb);
  std::vector<uint8_t> takeOutbox();
  std::vector<AncsActionRequest> takeAncsActions();
  bool takeSleepRequest();
  uint8_t brightness() const { return state_.brightness; }

  // introspection (tests, simulator labels)
  const DeviceState& state() const { return state_; }
  ScreenManager& screens() { return mgr_; }
  ancs::AncsStore& ancsStore() { return store_; }
  uint32_t ticks() const { return tick_; }

 private:
  void dispatchFrame(const proto::Frame& f);
  void sendFrame(proto::Frame f);
  void sendAck(uint8_t ackedSeq, uint8_t status);
  void refreshAssistantScreen();
  void reportBattery();

  ScreenManager mgr_;
  ancs::AncsStore store_;
  proto::StreamDecoder decoder_;
  DeviceState state_;

  std::string assistantRaw_;  // accumulating UTF-8 as it arrives off the radio
  bool assistantStreaming_ = false;

  uint32_t tick_ = 0;
  uint32_t timeBaseTick_ = 0;
  uint8_t seqOut_ = 0;
  int lastReportedPercent_ = -1;
  uint32_t lastBatteryReportTick_ = 0;
  bool sleepRequested_ = false;

  std::vector<uint8_t> outbox_;
  std::vector<AncsActionRequest> ancsActions_;
};

}  // namespace phoenix
