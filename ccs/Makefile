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
VCC_CFLAGS = -std=c11 -O2 -Wall -Wextra -Wno-unused-parameter -D_DARWIN_C_SOURCE -DVCC_INCLUDE='"$(abspath vcc/include)"'

vcc: $(OUT)/vcc $(OUT)/vld $(OUT)/vas $(OUT)/vccd $(OUT)/lextest $(OUT)/vpp $(OUT)/vlink $(OUT)/vasm

AS_SRC = vcc/as.c vcc/a64.c vcc/elf.c vcc/macho.c
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

# self: vcc compiles its own sources (Mach-O) and ld64 links stage 2 into
# $(SELF); self-check builds stage 3 with stage 2 and requires identical objects.
SELF ?= ../build/self
SELF_CC = $(OUT)/vpp
SELF_FLAGS = -c -std=c11 -O2 -D_DARWIN_C_SOURCE -DVCC_INCLUDE='"$(abspath vcc/include)"'
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
