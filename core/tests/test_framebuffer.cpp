#include "phoenix/framebuffer.h"

#include "test_framework.h"

using phoenix::FrameBuffer;
using phoenix::Sprite;

TEST(pixel_roundtrip_and_page_packing) {
  FrameBuffer fb;
  fb.setPixel(0, 0);
  CHECK_EQ(fb.pages()[0], 0x01);  // page 0, column 0, bit 0 = top row
  fb.setPixel(3, 9);
  CHECK_EQ(fb.pages()[1 * 72 + 3], 0x02);  // page 1 (rows 8-15), bit 1 = row 9
  fb.setPixel(71, 39);
  CHECK_EQ(fb.pages()[4 * 72 + 71], 0x80);  // last page, bit 7 = row 39
  CHECK(fb.getPixel(0, 0));
  CHECK(fb.getPixel(3, 9));
  CHECK(fb.getPixel(71, 39));
  CHECK(!fb.getPixel(1, 0));
  fb.setPixel(3, 9, false);
  CHECK(!fb.getPixel(3, 9));
  CHECK_EQ(fb.pages()[1 * 72 + 3], 0x00);
}

TEST(out_of_bounds_is_safe) {
  FrameBuffer fb;
  fb.setPixel(-1, 0);
  fb.setPixel(0, -1);
  fb.setPixel(72, 0);
  fb.setPixel(0, 40);
  fb.setPixel(100000, 100000);
  fb.setPixel(-100000, -100000);
  CHECK(!fb.getPixel(-1, 0));
  CHECK(!fb.getPixel(72, 0));
  FrameBuffer empty;
  CHECK(fb == empty);  // nothing landed in the buffer
}

TEST(clear_fills_buffer) {
  FrameBuffer fb;
  fb.clear(true);
  CHECK(fb.getPixel(0, 0));
  CHECK(fb.getPixel(71, 39));
  fb.clear(false);
  CHECK(!fb.getPixel(35, 20));
}

TEST(fill_rect_clips) {
  FrameBuffer fb;
  fb.fillRect(-5, -5, 10, 10);  // only the 5x5 on-screen corner lands
  int lit = 0;
  for (int y = 0; y < 40; ++y)
    for (int x = 0; x < 72; ++x)
      if (fb.getPixel(x, y)) ++lit;
  CHECK_EQ(lit, 25);
  CHECK(fb.getPixel(0, 0));
  CHECK(fb.getPixel(4, 4));
  CHECK(!fb.getPixel(5, 5));

  FrameBuffer fb2;
  fb2.fillRect(70, 38, 10, 10);  // clipped at bottom-right
  int lit2 = 0;
  for (int y = 0; y < 40; ++y)
    for (int x = 0; x < 72; ++x)
      if (fb2.getPixel(x, y)) ++lit2;
  CHECK_EQ(lit2, 4);

  FrameBuffer fb3;
  fb3.fillRect(10, 10, 0, 5);  // degenerate: nothing drawn
  fb3.fillRect(10, 10, 5, -1);
  CHECK(fb3 == FrameBuffer());
}

TEST(draw_rect_outline_only) {
  FrameBuffer fb;
  fb.drawRect(2, 3, 6, 5);
  CHECK(fb.getPixel(2, 3));
  CHECK(fb.getPixel(7, 3));
  CHECK(fb.getPixel(2, 7));
  CHECK(fb.getPixel(7, 7));
  CHECK(fb.getPixel(4, 3));   // top edge
  CHECK(fb.getPixel(2, 5));   // left edge
  CHECK(!fb.getPixel(4, 5));  // interior stays clear
}

TEST(draw_line_endpoints_and_diagonal) {
  FrameBuffer fb;
  fb.drawLine(0, 0, 10, 0);
  for (int x = 0; x <= 10; ++x) CHECK(fb.getPixel(x, 0));
  fb.drawLine(5, 5, 5, 15);
  for (int y = 5; y <= 15; ++y) CHECK(fb.getPixel(5, y));
  fb.drawLine(20, 20, 30, 30);
  for (int i = 0; i <= 10; ++i) CHECK(fb.getPixel(20 + i, 20 + i));
  // Off-screen endpoints must not crash and must clip.
  fb.drawLine(-10, 5, 80, 5);
  CHECK(fb.getPixel(0, 5));
  CHECK(fb.getPixel(71, 5));
}

TEST(blit_msb_first_and_composes) {
  // 3x3 X shape: rows 101 / 010 / 101, MSB-first in each row byte.
  static const uint8_t data[] = {0xA0, 0x40, 0xA0};
  const Sprite x{3, 3, data};
  FrameBuffer fb;
  fb.setPixel(11, 10);  // pre-existing pixel inside the blit area, sprite bit off
  fb.blit(x, 10, 10);
  CHECK(fb.getPixel(10, 10));
  CHECK(fb.getPixel(11, 10));  // sprite bit is off there: OR-composition keeps it
  CHECK(fb.getPixel(12, 10));
  CHECK(fb.getPixel(11, 11));
  CHECK(!fb.getPixel(10, 11));
  CHECK(fb.getPixel(10, 12));
  CHECK(fb.getPixel(12, 12));
  // Blit partially off-screen: clips safely.
  fb.blit(x, -1, -1);
  fb.blit(x, 71, 39);
  CHECK(fb.getPixel(71, 39));  // sprite (0,0) bit landed; the rest clipped away
}

TEST(equality_and_ascii_dump) {
  FrameBuffer a, b;
  CHECK(a == b);
  a.setPixel(7, 7);
  CHECK(a != b);
  b.setPixel(7, 7);
  CHECK(a == b);

  const std::string art = a.toAscii();
  // 40 rows of 72 chars + newline each.
  CHECK_EQ(art.size(), static_cast<size_t>(40 * 73));
  CHECK_EQ(art[7 * 73 + 7], '#');
  CHECK_EQ(art[0], '.');
}

TESTFW_MAIN
