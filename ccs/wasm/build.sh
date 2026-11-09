#!/bin/sh
# vcc's compiler (vpp) as WebAssembly (emscripten, memory64), for the
# compiler explorer page (wasm/explorer/).  Output: build/wasm/, the page
# with the compiler in build/wasm/site/.  Needs emscripten (brew install
# emscripten).  wasm/mman.c says what is different from the native build.
set -e
cd "$(dirname "$0")/.."
OUT=${OUT:-../build/wasm}
mkdir -p $OUT/obj
CFLAGS="-Iwasm/compat -std=c11 -O2 -sMEMORY64=1 -msimd128 -mfpu=neon -D_GNU_SOURCE -DVCC_INCDIR=\"/vcc/include\" -Wno-unused-parameter ${WFLAGS}"
SRC="vcc/driver/vpp.c vcc/lex/lex.c vcc/lib/intern.c vcc/arch/a64/as/enc.c vcc/platform/darwin/macho.c
 vcc/pp/dircache.c vcc/driver/ppdriver.c vcc/driver/ccsnap.c vcc/lib/ldhash.c"
for m in as obj cc pp emit; do
	case $m in
	as) f="vcc/as/as.c vcc/arch/a64/as/parse.c";;
	obj) f="vcc/obj/obj.c vcc/arch/a64/obj/fixup.c vcc/platform/linux/elf.c";;
	cc) f=$(ls vcc/cc/*.c);;
	pp) f=$(ls vcc/pp/*.c | grep -v dircache);;
	emit) f=$(ls vcc/emit/*.c vcc/arch/a64/emit/*.c vcc/arch/wasm/emit/*.c);;
	esac
	{ echo '#define VCC_UNITY 1'; for x in $f; do echo "#include \"$PWD/$x\""; done; } > $OUT/obj/u-$m.c
	SRC="$SRC $OUT/obj/u-$m.c"
done
OBJS=
pids=
for s in $SRC; do
	o=$OUT/obj/$(echo $s | sed 's,.*/,,; s,\.c$,.o,')
	OBJS="$OBJS $o"
	emcc $CFLAGS -c $s -o $o &
	pids="$pids $!"
done
fail=0
for p in $pids; do wait $p || fail=1; done
[ $fail = 0 ]
emcc $CFLAGS -c wasm/mman.c -o $OUT/obj/mman.o
mkdir -p $OUT/root/vcc/include $OUT/root/usr/include
cp vcc/include/*.h $OUT/root/vcc/include/
cp wasm/include/*.h $OUT/root/usr/include/
# clang's freestanding headers (the page passes -isystem /usr/lib/clang/include)
R=$(${LLVM:-/opt/homebrew/opt/llvm}/bin/clang -print-resource-dir)/include
mkdir -p $OUT/root/usr/lib/clang/include
for h in stddef stdint stdarg stdbool limits float stdalign stdnoreturn iso646 stdckdint '__stddef_*' '__stdarg_*'; do
	cp -f $R/$h.h $OUT/root/usr/lib/clang/include/
done
chmod -R u+w $OUT/root
emcc -O2 -sMEMORY64=1 -sALLOW_MEMORY_GROWTH=1 -sMAXIMUM_MEMORY=8GB -sSTACK_SIZE=16MB -sINITIAL_MEMORY=64MB \
	-sMODULARIZE=1 -sEXPORT_NAME=VccModule -sINVOKE_RUN=0 -sEXIT_RUNTIME=1 \
	-sEXPORTED_RUNTIME_METHODS=callMain,FS -sFORCE_FILESYSTEM=1 -sENVIRONMENT=web,worker,node \
	--embed-file $OUT/root@/ \
	$OBJS $OUT/obj/mman.o -o $OUT/vcc.js
ls -la $OUT/vcc.js $OUT/vcc.wasm
# the explorer page: index.html, worker.js, vcc.js, vcc.wasm
mkdir -p $OUT/site
cp wasm/explorer/index.html wasm/explorer/worker.js $OUT/vcc.js $OUT/vcc.wasm $OUT/site/
