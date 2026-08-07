#pragma once

// Minimal civil-time math for the clock screen. The device has no RTC
// concept of its own: CTS (or a simulator scenario) sets an absolute time
// and the tick loop advances it deterministically — no system clock anywhere
// in core.

#include <cstdint>
#include <string>

namespace phoenix {

struct DateTime {
  int year = 2026;
  int month = 1;  // 1-12
  int day = 1;    // 1-31
  int hour = 0;
  int minute = 0;
  int second = 0;
};

bool isLeapYear(int year);
int daysInMonth(int year, int month);
void addSeconds(DateTime& dt, uint32_t seconds);  // handles full rollover
int dayOfWeek(const DateTime& dt);                // 0 = Sunday

std::string formatClock(const DateTime& dt);  // "09:41"
std::string formatDate(const DateTime& dt);   // "Thu Aug 7"

}  // namespace phoenix
