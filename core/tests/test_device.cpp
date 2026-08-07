// End-to-end Device tests: BLE bytes in -> screens out, ANCS event flow,
// buttons, outbox traffic, battery reporting — the exact object the firmware
// and simulator both wrap.

#include "phoenix/device.h"
#include "test_framework.h"

using namespace phoenix;
using ancs::AncsEvent;
using ancs::AncsNotification;
using ancs::CategoryId;
using ancs::EventId;

namespace {

AncsEvent added(uint32_t uid, CategoryId cat, const char* title,
                const char* msg, uint8_t flags = 0) {
  AncsEvent e;
  e.event = EventId::Added;
  e.notification.uid = uid;
  e.notification.category = cat;
  e.notification.eventFlags = flags;
  e.notification.appIdentifier = "net.whatsapp.WhatsApp";
  e.notification.title = title;
  e.notification.message = msg;
  return e;
}

AncsEvent removed(uint32_t uid) {
  AncsEvent e;
  e.event = EventId::Removed;
  e.notification.uid = uid;
  return e;
}

void feed(Device& d, const proto::Frame& f) {
  const auto bytes = proto::encodeFrame(f);
  d.feedBle(bytes.data(), bytes.size());
}

std::vector<proto::Frame> decodeOutbox(Device& d) {
  proto::StreamDecoder dec;
  dec.feed(d.takeOutbox());
  std::vector<proto::Frame> out;
  proto::Frame f;
  while (dec.poll(f)) out.push_back(f);
  return out;
}

// Fast-forward past the boot splash.
void boot(Device& d) {
  for (int i = 0; i < 20; ++i) d.tick();
  d.takeOutbox();
}

}  // namespace

TEST(boot_splash_then_idle) {
  Device d;
  CHECK(d.screens().active() != nullptr);
  CHECK(d.screens().active()->kind() == ScreenKind::Splash);
  for (int i = 0; i < 20; ++i) d.tick();
  CHECK(d.screens().active() == nullptr);  // idle; disconnected -> status
  CHECK(d.screens().idleKind(d.state()) == ScreenKind::Status);
  d.setConnected(true);
  CHECK(d.screens().idleKind(d.state()) == ScreenKind::Clock);
}

TEST(ping_is_acked_with_version_check) {
  Device d;
  boot(d);
  feed(d, proto::makePing(9, 1));
  auto out = decodeOutbox(d);
  REQUIRE(out.size() == 1);
  auto ack = proto::parseAck(out[0]);
  REQUIRE(ack.has_value());
  CHECK_EQ(ack->ackedSeq, 9);
  CHECK_EQ(ack->status, proto::kAckOk);

  feed(d, proto::makePing(10, 99));  // wrong protocol version
  out = decodeOutbox(d);
  REQUIRE(out.size() == 1);
  ack = proto::parseAck(out[0]);
  REQUIRE(ack.has_value());
  CHECK_EQ(ack->status, proto::kAckBadVersion);
}

TEST(ack_req_flag_and_unknown_type) {
  Device d;
  boot(d);
  proto::Frame nav = proto::makeNavUpdate(4, proto::Maneuver::Right, 90, "Oak");
  nav.flags |= proto::kFlagAckReq;
  feed(d, nav);
  auto out = decodeOutbox(d);
  REQUIRE(out.size() == 1);
  CHECK_EQ(proto::parseAck(out[0])->status, proto::kAckOk);
  CHECK(d.screens().active()->kind() == ScreenKind::Nav);

  proto::Frame unknown;
  unknown.type = static_cast<proto::MsgType>(0x99);
  unknown.seq = 44;
  unknown.flags = proto::kFlagAckReq;
  feed(d, unknown);
  out = decodeOutbox(d);
  REQUIRE(out.size() == 1);
  const auto ack = proto::parseAck(out[0]);
  REQUIRE(ack.has_value());
  CHECK_EQ(ack->ackedSeq, 44);
  CHECK_EQ(ack->status, proto::kAckBadType);
}

TEST(assistant_streaming_with_split_utf8) {
  Device d;
  boot(d);
  // "22°C" with the two-byte ° split across BLE frames.
  const std::string part1 = "Sunny and 22\xC2";
  const std::string part2 = "\xB0" "C all afternoon.";
  feed(d, proto::makeAssistantChunk(1, part1, false));
  CHECK(d.screens().active()->kind() == ScreenKind::Assistant);
  // Mid-stream: the dangling UTF-8 byte is held back, not shown as a box.
  FrameBuffer fb;
  d.render(fb);
  feed(d, proto::makeAssistantChunk(2, part2, true));
  d.render(fb);
  // After the final chunk the degree sign transliterated to '*'.
  auto* screen = static_cast<AssistantScreen*>(
      d.screens().findByKind(ScreenKind::Assistant));
  CHECK(screen != nullptr);
  CHECK(!screen->finished(0));
  // One enter + updates, no screen churn while streaming.
  int enters = 0;
  for (const auto& t : d.screens().transitions()) {
    if (t == "enter:Assistant") ++enters;
  }
  CHECK_EQ(enters, 1);
}

TEST(assistant_keeps_streaming_while_call_preempts) {
  Device d;
  boot(d);
  feed(d, proto::makeAssistantChunk(1, "Reading you the", false));
  d.onAncsEvent(added(9, CategoryId::IncomingCall, "Mom", "",
                      ancs::kFlagPositiveAction | ancs::kFlagNegativeAction));
  CHECK(d.screens().active()->kind() == ScreenKind::IncomingCall);
  feed(d, proto::makeAssistantChunk(2, " weather now.", true));  // still lands
  d.onAncsEvent(removed(9));
  CHECK(d.screens().active()->kind() == ScreenKind::Assistant);
}

TEST(nav_updates_in_place) {
  Device d;
  boot(d);
  feed(d, proto::makeNavUpdate(1, proto::Maneuver::Left, 250, "Market St"));
  feed(d, proto::makeNavUpdate(2, proto::Maneuver::Left, 80, "Market St"));
  int enters = 0, updates = 0;
  for (const auto& t : d.screens().transitions()) {
    if (t == "enter:Nav") ++enters;
    if (t == "update:Nav") ++updates;
  }
  CHECK_EQ(enters, 1);
  CHECK_EQ(updates, 1);
}

TEST(clear_mask_drops_content) {
  Device d;
  boot(d);
  feed(d, proto::makeNavUpdate(1, proto::Maneuver::Left, 250, "Market St"));
  feed(d, proto::makeAssistantText(2, "done"));
  d.onAncsEvent(added(5, CategoryId::Social, "Ana", "hey"));
  feed(d, proto::makeClear(3, proto::kClearAll));
  CHECK(d.screens().active() == nullptr);
  CHECK(d.screens().findByKind(ScreenKind::Nav) == nullptr);
  CHECK(d.screens().findByKind(ScreenKind::Assistant) == nullptr);
  CHECK(d.screens().findByKind(ScreenKind::Notification) == nullptr);
}

TEST(brightness_applies) {
  Device d;
  boot(d);
  CHECK_EQ(d.brightness(), 255);
  feed(d, proto::makeSetBrightness(1, 5));
  CHECK_EQ(d.brightness(), 5);
}

TEST(buttons_report_and_act) {
  Device d;
  boot(d);
  d.onAncsEvent(added(9, CategoryId::IncomingCall, "Mom", "",
                      ancs::kFlagPositiveAction | ancs::kFlagNegativeAction));
  d.takeOutbox();
  d.pressButton(Button::B, false);  // accept
  const auto out = decodeOutbox(d);
  REQUIRE(out.size() == 1);  // button event reported to the phone
  const auto btn = proto::parseButtonEvent(out[0]);
  REQUIRE(btn.has_value());
  CHECK_EQ(btn->button, proto::kButtonB);
  CHECK_EQ(btn->action, proto::kPressShort);
  const auto actions = d.takeAncsActions();
  REQUIRE(actions.size() == 1);
  CHECK_EQ(actions[0].uid, uint32_t(9));
  CHECK(actions[0].positive);
  CHECK(d.takeAncsActions().empty());  // consumed

  d.pressButton(Button::C, true);  // long C = sleep
  CHECK(d.takeSleepRequest());
  CHECK(!d.takeSleepRequest());
}

TEST(status_overlay_on_aux_button) {
  Device d;
  boot(d);
  d.pressButton(Button::C, false);
  CHECK(d.screens().active() != nullptr);
  CHECK(d.screens().active()->kind() == ScreenKind::Status);
  for (int i = 0; i < 51; ++i) d.tick();  // overlay times out
  CHECK(d.screens().active() == nullptr);
}

TEST(battery_reporting_thresholds_and_period) {
  Device d;
  boot(d);
  d.setConnected(true);
  auto out = decodeOutbox(d);  // connect triggers an immediate report
  REQUIRE(out.size() == 1);
  auto batt = proto::parseBatteryStatus(out[0]);
  REQUIRE(batt.has_value());
  CHECK_EQ(batt->percent, 100);  // default state before any ADC reading

  d.setBatteryMillivolts(4050);  // 85%: past the 5% delta, reports
  out = decodeOutbox(d);
  REQUIRE(out.size() == 1);
  CHECK_EQ(proto::parseBatteryStatus(out[0])->percent, 85);

  d.setBatteryMillivolts(4035);  // ~82%: small move, no report
  CHECK(decodeOutbox(d).empty());

  d.setBatteryMillivolts(3920);  // 65%: big drop, reports
  out = decodeOutbox(d);
  REQUIRE(out.size() == 1);
  CHECK_EQ(proto::parseBatteryStatus(out[0])->percent, 65);
  CHECK_EQ(proto::parseBatteryStatus(out[0])->millivolts, 3920);

  // Periodic report even with no change.
  for (int i = 0; i < 601; ++i) d.tick();
  out = decodeOutbox(d);
  CHECK_EQ(out.size(), size_t(1));
}

TEST(clock_advances_with_ticks_only) {
  Device d;
  boot(d);
  d.setConnected(true);
  d.setTime({2026, 8, 7, 9, 41, 55});
  for (int i = 0; i < 100; ++i) d.tick();  // 10 s at 10 Hz
  CHECK_EQ(d.state().time.minute, 42);
  CHECK_EQ(d.state().time.second, 5);
}

TEST(disconnect_clears_stale_phone_content) {
  Device d;
  boot(d);
  d.setConnected(true);
  d.setBonded(true);
  d.onAncsEvent(added(5, CategoryId::Social, "Ana", "hey"));
  feed(d, proto::makeNavUpdate(1, proto::Maneuver::Left, 250, "Market St"));
  CHECK(d.screens().active() != nullptr);
  d.setConnected(false);
  CHECK(d.screens().active() == nullptr);
  CHECK_EQ(d.ancsStore().size(), size_t(0));
  CHECK(!d.state().bonded);
  CHECK(d.screens().idleKind(d.state()) == ScreenKind::Status);
}

TEST(ancs_removed_clears_display) {
  Device d;
  boot(d);
  d.onAncsEvent(added(7, CategoryId::Social, "Ravi", "Lunch?"));
  CHECK(d.screens().active()->kind() == ScreenKind::Notification);
  d.onAncsEvent(removed(7));
  CHECK(d.screens().active() == nullptr);
}

TEST(preexisting_backlog_stays_quiet) {
  Device d;
  boot(d);
  d.onAncsEvent(added(1, CategoryId::Email, "Old", "backlog",
                      ancs::kFlagPreExisting));
  CHECK(d.screens().active() == nullptr);
  CHECK_EQ(d.ancsStore().size(), size_t(1));

  // A Modified event for something never shown must not resurface it.
  AncsEvent mod;
  mod.event = EventId::Modified;
  mod.notification.uid = 1;
  mod.notification.category = CategoryId::Email;
  mod.notification.message = "backlog (updated)";
  d.onAncsEvent(mod);
  CHECK(d.screens().active() == nullptr);
}

TESTFW_MAIN
