// Golden-frame tests for the raw text/icon rendering layer. Screen-level
// goldens (clock, notification, call, ...) live in test_screens.cpp.

#include "golden.h"
#include "phoenix/font.h"
#include "phoenix/font_data.h"
#include "phoenix/text_sanitize.h"
#include "test_framework.h"

using namespace phoenix;

TEST(golden_text_specimen) {
  FrameBuffer fb;
  drawText(fb, kFontBody, 0, 0, "The quick brown");
  drawText(fb, kFontBody, 0, 8, "fox jumps 0123");
  drawText(fb, kFontBody, 0, 16, "!\"#$%&'()*+,-./");
  drawText(fb, kFontBody, 0, 24, "gyqpj ?~{}[]<=>");
  drawText(fb, kFontBody, 0, 32, sanitizeForDisplay("caf\xC3\xA9 \xF0\x9F\x94\xA5 ok"));
  testfw::checkGolden("text_specimen", fb);
}

TEST(golden_clock_digits) {
  FrameBuffer fb;
  drawText(fb, kFontClock, 12, 2, "09:41");
  drawText(fb, kFontClock, 23, 22, "3.7");
  drawText(fb, kFontBody, 52, 31, "km");
  testfw::checkGolden("clock_digits", fb);
}

TEST(golden_icons_strip) {
  FrameBuffer fb;
  const Sprite* row0[] = {&kIconBell,     &kIconBubble, &kIconEnvelope,
                          &kIconCalendar, &kIconPhone,  &kIconHeart,
                          &kIconPin,      &kIconSpark};
  const Sprite* row1[] = {&kIconNews,        &kIconDollar, &kIconNote,
                          &kIconVoicemail,   &kIconPhoneMissed,
                          &kIconCheck,       &kIconCross,  &kIconBle};
  for (int i = 0; i < 8; ++i) fb.blit(*row0[i], i * 9, 0);
  for (int i = 0; i < 8; ++i) fb.blit(*row1[i], i * 9, 8);
  fb.blit(kArrowStraight, 0, 17);
  fb.blit(kArrowLeft, 18, 17);
  fb.blit(kArrowSlightRight, 36, 17);
  fb.blit(kArrowUturn, 54, 17);
  testfw::checkGolden("icons_strip", fb);
}

TESTFW_MAIN
