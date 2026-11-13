/* The predefined macros of a job, as clang predefines them for the target, the standard and the flags. */
#include "driver-internal.h"
#include "../frontend/pp/ppdata-predef.inc"
#define PPDATA_X86_PREDEF
#include "../frontend/pp/ppdata-x86.inc"
#include "x86predefines.h"

/* -f[un]signed-char against the target's default, the math flags, -pthread */
static __attribute__((noinline, cold)) void predef_flags(buf *pre, int darwin, int uchar_def, int uchar, int fmath)
{
	if (uchar >= 0 && uchar != uchar_def)
		bstr(pre, uchar ? "#define __CHAR_UNSIGNED__ 1\n" : "#undef __CHAR_UNSIGNED__\n");
	if (fmath & 1)
		bstr(pre, "#undef __FINITE_MATH_ONLY__\n#define __FINITE_MATH_ONLY__ 1\n");
	if ((fmath & 2) && !darwin)	/* Darwin's default is already no errno */
		bstr(pre, "#define __NO_MATH_ERRNO__ 1\n");
	if ((fmath & 16) && darwin)
		bstr(pre, "#undef __NO_MATH_ERRNO__\n");
	if (fmath & 4)
		bstr(pre, "#define __FAST_MATH__ 1\n");
	if (fmath & 32)
		bstr(pre, "#define __ARM_FP_FAST 1\n");
	if (fmath & 8)
		bstr(pre, "#define _REENTRANT 1\n");
}

/* C23: char8_t and the %B/%b formats (out of line: the caller's loops stay as compiled) */
static __attribute__((noinline, cold)) void predef_c23(buf *pre, int darwin)
{
	/* char8_t and the %B/%b formats */
	static const char *const fmt[][2] = {
		{ "SIZE", "l" }, { "UINT16", "h" }, { "UINT32", "" }, { "UINT64", "l" }, { "UINT8", "hh" },
		{ "UINTMAX", "l" }, { "UINTPTR", "l" }, { "UINT_FAST16", "h" }, { "UINT_FAST32", "" },
		{ "UINT_FAST64", "l" }, { "UINT_FAST8", "hh" }, { "UINT_LEAST16", "h" },
		{ "UINT_LEAST32", "" }, { "UINT_LEAST64", "l" }, { "UINT_LEAST8", "hh" },
	};
	char b[96];

	bstr(pre, "#define __CHAR8_TYPE__ unsigned char\n#define __CLANG_ATOMIC_CHAR8_T_LOCK_FREE 2\n"
		   "#define __GCC_ATOMIC_CHAR8_T_LOCK_FREE 2\n");
	for (size_t k = 0; k < sizeof(fmt) / sizeof(*fmt); k++) {
		/* Darwin's 64-bit types are long long */
		const char *l = darwin && strstr(fmt[k][0], "64") ? "ll" : fmt[k][1];

		snprintf(b, sizeof(b), "#define __%s_FMTB__ \"%sB\"\n#define __%s_FMTb__ \"%sb\"\n",
			 fmt[k][0], l, fmt[k][0], l);
		bstr(pre, b);
	}
}

VPRIV const char *const *driver_predef_darwin_sysdirs(size_t *n)
{
	*n = sizeof(sysdirs_darwin) / sizeof(sysdirs_darwin[0]);
	return sysdirs_darwin;
}

VPRIV const char *const *driver_predef_darwin_fwdirs(size_t *n)
{
	*n = sizeof(fwdirs_darwin) / sizeof(fwdirs_darwin[0]);
	return fwdirs_darwin;
}

/* the -dD order and the predefine text of the job */
VPRIV void driver_predef_build(struct driver_job *job)
{
	job->po.dd_order = job->darwin ? predef_order_darwin : job->wasm == 64 ? predef_order_wasm64 : job->wasm ? predef_order_wasm32
		    : job->x86 ? predef_order_x86_64 : predef_order_linux;
	job->po.ndd_order = job->x86 ? (int)(sizeof(predef_order_x86_64) / sizeof(*predef_order_x86_64))
		     : job->darwin ? (int)(sizeof(predef_order_darwin) / sizeof(*predef_order_darwin))
		     : job->wasm == 64 ? (int)(sizeof(predef_order_wasm64) / sizeof(*predef_order_wasm64))
		     : job->wasm ? (int)(sizeof(predef_order_wasm32) / sizeof(*predef_order_wasm32))
			    : (int)(sizeof(predef_order_linux) / sizeof(*predef_order_linux));

	if (job->undef)
		bstr(&job->pre, job->asm_mode ? predef_undef_asm : predef_undef_c);
	else
		bstr(&job->pre, job->darwin ? predef_darwin : job->wasm == 64 ? predef_wasm64 : job->wasm ? predef_wasm32
			 : job->x86 ? predef_x86_64_linux : predef_aarch64_linux);
	if (job->asm_mode && !job->undef)
		bstr(&job->pre, "#define __ASSEMBLER__ 1\n#undef __STDC_VERSION__\n#define __STDC_VERSION__ 201710L\n");
	else if (!job->undef && job->std) {
		/* the tables are for -std=gnu11 (the kernel's); other standards as clang predefines them */
		static const char *const ver[] = { [89] = NULL, [99] = "199901L", [11] = "201112L", [17] = "201710L", [23] = "202311L" };

		bstr(&job->pre, "#undef __STDC_VERSION__\n");
		if (job->stdver || ver[job->std]) {
			bstr(&job->pre, "#define __STDC_VERSION__ ");
			bstr(&job->pre, job->stdver ? job->stdver : ver[job->std]);
			bstr(&job->pre, "\n");
		}
		if (job->std == 89)
			bstr(&job->pre, "#undef __GNUC_STDC_INLINE__\n#define __GNUC_GNU_INLINE__ 1\n");
		if (job->strict)
			bstr(&job->pre, "#define __STRICT_ANSI__ 1\n#undef linux\n#undef unix\n");
		if (job->std == 23)
			predef_c23(&job->pre, job->darwin);
	} else if (!job->undef && !job->asm_mode) {
		bstr(&job->pre, "#undef __STDC_VERSION__\n#define __STDC_VERSION__ 201710L\n");
	}
	job->po.target = job->darwin ? PP_DARWIN : job->wasm == 64 ? PP_WASM64 : job->wasm ? PP_WASM32 : job->x86 ? PP_LINUX_X86_64 : PP_LINUX_AARCH64;
	if (job->opt == 0 && !job->undef) bstr(&job->pre, "#undef __OPTIMIZE__\n#define __NO_INLINE__ 1\n");
	if (job->os && !job->undef) bstr(&job->pre, "#define __OPTIMIZE_SIZE__ 1\n");
	if ((job->pie || job->pic) && !job->undef) {
		char b[160];
		int l = job->pie ? job->pie : job->pic;

		snprintf(b, sizeof(b), "#define __PIC__ %d\n#define __pic__ %d\n", l, l);
		bstr(&job->pre, b);
		if (job->pie) {
			snprintf(b, sizeof(b), "#define __PIE__ %d\n#define __pie__ %d\n", l, l);
			bstr(&job->pre, b);
		}
	}
	if (job->gnu89_inline < 0)
		job->gnu89_inline = job->std == 89;
	else if (!job->undef && !job->asm_mode)
		bstr(&job->pre, job->gnu89_inline ? "#undef __GNUC_STDC_INLINE__\n#undef __GNUC_GNU_INLINE__\n#define __GNUC_GNU_INLINE__ 1\n"
					: "#undef __GNUC_GNU_INLINE__\n#undef __GNUC_STDC_INLINE__\n#define __GNUC_STDC_INLINE__ 1\n");
	if (job->freestanding) bstr(&job->pre, "#undef __STDC_HOSTED__\n#define __STDC_HOSTED__ 0\n");
	if (job->lnx && !job->short_wchar && !job->undef)	/* the Linux table is for the kernel's -fshort-wchar */
		bstr(&job->pre, "#undef __ARM_SIZEOF_WCHAR_T\n#define __ARM_SIZEOF_WCHAR_T 4\n"
			   "#undef __SIZEOF_WCHAR_T__\n#define __SIZEOF_WCHAR_T__ 4\n"
			   "#undef __WCHAR_MAX__\n#define __WCHAR_MAX__ 4294967295U\n"
			   "#undef __WCHAR_MIN__\n#define __WCHAR_MIN__ 0U\n"
			   "#undef __WCHAR_TYPE__\n#define __WCHAR_TYPE__ unsigned int\n"
			   "#undef __WCHAR_WIDTH__\n#define __WCHAR_WIDTH__ 32\n"
			   "#undef __clang_wide_literal_encoding__\n#define __clang_wide_literal_encoding__ \"UTF-32\"\n");
	if (!job->undef && (job->fmath || (job->uchar >= 0 && job->uchar != job->lnx)))	/* char is unsigned by default on aarch64 Linux only */
		predef_flags(&job->pre, job->darwin, job->lnx, job->uchar, job->x86 ? job->fmath & ~32 : job->fmath);	/* no __ARM_FP_FAST on x86 */
	if (job->darwin && job->macos_min && !job->undef) {
		/* clang: MMmmpp (10.x before 10.10: 10mp) */
		unsigned maj = 0, min = 0, pat = 0, vv;
		char b[320];

		sscanf(job->macos_min, "%u.%u.%u", &maj, &min, &pat);
		vv = maj == 10 && min < 10 ? 1000 + min * 10 + (pat > 9 ? 9 : pat) : maj * 10000 + min * 100 + pat;
		snprintf(b, sizeof(b), "#undef __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__\n"
			 "#define __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ %u\n"
			 "#undef __ENVIRONMENT_OS_VERSION_MIN_REQUIRED__\n#define __ENVIRONMENT_OS_VERSION_MIN_REQUIRED__ %u\n",
			 vv, vv);
		bstr(&job->pre, b);
	}
	if (job->x86 && job->short_wchar && !job->undef)
		bstr(&job->pre, "#undef __SIZEOF_WCHAR_T__\n#define __SIZEOF_WCHAR_T__ 2\n"
			   "#undef __WCHAR_MAX__\n#define __WCHAR_MAX__ 65535\n"
			   "#undef __WCHAR_MIN__\n#define __WCHAR_MIN__ 0\n"
			   "#undef __WCHAR_TYPE__\n#define __WCHAR_TYPE__ unsigned short\n"
			   "#define __WCHAR_UNSIGNED__ 1\n"
			   "#undef __WCHAR_WIDTH__\n#define __WCHAR_WIDTH__ 16\n"
			   "#undef __clang_wide_literal_encoding__\n#define __clang_wide_literal_encoding__ \"UTF-16\"\n");
	if (job->darwin && job->short_wchar && !job->undef)
		bstr(&job->pre, "#undef __ARM_SIZEOF_WCHAR_T\n#define __ARM_SIZEOF_WCHAR_T 2\n"
			   "#undef __SIZEOF_WCHAR_T__\n#define __SIZEOF_WCHAR_T__ 2\n"
			   "#undef __WCHAR_MAX__\n#define __WCHAR_MAX__ 65535\n"
			   "#undef __WCHAR_MIN__\n#define __WCHAR_MIN__ 0\n"
			   "#undef __WCHAR_TYPE__\n#define __WCHAR_TYPE__ unsigned short\n"
			   "#define __WCHAR_UNSIGNED__ 1\n"
			   "#undef __WCHAR_WIDTH__\n#define __WCHAR_WIDTH__ 16\n"
			   "#undef __clang_wide_literal_encoding__\n#define __clang_wide_literal_encoding__ \"UTF-16\"\n");
	if (job->darwin && job->gregs && !job->undef)	/* the Darwin table has Apple's FP/SIMD features */
		bstr(&job->pre, "#undef __ARM_FEATURE_AES\n#undef __ARM_FEATURE_CRYPTO\n#undef __ARM_FEATURE_DOTPROD\n"
			   "#undef __ARM_FEATURE_FP16_FML\n#undef __ARM_FEATURE_FP16_SCALAR_ARITHMETIC\n"
			   "#undef __ARM_FEATURE_FP16_VECTOR_ARITHMETIC\n#undef __ARM_FEATURE_SHA2\n"
			   "#undef __ARM_FEATURE_SHA3\n#undef __ARM_FEATURE_SHA512\n#undef __ARM_FP\n"
			   "#undef __ARM_NEON\n#undef __ARM_NEON_FP\n");
	if (job->x86 && !job->undef) {
		x86_feature_predefines(&job->pre, job->x86_features_, job->x86_cpu);
		/* clang: -mgeneral-regs-only is -mno-sse -mno-mmx; without SSE2 no _Float16 either */
		if (job->gregs || job->nosse)
			bstr(&job->pre, "#undef __SSE__\n#undef __SSE_MATH__\n");
		if (job->gregs || job->nosse || job->nosse2)
			bstr(&job->pre, "#undef __SSE2__\n#undef __SSE2_MATH__\n#undef __FLT16_DECIMAL_DIG__\n#undef __FLT16_DENORM_MIN__\n"
				   "#undef __FLT16_DIG__\n#undef __FLT16_EPSILON__\n#undef __FLT16_HAS_DENORM__\n"
				   "#undef __FLT16_HAS_INFINITY__\n#undef __FLT16_HAS_QUIET_NAN__\n#undef __FLT16_MANT_DIG__\n"
				   "#undef __FLT16_MAX_10_EXP__\n#undef __FLT16_MAX__\n#undef __FLT16_MAX_EXP__\n"
				   "#undef __FLT16_MIN_10_EXP__\n#undef __FLT16_MIN__\n#undef __FLT16_MIN_EXP__\n#undef __FLT16_NORM_MAX__\n");
		if (job->gregs || job->nommx)
			bstr(&job->pre, "#undef __MMX__\n");
	}
	if (job->cfi && !job->undef) bstr(&job->pre, "#define __GCC_HAVE_DWARF2_CFI_ASM 1\n");
	if (job->darwin && job->freestanding && !job->undef) bstr(&job->pre, "#undef __GCC_HAVE_DWARF2_CFI_ASM\n");
	/* the Linux table is for the kernel's -mgeneral-regs-only */
	if (job->lnx && !job->gregs && !job->undef)
		bstr(&job->pre, "#define __ARM_FP 0xE\n#define __ARM_NEON 1\n#define __ARM_NEON_FP 0xE\n");
	/* -march wins over -mcpu (clang); Darwin: Apple cores, LSE always */
	job->lse = job->darwin ? 1 : job->march_lse >= 0 ? job->march_lse : job->mcpu_lse >= 0 ? job->mcpu_lse : 0;
	if (job->lnx && job->lse && !job->undef)
		bstr(&job->pre, "#define __ARM_FEATURE_ATOMICS 1\n");
	if (job->strict_align) bstr(&job->pre, "#undef __ARM_FEATURE_UNALIGNED\n");
	if (job->tiny) bstr(&job->pre, "#undef __AARCH64_CMODEL_SMALL__\n#define __AARCH64_CMODEL_TINY__ 1\n");
}
