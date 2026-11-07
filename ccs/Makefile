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

vcc: $(OUT)/vcc $(OUT)/vld $(OUT)/vas $(OUT)/vccd $(OUT)/lextest $(OUT)/vpp

LEX_SRC = vcc/lex.c vcc/intern.c
LEX_HDR = vcc/lex.h vcc/intern.h
PP_SRC = vcc/pp.c vcc/dircache.c vcc/ppdriver.c vcc/cc.c vcc/emit.c
PP_HDR = vcc/pp.h vcc/cc.h vcc/ccint.h vcc/emit.h vcc/dircache.h vcc/ppdriver.h vcc/ppdata.inc vcc/ppdata-predef.inc

$(OUT)/vpp: vcc/vpp.c vcc/ppdata-predef.inc $(PP_SRC) $(PP_HDR) $(LEX_SRC) $(LEX_HDR) | $(OUT)
	/usr/bin/clang $(VCC_CFLAGS) -O3 -mcpu=apple-m1 -pthread vcc/vpp.c $(PP_SRC) $(LEX_SRC) -o $@

$(OUT)/lextest: vcc/lextest.c vcc/pmc.c vcc/pmc.h $(LEX_SRC) $(LEX_HDR) | $(OUT)
	/usr/bin/clang $(VCC_CFLAGS) -O3 -mcpu=apple-m1 -mno-outline -pthread vcc/lextest.c vcc/pmc.c $(LEX_SRC) -o $@

$(OUT)/vcc: vcc/shim.c protocol.h | $(OUT)
	/usr/bin/clang $(VCC_CFLAGS) $< -o $@

$(OUT)/vld $(OUT)/vas: $(OUT)/vcc
	ln -sf vcc $@

$(OUT)/vccd: vcc/vccd.c vcc/caps.c vcc/caps.h vcc/warnings.inc protocol.h $(PP_SRC) $(PP_HDR) $(LEX_SRC) $(LEX_HDR) | $(OUT)
	/usr/bin/clang $(VCC_CFLAGS) -O3 -mcpu=apple-m1 -pthread vcc/vccd.c vcc/caps.c $(PP_SRC) $(LEX_SRC) -o $@
