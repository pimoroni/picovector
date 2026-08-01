// Minimal assertion harness for the host tests.
//
// No framework: the tests are plain functions, CHECK records a pass or a failure
// with its file and line, and main() prints a summary and sets the exit code.
#pragma once

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace pvtest {

  inline int &passed() { static int n = 0; return n; }
  inline int &failed() { static int n = 0; return n; }
  inline const char *&suite() { static const char *s = ""; return s; }

  inline void record(bool ok, const char *expr, const char *file, int line,
                     const char *detail = nullptr) {
    if(ok) { passed()++; return; }
    failed()++;
    const char *base = strrchr(file, '/');
    printf("  FAIL %s:%d  %s", base ? base + 1 : file, line, expr);
    if(detail && *detail) printf("  [%s]", detail);
    printf("\n");
  }

  inline int summary() {
    printf("\n%d passed, %d failed\n", passed(), failed());
    return failed() ? 1 : 0;
  }

}

#define CHECK(expr) ::pvtest::record((expr), #expr, __FILE__, __LINE__)
#define CHECK_MSG(expr, detail) ::pvtest::record((expr), #expr, __FILE__, __LINE__, detail)

// Each test file provides one of these; main.cpp calls them in order.
#define SUITE(name) \
  void name(); \
  struct name##_reg { name##_reg() { } }; \
  void name()
