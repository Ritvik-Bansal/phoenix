#pragma once

// The seven HUD screens. Each renders into the framebuffer as a pure function
// of its content, the tick count since it was entered, and the shared device
// state — no wall clock, no hardware, fully golden-testable.

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "phoenix/ancs.h"
#include "phoenix/datetime.h"
#include "phoenix/framebuffer.h"
#include "phoenix/layout.h"
#include "phoenix/protocol.h"

namespace phoenix {

enum class ScreenKind : uint8_t {
  Splash,
  Clock,
  Status,
  Notification,
  Assistant,
  Nav,
  IncomingCall,
};

// Preemption order: incoming call > navigation > assistant > notification >
// status overlay. Splash outranks everything during boot. Clock/Status are
// also the idle fallbacks (priority 0 path) rendered when nothing is queued.
int screenPriority(ScreenKind k);
const char* screenKindName(ScreenKind k);

enum class Button : uint8_t { A = 1, B = 2, C = 3 };  // matches PROTOCOL.md

// What a button press on a screen asks the system to do.
struct ScreenAction {
  enum class Type { None, Dismiss, AncsPositive, AncsNegative };
  Type type = Type::None;
  uint32_t uid = 0;
};

// Read-only context every render sees.
struct DeviceState {
  bool connected = false;
  bool bonded = false;
  int batteryPercent = 100;
  int batteryMillivolts = 4100;
  DateTime time;
  bool haveTime = false;
  uint8_t brightness = 255;
};

class Screen {
 public:
  virtual ~Screen() = default;
  virtual ScreenKind kind() const = 0;
  const char* name() const { return screenKindName(kind()); }
  virtual void render(FrameBuffer& fb, uint32_t localTick,
                      const DeviceState& state) = 0;
  // True once the screen has run its course; the manager tears it down.
  virtual bool finished(uint32_t /*localTick*/) const { return false; }
  virtual ScreenAction onButton(Button b, uint32_t localTick);
  // In-place refresh from a newer screen of the same identity (same kind, and
  // same UID where applicable). True if absorbed — no screen transition.
  // localTick is the receiving screen's current local tick, for animations
  // that restart on content change.
  virtual bool updateFrom(const Screen& other, uint32_t localTick) {
    (void)other;
    (void)localTick;
    return false;
  }
  virtual uint32_t contentUid() const { return 0; }

 protected:
  // Manual page jumps: screens add this bias to localTick for their pagers.
  uint32_t tickBias_ = 0;
};

class SplashScreen : public Screen {
 public:
  explicit SplashScreen(uint32_t showTicks = 18) : showTicks_(showTicks) {}
  ScreenKind kind() const override { return ScreenKind::Splash; }
  void render(FrameBuffer& fb, uint32_t t, const DeviceState& s) override;
  bool finished(uint32_t t) const override { return t >= showTicks_; }

 private:
  uint32_t showTicks_;
};

class ClockScreen : public Screen {
 public:
  ScreenKind kind() const override { return ScreenKind::Clock; }
  void render(FrameBuffer& fb, uint32_t t, const DeviceState& s) override;
};

class StatusScreen : public Screen {
 public:
  // Idle mode (overlay = false) renders forever as the disconnected home
  // screen; overlay mode is the button-summoned popup and times out.
  explicit StatusScreen(bool overlay = false, uint32_t showTicks = 50)
      : overlay_(overlay), showTicks_(showTicks) {}
  ScreenKind kind() const override { return ScreenKind::Status; }
  void render(FrameBuffer& fb, uint32_t t, const DeviceState& s) override;
  bool finished(uint32_t t) const override {
    return overlay_ && t >= showTicks_;
  }

 private:
  bool overlay_;
  uint32_t showTicks_;
};

class NotificationScreen : public Screen {
 public:
  explicit NotificationScreen(const ancs::AncsNotification& n);
  ScreenKind kind() const override { return ScreenKind::Notification; }
  void render(FrameBuffer& fb, uint32_t t, const DeviceState& s) override;
  bool finished(uint32_t t) const override;
  ScreenAction onButton(Button b, uint32_t t) override;
  bool updateFrom(const Screen& other, uint32_t localTick) override;
  uint32_t contentUid() const override { return notification_.uid; }
  int pageCount() const { return pager_.pageCount(); }

 private:
  void rebuildLayout();
  ancs::AncsNotification notification_;
  std::string appLine_;
  std::string titleLine_;
  std::vector<std::string> bodyLines_;
  Marquee titleMarquee_;
  VerticalPager pager_;
  uint32_t minShowTicks_ = 80;
};

class IncomingCallScreen : public Screen {
 public:
  explicit IncomingCallScreen(const ancs::AncsNotification& n);
  ScreenKind kind() const override { return ScreenKind::IncomingCall; }
  void render(FrameBuffer& fb, uint32_t t, const DeviceState& s) override;
  // Safety net only — normally dismissed by the ANCS Removed event when the
  // call is answered elsewhere or stops ringing.
  bool finished(uint32_t t) const override { return t >= 600; }
  ScreenAction onButton(Button b, uint32_t t) override;
  bool updateFrom(const Screen& other, uint32_t localTick) override;
  uint32_t contentUid() const override { return notification_.uid; }

 private:
  ancs::AncsNotification notification_;
  std::string caller_;
  Marquee callerMarquee_;
};

class AssistantScreen : public Screen {
 public:
  AssistantScreen(const std::string& displayText, bool streaming);
  ScreenKind kind() const override { return ScreenKind::Assistant; }
  void render(FrameBuffer& fb, uint32_t t, const DeviceState& s) override;
  bool finished(uint32_t t) const override;
  ScreenAction onButton(Button b, uint32_t t) override;
  bool updateFrom(const Screen& other, uint32_t localTick) override;
  // Direct content refresh used by Device while chunks stream in.
  void setContent(const std::string& displayText, bool streaming,
                  uint32_t nowLocalTick);
  int pageCount() const { return pager_.pageCount(); }

 private:
  uint32_t pagingTick(uint32_t t) const;
  std::string text_;
  bool streaming_ = false;
  std::vector<std::string> lines_;
  VerticalPager pager_;
  uint32_t pagingStart_ = 0;  // localTick when the final text began paging
};

class NavScreen : public Screen {
 public:
  explicit NavScreen(const proto::NavUpdateMsg& nav);
  ScreenKind kind() const override { return ScreenKind::Nav; }
  void render(FrameBuffer& fb, uint32_t t, const DeviceState& s) override;
  // Persistent until CLEAR/dismiss; after ARRIVE it lingers briefly and ends.
  bool finished(uint32_t t) const override;
  ScreenAction onButton(Button b, uint32_t t) override;
  bool updateFrom(const Screen& other, uint32_t localTick) override;

 private:
  void apply(const proto::NavUpdateMsg& nav, uint32_t atTick);
  proto::NavUpdateMsg nav_;
  std::string street_;
  Marquee streetMarquee_;
  uint32_t arrivedAt_ = 0;
  bool arrived_ = false;
};

// "850" + "m" / "1.2" + "km" split for big-digit + small-unit rendering.
struct NavDistance {
  std::string big;
  std::string unit;
};
NavDistance formatNavDistance(uint16_t meters);

// Shared indicator widget (clock + status screens).
void drawBatteryIcon(FrameBuffer& fb, int x, int y, int percent);

}  // namespace phoenix
