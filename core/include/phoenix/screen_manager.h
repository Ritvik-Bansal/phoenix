#pragma once

// Priority-queue screen state machine. Invariants:
//   - at most one active screen; a screen is always formally torn down
//     (exit logged) before the next one is entered
//   - higher priority preempts immediately; the preempted screen returns to
//     the queue (with its timers restarted on resume) unless it had finished
//   - equal priority queues FIFO
//   - same-identity screens (nav, assistant, same-UID notifications/calls)
//     update the existing screen in place: no transition, no re-entry
//   - when nothing is active, the idle screen renders: clock while connected,
//     status while not
// The transition log makes the machine's behavior directly assertable.

#include <memory>
#include <string>
#include <vector>

#include "phoenix/framebuffer.h"
#include "phoenix/screens.h"

namespace phoenix {

class ScreenManager {
 public:
  // Advance time by one tick; tears down finished screens and promotes the
  // next queued one.
  void tick();

  // Present a screen (or absorb it into an existing same-identity one).
  void show(std::unique_ptr<Screen> s);

  // Route a button press to the active screen. Dismiss is handled here;
  // ANCS actions are returned for the device layer to execute.
  ScreenAction handleButton(Button b);

  // ANCS notification removed on the phone (or call ended): drop it wherever
  // it is. True if something was removed.
  bool removeByUid(uint32_t uid);

  // CLEAR handling / disconnect cleanup.
  void clearKind(ScreenKind kind);

  void render(FrameBuffer& fb, const DeviceState& state);

  Screen* active() { return active_.get(); }
  // Finds the active or a queued screen of a kind (active first).
  Screen* findByKind(ScreenKind kind);
  // Finds the screen carrying an ANCS uid, wherever it is.
  Screen* findByUid(uint32_t uid);
  uint32_t activeLocalTick() const {
    return active_ ? now_ - activeSince_ : 0;
  }
  uint32_t now() const { return now_; }
  ScreenKind idleKind(const DeviceState& state) const {
    return state.connected ? ScreenKind::Clock : ScreenKind::Status;
  }

  // "enter:<name>", "exit:<name>:<reason>", "update:<name>" — in order.
  const std::vector<std::string>& transitions() const { return transitions_; }

 private:
  void enter(std::unique_ptr<Screen> s);
  void teardown(const char* reason, bool requeueUnfinished);
  void promote();

  std::unique_ptr<Screen> active_;
  uint32_t activeSince_ = 0;
  std::vector<std::unique_ptr<Screen>> queue_;
  uint32_t now_ = 0;
  std::vector<std::string> transitions_;
  ClockScreen idleClock_;
  StatusScreen idleStatus_{false};
};

}  // namespace phoenix
