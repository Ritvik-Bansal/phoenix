#include "phoenix/screen_manager.h"

namespace phoenix {

void ScreenManager::tick() {
  ++now_;
  if (active_ && active_->finished(activeLocalTick())) {
    teardown("finished", /*requeueUnfinished=*/false);
  }
  if (!active_) promote();
}

void ScreenManager::show(std::unique_ptr<Screen> s) {
  // Same-identity in-place update: active first, then the queue.
  if (active_ && active_->updateFrom(*s, activeLocalTick())) {
    transitions_.push_back(std::string("update:") + active_->name());
    return;
  }
  for (auto& q : queue_) {
    if (q->updateFrom(*s, 0)) {
      transitions_.push_back(std::string("update:") + q->name() + ":queued");
      return;
    }
  }

  if (!active_) {
    enter(std::move(s));
    return;
  }
  if (screenPriority(s->kind()) > screenPriority(active_->kind())) {
    teardown("preempted", /*requeueUnfinished=*/true);
    enter(std::move(s));
    return;
  }
  queue_.push_back(std::move(s));
}

ScreenAction ScreenManager::handleButton(Button b) {
  ScreenAction a;
  if (!active_) return a;
  a = active_->onButton(b, activeLocalTick());
  if (a.type == ScreenAction::Type::Dismiss) {
    teardown("button", /*requeueUnfinished=*/false);
    promote();
    a.type = ScreenAction::Type::None;  // consumed here
  }
  return a;
}

bool ScreenManager::removeByUid(uint32_t uid) {
  if (uid == 0) return false;
  if (active_ && active_->contentUid() == uid) {
    teardown("removed", /*requeueUnfinished=*/false);
    promote();
    return true;
  }
  for (auto it = queue_.begin(); it != queue_.end(); ++it) {
    if ((*it)->contentUid() == uid) {
      queue_.erase(it);
      return true;
    }
  }
  return false;
}

void ScreenManager::clearKind(ScreenKind kind) {
  for (auto it = queue_.begin(); it != queue_.end();) {
    if ((*it)->kind() == kind) {
      it = queue_.erase(it);
    } else {
      ++it;
    }
  }
  if (active_ && active_->kind() == kind) {
    teardown("clear", /*requeueUnfinished=*/false);
    promote();
  }
}

void ScreenManager::render(FrameBuffer& fb, const DeviceState& state) {
  fb.clear();
  if (active_) {
    active_->render(fb, activeLocalTick(), state);
  } else if (idleKind(state) == ScreenKind::Clock) {
    idleClock_.render(fb, now_, state);
  } else {
    idleStatus_.render(fb, now_, state);
  }
}

Screen* ScreenManager::findByKind(ScreenKind kind) {
  if (active_ && active_->kind() == kind) return active_.get();
  for (auto& q : queue_) {
    if (q->kind() == kind) return q.get();
  }
  return nullptr;
}

Screen* ScreenManager::findByUid(uint32_t uid) {
  if (uid == 0) return nullptr;
  if (active_ && active_->contentUid() == uid) return active_.get();
  for (auto& q : queue_) {
    if (q->contentUid() == uid) return q.get();
  }
  return nullptr;
}

void ScreenManager::enter(std::unique_ptr<Screen> s) {
  // teardown-before-enter invariant: entering over a live screen is a bug.
  if (active_) teardown("invariant", /*requeueUnfinished=*/true);
  active_ = std::move(s);
  activeSince_ = now_;
  transitions_.push_back(std::string("enter:") + active_->name());
}

void ScreenManager::teardown(const char* reason, bool requeueUnfinished) {
  if (!active_) return;
  const uint32_t local = activeLocalTick();  // before active_ moves away
  transitions_.push_back(std::string("exit:") + active_->name() + ":" + reason);
  std::unique_ptr<Screen> old = std::move(active_);
  if (requeueUnfinished && !old->finished(local)) {
    queue_.push_back(std::move(old));
  }
}

void ScreenManager::promote() {
  if (active_ || queue_.empty()) return;
  // Highest priority wins; FIFO within equal priority (stable pick of the
  // earliest max).
  size_t best = 0;
  for (size_t i = 1; i < queue_.size(); ++i) {
    if (screenPriority(queue_[i]->kind()) >
        screenPriority(queue_[best]->kind())) {
      best = i;
    }
  }
  std::unique_ptr<Screen> next = std::move(queue_[best]);
  queue_.erase(queue_.begin() + static_cast<long>(best));
  enter(std::move(next));
}

}  // namespace phoenix
