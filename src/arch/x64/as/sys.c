/* sys.c: x86-64 assembler, the system and control families: x64_i_crdr (mov to and from control,
 * debug and segment registers), x64_i_zero (instructions without operands, from one table: ret,
 * leave, the fences, ...) and x64_i_sys (system and cache instructions on one memory or register
 * operand, the kernel's).
 *
 * Each returns X64_NEXT for a mnemonic that is not theirs; the order in which asm.c tries the
 * families is in x64as.h.  Segment registers are carried as their prefix bytes (0x26 ... 0x65), not
 * as architectural indices.  The bytes equal LLVM MC's (bench/vas-enc.py); a bad operand shape
 * fails the assembly through as_error_report. */
#include "x64as.h"

VPRIV int x64_i_crdr(as_context *a, char *mn, XOp *o, int nops)
{
	char stem[32];
	int sz = 0, cc;

	(void)a, (void)mn, (void)o, (void)nops, (void)stem, (void)sz, (void)cc;

	/* mov to/from a segment register: 8e /r (to; MC: movw has no 0x66, movq REX.W), 8c /r (from) */
	if (nops == 2 && (!strcmp(mn, "mov") || !strcmp(mn, "movl") || !strcmp(mn, "movw") || !strcmp(mn, "movq")) &&
	    ((o[0].kind == XK_REG && o[0].cls == XR_SEG) || (o[1].kind == XK_REG && o[1].cls == XR_SEG))) {
		static const uint8_t pfx[6] = { 0x26, 0x2e, 0x36, 0x3e, 0x64, 0x65 };	/* es cs ss ds fs gs */
		bool to = o[1].kind == XK_REG && o[1].cls == XR_SEG;
		XOp *sr = to ? &o[1] : &o[0], *g = to ? &o[0] : &o[1];
		int sn = -1;

		for (int k = 0; k < 6; k++)
			if (pfx[k] == sr->reg)
				sn = k;
		if (sn < 0)
			return as_error_report(a, "invalid segment register");
		if (g->kind == XK_EXPR)
			g->kind = XK_MEM;
		if (to)
			return x64_emit(a, &F(.w = g->kind == XK_REG && g->size == 8, .op = OPB(0x8e), .nop = 1, .reg = sn, .rm = g));
		return x64_emit(a, &F(.w = g->kind == XK_REG && g->size == 8, .opsz16 = g->kind == XK_REG && g->size == 2,
				      .op = OPB(0x8c), .nop = 1, .reg = sn, .rm = g));
	}
	/* ---- mov to and from control and debug registers (always 64-bit) ---- */
	if (nops == 2 && (!strcmp(mn, "mov") || !strcmp(mn, "movq")) &&
	    ((o[0].kind == XK_REG && (o[0].cls == XR_CR || o[0].cls == XR_DR)) ||
	     (o[1].kind == XK_REG && (o[1].cls == XR_CR || o[1].cls == XR_DR)))) {
		bool to = o[1].cls == XR_CR || o[1].cls == XR_DR;
		XOp *sr = to ? &o[1] : &o[0], *g = to ? &o[0] : &o[1];
		uint8_t ops[2] = { 0x0f, (uint8_t)((sr->cls == XR_CR ? 0x20 : 0x21) | (to ? 2 : 0)) };

		if (g->kind != XK_REG || g->cls != XR_GPR || g->size != 8)
			return as_error_report(a, "invalid operand for instruction");
		return x64_emit(a, &F(.op = ops, .nop = 2, .reg = sr->reg, .rm = g));
	}
	return X64_NEXT;
}

VPRIV int x64_i_zero(as_context *a, char *mn, XOp *o, int nops)
{
	char stem[32];
	int sz = 0, cc;

	(void)a, (void)mn, (void)o, (void)nops, (void)stem, (void)sz, (void)cc;
	/* ---- no operands ---- */
	{
		static const struct { const char *n; uint8_t b[4]; int k; } zero[] = {
			{ "retq", { 0xc3 }, 1 }, { "ret", { 0xc3 }, 1 }, { "leave", { 0xc9 }, 1 }, { "leaveq", { 0xc9 }, 1 },
			{ "cltd", { 0x99 }, 1 }, { "cqto", { 0x48, 0x99 }, 2 }, { "cltq", { 0x48, 0x98 }, 2 }, { "cwtl", { 0x98 }, 1 },
			{ "cwtd", { 0x66, 0x99 }, 2 }, { "cbtw", { 0x66, 0x98 }, 2 }, { "nop", { 0x90 }, 1 }, { "ud2", { 0x0f, 0x0b }, 2 }, { "hlt", { 0xf4 }, 1 },
			{ "mfence", { 0x0f, 0xae, 0xf0 }, 3 }, { "lfence", { 0x0f, 0xae, 0xe8 }, 3 }, { "sfence", { 0x0f, 0xae, 0xf8 }, 3 },
			{ "pause", { 0xf3, 0x90 }, 2 }, { "int3", { 0xcc }, 1 }, { "cpuid", { 0x0f, 0xa2 }, 2 }, { "rdtsc", { 0x0f, 0x31 }, 2 },
			{ "movsb", { 0xa4 }, 1 }, { "stosb", { 0xaa }, 1 }, { "movsq", { 0x48, 0xa5 }, 2 }, { "stosq", { 0x48, 0xab }, 2 },
			{ "movsl", { 0xa5 }, 1 }, { "stosl", { 0xab }, 1 }, { "cld", { 0xfc }, 1 }, { "std", { 0xfd }, 1 },
			{ "fchs", { 0xd9, 0xe0 }, 2 }, { "fabs", { 0xd9, 0xe1 }, 2 }, { "fldz", { 0xd9, 0xee }, 2 }, { "fld1", { 0xd9, 0xe8 }, 2 },
			{ "fsqrt", { 0xd9, 0xfa }, 2 }, { "fpatan", { 0xd9, 0xf3 }, 2 }, { "fxch", { 0xd9, 0xc9 }, 2 }, { "fucompp", { 0xda, 0xe9 }, 2 },
			{ "faddp", { 0xde, 0xc1 }, 2 }, { "fmulp", { 0xde, 0xc9 }, 2 }, { "fsubrp", { 0xde, 0xe9 }, 2 }, { "fsubp", { 0xde, 0xe1 }, 2 },
			{ "fdivrp", { 0xde, 0xf9 }, 2 }, { "fdivp", { 0xde, 0xf1 }, 2 }, { "fwait", { 0x9b }, 1 }, { "wait", { 0x9b }, 1 },
			{ "endbr64", { 0xf3, 0x0f, 0x1e, 0xfa }, 4 }, { "syscall", { 0x0f, 0x05 }, 2 }, { "sahf", { 0x9e }, 1 }, { "lahf", { 0x9f }, 1 },
			{ "fninit", { 0xdb, 0xe3 }, 2 }, { "frndint", { 0xd9, 0xfc }, 2 }, { "fprem", { 0xd9, 0xf8 }, 2 }, { "fscale", { 0xd9, 0xfd }, 2 },
			/* system (the kernel) */
			{ "fsincos", { 0xd9, 0xfb }, 2 }, { "fsin", { 0xd9, 0xfe }, 2 }, { "fcos", { 0xd9, 0xff }, 2 }, { "fptan", { 0xd9, 0xf2 }, 2 },
			{ "f2xm1", { 0xd9, 0xf0 }, 2 }, { "fyl2x", { 0xd9, 0xf1 }, 2 }, { "fyl2xp1", { 0xd9, 0xf9 }, 2 }, { "fxtract", { 0xd9, 0xf4 }, 2 },
			{ "fprem1", { 0xd9, 0xf5 }, 2 }, { "fdecstp", { 0xd9, 0xf6 }, 2 }, { "fincstp", { 0xd9, 0xf7 }, 2 }, { "ftst", { 0xd9, 0xe4 }, 2 },
			{ "fxam", { 0xd9, 0xe5 }, 2 }, { "fldl2t", { 0xd9, 0xe9 }, 2 }, { "fldl2e", { 0xd9, 0xea }, 2 }, { "fldpi", { 0xd9, 0xeb }, 2 },
			{ "fldlg2", { 0xd9, 0xec }, 2 }, { "fldln2", { 0xd9, 0xed }, 2 }, { "fnop", { 0xd9, 0xd0 }, 2 },
			{ "fclex", { 0x9b, 0xdb, 0xe2 }, 3 }, { "finit", { 0x9b, 0xdb, 0xe3 }, 3 },
			{ "cli", { 0xfa }, 1 }, { "sti", { 0xfb }, 1 }, { "clc", { 0xf8 }, 1 }, { "stc", { 0xf9 }, 1 }, { "cmc", { 0xf5 }, 1 },
			{ "clac", { 0x0f, 0x01, 0xca }, 3 }, { "stac", { 0x0f, 0x01, 0xcb }, 3 }, { "swapgs", { 0x0f, 0x01, 0xf8 }, 3 },
			{ "pushf", { 0x9c }, 1 }, { "pushfq", { 0x9c }, 1 }, { "popf", { 0x9d }, 1 }, { "popfq", { 0x9d }, 1 },
			{ "rdmsr", { 0x0f, 0x32 }, 2 }, { "wrmsr", { 0x0f, 0x30 }, 2 }, { "rdpmc", { 0x0f, 0x33 }, 2 }, { "rdtscp", { 0x0f, 0x01, 0xf9 }, 3 },
			{ "wbinvd", { 0x0f, 0x09 }, 2 }, { "invd", { 0x0f, 0x08 }, 2 }, { "sysenter", { 0x0f, 0x34 }, 2 }, { "sysexit", { 0x0f, 0x35 }, 2 },
			{ "sysretq", { 0x48, 0x0f, 0x07 }, 3 }, { "sysretl", { 0x0f, 0x07 }, 2 }, { "iretq", { 0x48, 0xcf }, 2 }, { "iretl", { 0xcf }, 1 }, { "iret", { 0xcf }, 1 },
			{ "lretq", { 0x48, 0xcb }, 2 }, { "lret", { 0xcb }, 1 }, { "lretl", { 0xcb }, 1 }, { "ud2a", { 0x0f, 0x0b }, 2 }, { "ud1", { 0x0f, 0xb9 }, 2 },
			{ "vmcall", { 0x0f, 0x01, 0xc1 }, 3 }, { "vmmcall", { 0x0f, 0x01, 0xd9 }, 3 }, { "vmlaunch", { 0x0f, 0x01, 0xc2 }, 3 },
			{ "vmresume", { 0x0f, 0x01, 0xc3 }, 3 }, { "vmxoff", { 0x0f, 0x01, 0xc4 }, 3 }, { "vmrun", { 0x0f, 0x01, 0xd8 }, 3 },
			{ "rdpkru", { 0x0f, 0x01, 0xee }, 3 }, { "wrpkru", { 0x0f, 0x01, 0xef }, 3 }, { "xgetbv", { 0x0f, 0x01, 0xd0 }, 3 },
			{ "xsetbv", { 0x0f, 0x01, 0xd1 }, 3 }, { "monitor", { 0x0f, 0x01, 0xc8 }, 3 }, { "mwait", { 0x0f, 0x01, 0xc9 }, 3 },
			{ "serialize", { 0x0f, 0x01, 0xe8 }, 3 }, { "encls", { 0x0f, 0x01, 0xcf }, 3 }, { "enclu", { 0x0f, 0x01, 0xd7 }, 3 },
			{ "vzeroupper", { 0xc5, 0xf8, 0x77 }, 3 }, { "vzeroall", { 0xc5, 0xfc, 0x77 }, 3 }, { "int1", { 0xf1 }, 1 },
			{ "insb", { 0x6c }, 1 }, { "insw", { 0x66, 0x6d }, 2 }, { "insl", { 0x6d }, 1 },
			{ "outsb", { 0x6e }, 1 }, { "outsw", { 0x66, 0x6f }, 2 }, { "outsl", { 0x6f }, 1 },
			{ "movsw", { 0x66, 0xa5 }, 2 }, { "stosw", { 0x66, 0xab }, 2 }, { "lodsb", { 0xac }, 1 }, { "lodsq", { 0x48, 0xad }, 2 },
			{ "scasb", { 0xae }, 1 }, { "cmpsb", { 0xa6 }, 1 }, { "xlat", { 0xd7 }, 1 }, { "pconfig", { 0x0f, 0x01, 0xc5 }, 3 },
			{ "stui", { 0xf3, 0x0f, 0x01, 0xef }, 4 }, { "clui", { 0xf3, 0x0f, 0x01, 0xee }, 4 }, { "uiret", { 0xf3, 0x0f, 0x01, 0xec }, 4 },
			{ "endbr32", { 0xf3, 0x0f, 0x1e, 0xfb }, 4 }, { "saveprevssp", { 0xf3, 0x0f, 0x01, 0xea }, 4 }, { "setssbsy", { 0xf3, 0x0f, 0x01, 0xe8 }, 4 },
			{ "eretu", { 0xf3, 0x0f, 0x01, 0xca }, 4 }, { "erets", { 0xf2, 0x0f, 0x01, 0xca }, 4 }, { "tdcall", { 0x66, 0x0f, 0x01, 0xcc }, 4 },
			{ "seamcall", { 0x66, 0x0f, 0x01, 0xcf }, 4 }, { "hreset", { 0xf3, 0x0f, 0x3a, 0xf0 }, 4 },
		};

		if (!nops)
			for (size_t i = 0; i < sizeof zero / sizeof *zero; i++)
				if (!strcmp(mn, zero[i].n))
					return x64_raw(a, zero[i].b, zero[i].k);
	}
	return X64_NEXT;
}

VPRIV int x64_i_sys(as_context *a, char *mn, XOp *o, int nops)
{
	char stem[32];
	int sz = 0, cc;

	(void)a, (void)mn, (void)o, (void)nops, (void)stem, (void)sz, (void)cc;
	/* ---- system and cache instructions on one memory or register operand (the kernel) ---- */
	{
		static const struct { const char *n; uint8_t pre, op1, op2, d, w, reg; } sm[] = {
			{ "clflush", 0, 0x0f, 0xae, 7, 0, 0 }, { "clflushopt", 0x66, 0x0f, 0xae, 7, 0, 0 }, { "clwb", 0x66, 0x0f, 0xae, 6, 0, 0 },
			{ "fxsave", 0, 0x0f, 0xae, 0, 0, 0 }, { "fxrstor", 0, 0x0f, 0xae, 1, 0, 0 }, { "fxsaveq", 0, 0x0f, 0xae, 0, 1, 0 },
			{ "fxrstorq", 0, 0x0f, 0xae, 1, 1, 0 }, { "fxsave64", 0, 0x0f, 0xae, 0, 1, 0 }, { "fxrstor64", 0, 0x0f, 0xae, 1, 1, 0 },
			{ "ldmxcsr", 0, 0x0f, 0xae, 2, 0, 0 }, { "stmxcsr", 0, 0x0f, 0xae, 3, 0, 0 },
			{ "xsave", 0, 0x0f, 0xae, 4, 0, 0 }, { "xrstor", 0, 0x0f, 0xae, 5, 0, 0 }, { "xsaveopt", 0, 0x0f, 0xae, 6, 0, 0 },
			{ "xsave64", 0, 0x0f, 0xae, 4, 1, 0 }, { "xrstor64", 0, 0x0f, 0xae, 5, 1, 0 }, { "xsaveopt64", 0, 0x0f, 0xae, 6, 1, 0 },
			{ "xsaves", 0, 0x0f, 0xc7, 5, 0, 0 }, { "xrstors", 0, 0x0f, 0xc7, 3, 0, 0 }, { "xsavec", 0, 0x0f, 0xc7, 4, 0, 0 },
			{ "xsaves64", 0, 0x0f, 0xc7, 5, 1, 0 }, { "xrstors64", 0, 0x0f, 0xc7, 3, 1, 0 }, { "xsavec64", 0, 0x0f, 0xc7, 4, 1, 0 },
			{ "lgdt", 0, 0x0f, 0x01, 2, 0, 0 }, { "lidt", 0, 0x0f, 0x01, 3, 0, 0 }, { "sgdt", 0, 0x0f, 0x01, 0, 0, 0 },
			{ "sidt", 0, 0x0f, 0x01, 1, 0, 0 }, { "lgdtq", 0, 0x0f, 0x01, 2, 0, 0 }, { "lidtq", 0, 0x0f, 0x01, 3, 0, 0 },
			{ "sgdtq", 0, 0x0f, 0x01, 0, 0, 0 }, { "sidtq", 0, 0x0f, 0x01, 1, 0, 0 }, { "invlpg", 0, 0x0f, 0x01, 7, 0, 0 },
			{ "lmsw", 0, 0x0f, 0x01, 6, 0, 1 }, { "smsw", 0, 0x0f, 0x01, 4, 0, 1 },
			{ "lldt", 0, 0x0f, 0x00, 2, 0, 1 }, { "ltr", 0, 0x0f, 0x00, 3, 0, 1 }, { "sldt", 0, 0x0f, 0x00, 0, 0, 1 }, { "str", 0, 0x0f, 0x00, 1, 0, 1 },
			{ "verw", 0, 0x0f, 0x00, 5, 0, 1 }, { "verr", 0, 0x0f, 0x00, 4, 0, 1 },
			{ "cmpxchg16b", 0, 0x0f, 0xc7, 1, 1, 0 }, { "cmpxchg8b", 0, 0x0f, 0xc7, 1, 0, 0 },
			{ "vmptrld", 0, 0x0f, 0xc7, 6, 0, 0 }, { "vmptrst", 0, 0x0f, 0xc7, 7, 0, 0 }, { "vmclear", 0x66, 0x0f, 0xc7, 6, 0, 0 },
			{ "vmxon", 0xf3, 0x0f, 0xc7, 6, 0, 0 }, { "clrssbsy", 0xf3, 0x0f, 0xae, 6, 0, 0 },
		};

		if (nops == 1)
			for (size_t i = 0; i < sizeof sm / sizeof *sm; i++)
				if (!strcmp(mn, sm[i].n)) {
					uint8_t ops[2] = { sm[i].op1, sm[i].op2 };

					if (o[0].kind == XK_EXPR)
						o[0].kind = XK_MEM;
					if (o[0].kind != XK_MEM && !(sm[i].reg && o[0].kind == XK_REG && o[0].cls == XR_GPR))
						return as_error_report(a, "invalid operand for instruction");
					return x64_emit(a, &F(.w = sm[i].w, .opsz16 = sm[i].pre == 0x66, .rep = sm[i].pre == 0xf3 ? 0xf3 : 0,
							      .op = ops, .nop = 2, .reg = sm[i].d, .rm = &o[0]));
				}
		/* rdrand/rdseed/rdpid reg */
		if (nops == 1 && o[0].kind == XK_REG && o[0].cls == XR_GPR &&
		    (!strncmp(mn, "rdrand", 6) || !strncmp(mn, "rdseed", 6) || !strcmp(mn, "rdpid"))) {
			uint8_t ops[2] = { 0x0f, 0xc7 };
			bool pid = mn[2] == 'p';
			int z = o[0].size;

			return x64_emit(a, &F(.w = !pid && z == 8, .opsz16 = z == 2, .rep = pid ? 0xf3 : 0, .op = ops, .nop = 2,
					      .reg = mn[2] == 's' || pid ? 7 : 6, .rm = &o[0]));
		}
		/* movnti reg, mem */
		if (nops == 2 && !strncmp(mn, "movnti", 6) && o[0].kind == XK_REG && o[0].cls == XR_GPR) {
			uint8_t ops[2] = { 0x0f, 0xc3 };

			if (o[1].kind == XK_EXPR)
				o[1].kind = XK_MEM;
			return x64_emit(a, &F(.w = o[0].size == 8, .op = ops, .nop = 2, .reg = o[0].reg, .rm = &o[1]));
		}
		/* lsl/lar rm, reg */
		if (nops == 2 && (!strncmp(mn, "lsl", 3) || !strncmp(mn, "lar", 3)) && strlen(mn) <= 4 && o[1].kind == XK_REG) {
			uint8_t ops[2] = { 0x0f, mn[1] == 's' ? 0x03 : 0x02 };

			if (o[0].kind == XK_EXPR)
				o[0].kind = XK_MEM;
			return x64_emit(a, &F(.w = o[1].size == 8, .opsz16 = o[1].size == 2, .op = ops, .nop = 2, .reg = o[1].reg, .rm = &o[0]));
		}
		/* in/out with %dx or an imm8 port */
		if ((!strncmp(mn, "in", 2) || !strncmp(mn, "out", 3)) && nops == 2) {
			bool out = mn[0] == 'o';
			XOp *port = out ? &o[1] : &o[0], *acc = out ? &o[0] : &o[1];
			size_t l = out ? 3 : 2;
			char sf = mn[l];

			if (acc->kind == XK_REG && acc->cls == XR_GPR && acc->reg == 0 && (!sf || (sf == 'b' && acc->size == 1) ||
			    (sf == 'w' && acc->size == 2) || (sf == 'l' && acc->size == 4)) && (!sf || !mn[l + 1])) {
				uint8_t b[3];
				int n = 0;

				if (acc->size == 2)
					b[n++] = 0x66;
				if (port->kind == XK_REG && port->cls == XR_GPR && port->reg == 2 && port->size == 2) {
					b[n++] = (uint8_t)((out ? 0xee : 0xec) | (acc->size != 1));
					return x64_raw(a, b, n);
				}
				if (port->kind == XK_IMM && port->hasv && !port->v.add && !port->v.sub) {
					b[n++] = (uint8_t)((out ? 0xe6 : 0xe4) | (acc->size != 1));
					b[n++] = (uint8_t)port->v.c;
					return x64_raw(a, b, n);
				}
			}
		}
		/* CET shadow stack: wrss[dq]/wruss[dq] reg, mem; incssp/rdssp[dq] reg */
		if (nops == 2 && (!strncmp(mn, "wrss", 4) || !strncmp(mn, "wruss", 5)) && o[0].kind == XK_REG && o[0].cls == XR_GPR) {
			bool u = mn[2] == 'u';
			uint8_t ops[3] = { 0x0f, 0x38, (uint8_t)(u ? 0xf5 : 0xf6) };

			if (o[1].kind == XK_EXPR)
				o[1].kind = XK_MEM;
			return x64_emit(a, &F(.w = o[0].size == 8, .opsz16 = u, .op = ops, .nop = 3, .reg = o[0].reg, .rm = &o[1]));
		}
		if (nops == 1 && (!strncmp(mn, "incssp", 6) || !strncmp(mn, "rdssp", 5)) && o[0].kind == XK_REG && o[0].cls == XR_GPR) {
			bool inc = mn[0] == 'i';
			uint8_t ops[2] = { 0x0f, (uint8_t)(inc ? 0xae : 0x1e) };

			return x64_emit(a, &F(.w = o[0].size == 8, .rep = 0xf3, .op = ops, .nop = 2, .reg = inc ? 5 : 1, .rm = &o[0]));
		}
		/* invpcid m128, r64: 66 0f 38 82 /r */
		if (nops == 2 && !strcmp(mn, "invpcid") && o[1].kind == XK_REG && o[1].cls == XR_GPR) {
			uint8_t ops[3] = { 0x0f, 0x38, 0x82 };

			if (o[0].kind == XK_EXPR)
				o[0].kind = XK_MEM;
			return x64_emit(a, &F(.opsz16 = 1, .op = ops, .nop = 3, .reg = o[1].reg, .rm = &o[0]));
		}
		/* ret/lret $imm16 */
		if (nops == 1 && o[0].kind == XK_IMM && !o[0].v.add && !o[0].v.sub &&
		    (!strcmp(mn, "ret") || !strcmp(mn, "retq") || !strcmp(mn, "lret") || !strcmp(mn, "lretl") || !strcmp(mn, "lretq"))) {
			uint8_t b[4], n = 0;

			if (!strcmp(mn, "lretq"))
				b[n++] = 0x48;
			b[n++] = mn[0] == 'l' ? 0xca : 0xc2;
			b[n++] = (uint8_t)o[0].v.c, b[n++] = (uint8_t)(o[0].v.c >> 8);
			return x64_raw(a, b, n);
		}
		/* int $n */
		if (!strcmp(mn, "int") && nops == 1 && o[0].kind == XK_IMM && !o[0].v.add && !o[0].v.sub) {
			uint8_t b[2] = { 0xcd, (uint8_t)o[0].v.c };

			if (o[0].v.c == 3)	/* (MC: int $3 is int3) */
				return x64_raw(a, (const uint8_t[]){ 0xcc }, 1);
			return x64_raw(a, b, 2);
		}
	}
	return X64_NEXT;
}
