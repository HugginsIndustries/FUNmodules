# If RACK_DIR is not defined when calling the Makefile, default to two directories above
RACK_DIR ?= ../..

# FLAGS will be passed to both the C and C++ compiler
FLAGS +=
CFLAGS +=
CXXFLAGS +=

# Careful about linking to shared libraries, since you can't assume much about the user's environment and library search path.
# Static libraries are fine, but they should be added to this plugin's build system.
LDFLAGS +=

# Add .cpp files to the build (exclude legacy duplicate file)
SOURCES += $(wildcard src/*.cpp)
# Include core subdirectory sources
SOURCES += $(wildcard src/core/*.cpp)
# Include deeper core ui sources (relocated Quantities)
SOURCES += $(wildcard src/core/ui/*.cpp)

# Add files to the ZIP package when running `make dist`
# The compiled plugin and "plugin.json" are automatically added.
DISTRIBUTABLES += res
DISTRIBUTABLES += $(wildcard LICENSE*)
DISTRIBUTABLES += $(wildcard presets)

# Include the Rack plugin Makefile framework
include $(RACK_DIR)/plugin.mk

# ── Convenience targets usable from any terminal (and by Cascade) ─────────────

.PHONY: quick core_tests

# Parallel build (defers to the plugin’s default build)
NPROC ?= $(shell nproc 2>/dev/null || echo 4)
quick:
	@$(MAKE) -j$(NPROC)

# Headless unit tests (same compilation line you use in the VS Code task)
build/core_tests: src/core/Strum.cpp src/core/PolyQuantaCore.cpp src/core/ScaleDefs.cpp tests/main.cpp
	@mkdir -p build
	@g++ -std=c++17 -O2 -Wall -DUNIT_TESTS $^ -Isrc -o $@

core_tests: build/core_tests
	@./build/core_tests

compiledb:
	compiledb -n -o compile_commands.json $(MAKE)