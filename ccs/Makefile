LLVM ?= /opt/homebrew/opt/llvm
OUT  ?= ../build/ccs

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
CCACHE    ?= $(shell command -v ccache 2>/dev/null)
VCC_CC     = $(CCACHE) /usr/bin/clang
# VCC_SAN/VCC_OPT: set by the sanitizer variants (vcc-asan, vcc-tsan below)
VCC_SAN   ?=
VCC_OPT   ?= -O3 -mcpu=apple-m1
VCC_CFLAGS = -std=c11 -O2 -Wall -Wextra -Wno-unused-parameter -D_DARWIN_C_SOURCE -DVCC_INCDIR='"$(abspath vcc/include)"' $(VCC_SAN)
VCC_FAST   = $(VCC_CFLAGS) $(VCC_OPT) -pthread
# -O3 also at link time: the driver passes it to ld, which then folds code
VCC_LDFLAGS = -O3 $(VCC_SAN)
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
	$(VCC_CC) $(VCC_LDFLAGS) -pthread $^ -o $@

$(OUT)/vccd: $(call o,vcc/vccd.c vcc/caps.c $(PP_SRC) $(LEX_SRC) $(AS_SRC))
	$(VCC_CC) $(VCC_LDFLAGS) -pthread $^ -o $@

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

# ---- sanitizer variants of the vcc tools (AGENTS.md, "Sanitizers") ----
# Own OUT (and so own obj/), -O1 -g; the normal build is unchanged.
SAN_COMMON = -O1 -g -fno-omit-frame-pointer -Wno-deprecated-declarations
vcc-asan:
	$(MAKE) vcc OUT=../build/ccs-asan VCC_OPT="-O1 -mcpu=apple-m1" \
		VCC_SAN="$(SAN_COMMON) -fsanitize=address,undefined"
vcc-tsan:
	$(MAKE) vcc OUT=../build/ccs-tsan VCC_OPT="-O1 -mcpu=apple-m1" \
		VCC_SAN="$(SAN_COMMON) -fsanitize=thread"

.PHONY: vcc-asan vcc-tsan

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

# ---- fuzzing (fuzz/; see AGENTS.md) ----
# libFuzzer from Homebrew LLVM when it has one, else the standalone driver
# (fuzz/driver.c: replay + blind mutation) with Apple clang.
FUZZ_LIBFUZZER := $(wildcard $(LLVM)/lib/clang/*/lib/darwin/libclang_rt.fuzzer_osx.a)
ifneq ($(FUZZ_LIBFUZZER),)
FUZZ_CC = $(LLVM)/bin/clang
FUZZ_ENGINE = -fsanitize=fuzzer
FUZZ_DRIVER =
else
FUZZ_CC = /usr/bin/clang
FUZZ_ENGINE =
FUZZ_DRIVER = fuzz/driver.c
endif
FUZZ_CFLAGS = -std=gnu11 -g -O1 -mcpu=apple-m1 -D_DARWIN_C_SOURCE -pthread \
	-fsanitize=address,undefined -fno-sanitize-recover=undefined -fno-omit-frame-pointer \
	-Wall -Wno-unused-parameter
FUZZ_OUT = $(OUT)/fuzz
FUZZ_HDR = vcc/lex.h vcc/intern.h vcc/pp.h vcc/dircache.h

fuzz: $(FUZZ_OUT)/fuzz-lex $(FUZZ_OUT)/fuzz-pp

$(FUZZ_OUT):
	mkdir -p $@

$(FUZZ_OUT)/fuzz-lex: fuzz/fuzz-lex.c fuzz/lexref.c fuzz/fuzz.h $(FUZZ_DRIVER) $(LEX_SRC) $(FUZZ_HDR) | $(FUZZ_OUT)
	$(FUZZ_CC) $(FUZZ_CFLAGS) $(FUZZ_ENGINE) fuzz/fuzz-lex.c fuzz/lexref.c $(FUZZ_DRIVER) $(LEX_SRC) -o $@

$(FUZZ_OUT)/fuzz-pp: fuzz/fuzz-pp.c fuzz/fuzz.h $(FUZZ_DRIVER) vcc/pp.c vcc/dircache.c $(LEX_SRC) $(FUZZ_HDR) | $(FUZZ_OUT)
	$(FUZZ_CC) $(FUZZ_CFLAGS) $(FUZZ_ENGINE) fuzz/fuzz-pp.c $(FUZZ_DRIVER) vcc/pp.c vcc/dircache.c $(LEX_SRC) -o $@

# replay the regression inputs (fuzz/regress/{lex,pp}/): every one must pass
fuzz-regress: fuzz
	$(FUZZ_OUT)/fuzz-lex -runs=0 fuzz/regress/lex
	$(FUZZ_OUT)/fuzz-pp -runs=0 -timeout=10 fuzz/regress/pp

# the differential check (vpp against clang -E -P) on the regression and seed inputs
fuzz-diff: $(OUT)/vpp
	fuzz/pp-diff.py -j 4 fuzz/regress/pp fuzz/seeds/pp

.PHONY: fuzz fuzz-regress fuzz-diff
