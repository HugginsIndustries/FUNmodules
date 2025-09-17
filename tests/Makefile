# Lightweight core test build that skips Rack SDK dependencies.
CXX ?= g++
CXXFLAGS ?= -std=c++17 -O2 -Wall
CXXFLAGS += -DUNIT_TESTS -I../src -I../src/core
LDFLAGS ?=

SRCS := main.cpp \
	../src/core/PolyQuantaCore.cpp \
	../src/core/ScaleDefs.cpp \
	../src/core/Strum.cpp
OUT := ../build/core_tests

.PHONY: all run clean

all: $(OUT)

run: $(OUT)
	@$(OUT)

$(OUT): $(SRCS)
	@mkdir -p ../build
	@$(CXX) $(CXXFLAGS) $(SRCS) -o $(OUT) $(LDFLAGS)

clean:
	@rm -f $(OUT)
