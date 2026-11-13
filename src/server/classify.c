/*
 * classify.c: decide what kind of request a command line is: -E, a compile vcc does itself, a host
 * program's link, a candidate for VCC_DELEGATE.
 *
 * These are pure functions of the request, called by dispatch.c in its order of precedence; they
 * only look at argv.  Being conservative is the rule: a command line that vcc's driver might handle
 * differently from clang (-shared, -x with another language, several inputs, an input that is not a
 * .c or .S) is not ours and goes to the backend or fails, so every option that takes a value is
 * listed in takes_value (a value taken for an input would misclassify the job).  own_pp and own_cc
 * (VCC_PP=0, VCC_CC=0) switch whole kinds off.
 */
#include "vccd-internal.h"

/* -E on a named file or stdin: vcc's preprocessor handles it */
VPRIV bool cc_is_pp(const struct req *r)
{
	bool e = false;
	const char *last = r->argv[r->argc - 1];

	for (int i = 1; i < r->argc; i++)
		e |= !strcmp(r->argv[i], "-E");
	return e && own_pp && (!strcmp(last, "-") || (last[0] != '-' && (strstr(last, ".c") || strstr(last, ".S") ||
						  strstr(last, ".dts") || strstr(last, ".lds"))));
}
/*
 * -c or -S of a named .c or .S file or of stdin (`-`) for aarch64 Linux: vcc
 * compiles it; on an arm64 Mac also a -c without --target (HOST_CC).  Not:
 * -shared, anything with -x other than c/assembler-with-cpp.  The vDSO's
 * -mcmodel=tiny is vcc's too: its ADRP pairs are PC-relative as tiny's ADR
 * (larger code, no dynamic relocation).  stdin is read by the driver from the
 * client's fd 0, or by vccd first when clang may need the same bytes.
 */
/* options whose value is the next argument (so it is not an input) */
static bool takes_value(const char *a)
{
	static const char *const v[] = {
		"-o", "-I", "-D", "-U", "-include", "-imacros", "-isystem", "-idirafter", "-iquote", "-iprefix",
		"-iwithprefix", "-isysroot", "-MF", "-MT", "-MQ", "-x", "-Xclang", "-Xassembler", "-Xpreprocessor",
		"-mllvm", "--param", "-arch", "-target", "-include-pch", "--sysroot",
	};

	for (size_t k = 0; k < sizeof v / sizeof *v; k++)
		if (!strcmp(a, v[k]))
			return true;
	return false;
}

/* the one input of a compile: a .c or .S file anywhere on the line or "-", NULL if none or several */
VPRIV const char *cc_input(const struct req *r)
{
	const char *in = NULL;

	for (int i = 1; i < r->argc; i++) {
		const char *a = r->argv[i];
		size_t n = strlen(a);

		if (!strcmp(a, "-")) {	/* stdin (Kconfig's cc-version.sh, tools-support-relr.sh) */
			if (in)
				return NULL;
			in = a;
			continue;
		}
		if (a[0] == '-') {
			if (takes_value(a))
				i++;
			continue;
		}
		if (n < 3 || a[n - 2] != '.' || (a[n - 1] != 'c' && a[n - 1] != 'S') || in)
			return NULL;	/* another kind of input (.o, .s, ...) or a second one */
		in = a;
	}
	return in;
}

/* a host program's compile (no --target) is vcc's on an arm64 Mac, the target it defaults to */
#if defined(__APPLE__) && defined(__aarch64__)
#define HOST_CC 1
#else
#define HOST_CC 0
#endif

VPRIV bool cc_is_compile(const struct req *r)
{
	bool c = false, linux_target = false, target = false;

	if (!own_cc || !cc_input(r))
		return false;
	for (int i = 1; i < r->argc; i++) {
		const char *a = r->argv[i];

		if (a[0] != '-')
			continue;
		if (!strcmp(a, "-c"))
			c = true;
		else if (!strncmp(a, "--target=aarch64-", 17) && strstr(a, "-linux"))
			linux_target = target = true;
		else if (!strncmp(a, "--target=", 9) || !strcmp(a, "-target") || !strcmp(a, "-arch"))
			target = true;
		else if (!strcmp(a, "-S"))
			c = true;	/* assembly text: the same job */
		else if (!strcmp(a, "-E") ||
			 !strcmp(a, "-shared") || !strcmp(a, "-emit-llvm"))
			return false;
		else if (!strcmp(a, "-x") && i + 1 < r->argc && strcmp(r->argv[i + 1], "c") &&
			 strcmp(r->argv[i + 1], "assembler-with-cpp"))
			return false;
		else if (takes_value(a))
			i++;
	}
	return c && (linux_target || (HOST_CC && !target));
}

/*
 * A host program from one .c in one command (kbuild's hostprogs: fixdep,
 * mk_elfconfig, ...): no -c, -S, -E, no target, an -o.  vcc compiles it to a
 * temporary object and the backend links that with the same options.
 */
VPRIV bool host_cc_link(const struct req *r)
{
	const char *in = cc_input(r);
	size_t n;
	bool out = false;

	if (!HOST_CC || !own_cc || !in || !strcmp(in, "-") || (n = strlen(in)) < 3 || strcmp(in + n - 2, ".c"))
		return false;
	for (int i = 1; i < r->argc; i++) {
		const char *a = r->argv[i];

		if (!strcmp(a, "-c") || !strcmp(a, "-S") || !strcmp(a, "-E") || !strcmp(a, "-M") || !strcmp(a, "-MM") ||
		    !strcmp(a, "-x") || !strcmp(a, "-shared") || !strcmp(a, "-emit-llvm") || !strcmp(a, "-###") ||
		    !strcmp(a, "-v") || !strncmp(a, "-print-", 7) || !strncmp(a, "--target=", 9) ||
		    !strcmp(a, "-target") || !strcmp(a, "-arch"))
			return false;
		if (!strcmp(a, "-o") && i + 1 < r->argc)
			out = true;
		if (takes_value(a))
			i++;
	}
	return out;
}

/* VCC_DELEGATE: -c of a named .c file (the last argument), nothing vcc's driver would not do */
VPRIV bool dg_is_compile(const struct req *r, const char *skip)
{
	const char *last = r->argv[r->argc - 1];
	size_t n = strlen(last);
	bool c = false;

	if (!own_cc || last[0] == '-' || n < 3 || strcmp(last + n - 2, ".c") || (skip && *skip && strstr(last, skip)))
		return false;
	for (int i = 1; i < r->argc - 1; i++) {
		const char *a = r->argv[i];

		if (!strcmp(a, "-c"))
			c = true;
		else if (!strcmp(a, "-E") || !strcmp(a, "-S") || !strcmp(a, "-M") || !strcmp(a, "-MM") || !strcmp(a, "-x") ||
			 !strcmp(a, "-") || !strcmp(a, "-emit-llvm") || !strncmp(a, "-print-", 7) || !strcmp(a, "-###"))
			return false;
	}
	return c;
}
