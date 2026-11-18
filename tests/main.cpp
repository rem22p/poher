// Runs every test, or only those whose name contains the first argument.
#include <chrono>
#include <cstring>

#include "check.hpp"

int main(int argc, char** argv) {
    const char* filter = argc > 1 ? argv[1] : "";
    int ran = 0;
    for (const check::Test& test : check::tests()) {
        if (std::strstr(test.name, filter) == nullptr) continue;
        const int before = check::failures();
        const auto start = std::chrono::steady_clock::now();
        test.run();
        const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        std::printf("%s %s (%.2f s)\n", check::failures() == before ? "ok    " : "FAILED", test.name, seconds);
        ++ran;
    }
    std::printf("\n%d tests, %d failed checks\n", ran, check::failures());
    return check::failures() == 0 ? 0 : 1;
}
