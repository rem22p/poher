// A very small test kit: TEST(name) { ... } registers a test, CHECK reports a
// broken expectation and lets the test go on.
#pragma once

#include <cstdio>
#include <vector>

namespace check {

struct Test {
    const char* name;
    void (*run)();
};

inline std::vector<Test>& tests() {
    static std::vector<Test> all;
    return all;
}

inline int& failures() {
    static int count = 0;
    return count;
}

struct Register {
    Register(const char* name, void (*run)()) { tests().push_back({name, run}); }
};

}  // namespace check

#define TEST(name)                                                    \
    static void test_##name();                                        \
    static const check::Register register_##name(#name, test_##name); \
    static void test_##name()

#define CHECK(condition)                                                           \
    do {                                                                           \
        if (!(condition)) {                                                        \
            ++check::failures();                                                   \
            std::printf("  FAILED %s:%d: %s\n", __FILE__, __LINE__, #condition);   \
        }                                                                          \
    } while (false)

// For whole numbers: also prints both values.
#define CHECK_EQ(actual, expected)                                                               \
    do {                                                                                         \
        const long long check_a = static_cast<long long>(actual);                                \
        const long long check_e = static_cast<long long>(expected);                              \
        if (check_a != check_e) {                                                                \
            ++check::failures();                                                                 \
            std::printf("  FAILED %s:%d: %s is %lld, expected %lld\n", __FILE__, __LINE__, #actual, \
                        check_a, check_e);                                                       \
        }                                                                                        \
    } while (false)
