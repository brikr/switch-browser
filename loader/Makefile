# Cross-builds the loader as a static Linux AArch64 binary (runs under qemu-aarch64-static on x86).
#
# Needs Nintendo's OSS package for the WKC headers (LGPL):
#   OSS = extracted NintendoSwitch_OpenSources<ver>.zip (support.nintendo.com/jp/oss)
CROSS    ?= aarch64-linux-gnu-
CC       := $(CROSS)gcc
CXX      := $(CROSS)g++
NM       := $(CROSS)nm
PYTHON   ?= python3 -I

OSS      ?= $(abspath ../firmware/oss/22.5.0/NintendoSwitch_OpenSources22.5.0)
WKC_VER  ?= 0.30.0
WKC      := $(OSS)/WKC_$(WKC_VER)
# The 0.30.0 tree lacks wkcbase.h / wkcconfig.h; the 3.30.3 copies are generic and are used as a fallback.
WKC_FALLBACK := $(OSS)/WKC_3.30.3/include

BUILD := build
INCLUDES := -Isrc -I$(BUILD) -I$(BUILD)/include -I$(WKC)/include -I$(WKC_FALLBACK) \
            -I$(WKC)/WebKit/WKC -I$(WKC)/WebKit/WKC/helpers -I$(WKC)/WebKit/WKC/webkit

CFLAGS   := -O2 -g -Wall -Wextra -Wno-unused-parameter
# Client objects are deleted through their concrete type (the WKC interfaces lack virtual dtors).
CXXFLAGS := $(CFLAGS) -std=c++17 -Wno-delete-non-virtual-dtor $(INCLUDES)
LDFLAGS  := -static -pthread
# glibc's static libpthread needs whole-archive, or weak pthread symbols resolve to null.
LDLIBS   := -Wl,--whole-archive -lpthread -Wl,--no-whole-archive -lm

CXX_SRCS := $(wildcard src/*.cpp src/shims/*.cpp src/host/*.cpp)
C_SRCS   := $(wildcard src/*.c src/shims/*.c)
OBJS     := $(patsubst src/%,$(BUILD)/%.o,$(CXX_SRCS) $(C_SRCS))
GEN_STUBS := $(BUILD)/generated/stub_clients.h
INC_STAMP := $(BUILD)/include/.stamp
BIN      := $(BUILD)/wkc-loader

all: $(BIN)

$(BIN): $(OBJS) $(BUILD)/engine_imports.o
	$(CXX) $(LDFLAGS) -o $@ $^ $(LDLIBS)

# Header shims: JavaScriptCore/JavaScript.h and WKC/wkcconfig.h as the WKC headers expect them.
$(INC_STAMP):
	@test -d $(WKC) || { echo "WKC headers not found at $(WKC); set OSS=<extracted OSS package>"; exit 1; }
	mkdir -p $(BUILD)/include
	ln -sfn $(OSS)/webkit_$(WKC_VER)/JavaScriptCore/API $(BUILD)/include/JavaScriptCore
	ln -sfn $(WKC_FALLBACK)/wkc $(BUILD)/include/WKC
	touch $@

$(GEN_STUBS): ../scripts/gen_client_stubs.py | $(INC_STAMP)
	$(PYTHON) ../scripts/gen_client_stubs.py --wkc $(WKC) --out $@

# Trampolines for every engine symbol the host objects call (bound at runtime by the loader).
$(BUILD)/engine_imports.S: $(OBJS) ../scripts/gen_engine_imports.py
	$(NM) -u $(OBJS) | $(PYTHON) ../scripts/gen_engine_imports.py > $@

$(BUILD)/engine_imports.o: $(BUILD)/engine_imports.S
	$(CC) -c -o $@ $<

$(BUILD)/%.cpp.o: src/%.cpp | $(GEN_STUBS)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -MMD -MP -c -o $@ $<

$(BUILD)/%.c.o: src/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -MMD -MP -c -o $@ $<

clean:
	rm -rf $(BUILD)

.PHONY: all clean
-include $(OBJS:.o=.d)
