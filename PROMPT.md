Below is a single, ready-to-paste prompt you can drop into a fresh AI chat (e.g., ChatGPT (GPT-5 Thinking)) to help craft **coding prompts for the Cascade AI code platform in Windsurf** in the exact style we’ve been using. I am currently using Claude Sonnet 4 for all AI coding tasks.

---

**HOLD** — wait for me to propose a prompt idea before drafting anything. Do not start until I explicitly say “GO WRITE PROMPT”.

Hello! This is what we’ll be doing.

We’re going to craft **high-quality prompts for the Cascade AI code platform inside Windsurf** to automate changes to my **FUNmodules** project (a **VCV Rack 2 plugin** written in **C++17**, built on **Windows/MSYS2/MINGW64** with `make`, and using **clangd** for IntelliSense via `compile_commands.json`). You’ll generate prompts that Cascade can execute, and you’ll also provide precise, minimal code diffs and verification steps.

## Project context (concise)

* **Repo**: FUNmodules (modules for VCV Rack 2).
* **Repo URL**: https://github.com/HugginsIndustries/FUNmodules
* **Language/tooling**: C++17, MSYS2/MINGW64, `make`, clangd (`compile_commands.json` kept in sync via `make compiledb`).
* **Core patterns/invariants** (keep these intact):

  * Enforce **root-relative scale masks** (no chromatic leakage).
  * Maintain **center-anchored Schmitt hysteresis** for stable step latching (no boundary chatter).
  * **Directional Snap** should step **one allowed degree** from the **latched** step (use the “next allowed step in the current direction” idea), with **modest direction hysteresis** derived from the stickiness setting.
* **Tasks available by label** (Windsurf tasks and terminal-friendly):

  * **“Build”** → `make -j$(nproc)`
  * **“Build (core tests)”** →
    `mkdir -p build && g++ -std=c++17 -O2 -Wall -DUNIT_TESTS src/core/Strum.cpp src/core/PolyQuantaCore.cpp src/core/ScaleDefs.cpp tests/main.cpp -Isrc -o build/core_tests && ./build/core_tests`
* **Test success signal**: The tests are considered **green** only when the output includes exactly: **`All core tests passed.`**

## Your output style (requirements)

* Prefer **“do the work now”**: make reasonable assumptions instead of asking clarifying questions unless truly blocking.
* Keep changes **surgical** and **scoped**. No bulk reformatting or unrelated refactors.
* Provide **unified diffs** only, repo-rooted paths, fenced in \`\`\`diff blocks.
* When instructing to verify, **paste the last 40 lines** of each task’s console output verbatim.
* If Cascade can’t run tasks, include terminal **fallback commands**.
* If something remains as a tiny known edge case, call it out and suggest a follow-up issue, but don’t block the main change.

---

## Template A — EXECUTION MODE — TWO-PHASE GATE (for initial prompts)

Use this to kick off a new change (analyze first, then apply).

````
EXECUTION MODE — TWO-PHASE GATE

HOLD — DO NOT draft code, diffs, or commands until I type exactly: GO PHASE 1
HARD GATE — DO NOT begin Phase 2 unless I reply exactly: APPROVE PHASE 2

PHASE 1 — ANALYSIS ONLY (NO WRITES)

What you may do: read files, reason, and write a concise plan.  
What you may NOT do: no code edits, no diffs, no shell commands, no task runs.  
Do not propose renames, refactors, reformatting, version bumps, or new dependencies.

Goal:  
[[crisp, 1–2 sentence goal]]

Scope:  
Files likely to change: [[paths using repo-rooted, forward-slash POSIX paths]]  
Respect these invariants:
- Root-relative scale masks must remain correct (no chromatic leakage)
- Center-anchored Schmitt hysteresis for latch stability
- Directional Snap = one allowed degree from latched step (directional gating via modest hysteresis)
- Keep code style; add includes only when needed; no mass reformat; preserve indentation/line endings; avoid whitespace churn

Plan of attack:
1. Identify exact locations to touch (file + brief anchor descriptions)  
2. Describe minimal algorithmic/code changes  
3. Note unit tests to add/adjust (names & intent)  
4. Confirm the two tasks to run for validation (task labels)  
5. Provide Touched files (planned): short list

---

PHASE 1 — ANALYSIS COMPLETE  
[[bullet list of findings and decisions]]

End with the single line only: AWAITING_APPROVAL  
Then wait. Do nothing else until I reply exactly: APPROVE PHASE 2

---

PHASE 2 — APPLY PATCH + VERIFY  

Proceed only after I reply exactly: APPROVE PHASE 2

Assume workspace root is the FUNmodules repo (contains Makefile and plugin.json).  
Use POSIX repo-rooted paths (e.g., src/PolyQuanta.cpp) in diffs and file edits.  
No file creates/renames/moves unless I explicitly ask.

1) Unified diffs (repo-rooted), fenced exactly as:

```diff
[[diffs here — complete hunks, no ellipses, minimal changes only]]
```

2) Run Build; paste the last 40 lines of output (only the tail, nothing else).

3) Run Build (core tests); paste the last 40 lines of output.  
   - Report SUCCESS only if the output contains exactly: All core tests passed.

4) If VS Code tasks are unavailable, use the fallbacks:
- Build → `.\scripts\win_msys_build.cmd`
- Build (core tests) → `.\scripts\win_msys_core_tests.cmd`

If everything is green, end with the single line only: READY_FOR_APPROVAL  
If not, provide a tight corrective patch (minimal diffs only) and rerun steps 2–3.

````

---

## Template B — APPROVE AFTER THESE PATCH ADJUSTMENTS (for follow-ups)

Use this when we’re iterating on a landed change with small fixes or test additions.

````

APPROVE AFTER THESE PATCH ADJUSTMENTS

HOLD — DO NOT draft edits or run commands until I type exactly: GO PROPOSE PATCH
TWO-STEP GATE — Propose first; do not apply or run tasks unless I reply exactly: APPLY PATCH

PHASE 1 — PROPOSE PATCH (NO WRITES)

What you may do: reason, show minimal diffs, and a concise justification.
What you may NOT do: no file edits, no shell commands, no task runs.

Assumptions:
- Workspace root is the FUNmodules repo (contains Makefile and plugin.json)
- Use POSIX repo-rooted paths (e.g., src/PolyQuanta.cpp)
- Respect project invariants (root-relative masks correct; center-anchored Schmitt latch; Directional Snap = one allowed degree from latched step; minimal changes; preserve style/whitespace)

Context (what needs changing):
[[brief bullets: observed symptom, which mode/code path, inputs involved]]

Minimal unified diffs:
[[repo-rooted unified diffs — complete hunks, minimal changes only]]

Why these changes:
[[tie to invariants and intended behavior]]
[[note any math/type-safety or boundary conditions]]

Touched files (planned):
[[short list]]

End the propose phase with the single line only: AWAITING_APPLY
Then wait. Do nothing else until I reply exactly: APPLY PATCH

---

PHASE 2 — APPLY + VERIFY
(Proceed only after I reply exactly: APPLY PATCH)

Apply the proposed diffs to the repo (same paths as shown above). No file creates/renames/moves unless I explicitly ask.

1) Run Build; paste the last 40 lines of output (tail only).
2) Run Build (core tests); paste the last 40 lines of output.
   - Report SUCCESS only if the output contains exactly: All core tests passed.

If VS Code tasks are unavailable, use the fallbacks:
- Build → .\scripts\win_msys_build.cmd
- Build (core tests) → .\scripts\win_msys_core_tests.cmd

If everything is green, end with the single line only: READY_FOR_APPROVAL
If not, provide a tight corrective patch (minimal diffs only) and rerun steps 1–2.

````

---

## What I expect from you in each session

* Use the appropriate template (initial vs follow-up).
* Keep diffs compact and focused on the requested change.
* Paste verification outputs exactly as requested (last 40 lines).
* If you must choose among interpretations, make a sensible call, document it briefly, and continue.
* If a tradeoff is needed, pick a sensible default and note it briefly.
* Preserve project invariants: root-relative mask correctness; center-anchored latch hysteresis; Directional Snap = one step from the latched degree with modest direction hysteresis.

That’s the framework—let’s craft the first Cascade prompt using Template A or B, depending on the task.