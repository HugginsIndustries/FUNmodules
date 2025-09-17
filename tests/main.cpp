// This runner builds only the core with UNIT_TESTS defined; it does not depend on Rack.
// tests/main.cpp — Headless core unit test runner for PolyQuanta core logic.
// IMPORTANT: This file is NOT part of the Rack plugin build; it is compiled
// only via the dedicated VS Code task with -DUNIT_TESTS. It introduces no
// dependencies on Rack SDK headers. All asserts live in core; this just invokes them.
// UNIT_TESTS is passed via build task; avoid redefining to suppress warnings.
// (If not defined, optionally define here.)
#ifndef UNIT_TESTS
#define UNIT_TESTS
#endif
#include <iostream>
#include "../src/core/PolyQuantaCore.hpp"

int main() {
    // Delegate to the core test harness; returns 0 on success.
    int rc = pqtests::run_core_tests();
    // Emit a single success line (kept minimal for CI/log parsing).
    if (rc == 0) {
        std::cout << "All core tests passed.\n";
    }
    return rc; // Non-zero if any assertion failed (assert aborts earlier normally).
}
