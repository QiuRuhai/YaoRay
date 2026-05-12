#pragma once

#include <cmath>
#include <exception>
#include <functional>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace yrtest {

using TestFn = void (*)();

struct TestCase {
    std::string_view name;
    TestFn fn;
};

inline std::vector<TestCase>& Registry() {
    static std::vector<TestCase> tests;
    return tests;
}

struct Registrar {
    Registrar(std::string_view name, TestFn fn) {
        Registry().push_back(TestCase{name, fn});
    }
};

inline void Fail(const char* expr, const char* file, int line) {
    std::ostringstream out;
    out << file << ':' << line << ": expectation failed: " << expr;
    throw std::runtime_error(out.str());
}

inline void ExpectNear(double actual, double expected, double eps, const char* expr, const char* file, int line) {
    if (std::fabs(actual - expected) > eps) {
        std::ostringstream out;
        out << file << ':' << line << ": near expectation failed: " << expr
            << " actual=" << actual << " expected=" << expected << " eps=" << eps;
        throw std::runtime_error(out.str());
    }
}

inline int RunAll() {
    int failed = 0;
    for (const auto& test : Registry()) {
        try {
            test.fn();
            std::cout << "[PASS] " << test.name << '\n';
        } catch (const std::exception& e) {
            ++failed;
            std::cerr << "[FAIL] " << test.name << ": " << e.what() << '\n';
        }
    }
    return failed == 0 ? 0 : 1;
}

} // namespace yrtest

#define YR_TEST(name) \
    static void name(); \
    static ::yrtest::Registrar name##_registrar{#name, &name}; \
    static void name()

#define YR_EXPECT_TRUE(expr) \
    do { if (!(expr)) ::yrtest::Fail(#expr, __FILE__, __LINE__); } while (false)

#define YR_EXPECT_EQ(actual, expected) \
    do { if (!((actual) == (expected))) ::yrtest::Fail(#actual " == " #expected, __FILE__, __LINE__); } while (false)

#define YR_EXPECT_NEAR(actual, expected, eps) \
    do { ::yrtest::ExpectNear((actual), (expected), (eps), #actual " ~= " #expected, __FILE__, __LINE__); } while (false)
