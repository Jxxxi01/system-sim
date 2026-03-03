#pragma once

#include <exception>
#include <functional>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace sim::test {

struct TestCase {
  std::string name;
  std::function<void()> fn;
};

inline std::vector<TestCase>& Registry() {
  static std::vector<TestCase> tests;
  return tests;
}

inline void Register(const std::string& name, std::function<void()> fn) {
  Registry().push_back(TestCase{name, std::move(fn)});
}

inline int RunAll() {
  int failed = 0;
  for (const auto& test : Registry()) {
    try {
      test.fn();
      std::cout << "[PASS] " << test.name << '\n';
    } catch (const std::exception& ex) {
      ++failed;
      std::cout << "[FAIL] " << test.name << ": " << ex.what() << '\n';
    } catch (...) {
      ++failed;
      std::cout << "[FAIL] " << test.name << ": unknown exception\n";
    }
  }
  std::cout << "Tests run: " << Registry().size() << ", failures: " << failed << '\n';
  return failed == 0 ? 0 : 1;
}

inline void ExpectTrue(bool value, const char* expr, const char* file, int line) {
  if (!value) {
    std::ostringstream oss;
    oss << file << ":" << line << " expected true: " << expr;
    throw std::runtime_error(oss.str());
  }
}

template <typename T, typename U>
inline void ExpectEq(const T& lhs, const U& rhs, const char* lhs_expr, const char* rhs_expr,
                     const char* file, int line) {
  if (!(lhs == rhs)) {
    std::ostringstream oss;
    oss << file << ":" << line << " expected equality: " << lhs_expr << " == " << rhs_expr;
    throw std::runtime_error(oss.str());
  }
}

}  // namespace sim::test

#define SIM_TEST(name)                                       \
  void name();                                               \
  namespace {                                                \
  struct name##_registrar {                                  \
    name##_registrar() { sim::test::Register(#name, &name); } \
  } name##_registrar_instance;                               \
  }                                                          \
  void name()

#define SIM_EXPECT_TRUE(expr) sim::test::ExpectTrue((expr), #expr, __FILE__, __LINE__)

#define SIM_EXPECT_EQ(lhs, rhs) \
  sim::test::ExpectEq((lhs), (rhs), #lhs, #rhs, __FILE__, __LINE__)
