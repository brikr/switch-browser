# Builds the loader as a static Linux AArch64 binary: natively on an AArch64 Linux host, otherwise
# with a cross toolchain (the result then runs under qemu-aarch64; see scripts/wkc.sh).
#
# Needs Nintendo's OSS package for the WKC headers (LGPL), unpacked by scripts/prepare_firmware.py
# into <firmware version dir>/oss. FIRMWARE selects the version directory; OSS overrides the path.
ifeq ($(shell uname -m),aarch64)
CROSS    ?=
else
CROSS    ?= aarch64-linux-gnu-
endif
CC       := $(CROSS)gcc
CXX      := $(CROSS)g++
NM       := $(CROSS)nm
PYTHON   ?= python3 -I

FIRMWARE ?= ../firmware/22.5.0
OSS      ?= $(abspath $(FIRMWARE)/oss)
# Engine generation: 0 = core_0 (netfront 0.30.0, WebKit 601), 3 = core_3 (netfront 3.30.3, WebKit 613).
CORE     ?= 3
ifeq ($(CORE),0)
WKC_VER  := 0.30.0
else ifeq ($(CORE),3)
WKC_VER  := 3.30.3
else
$(error CORE must be 0 or 3)
endif
WKC      := $(OSS)/WKC_$(WKC_VER)
# The 0.30.0 tree lacks wkcbase.h / wkcconfig.h & co; the 3.30.3 copies are used as a fallback
# (check borrowed structs against the core_0 binary: some differ).
WKC_FALLBACK := $(OSS)/WKC_3.30.3/include

BUILD := build/core$(CORE)
INCLUDES := -Isrc -I$(BUILD) -I$(BUILD)/include -I$(WKC)/include -I$(WKC_FALLBACK) \
            -I$(WKC)/WebKit/WKC -I$(WKC)/WebKit/WKC/helpers -I$(WKC)/WebKit/WKC/webkit

CFLAGS   := -O2 -g -Wall -Wextra -Wno-unused-parameter -DNXB_CORE=$(CORE)
# Client objects are deleted through their concrete type (the WKC interfaces lack virtual dtors).
CXXFLAGS := $(CFLAGS) -std=c++17 -Wno-delete-non-virtual-dtor $(INCLUDES)
LDFLAGS  := -static -pthread
# glibc's static libpthread needs whole-archive, or weak pthread symbols resolve to null.
LDLIBS   := -Wl,--whole-archive -lpthread -Wl,--no-whole-archive -lm
# Subset of a prebuilt aarch64 libc++ (std::__1 internals the engine imports), made by scripts/fetch_deps.sh.
LIBCXX_A ?= ../deps/libcxx_subset.o

CXX_SRCS := $(wildcard src/*.cpp src/shims/*.cpp src/host/*.cpp)
C_SRCS   := $(wildcard src/*.c src/shims/*.c)
ASM_SRCS := $(wildcard src/*.S)
OBJS     := $(patsubst src/%,$(BUILD)/%.o,$(CXX_SRCS) $(C_SRCS) $(ASM_SRCS))
GEN_STUBS := $(BUILD)/generated/stub_clients.h
INC_STAMP := $(BUILD)/include/.stamp
BIN      := build/wkc-loader-core$(CORE)

all: $(BIN)

$(BIN): $(OBJS) $(BUILD)/engine_imports.o
	@test -f $(LIBCXX_A) || { echo "$(LIBCXX_A) missing; run scripts/fetch_deps.sh"; exit 1; }
	$(CXX) $(LDFLAGS) -o $@ $^ $(LIBCXX_A) $(LDLIBS)

# Header shims: JavaScriptCore/JavaScript.h and WKC/wkcconfig.h as the WKC headers expect them.
$(INC_STAMP):
	@test -d $(WKC) || { echo "WKC headers not found at $(WKC); run scripts/prepare_firmware.py $(abspath $(FIRMWARE)) (or set OSS=)"; exit 1; }
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

$(BUILD)/%.S.o: src/%.S
	@mkdir -p $(dir $@)
	$(CC) -c -o $@ $<

$(BUILD)/%.c.o: src/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -MMD -MP -c -o $@ $<

clean:
	rm -rf $(BUILD)

.PHONY: all clean
-include $(OBJS:.o=.d)
