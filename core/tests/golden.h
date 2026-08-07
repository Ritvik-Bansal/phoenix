#pragma once

// Golden-frame testing: render a known input, compare the framebuffer's ASCII
// dump against a committed .txt file in core/tests/golden/.
//
//   - On mismatch the test fails and prints BOTH frames side by side (expected
//     left, actual right, '!' marking differing rows) and writes the actual
//     frame next to the golden as <name>.actual.txt for inspection.
//   - PHOENIX_BLESS=1 in the environment rewrites goldens from the current
//     render (used once when authoring; review the ASCII art before commit).

#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "phoenix/framebuffer.h"
#include "test_framework.h"

namespace testfw {

inline std::vector<std::string> splitLines(const std::string& s) {
  std::vector<std::string> lines;
  std::string cur;
  for (char c : s) {
    if (c == '\n') {
      lines.push_back(cur);
      cur.clear();
    } else {
      cur.push_back(c);
    }
  }
  if (!cur.empty()) lines.push_back(cur);
  return lines;
}

inline void printSideBySide(const std::string& expected,
                            const std::string& actual) {
  const auto exp = splitLines(expected);
  const auto act = splitLines(actual);
  const size_t rows = exp.size() > act.size() ? exp.size() : act.size();
  std::printf("  %-72s   %s\n", "--- expected ---", "--- actual ---");
  for (size_t i = 0; i < rows; ++i) {
    const std::string& e = i < exp.size() ? exp[i] : std::string("<missing>");
    const std::string& a = i < act.size() ? act[i] : std::string("<missing>");
    std::printf("%c %-72s | %s\n", e == a ? ' ' : '!', e.c_str(), a.c_str());
  }
}

inline void checkGolden(const char* name, const phoenix::FrameBuffer& fb) {
  const std::string path = std::string(GOLDEN_DIR) + "/" + name + ".txt";
  const std::string actual = fb.toAscii();

  std::string expected;
  bool haveGolden = false;
  {
    std::ifstream in(path);
    if (in) {
      std::ostringstream ss;
      ss << in.rdbuf();
      expected = ss.str();
      haveGolden = true;
    }
  }

  if (std::getenv("PHOENIX_BLESS") != nullptr) {
    if (!haveGolden || expected != actual) {
      std::ofstream out(path);
      out << actual;
      std::printf("blessed golden '%s'\n", name);
    }
    return;
  }

  if (haveGolden && expected == actual) return;

  const std::string actualPath =
      std::string(GOLDEN_DIR) + "/" + name + ".actual.txt";
  std::ofstream(actualPath) << actual;
  if (!haveGolden) {
    fail(__FILE__, __LINE__,
         std::string("missing golden '") + name + "' (actual written to " +
             actualPath + "; review it, then rerun with PHOENIX_BLESS=1)");
    return;
  }
  fail(__FILE__, __LINE__,
       std::string("golden mismatch '") + name + "' (actual written to " +
           actualPath + ")");
  printSideBySide(expected, actual);
}

}  // namespace testfw
