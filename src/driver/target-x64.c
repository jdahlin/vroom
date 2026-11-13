/*
 * target-x64.c: resolve the x86-64 target's CPU and feature set from the options, and reject x86
 * switches on arm64.
 *
 * x86_driver_features follows clang: the CPU first (-march=, `native` asking the host through
 * port/x86native.h), then every -m[no-]feature in command-line order, each applying the feature
 * table's dependencies (include/arch/x64/x86features.h, generated from clang by
 * tools/gen-x86features.py).  Besides the result in job->x86_features_ it keeps the raw, on and off
 * sets that the code generator and the predefined macros need.  An unknown CPU or feature fails the
 * job with status 1.  On arm64 and Darwin the same switches (-mavx2, -mno-sse ...) are errors, as in
 * clang, except -msse4 and -msse4.2 which clang only calls unused.  Called once per job, after the
 * option loop and before anything reads the feature fields.
 */
#include "driver-internal.h"
#include "port/x86native.h"

/* Resolve CPU first, then feature options in command-line order, as clang. */
static const char *x86_driver_features(int argc, char **argv,
                                     x86_features *features, x86_features *disabled,
                                     x86_features *raw, x86_features *on, x86_features *off,
                                     const char **cpu)
{
 *cpu = "x86-64";
	for (int i = 1; i < argc; i++)
		if (!strncmp(argv[i], "-march=", 7)) *cpu = argv[i] + 7;
	if (!strcmp(*cpu, "native")) {
		if (!x86_native_features(features, disabled, cpu)) return "native";
  *on = *features; *off = *disabled;
	} else if (!x86_cpu_features(*cpu, features)) return *cpu;
	for (int i = 1; i < argc; i++) {
		const char *a = argv[i];
		if (!strncmp(a, "-mcpu=", 6)) return a;
		if (!strncmp(a, "-mtune=", 7)) {
			x86_features unused = {0};
			if (strcmp(a + 7, "generic") && strcmp(a + 7, "native") && !x86_cpu_features(a + 7, &unused)) return a;
			continue;
		}
		if (!strcmp(a, "-mllvm")) { i++; continue; }
		/* These driver switches control generated entry/return sequences. */
		if (!strcmp(a, "-mfunction-return=thunk-extern") || !strcmp(a, "-mfunction-return=keep") ||
		    !strcmp(a, "-mretpoline-external-thunk") || !strcmp(a, "-mindirect-branch-cs-prefix"))
		    continue;
		if (!strcmp(a, "-mgeneral-regs-only")) {
			const unsigned bits[] = {X86_FEATURE_X87, X86_FEATURE_MMX, X86_FEATURE_SSE};
			for (unsigned j = 0; j < sizeof(bits)/sizeof(*bits); j++) {
				x86_feature_apply(features, bits[j], false);
				for (unsigned w = 0; w < X86_FEATURE_WORDS; w++) {
					off->bits[w] |= x86_feature_deps[bits[j]][0].bits[w];
					on->bits[w] &= ~x86_feature_deps[bits[j]][0].bits[w];
				}
				disabled->bits[bits[j]/64] |= UINT64_C(1) << (bits[j]%64);
			}
			continue;
		}
		if (strncmp(a, "-m", 2)) continue;
		bool enable = strncmp(a, "-mno-", 5) != 0;
		const char *name = a + (enable ? 2 : 5);
		if (!strcmp(name, "80387")) name = "x87";
		if (!strcmp(name, "sse4")) name = enable ? "sse4.2" : "sse4.1";
		int bit = x86_feature_find(name);
		if (bit < 0 || !x86_feature_has(x86_cli_valid, (unsigned)bit)) {
			/* Driver/code-model switches are not instruction-set feature names. */
			if (!strncmp(name, "arch=", 5) || !strncmp(name, "tune=", 5) ||
	!strncmp(name, "cmodel=", 7) || !strncmp(name, "stack-alignment=", 16) ||
	!strncmp(name, "stack-protector-guard", 21) || !strcmp(name, "64") || !strcmp(name, "red-zone") ||
	!strcmp(name, "stackrealign") || !strcmp(name, "omit-leaf-frame-pointer") ||
	!strcmp(name, "stack-arg-probe") || !strcmp(name, "accumulate-outgoing-args") ||
	!strcmp(name, "outline-atomics") || !strcmp(name, "strict-align") ||
	/* the kernel's: no instructions vcc emits (3DNow!, x87 returns, al before varargs, regparm) */
	!strcmp(name, "3dnow") || !strcmp(name, "3dnowa") || !strcmp(name, "fp-ret-in-387") ||
	!strcmp(name, "skip-rax-setup") || !strncmp(name, "regparm=", 8) || !strcmp(name, "retpoline")) continue;
			return a;
		}
		/* Clang's driver retains only the last spelling of each feature before
					dependency expansion; an overridden enable must not leave prerequisites. */
		bool superseded = false;
		for (int j = i + 1; j < argc; j++) {
			const char *later = argv[j];
			if (!strcmp(later, "-mllvm")) { j++; continue; }
			if (strncmp(later, "-m", 2)) continue;
			later += strncmp(later, "-mno-", 5) ? 2 : 5;
			const char *current = a + (enable ? 2 : 5);
			if (!strcmp(later, current)) { superseded = true; break; }
		}
		if (superseded) continue;
		x86_feature_apply(features, (unsigned)bit, enable);
		x86_features expanded = x86_feature_change((unsigned)bit, enable);
		for (unsigned w = 0; w < X86_FEATURE_WORDS; w++) {
			uint64_t change = expanded.bits[w];
			if (enable) on->bits[w] |= change, off->bits[w] &= ~change;
			else off->bits[w] |= change, on->bits[w] &= ~change;
		}
		if (!enable) disabled->bits[bit/64] |= UINT64_C(1) << (bit%64);
		else disabled->bits[bit/64] &= ~(UINT64_C(1) << (bit%64));
	}
 *raw = *features;
	x86_features_finish(features, *disabled);
	return NULL;
}

/* 0 to go on; 1: the job ends (status and message set) */
VPRIV int driver_target_features_check(struct driver_job *job, int argc, char **argv)
{
	if (!job->x86 && !job->wasm) {
		/* arm64: x86 instruction-set switches (-mavx2, -mno-sse, ...) are
		   errors, as clang's TargetSpecific options; -msse4/-msse4.2 are
		   only unused there, so they stay accepted. */
		for (int i = 1; i < argc; i++) {
			const char *a = argv[i];
			if (!strcmp(a, "-mllvm")) { i++; continue; }
			if (strncmp(a, "-m", 2)) continue;
			const char *name = a + (strncmp(a, "-mno-", 5) ? 2 : 5);
			if (!strcmp(name, "sse4") || !strcmp(name, "sse4.2")) continue;
			if (!strcmp(name, "80387")) name = "x87";
			int bit = x86_feature_find(name);
			if (bit < 0 || !x86_feature_has(x86_cli_valid, (unsigned)bit)) continue;
			dprintf(job->errfd, "vcc: error: unsupported option '%s' for target '%s'\n", a,
			        job->darwin ? "arm64-apple-darwin" : "aarch64-unknown-linux-gnu");
			job->status = 1;
			return 1;
		}
	}
	if (job->x86) {
		const char *unsupported = x86_driver_features(argc, argv, &job->x86_features_, &job->x86_disabled, &job->x86_raw, &job->x86_on, &job->x86_off, &job->x86_cpu);
		if (unsupported) {
			if (!strcmp(unsupported, "native"))
				dprintf(job->errfd, "vcc: error: -march=native requires a matching x86-64 host; use an explicit x86 CPU name for cross-compilation\n");
			else
				dprintf(job->errfd, "vcc: error: unsupported x86 target CPU or feature '%s'\n", unsupported);
			job->status = 1;
			return 1;
		}
		job->nosse = !x86_feature_has(job->x86_features_, X86_FEATURE_SSE);
		job->nosse2 = !x86_feature_has(job->x86_features_, X86_FEATURE_SSE2);
		job->nommx = !x86_feature_has(job->x86_features_, X86_FEATURE_MMX);
		job->gregs = 0; /* already applied in option order; derive SIMD restriction below */
				}
	return 0;
}
