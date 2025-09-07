#pragma once
/*
 * ScaleDefs.hpp — Single source of truth for built‑in 12-EDO and 24-EDO
 * (quarter-tone) scale tables used by PolyQuanta. ARRAY ORDER IS STABLE and
 * MUST NOT CHANGE to preserve JSON/backwards compatibility (scaleIndex
 * persisted values map directly to these indices). Any additions must append
 * at the end only.
 */
namespace hi { namespace music {
struct Scale { const char* name; unsigned int mask; }; // name + bitmask (bit0 = root degree)
extern const int NUM_SCALES12; // count of 12-EDO preset scales
extern const int NUM_SCALES24; // count of 24-EDO preset scales
// Accessors return pointer to first element of internal static arrays (do not modify contents)
const Scale* scales12();
const Scale* scales24();
}} // namespace hi::music
