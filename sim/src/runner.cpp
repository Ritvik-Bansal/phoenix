#include "runner.h"

namespace sim {
namespace {

using phoenix::Device;
using phoenix::FrameBuffer;
namespace proto = phoenix::proto;

constexpr size_t kSimulatedMtuChunk = 20;  // classic 23-byte MTU minus ATT header

std::string shortText(const std::string& s, size_t max = 26) {
  if (s.size() <= max) return s;
  return s.substr(0, max - 3) + "...";
}

const char* maneuverName(proto::Maneuver m) {
  switch (m) {
    case proto::Maneuver::Straight: return "straight";
    case proto::Maneuver::Left: return "left";
    case proto::Maneuver::Right: return "right";
    case proto::Maneuver::SlightLeft: return "slight-left";
    case proto::Maneuver::SlightRight: return "slight-right";
    case proto::Maneuver::SharpLeft: return "sharp-left";
    case proto::Maneuver::SharpRight: return "sharp-right";
    case proto::Maneuver::UTurn: return "u-turn";
    case proto::Maneuver::Arrive: return "arrive";
  }
  return "?";
}

class Runner {
 public:
  explicit Runner(const Scenario& sc) : sc_(sc) {}

  ScenarioResult run() {
    ScenarioResult result;
    result.fileStem = sc_.fileStem;
    result.name = sc_.name;
    result.desc = sc_.desc;

    capture("boot", /*forceKey=*/true);  // splash, tick 0
    for (const Event& e : sc_.events) {
      if (e.type == Event::Type::Tick) {
        for (uint32_t i = 0; i < e.number; ++i) {
          dev_.tick();
          ++tick_;
          capture("", /*forceKey=*/false);
        }
      } else {
        capture(apply(e), /*forceKey=*/true);
      }
    }
    result.totalTicks = tick_ + 1;
    result.frames = std::move(frames_);
    return result;
  }

 private:
  // Applies one event and returns its display label.
  std::string apply(const Event& e) {
    std::string label;
    switch (e.type) {
      case Event::Type::Tick:
        break;
      case Event::Type::Connect:
        dev_.setConnected(true);
        label = "connect";
        break;
      case Event::Type::Disconnect:
        dev_.setConnected(false);
        label = "disconnect";
        break;
      case Event::Type::Bond:
        dev_.setBonded(true);
        label = "bonded";
        break;
      case Event::Type::Time:
        dev_.setTime(e.time);
        label = "CTS time " + phoenix::formatClock(e.time);
        break;
      case Event::Type::Battery:
        dev_.setBatteryMillivolts(static_cast<int>(e.number));
        label = "battery " + std::to_string(e.number) + "mV";
        break;
      case Event::Type::AncsAdd:
        dev_.onAncsEvent({phoenix::ancs::EventId::Added, e.notif});
        label = std::string("ANCS add ") +
                phoenix::ancs::categoryName(e.notif.category) + " \"" +
                shortText(!e.notif.title.empty() ? e.notif.title
                                                 : e.notif.message) +
                "\"";
        break;
      case Event::Type::AncsModify:
        dev_.onAncsEvent({phoenix::ancs::EventId::Modified, e.notif});
        label = "ANCS modify uid " + std::to_string(e.notif.uid);
        break;
      case Event::Type::AncsRemove: {
        phoenix::ancs::AncsNotification n;
        n.uid = e.number;
        dev_.onAncsEvent({phoenix::ancs::EventId::Removed, n});
        label = "ANCS remove uid " + std::to_string(e.number);
        break;
      }
      case Event::Type::AssistantText:
        feed(proto::makeAssistantText(seq_++, e.text));
        label = "TX ASSISTANT_TEXT \"" + shortText(e.text) + "\"";
        break;
      case Event::Type::AssistantChunk:
        feed(proto::makeAssistantChunk(seq_++, e.text, e.flag));
        label = std::string(e.flag ? "TX CHUNK(final) \"" : "TX CHUNK \"") +
                shortText(e.text) + "\"";
        break;
      case Event::Type::Nav:
        feed(proto::makeNavUpdate(seq_++, e.maneuver, e.meters, e.text));
        label = std::string("TX NAV_UPDATE ") + maneuverName(e.maneuver);
        if (e.meters != proto::kDistanceUnknown) {
          label += " " + std::to_string(e.meters) + "m";
        }
        if (!e.text.empty()) label += " \"" + shortText(e.text, 18) + "\"";
        break;
      case Event::Type::Clear:
        feed(proto::makeClear(seq_++, e.byte));
        label = "TX CLEAR";
        break;
      case Event::Type::Brightness:
        feed(proto::makeSetBrightness(seq_++, e.byte));
        label = "TX SET_BRIGHTNESS " + std::to_string(e.byte);
        break;
      case Event::Type::Ping:
        feed(proto::makePing(seq_++, e.byte));
        label = "TX PING v" + std::to_string(e.byte);
        break;
      case Event::Type::ButtonPress:
        dev_.pressButton(static_cast<phoenix::Button>(e.byte), e.flag);
        label = std::string("button ") +
                static_cast<char>('A' + e.byte - 1) + (e.flag ? " long" : "");
        break;
    }
    label += drainDeviceOutputs();
    return label;
  }

  // Encoded frame -> MTU-sized chunks, exactly like the radio would carry it.
  void feed(const proto::Frame& f) {
    const auto bytes = proto::encodeFrame(f);
    size_t i = 0;
    while (i < bytes.size()) {
      const size_t n =
          bytes.size() - i < kSimulatedMtuChunk ? bytes.size() - i : kSimulatedMtuChunk;
      dev_.feedBle(bytes.data() + i, n);
      i += n;
    }
  }

  // Summarizes glasses->phone traffic and ANCS action requests for labels.
  std::string drainDeviceOutputs() {
    std::string note;
    proto::StreamDecoder dec;
    dec.feed(dev_.takeOutbox());
    proto::Frame f;
    while (dec.poll(f)) {
      if (const auto ack = proto::parseAck(f)) {
        note += " -> RX ACK(seq " + std::to_string(ack->ackedSeq) +
                (ack->status == proto::kAckOk ? ", ok)" : ", err)");
      } else if (const auto b = proto::parseButtonEvent(f)) {
        note += std::string(" -> RX BUTTON_EVENT ") +
                static_cast<char>('A' + b->button - 1) +
                (b->action == proto::kPressLong ? " long" : "");
      } else if (const auto batt = proto::parseBatteryStatus(f)) {
        note += " -> RX BATTERY " + std::to_string(batt->percent) + "%";
      }
    }
    for (const auto& a : dev_.takeAncsActions()) {
      note += std::string(" -> ANCS action ") +
              (a.positive ? "accept" : "decline") + " uid " +
              std::to_string(a.uid);
    }
    if (dev_.takeSleepRequest()) note += " -> sleep";
    return note;
  }

  void capture(const std::string& label, bool forceKey) {
    FrameBuffer fb;
    dev_.render(fb);
    const bool changed = !haveLast_ || fb != last_;
    if (!changed && !forceKey) return;
    if (!changed && forceKey && !frames_.empty() &&
        frames_.back().tick == tick_) {
      // Same tick, same pixels: merge labels instead of duplicating.
      CapturedFrame& prev = frames_.back();
      if (!label.empty()) {
        prev.label += prev.label.empty() ? label : " | " + label;
        prev.key = true;
      }
      prev.brightness = dev_.brightness();
      return;
    }
    CapturedFrame cf;
    cf.tick = tick_;
    cf.ascii = fb.toAscii();
    cf.brightness = dev_.brightness();
    cf.label = label;
    cf.key = forceKey;
    // An event at the same tick as the previous animation frame with equal
    // pixels was handled above; equal-tick different-pixel captures stand.
    frames_.push_back(std::move(cf));
    last_ = fb;
    haveLast_ = true;
  }

  const Scenario& sc_;
  Device dev_;
  FrameBuffer last_;
  bool haveLast_ = false;
  uint32_t tick_ = 0;
  uint8_t seq_ = 0;
  std::vector<CapturedFrame> frames_;
};

}  // namespace

ScenarioResult runScenario(const Scenario& scenario) {
  return Runner(scenario).run();
}

}  // namespace sim
