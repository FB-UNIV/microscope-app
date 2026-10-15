#pragma once

// Shared by every test executable: plain [PASS]/[FAIL] lines, no test
// framework. main() ends with `return summary();`.

#include <cmath>
#include <iostream>
#include <string>

namespace msapp::test {

inline int& failureCount() {
    static int count = 0;
    return count;
}

inline void check(bool condition, const std::string& description) {
    std::cout << (condition ? "  [PASS] " : "  [FAIL] ") << description << "\n";
    if (!condition) ++failureCount();
}

inline bool nearlyEqual(double a, double b, double eps = 1e-9) {
    return std::abs(a - b) < eps;
}

// The exception's message if fn throws Exception; empty if it does not throw.
template <typename Exception, typename Fn>
std::string throwsWith(Fn fn) {
    try {
        fn();
    } catch (const Exception& e) {
        return e.what();
    }
    return {};
}

// Prints the closing line and returns main()'s exit code.
inline int summary() {
    const int failures = failureCount();
    std::cout << "\n== " << (failures == 0 ? "ALL CHECKS PASSED" : std::to_string(failures) + " CHECK(S) FAILED")
              << " ==\n";
    return failures == 0 ? 0 : 1;
}

} // namespace msapp::test
