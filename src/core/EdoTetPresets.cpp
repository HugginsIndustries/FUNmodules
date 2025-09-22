#include "EdoTetPresets.hpp"
#include <algorithm> // std::find
#include <cmath>      // std::log2
/*
 * EdoTetPresets.cpp — Definitions for curated EDO and TET preset groups.
 * Moved verbatim from PolyQuanta.cpp (Phase 2D). Do not alter ordering or
 * string contents; they are relied upon by UI and persistence.
 */
namespace hi { namespace music { namespace edo {
// Curated EDO presets grouped by usefulness
static inline const std::vector<int>& _near12_impl() { static const std::vector<int> v{10,14,16}; return v; }
static inline const std::vector<int>& _diatonicFavs_impl() { static const std::vector<int> v{19,31,22,17,13}; return v; }
static inline const std::vector<int>& _microFamilies_impl() { static const std::vector<int> v{18,36,48,72}; return v; }
static inline const std::vector<int>& _jiAccurate_impl() { static const std::vector<int> v{41,53}; return v; }
static inline const std::vector<int>& _extras_impl() { static const std::vector<int> v{11,20,26,34}; return v; }

const std::vector<int>& near12() { return _near12_impl(); }
const std::vector<int>& diatonicFavs() { return _diatonicFavs_impl(); }
const std::vector<int>& microFamilies() { return _microFamilies_impl(); }
const std::vector<int>& jiAccurate() { return _jiAccurate_impl(); }
const std::vector<int>& extras() { return _extras_impl(); }

// Just Intonation EDO systems (octave-based)
static inline const std::vector<int>& _justIntonation_impl() {
    static const std::vector<int> v{
        12,  // 12-EDO for Just Intonation approximations
        24,  // 24-EDO for quarter-tone Just Intonation
        31,  // 31-EDO for accurate Just Intonation
        53,  // 53-EDO for very accurate Just Intonation
        72   // 72-EDO for extended Just Intonation
    }; return v; }
const std::vector<int>& justIntonation() { return _justIntonation_impl(); }

// Historical temperament EDO systems (octave-based)
static inline const std::vector<int>& _historical_impl() {
    static const std::vector<int> v{
        12,  // 12-EDO for historical temperaments
        24,  // 24-EDO for quarter-tone historical temperaments
        19,  // 19-EDO for meantone approximations
        31,  // 31-EDO for accurate meantone
        43,  // 43-EDO for extended meantone
        50   // 50-EDO for very accurate historical temperaments
    }; return v; }
const std::vector<int>& historical() { return _historical_impl(); }

// World Music EDO systems (octave-based)
static inline const std::vector<int>& _worldMusic_impl() {
    static const std::vector<int> v{
        5,   // 5-EDO for pentatonic systems
        7,   // 7-EDO for heptatonic systems
        17,  // 17-EDO for Arabic maqam
        22,  // 22-EDO for Indian shruti
        24,  // 24-EDO for quarter-tone systems
        34,  // 34-EDO for extended Arabic systems
        41   // 41-EDO for very accurate world music systems
    }; return v; }
const std::vector<int>& worldMusic() { return _worldMusic_impl(); }

// Experimental EDO systems (octave-based)
static inline const std::vector<int>& _experimental_impl() {
    static const std::vector<int> v{
        13,  // 13-EDO for Bohlen-Pierce approximations
        16,  // 16-EDO for harmonic series
        21,  // 21-EDO for golden ratio approximations
        26,  // 26-EDO for extended Bohlen-Pierce
        32,  // 32-EDO for extended harmonic series
        41,  // 41-EDO for very accurate experimental systems
        55   // 55-EDO for ultra-accurate experimental systems
    }; return v; }
const std::vector<int>& experimental() { return _experimental_impl(); }

std::vector<int> allRecommended() {
    std::vector<int> all; all.reserve(64);
    auto add=[&](const std::vector<int>& g){ all.insert(all.end(), g.begin(), g.end()); };
    add(near12()); add(diatonicFavs()); add(microFamilies()); add(jiAccurate()); add(extras());
    add(justIntonation()); add(historical()); add(worldMusic()); add(experimental());
    std::vector<int> out; out.reserve(all.size());
    for (int n : all) { if (std::find(out.begin(), out.end(), n) == out.end()) out.push_back(n); }
    return out;
}
}}} // namespace hi::music::edo

namespace hi { namespace music { namespace tets {
static inline const std::vector<Tet>& _carlos_impl() {
    static const std::vector<Tet> v{
        {"Carlos Alpha", 9,  std::log2(3.f/2.f)},
        {"Carlos Beta",  11, std::log2(3.f/2.f)},
        {"Carlos Gamma", 20, std::log2(3.f/2.f)}
    }; return v; }
const std::vector<Tet>& carlos() { return _carlos_impl(); }

// TET Experimental systems (non-octave)
static inline const std::vector<Tet>& _tet_experimental_impl() {
    static const std::vector<Tet> v{
        // Bohlen-Pierce Scale (13-tone equal division of the tritave)
        {"Bohlen-Pierce (13-tone)", 13, std::log2(3.f)},  // 13 equal divisions of 3:1 (tritave)
        
        // Golden Ratio tunings
        {"Golden Ratio (φ-based)", 12, std::log2(1.618f)},   // 12 equal divisions of golden ratio period
        
        // Fibonacci tunings
        {"Fibonacci (5-tone)", 5, std::log2(1.618f)},        // 5 equal divisions of golden ratio period
        {"Fibonacci (8-tone)", 8, std::log2(1.618f)},        // 8 equal divisions of golden ratio period
        
        // Harmonic Series
        {"Harmonic Series (16-tone)", 16, std::log2(2.0f)}, // 16 equal divisions of octave
        {"Harmonic Series (32-tone)", 32, std::log2(2.0f)}  // 32 equal divisions of octave
    }; return v; }
const std::vector<Tet>& tetExperimental() { return _tet_experimental_impl(); }
}}} // namespace hi::music::tets
