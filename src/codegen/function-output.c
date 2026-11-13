/* vcc code generator: handing a finished function's text to the unit, symbol headers and constructor entries */
#include "emitimpl.h"

VPRIV void out_chunk(ecx *e, uint64_t key, size_t start)
{
	emitter *E = e->E;

	pthread_mutex_lock(&E->cmu);
	E->chunks[E->nchunk++] = (chunk){ key, e->out.p + start, e->out.n - start, e->rlog + e->rlog0,
					  (uint32_t)((e->nrlog - e->rlog0) / 2) };
	pthread_mutex_unlock(&E->cmu);
}

VPRIV void sym_header(ecx *e, tbuf *b, uint32_t s, bool fn, const char *defsec, uint32_t align)
{
	cc *c = e->c;
	const sym *y = &c->syms[s];
	const attrs *a = sym_attrs(c, s);
	bool global = y->sc != SC_STATIC && y->depth == 0 && y->name;
	size_t h = it_open(e, IT_TEXT);

	if (a->section) {
		const char *sn = c->strs[a->section - 1].s;
		bool bss = !strncmp(sn, ".bss", 4) || !strncmp(sn, ".tbss", 5);

		tb_s(b, "\t.section\t\"");
		tb_s(b, sn);
		tb_s(b, fn ? "\",\"ax\",@progbits\n" : bss ? "\",\"aw\",@nobits\n" :
			 (QQ(y->ty) & Q_CONST) && !strncmp(sn, ".rodata", 7) ? "\",\"a\",@progbits\n" : "\",\"aw\",@progbits\n");
	} else if (!DARWIN(c) && (c->o->sections & (fn ? 1 : 2))) {
		/* -ffunction-sections / -fdata-sections: .text.NAME etc., as clang */
		static const struct { const char *def, *base, *rest; } m[] = {
			{ "\t.text\n", ".text.", ",\"ax\",@progbits\n" },
			{ "\t.data\n", ".data.", ",\"aw\",@progbits\n" },
			{ "\t.bss\n", ".bss.", ",\"aw\",@nobits\n" },
		};
		const char *base = NULL, *rest = NULL;
		char nb[16];

		for (size_t k = 0; k < sizeof m / sizeof *m && !base; k++)
			if (!strcmp(defsec, m[k].def))
				base = m[k].base, rest = m[k].rest;
		if (!base && !strncmp(defsec, "\t.section\t.", 10)) {
			/* "\t.section\t.rodata,..." -> ".rodata." and ",..." */
			const char *cm = strchr(defsec + 10, ',');
			size_t n = cm ? (size_t)(cm - (defsec + 10)) : 0;

			if (cm && n + 2 <= sizeof nb) {
				memcpy(nb, defsec + 10, n);
				nb[n] = '.', nb[n + 1] = 0;
				base = nb, rest = cm;
			}
		}
		if (base) {
			tb_s(b, "\t.section\t");
			tb_s(b, base);
			sym_name(c, b, s);
			tb_s(b, rest);
		} else {
			tb_s(b, defsec);
		}
	} else {
		tb_s(b, defsec);
	}
	if (h != SIZE_MAX && !(fn && X86(c) && c->o->pfe_m)) {
		symhdr *sh;

		it_close(e, h);
		h = it_open(e, IT_SYMHDR);
		sh = it_room(e, sizeof *sh);
		*sh = (symhdr){ .sym = s, .p2 = (uint8_t)__builtin_ctz(align ? align : 1), .fn = fn };
		if (global) {
			sh->bind = (y->flags & SF_WEAK) ? STB_WEAK : STB_GLOBAL;
			if (a->visibility) {
				const char *v = c->strs[a->visibility - 1].s;

				sh->vis = !strcmp(v, "hidden") ? STV_HIDDEN : !strcmp(v, "protected") ? STV_PROTECTED : 0;
			}
		}
		it_close(e, h);
		return;
	}
	if (global) {
		tb_s(b, (y->flags & SF_WEAK) ? "\t.weak\t" : "\t.globl\t");
		sym_name(c, b, s);
		tb_c(b, '\n');
		if (a->visibility) {
			const char *v = c->strs[a->visibility - 1].s;

			if (!strcmp(v, "hidden") || !strcmp(v, "protected")) {
				tb_s(b, !strcmp(v, "hidden") ? "\t.hidden\t" : "\t.protected\t");
				sym_name(c, b, s);
				tb_c(b, '\n');
			}
		}
	}
	tb_s(b, "\t.p2align\t");
	tb_u(b, (uint64_t)__builtin_ctz(align ? align : 1));
	tb_s(b, fn ? "\n\t.type\t" : "\n\t.type\t");
	sym_name(c, b, s);
	tb_s(b, fn ? ",@function\n" : ",@object\n");
	if (fn && X86(c) && c->o->pfe_m) {
		/* -fpatchable-function-entry=N,M: M nops before the entry, recorded for ftrace */
		tb_s(b, ".Lpfe");
		tb_u(b, s);
		tb_s(b, ":\n");
		for (int k = 0; k < c->o->pfe_m; k++)
			tb_s(b, "\tnop\n");
	}
	sym_name(c, b, s);
	tb_s(b, ":\n");
	it_close(e, h);
}

/* a constructor's or destructor's pointer: .init_array[.prio] / .fini_array[.prio], as clang */
VPRIV void ctor_entry(cc *c, tbuf *b, uint32_t s, bool fini)
{
	const attrs *a = sym_attrs(c, s);

	tb_s(b, fini ? "\t.section\t.fini_array" : "\t.section\t.init_array");
	if (a && a->prio) {
		tb_c(b, '.');
		tb_u(b, a->prio);
	}
	tb_s(b, fini ? ",\"aw\",@fini_array\n\t.p2align\t3\n" : ",\"aw\",@init_array\n\t.p2align\t3\n");
	tb_s(b, X86(c) ? "\t.quad\t" : "\t.xword\t");
	sym_name(c, b, s);
	tb_c(b, '\n');
}
