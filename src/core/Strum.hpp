#pragma once
/*
 * Strum.hpp — Relocated (verbatim) strum timing helpers from PolyQuanta.cpp.
 * Provides deterministic per-channel delay assignments used by PolyQuanta
 * for Up/Down/Random strum ordering and countdown tick logic.
 *
 * Functions are pure and have no side effects beyond writing to provided
 * arrays; behavior must remain bit‑for‑bit identical to the original.
 * Arguments:
 *  spreadMs   : milliseconds between adjacent channels (0 => all zero)
 *  N          : number of active voices (<=16)
 *  mode       : ordering (Up, Down, Random)
 *  outDelaySec[16] : output array of per‑channel delays in SECONDS
 *  tickStartDelays(): decrements per-channel remaining delays in place.
 */
#include <cstddef>
#ifdef UNIT_TESTS
#include <cstdlib>
#endif
namespace hi { namespace dsp { namespace strum {
    enum class Mode { Up = 0, Down = 1, Random = 2 }; // (verbatim enum)
    enum class Type { TimeStretch = 0, StartDelay = 1 }; // retained for context (not used here directly)
    // Assign per-channel delays (seconds) given ms spread, voice count, and mode. (Relocated verbatim)
    void assign(float spreadMs, int N, Mode mode, float outDelaySec[16]);
    // Tick countdown timers for StartDelay type (relocated verbatim)
    void tickStartDelays(float dt, int N, float delaysLeft[16]);
}}} // namespace hi::dsp::strum
