#!/bin/sh
# The premerge test set, one job per argument (.github/workflows/*.yml run one
# each, in parallel).  The cheap suites only: no kernel tree, no PostgreSQL,
# no clang reference outputs (work/ is not needed).  Left out for that reason:
# vcc-driver.py (compares with the exact clang version string), vas-enc.py,
# gen-diagids.py --check, vcc-fpcheck.py (kernel tree / work/clang-td),
# vcc-builtins.py (Homebrew's clang).
#
#   ci/premerge.sh arm64    vcc build, the C suites under qemu, fast harnesses
#   ci/premerge.sh asan     ASan+UBSan build: vcc suite, diag, stress, vccd, vlink-bad
#   ci/premerge.sh tsan     TSan build: vcc suite (threaded emitter), vccd
#   ci/premerge.sh wasm     the C suites for wasm64 (emcc + node)
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

build() {	# build MAKE-TARGET: a build failure ends the job
	echo "::group::make $1"
	if ! make -C ccs -j "$J" "$1" > "$LOGS/make-$1.log" 2>&1; then
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
	step ctests       python3 bench/vcc-ctests.py -j "$J"
	step switch-plan python3 bench/vcc-switch-plan.py --sanitize
	step rec-effects  python3 bench/vcc-rec-effects.py
	step rec-slots python3 bench/vcc-rec-slots.py --sanitize
	step rec-slot-values python3 bench/vcc-rec-slot-values.py --sanitize
	step slot-observable python3 bench/vcc-slot-observable.py --check-mutation
	step opt-debug-x86 python3 bench/vcc-opt-debug.py
	step rec-copies python3 bench/vcc-rec-copies.py
	step opt-rules-arm64 python3 bench/vcc-opt-rules.py --target aarch64-linux-gnu
	step opt-rules-x86 python3 bench/vcc-opt-rules.py --target x86_64-linux-gnu --run
	step c2y-cache    python3 bench/vcc-c2y-cache.py
	step diag         python3 bench/vcc-diag.py -j "$J"
	step stress       python3 bench/vcc-stress.py
	step stress-vccd  python3 bench/vcc-stress.py --vccd
	step vccd         python3 bench/vccd-test.py
	step abi          python3 bench/vcc-abi.py
	step atomic       python3 bench/vcc-atomic.py --bin-dir "$BUILD" --target aarch64-linux-gnu
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
	B=$(dirname "$BUILD")/ccs-asan
	# LeakSanitizer is on by default on Linux (not on macOS): vcc exits with
	# its arenas and caches mapped, by design, so every compile would "leak"
	export ASAN_OPTIONS=${ASAN_OPTIONS:+$ASAN_OPTIONS:}detect_leaks=0
	step ctests-vcc   env VCC_BIN="$B" python3 bench/vcc-ctests.py -j "$J" --suite vcc
	step atomic       python3 bench/vcc-atomic.py --bin-dir "$B" --target aarch64-linux-gnu
	step diag         python3 bench/vcc-diag.py -j "$J" --bin-dir "$B"
	step stress       python3 bench/vcc-stress.py --bin-dir "$B"
	step vccd         python3 bench/vccd-test.py --bin-dir "$B"
	step vlink-bad    python3 bench/vlink-bad.py --bin-dir "$B"
	;;
tsan)
	build vcc-tsan
	B=$(dirname "$BUILD")/ccs-tsan
	step ctests-vcc   env VCC_BIN="$B" python3 bench/vcc-ctests.py -j "$J" --suite vcc
	step vccd         python3 bench/vccd-test.py --bin-dir "$B"
	step stress-vccd  python3 bench/vcc-stress.py --vccd --bin-dir "$B"
	;;
wasm)
	build vcc
	step opt-rules-wasm32 python3 bench/vcc-opt-rules.py --target wasm32-unknown-emscripten --run
	step opt-rules-wasm64 python3 bench/vcc-opt-rules.py --target wasm64-unknown-emscripten --run
	step wasm-cells python3 bench/vcc-wasm-cells.py
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
