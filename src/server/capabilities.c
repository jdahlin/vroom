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

#include "capabilities.h"

static bool vccd_capabilities_list_contains(const char *s, const char *const *list)
{
	for (; *list; list++)
		if (!strcmp(s, *list))
			return true;
	return false;
}

static bool vccd_capabilities_has_prefix(const char *s, const char *p)
{
	return !strncmp(s, p, strlen(p));
}

/* options taking the next argument */
static const char *const vccd_capabilities_cc_with_argument[] = {
	"-o", "-include", "-isystem", "-iquote", "-x", "-MF", "-MT", "-MQ", "--explain", NULL
};

static const char *const vccd_capabilities_cc_exact[] = {
	"-c", "-S", "-E", "-P", "-C", "-nostdinc", "-pipe", "-g0",
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
	/* diagnostics (driver/cmdline.c parses them) */
	"-fcolor-diagnostics", "-fno-color-diagnostics", "-fdiagnostics-color", "-fno-diagnostics-color",
	"-fdiagnostics-color=always", "-fdiagnostics-color=never", "-fdiagnostics-color=auto",
	"-fno-caret-diagnostics", "-fcaret-diagnostics", "-fdiagnostics-show-id", "-fno-diagnostics-show-id",
	"-fdiagnostics-parseable-fixits", "-fno-diagnostics-parseable-fixits", "-fdiagnostics-generate-patch", "--fix",
	"-fdiagnostics-format=json", "-fdiagnostics-format=sarif", "-fdiagnostics-format=clang", "-fno-spell-checking",
	"-fdiagnostics-style=vcc", "-fdiagnostics-style=clang", "-fdiagnostics-style=narrated",
	"-fdiagnostics-text-art-charset=ascii", "-fdiagnostics-text-art-charset=unicode",
	"-fdiagnostics-text-art-charset=emoji", "-fdiagnostics-text-art-charset=none",
	NULL
};

static const char *const vccd_capabilities_cc_prefix[] = {
	"-D", "-U", "-I", "-fmacro-prefix-map=", "-ffile-prefix-map=",
	"-falign-functions=", "-fno-builtin-", "-Wp,-MMD,", "-Wp,-MD,",
	"-ferror-limit=", "-fmacro-backtrace-limit=", "-fspell-checking-limit=", "-fdiagnostics-add-output=sarif:file=",
	NULL
};

/* assembler options accepted through -Wa, */
static bool vccd_capabilities_as_option_is_ok(const char *o, size_t n)
{
	static const char *const ok[] = { "--fatal-warnings", NULL };
	char tmp[256];

	if (n >= sizeof(tmp))
		return false;
	memcpy(tmp, o, n);
	tmp[n] = 0;
	return vccd_capabilities_list_contains(tmp, ok) || vccd_capabilities_has_prefix(tmp, "-march=armv8") || vccd_capabilities_has_prefix(tmp, "-march=armv9");
}

static const char *const vccd_capabilities_warning_groups[] = {
#include "warnings.inc"
};

static int vccd_capabilities_string_compare(const void *k, const void *e)
{
	return strcmp(k, *(const char *const *)e);
}

/*
 * -W<name>, -Wno-<name>, -Werror=<name>, -Wno-error=<name>, -W<name>=<n>:
 * accepted if <name> is a known warning group.
 */
static bool vccd_capabilities_warning_is_ok(const char *a)
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
	return bsearch(name, vccd_capabilities_warning_groups, sizeof(vccd_capabilities_warning_groups) / sizeof(vccd_capabilities_warning_groups[0]),
		       sizeof(vccd_capabilities_warning_groups[0]), vccd_capabilities_string_compare) != NULL;
}

int vccd_capabilities_cc_option_arity(const char *a, const char *next, const char **why)
{
	*why = a;
	if (vccd_capabilities_list_contains(a, vccd_capabilities_cc_with_argument))
		return next ? 2 : 0;
	if (vccd_capabilities_list_contains(a, vccd_capabilities_cc_exact))
		return 1;
	for (const char *const *p = vccd_capabilities_cc_prefix; *p; p++)
		if (vccd_capabilities_has_prefix(a, *p))
			return 1;
	if (vccd_capabilities_has_prefix(a, "-Wa,")) {
		const char *s = a + 4;

		while (*s) {
			const char *e = strchr(s, ',');
			size_t n = e ? (size_t)(e - s) : strlen(s);

			if (!vccd_capabilities_as_option_is_ok(s, n))
				return 0;
			s += n + (e != NULL);
		}
		return 1;
	}
	/* warning options: diagnostics only, but must name a known group */
	if (a[0] == '-' && a[1] == 'W' && !vccd_capabilities_has_prefix(a, "-Wl,") && !vccd_capabilities_has_prefix(a, "-Wp,"))
		return vccd_capabilities_warning_is_ok(a);
	return 0;
}

static const char *const vccd_capabilities_ld_exact[] = {
	"-maarch64elf", "-EL", "--gc-sections", "--orphan-handling=warn",
	"--orphan-handling=error", "--build-id=sha1", "--no-undefined",
	"-z", "norelro", "noexecstack", "max-page-size=4096", "separate-code",
	"-shared", "-Bsymbolic", "-n", "-r", "--emit-relocs", "--pie", "-pie",
	"--no-dynamic-linker", "--fix-cortex-a53-843419", NULL
};

int vccd_capabilities_ld_option_is_accepted(const char *a, const char **why)
{
	*why = a;
	return vccd_capabilities_list_contains(a, vccd_capabilities_ld_exact);
}

/*
 * Identity.  Kbuild/Kconfig parse these (cc-version.sh takes the version from
 * __clang_major__ etc., ld-version.sh and CC_VERSION_TEXT from --version), so
 * they are what clang/lld 23.1.4 print: the .config stays the same.
 */
const char vccd_capabilities_cc_version_text[] = "Homebrew clang version 23.1.4\n";
const char vccd_capabilities_ld_version_text[] = "Homebrew LLD 23.1.4 (compatible with GNU linkers)\n";

/* `-print-file-name=include`: the compiler's own headers (stddef.h, ...) */
#ifdef VCC_RESDIR
const char vccd_capabilities_resource_include[] = VCC_RESDIR;
#else
const char vccd_capabilities_resource_include[] = "/opt/homebrew/opt/llvm/lib/clang/23/include";
#endif

/* vcc builds the kernel only: no target libc, so no user-space links */
const bool vccd_capabilities_can_link = false;

/*
 * Link options vld rejects up front, so that a probe link (Kconfig's
 * tools-support-relr.sh) fails without running a linker: vld does not pack
 * relative relocations (RELR), and TOOLS_SUPPORT_RELR stays n.
 */
bool vccd_capabilities_ld_option_is_rejected(const char *a, const char *next)
{
	return vccd_capabilities_has_prefix(a, "--pack-dyn-relocs=") || !strcmp(a, "-zpack-relative-relocs") ||
	       (!strcmp(a, "-z") && next && !strcmp(next, "pack-relative-relocs"));
}

/*
 * Probe sources: what Kbuild and Kconfig pipe into the compiler to test a
 * feature (as-instr; `echo '...' | $(CC) -x c - -c`), with vcc's answer.
 * Compared after printf %b/echo, trailing newlines ignored.  A source that is
 * not listed is compiled for real (vccd forwards it), so the list only has to
 * cover the probes on the critical path; all answers equal clang's.
 */
struct vccd_capabilities_probe_source {
	const char *src;
	bool ok;
};

static const struct vccd_capabilities_probe_source vccd_capabilities_as_sources[] = {
	{ "1:\n.inst 0\n.rept . - 1b\n\nnop\n.endr", true },		/* arm64 Kconfig (negated) */
	{ ".cfi_startproc\n.cfi_negate_ra_state\n.cfi_endproc", true },	/* AS_HAS_CFI_NEGATE_RA_STATE */
	{ ".arch armv8.5-a+memtag\nstgm xzr,[x0]", true },			/* ARM64_AS_HAS_MTE */
	{ ".arch_extension mops", true },					/* AS_HAS_MOPS */
	{ ".arch_extension lsui", true },					/* AS_HAS_LSUI */
	{ ".arch_extension sme", true },					/* AS_HAS_SME */
	{ ".uleb128 .Lexpr_end4 - .Lexpr_start3\n.Lexpr_start3:\n.Lexpr_end4:", true }, /* AS_HAS_NON_CONST_ULEB128 */
	{ NULL, false }
};

static const struct vccd_capabilities_probe_source vccd_capabilities_c_sources[] = {
	/* arm64 Makefile: "K" takes a 32-bit logical immediate, 0xffffffff is not one */
	{ "int main(void) { asm volatile(\"and w0, w0, %w0\" :: \"K\" (4294967295)); return 0; }", false },
	{ "__attribute__((no_profile_instrument_function)) int x();", true },
	{ "char tag[][4] __attribute__((__nonstring__)) = { };", true },
	{ "int foo(int *x) { asm goto (\".long (%l[bar]) - .\": \"+m\"(*x) ::: bar); return *x; bar: return 0; }", true },
	{ "int foo(int x) { asm goto (\"\": \"=r\"(x) ::: bar); return x; bar: return 0; }", true },
	{ "void foo(void) { asm inline (\"\"); }", true },
	{ NULL, false }
};

static int vccd_capabilities_probe_lookup(const struct vccd_capabilities_probe_source *t, const char *src, size_t n)
{
	while (n && src[n - 1] == '\n')
		n--;
	for (; t->src; t++)
		if (strlen(t->src) == n && !memcmp(t->src, src, n))
			return t->ok;
	return -1;
}

int vccd_capabilities_as_source_probe(const char *src, size_t n)
{
	return vccd_capabilities_probe_lookup(vccd_capabilities_as_sources, src, n);
}

int vccd_capabilities_c_source_probe(const char *src, size_t n)
{
	return vccd_capabilities_probe_lookup(vccd_capabilities_c_sources, src, n);
}

/* macros vcc predefines (with a value of its own) */
bool vccd_capabilities_macro_is_predefined(const char *def)
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
