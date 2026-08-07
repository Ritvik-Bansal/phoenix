#include "phoenix/battery.h"

namespace phoenix {

int lipoPercentFromMillivolts(int millivolts) {
  // Typical 1S LiPo open-circuit voltage vs. state of charge.
  struct Point {
    int mv;
    int pct;
  };
  static const Point kCurve[] = {
      {4200, 100}, {4110, 95}, {4080, 90}, {4020, 80}, {3980, 75},
      {3920, 65},  {3870, 55}, {3820, 45}, {3790, 40}, {3750, 30},
      {3700, 20},  {3600, 10}, {3500, 5},  {3400, 2},  {3300, 0},
  };
  constexpr int kPoints = sizeof(kCurve) / sizeof(kCurve[0]);

  if (millivolts >= kCurve[0].mv) return 100;
  if (millivolts <= kCurve[kPoints - 1].mv) return 0;
  for (int i = 1; i < kPoints; ++i) {
    if (millivolts >= kCurve[i].mv) {
      const Point& hi = kCurve[i - 1];
      const Point& lo = kCurve[i];
      // Linear interpolation inside the segment.
      return lo.pct +
             (millivolts - lo.mv) * (hi.pct - lo.pct) / (hi.mv - lo.mv);
    }
  }
  return 0;
}

}  // namespace phoenix
