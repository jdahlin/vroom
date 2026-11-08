LLVM ?= /opt/homebrew/opt/llvm
# Linux builds go to their own tree: the checkout may be shared with a Mac
OUT  ?= $(if $(filter Linux,$(shell uname -s)),../build/linux/ccs,../build/ccs)

all: $(OUT)/vroomd $(OUT)/vroom-cc vcc

$(OUT)/vroomd: vroomd.cpp protocol.h | $(OUT)
	$(LLVM)/bin/clang++ -std=c++17 -O2 -fno-rtti -I$(LLVM)/include \
		-DVROOM_PREFIX_DEFAULT='"$(abspath pch/vroom-prefix.h)"' $< -o $@ \
		-L$(LLVM)/lib -lclang-cpp -lLLVM -Wl,-rpath,$(LLVM)/lib

$(OUT)/vroom-cc: vroom-cc.c protocol.h | $(OUT)
	/usr/bin/clang -O2 -Wall $< -o $@

$(OUT):
	mkdir -p $@

clean:
	rm -rf $(OUT)

# ---- vcc: egen verktygskedja (fas 2) ----
# One object per source file, shared between the tools, so `make -j` builds
# them in parallel and an edit recompiles one file.  Header dependencies come
# from -MMD.  ccache is used when installed; disable with `make CCACHE=`.
# Host: macOS (Apple clang, M1) or Linux aarch64 (clang from the distro).
CCACHE    ?= $(shell command -v ccache 2>/dev/null)
HOST_OS   := $(shell uname -s)
ifeq ($(HOST_OS),Darwin)
HOST_CC    = /usr/bin/clang
HOST_DEFS  = -D_DARWIN_C_SOURCE
HOST_LIBS  =
HOST_CPU   = -mcpu=apple-m1
else
HOST_CC    = clang
HOST_DEFS  = -D_GNU_SOURCE
HOST_LIBS  = -lm
# Apple cores (implementer 0x61: Docker Desktop, Asahi, a VM on a Mac) report no
# CPU part, so -mcpu=native falls back to generic; name the core instead.
HOST_CPU  ?= $(if $(shell grep -qm1 'implementer.*0x61' /proc/cpuinfo && echo y),-mcpu=apple-m1,-mcpu=native)
endif
VCC_CC     = $(CCACHE) $(HOST_CC)
VCC_CFLAGS = -std=c11 -O2 -Wall -Wextra -Wno-unused-parameter $(HOST_DEFS) -DVCC_INCDIR='"$(abspath vcc/include)"'
VCC_FAST   = $(VCC_CFLAGS) -O3 $(HOST_CPU) -pthread
# -O3 also at link time: the driver passes it to ld, which then folds code
VCC_LDFLAGS = -O3
OBJ        = $(OUT)/obj

vcc: $(OUT)/vcc $(OUT)/vld $(OUT)/vas $(OUT)/vccd $(OUT)/lextest $(OUT)/vpp $(OUT)/vlink $(OUT)/vasm

o = $(patsubst vcc/%.c,$(OBJ)/%.o,$(1))
AS_SRC  = vcc/as.c vcc/a64.c vcc/elf.c vcc/macho.c
LEX_SRC = vcc/lex.c vcc/intern.c
PP_SRC  = vcc/pp.c vcc/dircache.c vcc/ppdriver.c vcc/cc.c vcc/ccsnap.c vcc/emit.c
LD_SRC  = vcc/ld.c vcc/ldhash.c vcc/objcopy.c

$(OBJ)/%.o: vcc/%.c | $(OBJ)
	$(VCC_CC) $(VCC_FAST) $(XFLAGS) -MMD -MP -c $< -o $@

# lextest is built with -mno-outline (stable code layout for PMC runs)
$(OBJ)/no/%.o: vcc/%.c | $(OBJ)
	$(VCC_CC) $(VCC_FAST) -mno-outline -MMD -MP -c $< -o $@

$(OBJ)/ld.o: XFLAGS = -DVLD_MAIN

$(OUT)/vpp: $(call o,vcc/vpp.c $(PP_SRC) $(LEX_SRC) $(AS_SRC))
	$(VCC_CC) $(VCC_LDFLAGS) -pthread $^ -o $@ $(HOST_LIBS)

$(OUT)/vccd: $(call o,vcc/vccd.c vcc/caps.c $(PP_SRC) $(LEX_SRC) $(AS_SRC))
	$(VCC_CC) $(VCC_LDFLAGS) -pthread $^ -o $@ $(HOST_LIBS)

$(OUT)/lextest: $(patsubst vcc/%.c,$(OBJ)/no/%.o,vcc/lextest.c vcc/pmc.c $(LEX_SRC))
	$(VCC_CC) $(VCC_LDFLAGS) -pthread $^ -o $@

$(OUT)/vasm: $(call o,vcc/vasm.c $(AS_SRC))
	$(VCC_CC) $(VCC_LDFLAGS) $^ -o $@

# vlink: the linker (ld.c) as a standalone tool; vobjcopy/vnm by argv[0]
$(OUT)/vlink: $(call o,$(LD_SRC))
	$(VCC_CC) $(VCC_LDFLAGS) -pthread $^ -o $@
	ln -sf vlink $(OUT)/vobjcopy
	ln -sf vlink $(OUT)/vnm

$(OUT)/vcc: vcc/shim.c protocol.h | $(OUT)
	$(VCC_CC) $(VCC_CFLAGS) $< -o $@

$(OUT)/vld $(OUT)/vas: $(OUT)/vcc
	ln -sf vcc $@

$(OBJ):
	mkdir -p $@ $@/no

-include $(wildcard $(OBJ)/*.d $(OBJ)/no/*.d)

# self: vcc compiles its own sources (Mach-O) and ld64 links stage 2 into
# $(SELF); self-check builds stage 3 with stage 2 and requires identical objects.
SELF ?= ../build/self
SELF_CC = $(OUT)/vpp
SELF_FLAGS = -c -std=c11 -O2 -D_DARWIN_C_SOURCE -DVCC_INCDIR='"$(abspath vcc/include)"'
SELF_LD = ld -arch arm64 -platform_version macos 14.0 14.0 -syslibroot $$(xcrun --show-sdk-path) -lSystem
SELF_SRC = $(wildcard vcc/*.c)
SO = $(SELF)
SELF_AS = $(SO)/as.o $(SO)/a64.o $(SO)/elf.o $(SO)/macho.o
SELF_LEX = $(SO)/lex.o $(SO)/intern.o
SELF_PP = $(SO)/pp.o $(SO)/dircache.o $(SO)/ppdriver.o $(SO)/cc.o $(SO)/ccsnap.o $(SO)/emit.o

self: $(OUT)/vpp
	mkdir -p $(SELF)
	for f in $(SELF_SRC); do $(SELF_CC) $(SELF_FLAGS) $$f -o $(SELF)/$$(basename $$f .c).o || exit 1; done
	$(SELF_CC) $(SELF_FLAGS) -DVLD_MAIN vcc/ld.c -o $(SELF)/ld-main.o
	$(SELF_LD) $(SO)/vpp.o $(SELF_PP) $(SELF_LEX) $(SELF_AS) -o $(SO)/vpp
	$(SELF_LD) $(SO)/vccd.o $(SO)/caps.o $(SELF_PP) $(SELF_LEX) $(SELF_AS) -o $(SO)/vccd
	$(SELF_LD) $(SO)/vasm.o $(SELF_AS) -o $(SO)/vasm
	$(SELF_LD) $(SO)/lextest.o $(SO)/pmc.o $(SELF_LEX) -o $(SO)/lextest
	$(SELF_LD) $(SO)/ld-main.o $(SO)/ldhash.o $(SO)/objcopy.o -o $(SO)/vlink
	$(SELF_LD) $(SO)/shim.o -o $(SO)/vcc

self-check: self
	$(MAKE) self SELF=$(SELF)3 SELF_CC=$(abspath $(SELF))/vpp
	for o in $(SELF)/*.o; do cmp $$o $(SELF)3/$$(basename $$o) || exit 1; done
	@echo "self-check: stage 2 and stage 3 objects identical"

.PHONY: self self-check
