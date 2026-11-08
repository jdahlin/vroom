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
VCC_CFLAGS = -std=c11 -O2 -Wall -Wextra -Wno-unused-parameter -D_DARWIN_C_SOURCE $(VCC_SAN)
# per-tool optimisation; the sanitizer variants below override both
VCC_OPT ?= -O3 -mcpu=apple-m1
VCC_SAN ?=

vcc: $(OUT)/vcc $(OUT)/vld $(OUT)/vas $(OUT)/vccd $(OUT)/lextest $(OUT)/vpp $(OUT)/vlink $(OUT)/vasm

AS_SRC = vcc/as.c vcc/a64.c vcc/elf.c
AS_HDR = vcc/as.h vcc/a64.h vcc/elf.h vcc/a64sysreg.inc
LEX_SRC = vcc/lex.c vcc/intern.c
LEX_HDR = vcc/lex.h vcc/intern.h
PP_SRC = vcc/pp.c vcc/dircache.c vcc/ppdriver.c vcc/cc.c vcc/ccsnap.c vcc/emit.c
PP_HDR = vcc/pp.h vcc/cc.h vcc/ccint.h vcc/ccsnap.h vcc/emit.h vcc/dircache.h vcc/ppdriver.h vcc/ppdata.inc vcc/ppdata-predef.inc

$(OUT)/vpp: vcc/vpp.c vcc/ppdata-predef.inc $(PP_SRC) $(PP_HDR) $(LEX_SRC) $(LEX_HDR) $(AS_SRC) $(AS_HDR) | $(OUT)
	/usr/bin/clang $(VCC_CFLAGS) $(VCC_OPT) -pthread vcc/vpp.c $(PP_SRC) $(LEX_SRC) $(AS_SRC) -o $@

$(OUT)/lextest: vcc/lextest.c vcc/pmc.c vcc/pmc.h $(LEX_SRC) $(LEX_HDR) | $(OUT)
	/usr/bin/clang $(VCC_CFLAGS) $(VCC_OPT) -mno-outline -pthread vcc/lextest.c vcc/pmc.c $(LEX_SRC) -o $@


$(OUT)/vasm: vcc/vasm.c $(AS_SRC) $(AS_HDR) | $(OUT)
	/usr/bin/clang $(VCC_CFLAGS) $(VCC_OPT) vcc/vasm.c $(AS_SRC) -o $@

$(OUT)/vcc: vcc/shim.c protocol.h | $(OUT)
	/usr/bin/clang $(VCC_CFLAGS) $< -o $@

$(OUT)/vld $(OUT)/vas: $(OUT)/vcc
	ln -sf vcc $@

# vlink: the linker (ld.c) as a standalone tool; vobjcopy/vnm by argv[0]
LD_SRC = vcc/ld.c vcc/ldhash.c vcc/objcopy.c
LD_HDR = vcc/ld.h vcc/ldhash.h vcc/ldelf.h
$(OUT)/vlink: $(LD_SRC) $(LD_HDR) | $(OUT)
	/usr/bin/clang $(VCC_CFLAGS) $(VCC_OPT) -pthread -DVLD_MAIN $(LD_SRC) -o $@
	ln -sf vlink $(OUT)/vobjcopy
	ln -sf vlink $(OUT)/vnm

$(OUT)/vccd: vcc/vccd.c vcc/caps.c vcc/caps.h vcc/warnings.inc protocol.h $(PP_SRC) $(PP_HDR) $(LEX_SRC) $(LEX_HDR) $(AS_SRC) $(AS_HDR) | $(OUT)
	/usr/bin/clang $(VCC_CFLAGS) $(VCC_OPT) -pthread vcc/vccd.c vcc/caps.c $(PP_SRC) $(LEX_SRC) $(AS_SRC) -o $@

# ---- sanitizer variants of the vcc tools (AGENTS.md, "Sanitizers") ----
SAN_COMMON = -O1 -g -fno-omit-frame-pointer -Wno-deprecated-declarations
vcc-asan:
	$(MAKE) vcc OUT=../build/ccs-asan VCC_OPT="-O1 -mcpu=apple-m1" \
		VCC_SAN="$(SAN_COMMON) -fsanitize=address,undefined"
vcc-tsan:
	$(MAKE) vcc OUT=../build/ccs-tsan VCC_OPT="-O1 -mcpu=apple-m1" \
		VCC_SAN="$(SAN_COMMON) -fsanitize=thread"

.PHONY: all clean vcc vcc-asan vcc-tsan
