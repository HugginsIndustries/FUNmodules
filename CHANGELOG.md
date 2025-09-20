# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]
### Added
- Extended pq core tests to cover range conditioning, clip limit bounds, glide shaping, and strum deterministic span expectations.
- **Module Template**: Complete template package for creating new VCV Rack 2 modules based on PolyQuanta architecture, including source files and documentation.

### Changed
- **PolyQuanta panel width**: Increased panel width from 12HP to 14HP and repositioned components for improved layout and usability.
- **Core test build is Rack-SDK optional**: `make core_tests` now proxies to a standalone `tests/Makefile`, so headless test builds succeed even when `plugin.mk` is unavailable (Codex/CI containers).
- **PolyQuanta header/implementation separation**: Created PolyQuanta.hpp header file and moved forward declarations and core includes from PolyQuanta.cpp to the header, following C++ best practices for cleaner code organization.

### Fixed
- **PolyQuanta sync randomization precision**: Promoted the clock/multiplication timekeeping to double precision so long-running subdivisions keep firing on schedule.
- **PolyQuanta rounding nudges honor tuning**: Directional/Ceil/Floor rounding now derives the nudge size from the active
  tuning step instead of assuming 12-EDO semitones, keeping non-12 scales aligned.
- **Quantizer limit bounding respects scale masks**: When `boundToLimit` clamps
  voltages at the range edges, the quantizer now searches for the nearest
  allowed degree inside the window before falling back to the boundary,
  preventing masked steps from leaking through at the limits.
- **Strum start-delay timing**: start-delay countdown now ticks once per audio block, keeping subdivision spacing consistent.
- **Directional Snap + Strum stability**: Start-delay now holds only the **output** while the quantizer continues tracking, and strum assignment triggers only on **real target changes** (with a small tolerance, computed from the processed target). Together this removes runaway re-triggers/step-chasing and the rare crash when Directional Snap and Strum are enabled, while preserving pre-patch behavior when strum is disabled.


### Removed
- **Custom scales follow root toggle fully removed**: custom scales now always track the selected root; option and related state have been cleaned up for consistency.

### Deprecated
- _None._

### Security
- _None._

### Docs
- **README.md**: documented the Rack-free core test workflow and the new `tests/Makefile` entry point.


## [v2.0.2] — 2025-09-16 ([diff][v2.0.2-diff])
### Added
- **Per-channel slew enable/disable toggles**: Each channel's slew knob context menu now includes "Slew processing enabled" toggle to bypass slew processing for that channel, even when Global slew add > 0 ms.
- **Batch slew enable/disable actions**: Global Slew knob context menu includes "Batch: Slew enable/disable" submenu with ALL/EVEN/ODD ON/OFF actions for quick channel group management.
- **`scripts/win_msys_build.cmd`**: batch script for building the project on Windows using MSYS2/MINGW64.
- **`scripts/win_msys_core_tests.cmd`**: batch script for running the core unit tests on Windows using MSYS2/MINGW64.
- **Per-channel pre-range scale/offset transforms**: Each channel's **Offset** knob context menu now includes **"Scale (attenuverter)"** (-10.00…10.00×, default 1.00×) and **"Offset (post-scale)"** (−10…+10 V, default 0.00 V) sliders. These multiply then offset the per-channel signal **before** the global **Range** (Clip/Scale + global range offset) stage and **before** quantization, letting you define octave windows per channel (bass/pads/melody) while keeping the global Range as the sole limiter. Settings persist in patch JSON; existing patches are unchanged by default. Supports double-click to reset.
- **EDO/TET multiples workflow**: New **"Select 12-EDO scale"** submenu (visible when EDO is a multiple of 12). Quickly seed a custom scale from standard 12-EDO sets, **without** removing the degree picker—so you can add extra notes immediately.
- **Root & scale persistence across EDO changes**: When **Use custom scale** is enabled, switching EDO now preserves intent:

  * **Root follows pitch** (maps to the nearest degree in the new EDO).
  * **Custom degree mask resamples** to the new grid (e.g., 24→36→48…), retaining selections.
- **Added EDO quick picks**: 27 & 29 EDOs have dedicated quick pick options.

### Changed
- **Refactor: rename "quantize" → "snap" for Offset knob modes & improve UI labels**: Offset knob context menu now uses **"Offset knob snap mode"** wording (previously "Quantize knob") to avoid confusion with the module's quantizer. Options have clearer labels: **"Voltages (±10 V)"**, **"Semitones (EDO/TET accurate)"**, **"Cents (1/1200 V)"**. Internal symbols updated for consistency; behavior and patch compatibility are unchanged.
- **Quantize strength: slider UI**: Replaced the discrete submenu (0/25/50/75/100) with a single **percent slider (0–100 %)** bound to the normalized `quantStrength` (0–1). Matches other context-menu sliders in width/behavior, supports double-click to reset (100 %), and preserves patch compatibility/JSON keys.
- **Degree menu (large EDOs)**: Split menus (halves/thirds) are rebuilt to avoid late-binding captures; all labels are generated with the same logic as the unsplit view for consistent names.
- **Degree labeling**: For multiples of 12, degrees aligned to 12-EDO show exact pitch-class names; near-matches show an approximate label with cents, e.g. `≈A# +3¢`.

### Fixed
- **Clocked randomization: multiplication stuck at 1×**: Fixed a bug where ×2/×3… modes behaved like 1× because ratio changes were keyed to the raw index, repeatedly resetting the schedule on knob jitter. Scheduling now compares actual `(divide, multiply)` values and anchors to the measured clock edge, generating interior pulses at `period/mul` as expected. Division behavior unchanged; no patch compatibility impact.
- **Custom scale, root-relative correctness**: Selecting **degree 1** with a non-C root now yields the **root note** (e.g., F#) instead of C. Fix removes an extra add/subtract of root in the quantizer's EDO snap path.
- **EDO 24→36 mask loss**: Changing from one 12-multiple to another no longer clears the custom scale; selections persist via mask resampling.
- **Degree split submenu crash (>36 EDO)**: Eliminated a dangling-capture bug that could segfault when opening split degree submenus.
- **Degree labels wrong in split menus**: Labels now compute against the current root/EDO at open time, so names stay correct at 48/60/72/… EDO.

### Removed
- **Sync glides across channels** feature: eliminated cross-channel glide synchronization to simplify DSP and reduce complexity. Each channel now handles glides independently (as if sync was always disabled).
- **Glide normalization** feature: removed glide normalization enable toggle, mode selection (Volts-linear, Cent-linear, Step-safe), related DSP branches, JSON serialization, and UI menu entries. Module now uses equal-time glide behavior for all cases (preserves existing default behavior).
- **"Custom scales follow root" toggle**: Custom scales now **always** follow the selected root (the previous toggle caused inconsistent behavior and surprising note choices).

### Deprecated
- _None._

### Security
- _None._

### Docs
- **README.md**: updated PolyQuanta section to reflect changes in feature set and planned features. Added PolyFlux to planned modules.
- **PolyQuanta.cpp**: added comprehensive architectural documentation including detailed file header with @doxygen tags, complete feature overview covering all 8 major subsystems (DSP architecture, quantization, dual-mode controls, polyphony management, strum timing, randomization engine, slew processing, UI/UX design), signal flow diagrams, implementation index with line references, and extensive inline documentation throughout all major code sections. Documentation now serves as both user manual and developer architectural guide.


## [v2.0.1] — 2025-09-09 ([diff][v2.0.1-diff])
### Added
- Headless core tests for quantizer stability:
  - `PeakHold_NoChatter`
  - `DirSnap_NoSkip_AfterFlip`
  - `DirSnap_VeryLowFreq_PeakHold`
- Directional Snap internal state for robust direction detection (`lastFs[]`, `lastDir[]`).

### Changed
- Direction hysteresis for Directional Snap widened to `Hd = max(0.75·Hs, 0.02 steps)`.
- Directional target selection (Directional Snap only) now uses `nextAllowedStep(latched, ±1)` instead of nearest-to-`fs`.  
  *No UI/param changes; Nearest/Up/Down paths unchanged; negligible CPU impact.*

### Fixed
- **Scale leakage eliminated**: root-relative mask enforcement (honors `customFollowsRoot`) prevents chromatic notes when a scale is active.
- **Boundary flicker removed** under slow LFO: center-anchored Schmitt hysteresis around the latched step stabilizes edges.
- Directional Snap crest chatter substantially reduced; a rare <~0.1 Hz “peak jump” may still appear (tracked in a follow-up issue).

### Removed
- **Custom scale follow-root toggle removed**: custom scales now always track the selected root; option and related state have been cleaned up for consistency.

### Deprecated
- _None._

### Security
- _None._

### Docs
- Inline comments clarify the **candidate → target → latch** flow and mask semantics in `PolyQuanta.cpp` / `PolyQuantaCore.cpp`.
- Refresh **README.md**: new nested Contents and expanded **Modules → PolyQuanta** section (highlights, typical uses). Also clarifies quick-start, Windsurf/VS Code tasks, and terminal equivalents.
- Update **CONTRIBUTING.md**: streamlined **Windows/MSYS2** setup, `RACK_DIR`/PATH env notes, “Build & Install” mapping to `make` targets, and troubleshooting. Adds editor setup: **clangd** config (`settings.json`), `compile_commands.json` generation, and recommended extensions.
- Housekeeping: revise **tasks.json** (problemMatcher fix and terminal compatibility), recommend extensions in **.vscode/extensions.json**, and tidy **.gitignore** / **.gitattributes** for build artifacts and binary assets.


## [v2.0.0] — 2025-09-08
### Added
- Glide normalization master enable with modes: Volts-linear, Cent-linear (1 V/oct), Step-safe (EDO/TET period).
- Signal chain position switch (Pre quantize → Slew vs Post Slew → Quantize) for pitch stability during long glides.
- Headless core unit tests (glide normalization ratios, quantizer boundaries, mask parity, rounding & hysteresis).

### Changed
- Core refactor scaffolding: extracted `PanelExport`, `ScaleDefs`, `EdoTetPresets`, and `PolyQuantaCore` into dedicated core/ files for separation of UI and DSP concerns.

### Fixed
- Generic EDO custom mask handling (byte-per-step parity now accurate and matches API).
- (If applicable) Clock multiplier/divider scheduling robustness (edge timing stabilization) — include precise note when commit present.

### Removed
- Legacy standalone pitch-safe toggle in favor of unified normalization enable + mode submenu.
- Offset module (was just a test module)

### Deprecated
- Legacy implicit semitone glide key (`pitchSafe`) retained only for JSON backward compatibility (no longer written).

### Security
- _None._

### Docs
- Introduced this CHANGELOG following Keep a Changelog format. 

[Unreleased]: https://github.com/HugginsIndustries/FUNmodules/compare/v2.0.2...HEAD
[v2.0.2]:     https://github.com/HugginsIndustries/FUNmodules/releases/tag/v2.0.2
[v2.0.2-diff]:https://github.com/HugginsIndustries/FUNmodules/compare/v2.0.1...v2.0.2
[v2.0.1]:     https://github.com/HugginsIndustries/FUNmodules/releases/tag/v2.0.1
[v2.0.1-diff]:https://github.com/HugginsIndustries/FUNmodules/compare/v2.0.0...v2.0.1
[v2.0.0]:     https://github.com/HugginsIndustries/FUNmodules/releases/tag/v2.0.0