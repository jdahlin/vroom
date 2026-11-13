/* x86-64 assembler: x87 floating point */
#include "x64as.h"

VPRIV int x64_i_x87(as_context *a, char *mn, XOp *o, int nops)
{
	char stem[32];
	int sz = 0, cc;

	(void)a, (void)mn, (void)o, (void)nops, (void)stem, (void)sz, (void)cc;
	/* ---- x87 ---- */
	if (mn[0] == 'f') {
		static const struct { const char *n; uint8_t op, d; } fm[] = {
			{ "fldt", 0xdb, 5 }, { "fstpt", 0xdb, 7 }, { "fldl", 0xdd, 0 }, { "flds", 0xd9, 0 }, { "fstpl", 0xdd, 3 },
			{ "fstps", 0xd9, 3 }, { "fstl", 0xdd, 2 }, { "fsts", 0xd9, 2 }, { "fildll", 0xdf, 5 }, { "fildq", 0xdf, 5 },
			{ "fildl", 0xdb, 0 }, { "filds", 0xdf, 0 }, { "fistpll", 0xdf, 7 }, { "fistpq", 0xdf, 7 }, { "fistpl", 0xdb, 3 },
			{ "fistps", 0xdf, 3 }, { "fnstcw", 0xd9, 7 }, { "fldcw", 0xd9, 5 }, { "fisttpll", 0xdd, 1 }, { "fadds", 0xd8, 0 },
			{ "faddl", 0xdc, 0 }, { "fmuls", 0xd8, 1 }, { "fmull", 0xdc, 1 }, { "fsubs", 0xd8, 4 }, { "fsubl", 0xdc, 4 },
			{ "fdivs", 0xd8, 6 }, { "fdivl", 0xdc, 6 }, { "fnstsw", 0xdd, 7 },
		};

		if (nops == 1 && o[0].kind != XK_REG) {
			if (o[0].kind == XK_EXPR)
				o[0].kind = XK_MEM;
			for (size_t i = 0; i < sizeof fm / sizeof *fm; i++)
				if (!strcmp(mn, fm[i].n))
					return x64_x87m(a, fm[i].op, fm[i].d, &o[0]);
		}
		if (o[0].kind == XK_REG && o[0].cls == XR_ST) {
			int i = o[0].reg;

			if (!strcmp(mn, "fstp") && nops == 1) return x64_x87r(a, 0xdd, 0xd8, i);
			if (!strcmp(mn, "fst") && nops == 1) return x64_x87r(a, 0xdd, 0xd0, i);
			if (!strcmp(mn, "fld") && nops == 1) return x64_x87r(a, 0xd9, 0xc0, i);
			if (!strcmp(mn, "fxch") && nops == 1) return x64_x87r(a, 0xd9, 0xc8, i);
			if (!strcmp(mn, "fucomip") || !strcmp(mn, "fucomi") || !strcmp(mn, "fcomip") || !strcmp(mn, "fcomi") ||
			    !strcmp(mn, "fucompi") || !strcmp(mn, "fcompi")) {
				int u = mn[1] == 'u', pop = strchr(mn + 4, 'p') != NULL;

				return x64_x87r(a, pop ? 0xdf : 0xdb, u ? 0xe8 : 0xf0, i);
			}
			if (nops == 2 && o[1].kind == XK_REG && o[1].cls == XR_ST) {
				/* faddp %st, %st(i) ... (AT&T keeps the swapped fsub/fdiv names) */
				static const struct { const char *n; uint8_t op, base; } fp2[] = {
					{ "faddp", 0xde, 0xc0 }, { "fmulp", 0xde, 0xc8 }, { "fsubp", 0xde, 0xe0 }, { "fsubrp", 0xde, 0xe8 },
					{ "fdivp", 0xde, 0xf0 }, { "fdivrp", 0xde, 0xf8 },
				};

				for (size_t k = 0; k < sizeof fp2 / sizeof *fp2; k++)
					if (!strcmp(mn, fp2[k].n))
						return x64_x87r(a, fp2[k].op, fp2[k].base, o[1].reg);
			}
		}
	}
	return X64_NEXT;
}

VPRIV int x64_i_x87ar(as_context *a, char *mn, XOp *o, int nops)
{
	char stem[32];
	int sz = 0, cc;

	(void)a, (void)mn, (void)o, (void)nops, (void)stem, (void)sz, (void)cc;
	/* ---- x87 arithmetic with st registers and integer memory, fcmov ---- */
	if (mn[0] == 'f') {
		static const char *const ar[4] = { "fadd", "fmul", "fsub", "fdiv" };
		static const uint8_t base0[4] = { 0xc0, 0xc8, 0xe0, 0xf0 };	/* op %st(i), %st */

		for (int k = 0; k < 4; k++) {
			size_t l = strlen(ar[k]);

			if (!strncmp(mn, ar[k], l) && mn[l] == 'r' && !mn[l + 1] && nops == 2 && o[0].cls == XR_ST && o[1].cls == XR_ST) {
				/* fsubr/fdivr %st(i), %st and %st, %st(i) */
				if (o[1].reg == 0)
					return x64_x87r(a, 0xd8, base0[k] + 8, o[0].reg);
				return x64_x87r(a, 0xdc, base0[k] + 8, o[1].reg);
			}
			if (!strcmp(mn, ar[k]) && nops == 2 && o[0].kind == XK_REG && o[0].cls == XR_ST && o[1].cls == XR_ST) {
				if (o[1].reg == 0)
					return x64_x87r(a, 0xd8, base0[k], o[0].reg);
				return x64_x87r(a, 0xdc, base0[k], o[1].reg);
			}
			if (!strcmp(mn, ar[k]) && nops == 1 && o[0].kind == XK_REG && o[0].cls == XR_ST)
				return x64_x87r(a, 0xd8, base0[k], o[0].reg);
		}
		{
			static const struct { const char *n; uint8_t op, d; } im[] = {
				{ "fiaddl", 0xda, 0 }, { "fimull", 0xda, 1 }, { "fisubl", 0xda, 4 }, { "fisubrl", 0xda, 5 }, { "fidivl", 0xda, 6 },
				{ "fidivrl", 0xda, 7 }, { "fiadds", 0xde, 0 }, { "fimuls", 0xde, 1 }, { "fisubs", 0xde, 4 }, { "fidivs", 0xde, 6 },
				{ "fsubrs", 0xd8, 5 }, { "fsubrl", 0xdc, 5 }, { "fdivrs", 0xd8, 7 }, { "fdivrl", 0xdc, 7 },
				{ "fcoms", 0xd8, 2 }, { "fcoml", 0xdc, 2 }, { "fcomps", 0xd8, 3 }, { "fcompl", 0xdc, 3 },
				{ "fisttpl", 0xdb, 1 }, { "fisttps", 0xdf, 1 }, { "fnstenv", 0xd9, 6 }, { "fldenv", 0xd9, 4 },
			};

			if (nops == 1 && o[0].kind != XK_REG) {
				if (o[0].kind == XK_EXPR)
					o[0].kind = XK_MEM;
				for (size_t i = 0; i < sizeof im / sizeof *im; i++)
					if (!strcmp(mn, im[i].n))
						return x64_x87m(a, im[i].op, im[i].d, &o[0]);
			}
		}
		if (!strncmp(mn, "fcmov", 5) && nops == 2 && o[0].cls == XR_ST) {
			static const struct { const char *n; uint8_t op, base; } cm[] = {
				{ "b", 0xda, 0xc0 }, { "e", 0xda, 0xc8 }, { "be", 0xda, 0xd0 }, { "u", 0xda, 0xd8 },
				{ "nb", 0xdb, 0xc0 }, { "ne", 0xdb, 0xc8 }, { "nbe", 0xdb, 0xd0 }, { "nu", 0xdb, 0xd8 },
			};

			for (size_t i = 0; i < sizeof cm / sizeof *cm; i++)
				if (!strcmp(mn + 5, cm[i].n))
					return x64_x87r(a, cm[i].op, cm[i].base, o[0].reg);
		}
	}
	return X64_NEXT;
}
