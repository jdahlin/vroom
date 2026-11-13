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
OUT  ?= $(if $(filter Linux,$(shell uname -s)),../build/linux/src,../build/src)

all: $(OUT)/vroomd $(OUT)/vroom-cc vcc

$(OUT)/vroomd: ../contrib/phase1/vroomd.cpp include/protocol.h | $(OUT)
	$(LLVM)/bin/clang++ -std=c++17 -O2 -fno-rtti -I$(LLVM)/include $(TOOL_DEFS) \
		-DVROOM_PREFIX_DEFAULT='"$(abspath ../contrib/phase1/pch/vroom-prefix.h)"' $< -o $@ \
		-L$(LLVM)/lib -lclang-cpp -lLLVM -Wl,-rpath,$(LLVM)/lib

$(OUT)/vroom-cc: ../contrib/phase1/vroom-cc.c include/protocol.h | $(OUT)
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
HOST_CPU_A64 =  $(if $(shell grep -qm1 'implementer.*0x61' /proc/cpuinfo && echo y),-mcpu=apple-m1,-mcpu=native)
ifeq ($(shell uname -m),x86_64)
HOST_CPU  ?= -march=native -msse4.2
else
HOST_CPU  ?= $(HOST_CPU_A64)
endif
endif
VCC_CC     = $(CCACHE) $(HOST_CC)
# VCC_SAN/VCC_OPT: set by the sanitizer variants (vcc-asan, vcc-tsan below)
VCC_SAN   ?=
VCC_SANLD ?=
VCC_OPT   ?= -O3 $(HOST_CPU)
# include/: the headers other modules include (common/arena.h, frontend/pp/pp.h, ...)
VCC_INC    = -I$(abspath include)
VCC_CFLAGS = -std=c11 -O2 -Wall -Wextra -Wimplicit-fallthrough -Wno-unused-parameter $(HOST_DEFS) $(VCC_INC) -DVCC_INCDIR='"$(abspath headers)"' $(VCC_SAN)
# LTO: off; the split modules are one TU each (SPLIT_MODS), which gives the
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
VCC_LDFLAGS = -O3 $(VCC_SAN) $(VCC_SANLD) $(VCC_LTO) $(if $(VCC_LTO),$(LTO_LINK))
OBJ        = $(OUT)/obj

vcc: $(OUT)/vcc $(OUT)/vsh/sh $(OUT)/vld $(OUT)/vas $(OUT)/var $(OUT)/vcc-nm $(OUT)/vcc-objcopy $(OUT)/vccd $(OUT)/lextest $(OUT)/vpp $(OUT)/vlink $(OUT)/vasm $(OUT)/virtest $(OUT)/viralloc

o = $(patsubst %.c,$(OBJ)/%.o,$(1))
AS_FILES = arch/a64/as/enc.c platform/darwin/macho.c arch/wasm/as/as.c platform/wasm/wasm.c
as_SRC   = as/as.c arch/a64/as/parse.c arch/x64/as/asm.c arch/x64/as/sys.c arch/x64/as/gpr.c \
           arch/x64/as/x87.c arch/x64/as/sse.c arch/x64/as/avx.c
LEX_SRC = frontend/lex/lex.c common/intern.c
PP_SRC  = frontend/pp/dircache.c driver/ppdriver.c driver/ccsnap.c common/ldhash.c
VCCD_SRC = server/vccd.c server/dstore.c server/caps.c utils/fixdep.c utils/ar.c utils/rsh.c
# Split modules: a list of files each (MOD_SRC, in this order).  By default
# each module is compiled as one TU (obj/unity/MOD.c #includes its files, as
# SQLite's amalgamation), so the code generator sees all of it, also in `make
# self` where there is no LTO, and what the files share stays static (VPRIV in
# the headers).  `make VCC_UNITY=` compiles the files one by one.
SPLIT_MODS = cc pp codegen
# the object writer: the model, the CPU's fixups, the ELF writer (linked with as)
# VIR (SPEC-vir.md): the format and its tools, register allocation (V4); linked into the
# compiler for codegen/vir/ (VCC_VIR)
vir_SRC  = vir/table.c vir/vir.c vir/print.c vir/parse.c vir/verify.c vir/facts.c vir/layout.c
VIRA_SRC = vir/loops.c vir/alloc.c vir/allocchk.c vir/allocdump.c	# (not in the unity TU: own statics)
obj_SRC  = obj/obj.c arch/a64/obj/fixup.c arch/x64/obj/fixup.c platform/linux/elf.c
cc_SRC   = $(wildcard frontend/c/*.c)
# pp: the preprocessor and, for now, the diagnostics engine (it uses ppimpl.h)
pp_SRC   = $(filter-out frontend/pp/dircache.c,$(wildcard frontend/pp/*.c)) $(wildcard frontend/diag/*.c)
codegen_SRC = $(wildcard codegen/*.c codegen/opt/*.c codegen/rec/*.c codegen/flow/*.c arch/a64/codegen/*.c arch/wasm/codegen/*.c codegen/walk/*.c arch/x64/codegen/*.c codegen/vir/*.c)
VCC_UNITY ?= y
SPLIT_SRC = $(foreach m,$(SPLIT_MODS),$($(m)_SRC))
SPLIT_OBJ = $(if $(VCC_UNITY),$(patsubst %,$(OBJ)/unity/%.o,$(SPLIT_MODS)),$(call o,$(SPLIT_SRC)))
PP_OBJ  = $(call o,$(PP_SRC)) $(SPLIT_OBJ) $(call mod,vir) $(call o,$(VIRA_SRC))
# one module's objects: its unity TU, or its files
mod = $(if $(VCC_UNITY),$(OBJ)/unity/$(1).o,$(call o,$($(1)_SRC)))
AS_SRC  = $(AS_FILES) $(as_SRC) $(obj_SRC)
AS_OBJ  = $(call o,$(AS_FILES)) $(call mod,as) $(call mod,obj)
LD_SRC  = ld/ld.c common/ldhash.c ld/objcopy.c ld/wasmld.c platform/darwin/ldmacho.c

$(OBJ)/%.o: %.c | $(OBJ)
	@mkdir -p $(dir $@)
	$(VCC_CC) $(VCC_FAST) $(VCC_LTO) $(XFLAGS) -MMD -MP -c $< -o $@

# rewritten only when the list of files changes
UNITY_C = $(patsubst %,$(OBJ)/unity/%.c,$(SPLIT_MODS) as obj vir)
$(UNITY_C): $(OBJ)/unity/%.c: FORCE
	@mkdir -p $(dir $@)
	@(echo '#define VCC_UNITY 1'; for f in $(abspath $($*_SRC)); do echo "#include \"$$f\""; done) > $@.tmp
	@if cmp -s $@.tmp $@; then rm $@.tmp; else mv $@.tmp $@; fi

$(UNITY_C:.c=.o): %.o: %.c
	$(VCC_CC) $(VCC_FAST) $(VCC_LTO) -MMD -MP -c $< -o $@

FORCE:
.PHONY: FORCE
.SECONDARY: $(UNITY_C)

# lextest is built with -mno-outline (stable code layout for PMC runs); no LTO,
# since -mno-outline is a code generator option the bitcode does not keep
$(OBJ)/no/%.o: %.c | $(OBJ)
	@mkdir -p $(dir $@)
	$(VCC_CC) $(VCC_FAST) -mno-outline -MMD -MP -c $< -o $@

$(OBJ)/ld/ld.o: XFLAGS = -DVLD_MAIN

# the linker without main(), for vccd (links, nm and objcopy run in its worker threads)
VCCD_LD_OBJ = $(OBJ)/ld/ldlib.o $(call o,ld/objcopy.c platform/darwin/ldmacho.c)
$(OBJ)/ld/ldlib.o: ld/ld.c | $(OBJ)
	@mkdir -p $(dir $@)
	$(VCC_CC) $(VCC_FAST) $(VCC_LTO) -MMD -MP -c $< -o $@

$(OUT)/vpp: $(call o,bin/vpp/vpp.c $(LEX_SRC)) $(AS_OBJ) $(PP_OBJ)
	$(VCC_CC) $(VCC_LDFLAGS) -pthread $^ -o $@ $(HOST_LIBS)

$(OUT)/vccd: $(call o,$(VCCD_SRC) $(LEX_SRC)) $(AS_OBJ) $(PP_OBJ) $(VCCD_LD_OBJ)
	$(VCC_CC) $(VCC_LDFLAGS) -pthread $^ -o $@ $(HOST_LIBS)

$(OUT)/lextest: $(patsubst %.c,$(OBJ)/no/%.o,bin/lextest/lextest.c port/darwin/pmc.c $(LEX_SRC))
	$(VCC_CC) -O3 $(VCC_SAN) $(VCC_SANLD) -pthread $^ -o $@

$(OUT)/vasm: $(call o,bin/vasm/vasm.c) $(AS_OBJ)
	$(VCC_CC) $(VCC_LDFLAGS) $^ -o $@

# virtest: VIR's parser, verifier and printer (SPEC-vir.md 16); depends on nothing else
$(OUT)/virtest: $(call o,bin/virtest/virtest.c) $(call mod,vir)
	$(VCC_CC) $(VCC_LDFLAGS) $^ -o $@

# viralloc: VIR register allocation with a synthetic target (PLAN-vir.md V4)
VIRALLOC_SRC = $(VIRA_SRC) vir/synth.c
$(OUT)/viralloc: $(call o,bin/viralloc/viralloc.c $(VIRALLOC_SRC)) $(call mod,vir)
	$(VCC_CC) $(VCC_LDFLAGS) $^ -o $@

# vlink: the linker (ld.c) as a standalone tool; vobjcopy/vnm by argv[0]
$(OUT)/vlink: $(call o,$(LD_SRC))
	$(VCC_CC) $(VCC_LDFLAGS) -pthread $^ -o $@
	ln -sf vlink $(OUT)/vobjcopy
	ln -sf vlink $(OUT)/vnm

$(OUT)/vcc: bin/vcc/shim.c include/protocol.h | $(OUT)
	$(VCC_CC) $(VCC_CFLAGS) $< -o $@

# vsh: make's SHELL; named sh so make still execs simple commands directly
$(OUT)/vsh/sh: bin/vcc/shim.c include/protocol.h | $(OUT)
	mkdir -p $(OUT)/vsh
	$(VCC_CC) $(VCC_CFLAGS) -DVSH $< -o $@

$(OUT)/vld $(OUT)/vas $(OUT)/var $(OUT)/vcc-nm $(OUT)/vcc-objcopy: $(OUT)/vcc
	ln -sf vcc $@

$(OBJ):
	mkdir -p $@ $@/no

-include $(wildcard $(OBJ)/*.d $(OBJ)/*/*.d $(OBJ)/*/*/*.d $(OBJ)/*/*/*/*.d $(OBJ)/*/*/*/*/*.d)

# ---- sanitizer variants of the vcc tools (AGENTS.md, "Sanitizers") ----
# Own OUT (and so own obj/), -O1 -g; the normal build is unchanged.
SAN_COMMON = -O1 -g -fno-omit-frame-pointer -Wno-deprecated-declarations
# ASan's frames are 2-3x larger: the parser's recursion (vcc-stress block-nest-5000)
# gets a 64 MB main stack, as vccd's compile threads get 16 MB in the normal build
ASAN_LD = $(if $(filter Darwin,$(HOST_OS)),-Wl$(comma)-stack_size$(comma)0x4000000)
comma := ,
vcc-asan:
	$(MAKE) vcc OUT=$(OUT)-asan VCC_OPT="-O1 $(HOST_CPU)" VCC_LTO= \
		VCC_SAN="$(SAN_COMMON) -fsanitize=address,undefined" VCC_SANLD="$(ASAN_LD)"
vcc-tsan:
	$(MAKE) vcc OUT=$(OUT)-tsan VCC_OPT="-O1 $(HOST_CPU)" VCC_LTO= \
		VCC_SAN="$(SAN_COMMON) -fsanitize=thread"

.PHONY: vcc-asan vcc-tsan

# self: vcc compiles its own sources (Mach-O) and ld64 links stage 2 into
# $(SELF); self-check builds stage 3 with stage 2 and requires identical objects.
SELF ?= ../build/self
SELF_CC = $(OUT)/vpp
SELF_FLAGS = -c -std=c11 -O2 -D_DARWIN_C_SOURCE $(VCC_INC) -DVCC_INCDIR='"$(abspath headers)"'
SELF_LD = ld -arch arm64 -platform_version macos 14.0 14.0 -syslibroot $$(xcrun --show-sdk-path) -lSystem
SELF_SRC = $(filter-out test/% test/fuzz/%,$(wildcard */*.c */*/*.c */*/*/*.c))
SO = $(SELF)
# objects are named after the path below src/, / -> - (frontend/c/expr.c -> frontend-c-expr.o)
so = $(addprefix $(SO)/,$(subst /,-,$(patsubst %.c,%.o,$(1))))
SELF_AS = $(call so,$(AS_SRC))
SELF_LEX = $(call so,$(LEX_SRC))
SELF_PP = $(call so,$(PP_SRC) $(SPLIT_SRC))

self: $(OUT)/vpp
	mkdir -p $(SELF)
	for f in $(SELF_SRC); do o=$$(echo $$f | sed 's,/,-,g'); $(SELF_CC) $(SELF_FLAGS) $$f -o $(SELF)/$${o%.c}.o || exit 1; done
	$(SELF_CC) $(SELF_FLAGS) -DVLD_MAIN ld/ld.c -o $(SELF)/ld-main.o
	$(SELF_LD) $(call so,bin/vpp/vpp.c) $(SELF_PP) $(SELF_LEX) $(SELF_AS) -o $(SO)/vpp
	$(SELF_LD) $(call so,$(VCCD_SRC) ld/ld.c ld/objcopy.c platform/darwin/ldmacho.c) $(SELF_PP) $(SELF_LEX) $(SELF_AS) -o $(SO)/vccd
	$(SELF_LD) $(call so,bin/vasm/vasm.c) $(SELF_AS) -o $(SO)/vasm
	$(SELF_LD) $(call so,bin/lextest/lextest.c port/darwin/pmc.c) $(SELF_LEX) -o $(SO)/lextest
	$(SELF_LD) $(SO)/ld-main.o $(call so,common/ldhash.c) $(SO)/ld-objcopy.o $(SO)/platform-darwin-ldmacho.o -o $(SO)/vlink
	$(SELF_LD) $(call so,bin/vcc/shim.c) -o $(SO)/vcc

self-check: self
	$(MAKE) self SELF=$(SELF)3 SELF_CC=$(abspath $(SELF))/vpp
	for o in $(SELF)/*.o; do cmp $$o $(SELF)3/$$(basename $$o) || exit 1; done
	@echo "self-check: stage 2 and stage 3 objects identical"

.PHONY: self self-check

# ---- fuzzing (test/fuzz/; see AGENTS.md) ----
# libFuzzer from Homebrew LLVM when it has one, else the standalone driver
# (test/fuzz/driver.c: replay + blind mutation) with Apple clang.
# On Linux: the distro clang's libFuzzer and the host's CPU flags.
FUZZ_LIBFUZZER := $(wildcard $(LLVM)/lib/clang/*/lib/darwin/libclang_rt.fuzzer_osx.a $(LLVM)/lib/clang/*/lib/linux/libclang_rt.fuzzer-$(shell uname -m).a)
ifneq ($(FUZZ_LIBFUZZER),)
FUZZ_CC = $(LLVM)/bin/clang
FUZZ_ENGINE = -fsanitize=fuzzer
FUZZ_DRIVER =
else
FUZZ_CC = $(HOST_CC)
FUZZ_ENGINE =
FUZZ_DRIVER = test/fuzz/driver.c
endif
FUZZ_CFLAGS = -std=gnu11 -g -O1 $(HOST_CPU) $(HOST_DEFS) $(VCC_INC) -pthread \
	-fsanitize=address,undefined -fno-sanitize-recover=undefined -fno-omit-frame-pointer \
	-Wall -Wno-unused-parameter
FUZZ_OUT = $(OUT)/fuzz
FUZZ_HDR = include/frontend/lex/lex.h include/common/intern.h include/frontend/pp/pp.h include/frontend/pp/dircache.h

fuzz: $(FUZZ_OUT)/fuzz-lex $(FUZZ_OUT)/fuzz-pp $(FUZZ_OUT)/fuzz-diag

$(FUZZ_OUT):
	mkdir -p $@

$(FUZZ_OUT)/fuzz-lex: test/fuzz/fuzz-lex.c test/fuzz/lexref.c test/fuzz/fuzz.h $(FUZZ_DRIVER) $(LEX_SRC) $(FUZZ_HDR) | $(FUZZ_OUT)
	$(FUZZ_CC) $(FUZZ_CFLAGS) $(FUZZ_ENGINE) test/fuzz/fuzz-lex.c test/fuzz/lexref.c $(FUZZ_DRIVER) $(LEX_SRC) -o $@

$(FUZZ_OUT)/fuzz-pp: test/fuzz/fuzz-pp.c test/fuzz/fuzz.h $(FUZZ_DRIVER) $(wildcard frontend/pp/*.c frontend/pp/*.h frontend/diag/*.c) $(LEX_SRC) $(FUZZ_HDR) | $(FUZZ_OUT)
	$(FUZZ_CC) $(FUZZ_CFLAGS) $(FUZZ_ENGINE) test/fuzz/fuzz-pp.c $(FUZZ_DRIVER) $(pp_SRC) frontend/pp/dircache.c $(LEX_SRC) -o $@

$(FUZZ_OUT)/fuzz-diag: test/fuzz/fuzz-diag.c test/fuzz/fuzz.h $(FUZZ_DRIVER) $(wildcard frontend/pp/*.c frontend/pp/*.h frontend/diag/*.c) $(LEX_SRC) $(FUZZ_HDR) | $(FUZZ_OUT)
	$(FUZZ_CC) $(FUZZ_CFLAGS) $(FUZZ_ENGINE) test/fuzz/fuzz-diag.c $(FUZZ_DRIVER) $(pp_SRC) frontend/pp/dircache.c $(LEX_SRC) -o $@

# replay the regression inputs (test/fuzz/regress/{lex,pp}/): every one must pass
fuzz-regress: fuzz
	$(FUZZ_OUT)/fuzz-lex -runs=0 test/fuzz/regress/lex
	$(FUZZ_OUT)/fuzz-pp -runs=0 -timeout=10 test/fuzz/regress/pp
	$(FUZZ_OUT)/fuzz-diag -runs=0 test/fuzz/regress/diag

# the differential check (vpp against clang -E -P) on the regression and seed inputs
fuzz-diff: $(OUT)/vpp
	test/fuzz/pp-diff.py -j 4 test/fuzz/regress/pp test/fuzz/seeds/pp

.PHONY: fuzz fuzz-regress fuzz-diff
