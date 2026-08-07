#include "phoenix/screens.h"

#include "phoenix/font.h"
#include "phoenix/font_data.h"
#include "phoenix/text_sanitize.h"
#include "phoenix/version.h"

namespace phoenix {

namespace {

constexpr int kBodyRow = 8;  // body font line pitch

int centeredX(const Font& font, const std::string& s) {
  const int w = measureText(font, s).width;
  return w >= kDisplayWidth ? 0 : (kDisplayWidth - w) / 2;
}

const Sprite& maneuverArrow(proto::Maneuver m) {
  using proto::Maneuver;
  switch (m) {
    case Maneuver::Left: return kArrowLeft;
    case Maneuver::Right: return kArrowRight;
    case Maneuver::SlightLeft: return kArrowSlightLeft;
    case Maneuver::SlightRight: return kArrowSlightRight;
    case Maneuver::SharpLeft: return kArrowSharpLeft;
    case Maneuver::SharpRight: return kArrowSharpRight;
    case Maneuver::UTurn: return kArrowUturn;
    case Maneuver::Arrive: return kArrowArrive;
    case Maneuver::Straight: break;
  }
  return kArrowStraight;
}

}  // namespace

int screenPriority(ScreenKind k) {
  switch (k) {
    case ScreenKind::Splash: return 100;
    case ScreenKind::IncomingCall: return 50;
    case ScreenKind::Nav: return 40;
    case ScreenKind::Assistant: return 30;
    case ScreenKind::Notification: return 20;
    case ScreenKind::Status: return 10;
    case ScreenKind::Clock: return 0;
  }
  return 0;
}

const char* screenKindName(ScreenKind k) {
  switch (k) {
    case ScreenKind::Splash: return "Splash";
    case ScreenKind::Clock: return "Clock";
    case ScreenKind::Status: return "Status";
    case ScreenKind::Notification: return "Notification";
    case ScreenKind::Assistant: return "Assistant";
    case ScreenKind::Nav: return "Nav";
    case ScreenKind::IncomingCall: return "IncomingCall";
  }
  return "?";
}

ScreenAction Screen::onButton(Button b, uint32_t) {
  ScreenAction a;
  if (b == Button::A) a.type = ScreenAction::Type::Dismiss;
  return a;
}

void drawBatteryIcon(FrameBuffer& fb, int x, int y, int percent) {
  if (percent < 0) percent = 0;
  if (percent > 100) percent = 100;
  fb.drawRect(x, y, 10, 6);
  fb.fillRect(x + 10, y + 2, 1, 2);  // terminal nub
  const int fill = (percent * 8 + 50) / 100;
  if (fill > 0) fb.fillRect(x + 1, y + 1, fill, 4);
}

// ---- SplashScreen ----

void SplashScreen::render(FrameBuffer& fb, uint32_t t, const DeviceState&) {
  const Sprite& flame = ((t / 4) % 2 == 0) ? kFlameA : kFlameB;
  fb.blit(flame, (kDisplayWidth - flame.width) / 2, 2);
  drawText(fb, kFontBody, centeredX(kFontBody, "PHOENIX"), 20, "PHOENIX");
  const std::string ver = std::string("v") + kFirmwareVersion;
  drawText(fb, kFontBody, centeredX(kFontBody, ver), 30, ver);
}

// ---- ClockScreen ----

void ClockScreen::render(FrameBuffer& fb, uint32_t t, const DeviceState& s) {
  if (!s.haveTime) {
    drawText(fb, kFontBody, centeredX(kFontBody, "PHOENIX"), 8, "PHOENIX");
    drawText(fb, kFontBody, centeredX(kFontBody, "syncing clock"), 18,
             "syncing clock");
  } else {
    std::string time = formatClock(s.time);
    if ((t / 5) % 2 == 1) time[2] = ' ';  // colon blinks at 1 Hz
    drawText(fb, kFontClock, 12, 3, time);
    const std::string date = formatDate(s.time);
    drawText(fb, kFontBody, centeredX(kFontBody, date), 22, date);
  }
  if (s.connected) fb.blit(kIconBle, 1, 32);
  const std::string pct = std::to_string(s.batteryPercent) + "%";
  drawText(fb, kFontBody, 58 - measureText(kFontBody, pct).width, 32, pct);
  drawBatteryIcon(fb, 60, 32, s.batteryPercent);
}

// ---- StatusScreen ----

void StatusScreen::render(FrameBuffer& fb, uint32_t t, const DeviceState& s) {
  drawText(fb, kFontBody, 0, 0, "PHOENIX");
  if (s.connected) fb.blit(kIconBle, 66, 0);
  drawText(fb, kFontBody, 0, 8, std::string("FW ") + kFirmwareVersion);
  if (s.connected) {
    drawText(fb, kFontBody, 0, 16, "Connected");
  } else {
    const int w = drawText(fb, kFontBody, 0, 16, "Advertising");
    const int dots = static_cast<int>((t / 5) % 4);
    std::string trail;
    for (int i = 0; i < dots; ++i) trail += '.';
    drawText(fb, kFontBody, w, 16, trail);
  }
  drawText(fb, kFontBody, 0, 24, s.bonded ? "Bonded" : "Not bonded");
  drawBatteryIcon(fb, 0, 33, s.batteryPercent);
  drawText(fb, kFontBody, 15, 32, std::to_string(s.batteryPercent) + "%");
  const std::string mv = std::to_string(s.batteryMillivolts) + "mV";
  drawText(fb, kFontBody, kDisplayWidth - measureText(kFontBody, mv).width, 32,
           mv);
}

// ---- NotificationScreen ----

NotificationScreen::NotificationScreen(const ancs::AncsNotification& n)
    : notification_(n) {
  rebuildLayout();
}

void NotificationScreen::rebuildLayout() {
  const ancs::AncsNotification& n = notification_;
  appLine_ = truncateWithEllipsis(kFontBody,
                                  sanitizeForDisplay(n.displayAppName()), 62);

  std::string title = n.title;
  std::string body;
  if (title.empty()) {
    // Promote the subtitle to the title slot; last resort: category label.
    title = !n.subtitle.empty() ? n.subtitle : ancs::categoryName(n.category);
    body = n.message;
  } else if (!n.subtitle.empty()) {
    body = n.subtitle + "\n" + n.message;
  } else {
    body = n.message;
  }
  titleLine_ = sanitizeForDisplay(title);
  titleMarquee_ =
      Marquee(measureText(kFontBody, titleLine_).width, kDisplayWidth);

  // 69 px body column leaves the right edge for the page dots.
  bodyLines_ = wrapText(kFontBody, sanitizeForDisplay(body), 69);
  PagerConfig pc;
  pc.linesPerPage = 3;
  pc.ticksPerPage = 25;
  pager_ = VerticalPager(static_cast<int>(bodyLines_.size()), pc);
}

void NotificationScreen::render(FrameBuffer& fb, uint32_t t,
                                const DeviceState&) {
  fb.blit(ancs::categoryIcon(notification_.category), 0, 0);
  drawText(fb, kFontBody, 9, 0, appLine_);
  drawMarqueeText(fb, kFontBody, 0, 9, kDisplayWidth, titleLine_,
                  titleMarquee_, t);
  const int page = pager_.pageAt(t + tickBias_);
  const int first = pager_.firstLine(page);
  const int count = pager_.lineCountOn(page);
  for (int i = 0; i < count; ++i) {
    drawText(fb, kFontBody, 0, 17 + i * kBodyRow,
             bodyLines_[static_cast<size_t>(first + i)]);
  }
  const int pages = pager_.pageCount();
  if (pages > 1) {
    for (int p = 0; p < pages && p < 4; ++p) {
      const int y = 17 + p * 5;
      if (p == page) {
        fb.fillRect(70, y, 2, 2);
      } else {
        fb.setPixel(71, y);
      }
    }
  }
}

bool NotificationScreen::finished(uint32_t t) const {
  const uint32_t bt = t + tickBias_;
  uint32_t need = minShowTicks_;
  const uint32_t pagerNeed = static_cast<uint32_t>(pager_.pageCount()) * 25;
  if (pagerNeed > need) need = pagerNeed;
  const uint32_t marqueeNeed = static_cast<uint32_t>(titleMarquee_.cycleTicks());
  if (marqueeNeed > need) need = marqueeNeed;
  return bt >= need;
}

ScreenAction NotificationScreen::onButton(Button b, uint32_t t) {
  ScreenAction a;
  if (b == Button::A) {
    a.type = ScreenAction::Type::Dismiss;
  } else if (b == Button::B) {
    const uint32_t bt = t + tickBias_;
    if (pager_.pageAt(bt) < pager_.pageCount() - 1) {
      tickBias_ += pager_.nextBoundaryAfter(bt) - bt;  // jump to next page
    } else {
      a.type = ScreenAction::Type::Dismiss;  // "done reading" on last page
    }
  }
  return a;
}

bool NotificationScreen::updateFrom(const Screen& other, uint32_t) {
  if (other.kind() != ScreenKind::Notification ||
      other.contentUid() != contentUid()) {
    return false;
  }
  notification_ = static_cast<const NotificationScreen&>(other).notification_;
  rebuildLayout();
  return true;
}

// ---- IncomingCallScreen ----

IncomingCallScreen::IncomingCallScreen(const ancs::AncsNotification& n)
    : notification_(n) {
  caller_ = sanitizeForDisplay(!n.title.empty() ? n.title : n.displayAppName());
  callerMarquee_ = Marquee(measureText(kFontBody, caller_).width, kDisplayWidth);
}

void IncomingCallScreen::render(FrameBuffer& fb, uint32_t t,
                                const DeviceState&) {
  // Ring pulse: the phone icon blinks off briefly once a second.
  if ((t % 10) < 8) fb.blit(kIconPhone, 0, 0);
  drawText(fb, kFontBody, 10, 0, "INCOMING");
  drawMarqueeText(fb, kFontBody, 0, 12, kDisplayWidth, caller_, callerMarquee_,
                  t);
  const bool canAccept = notification_.hasPositiveAction();
  const bool canDecline = notification_.hasNegativeAction();
  // Physical layout: button A sits left (decline), button B right (accept).
  if (canDecline && canAccept) {
    fb.drawRect(2, 26, 32, 13);
    fb.blit(kIconCross, 14, 29);
    fb.drawRect(38, 26, 32, 13);
    fb.blit(kIconCheck, 50, 29);
  } else if (canDecline || canAccept) {
    fb.drawRect(20, 26, 32, 13);
    fb.blit(canAccept ? kIconCheck : kIconCross, 32, 29);
  }
  // Ring pulse: the phone icon blinks off one tick in eight.
  if ((t % 8) == 7) {
    fb.fillRect(0, 0, 7, 7, false);
    fb.blit(kIconPhone, 0, 0, false);
    fb.blit(kIconPhone, 0, 0);
  }
}

ScreenAction IncomingCallScreen::onButton(Button b, uint32_t) {
  ScreenAction a;
  a.uid = notification_.uid;
  if (b == Button::B && notification_.hasPositiveAction()) {
    a.type = ScreenAction::Type::AncsPositive;
  } else if (b == Button::A) {
    a.type = notification_.hasNegativeAction()
                 ? ScreenAction::Type::AncsNegative
                 : ScreenAction::Type::Dismiss;
  }
  return a;
}

bool IncomingCallScreen::updateFrom(const Screen& other, uint32_t) {
  if (other.kind() != ScreenKind::IncomingCall ||
      other.contentUid() != contentUid()) {
    return false;
  }
  const auto& o = static_cast<const IncomingCallScreen&>(other);
  notification_ = o.notification_;
  caller_ = o.caller_;
  callerMarquee_ = o.callerMarquee_;
  return true;
}

// ---- AssistantScreen ----

AssistantScreen::AssistantScreen(const std::string& displayText,
                                 bool streaming) {
  streaming_ = !streaming;  // force the state change in setContent
  setContent(displayText, streaming, 0);
}

void AssistantScreen::setContent(const std::string& displayText, bool streaming,
                                 uint32_t nowLocalTick) {
  const bool wasStreaming = streaming_;
  text_ = displayText;
  streaming_ = streaming;
  lines_ = wrapText(kFontBody, text_, kDisplayWidth);
  PagerConfig pc;
  pc.linesPerPage = 4;
  pc.ticksPerPage = 30;
  pager_ = VerticalPager(static_cast<int>(lines_.size()), pc);
  if (wasStreaming && !streaming_) {
    // Reply complete: page through it from the start.
    pagingStart_ = nowLocalTick;
    tickBias_ = 0;
  }
}

uint32_t AssistantScreen::pagingTick(uint32_t t) const {
  const uint32_t bt = t + tickBias_;
  return bt >= pagingStart_ ? bt - pagingStart_ : 0;
}

void AssistantScreen::render(FrameBuffer& fb, uint32_t t, const DeviceState&) {
  const int page = streaming_ ? pager_.pageCount() - 1
                              : pager_.pageAt(pagingTick(t));
  const int first = pager_.firstLine(page);
  const int count = pager_.lineCountOn(page);
  int lastW = 0;
  int lastY = 1;
  for (int i = 0; i < count; ++i) {
    const std::string& line = lines_[static_cast<size_t>(first + i)];
    lastY = 1 + i * kBodyRow;
    lastW = measureText(kFontBody, line).width;
    drawText(fb, kFontBody, 0, lastY, line);
  }
  fb.blit(kIconSpark, 0, 32);
  if (streaming_) {
    if ((t / 3) % 2 == 0) {  // typing cursor
      const int cx = lines_.empty() ? 0 : lastW + 2;
      fb.fillRect(cx > 68 ? 68 : cx, lastY, 3, 6);
    }
  } else if (pager_.pageCount() > 1) {
    const std::string ind =
        std::to_string(page + 1) + "/" + std::to_string(pager_.pageCount());
    drawText(fb, kFontBody, kDisplayWidth - measureText(kFontBody, ind).width,
             33, ind);
  }
}

bool AssistantScreen::finished(uint32_t t) const {
  if (streaming_) return false;
  const uint32_t pt = pagingTick(t);
  return pt >= static_cast<uint32_t>(pager_.pageCount()) * 30 + 20;
}

ScreenAction AssistantScreen::onButton(Button b, uint32_t t) {
  ScreenAction a;
  if (b == Button::A) {
    a.type = ScreenAction::Type::Dismiss;
  } else if (b == Button::B && !streaming_) {
    const uint32_t pt = pagingTick(t);
    if (pager_.pageAt(pt) < pager_.pageCount() - 1) {
      tickBias_ += pager_.nextBoundaryAfter(pt) - pt;
    } else {
      // Wrap to the first page for a re-read.
      pagingStart_ = t + tickBias_;
    }
  }
  return a;
}

bool AssistantScreen::updateFrom(const Screen& other, uint32_t localTick) {
  if (other.kind() != ScreenKind::Assistant) return false;
  const auto& o = static_cast<const AssistantScreen&>(other);
  setContent(o.text_, o.streaming_, localTick);
  return true;
}

// ---- NavScreen ----

NavScreen::NavScreen(const proto::NavUpdateMsg& nav) { apply(nav, 0); }

void NavScreen::apply(const proto::NavUpdateMsg& nav, uint32_t atTick) {
  nav_ = nav;
  street_ = sanitizeForDisplay(nav.street);
  streetMarquee_ =
      Marquee(measureText(kFontBody, street_).width, kDisplayWidth);
  if (nav.maneuver == proto::Maneuver::Arrive) {
    if (!arrived_) {
      arrived_ = true;
      arrivedAt_ = atTick;
    }
  } else {
    arrived_ = false;
  }
}

void NavScreen::render(FrameBuffer& fb, uint32_t t, const DeviceState&) {
  fb.blit(maneuverArrow(nav_.maneuver), 0, 4);
  if (arrived_) {
    drawText(fb, kFontBody, 22, 8, "Arrived");
  } else {
    const NavDistance d = formatNavDistance(nav_.distanceMeters);
    if (!d.big.empty()) {
      const int w = drawText(fb, kFontClock, 20, 2, d.big);
      drawText(fb, kFontBody, 20 + w + 1, 11, d.unit);
    }
  }
  drawMarqueeText(fb, kFontBody, 0, 28, kDisplayWidth, street_, streetMarquee_,
                  t);
}

bool NavScreen::finished(uint32_t t) const {
  return arrived_ && t >= arrivedAt_ + 100;
}

ScreenAction NavScreen::onButton(Button b, uint32_t) {
  ScreenAction a;
  if (b == Button::A) a.type = ScreenAction::Type::Dismiss;
  return a;
}

bool NavScreen::updateFrom(const Screen& other, uint32_t localTick) {
  if (other.kind() != ScreenKind::Nav) return false;
  apply(static_cast<const NavScreen&>(other).nav_, localTick);
  return true;
}

NavDistance formatNavDistance(uint16_t meters) {
  if (meters == proto::kDistanceUnknown) return {"", ""};
  if (meters < 1000) return {std::to_string(meters), "m"};
  if (meters < 10000) {
    return {std::to_string(meters / 1000) + "." +
                std::to_string((meters % 1000) / 100),
            "km"};
  }
  return {std::to_string(meters / 1000), "km"};
}

}  // namespace phoenix
