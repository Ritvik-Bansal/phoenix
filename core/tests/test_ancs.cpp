// ANCS domain model, civil-time math, and the battery curve.

#include "phoenix/ancs.h"
#include "phoenix/battery.h"
#include "phoenix/datetime.h"
#include "test_framework.h"

using namespace phoenix;
using namespace phoenix::ancs;

namespace {
AncsNotification makeNotif(uint32_t uid, CategoryId cat, const char* title,
                           const char* msg, uint8_t flags = 0) {
  AncsNotification n;
  n.uid = uid;
  n.category = cat;
  n.eventFlags = flags;
  n.appIdentifier = "net.whatsapp.WhatsApp";
  n.title = title;
  n.message = msg;
  return n;
}
}  // namespace

TEST(store_add_show_and_find) {
  AncsStore store;
  const auto n = makeNotif(7, CategoryId::Social, "Ravi", "Lunch at 12?");
  CHECK(store.apply({EventId::Added, n}) == AncsStore::Effect::Show);
  CHECK_EQ(store.size(), size_t(1));
  const AncsNotification* found = store.find(7);
  CHECK(found != nullptr);
  CHECK_EQ(found->title, "Ravi");
  CHECK_EQ(found->message, "Lunch at 12?");
  CHECK(store.find(8) == nullptr);
}

TEST(store_preexisting_stays_quiet) {
  AncsStore store;
  const auto n =
      makeNotif(1, CategoryId::Email, "Old mail", "from last week", kFlagPreExisting);
  CHECK(store.apply({EventId::Added, n}) == AncsStore::Effect::None);
  CHECK_EQ(store.size(), size_t(1));  // stored, just not popped
}

TEST(store_modified_merges_changed_fields) {
  AncsStore store;
  store.apply({EventId::Added, makeNotif(3, CategoryId::Social, "Ana", "hi")});
  AncsNotification mod;
  mod.uid = 3;
  mod.category = CategoryId::Social;
  mod.message = "hi (edited: hello!)";  // only the message changed
  CHECK(store.apply({EventId::Modified, mod}) == AncsStore::Effect::Update);
  const auto* n = store.find(3);
  CHECK_EQ(n->title, "Ana");  // empty fields in the event didn't clobber
  CHECK_EQ(n->message, "hi (edited: hello!)");
}

TEST(store_removed) {
  AncsStore store;
  store.apply({EventId::Added, makeNotif(5, CategoryId::Social, "x", "y")});
  AncsNotification gone;
  gone.uid = 5;
  CHECK(store.apply({EventId::Removed, gone}) == AncsStore::Effect::Remove);
  CHECK_EQ(store.size(), size_t(0));
  CHECK(store.apply({EventId::Removed, gone}) == AncsStore::Effect::None);
}

TEST(display_app_name_fallbacks) {
  AncsNotification n;
  n.appIdentifier = "net.whatsapp.WhatsApp";
  CHECK_EQ(n.displayAppName(), "WhatsApp");
  n.appIdentifier = "signal";
  CHECK_EQ(n.displayAppName(), "Signal");
  n.appIdentifier = "";
  CHECK_EQ(n.displayAppName(), "App");
  n.appIdentifier = "com.apple.MobileSMS";
  n.appDisplayName = "Messages";  // fetched app attribute wins
  CHECK_EQ(n.displayAppName(), "Messages");
}

TEST(category_mapping_covers_documented_set) {
  // Every documented category maps to an icon and a label; call variants
  // are visually distinct.
  for (int c = 0; c <= 11; ++c) {
    const auto cat = static_cast<CategoryId>(c);
    CHECK(categoryIcon(cat).data != nullptr);
    CHECK(categoryName(cat)[0] != '\0');
  }
  CHECK(categoryIcon(CategoryId::IncomingCall).data !=
        categoryIcon(CategoryId::MissedCall).data);
  CHECK_EQ(std::string(categoryName(CategoryId::Email)), "Email");
}

TEST(datetime_leap_years_and_month_lengths) {
  CHECK(isLeapYear(2024));
  CHECK(!isLeapYear(2026));
  CHECK(!isLeapYear(2100));
  CHECK(isLeapYear(2000));
  CHECK_EQ(daysInMonth(2024, 2), 29);
  CHECK_EQ(daysInMonth(2026, 2), 28);
  CHECK_EQ(daysInMonth(2026, 9), 30);
  CHECK_EQ(daysInMonth(2026, 12), 31);
}

TEST(datetime_add_seconds_rollovers) {
  DateTime dt{2026, 12, 31, 23, 59, 59};
  addSeconds(dt, 1);
  CHECK_EQ(dt.year, 2027);
  CHECK_EQ(dt.month, 1);
  CHECK_EQ(dt.day, 1);
  CHECK_EQ(dt.hour, 0);
  CHECK_EQ(dt.minute, 0);
  CHECK_EQ(dt.second, 0);

  DateTime feb{2024, 2, 28, 12, 0, 0};
  addSeconds(feb, 86400);
  CHECK_EQ(feb.month, 2);
  CHECK_EQ(feb.day, 29);  // leap day exists in 2024
  addSeconds(feb, 86400);
  CHECK_EQ(feb.month, 3);
  CHECK_EQ(feb.day, 1);
}

TEST(datetime_day_of_week_and_formatting) {
  CHECK_EQ(dayOfWeek({2026, 8, 7, 0, 0, 0}), 5);   // Friday
  CHECK_EQ(dayOfWeek({2000, 1, 1, 0, 0, 0}), 6);   // Saturday
  CHECK_EQ(dayOfWeek({2024, 2, 29, 0, 0, 0}), 4);  // Thursday
  CHECK_EQ(formatClock({2026, 8, 7, 9, 5, 0}), "09:05");
  CHECK_EQ(formatClock({2026, 8, 7, 23, 41, 0}), "23:41");
  CHECK_EQ(formatDate({2026, 8, 7, 0, 0, 0}), "Fri Aug 7");
}

TEST(battery_curve_anchors_and_monotonic) {
  CHECK_EQ(lipoPercentFromMillivolts(4250), 100);  // clamps high
  CHECK_EQ(lipoPercentFromMillivolts(4200), 100);
  CHECK_EQ(lipoPercentFromMillivolts(4110), 95);
  CHECK_EQ(lipoPercentFromMillivolts(3750), 30);
  CHECK_EQ(lipoPercentFromMillivolts(3700), 20);
  CHECK_EQ(lipoPercentFromMillivolts(3300), 0);
  CHECK_EQ(lipoPercentFromMillivolts(3000), 0);  // clamps low
  // The whole point vs. a linear map: 3.8 V is mid-charge, not near-dead.
  CHECK(lipoPercentFromMillivolts(3800) >= 40);
  int prev = -1;
  for (int mv = 3200; mv <= 4300; mv += 10) {
    const int pct = lipoPercentFromMillivolts(mv);
    CHECK(pct >= prev);
    CHECK(pct >= 0);
    CHECK(pct <= 100);
    prev = pct;
  }
}

TESTFW_MAIN
