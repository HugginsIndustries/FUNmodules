# Contributing

Welcome! This repo contains **FUN** modules by **HugginsIndustries**. The goal: have fun.

---

## TL;DR (Windows + MSYS2)

1. **Install MSYS2** and open a **MINGW64** shell, then:

   ```bash
   pacman -S --needed base-devel make zip unzip mingw-w64-x86_64-toolchain
   ```

2. **One-time environment** (run in **PowerShell**):

   ```powershell
   setx RACK_SDK      "C:\dev\Rack-SDK"
   setx RACK_USER_DIR "C:\Users\YOURNAME\AppData\Local\Rack2"
   setx RACK_EXE      "C:\Program Files\VCV\Rack2Free\Rack.exe"
   ```

3. **Clone and build** (in an **MSYS2 MINGW64** shell):

   ```bash
   git clone https://github.com/HugginsIndustries/FUNmodules.git
   cd FUNmodules
   make -j"$(nproc)"
   make install RACK_USER_DIR="$RACK_USER_DIR"
   ```

4. **Run Rack**:

   ```bash
   "$RACK_EXE"
   ```

Windsurf/VS Code users: **Ctrl+Shift+B** runs the default task **Clean → Build & Install**.
All tasks also work headlessly in the integrated terminal, so assistant tools can trigger them too.

---

## Prerequisites

* **VCV Rack 2 SDK** (unzip to something like `C:\dev\Rack-SDK`)

* **MSYS2** with **MINGW64** toolchain

  ```bash
  pacman -S --needed base-devel make zip unzip mingw-w64-x86_64-toolchain
  ```

* **Git** (MSYS2 includes one)

* **Windsurf / VS Code** (recommended)

### Recommended editor extensions

The repo suggests these in `.vscode/extensions.json`:

* **clangd** – primary C/C++ IntelliSense
* **Makefile Tools** – parses `Makefile` and wires IntelliSense
* **GitLens** – better blame/history
* **DotENV** – highlights `.env` files
* *(optional)* **GitHub Pull Requests** – open/preview PRs in-editor

---

## Environment

The build expects:

* `RACK_SDK` – path to Rack SDK (e.g. `C:\dev\Rack-SDK`)
* `RACK_USER_DIR` – Rack user dir (e.g. `C:\Users\YOU\AppData\Local\Rack2`)
* `RACK_EXE` – Rack executable (e.g. `C:\Program Files\VCV\Rack2Free\Rack.exe`)

> On Windows, we also configure the terminal to use **MSYS2 MINGW64** so `make`, `gcc`, and friends are on `PATH`.

---

## Building & Installing

From an **MSYS2 MINGW64** shell in the repo root:

```bash
make -j"$(nproc)"
make install RACK_USER_DIR="$RACK_USER_DIR"
```

The library (`plugin.dll`) lands in `dist/`, and installing copies it to your Rack user plugins dir.

---

## Tasks (editor & terminal)

These tasks are available both via **Terminal** and **Ctrl+Shift+B**:

| Task (editor label) | Terminal equivalent                           | Notes                                      |
| ------------------- | --------------------------------------------- | ------------------------------------------ |
| **Build**           | `make -j"$(nproc)"`                           | Compiles the plugin                        |
| **Install**         | `make install RACK_USER_DIR="$RACK_USER_DIR"` | Copies into your Rack user dir             |
| **Clean**           | `make clean`                                  | Removes build artifacts                    |
| **Build & Install** | `make -j"$(nproc)" && make install …`         | Default build task                         |
| **Core tests**      | `make core_tests`                             | Prints `All core tests passed.` on success |
| **Quick**           | `make quick`                                  | Fast incremental rebuild (where supported) |

> You can run any of the above directly in the integrated terminal if you prefer (or if an assistant tool needs to).

---

## Running tests

```bash
make core_tests
# Expect: "All core tests passed."
```

Unit tests live under `tests/` and core test hooks reside in `src/core/*`.

---

## Branch & PR workflow

1. Branch from `main`:

   ```bash
   git checkout -b bug/quant-fix
   ```

2. Make small, reviewable commits.

3. Push and open a PR. Include a brief summary and a bullet list of key changes and verification steps.

4. **Recommended merge strategy**: **Squash & merge** (keeps main history tidy; PR number links back to discussion).

5. Tag releases from `main` (e.g. `v2.0.1`) after merging.

---

## Coding guidelines

* C++17 (or repo default), no new heavy deps.
* Keep warnings clean; prefer small, isolated changes.
* Tests for tricky logic (hysteresis, rounding, masks).
* Maintain style and file organization already in the project.

---

## Getting help

If something doesn’t build:

* Confirm you are in **MSYS2 MINGW64** (`uname -s` shows `MINGW64_NT-...`).
* Verify `RACK_SDK`, `RACK_USER_DIR`, and `RACK_EXE` are set correctly.
* Try a clean rebuild: `make clean && make -j"$(nproc)"`.
* Open an issue with the failing task output and your environment details.

Thanks for helping make weird sounds weirder. 🎛️✨