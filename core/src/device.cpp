#include "phoenix/device.h"

#include "phoenix/text_sanitize.h"

namespace phoenix {

namespace {
constexpr uint32_t kTicksPerSecond = 10;
constexpr uint32_t kBatteryReportPeriodTicks = 600;  // 60 s
constexpr int kBatteryReportDeltaPct = 5;
}  // namespace

Device::Device() {
  mgr_.show(std::make_unique<SplashScreen>());
}

void Device::tick() {
  ++tick_;
  if (state_.haveTime && (tick_ - timeBaseTick_) % kTicksPerSecond == 0) {
    addSeconds(state_.time, 1);
  }
  mgr_.tick();
  if (state_.connected &&
      tick_ - lastBatteryReportTick_ >= kBatteryReportPeriodTicks) {
    reportBattery();
  }
}

void Device::feedBle(const uint8_t* data, size_t n) {
  decoder_.feed(data, n);
  proto::Frame f;
  while (decoder_.poll(f)) dispatchFrame(f);
}

void Device::dispatchFrame(const proto::Frame& f) {
  using proto::MsgType;
  bool known = true;
  switch (f.type) {
    case MsgType::AssistantText: {
      assistantRaw_.assign(f.payload.begin(), f.payload.end());
      assistantStreaming_ = false;
      if (assistantRaw_.empty()) {
        mgr_.clearKind(ScreenKind::Assistant);
      } else {
        refreshAssistantScreen();
      }
      break;
    }
    case MsgType::AssistantStreamChunk: {
      if (!assistantStreaming_) {
        assistantRaw_.clear();
        assistantStreaming_ = true;
      }
      assistantRaw_.append(f.payload.begin(), f.payload.end());
      if (f.flags & proto::kFlagStreamFinal) assistantStreaming_ = false;
      refreshAssistantScreen();
      break;
    }
    case MsgType::NavUpdate: {
      const auto nav = proto::parseNavUpdate(f);
      if (nav) mgr_.show(std::make_unique<NavScreen>(*nav));
      break;
    }
    case MsgType::Clear: {
      const auto mask = proto::parseSingleByte(f);
      if (!mask) break;
      if (*mask & proto::kClearAssistant) {
        assistantRaw_.clear();
        assistantStreaming_ = false;
        mgr_.clearKind(ScreenKind::Assistant);
      }
      if (*mask & proto::kClearNav) mgr_.clearKind(ScreenKind::Nav);
      if (*mask & proto::kClearNotification) {
        mgr_.clearKind(ScreenKind::Notification);
      }
      break;
    }
    case MsgType::SetBrightness: {
      const auto level = proto::parseSingleByte(f);
      if (level) state_.brightness = *level;
      break;
    }
    case MsgType::Ping: {
      const auto version = proto::parseSingleByte(f);
      const bool ok = version && *version == kProtocolVersion;
      sendAck(f.seq, ok ? proto::kAckOk : proto::kAckBadVersion);
      return;  // ping is always answered; no second ack below
    }
    case MsgType::Ack:
      break;  // informational
    case MsgType::ButtonEvent:
    case MsgType::BatteryStatus:
      // Glasses-to-phone types arriving here are protocol misuse; ignore.
      break;
    default:
      known = false;
      break;
  }
  if (f.flags & proto::kFlagAckReq) {
    sendAck(f.seq, known ? proto::kAckOk : proto::kAckBadType);
  }
}

void Device::refreshAssistantScreen() {
  // Re-sanitize the whole buffer each update: idempotent, and a UTF-8
  // sequence split across BLE chunks stays invisible until complete.
  const std::string display =
      sanitizeForDisplay(assistantRaw_, /*keepIncompleteTail=*/assistantStreaming_);
  Screen* existing = mgr_.findByKind(ScreenKind::Assistant);
  if (existing) {
    const uint32_t local = (existing == mgr_.active()) ? mgr_.activeLocalTick() : 0;
    static_cast<AssistantScreen*>(existing)->setContent(
        display, assistantStreaming_, local);
  } else {
    mgr_.show(std::make_unique<AssistantScreen>(display, assistantStreaming_));
  }
}

void Device::onAncsEvent(const ancs::AncsEvent& e) {
  const auto effect = store_.apply(e);
  const uint32_t uid = e.notification.uid;
  switch (effect) {
    case ancs::AncsStore::Effect::None:
      break;
    case ancs::AncsStore::Effect::Show:
    case ancs::AncsStore::Effect::Update: {
      const ancs::AncsNotification* n = store_.find(uid);
      if (!n) break;
      if (effect == ancs::AncsStore::Effect::Update && !mgr_.findByUid(uid)) {
        // Modified something not on display (pre-existing backlog, or already
        // dismissed by the wearer): stay quiet.
        break;
      }
      if (n->category == ancs::CategoryId::IncomingCall) {
        mgr_.show(std::make_unique<IncomingCallScreen>(*n));
      } else {
        mgr_.show(std::make_unique<NotificationScreen>(*n));
      }
      break;
    }
    case ancs::AncsStore::Effect::Remove:
      mgr_.removeByUid(uid);
      break;
  }
}

void Device::pressButton(Button b, bool longPress) {
  sendFrame(proto::makeButtonEvent(
      0, static_cast<uint8_t>(b),
      longPress ? proto::kPressLong : proto::kPressShort));
  if (b == Button::C && longPress) {
    sleepRequested_ = true;
    return;
  }
  if (longPress) return;  // long A/B currently unassigned
  if (b == Button::C) {
    if (!mgr_.active()) {
      mgr_.show(std::make_unique<StatusScreen>(/*overlay=*/true));
    }
    return;
  }
  const ScreenAction a = mgr_.handleButton(b);
  if (a.type == ScreenAction::Type::AncsPositive) {
    ancsActions_.push_back({a.uid, true});
  } else if (a.type == ScreenAction::Type::AncsNegative) {
    ancsActions_.push_back({a.uid, false});
  }
}

void Device::setConnected(bool connected) {
  if (state_.connected == connected) return;
  state_.connected = connected;
  if (!connected) {
    // Everything phone-fed is stale the moment the link drops. iOS replays
    // pre-existing notifications after the next connect, so nothing is lost.
    store_.clear();
    mgr_.clearKind(ScreenKind::IncomingCall);
    mgr_.clearKind(ScreenKind::Notification);
    mgr_.clearKind(ScreenKind::Nav);
    mgr_.clearKind(ScreenKind::Assistant);
    assistantRaw_.clear();
    assistantStreaming_ = false;
    state_.bonded = false;
  } else {
    reportBattery();
  }
}

void Device::setBonded(bool bonded) { state_.bonded = bonded; }

void Device::setTime(const DateTime& t) {
  state_.time = t;
  state_.haveTime = true;
  timeBaseTick_ = tick_;
}

void Device::setBatteryMillivolts(int mv) {
  state_.batteryMillivolts = mv;
  state_.batteryPercent = lipoPercentFromMillivolts(mv);
  if (state_.connected &&
      (lastReportedPercent_ < 0 ||
       state_.batteryPercent <= lastReportedPercent_ - kBatteryReportDeltaPct ||
       state_.batteryPercent >= lastReportedPercent_ + kBatteryReportDeltaPct)) {
    reportBattery();
  }
}

void Device::reportBattery() {
  sendFrame(proto::makeBatteryStatus(
      0, static_cast<uint8_t>(state_.batteryPercent),
      static_cast<uint16_t>(state_.batteryMillivolts)));
  lastReportedPercent_ = state_.batteryPercent;
  lastBatteryReportTick_ = tick_;
}

void Device::render(FrameBuffer& fb) { mgr_.render(fb, state_); }

void Device::sendFrame(proto::Frame f) {
  f.seq = seqOut_++;
  const auto bytes = proto::encodeFrame(f);
  outbox_.insert(outbox_.end(), bytes.begin(), bytes.end());
}

void Device::sendAck(uint8_t ackedSeq, uint8_t status) {
  sendFrame(proto::makeAck(0, ackedSeq, status));
}

std::vector<uint8_t> Device::takeOutbox() {
  std::vector<uint8_t> out;
  out.swap(outbox_);
  return out;
}

std::vector<Device::AncsActionRequest> Device::takeAncsActions() {
  std::vector<AncsActionRequest> out;
  out.swap(ancsActions_);
  return out;
}

bool Device::takeSleepRequest() {
  const bool r = sleepRequested_;
  sleepRequested_ = false;
  return r;
}

}  // namespace phoenix
