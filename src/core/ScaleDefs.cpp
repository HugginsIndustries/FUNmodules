#include "ScaleDefs.hpp"
/*
 * ScaleDefs.cpp — Defines the immutable 12-EDO and 24-EDO preset scale tables.
 * Index within each array == stable scale ID used throughout code & JSON.
 * Bit 0 corresponds to the root (0 semitones / 0 quarter-tones). For 12-EDO
 * masks, bits 0..11 = semitone degrees. For 24-EDO, bits 0..23 = quarter-tones.
 * Existing comments from original locations are preserved.
 */
namespace hi { namespace music {
// 12-EDO scales (bit 0 = root degree)
const int NUM_SCALES12 = 14;
static const Scale SCALES12[] = {
    {"Chromatic",      0xFFFu},
    // Bit 0 = root (0 st), increasing by semitone up to bit 11 = 11 st
    {"Major (Ionian)", (1u<<0)|(1u<<2)|(1u<<4)|(1u<<5)|(1u<<7)|(1u<<9)|(1u<<11)},
    {"Natural minor",  (1u<<0)|(1u<<2)|(1u<<3)|(1u<<5)|(1u<<7)|(1u<<8)|(1u<<10)},
    {"Harmonic minor", (1u<<0)|(1u<<2)|(1u<<3)|(1u<<5)|(1u<<7)|(1u<<8)|(1u<<11)},
    {"Melodic minor",  (1u<<0)|(1u<<2)|(1u<<3)|(1u<<5)|(1u<<7)|(1u<<9)|(1u<<11)},
    {"Pentatonic maj", (1u<<0)|(1u<<2)|(1u<<4)|(1u<<7)|(1u<<9)},
    {"Pentatonic min", (1u<<0)|(1u<<3)|(1u<<5)|(1u<<7)|(1u<<10)},
    // Common hexatonic blues: 1 b3 4 b5 5 b7 (plus root)
    {"Blues",          (1u<<0)|(1u<<3)|(1u<<5)|(1u<<6)|(1u<<7)|(1u<<10)},
    {"Dorian",         (1u<<0)|(1u<<2)|(1u<<3)|(1u<<5)|(1u<<7)|(1u<<9)|(1u<<10)},
    {"Mixolydian",     (1u<<0)|(1u<<2)|(1u<<4)|(1u<<5)|(1u<<7)|(1u<<9)|(1u<<10)},
    {"Phrygian",       (1u<<0)|(1u<<1)|(1u<<3)|(1u<<5)|(1u<<7)|(1u<<8)|(1u<<10)},
    {"Lydian",         (1u<<0)|(1u<<2)|(1u<<4)|(1u<<6)|(1u<<7)|(1u<<9)|(1u<<11)},
    {"Locrian",        (1u<<0)|(1u<<1)|(1u<<3)|(1u<<5)|(1u<<6)|(1u<<8)|(1u<<10)},
    {"Whole tone",     (1u<<0)|(1u<<2)|(1u<<4)|(1u<<6)|(1u<<8)|(1u<<10)}
};
// 24-EDO preset scales (bit 0 = root; masks are musical approximations)
const int NUM_SCALES24 = 7;
static const Scale SCALES24[] = {
    {"Quarter-tone Major", (
        (1u<<0)  | (1u<<4)  | (1u<<8)  | (1u<<10) | (1u<<14) | (1u<<18) | (1u<<22)
    )},
    {"Chromatic Blues (24)", (
        (1u<<0)  | (1u<<6)  | (1u<<10) | (1u<<12) | (1u<<14) | (1u<<20)
    )},
    {"Quarter-tone Maqam (Rast)", (
        (1u<<0)  | (1u<<4)  | (1u<<7)  | (1u<<10) | (1u<<14) | (1u<<18) | (1u<<21)
    )},
    {"Neutral 3rd Pentatonic (Maj)", (
        (1u<<0)  | (1u<<4)  | (1u<<7)  | (1u<<14) | (1u<<18)
    )},
    {"Neutral 3rd Pentatonic (Min)", (
        (1u<<0)  | (1u<<7)  | (1u<<10) | (1u<<14) | (1u<<20)
    )},
    {"Porcupine", (
        (1u<<0)  | (1u<<3)  | (1u<<6)  | (1u<<10) | (1u<<13) | (1u<<16) | (1u<<20) | (1u<<23)
    )},
    {"Quarter-tone Whole-tone", (
        (1u<<0)  | (1u<<4)  | (1u<<8)  | (1u<<12) | (1u<<16) | (1u<<20)
    )}
};
const Scale* scales12() { return SCALES12; }
const Scale* scales24() { return SCALES24; }
}} // namespace hi::music
