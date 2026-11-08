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
VCC_CFLAGS = -std=c11 -O2 -Wall -Wextra -Wno-unused-parameter -D_DARWIN_C_SOURCE

vcc: $(OUT)/vcc $(OUT)/vld $(OUT)/vas $(OUT)/vccd $(OUT)/lextest $(OUT)/vpp $(OUT)/vlink $(OUT)/vasm

AS_SRC = vcc/as.c vcc/a64.c vcc/elf.c
AS_HDR = vcc/as.h vcc/a64.h vcc/elf.h vcc/a64sysreg.inc
LEX_SRC = vcc/lex.c vcc/intern.c
LEX_HDR = vcc/lex.h vcc/intern.h
PP_SRC = vcc/pp.c vcc/dircache.c vcc/ppdriver.c vcc/cc.c vcc/ccsnap.c vcc/emit.c
PP_HDR = vcc/pp.h vcc/cc.h vcc/ccint.h vcc/ccsnap.h vcc/emit.h vcc/dircache.h vcc/ppdriver.h vcc/ppdata.inc vcc/ppdata-predef.inc

$(OUT)/vpp: vcc/vpp.c vcc/ppdata-predef.inc $(PP_SRC) $(PP_HDR) $(LEX_SRC) $(LEX_HDR) $(AS_SRC) $(AS_HDR) | $(OUT)
	/usr/bin/clang $(VCC_CFLAGS) -O3 -mcpu=apple-m1 -pthread vcc/vpp.c $(PP_SRC) $(LEX_SRC) $(AS_SRC) -o $@

$(OUT)/lextest: vcc/lextest.c vcc/pmc.c vcc/pmc.h $(LEX_SRC) $(LEX_HDR) | $(OUT)
	/usr/bin/clang $(VCC_CFLAGS) -O3 -mcpu=apple-m1 -mno-outline -pthread vcc/lextest.c vcc/pmc.c $(LEX_SRC) -o $@


$(OUT)/vasm: vcc/vasm.c $(AS_SRC) $(AS_HDR) | $(OUT)
	/usr/bin/clang $(VCC_CFLAGS) -O3 -mcpu=apple-m1 vcc/vasm.c $(AS_SRC) -o $@

$(OUT)/vcc: vcc/shim.c protocol.h | $(OUT)
	/usr/bin/clang $(VCC_CFLAGS) $< -o $@

$(OUT)/vld $(OUT)/vas: $(OUT)/vcc
	ln -sf vcc $@

# vlink: the linker (ld.c) as a standalone tool; vobjcopy/vnm by argv[0]
LD_SRC = vcc/ld.c vcc/ldhash.c vcc/objcopy.c
LD_HDR = vcc/ld.h vcc/ldhash.h vcc/ldelf.h
$(OUT)/vlink: $(LD_SRC) $(LD_HDR) | $(OUT)
	/usr/bin/clang $(VCC_CFLAGS) -O3 -mcpu=apple-m1 -pthread -DVLD_MAIN $(LD_SRC) -o $@
	ln -sf vlink $(OUT)/vobjcopy
	ln -sf vlink $(OUT)/vnm

$(OUT)/vccd: vcc/vccd.c vcc/caps.c vcc/caps.h vcc/warnings.inc protocol.h $(PP_SRC) $(PP_HDR) $(LEX_SRC) $(LEX_HDR) $(AS_SRC) $(AS_HDR) | $(OUT)
	/usr/bin/clang $(VCC_CFLAGS) -O3 -mcpu=apple-m1 -pthread vcc/vccd.c vcc/caps.c $(PP_SRC) $(LEX_SRC) $(AS_SRC) -o $@

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

fuzz: $(FUZZ_OUT)/fuzz-lex $(FUZZ_OUT)/fuzz-pp

$(FUZZ_OUT):
	mkdir -p $@

$(FUZZ_OUT)/fuzz-lex: fuzz/fuzz-lex.c fuzz/lexref.c fuzz/fuzz.h $(FUZZ_DRIVER) $(LEX_SRC) $(LEX_HDR) | $(FUZZ_OUT)
	$(FUZZ_CC) $(FUZZ_CFLAGS) $(FUZZ_ENGINE) fuzz/fuzz-lex.c fuzz/lexref.c $(FUZZ_DRIVER) $(LEX_SRC) -o $@

$(FUZZ_OUT)/fuzz-pp: fuzz/fuzz-pp.c fuzz/fuzz.h $(FUZZ_DRIVER) vcc/pp.c vcc/dircache.c $(PP_HDR) $(LEX_SRC) $(LEX_HDR) | $(FUZZ_OUT)
	$(FUZZ_CC) $(FUZZ_CFLAGS) $(FUZZ_ENGINE) fuzz/fuzz-pp.c $(FUZZ_DRIVER) vcc/pp.c vcc/dircache.c $(LEX_SRC) -o $@

# replay the regression inputs (fuzz/regress/{lex,pp}/): every one must pass
fuzz-regress: fuzz
	$(FUZZ_OUT)/fuzz-lex -runs=0 fuzz/regress/lex
	$(FUZZ_OUT)/fuzz-pp -runs=0 -timeout=10 fuzz/regress/pp

# the differential check (vpp against clang -E -P) on the regression and seed inputs
fuzz-diff: $(OUT)/vpp
	fuzz/pp-diff.py -j 4 fuzz/regress/pp fuzz/seeds/pp

.PHONY: fuzz fuzz-regress fuzz-diff
