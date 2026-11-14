#!/bin/sh
# The premerge test set, one job per argument (.github/workflows/*.yml run one
# each, in parallel).  The cheap suites only: no kernel tree, no PostgreSQL,
# no clang reference outputs (work/ is not needed).  Left out for that reason:
# vcc-driver.py (compares with the exact clang version string), vas-enc.py,
# gen-diagids.py --check, vcc-fpcheck.py (kernel tree / work/clang-td),
# vcc-builtins.py (Homebrew's clang).
#
#   ci/premerge.sh arm64    vcc build, the C suites under qemu, fast harnesses
#   ci/premerge.sh x86      x86-64 host: the steps needing one (x86 runner)
#   ci/premerge.sh asan     ASan+UBSan build: vcc suite, diag, stress, vccd, vlink-bad, fuzz-regress
#   ci/premerge.sh tsan     TSan build: vcc suite (threaded emitter), VIR x86
#   ci/premerge.sh tsan-vccd  TSan build: vccd service and stress
#   ci/premerge.sh wasm     the C suites for wasm64 (emcc + node); DISABLED, manual (doc/PLAN-wasm.md)
#   ci/premerge.sh win64    Windows x64 (MinGW-w64) under Wine: suites, vlink PE, libvccrt, var COFF;
#                           exports the programs for the native Windows job (doc/PLAN-win64.md WIN9)
#
# Locally (Debian arm64 container, as CI):  env/linux/run.sh ci/premerge.sh arm64
# Each step's output goes to build/linux/ci-logs/STEP.log (CI uploads it on
# failure); the script runs every step and fails at the end if any failed.
set -u
JOB=${1:?usage: ci/premerge.sh arm64|x86|asan|tsan|tsan-vccd|wasm|win64}
cd "$(dirname "$0")/.."
J=${CI_JOBS:-$(nproc 2>/dev/null || sysctl -n hw.ncpu)}
BUILD=$(python3 -c 'import sys; sys.path.insert(0, "bench"); import hostenv; print(hostenv.BUILD)')
LOGS=$(dirname "$BUILD")/ci-logs
mkdir -p "$LOGS"
export UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1
FAILED=

step() {	# step NAME CMD...: run, log, report; keep going on failure
	name=$1; shift
	echo "::group::$name"
	t0=$(date +%s)
	"$@" > "$LOGS/$name.log" 2>&1
	rc=$?
	tail -n 25 "$LOGS/$name.log"
	echo "::endgroup::"
	if [ $rc -eq 0 ]; then
		echo "ok   $name ($(( $(date +%s) - t0 )) s)"
	else
		echo "FAIL $name (exit $rc, $(( $(date +%s) - t0 )) s): $LOGS/$name.log"
		[ -n "${GITHUB_ACTIONS:-}" ] && echo "::error title=$JOB: $name failed::$(tail -n 5 "$LOGS/$name.log" | tr '\n' ' ' | cut -c1-500)"
		FAILED="$FAILED $name"
	fi
	[ -n "${GITHUB_STEP_SUMMARY:-}" ] && echo "| $name | $([ $rc -eq 0 ] && echo pass || echo '**FAIL**') |" >> "$GITHUB_STEP_SUMMARY"
	return 0
}

skip() {	# skip NAME WHY: a guarded step whose feature is not in this tree
	echo "skip $1 ($2)"
	[ -n "${GITHUB_STEP_SUMMARY:-}" ] && echo "| $1 | skipped: $2 |" >> "$GITHUB_STEP_SUMMARY"
	return 0
}

has() {	# has FILE TEXT: FILE exists and contains TEXT (a feature guard)
	[ -f "$1" ] && grep -qF -- "$2" "$1"
}

build() {	# build MAKE-TARGET [VAR=VALUE]: a build failure ends the job
	echo "::group::make $1"
	if ! make -C src -j "$J" "$@" > "$LOGS/make-$1.log" 2>&1; then
		tail -n 60 "$LOGS/make-$1.log"; echo "::endgroup::"
		echo "FAIL make $1"; exit 1
	fi
	tail -n 3 "$LOGS/make-$1.log"; echo "::endgroup::"
}

[ -n "${GITHUB_STEP_SUMMARY:-}" ] && printf '### %s\n| step | result |\n|---|---|\n' "$JOB" >> "$GITHUB_STEP_SUMMARY"
case $JOB in
arm64)
	build vcc
	step runtime-exceptions python3 bench/test-runtime-exceptions.py
	step pg-cc-contract python3 bench/pg/test-cc.py
	step pg-speed-accounting python3 bench/pg/test-speed.py
	step memory-cells-generated python3 src/tools/gen-memory-cells.py --check
	# the suites at each code generator (doc/OPTIMIZATION.md): -O2 VIR (the default), -O1 the
	# direct emitter, -O3 VIR with the vectorizer and its checks (verifier, allocation check)
	step ctests       python3 bench/vcc-ctests.py -j "$J"
	step ctests-O1    python3 bench/vcc-ctests.py -j "$J" -O1
	step ctests-O3    python3 bench/vcc-ctests.py -j "$J" -O3 --vflags "-fvir-verify -fvir-check"
	step switch-plan python3 bench/vcc-switch-plan.py --sanitize
	step udivconst python3 bench/vcc-udivconst.py --sanitize
	step sdivconst python3 bench/vcc-sdivconst.py --sanitize
	step mulhi python3 bench/vcc-mulhi.py --sanitize
	step rec-effects  python3 bench/vcc-rec-effects.py
	step rec-slots python3 bench/vcc-rec-slots.py --sanitize
	step rec-slot-values python3 bench/vcc-rec-slot-values.py --sanitize
	step rec-mulconst python3 bench/vcc-rec-mulconst.py --sanitize --check-mutation

	step flow-contracts python3 bench/vcc-flow.py --sanitize
	step flow-values python3 bench/vcc-flow-values.py --sanitize
	step vperf-lifecycle python3 bench/vperf-test.py
	step vir-tests python3 bench/vir-test.py -j "$J" --bin-dir "$BUILD"
	step vir-exposure python3 bench/vir-exposure.py --bin-dir "$BUILD"
	step rec-dead python3 bench/vcc-rec-dead.py --sanitize
	step emit-names python3 bench/vcc-emit-names.py --sanitize
	step unused-init-contracts python3 bench/vcc-unused-init.py --sanitize
	step slot-observable python3 bench/vcc-slot-observable.py --check-mutation
	step opt-debug-x86 python3 bench/vcc-opt-debug.py
	step x86-leaf-frame python3 bench/vcc-x86-leaf-frame.py --no-run
	step rec-copies python3 bench/vcc-rec-copies.py
	step switch-bitsets-arm64 python3 bench/vcc-switch-bitsets.py --target aarch64-linux-gnu --run
	step switch-bitsets-x86 python3 bench/vcc-switch-bitsets.py --target x86_64-linux-gnu --run
	step opt-rules-arm64 python3 bench/vcc-opt-rules.py --target aarch64-linux-gnu
	step switch-values-arm64 python3 bench/vcc-opt-rules.py --target aarch64-linux-gnu --rule switchvalues --run
	step tail-controls python3 bench/vcc-tail-controls.py
	step tail-object-arm64 python3 bench/vcc-opt-rules.py --target aarch64-linux-gnu --rule tails --run --tail-obj --cfi
	step const-control-arm64 python3 bench/vcc-constant-control.py --target aarch64-linux-gnu
	step opt-rules-x86 python3 bench/vcc-opt-rules.py --target x86_64-linux-gnu --run
	step const-control-x86 python3 bench/vcc-constant-control.py --target x86_64-linux-gnu
	step c2y-cache    python3 bench/vcc-c2y-cache.py
	step embed-search python3 bench/vcc-embed-search.py
	step diag         python3 bench/vcc-diag.py -j "$J"
	step field-uses   python3 bench/vcc-field-uses.py -j "$J"
	step x86-features python3 bench/vcc-x86features.py --clang "${LLVM:-/usr/lib/llvm-23}/bin/clang" --vpp "$BUILD/vpp"
	step x86-targets  python3 bench/vcc-x86target.py --clang "${LLVM:-/usr/lib/llvm-23}/bin/clang" --vpp "$BUILD/vpp"
	step stress       python3 bench/vcc-stress.py
	step stress-vccd  python3 bench/vcc-stress.py --vccd
	step task-test    "$BUILD/task-test"
	step common-test  env CC="${LLVM:-/usr/lib/llvm-23}/bin/clang" python3 bench/common-test.py -j "$J"
	step vccd         python3 bench/vccd-test.py
	step abi          python3 bench/vcc-abi.py
	step atomic       python3 bench/vcc-atomic.py --bin-dir "$BUILD" --target aarch64-linux-gnu
	step bitint-ops   python3 bench/vcc-bitint.py --target aarch64 --bin-dir "$BUILD" --output "$LOGS/bitint-ops" -j "$J"
	step bitint-abi   python3 bench/vcc-bitint-abi.py --target aarch64 --bin-dir "$BUILD" --output "$LOGS/bitint-abi" -j "$J"
	step c90-modes    python3 bench/vcc-c90.py --target aarch64-linux-gnu --bin-dir "$BUILD" --output "$LOGS/c90-modes"
	step extint-alias python3 bench/vcc-extint.py --target aarch64-linux-gnu --bin-dir "$BUILD" --output "$LOGS/extint-alias"
	step bitint-constants python3 bench/vcc-bitint-constants.py --target aarch64-linux-gnu --bin-dir "$BUILD" --output "$LOGS/bitint-constants"
	step bitint-float python3 bench/vcc-bitint-float.py --target aarch64-linux-gnu --bin-dir "$BUILD" --output "$LOGS/bitint-float" --cflag=-O3
	step pic          python3 bench/vcc-pic.py
	step vas-obj      python3 bench/vas-obj.py
	step vlink-opts   python3 bench/vlink-opts.py
	step vsh          python3 bench/vsh-test.py
	step torture-dg   python3 bench/test-torture-directives.py
	step gcc-dg       python3 bench/test-gcc-dg.py
	step libc-test-harness python3 bench/test-libc-test.py
	step kefir-harness python3 bench/test-kefir.py
	step runtime-oracle-harness python3 bench/test-runtime-oracle.py
	step verify-contract python3 bench/test-verify.py
	step onboarding-verifier python3 bench/test-onboarding.py
	step onboarding   python3 bench/vcc-onboarding.py --target aarch64-linux-gnu --baseline
	step bflayout     python3 bench/vcc-bflayout.py
	step ldconst      python3 bench/vcc-ldconst.py
	step cfi          python3 bench/vcc-cfi.py -j "$J"
	step dwarf        python3 bench/vcc-dwarf.py -j "$J"
	;;
x86)
	# vcc as an x86-64 program on an x86-64 host: the steps that need one
	build vcc
	step host-dispatch python3 bench/vcc-host-dispatch.py --bin-dir "$BUILD"
	step vlink-dyn python3 bench/vlink-dyn.py
	step x86-direct-objects python3 bench/vcc-x86.py --suite vcc --objcmp -j "$J"
	step interrupt python3 bench/vcc-interrupt.py
	step vir-x86 python3 bench/vcc-vir-x86.py --bin-dir "$BUILD"
	step bitint-float-x86 python3 bench/vcc-bitint-float.py --bin-dir "$BUILD" --output "$LOGS/bitint-float-x86" --cflag=-O3
	;;
asan)
	build vcc-asan
	B=$(dirname "$BUILD")/src-asan
	# LeakSanitizer is on by default on Linux (not on macOS): vcc exits with
	# its arenas and caches mapped, by design, so every compile would "leak"
	export ASAN_OPTIONS=${ASAN_OPTIONS:+$ASAN_OPTIONS:}detect_leaks=0
	# Explicit vcc-timeout limits measure the normal compiler. ASan/UBSan
	# makes init_large about 7x slower; allow sanitizer overhead as for TSan.
	step ctests-vcc   env VCC_BIN="$B" VCC_TIMEOUT_SCALE=6 python3 bench/vcc-ctests.py -j "$J" --suite vcc
	step ctests-vcc-O1 env VCC_BIN="$B" VCC_TIMEOUT_SCALE=6 python3 bench/vcc-ctests.py -j "$J" --suite vcc -O1
	step vir-tests-asan python3 bench/vir-test.py -j "$J" --bin-dir "$B"
	step vir-exposure-asan python3 bench/vir-exposure.py --bin-dir "$B"
	# Hosted atomics use the runner's native libc/libatomic; arm64 is covered above.
	step atomic       python3 bench/vcc-atomic.py --bin-dir "$B" --target x86_64-linux-gnu
	step virgen       python3 bench/virgen-test.py --bin-dir "$B"
	step bitint-replay python3 bench/vcc-bitint-replay.py --bin-dir "$B" --output "$LOGS/bitint-replay-asan"
	step diag         python3 bench/vcc-diag.py -j "$J" --bin-dir "$B"
	step field-uses   python3 bench/vcc-field-uses.py -j "$J" --bin-dir "$B"
	step stress       python3 bench/vcc-stress.py --bin-dir "$B"
	step vccd         python3 bench/vccd-test.py --bin-dir "$B"
	step vlink-bad    python3 bench/vlink-bad.py --bin-dir "$B"
	step vlink-pe-bad python3 bench/vlink-pe-bad.py --bin-dir "$B"
	# the fuzz targets (ASan+UBSan, libFuzzer from libclang-rt-N-dev when present, else test/fuzz/driver.c)
	# build and replay test/fuzz/regress: replay only, no fuzzing; a build break or a failing input fails the job
	step fuzz-regress make -C src -j "$J" fuzz-regress
	;;
tsan)
	build vcc-tsan
	B=$(dirname "$BUILD")/src-tsan
	if [ "$(uname -s)/$(uname -m)" = Linux/x86_64 ]; then
		step vir-x86-tsan python3 bench/vcc-vir-x86.py --bin-dir "$B"
	fi
	# a test's vcc-timeout bounds the normal build; TSan's is 15-20x slower
	# (init_large: 10 s was not enough on the x86-64 runner)
	step ctests-vcc   env VCC_BIN="$B" VCC_TIMEOUT_SCALE=6 python3 bench/vcc-ctests.py -j "$J" --suite vcc
	step task-test    "$B/task-test"
	;;
tsan-vccd)
	# split from tsan for wall time: these two are ~325 s of TSan's ~490 s
	build vcc-tsan
	B=$(dirname "$BUILD")/src-tsan
	step vccd         python3 bench/vccd-test.py --bin-dir "$B"
	step stress-vccd  python3 bench/vcc-stress.py --vccd --bin-dir "$B"
	;;
wasm)
	# disabled (2026-11-13): needs the wasm sources compiled in
	build vcc VCC_WASM=1
	step switch-bitsets-wasm32 python3 bench/vcc-switch-bitsets.py --target wasm32-unknown-emscripten --run
	step switch-bitsets-wasm64 python3 bench/vcc-switch-bitsets.py --target wasm64-unknown-emscripten --run
	step opt-rules-wasm32 python3 bench/vcc-opt-rules.py --target wasm32-unknown-emscripten --run
	step opt-rules-wasm64 python3 bench/vcc-opt-rules.py --target wasm64-unknown-emscripten --run
	step wasm-cells python3 bench/vcc-wasm-cells.py
	step wasm-flow python3 bench/vcc-wasm-flow.py
	step const-control-wasm32 python3 bench/vcc-constant-control.py --target wasm32-unknown-emscripten
	step const-control-wasm64 python3 bench/vcc-constant-control.py --target wasm64-unknown-emscripten
	step wasm64       python3 bench/vcc-wasm.py -j "$J"
	;;
win64)
	# Windows x64 (doc/PLAN-win64.md WIN9): x86-64 Linux with Wine and the
	# llvm-mingw sysroot (ci/setup-debian.sh win64).  Clang and lld are the
	# oracle only.  The programs that pass are exported with a manifest for
	# the `native` job (bench/win64-native.py on windows-latest).
	build vcc
	X=${CI_WIN64_EXPORT:-$(dirname "$BUILD")/win64-exes}
	rm -rf "$X" "$X.tar.xz"
	step win64-ref     python3 bench/vcc-win64.py --ref-only -j "$J"
	step win64-vlink   python3 bench/vcc-win64.py --vlink-ref -j "$J"
	# GUARD own: vcc's code generator, COFF assembler and own-toolchain mode
	# (vcc compiles, vlink links with libvccrt.a, Wine runs), from the
	# WIN1-WIN5 branch.  Until it is here, the exported programs are clang's
	# objects linked by vlink with libvccrt.a (--vccrt-ref); after, vcc's.
	if has bench/vcc-win64.py "'--own'"; then
		step win64-vccrt python3 bench/vcc-win64.py --vccrt-ref -j "$J"
		step win64-own   python3 bench/vcc-win64.py --own -j "$J" --export "$X"
		# GUARD own+vir: the same through VIR (-O2's SSA path)
		if has bench/vcc-win64.py "'--vir'"; then
			step win64-own-vir python3 bench/vcc-win64.py --own --vir -j "$J"
		else
			skip win64-own-vir "no --vir in bench/vcc-win64.py"
		fi
	else
		step win64-vccrt python3 bench/vcc-win64.py --vccrt-ref -j "$J" --export "$X"
		skip win64-own "no --own in bench/vcc-win64.py (WIN1-WIN5 not merged)"
		skip win64-own-vir "no --own in bench/vcc-win64.py"
	fi
	step vlink-pe      python3 bench/vlink-pe.py --run
	step vccrt-diff    python3 bench/vccrt-test.py diff --export "$X"
	step vccrt-chkstk  python3 bench/vccrt-test.py chkstk --export "$X"
	step var-coff      python3 bench/var-cmp.py coff -j "$J" -n 300 --bin-dir "$BUILD"
	step vnm-coff      python3 bench/vnm-coff.py -j "$J" --bin-dir "$BUILD"
	# GUARD abi: cross-compiler calls clang/vcc x caller/callee under Wine (WIN2)
	if has bench/vcc-abi.py win64; then
		step abi-win64 python3 bench/vcc-abi.py --target win64
	else
		skip abi-win64 "no --target win64 in bench/vcc-abi.py (WIN2)"
	fi
	# GUARD vas-obj: vasm's COFF cases (WIN4), src/test/vas-obj/{win64,coff}-*.s
	if ls src/test/vas-obj | grep -qE '^(win64|coff)-.*[.]s$'; then
		step vas-obj-coff python3 bench/vas-obj.py
	else
		skip vas-obj-coff "no COFF cases in src/test/vas-obj (WIN4)"
	fi
	# WIN8: zlib and SQLite built by vcc for the target (and zlib's own
	# configure through the vcc shim and vccd), their own tests under Wine
	# against clang's cross build; each skips cleanly without its source.
	if has bench/zlib/run.py win64; then
		step zlib-win64 python3 bench/zlib/run.py --target win64 all
	else
		skip zlib-win64 "WIN8: no win64 in bench/zlib/run.py"
	fi
	if has bench/sqlite/run.py win64; then
		step sqlite-win64 python3 bench/sqlite/run.py --target win64 all
	else
		skip sqlite-win64 "WIN8: no win64 in bench/sqlite/run.py"
	fi
	# the artifact for the native job: one tar.xz (the programs share the
	# CRT, so a solid archive is a fraction of a zip of each)
	step win64-pack    sh -c 'for m in vcc-win64 vccrt-diff vccrt-chkstk; do
		[ -f "$1/$m.manifest.json" ] || { echo "no $m.manifest.json: its step failed"; exit 1; }; done
		tar -C "$(dirname "$1")" -cJf "$1.tar.xz" "$(basename "$1")"' pack "$X"
	[ -f "$X.tar.xz" ] && echo "win64-exes: $(ls "$X"/*.manifest.json | wc -l) manifests, $(find "$X/exe" -name '*.exe' | wc -l) programs, $(du -k "$X.tar.xz" | cut -f1) KiB packed"
	;;
*)
	echo "unknown job $JOB"; exit 2;;
esac

if [ -n "$FAILED" ]; then
	echo "$JOB: failed:$FAILED"
	exit 1
fi
echo "$JOB: all passed"
