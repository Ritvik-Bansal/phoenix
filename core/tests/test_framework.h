#pragma once

// Minimal hand-rolled test harness — the core stays zero-dependency, so no
// googletest/catch. TEST(name) registers a case; CHECK/CHECK_EQ record
// failures without aborting the run; TESTFW_MAIN emits main().

#include <cstdio>
#include <sstream>
#include <string>
#include <vector>

namespace testfw {

struct Test {
  const char* name;
  void (*fn)();
};

inline std::vector<Test>& registry() {
  static std::vector<Test> r;
  return r;
}

struct Registrar {
  Registrar(const char* n, void (*f)()) { registry().push_back({n, f}); }
};

inline int g_failures = 0;
inline const char* g_current = "";

inline void fail(const char* file, int line, const std::string& msg) {
  ++g_failures;
  std::printf("FAIL %s at %s:%d\n  %s\n", g_current, file, line, msg.c_str());
}

inline void check(bool ok, const char* expr, const char* file, int line) {
  if (!ok) fail(file, line, std::string("CHECK(") + expr + ")");
}

template <typename A, typename B>
void checkEq(const A& a, const B& b, const char* ea, const char* eb,
             const char* file, int line) {
  if (!(a == b)) {
    std::ostringstream os;
    os << "CHECK_EQ(" << ea << ", " << eb << ")\n  left  = " << a
       << "\n  right = " << b;
    fail(file, line, os.str());
  }
}

inline int runAll() {
  for (const Test& t : registry()) {
    g_current = t.name;
    t.fn();
  }
  if (g_failures == 0) {
    std::printf("OK: %zu tests\n", registry().size());
    return 0;
  }
  std::printf("FAILURES: %d\n", g_failures);
  return 1;
}

}  // namespace testfw

#define TEST(name)                                             \
  static void test_##name();                                   \
  static ::testfw::Registrar reg_##name(#name, &test_##name);  \
  static void test_##name()

#define CHECK(cond) ::testfw::check((cond), #cond, __FILE__, __LINE__)
#define CHECK_EQ(a, b) ::testfw::checkEq((a), (b), #a, #b, __FILE__, __LINE__)

// Like CHECK, but bails out of the test on failure — for preconditions whose
// failure would make the following code unsafe (e.g. indexing).
#define REQUIRE(cond)                                    \
  do {                                                   \
    const bool req_ok_ = static_cast<bool>(cond);        \
    ::testfw::check(req_ok_, #cond, __FILE__, __LINE__); \
    if (!req_ok_) return;                                \
  } while (0)

#define TESTFW_MAIN \
  int main() { return ::testfw::runAll(); }
