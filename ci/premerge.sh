#!/bin/sh
# The premerge test set, one job per argument (.github/workflows/*.yml run one
# each, in parallel).  The cheap suites only: no kernel tree, no PostgreSQL,
# no clang reference outputs (work/ is not needed).  Left out for that reason:
# vcc-driver.py (compares with the exact clang version string), vas-enc.py,
# gen-diagids.py --check, vcc-fpcheck.py (kernel tree / work/clang-td),
# vcc-builtins.py (Homebrew's clang).
#
#   ci/premerge.sh arm64    vcc build, the C suites under qemu, fast harnesses
#   ci/premerge.sh asan     ASan+UBSan build: vcc suite, diag, stress, vccd, vlink-bad, fuzz-regress
#   ci/premerge.sh tsan     TSan build: vcc suite (threaded emitter), vccd
#   ci/premerge.sh wasm     the C suites for wasm64 (emcc + node); DISABLED, manual (doc/PLAN-wasm.md)
#
# Locally (Debian arm64 container, as CI):  env/linux/run.sh ci/premerge.sh arm64
# Each step's output goes to build/linux/ci-logs/STEP.log (CI uploads it on
# failure); the script runs every step and fails at the end if any failed.
set -u
JOB=${1:?usage: ci/premerge.sh arm64|asan|tsan|wasm}
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
	if [ "$(uname -s)/$(uname -m)" = Linux/x86_64 ]; then
		step host-dispatch python3 bench/vcc-host-dispatch.py --bin-dir "$BUILD"
		step x86-direct-objects python3 bench/vcc-x86.py --suite vcc --objcmp -j "$J"
		step interrupt python3 bench/vcc-interrupt.py
		step vir-x86 python3 bench/vcc-vir-x86.py --bin-dir "$BUILD"
	fi
	step runtime-exceptions python3 bench/test-runtime-exceptions.py
	step pg-cc-contract python3 bench/pg/test-cc.py
	step pg-speed-accounting python3 bench/pg/test-speed.py
	step memory-cells-generated python3 src/tools/gen-memory-cells.py --check
	step ctests       python3 bench/vcc-ctests.py -j "$J"
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
	if [ "$(uname -s)/$(uname -m)" = Linux/x86_64 ]; then
		step bitint-float-x86 python3 bench/vcc-bitint-float.py --bin-dir "$BUILD" --output "$LOGS/bitint-float-x86" --cflag=-O3
	fi
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
asan)
	build vcc-asan
	B=$(dirname "$BUILD")/src-asan
	# LeakSanitizer is on by default on Linux (not on macOS): vcc exits with
	# its arenas and caches mapped, by design, so every compile would "leak"
	export ASAN_OPTIONS=${ASAN_OPTIONS:+$ASAN_OPTIONS:}detect_leaks=0
	step ctests-vcc   env VCC_BIN="$B" python3 bench/vcc-ctests.py -j "$J" --suite vcc
	step vir-tests-asan python3 bench/vir-test.py -j "$J" --bin-dir "$B"
	step vir-exposure-asan python3 bench/vir-exposure.py --bin-dir "$B"
	step atomic       python3 bench/vcc-atomic.py --bin-dir "$B" --target aarch64-linux-gnu
	step virgen       python3 bench/virgen-test.py --bin-dir "$B"
	step bitint-replay python3 bench/vcc-bitint-replay.py --bin-dir "$B" --output "$LOGS/bitint-replay-asan"
	step diag         python3 bench/vcc-diag.py -j "$J" --bin-dir "$B"
	step stress       python3 bench/vcc-stress.py --bin-dir "$B"
	step vccd         python3 bench/vccd-test.py --bin-dir "$B"
	step vlink-bad    python3 bench/vlink-bad.py --bin-dir "$B"
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
*)
	echo "unknown job $JOB"; exit 2;;
esac

if [ -n "$FAILED" ]; then
	echo "$JOB: failed:$FAILED"
	exit 1
fi
echo "$JOB: all passed"
