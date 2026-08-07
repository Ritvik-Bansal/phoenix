#include "phoenix/ancs.h"

#include "phoenix/font_data.h"

namespace phoenix {
namespace ancs {

std::string AncsNotification::displayAppName() const {
  if (!appDisplayName.empty()) return appDisplayName;
  if (appIdentifier.empty()) return "App";
  const size_t dot = appIdentifier.find_last_of('.');
  std::string name =
      dot == std::string::npos ? appIdentifier : appIdentifier.substr(dot + 1);
  if (name.empty()) return "App";
  if (name[0] >= 'a' && name[0] <= 'z') {
    name[0] = static_cast<char>(name[0] - 'a' + 'A');
  }
  return name;
}

const Sprite& categoryIcon(CategoryId c) {
  switch (c) {
    case CategoryId::IncomingCall: return kIconPhone;
    case CategoryId::MissedCall: return kIconPhoneMissed;
    case CategoryId::Voicemail: return kIconVoicemail;
    case CategoryId::Social: return kIconBubble;
    case CategoryId::Schedule: return kIconCalendar;
    case CategoryId::Email: return kIconEnvelope;
    case CategoryId::News: return kIconNews;
    case CategoryId::HealthAndFitness: return kIconHeart;
    case CategoryId::BusinessAndFinance: return kIconDollar;
    case CategoryId::Location: return kIconPin;
    case CategoryId::Entertainment: return kIconNote;
    case CategoryId::Other: break;
  }
  return kIconBell;
}

const char* categoryName(CategoryId c) {
  switch (c) {
    case CategoryId::IncomingCall: return "Call";
    case CategoryId::MissedCall: return "Missed call";
    case CategoryId::Voicemail: return "Voicemail";
    case CategoryId::Social: return "Message";
    case CategoryId::Schedule: return "Event";
    case CategoryId::Email: return "Email";
    case CategoryId::News: return "News";
    case CategoryId::HealthAndFitness: return "Health";
    case CategoryId::BusinessAndFinance: return "Finance";
    case CategoryId::Location: return "Location";
    case CategoryId::Entertainment: return "Media";
    case CategoryId::Other: break;
  }
  return "Notification";
}

AncsStore::Effect AncsStore::apply(const AncsEvent& e) {
  const uint32_t uid = e.notification.uid;
  auto it = items_.begin();
  for (; it != items_.end(); ++it) {
    if (it->uid == uid) break;
  }

  switch (e.event) {
    case EventId::Added:
      if (it != items_.end()) {
        *it = e.notification;  // duplicate add: treat as refresh
        return Effect::Update;
      }
      items_.push_back(e.notification);
      // Pre-existing notifications arrive in a burst right after (re)connect;
      // they are weeks of backlog, not news — keep them queryable but never
      // pop screens for them.
      return e.notification.isPreExisting() ? Effect::None : Effect::Show;

    case EventId::Modified:
      if (it != items_.end()) {
        // Modified events may carry only changed attributes; merge non-empty
        // fields over the stored ones.
        it->category = e.notification.category;
        it->eventFlags = e.notification.eventFlags;
        if (!e.notification.title.empty()) it->title = e.notification.title;
        if (!e.notification.subtitle.empty()) it->subtitle = e.notification.subtitle;
        if (!e.notification.message.empty()) it->message = e.notification.message;
        if (!e.notification.appIdentifier.empty()) {
          it->appIdentifier = e.notification.appIdentifier;
        }
        if (!e.notification.appDisplayName.empty()) {
          it->appDisplayName = e.notification.appDisplayName;
        }
        if (!e.notification.date.empty()) it->date = e.notification.date;
        return Effect::Update;
      }
      items_.push_back(e.notification);  // modify for an unknown uid: adopt it
      return Effect::Update;

    case EventId::Removed:
      if (it == items_.end()) return Effect::None;
      items_.erase(it);
      return Effect::Remove;
  }
  return Effect::None;
}

const AncsNotification* AncsStore::find(uint32_t uid) const {
  for (const auto& n : items_) {
    if (n.uid == uid) return &n;
  }
  return nullptr;
}

}  // namespace ancs
}  // namespace phoenix
