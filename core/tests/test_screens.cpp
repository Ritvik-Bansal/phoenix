// Screen manager state machine (priorities, preemption, FIFO, teardown
// discipline) and golden frames for every screen.

#include <memory>

#include "golden.h"
#include "phoenix/screen_manager.h"
#include "phoenix/screens.h"
#include "test_framework.h"

using namespace phoenix;
using ancs::AncsNotification;
using ancs::CategoryId;

namespace {

AncsNotification social(uint32_t uid, const char* title, const char* msg) {
  AncsNotification n;
  n.uid = uid;
  n.category = CategoryId::Social;
  n.appIdentifier = "net.whatsapp.WhatsApp";
  n.title = title;
  n.message = msg;
  return n;
}

AncsNotification call(uint32_t uid, const char* who) {
  AncsNotification n;
  n.uid = uid;
  n.category = CategoryId::IncomingCall;
  n.appIdentifier = "com.apple.mobilephone";
  n.title = who;
  n.eventFlags = ancs::kFlagPositiveAction | ancs::kFlagNegativeAction;
  return n;
}

proto::NavUpdateMsg navMsg(proto::Maneuver m, uint16_t meters,
                           const char* street) {
  proto::NavUpdateMsg n;
  n.maneuver = m;
  n.distanceMeters = meters;
  n.street = street;
  return n;
}

DeviceState fixtureState() {
  DeviceState s;
  s.connected = true;
  s.bonded = true;
  s.batteryPercent = 87;
  s.batteryMillivolts = 4050;
  s.time = {2026, 8, 7, 9, 41, 30};
  s.haveTime = true;
  return s;
}

// The formal state machine claim: enters and exits alternate perfectly —
// never two screens up, never an enter over a live screen.
void checkWellFormed(const ScreenManager& m) {
  int live = 0;
  for (const auto& t : m.transitions()) {
    if (t.rfind("enter:", 0) == 0) {
      CHECK_EQ(live, 0);
      live = 1;
    } else if (t.rfind("exit:", 0) == 0) {
      CHECK_EQ(live, 1);
      live = 0;
    }
  }
}

}  // namespace

TEST(priority_preemption_and_resume) {
  ScreenManager m;
  m.show(std::make_unique<NotificationScreen>(social(1, "Ravi", "Lunch?")));
  m.show(std::make_unique<NavScreen>(navMsg(proto::Maneuver::Left, 250, "Market St")));
  m.show(std::make_unique<IncomingCallScreen>(call(9, "Mom")));
  // Call outranks nav outranks notification.
  CHECK(m.active()->kind() == ScreenKind::IncomingCall);

  CHECK(m.removeByUid(9));  // call ended on the phone
  CHECK(m.active()->kind() == ScreenKind::Nav);  // nav resumed, not the notif

  m.clearKind(ScreenKind::Nav);
  CHECK(m.active()->kind() == ScreenKind::Notification);

  for (int i = 0; i < 81; ++i) m.tick();  // notification times out
  CHECK(m.active() == nullptr);

  const std::vector<std::string> expect = {
      "enter:Notification",
      "exit:Notification:preempted",
      "enter:Nav",
      "exit:Nav:preempted",
      "enter:IncomingCall",
      "exit:IncomingCall:removed",
      "enter:Nav",
      "exit:Nav:clear",
      "enter:Notification",
      "exit:Notification:finished",
  };
  CHECK(m.transitions() == expect);
  checkWellFormed(m);
}

TEST(equal_priority_is_fifo) {
  ScreenManager m;
  m.show(std::make_unique<NotificationScreen>(social(1, "One", "first")));
  m.show(std::make_unique<NotificationScreen>(social(2, "Two", "second")));
  m.show(std::make_unique<NotificationScreen>(social(3, "Three", "third")));
  CHECK_EQ(m.active()->contentUid(), uint32_t(1));
  m.handleButton(Button::A);
  CHECK_EQ(m.active()->contentUid(), uint32_t(2));
  m.handleButton(Button::A);
  CHECK_EQ(m.active()->contentUid(), uint32_t(3));
  m.handleButton(Button::A);
  CHECK(m.active() == nullptr);
  checkWellFormed(m);
}

TEST(same_identity_updates_in_place) {
  ScreenManager m;
  m.show(std::make_unique<NavScreen>(navMsg(proto::Maneuver::Left, 250, "Market St")));
  m.show(std::make_unique<NavScreen>(navMsg(proto::Maneuver::Left, 80, "Market St")));
  m.show(std::make_unique<NotificationScreen>(social(4, "Ana", "hey")));  // queued
  m.show(std::make_unique<NotificationScreen>(social(4, "Ana", "hey (edited)")));
  const std::vector<std::string> expect = {
      "enter:Nav",
      "update:Nav",
      "update:Notification:queued",
  };
  CHECK(m.transitions() == expect);
  checkWellFormed(m);
}

TEST(remove_from_queue_without_transition) {
  ScreenManager m;
  m.show(std::make_unique<NotificationScreen>(social(1, "A", "x")));
  m.show(std::make_unique<NotificationScreen>(social(2, "B", "y")));
  CHECK(m.removeByUid(2));       // dismissed on the phone while queued
  CHECK(!m.removeByUid(2));      // second removal is a no-op
  m.handleButton(Button::A);
  CHECK(m.active() == nullptr);  // 2 never surfaced
  checkWellFormed(m);
}

TEST(call_buttons_map_to_ancs_actions) {
  ScreenManager m;
  m.show(std::make_unique<IncomingCallScreen>(call(9, "Mom")));
  ScreenAction a = m.handleButton(Button::B);
  CHECK(a.type == ScreenAction::Type::AncsPositive);
  CHECK_EQ(a.uid, uint32_t(9));
  a = m.handleButton(Button::A);
  CHECK(a.type == ScreenAction::Type::AncsNegative);
  CHECK_EQ(a.uid, uint32_t(9));
  // Screen stays up until ANCS removes it (the phone confirms the action).
  CHECK(m.active() != nullptr);

  // A call with no accept affordance ignores B.
  ScreenManager m2;
  auto c = call(10, "Unknown");
  c.eventFlags = ancs::kFlagNegativeAction;
  m2.show(std::make_unique<IncomingCallScreen>(c));
  CHECK(m2.handleButton(Button::B).type == ScreenAction::Type::None);
}

TEST(notification_next_page_and_dismiss_flow) {
  ScreenManager m;
  m.show(std::make_unique<NotificationScreen>(social(
      1, "Long one",
      "This message is long enough to wrap across more than one page of the "
      "tiny display so the pager and the next-page button both matter.")));
  auto* n = static_cast<NotificationScreen*>(m.active());
  CHECK(n->pageCount() > 1);
  for (int p = 0; p < n->pageCount() - 1; ++p) {
    CHECK(m.handleButton(Button::B).type == ScreenAction::Type::None);
    CHECK(m.active() != nullptr);
  }
  m.handleButton(Button::B);  // B on the last page dismisses
  CHECK(m.active() == nullptr);
  checkWellFormed(m);
}

TEST(idle_screen_follows_connection) {
  ScreenManager m;
  DeviceState s = fixtureState();
  CHECK(m.idleKind(s) == ScreenKind::Clock);
  s.connected = false;
  CHECK(m.idleKind(s) == ScreenKind::Status);
}

// ---- Golden frames ----

TEST(golden_splash) {
  FrameBuffer fb;
  SplashScreen splash;
  splash.render(fb, 2, fixtureState());
  testfw::checkGolden("screen_splash", fb);
}

TEST(golden_clock_idle) {
  FrameBuffer fb;
  ClockScreen clock;
  clock.render(fb, 0, fixtureState());
  testfw::checkGolden("screen_clock", fb);
}

TEST(golden_status_disconnected) {
  FrameBuffer fb;
  StatusScreen status;
  DeviceState s = fixtureState();
  s.connected = false;
  s.bonded = true;
  status.render(fb, 0, s);
  testfw::checkGolden("screen_status", fb);
}

TEST(golden_notification_short) {
  FrameBuffer fb;
  NotificationScreen n(social(1, "Ravi", "Lunch at 12?"));
  n.render(fb, 0, fixtureState());
  testfw::checkGolden("screen_notification", fb);
}

TEST(golden_notification_page2) {
  FrameBuffer fb;
  NotificationScreen n(social(
      2, "Meeting notes",
      "Standup moved to 10am tomorrow. Bring the prototype and the battery "
      "measurements. Sarah wants the demo on the real panel."));
  n.render(fb, 30, fixtureState());  // 25 ticks/page -> page 2
  testfw::checkGolden("screen_notification_page2", fb);
}

TEST(golden_incoming_call) {
  FrameBuffer fb;
  IncomingCallScreen c(call(9, "Mom"));
  c.render(fb, 0, fixtureState());
  testfw::checkGolden("screen_incoming_call", fb);
}

TEST(golden_assistant_page2) {
  FrameBuffer fb;
  AssistantScreen a(
      "Sunny with a high of 22C. Light wind from the northwest in the "
      "afternoon. Tomorrow looks similar but two degrees cooler with a "
      "chance of fog rolling in from the bay after sunset.",
      /*streaming=*/false);
  CHECK(a.pageCount() >= 2);
  a.render(fb, 35, fixtureState());  // 30 ticks/page -> page 2, "2/N" indicator
  testfw::checkGolden("screen_assistant_page2", fb);
}

TEST(golden_nav_left) {
  FrameBuffer fb;
  NavScreen n(navMsg(proto::Maneuver::Left, 250, "Market St"));
  n.render(fb, 0, fixtureState());
  testfw::checkGolden("screen_nav_left", fb);
}

TEST(golden_nav_arrived) {
  FrameBuffer fb;
  NavScreen n(navMsg(proto::Maneuver::Arrive, 0, "Ferry Building"));
  n.render(fb, 5, fixtureState());
  testfw::checkGolden("screen_nav_arrived", fb);
}

TEST(nav_distance_formatting) {
  CHECK_EQ(formatNavDistance(850).big, "850");
  CHECK_EQ(formatNavDistance(850).unit, "m");
  CHECK_EQ(formatNavDistance(1250).big, "1.2");
  CHECK_EQ(formatNavDistance(1250).unit, "km");
  CHECK_EQ(formatNavDistance(12600).big, "12");
  CHECK_EQ(formatNavDistance(12600).unit, "km");
  CHECK(formatNavDistance(proto::kDistanceUnknown).big.empty());
}

TESTFW_MAIN
