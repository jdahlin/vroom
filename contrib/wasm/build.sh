#!/bin/sh
# vcc's compiler (vpp) as WebAssembly (emscripten, memory64), for the
# compiler explorer page (contrib/wasm/explorer/).  Output: build/wasm/, the page
# with the compiler in build/wasm/site/.  Needs emscripten (brew install
# emscripten).  src/port/wasm/mman.c says what is different from the native build.
# VCC=path/to/vpp: vcc compiles itself (PLAN-wasm.md W6), every object from
# vcc's wasm64 generator and assembler with emscripten's libc headers; emcc
# only links.  vcc's own <arm_neon.h>/<arm_acle.h> give the intrinsics in C.
set -e
cd "$(dirname "$0")/../../src"
W=../contrib/wasm
OUT=${OUT:-../build/wasm}
mkdir -p $OUT/obj
CFLAGS="-Iport/wasm/compat -std=c11 -O2 -sMEMORY64=1 -msimd128 -mfpu=neon -D_GNU_SOURCE -DVCC_INCDIR=\"/vcc/include\" -Wno-unused-parameter ${WFLAGS}"
SRC="driver/vpp.c lex/lex.c common/intern.c arch/a64/as/enc.c platform/darwin/macho.c arch/wasm/as/as.c platform/wasm/wasm.c
 pp/dircache.c driver/ppdriver.c driver/ccsnap.c common/ldhash.c"
for m in as obj cc pp emit; do
	case $m in
	as) f="as/as.c arch/a64/as/parse.c";;
	obj) f="obj/obj.c arch/a64/obj/fixup.c platform/linux/elf.c";;
	cc) f=$(ls cc/*.c);;
	pp) f=$(ls pp/*.c | grep -v dircache);;
	emit) f=$(ls emit/*.c emit/opt/*.c emit/rec/*.c emit/flow/*.c arch/a64/emit/*.c arch/wasm/emit/*.c);;
	esac
	{ echo '#define VCC_UNITY 1'; for x in $f; do echo "#include \"$PWD/$x\""; done; } > $OUT/obj/u-$m.c
	SRC="$SRC $OUT/obj/u-$m.c"
done
if [ -n "$VCC" ]; then
	SR=$(em-config CACHE)/sysroot
	RES=$(${LLVM:-/opt/homebrew/opt/llvm}/bin/clang -print-resource-dir)/include
	COMPILE="$VCC --target=wasm64-unknown-emscripten -nostdinc -isystem $RES -isystem $SR/include/wasm64-emscripten
	 -isystem $SR/include -std=c11 -O2 -D_GNU_SOURCE -DVCC_INCDIR=\"/vcc/include\" -Wno-unused-parameter -mllvm -wasm-enable-sjlj ${WFLAGS}"
	LFLAGS=-sSUPPORT_LONGJMP=wasm	# setjmp through wasm exception handling, as vcc compiles it
else
	COMPILE="emcc $CFLAGS"
fi
OBJS=
pids=
for s in $SRC port/wasm/mman.c; do
	o=$OUT/obj/$(echo $s | sed 's,.*/,,; s,\.c$,.o,')
	OBJS="$OBJS $o"
	$COMPILE -c $s -o $o &
	pids="$pids $!"
done
fail=0
for p in $pids; do wait $p || fail=1; done
[ $fail = 0 ]
mkdir -p $OUT/root/vcc/include $OUT/root/usr/include
cp headers/*.h $OUT/root/vcc/include/
cp $W/include/*.h $OUT/root/usr/include/
# clang's freestanding headers (the page passes -isystem /usr/lib/clang/include)
R=$(${LLVM:-/opt/homebrew/opt/llvm}/bin/clang -print-resource-dir)/include
mkdir -p $OUT/root/usr/lib/clang/include
for h in stddef stdint stdarg stdbool limits float stdalign stdnoreturn iso646 stdckdint '__stddef_*' '__stdarg_*'; do
	cp -f $R/$h.h $OUT/root/usr/lib/clang/include/
done
chmod -R u+w $OUT/root
emcc -O2 -sMEMORY64=1 $LFLAGS -sALLOW_MEMORY_GROWTH=1 -sMAXIMUM_MEMORY=8GB -sSTACK_SIZE=16MB -sINITIAL_MEMORY=64MB \
	-sMODULARIZE=1 -sEXPORT_NAME=VccModule -sINVOKE_RUN=0 -sEXIT_RUNTIME=1 \
	-sEXPORTED_RUNTIME_METHODS=callMain,FS -sFORCE_FILESYSTEM=1 -sENVIRONMENT=web,worker,node \
	--embed-file $OUT/root@/ \
	$OBJS -o $OUT/vcc.js
ls -la $OUT/vcc.js $OUT/vcc.wasm
# the explorer page: index.html, worker.js, vcc.js, vcc.wasm
mkdir -p $OUT/site
cp $W/explorer/index.html $W/explorer/worker.js $OUT/vcc.js $OUT/vcc.wasm $OUT/site/
