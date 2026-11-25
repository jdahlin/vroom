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
HOST_ARCH := $(shell uname -m)
HOST_OS   := $(shell uname -s)
ifeq ($(HOST_OS),Darwin)
HOST_CC    = /usr/bin/clang
HOST_CXX   = /usr/bin/clang++
HOST_DEFS  = -D_DARWIN_C_SOURCE
HOST_LIBS  =
HOST_CPU   = -mcpu=apple-m1
else
HOST_CC    = clang
HOST_CXX   = clang++
HOST_RESOURCE := $(shell $(HOST_CC) -print-resource-dir)/include
HOST_DEFS  = -D_GNU_SOURCE -DVCC_RESDIR='"$(HOST_RESOURCE)"' $(TOOL_DEFS)
HOST_LIBS  = -lm
# Apple cores (implementer 0x61: Docker Desktop, Asahi, a VM on a Mac) report no
# CPU part, so -mcpu=native falls back to generic; name the core instead.
HOST_CPU_A64 =  $(if $(shell grep -qm1 'implementer.*0x61' /proc/cpuinfo && echo y),-mcpu=apple-m1,-mcpu=native)
ifeq ($(HOST_ARCH),x86_64)
# CRC32C and AVX lexer paths dispatch at runtime.
# Override HOST_CPU=-march=native only for a machine-local build.
HOST_CPU  ?= -march=x86-64 -mtune=generic
else
HOST_CPU  ?= $(HOST_CPU_A64)
endif
endif
VCC_CXX_DIALECT = -x c++ -std=gnu++20 -fno-exceptions
VCC_LANG ?= c++
ifeq ($(VCC_LANG),c++)
VCC_CC = $(CCACHE) $(HOST_CXX)
VCC_DIALECT = $(VCC_CXX_DIALECT)
# The sources are C that also compiles as C++: {0} and partial designated
# initializers, array designators, VLAs, string literals as char *, mixed
# anonymous enums.  Those idioms are deliberate; keep C++ from warning on them.
VCC_DIALECT_WARN = -Wno-missing-field-initializers -Wno-missing-braces \
	-Wno-c99-designator -Wno-vla-cxx-extension -Wno-writable-strings \
	-Wno-deprecated-anon-enum-enum-conversion
# The host tools use the C runtime. Let the C driver select the link libraries.
VCC_LINK = $(CCACHE) $(HOST_CC)
else ifeq ($(VCC_LANG),c)
VCC_CC = $(CCACHE) $(HOST_CC)
VCC_DIALECT = -x c -std=c11
VCC_DIALECT_WARN = -Wno-missing-field-initializers -Wno-missing-braces
VCC_LINK = $(VCC_CC)
else
$(error VCC_LANG must be c or c++)
endif
# VCC_SAN/VCC_OPT: set by the sanitizer variants (vcc-asan, vcc-tsan below)
VCC_SAN   ?=
VCC_SANLD ?=
VCC_OPT   ?= -O3 $(HOST_CPU)
# include/: the headers other modules include (common/region.h, frontend/pp/pp.h, ...)
VCC_INC    = -I$(abspath include) -I$(abspath $(OUT)/generated/include)
# Warnings are errors; `make VCC_WERROR=` for another compiler or version.
VCC_WERROR ?= -Werror
# Warnings beyond -Wall -Wextra, for every module.  Added one at a time with
# the fixes each asked for; `make VCC_WARN=` for a compiler without them.
VCC_WARN ?= -Wmissing-prototypes -Wunreachable-code -Wnull-dereference -Wredundant-decls -Wpointer-arith -Wextra-semi -Wcovered-switch-default -Wduplicate-enum -Wloop-analysis
VCC_CFLAGS = $(if $(VCC_WASM),-DVCC_WASM) -O2 -Wall -Wextra -Wimplicit-fallthrough -Wno-unused-parameter $(VCC_WARN) $(VCC_DIALECT_WARN) $(VCC_WERROR) $(HOST_DEFS) $(VCC_INC) -DVCC_INCDIR='"$(abspath headers)"' $(VCC_SAN)
# LTO: off; the split modules are one TU each (SPLIT_MODS), which gives the
# inlining LTO would.  `make VCC_LTO=-flto=thin` turns it on (vbench: pp +0.5 %,
# emit -2.5 % instructions, -c as a whole the same).
# For an instrumented host build use VCC_PGO=-fprofile-instr-generate;
# after training use -fprofile-instr-use=/absolute/path/to/merged.profdata.
# Use a separate OUT for each set of flags.
VCC_PGO   ?=
VCC_LTO   ?=
ifeq ($(HOST_OS),Darwin)
LTO_LINK   = -Wl,-cache_path_lto,$(OBJ)/lto-cache
else
LTO_LINK   = -Wl,--thinlto-cache-dir=$(OBJ)/lto-cache -fuse-ld=lld
endif
VCC_FAST   = $(VCC_CFLAGS) $(VCC_OPT) $(VCC_PGO) -pthread
# -O3 also at link time: the driver passes it to ld, which then folds code
# (and, with LTO, optimises and generates the code itself)
VCC_LDFLAGS = -O3 $(VCC_SAN) $(VCC_SANLD) $(VCC_PGO) $(VCC_LTO) $(if $(VCC_LTO),$(LTO_LINK))
OBJ        = $(OUT)/obj

# Build the query frontend from the compiler sources with occurrence recording.
# A separate object directory keeps production layouts and hot paths identical.
.PHONY: vref
vref: $(OUT)/vref
	$(MAKE) OUT=$(OUT)/vref-lib VCC_OPT='$(VCC_OPT) -DVCC_REFACTOR' $(OUT)/vref-lib/vpp

$(OUT)/vref: bin/vref/vref.py | $(OUT)
	cp $< $@
	chmod +x $@

vcc: $(OUT)/vcc $(OUT)/vsh/sh $(OUT)/vld $(OUT)/vas $(OUT)/var $(OUT)/vcc-nm $(OUT)/vcc-objcopy $(OUT)/vccd $(OUT)/lextest $(OUT)/vpp $(OUT)/vlink $(OUT)/vasm $(OUT)/virtest $(OUT)/viralloc $(OUT)/task-test

o = $(patsubst %.cpp,$(OBJ)/%.o,$(1))
# WebAssembly is off by default (disabled 2026-11-13, doc/PLAN-wasm.md): its
# sources are not built and a wasm target is an error.  `make VCC_WASM=1` builds it.
VCC_WASM ?=
AS_FILES = arch/a64/as/enc.cpp platform/darwin/macho.cpp $(if $(VCC_WASM),arch/wasm/as/as.cpp platform/wasm/wasm.cpp)
as_SRC   = as/as.cpp as/symbol.cpp as/expression.cpp as/section.cpp as/data-directives.cpp as/macro.cpp as/conditional.cpp as/call-frame-information.cpp as/variable-items.cpp \
           as/dwarf-line-table.cpp as/coff-directives.cpp platform/darwin/apple-dialect-translate.cpp arch/a64/as/parse.cpp arch/x64/as/asm.cpp arch/x64/as/sys.cpp arch/x64/as/gpr.cpp \
           arch/x64/as/x87.cpp arch/x64/as/sse.cpp arch/x64/as/avx.cpp
# the shared library (doc/quality/WORK-common.md): every tool links all of both lists,
# so a file moves between them and the tools without touching a tool's rule
common_SRC = common/archive-reader.cpp common/fail.cpp common/response-files.cpp common/intern.cpp common/digest-xxh3.cpp common/digest-blake3.cpp common/digest-sha256.cpp
port_SRC   = port/crc32c.cpp port/stack-guard.cpp port/job-regions.cpp port/task.cpp
LIB_SRC    = $(common_SRC) $(port_SRC)
LEX_ASM = $(if $(filter x86_64,$(HOST_ARCH)),frontend/lex/x86-simd.S)
LEX_OBJECT = $(patsubst %.S,$(OBJ)/%.o,$(LEX_ASM))
LEX_SRC = frontend/lex/lex.cpp frontend/c/keywords.cpp
PP_SRC  = frontend/pp/dircache.cpp driver/declaration-cache.cpp
VCCD_SRC = server/diagnostics-store.cpp server/capabilities.cpp utils/fixdep.cpp utils/ar.cpp utils/shell.cpp
# the server proper (vccd.cpp has main), one TU like the split modules; diagnostics-store.cpp and capabilities.cpp stay apart
server_SRC = $(filter-out server/diagnostics-store.cpp server/capabilities.cpp,$(wildcard server/*.cpp))
# Split modules: a list of files each (MOD_SRC, in this order).  By default
# each module is compiled as one TU (obj/unity/MOD.cpp #includes its files, as
# SQLite's amalgamation), so the code generator sees all of it, also in `make
# self` where there is no LTO, and what the files share stays static (VPRIV in
# the headers).  `make VCC_UNITY=` compiles the files one by one.
SPLIT_MODS = cc pp codegen driver
# the object writer: the model, the CPU's fixups, the ELF writer (linked with as)
# VIR (SPEC-vir.md): the format and its tools, register allocation (V4); linked into the
# compiler for codegen/vir/ (VIR, -O2 and up)
vir_SRC  = vir/opcode-table.cpp vir/vir.cpp vir/print.cpp vir/parse.cpp vir/verify.cpp vir/facts.cpp vir/layout.cpp vir/compact.cpp vir/pass-list.cpp $(wildcard vir/pass/*.cpp)
VIRA_SRC = vir/loops.cpp vir/register-allocation.cpp vir/register-allocation-check.cpp vir/register-allocation-print.cpp	# (not in the unity TU: own statics)
obj_SRC  = obj/obj.cpp arch/a64/obj/fixup.cpp arch/x64/obj/fixup.cpp platform/linux/elf.cpp platform/win/coff.cpp platform/win/unwind.cpp
cc_SRC   = $(filter-out frontend/c/keywords.cpp frontend/c/replay.cpp,$(wildcard frontend/c/*.cpp)) frontend/c/replay.cpp
# the compile job (driver_job_run); declaration-cache.cpp stays a TU of its own
driver_SRC = $(filter-out driver/declaration-cache.cpp,$(wildcard driver/*.cpp))
# pp: the preprocessor and, for now, the diagnostics engine (it uses pp-internal.h)
pp_SRC   = $(filter-out frontend/pp/dircache.cpp,$(wildcard frontend/pp/*.cpp)) $(wildcard diag/*.cpp)
codegen_SRC = $(wildcard codegen/*.cpp codegen/opt/*.cpp codegen/rec/*.cpp codegen/flow/*.cpp arch/a64/codegen/*.cpp arch/a64/vir/*.cpp $(if $(VCC_WASM),arch/wasm/codegen/*.cpp) codegen/walk/*.cpp arch/x64/codegen/*.cpp arch/x64/vir/*.cpp codegen/vir/*.cpp)
VCC_UNITY ?= y
SPLIT_SRC = $(foreach m,$(SPLIT_MODS),$($(m)_SRC))
SPLIT_OBJ = $(if $(VCC_UNITY),$(patsubst %,$(OBJ)/unity/%.o,$(SPLIT_MODS)),$(call o,$(SPLIT_SRC)))
PP_OBJ  = $(call o,$(PP_SRC)) $(SPLIT_OBJ) $(call mod,vir) $(call o,$(VIRA_SRC)) $(CXX_OBJ)
# the C++ front end written against the standard (src/frontend/cplusplus,
# doc/PLAN-cxx-spec.md) and its lowering, which the driver calls for C++:
# C++20 code, so only a C++-hosted vcc (VCC_LANG=c++) has them
# (the lowering to C's AST needs C's front end and code generator: apart
# from the cplusplus module; dump/ is the developer outputs, -fcxx-dump=)
cxxlower_SRC = $(wildcard frontend/cplusplus/lower/*.cpp)
cplusplus_SRC = $(filter-out $(cxxlower_SRC),$(wildcard frontend/cplusplus/*/*.cpp frontend/cplusplus/ext/*/*.cpp frontend/cplusplus/abi/*/*.cpp))
CXX_OBJ = $(if $(filter c++,$(VCC_LANG)),$(call mod,cplusplus) $(call o,$(cxxlower_SRC)))
# its subset of C++ (doc/PLAN-cxx-spec.md, Conventions), held by the compiler:
# no RTTI, no C++ library header (C headers only), no object with a static
# constructor or destructor, and no warning
CPLUSPLUS_FLAGS = -fno-rtti -nostdinc++ -Wglobal-constructors -Wexit-time-destructors -Werror
$(OBJ)/unity/cplusplus.o $(call o,$(cplusplus_SRC) $(cxxlower_SRC)): XFLAGS = $(CPLUSPLUS_FLAGS)
# one module's objects: its unity TU, or its files
mod = $(if $(VCC_UNITY),$(OBJ)/unity/$(1).o,$(call o,$($(1)_SRC)))
AS_SRC  = $(AS_FILES) $(as_SRC) $(obj_SRC)
AS_OBJ  = $(call o,$(AS_FILES)) $(call mod,as) $(call mod,obj)
# the ELF linker's parts, in the order ld/ld.cpp includes them (one TU, VCC_UNITY;
# ld-internal.h is what they share); with `make VCC_UNITY=` each part is compiled
# on its own, and vlink-main.cpp (main, under VLD_MAIN) only for vlink
ld_SRC  = $(shell sed -n 's|^\#include "\.\./\(.*\.cpp\)"$$|\1|p; s|^\#include "\([a-z][^"/]*\.cpp\)"$$|ld/\1|p' ld/ld.cpp)
LD_PARTS = $(filter-out ld/vlink-main.cpp,$(ld_SRC))
LD_SRC  = $(LIB_SRC) ld/objcopy.cpp $(if $(VCC_WASM),ld/wasmld.cpp) platform/darwin/macho-linker.cpp platform/win/pe-linker.cpp

LANG_STAMP = $(OBJ)/.lang-$(VCC_LANG)
$(LANG_STAMP): | $(OBJ)
	@rm -f $(OBJ)/.lang-c $(OBJ)/.lang-c++
	@touch $@

$(OBJ)/%.o: %.S Makefile $(LANG_STAMP) $(BOOTSTRAP_COMPILER) | $(OBJ)
	@mkdir -p $(dir $@)
	$(VCC_CC) $(VCC_INC) -MMD -MP -MF $(@:.o=.dep) -c $< -o $@

$(OBJ)/%.o: %.cpp Makefile $(LANG_STAMP) $(BOOTSTRAP_COMPILER) | $(OBJ)
	@mkdir -p $(dir $@)
	$(VCC_CC) $(VCC_DIALECT) $(VCC_FAST) $(VCC_LTO) $(XFLAGS) -MMD -MP -MF $(@:.o=.dep) -c $< -o $@

# rewritten only when the list of files changes
UNITY_C = $(patsubst %,$(OBJ)/unity/%.cpp,$(SPLIT_MODS) as obj vir server cplusplus)
$(UNITY_C): $(OBJ)/unity/%.cpp: FORCE
	@mkdir -p $(dir $@)
	@(echo '#define VCC_UNITY 1'; for f in $(abspath $($*_SRC)); do echo "#include \"$$f\""; done) > $@.tmp
	@if cmp -s $@.tmp $@; then rm $@.tmp; else mv $@.tmp $@; fi

$(UNITY_C:.cpp=.o): %.o: %.cpp Makefile $(LANG_STAMP) $(BOOTSTRAP_COMPILER)
	$(VCC_CC) $(VCC_DIALECT) $(VCC_FAST) $(VCC_LTO) $(XFLAGS) -MMD -MP -MF $(@:.o=.dep) -c $< -o $@

FORCE:
.PHONY: FORCE
.SECONDARY: $(UNITY_C)

# lextest is built with -mno-outline (stable code layout for PMC runs); no LTO,
# since -mno-outline is a code generator option the bitcode does not keep
$(OBJ)/no/%.o: %.cpp Makefile $(LANG_STAMP) $(BOOTSTRAP_COMPILER) | $(OBJ)
	@mkdir -p $(dir $@)
	$(VCC_CC) $(VCC_DIALECT) $(VCC_FAST) $(if $(filter-out x86_64,$(HOST_ARCH)),-mno-outline) -MMD -MP -MF $(@:.o=.dep) -c $< -o $@

$(OBJ)/ld/ld.o $(OBJ)/ld/vlink-main.o: XFLAGS = -DVLD_MAIN

# the ELF linker with main() for vlink, and without it for vccd (links, nm and
# objcopy run in its worker threads)
VLINK_LD_OBJ = $(if $(VCC_UNITY),$(OBJ)/ld/ld.o,$(call o,$(LD_PARTS) ld/vlink-main.cpp))
VCCD_LD_OBJ = $(if $(VCC_UNITY),$(OBJ)/ld/ldlib.o,$(call o,$(LD_PARTS))) $(call o,ld/objcopy.cpp platform/darwin/macho-linker.cpp platform/win/pe-linker.cpp)
$(OBJ)/ld/ldlib.o: ld/ld.cpp Makefile $(LANG_STAMP) $(BOOTSTRAP_COMPILER) | $(OBJ)
	@mkdir -p $(dir $@)
	$(VCC_CC) $(VCC_DIALECT) $(VCC_FAST) $(VCC_LTO) -MMD -MP -MF $(@:.o=.dep) -c $< -o $@

$(OUT)/vpp: $(call o,bin/vpp/vpp.cpp $(LEX_SRC) $(LIB_SRC)) $(LEX_OBJECT) $(AS_OBJ) $(PP_OBJ)
	$(VCC_LINK) $(VCC_LDFLAGS) -pthread $^ -o $@ $(HOST_LIBS)

$(OUT)/vccd: $(call o,$(VCCD_SRC) $(LEX_SRC) $(LIB_SRC)) $(call mod,server) $(LEX_OBJECT) $(AS_OBJ) $(PP_OBJ) $(VCCD_LD_OBJ)
	$(VCC_LINK) $(VCC_LDFLAGS) -pthread $^ -o $@ $(HOST_LIBS)

LEXTEST_SRC = bin/lextest/lextest.cpp port/darwin/pmc.cpp $(LEX_SRC) $(LIB_SRC)
ifeq ($(HOST_ARCH),x86_64)
# x86 has no machine outliner. Reuse the normal objects in non-LTO builds.
LEXTEST_OBJ = $(if $(VCC_LTO),$(patsubst %.cpp,$(OBJ)/no/%.o,$(LEXTEST_SRC)),$(call o,$(LEXTEST_SRC)))
else
LEXTEST_OBJ = $(patsubst %.cpp,$(OBJ)/no/%.o,$(LEXTEST_SRC))
endif
$(OUT)/lextest: $(LEXTEST_OBJ) $(LEX_OBJECT)
	$(VCC_LINK) -O3 $(VCC_SAN) $(VCC_SANLD) $(VCC_PGO) -pthread $^ -o $@

$(OUT)/vasm: $(call o,bin/vasm/vasm.cpp $(LIB_SRC)) $(AS_OBJ)
	$(VCC_LINK) $(VCC_LDFLAGS) $^ -o $@

# virtest: VIR's parser, verifier and printer (SPEC-vir.md 16); depends on nothing else
$(OUT)/virtest: $(call o,bin/virtest/virtest.cpp $(LIB_SRC)) $(call mod,vir)
	$(VCC_LINK) $(VCC_LDFLAGS) $^ -o $@

# The offline reader exercises C++20 and the host standard library.
CLANG_BUILTIN_READER_CXX ?= $(HOST_CXX)
CLANG_BUILTIN_READER_SRC = tools/clang-builtins/main.cpp tools/clang-builtins/reader.cpp tools/clang-builtins/arm.cpp

$(OUT)/clang-builtin-reader: $(CLANG_BUILTIN_READER_SRC) tools/clang-builtins/reader.h | $(OUT)
	$(CLANG_BUILTIN_READER_CXX) -std=c++20 -O2 -Wall -Wextra -Werror $(CLANG_BUILTIN_READER_SRC) -o $@

.PHONY: clang-builtin-reader clang-builtin-reader-check builtin-registrations
clang-builtin-reader: $(OUT)/clang-builtin-reader

clang-builtin-reader-check: clang-builtin-reader $(BUILTIN_GENERATED)
	python3 ../bench/vcc-builtins.py --check-clang-reader --clang-reader $(OUT)/clang-builtin-reader
	python3 tools/gen-builtin-registrations.py --check --reader $(OUT)/clang-builtin-reader \
		--output-root $(OUT)/generated/include

BUILTIN_TD = $(wildcard ../contrib/clang-builtins/clang/include/clang/Basic/*.td)
BUILTIN_GENERATED = $(OUT)/generated/include/frontend/integer-builtins.inc \
	$(OUT)/generated/include/frontend/stdc-builtins.inc \
	$(OUT)/generated/include/frontend/cxx-builtins.inc \
	$(OUT)/generated/include/frontend/c/target-builtins.inc \
	$(OUT)/generated/include/frontend/c/builtin-stubs.inc
$(BUILTIN_GENERATED) &: $(OUT)/clang-builtin-reader tools/gen-builtin-registrations.py $(BUILTIN_TD)
	python3 tools/gen-builtin-registrations.py --reader $(OUT)/clang-builtin-reader \
		--output-root $(OUT)/generated/include

builtin-registrations: $(BUILTIN_GENERATED)

$(OBJ)/unity/pp.o $(call o,$(pp_SRC)) $(OBJ)/unity/cc.o $(call o,$(cc_SRC)) $(OBJ)/unity/cplusplus.o $(call o,$(cplusplus_SRC) $(cxxlower_SRC)): $(BUILTIN_GENERATED)

# viralloc: VIR register allocation with a synthetic target (PLAN-vir.md V4)
VIRALLOC_SRC = $(VIRA_SRC) vir/synthetic-target.cpp
$(OUT)/viralloc: $(call o,bin/viralloc/viralloc.cpp $(VIRALLOC_SRC) $(LIB_SRC)) $(call mod,vir)
	$(VCC_LINK) $(VCC_LDFLAGS) $^ -o $@

# vlink: the linker (ld.cpp) as a standalone tool; vobjcopy/vnm by argv[0]
$(OUT)/vlink: $(VLINK_LD_OBJ) $(call o,$(LD_SRC))
	$(VCC_LINK) $(VCC_LDFLAGS) -pthread $^ -o $@
	ln -sf vlink $(OUT)/vobjcopy
	ln -sf vlink $(OUT)/vnm

$(OBJ)/test/port/task-test.o: test/port/task-test.c Makefile $(LANG_STAMP) $(BOOTSTRAP_COMPILER) | $(OBJ)
	@mkdir -p $(dir $@)
	$(VCC_CC) $(VCC_DIALECT) $(VCC_FAST) $(VCC_LTO) -MMD -MP -MF $(@:.o=.dep) -c $< -o $@

# task-test: the unit test of port/task.cpp (vtask.h)
$(OUT)/task-test: $(OBJ)/test/port/task-test.o $(call o,port/task.cpp)
	$(VCC_LINK) $(VCC_LDFLAGS) -pthread $^ -o $@
task-test: $(OUT)/task-test
.PHONY: task-test

$(OUT)/vcc: $(OBJ)/bin/vcc/shim.o | $(OUT)
	$(VCC_LINK) $(VCC_LDFLAGS) $< -o $@

# vsh: make's SHELL; named sh so make still execs simple commands directly
$(OBJ)/bin/vcc/vsh.o: bin/vcc/shim.cpp Makefile $(LANG_STAMP) $(BOOTSTRAP_COMPILER) | $(OBJ)
	@mkdir -p $(dir $@)
	$(VCC_CC) $(VCC_DIALECT) $(VCC_CFLAGS) -DVSH -MMD -MP -MF $(@:.o=.dep) -c $< -o $@

$(OUT)/vsh/sh: $(OBJ)/bin/vcc/vsh.o | $(OUT)
	mkdir -p $(OUT)/vsh
	$(VCC_LINK) $(VCC_LDFLAGS) $< -o $@

$(OUT)/vld $(OUT)/vas $(OUT)/var $(OUT)/vcc-nm $(OUT)/vcc-objcopy: $(OUT)/vcc
	ln -sf vcc $@

$(OBJ):
	mkdir -p $@ $@/no

-include $(wildcard $(OBJ)/*.dep $(OBJ)/*/*.dep $(OBJ)/*/*/*.dep $(OBJ)/*/*/*/*.dep $(OBJ)/*/*/*/*/*.dep)

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

# vcc's run-time support library for x86_64-w64-mingw32 (runtime/, libvccrt.a;
# PLAN-win64.md WIN7R): CC and AR are runtime/build.sh's parameters; by default it
# compiles with our vpp and archives with our var (and vccd): no LLVM tool
vccrt: $(OUT)/vpp $(if $(filter environment command line,$(origin AR)),,$(OUT)/var $(OUT)/vccd)
	sh runtime/build.sh $(OUT)/vccrt

.PHONY: vccrt

# Reuse the normal target graph so each stage gets the same objects and make
# can schedule independent compilation jobs across the recursive invocation.
SELF ?= $(if $(filter Darwin,$(HOST_OS)),../build/self,$(OUT)-self)
SELF_CC ?= $(abspath $(OUT))/vpp
SELF_LANG ?= c
SELF_TARGET = $(if $(filter Darwin,$(HOST_OS)),arm64-apple-darwin,$(HOST_ARCH)-linux-gnu)
SELF_FLAGS ?= --target=$(SELF_TARGET) -I$(abspath headers)
ifeq ($(HOST_OS),Linux)
SELF_FLAGS += -isystem $(HOST_RESOURCE) -isystem /usr/include/$(SELF_TARGET) -isystem /usr/include
endif
# The stage compiles at VCC_OPT (-O3: VIR with the vectorizer, doc/OPTIMIZATION.md) on
# every host; VCC_OPT=-O1 builds a comparison stage with the direct emitter.
SELF_LINK ?= $(HOST_CC) $(if $(filter Linux,$(HOST_OS)),-no-pie)

self: $(OUT)/vpp
	$(MAKE) vcc OUT=$(SELF) VCC_LANG=$(SELF_LANG) VCC_PGO= VCC_LTO= VCC_CC="$(SELF_CC) $(SELF_FLAGS)" VCC_LINK="$(SELF_LINK)" BOOTSTRAP_COMPILER="$(SELF_CC)"

self-check: self
	$(MAKE) self SELF=$(SELF)3 SELF_CC=$(abspath $(SELF))/vpp
	@cd $(SELF) && find obj -name '*.o' -exec sh -ec 'for o do cmp "$$o" "$(abspath $(SELF)3)/$$o"; done' sh {} +
	@echo "self-check: stage 2 and stage 3 objects identical"

.PHONY: self self-check

# Check the bootstrap C dialect and the host C++ dialect independently:
# a C warning must not hide the C++ diagnostics for that file under make -k.
# Use make -k -j 6 vcc-cxx-check, or set CXX_CHECK_SRC to inspect a subset.
CXX_CHECK_SRC ?= $(sort $(AS_SRC) $(LIB_SRC) $(LEX_SRC) $(PP_SRC) $(VCCD_SRC) $(server_SRC) \
                 $(SPLIT_SRC) $(vir_SRC) $(VIRALLOC_SRC) $(LD_SRC) $(ld_SRC) \
                 $(wildcard bin/*/*.cpp) port/darwin/pmc.cpp)
C_COMPAT_CHECK = $(addprefix check-c-compat/,$(CXX_CHECK_SRC))
CXX_CHECK = $(addprefix check-cxx/,$(CXX_CHECK_SRC))
CXX_CHECK_FLAGS = -Wpedantic -Werror -ferror-limit=0 -fsyntax-only
.PHONY: vcc-cxx-check $(C_COMPAT_CHECK) $(CXX_CHECK)
vcc-cxx-check: $(C_COMPAT_CHECK) $(CXX_CHECK)
$(C_COMPAT_CHECK): check-c-compat/%: %
	$(HOST_CC) -x c -std=c11 $(VCC_FAST) -Wc++-compat $(CXX_CHECK_FLAGS) $<
$(CXX_CHECK): check-cxx/%: %
	$(HOST_CXX) $(VCC_FAST) $(VCC_CXX_DIALECT) $(CXX_CHECK_FLAGS) $<

# Every file of the C++ front end compiles alone (the default build compiles
# them as one TU), with the front end's flags: make -k -j 6 cplusplus-check
CPLUSPLUS_CHECK = $(addprefix check-cplusplus/,$(cplusplus_SRC) $(cxxlower_SRC))
.PHONY: cplusplus-check $(CPLUSPLUS_CHECK)
cplusplus-check: $(CPLUSPLUS_CHECK)
$(CPLUSPLUS_CHECK): check-cplusplus/%: %
	$(HOST_CXX) $(VCC_CXX_DIALECT) $(VCC_FAST) $(CPLUSPLUS_FLAGS) -fsyntax-only $<

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
FUZZ_HDR = frontend/lex/masks.h include/frontend/lex/lex.h include/common/intern.h include/frontend/pp/pp.h include/frontend/pp/dircache.h

fuzz: $(FUZZ_OUT)/fuzz-lex $(FUZZ_OUT)/fuzz-pp $(FUZZ_OUT)/fuzz-diag

$(FUZZ_OUT):
	mkdir -p $@

$(FUZZ_OUT)/fuzz-lex: test/fuzz/fuzz-lex.c test/fuzz/lexref.c test/fuzz/fuzz.h $(FUZZ_DRIVER) $(LEX_ASM) $(LEX_SRC) $(FUZZ_HDR) | $(FUZZ_OUT)
	$(FUZZ_CC) $(FUZZ_CFLAGS) $(FUZZ_ENGINE) test/fuzz/fuzz-lex.c test/fuzz/lexref.c $(FUZZ_DRIVER) $(LEX_ASM) -x c $(LEX_SRC) $(LIB_SRC) -o $@

$(FUZZ_OUT)/fuzz-pp: test/fuzz/fuzz-pp.c test/fuzz/fuzz.h $(FUZZ_DRIVER) $(wildcard frontend/pp/*.cpp frontend/pp/*.h diag/*.cpp) $(LEX_ASM) $(LEX_SRC) $(FUZZ_HDR) $(BUILTIN_GENERATED) | $(FUZZ_OUT)
	$(FUZZ_CC) $(FUZZ_CFLAGS) $(FUZZ_ENGINE) test/fuzz/fuzz-pp.c $(FUZZ_DRIVER) $(LEX_ASM) -x c $(pp_SRC) frontend/pp/dircache.cpp $(LEX_SRC) $(LIB_SRC) -o $@

$(FUZZ_OUT)/fuzz-diag: test/fuzz/fuzz-diag.c test/fuzz/fuzz.h $(FUZZ_DRIVER) $(wildcard frontend/pp/*.cpp frontend/pp/*.h diag/*.cpp) $(LEX_ASM) $(LEX_SRC) $(FUZZ_HDR) $(BUILTIN_GENERATED) | $(FUZZ_OUT)
	$(FUZZ_CC) $(FUZZ_CFLAGS) $(FUZZ_ENGINE) test/fuzz/fuzz-diag.c $(FUZZ_DRIVER) $(LEX_ASM) -x c $(pp_SRC) frontend/pp/dircache.cpp $(LEX_SRC) $(LIB_SRC) -o $@

# replay the regression inputs (test/fuzz/regress/{lex,pp}/): every one must pass
fuzz-regress: fuzz
	$(FUZZ_OUT)/fuzz-lex -runs=0 test/fuzz/regress/lex
	$(FUZZ_OUT)/fuzz-pp -runs=0 -timeout=10 test/fuzz/regress/pp
	$(FUZZ_OUT)/fuzz-diag -runs=0 test/fuzz/regress/diag

# the differential check (vpp against clang -E -P) on the regression and seed inputs
fuzz-diff: $(OUT)/vpp
	test/fuzz/pp-diff.py -j 4 test/fuzz/regress/pp test/fuzz/seeds/pp

.PHONY: fuzz fuzz-regress fuzz-diff
