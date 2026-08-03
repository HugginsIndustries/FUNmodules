# PolyQuanta: cents-table quantizer + Scala `.scl` support

Revised against the codebase recon. Line references are from that report.

## Goal

Replace the step-index representation with a **sorted cents table plus a period**
as the single internal tuning format. EDO, TET, MOS, and `.scl` become table
*generators* feeding one quantizer path.

**Non-goals:** `.kbm` keyboard mapping, bundling the Scala archive, retuning
oscillators.

## What the recon changed

1. `snapEDO` is **pure and stateless** (`PolyQuantaCore.cpp:41–93`). The
   quantizer refactor can be proven with pointwise tests alone.
2. All hysteresis/latching lives in `PolyQuanta.cpp::process()` — Pre at
   2531–2694, Post at 2739–2854 — and the **Pre latch operates in fractional
   step space** (`fs = yRel * N / period`, thresholds `±0.5 + Hs` steps). That
   block is the real work, and it is in the module, not the core.
3. Mask storage convention is **inconsistent** between lookup and several
   writers. Must be resolved before anything else.
4. The Post rounding path has a **unit bug** that must be preserved through the
   refactor and fixed separately.

Performance is a non-issue: `_nearestAllowedStepRoot` currently does an O(edo)
ring search, so a binary search over a sorted table is strictly faster.

---

## Phase 0 — Resolve the mask convention (blocker)

Today:

| Site | Convention |
|---|---|
| `_isAllowedStepRootRel` (`PolyQuantaCore.cpp:16–24`) | root-relative (`step - rootShift`) |
| `isAllowedStep` (`95–112`) | root-relative (`(pc - root) % N`) |
| `setDeg` (`PolyQuanta.cpp:4726–4730`) | root-relative |
| Degrees submenu (`4813–4820`, `4914–4926`) | **absolute** `mask[i]` |
| MOS preset fill (`4649–4651`) | **absolute** from 0 |
| `buildMaskFromCycle` (`3198–3204`) | **absolute** |

With `root != 0` these disagree; the Degrees UI papers over it with a rotated
display index.

**Do this first:** write a test that, for each root 0..N-1, sets a known scale
through *each* writer and asserts the resulting allowed pitch classes. That
pins down current behavior as a spec before you change the representation.
Then pick root-relative as canonical (it matches the lookup path and the
persisted format) and fix the writers.

Delete `customFollowsRoot` while you're here — no runtime branch reads it
(`PolyQuantaCore.hpp:80`, set at `878`, `2753`, never tested outside
`UNIT_TESTS` parity code).

**Exit criteria:** one convention, tests that assert it per root, no behavior
change for `root == 0`.

---

## Phase 1 — `TuningTable` + generators + pure `snapTable`

```cpp
struct TuningTable {
    double periodCents;           // 1200.0 octave, 1901.955 for 3/1, etc.
    std::vector<double> offsets;  // ascending, offsets[0]==0, all < periodCents
    std::string name;             // .scl description or generated label
};

TuningTable makeEqualTable(int n, double periodCents,
                           const uint8_t* mask, int maskLen, int root);
```

Invariants to assert at generation time (and never again): ascending, first
entry exactly 0, last entry `< periodCents`, `periodCents > 0`, no two entries
within 0.01¢.

`makeEqualTable` covers EDO (`periodCents = 1200`) and TET
(`periodCents = 1200 * tetPeriodOct`). The degree mask feeds the generator
instead of the DSP; mask semantics are unchanged for equal tunings.

```cpp
float snapTable(float volts, const TuningTable& t,
                float boundLimit = 10.f, bool boundToLimit = false,
                int shiftSteps = 0);
```

Quantization:

```
cents      = volts * 1200
periodIdx  = floor(cents / periodCents)
residual   = cents - periodIdx * periodCents        // in [0, periodCents)
idx        = nearest entry to residual, considering wrap to offsets[0]
             of the next period
out        = (periodIdx * periodCents + offsets[idx]) / 1200
```

The wrap case is mandatory: for a residual near `periodCents`, `offsets[0]` of
the next period is often closer than `offsets.back()` of this one. Omitting it
produces a systematic error at every period boundary.

`shiftSteps` currently means "integer EDO steps." On an equal table that is
`shiftSteps` entries; keep that meaning and revisit in Phase 5.

`boundToLimit` must reproduce the existing fallback behavior at
`PolyQuantaCore.cpp:70–88`, including the "no allowed step in range ⇒ clamp to
the bound" case.

Nothing calls `snapTable` yet. Add new `.cpp` files to the main `Makefile` and
`tests/Makefile`.

**Exit criteria:** compiles, existing tests green, `snapTable` referenced only
from tests.

---

## Phase 2 — Pointwise differential harness

Because `snapEDO` is pure, this is straightforward and complete.

Add to the `#ifdef UNIT_TESTS` block in `PolyQuantaCore.cpp`, following the
existing pattern (`506–523`).

Matrix:

- `edo` ∈ {1..120}
- `root` ∈ {0..edo-1}
- mask ∈ {all-on, each MOS preset, ~20 seeded pseudorandom, single-note, empty}
- `periodOct` ∈ {1.0, log2(3/2), log2(3), 0.5}
- `shiftSteps` ∈ {-13, 0, +7}
- `boundToLimit` ∈ {false, true} with `boundLimit` ∈ {10.f, 2.f, 0.5f}

Assert `snapTable(v) == snapEDO(v)` within 1e-9 V.

Sampling: uniform sweeps waste effort mid-step. For each adjacent allowed pair,
test the midpoint and midpoint ± {1e-9, 1e-7, 1e-5}, plus a few hundred seeded
random points per configuration, plus exact voltages at ±10 V and 0.

Properties (assert for both):

- output always in the allowed set
- non-decreasing input ⇒ non-decreasing output
- idempotence: `snap(snap(x)) == snap(x)`
- adding exactly one period to input shifts output by exactly one period

**Exit criteria:** full matrix green.

---

## Phase 3 — Cut over the pure path

Replace `snapEDO`'s body with a `snapTable` call, keeping the signature. Build
the table wherever the config-change detector already fires — the same place
`latchedInit` is reset (`2558–2561`, `2769–2772`) — and cache it alongside the
MOS cache.

Delete the dead `nearestAllowedStep` lambda and its `(void)` silencer
(`PolyQuantaCore.cpp:52–62`).

Measure CPU with 16 channels at 120-EDO before and after. Expect an improvement.

**Exit criteria:** old body gone, all tests green, CPU no worse.

---

## Phase 4 — Move the latch into cents space

This is the hard phase. Nothing here is user-visible; the goal is byte-identical
behavior in a representation that generalizes.

### Pre path (`2531–2694`)

Currently `fs = yRel * N / period` (2569), directional delta `fs - lastFs` in
steps (2590), Schmitt latch `d = fs - latchedStep` against `±0.5 + Hs` (2632–2634).

Rewrite in cents:

- `centsRel = yRel * 1200`
- `latchedCents` replaces `latchedStep` (keep the entry index too, for the
  allowed-set logic)
- Schmitt thresholds become the **midpoints between adjacent table entries**,
  offset by `Hs` cents — on an equal table the midpoint is exactly the `±0.5`
  step boundary, which is what makes this a no-op for existing tunings
- directional delta compares `centsRel - lastCents` against `Hd` in cents

`lastFs[16]` (double) becomes `lastCents[16]` (double). `latchedStep[16]`
becomes `latchedIdx[16]` plus `latchedPeriod[16]`, since an entry index alone no
longer determines pitch.

### Post path (`2739–2854`)

Already in volts against `T_up`/`T_down` from `computeHysteresis` (`191–220`,
`2795–2808`). Convert to cents and replace the `dV = period/N` step-size
assumption (`2785–2789`) with the local interval from the table.

### Preserve the Post unit bug

`2814–2816` computes `diff = yRel*12 - yqRel*12` (semitones) and passes it as
`posWithin` to `pickRoundingTarget`, while Pre passes `diffSteps`. **Reproduce
this exactly.** Fixing it during the refactor makes every behavioral difference
ambiguous. Open a follow-up issue, fix it after Phase 4 lands, with a test that
documents the change.

### Trajectory tests

Stateful, so pointwise is insufficient. Run identical input *sequences* through
old and new and compare full output sequences:

- slow ramp up and down across several periods
- ramp reversing just inside and just outside the stickiness threshold
- seeded random walk, 10k samples
- step change larger than one period
- input parked on a boundary, dithered ±1 LSB
- config change mid-sequence (the `latchedInit` reset path)
- every round mode × {stickiness 0, 5, 20} × {12, 13, 31, 120}-EDO × TET 3/2

**Exit criteria:** trajectories identical, full Phase 2 matrix still green.

---

## Phase 5 — Enable unequal tables

### Stickiness clamping

Current: user value clamped to [0, 20]¢ (`2627`, `2787`, `2585`), then to
`0.4 * stepCents` where `stepCents = 1200 * period / N` (`2628–2630`,
`2785–2789`). The 40% cap only binds above 24-EDO, since below that
`0.4 * stepCents > 20`.

On an unequal table there is no single `stepCents`. Clamp **per boundary**: at
the boundary between entries `i` and `i+1`, effective stickiness must be
strictly less than half of `offsets[i+1] - offsets[i]`, otherwise entries become
unreachable.

For the built-in presets this rarely binds — the otonal scale's tightest
interval is 111.73¢, so half is 55.9¢, well above the 20¢ user ceiling. It
matters for dense imported JI scales, where 20¢ can exceed half of a small
interval. Add a property test: at maximum stickiness, every entry in every table
must be reachable from both directions.

Status line (`4242–4245`) should show the binding constraint, e.g.
`Stickiness 5.0¢ (max 12.4¢ at tightest interval)`.

### Root semantics

For equal tunings root is a step index. On an unequal table, "rotate the table
by N entries" (changes the interval pattern — mode-like) and "transpose by N
cents" (preserves it — key-like) are different operations.

Recommendation: keep entry rotation, matching current mask behavior. Users who
want transposition have the pre-quant offset stage (`2366–2384`).

### `shiftSteps`

Callers pass integer EDO steps. On an unequal table define it as entry offsets,
and audit the four Pre nudge call sites (`2674`, `2677`, `2682`, `2687`) plus
`2644` and `2811`.

### EDO control with an imported table

`resampleMask` (`4284–4294`) is meaningless for an imported scale. Grey out the
EDO/TET controls while a `.scl` table is active — less surprising than silently
discarding the import.

### Persistence

Add a **schema version field** — `coreToJson`/`coreFromJson` (`351–418`) have
none today.

- Generated tables: keep storing `edo`, `tuningMode`, `tetSteps`,
  `tetPeriodOct`, `customMaskGeneric`, `rootNote` and regenerate on load. Old
  patches keep working untouched.
- Imported tables: embed the full offsets list and period. Never store a path as
  the source of truth. A path may be kept purely as a convenience for a manual
  "reload" action.

Note that JSON helpers are compiled out under `UNIT_TESTS` (`349`, `420`), so
round-trip tests need either a separate build target or the guard relaxed.

While here: `dataToJson` writes `qzEnabled*` and `postOctShift*` twice — once in
the module loop (`1514–1522`) and again via `coreToJson` (`372–377`). Harmless
but worth removing.

**Exit criteria:** unequal tables quantize correctly, all properties hold with
non-uniform spacing, pre-refactor patch JSON loads bit-identically.

---

## Phase 6 — `.scl` parser

Standalone, headless, no Rack dependencies, testable from strings.

Format rules:

- lines beginning with `!` are comments, stripped **before** positional logic
- first non-comment line is the description (may be empty — do not skip blanks)
- second is the note count
- then one interval per line
- a value containing `.` is **cents**; otherwise a ratio `n/d`, and a bare
  integer `n` means `n/1`
- unison is implicit and never listed
- **the last entry is the period** — which is why non-octave files work, and
  why you read the period from the file rather than needing a policy for it

Edge cases to test: CRLF, UTF-8 BOM, trailing whitespace, declared count
disagreeing with actual lines (reject with reason), negative cents, descending
scales (reject, documented), near-duplicate entries (dedupe at 0.01¢),
`n/0`, `0/1`, non-numeric garbage, single-entry files, huge numerators, extra
content past the declared count (ignore).

Normalize: everything to cents, last entry becomes `periodCents` and is removed
from the list, prepend 0, sort, dedupe, validate against the `TuningTable`
invariants. **Reject rather than emit a malformed table** — note that a
wrong-sized mask currently degrades silently to chromatic
(`PolyQuantaCore.cpp:22`, `109–110`), which is a confusing failure mode to
inherit.

Golden files: 12-TET major subset, a 19-EDO file, a ratio-based JI scale,
Bohlen-Pierce (`3/1` period), and several malformed files with expected errors.

---

## Phase 7 — UI and built-in unequal presets

- Scale submenu (`4523`) → "Import Scala scale (.scl)…", Rack SDK file dialog
  (same `system::` family as `PanelExport`)
- Success: build table, `useCustomScale = true`, `invalidateMOSCache()`, show
  the description line in the status area
- Failure: surface the parse error; leave the existing tuning intact
- MOS detection is limited to `2 <= N <= 24` (`3220–3222`); decide whether to
  attempt detection on imported tables at all (suggest: no, show the `.scl`
  description instead)

Ship presets so the feature is useful with no files:

| Preset | Period | Entries |
|---|---|---|
| Otonal 8–16 | 2/1 | 1/1 9/8 5/4 11/8 3/2 13/8 7/4 15/8 |
| Utonal 16–8 | 2/1 | 1/1 16/15 8/7 16/13 4/3 16/11 8/5 16/9 |
| Odd harmonics in tritave | 3/1 | 1/1 5/3 7/3 |

Remove the existing "Harmonic Series (16-tone)" and "(32-tone)" entries. Both
are octave-periodic equal divisions already covered by N-EDO (1–120), so
they are redundant regardless of what they're called.

---

## Decisions to make before Phase 5

| Question | Options | Suggested |
|---|---|---|
| Canonical mask convention | root-relative / absolute | root-relative |
| Root on unequal tables | rotate entries / transpose cents | rotate |
| EDO control with import active | grey out / discard import | grey out |
| `shiftSteps` on unequal tables | entry offsets / cents | entry offsets |
| Descending `.scl` files | reject / sort | reject, with message |
| MOS detection on imported tables | attempt / skip | skip |
| Path persistence | embed only / embed + path | embed only |

## Risk register

**Pre latch translation.** The fractional-step Schmitt logic is the most likely
source of behavioral drift. Mitigated by Phase 4's trajectory tests running
against the untouched original.

**Preserved bugs.** The Post `posWithin` unit mismatch and any behavior arising
from the mask convention inconsistency must be reproduced deliberately, then
fixed in separate, individually tested commits.

**Precision.** Use `double` throughout the cents path; convert to `float` only
at output. `lastFs` is already `double` (`769`), so this is consistent with
existing practice.

**Patch compatibility.** Covered by keeping generator inputs in JSON. Add a test
that loads a saved pre-refactor patch and asserts the resulting table matches
old behavior exactly.
