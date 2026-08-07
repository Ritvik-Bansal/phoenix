#include <string>

#include "phoenix/font.h"
#include "phoenix/font_data.h"
#include "phoenix/text_sanitize.h"
#include "test_framework.h"

using phoenix::FrameBuffer;
using phoenix::kFallbackChar;
using phoenix::kFontBody;
using phoenix::kFontClock;
using phoenix::measureText;
using phoenix::sanitizeForDisplay;

TEST(measure_proportional_widths) {
  CHECK_EQ(measureText(kFontBody, "").width, 0);
  CHECK_EQ(measureText(kFontBody, "!").width, 1);       // '!' is 1 px wide
  CHECK_EQ(measureText(kFontBody, "!!").width, 3);      // 1 + tracking + 1
  CHECK_EQ(measureText(kFontBody, "A").width, 5);
  CHECK_EQ(measureText(kFontBody, "AB").width, 11);     // 5 + 1 + 5
  CHECK_EQ(measureText(kFontBody, "il").width, 7);      // narrow glyphs: 3 + 1 + 3
  CHECK_EQ(measureText(kFontBody, "A").height, 7);
  CHECK_EQ(measureText(kFontClock, "09:41").width, 48); // 10+1+10+1+4+1+10+1+10
  CHECK_EQ(measureText(kFontClock, "0").height, 16);
}

TEST(advance_includes_tracking) {
  CHECK_EQ(kFontBody.advance('A'), 6);
  CHECK_EQ(kFontBody.advance('!'), 2);
  CHECK_EQ(kFontBody.advance(' '), 4);
}

TEST(unknown_chars_use_fallback_glyph) {
  // Bytes outside the font range measure and render as the fallback box.
  const std::string boxed(1, kFallbackChar);
  CHECK_EQ(measureText(kFontBody, "\x01").width,
           measureText(kFontBody, boxed).width);
  CHECK(!kFontBody.hasGlyph('\x01'));
  CHECK(kFontBody.hasGlyph('A'));
  // Sparse clock font: '/' sits inside its char range but has no glyph.
  CHECK(!kFontClock.hasGlyph('/'));
  CHECK(kFontClock.hasGlyph(':'));
}

TEST(draw_text_returns_width_and_clips) {
  FrameBuffer fb;
  const int w = drawText(fb, kFontBody, 0, 0, "Hi");
  CHECK_EQ(w, measureText(kFontBody, "Hi").width + kFontBody.tracking);
  CHECK(fb.getPixel(0, 0));  // 'H' left stem, top row

  // Drawing past the right edge and at negative coordinates must be safe.
  FrameBuffer fb2;
  drawText(fb2, kFontBody, 60, 35, "WWWWWWWWWW");
  drawText(fb2, kFontBody, -3, -3, "clip");
  CHECK(true);  // reaching here without UB is the assertion (ASan build enforces)
}

TEST(sanitize_passthrough_and_typography) {
  CHECK_EQ(sanitizeForDisplay("plain ASCII 123!"), "plain ASCII 123!");
  CHECK_EQ(sanitizeForDisplay("caf\xC3\xA9"), "cafe");                  // é
  CHECK_EQ(sanitizeForDisplay("\xE2\x80\x99tis"), "'tis");              // ’
  CHECK_EQ(sanitizeForDisplay("\xE2\x80\x9Cq\xE2\x80\x9D"), "\"q\"");   // “q”
  CHECK_EQ(sanitizeForDisplay("a\xE2\x80\x94" "b"), "a-b");             // em dash
  CHECK_EQ(sanitizeForDisplay("wait\xE2\x80\xA6"), "wait...");          // …
  CHECK_EQ(sanitizeForDisplay("stra\xC3\x9F" "e"), "strasse");          // ß
  CHECK_EQ(sanitizeForDisplay("\xC2\xA0"), " ");                        // NBSP
  CHECK_EQ(sanitizeForDisplay("5\xC3\x97" "3"), "5x3");                 // ×
  CHECK_EQ(sanitizeForDisplay("72\xC2\xB0"), "72*");                    // °
}

TEST(sanitize_drops_zero_width_and_controls) {
  CHECK_EQ(sanitizeForDisplay("e\xCC\x81"), "e");     // e + combining acute
  CHECK_EQ(sanitizeForDisplay("a\xE2\x80\x8D" "b"), "ab");  // ZWJ
  CHECK_EQ(sanitizeForDisplay("x\x07y"), "xy");       // BEL dropped
  CHECK_EQ(sanitizeForDisplay("a\tb"), "a b");
  CHECK_EQ(sanitizeForDisplay("a\nb"), "a\nb");       // newline survives
}

TEST(sanitize_emoji_and_garbage_never_crash) {
  const std::string box(1, kFallbackChar);
  CHECK_EQ(sanitizeForDisplay("\xF0\x9F\x94\xA5"), box);        // 🔥 -> one box
  // Fire emoji + skin-tone-style modifier range: modifier vanishes.
  CHECK_EQ(sanitizeForDisplay("\xF0\x9F\x91\x8D\xF0\x9F\x8F\xBB"), box);  // 👍🏻
  CHECK_EQ(sanitizeForDisplay("\xFF\xFE"), box + box);          // invalid leads
  CHECK_EQ(sanitizeForDisplay("\x80"), box);                    // stray continuation
  CHECK_EQ(sanitizeForDisplay("\xC0\xAF"), box + box);          // overlong form
  CHECK_EQ(sanitizeForDisplay("\xED\xA0\x80"), box);            // surrogate half
  CHECK_EQ(sanitizeForDisplay("\xE2\x80"), box);                // truncated at end
  // Torture mix: emoji inside a normal sentence.
  CHECK_EQ(sanitizeForDisplay("ok \xF0\x9F\x91\x8D done"), "ok " + box + " done");
}

TEST(sanitize_streaming_holds_incomplete_tail) {
  // "hé" split between the two bytes of é.
  size_t pending = 0;
  std::string out = sanitizeForDisplay("h\xC3", true, &pending);
  CHECK_EQ(out, "h");
  CHECK_EQ(pending, static_cast<size_t>(1));
  out = sanitizeForDisplay("h\xC3\xA9", true, &pending);
  CHECK_EQ(out, "he");
  CHECK_EQ(pending, static_cast<size_t>(0));
  // Without the flag the truncated tail becomes a visible box.
  out = sanitizeForDisplay("h\xC3");
  CHECK_EQ(out, "h" + std::string(1, kFallbackChar));
}

TESTFW_MAIN
