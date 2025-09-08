#define HI_STRUM_IMPL
#include "Strum.hpp"
#ifndef UNIT_TESTS
#include <rack.hpp> // Rack SDK (rack::random)
using namespace rack;
#endif
/*
 * Strum.cpp — Definitions for relocated strum helpers.
 * Logic and math are copied verbatim from original PolyQuanta.cpp to maintain
 * identical runtime behavior. Comments augmented for clarity.
 */
namespace hi { namespace dsp { namespace strum {
    void assign(float spreadMs, int N, Mode mode, float outDelaySec[16]) {
        float base = (spreadMs <= 0.f) ? 0.f : (spreadMs * 0.001f);
        for (int ch = 0; ch < N && ch < 16; ++ch) {
            float d = 0.f;
            switch (mode) {
                case Mode::Up:     d = base * ch; break;
                case Mode::Down:   d = base * (N - 1 - ch); break;
                case Mode::Random:
#ifndef UNIT_TESTS
                    d = base * rack::random::uniform();
#else
                    d = base * 0.5f; // deterministic fallback (headless)
#endif
                    break;
            }
            outDelaySec[ch] = d;
        }
    }
    void tickStartDelays(float dt, int N, float delaysLeft[16]) {
        for (int ch = 0; ch < N && ch < 16; ++ch) {
            if (delaysLeft[ch] > 0.f) {
                delaysLeft[ch] -= dt; if (delaysLeft[ch] < 0.f) delaysLeft[ch] = 0.f;
            }
        }
    }
}}} // namespace hi::dsp::strum
