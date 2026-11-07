/*
 * vcc capability table: the options vcc/vld accept.
 *
 * Kbuild and Kconfig probe the toolchain (cc-option, ld-option, ...) and
 * enable features based on the answers.  vccd answers those probes from
 * this table instead of running a compiler, so the table *is* vcc's
 * declared feature set.  It is deliberately minimal: what the kernel needs
 * to build and boot, nothing more.  No sanitizers, stack protector, branch
 * protection, shadow call stack, auto-var-init, debug info or LLVM-specific
 * knobs; the kernel then simply builds without them.
 *
 * Whatever is accepted here must be implemented by vcc (and, while the
 * backend is still clang, be accepted by clang as well).
 */
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "caps.h"

static bool in(const char *s, const char *const *list)
{
	for (; *list; list++)
		if (!strcmp(s, *list))
			return true;
	return false;
}

static bool prefix(const char *s, const char *p)
{
	return !strncmp(s, p, strlen(p));
}

/* options taking the next argument */
static const char *const cc_with_arg[] = {
	"-o", "-include", "-isystem", "-iquote", "-x", "-MF", "-MT", "-MQ", NULL
};

static const char *const cc_exact[] = {
	"-c", "-S", "-E", "-P", "-nostdinc", "-pipe", "-g0",
	"-O0", "-O1", "-O2", "-Os",
	"--target=aarch64-linux-gnu", "-fintegrated-as",
	/* code generation model */
	"-mlittle-endian", "-mgeneral-regs-only", "-mstrict-align", "-m64",
	"-mcmodel=tiny", "-mcmodel=small", "-mbranch-protection=none",
	"-ffreestanding", "-fno-PIE", "-fno-PIC", "-fno-pie", "-fno-pic", "-fpie", "-fPIE",
	"-ffixed-x18", "-fno-common", "-funsigned-char", "-fshort-wchar",
	"-ffunction-sections", "-fdata-sections",
	"-fno-function-sections", "-fno-data-sections",
	"-fno-unwind-tables", "-fno-asynchronous-unwind-tables", "-fasynchronous-unwind-tables",
	"-fomit-frame-pointer", "-fno-omit-frame-pointer",
	"-fno-stack-protector", "-fno-stack-clash-protection", "-fno-stack-check",
	"-fno-strict-overflow", "-fno-strict-aliasing",
	"-fno-optimize-sibling-calls", "-fno-delete-null-pointer-checks",
	"-fno-builtin", "-fno-addrsig", "-fno-jump-tables", "-fno-ident",
	/* language */
	"-fms-anonymous-structs", "-fms-extensions", "-fstrict-flex-arrays=3",
	"-std=gnu11", "-std=gnu89", "-std=c11", "-std=gnu99",
	NULL
};

static const char *const cc_prefix[] = {
	"-D", "-U", "-I", "-fmacro-prefix-map=", "-ffile-prefix-map=",
	"-falign-functions=", "-fno-builtin-", "-Wp,-MMD,", "-Wp,-MD,",
	NULL
};

/* assembler options accepted through -Wa, */
static bool as_option_ok(const char *o, size_t n)
{
	static const char *const ok[] = { "--fatal-warnings", NULL };
	char tmp[256];

	if (n >= sizeof(tmp))
		return false;
	memcpy(tmp, o, n);
	tmp[n] = 0;
	return in(tmp, ok) || prefix(tmp, "-march=armv8") || prefix(tmp, "-march=armv9");
}

static const char *const warning_groups[] = {
#include "warnings.inc"
};

static int cmp_str(const void *k, const void *e)
{
	return strcmp(k, *(const char *const *)e);
}

/*
 * -W<name>, -Wno-<name>, -Werror=<name>, -Wno-error=<name>, -W<name>=<n>:
 * accepted if <name> is a known warning group.
 */
static bool warning_ok(const char *a)
{
	char name[128];
	const char *s = a + 2;
	size_t n;

	if (!strcmp(s, "error") || !strcmp(s, "all") || !strcmp(s, "extra"))
		return true;
	if (!strncmp(s, "no-", 3))
		s += 3;
	if (!strncmp(s, "error=", 6))
		s += 6;
	n = strcspn(s, "=");
	if (!n || n >= sizeof(name))
		return false;
	memcpy(name, s, n);
	name[n] = 0;
	return bsearch(name, warning_groups, sizeof(warning_groups) / sizeof(warning_groups[0]),
		       sizeof(warning_groups[0]), cmp_str) != NULL;
}

int cc_option(const char *a, const char *next, const char **why)
{
	*why = a;
	if (in(a, cc_with_arg))
		return next ? 2 : 0;
	if (in(a, cc_exact))
		return 1;
	for (const char *const *p = cc_prefix; *p; p++)
		if (prefix(a, *p))
			return 1;
	if (prefix(a, "-Wa,")) {
		const char *s = a + 4;

		while (*s) {
			const char *e = strchr(s, ',');
			size_t n = e ? (size_t)(e - s) : strlen(s);

			if (!as_option_ok(s, n))
				return 0;
			s += n + (e != NULL);
		}
		return 1;
	}
	/* warning options: diagnostics only, but must name a known group */
	if (a[0] == '-' && a[1] == 'W' && !prefix(a, "-Wl,") && !prefix(a, "-Wp,"))
		return warning_ok(a);
	return 0;
}

static const char *const ld_exact[] = {
	"-maarch64elf", "-EL", "--gc-sections", "--orphan-handling=warn",
	"--orphan-handling=error", "--build-id=sha1", "--no-undefined",
	"-z", "norelro", "noexecstack", "max-page-size=4096", "separate-code",
	"-shared", "-Bsymbolic", "-n", "-r", "--emit-relocs", "--pie", "-pie",
	"--no-dynamic-linker", "--fix-cortex-a53-843419", NULL
};

int ld_option(const char *a, const char **why)
{
	*why = a;
	return in(a, ld_exact);
}

/* macros vcc predefines (with a value of its own) */
bool cc_predefined(const char *def)
{
	static const char *const predef[] = {
		"__SIZEOF_INT128__", "__aarch64__", "__AARCH64EL__", "__LP64__",
		"__CHAR_UNSIGNED__", "__clang__", "__GNUC__", NULL
	};
	size_t n = strcspn(def, "=");

	for (const char *const *p = predef; *p; p++)
		if (strlen(*p) == n && !strncmp(def, *p, n))
			return true;
	return false;
}
