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
VCC_CFLAGS = -std=c11 -O2 -Wall -Wextra -Wno-unused-parameter -D_DARWIN_C_SOURCE -DVCC_INCDIR='"$(abspath vcc/include)"'
VCC_FAST   = $(VCC_CFLAGS) -O3 -mcpu=apple-m1 -pthread
# -O3 also at link time: the driver passes it to ld, which then folds code
VCC_LDFLAGS = -O3
OBJ        = $(OUT)/obj

vcc: $(OUT)/vcc $(OUT)/vld $(OUT)/vas $(OUT)/var $(OUT)/vccd $(OUT)/lextest $(OUT)/vpp $(OUT)/vlink $(OUT)/vasm

o = $(patsubst vcc/%.c,$(OBJ)/%.o,$(1))
AS_SRC  = vcc/as.c vcc/a64.c vcc/elf.c
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

$(OUT)/vccd: $(call o,vcc/vccd.c vcc/caps.c vcc/fixdep.c vcc/ar.c $(PP_SRC) $(LEX_SRC) $(AS_SRC))
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

$(OUT)/vld $(OUT)/vas $(OUT)/var: $(OUT)/vcc
	ln -sf vcc $@

$(OBJ):
	mkdir -p $@ $@/no

-include $(wildcard $(OBJ)/*.d $(OBJ)/no/*.d)
