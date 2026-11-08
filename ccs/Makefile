# LLVM: Homebrew's on macOS; on Linux the one that `clang` on PATH belongs to
# (the container puts /usr/lib/llvm-NN/bin first).  Its bin/ is where vroomd,
# vroom-cc and vccd find clang and ld.lld on Linux.
ifeq ($(shell uname -s),Darwin)
LLVM ?= /opt/homebrew/opt/llvm
SYS_CC = /usr/bin/clang
else
LLVM ?= $(patsubst %/bin/,%,$(dir $(realpath $(shell command -v clang))))
SYS_CC = clang
TOOL_DEFS = -DVROOM_LLVM_BIN='"$(LLVM)/bin"'
endif
# Linux builds go to their own tree: the checkout may be shared with a Mac
OUT  ?= $(if $(filter Linux,$(shell uname -s)),../build/linux/ccs,../build/ccs)

all: $(OUT)/vroomd $(OUT)/vroom-cc vcc

$(OUT)/vroomd: vroomd.cpp protocol.h | $(OUT)
	$(LLVM)/bin/clang++ -std=c++17 -O2 -fno-rtti -I$(LLVM)/include $(TOOL_DEFS) \
		-DVROOM_PREFIX_DEFAULT='"$(abspath pch/vroom-prefix.h)"' $< -o $@ \
		-L$(LLVM)/lib -lclang-cpp -lLLVM -Wl,-rpath,$(LLVM)/lib

$(OUT)/vroom-cc: vroom-cc.c protocol.h | $(OUT)
	$(SYS_CC) -O2 -Wall $(TOOL_DEFS) $< -o $@

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
HOST_DEFS  = -D_GNU_SOURCE -DVCC_RESDIR='"$(shell $(HOST_CC) -print-resource-dir)/include"' $(TOOL_DEFS)
HOST_LIBS  = -lm
# Apple cores (implementer 0x61: Docker Desktop, Asahi, a VM on a Mac) report no
# CPU part, so -mcpu=native falls back to generic; name the core instead.
HOST_CPU  ?= $(if $(shell grep -qm1 'implementer.*0x61' /proc/cpuinfo && echo y),-mcpu=apple-m1,-mcpu=native)
endif
VCC_CC     = $(CCACHE) $(HOST_CC)
# VCC_SAN/VCC_OPT: set by the sanitizer variants (vcc-asan, vcc-tsan below)
VCC_SAN   ?=
VCC_OPT   ?= -O3 $(HOST_CPU)
VCC_CFLAGS = -std=c11 -O2 -Wall -Wextra -Wno-unused-parameter $(HOST_DEFS) -DVCC_INCDIR='"$(abspath vcc/include)"' $(VCC_SAN)
# LTO: off; the split modules are one TU each (SPLIT_DIRS), which gives the
# inlining LTO would.  `make VCC_LTO=-flto=thin` turns it on (vbench: pp +0.5 %,
# emit -2.5 % instructions, -c as a whole the same).
VCC_LTO   ?=
ifeq ($(HOST_OS),Darwin)
LTO_LINK   = -Wl,-cache_path_lto,$(OBJ)/lto-cache
else
LTO_LINK   = -Wl,--thinlto-cache-dir=$(OBJ)/lto-cache -fuse-ld=lld
LTO_LINK   = -Wl,--thinlto-cache-dir=$(OBJ)/lto-cache
endif
VCC_FAST   = $(VCC_CFLAGS) $(VCC_OPT) -pthread
# -O3 also at link time: the driver passes it to ld, which then folds code
# (and, with LTO, optimises and generates the code itself)
VCC_LDFLAGS = -O3 $(VCC_SAN) $(VCC_LTO) $(if $(VCC_LTO),$(LTO_LINK))
OBJ        = $(OUT)/obj

vcc: $(OUT)/vcc $(OUT)/vsh/sh $(OUT)/vld $(OUT)/vas $(OUT)/var $(OUT)/vccd $(OUT)/lextest $(OUT)/vpp $(OUT)/vlink $(OUT)/vasm

o = $(patsubst vcc/%.c,$(OBJ)/%.o,$(1))
AS_SRC  = vcc/as.c vcc/a64.c vcc/elf.c vcc/macho.c
LEX_SRC = vcc/lex.c vcc/intern.c
PP_SRC  = vcc/dircache.c vcc/ppdriver.c vcc/ccsnap.c vcc/ldhash.c
# Split modules: one directory of files each.  By default each directory is
# compiled as one TU (obj/unity/DIR.c #includes its files, as SQLite's
# amalgamation), so the code generator sees all of it, also in `make self`
# where there is no LTO, and what the files share stays static (VPRIV in the
# headers).  `make VCC_UNITY=` compiles the files one by one.
SPLIT_DIRS = cc pp emit
VCC_UNITY ?= y
SPLIT_SRC = $(foreach d,$(SPLIT_DIRS),$(wildcard vcc/$(d)/*.c))
SPLIT_OBJ = $(if $(VCC_UNITY),$(patsubst %,$(OBJ)/unity/%.o,$(SPLIT_DIRS)),$(call o,$(SPLIT_SRC)))
PP_OBJ  = $(call o,$(PP_SRC)) $(SPLIT_OBJ)
LD_SRC  = vcc/ld.c vcc/ldhash.c vcc/objcopy.c

$(OBJ)/%.o: vcc/%.c | $(OBJ)
	@mkdir -p $(dir $@)
	$(VCC_CC) $(VCC_FAST) $(VCC_LTO) $(XFLAGS) -MMD -MP -c $< -o $@

# rewritten only when the list of files changes
UNITY_C = $(patsubst %,$(OBJ)/unity/%.c,$(SPLIT_DIRS))
$(UNITY_C): $(OBJ)/unity/%.c: FORCE
	@mkdir -p $(dir $@)
	@(echo '#define VCC_UNITY 1'; for f in $(abspath $(wildcard vcc/$*/*.c)); do echo "#include \"$$f\""; done) > $@.tmp
	@if cmp -s $@.tmp $@; then rm $@.tmp; else mv $@.tmp $@; fi

$(UNITY_C:.c=.o): %.o: %.c
	$(VCC_CC) $(VCC_FAST) $(VCC_LTO) -MMD -MP -c $< -o $@

FORCE:
.PHONY: FORCE
.SECONDARY: $(UNITY_C)

# lextest is built with -mno-outline (stable code layout for PMC runs); no LTO,
# since -mno-outline is a code generator option the bitcode does not keep
$(OBJ)/no/%.o: vcc/%.c | $(OBJ)
	$(VCC_CC) $(VCC_FAST) -mno-outline -MMD -MP -c $< -o $@

$(OBJ)/ld.o: XFLAGS = -DVLD_MAIN

$(OUT)/vpp: $(call o,vcc/vpp.c $(LEX_SRC) $(AS_SRC)) $(PP_OBJ)
	$(VCC_CC) $(VCC_LDFLAGS) -pthread $^ -o $@ $(HOST_LIBS)

$(OUT)/vccd: $(call o,vcc/vccd.c vcc/caps.c vcc/fixdep.c vcc/ar.c vcc/rsh.c $(LEX_SRC) $(AS_SRC)) $(PP_OBJ)
	$(VCC_CC) $(VCC_LDFLAGS) -pthread $^ -o $@ $(HOST_LIBS)

$(OUT)/lextest: $(patsubst vcc/%.c,$(OBJ)/no/%.o,vcc/lextest.c vcc/pmc.c $(LEX_SRC))
	$(VCC_CC) -O3 $(VCC_SAN) -pthread $^ -o $@

$(OUT)/vasm: $(call o,vcc/vasm.c $(AS_SRC))
	$(VCC_CC) $(VCC_LDFLAGS) $^ -o $@

# vlink: the linker (ld.c) as a standalone tool; vobjcopy/vnm by argv[0]
$(OUT)/vlink: $(call o,$(LD_SRC))
	$(VCC_CC) $(VCC_LDFLAGS) -pthread $^ -o $@
	ln -sf vlink $(OUT)/vobjcopy
	ln -sf vlink $(OUT)/vnm

$(OUT)/vcc: vcc/shim.c protocol.h | $(OUT)
	$(VCC_CC) $(VCC_CFLAGS) $< -o $@

# vsh: make's SHELL; named sh so make still execs simple commands directly
$(OUT)/vsh/sh: vcc/shim.c protocol.h | $(OUT)
	mkdir -p $(OUT)/vsh
	$(VCC_CC) $(VCC_CFLAGS) -DVSH $< -o $@

$(OUT)/vld $(OUT)/vas $(OUT)/var: $(OUT)/vcc
	ln -sf vcc $@

$(OBJ):
	mkdir -p $@ $@/no

-include $(wildcard $(OBJ)/*.d $(OBJ)/*/*.d)

# ---- sanitizer variants of the vcc tools (AGENTS.md, "Sanitizers") ----
# Own OUT (and so own obj/), -O1 -g; the normal build is unchanged.
SAN_COMMON = -O1 -g -fno-omit-frame-pointer -Wno-deprecated-declarations
vcc-asan:
	$(MAKE) vcc OUT=$(OUT)-asan VCC_OPT="-O1 $(HOST_CPU)" VCC_LTO= \
		VCC_SAN="$(SAN_COMMON) -fsanitize=address,undefined"
vcc-tsan:
	$(MAKE) vcc OUT=$(OUT)-tsan VCC_OPT="-O1 $(HOST_CPU)" VCC_LTO= \
		VCC_SAN="$(SAN_COMMON) -fsanitize=thread"

.PHONY: vcc-asan vcc-tsan

# self: vcc compiles its own sources (Mach-O) and ld64 links stage 2 into
# $(SELF); self-check builds stage 3 with stage 2 and requires identical objects.
SELF ?= ../build/self
SELF_CC = $(OUT)/vpp
SELF_FLAGS = -c -std=c11 -O2 -D_DARWIN_C_SOURCE -DVCC_INCDIR='"$(abspath vcc/include)"'
SELF_LD = ld -arch arm64 -platform_version macos 14.0 14.0 -syslibroot $$(xcrun --show-sdk-path) -lSystem
SELF_SRC = $(wildcard vcc/*.c vcc/*/*.c)
SO = $(SELF)
# objects are named after the path below vcc/, / -> - (cc/expr.c -> cc-expr.o)
so = $(addprefix $(SO)/,$(subst /,-,$(patsubst vcc/%.c,%.o,$(1))))
SELF_AS = $(call so,$(AS_SRC))
SELF_LEX = $(call so,$(LEX_SRC))
SELF_PP = $(call so,$(PP_SRC) $(SPLIT_SRC))

self: $(OUT)/vpp
	mkdir -p $(SELF)
	for f in $(SELF_SRC); do o=$$(echo $${f#vcc/} | sed 's,/,-,g'); $(SELF_CC) $(SELF_FLAGS) $$f -o $(SELF)/$${o%.c}.o || exit 1; done
	$(SELF_CC) $(SELF_FLAGS) -DVLD_MAIN vcc/ld.c -o $(SELF)/ld-main.o
	$(SELF_LD) $(SO)/vpp.o $(SELF_PP) $(SELF_LEX) $(SELF_AS) -o $(SO)/vpp
	$(SELF_LD) $(SO)/vccd.o $(SO)/caps.o $(SO)/fixdep.o $(SO)/ar.o $(SO)/rsh.o $(SELF_PP) $(SELF_LEX) $(SELF_AS) -o $(SO)/vccd
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

$(FUZZ_OUT)/fuzz-pp: fuzz/fuzz-pp.c fuzz/fuzz.h $(FUZZ_DRIVER) $(wildcard vcc/pp/*.c vcc/pp/*.h) vcc/dircache.c $(LEX_SRC) $(FUZZ_HDR) | $(FUZZ_OUT)
	$(FUZZ_CC) $(FUZZ_CFLAGS) $(FUZZ_ENGINE) fuzz/fuzz-pp.c $(FUZZ_DRIVER) $(wildcard vcc/pp/*.c) vcc/dircache.c $(LEX_SRC) -o $@

# replay the regression inputs (fuzz/regress/{lex,pp}/): every one must pass
fuzz-regress: fuzz
	$(FUZZ_OUT)/fuzz-lex -runs=0 fuzz/regress/lex
	$(FUZZ_OUT)/fuzz-pp -runs=0 -timeout=10 fuzz/regress/pp

# the differential check (vpp against clang -E -P) on the regression and seed inputs
fuzz-diff: $(OUT)/vpp
	fuzz/pp-diff.py -j 4 fuzz/regress/pp fuzz/seeds/pp

.PHONY: fuzz fuzz-regress fuzz-diff
