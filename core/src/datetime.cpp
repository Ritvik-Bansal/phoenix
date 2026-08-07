#include "phoenix/datetime.h"

namespace phoenix {

namespace {
const char* const kDayAbbrev[7] = {"Sun", "Mon", "Tue", "Wed",
                                   "Thu", "Fri", "Sat"};
const char* const kMonthAbbrev[12] = {"Jan", "Feb", "Mar", "Apr",
                                      "May", "Jun", "Jul", "Aug",
                                      "Sep", "Oct", "Nov", "Dec"};
}  // namespace

bool isLeapYear(int year) {
  return (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
}

int daysInMonth(int year, int month) {
  static const int kDays[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  if (month < 1 || month > 12) return 30;
  if (month == 2 && isLeapYear(year)) return 29;
  return kDays[month - 1];
}

void addSeconds(DateTime& dt, uint32_t seconds) {
  uint32_t total = static_cast<uint32_t>(dt.second) + seconds;
  dt.second = static_cast<int>(total % 60);
  uint32_t minutes = static_cast<uint32_t>(dt.minute) + total / 60;
  dt.minute = static_cast<int>(minutes % 60);
  uint32_t hours = static_cast<uint32_t>(dt.hour) + minutes / 60;
  dt.hour = static_cast<int>(hours % 24);
  uint32_t days = hours / 24;
  while (days-- > 0) {
    if (++dt.day > daysInMonth(dt.year, dt.month)) {
      dt.day = 1;
      if (++dt.month > 12) {
        dt.month = 1;
        ++dt.year;
      }
    }
  }
}

int dayOfWeek(const DateTime& dt) {
  // Sakamoto's algorithm, 0 = Sunday.
  static const int t[12] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
  int y = dt.year;
  if (dt.month < 3) --y;
  return (y + y / 4 - y / 100 + y / 400 + t[dt.month - 1] + dt.day) % 7;
}

std::string formatClock(const DateTime& dt) {
  std::string s;
  s.push_back(static_cast<char>('0' + dt.hour / 10));
  s.push_back(static_cast<char>('0' + dt.hour % 10));
  s.push_back(':');
  s.push_back(static_cast<char>('0' + dt.minute / 10));
  s.push_back(static_cast<char>('0' + dt.minute % 10));
  return s;
}

std::string formatDate(const DateTime& dt) {
  std::string s = kDayAbbrev[dayOfWeek(dt) % 7];
  s += ' ';
  s += kMonthAbbrev[(dt.month - 1) % 12];
  s += ' ';
  s += std::to_string(dt.day);
  return s;
}

}  // namespace phoenix
