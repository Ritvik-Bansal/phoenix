#pragma once

// Portable model of the Apple Notification Center Service concepts, so all
// display behavior is testable off-device. The firmware's BLEAncs callbacks
// translate into AncsEvent; the simulator fabricates the same events from
// scenario scripts.

#include <cstdint>
#include <string>
#include <vector>

#include "phoenix/framebuffer.h"

namespace phoenix {
namespace ancs {

// Apple-defined CategoryID values (ANCS spec).
enum class CategoryId : uint8_t {
  Other = 0,
  IncomingCall = 1,
  MissedCall = 2,
  Voicemail = 3,
  Social = 4,
  Schedule = 5,
  Email = 6,
  News = 7,
  HealthAndFitness = 8,
  BusinessAndFinance = 9,
  Location = 10,
  Entertainment = 11,
};

// Apple-defined EventID values.
enum class EventId : uint8_t {
  Added = 0,
  Modified = 1,
  Removed = 2,
};

// Apple-defined EventFlags bits.
inline constexpr uint8_t kFlagSilent = 0x01;
inline constexpr uint8_t kFlagImportant = 0x02;
inline constexpr uint8_t kFlagPreExisting = 0x04;
inline constexpr uint8_t kFlagPositiveAction = 0x08;
inline constexpr uint8_t kFlagNegativeAction = 0x10;

struct AncsNotification {
  uint32_t uid = 0;
  CategoryId category = CategoryId::Other;
  uint8_t eventFlags = 0;
  std::string appIdentifier;   // bundle id, e.g. "net.whatsapp.WhatsApp"
  std::string appDisplayName;  // from Get App Attributes; may be empty
  std::string title;
  std::string subtitle;
  std::string message;
  std::string date;  // raw ANCS date attribute: yyyyMMdd'T'HHmmSS

  bool isSilent() const { return eventFlags & kFlagSilent; }
  bool isImportant() const { return eventFlags & kFlagImportant; }
  bool isPreExisting() const { return eventFlags & kFlagPreExisting; }
  bool hasPositiveAction() const { return eventFlags & kFlagPositiveAction; }
  bool hasNegativeAction() const { return eventFlags & kFlagNegativeAction; }

  // Best display name: app attribute if fetched, else a readable fallback
  // derived from the bundle id's last segment.
  std::string displayAppName() const;
};

struct AncsEvent {
  EventId event = EventId::Added;
  AncsNotification notification;  // for Removed, uid/category are meaningful
};

// Display treatment for each documented ANCS category.
const Sprite& categoryIcon(CategoryId c);
const char* categoryName(CategoryId c);

// Active-notification set keyed by UID. apply() folds an ANCS event in and
// answers what the display should do about it.
class AncsStore {
 public:
  enum class Effect {
    None,    // stored (or ignored); nothing to show — e.g. pre-existing
    Show,    // newly added: present a screen
    Update,  // changed while it may be on screen: refresh in place
    Remove,  // gone on the phone: clear it from the display
  };

  Effect apply(const AncsEvent& e);
  const AncsNotification* find(uint32_t uid) const;
  size_t size() const { return items_.size(); }
  void clear() { items_.clear(); }

 private:
  std::vector<AncsNotification> items_;
};

}  // namespace ancs
}  // namespace phoenix
