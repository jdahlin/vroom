/* vcc code generator: functions: prologue, epilogue, finishing */
#include "emitimpl.h"

/* ---------------- functions ---------------- */

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
	} else {
		tb_s(b, defsec);
	}
	if (h != SIZE_MAX) {
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
	sym_name(c, b, s);
	tb_s(b, ":\n");
}

/* finish_function for the object backend: the same function as items */
static void finish_direct(ecx *e, uint32_t s, uint32_t align, const int *cs, int ncs, int32_t csave, int32_t total, int ctor)
{
	cc *c = e->c;
	tbuf *b = &e->out;
	size_t start, h;
	code_hdr *ch;
	ins *in;
	uint32_t n = 0, sym_labels = 0;

	b->n = (b->n + 7) & ~(size_t)7;
	start = b->n;
	sym_header(e, b, s, true, "\t.text\n", align);
	for (uint32_t k = 0; k < e->nin && !sym_labels; k++)
		sym_labels = e->in[k].op == I_RAW;
	h = it_open(e, IT_CODE);
	ch = it_room(e, sizeof *ch);
	in = it_room(e, ((size_t)e->nin + 2 * (size_t)ncs + 8) * sizeof(ins));
#define PUT(...) (in[n++] = (ins){ __VA_ARGS__ })
	PUT(.op = I_STP, .sz = 1, .rd = R_FP, .ra = R_LR, .rn = SP, .cond = 1, .imm = -16);
	PUT(.op = I_MOV, .sz = 1, .rd = R_FP, .rn = SP);
	if (total > 16)
		PUT(.op = I_SUBI, .sz = 1, .rd = SP, .rn = SP, .imm = total - 16);
	for (int k = 0; k + 1 < ncs; k += 2)
		PUT(.op = I_STP, .sz = 1, .rd = (uint8_t)cs[k], .ra = (uint8_t)cs[k + 1], .rn = R_FP, .imm = -csave + 8 * k);
	if (ncs & 1)
		PUT(.op = I_ST, .sz = 3, .rd = (uint8_t)cs[ncs - 1], .rn = R_FP, .imm = -csave + 8 * (ncs - 1));
	memcpy(in + n, e->in, (size_t)e->nin * sizeof(ins));
	n += e->nin;
	for (int k = 0; k + 1 < ncs; k += 2)
		PUT(.op = I_LDP, .sz = 1, .rd = (uint8_t)cs[k], .ra = (uint8_t)cs[k + 1], .rn = R_FP, .imm = -csave + 8 * k);
	if (ncs & 1)
		PUT(.op = I_LD, .sz = 3, .rd = (uint8_t)cs[ncs - 1], .rn = R_FP, .imm = -csave + 8 * (ncs - 1));
	PUT(.op = I_MOV, .sz = 1, .rd = SP, .rn = R_FP);
	PUT(.op = I_LDP, .sz = 1, .rd = R_FP, .ra = R_LR, .rn = SP, .cond = 2, .imm = 16);
	PUT(.op = I_RET);
#undef PUT
	/* give back the unused room, then the inline asm text */
	b->n -= ((size_t)e->nin + 2 * (size_t)ncs + 8 - n) * sizeof(ins);
	*ch = (code_hdr){ .fid = s, .nin = n, .nraw = sym_labels ? (uint32_t)e->raw.n : 0, .nlabel = e->nlabel, .sym_labels = sym_labels };
	if (sym_labels)
		memcpy(it_room(e, e->raw.n), e->raw.p, e->raw.n);
	it_close(e, h);
	h = it_open(e, IT_FEND);
	*(uint32_t *)it_room(e, sizeof(uint32_t)) = s;
	it_close(e, h);
	if (ctor & 1) {
		h = it_open(e, IT_TEXT);
		tb_s(b, "\t.section\t.init_array,\"aw\",@init_array\n\t.p2align\t3\n\t.xword\t");
		sym_name(c, b, s);
		tb_c(b, '\n');
		it_close(e, h);
	}
	if (ctor & 2) {
		h = it_open(e, IT_TEXT);
		tb_s(b, "\t.section\t.fini_array,\"aw\",@fini_array\n\t.p2align\t3\n\t.xword\t");
		sym_name(c, b, s);
		tb_c(b, '\n');
		it_close(e, h);
	}
	e->E->nins += e->nin;
	e->E->nfn++;
	out_chunk(e, (uint64_t)s << 1, start);
}

/* drop each b to a label among those right after it: the code falls through anyway */
static void drop_fallthrough(ecx *e)
{
	uint32_t n = 0;

	for (uint32_t k = 0; k < e->nin; k++) {
		const ins *i = &e->in[k];

		if (i->op == I_B) {
			uint32_t j = k + 1;

			while (j < e->nin && e->in[j].op == I_LABEL && e->in[j].label != i->label)
				j++;
			if (j < e->nin && e->in[j].op == I_LABEL)
				continue;
		}
		e->in[n++] = *i;
	}
	e->nin = n;
}

static void finish_function(ecx *e, uint32_t s)
{
	cc *c = e->c;
	tbuf *b = &e->out;
	size_t start = b->n;
	int cs[16], ncs = 0;
	int32_t csave, locals, outs, total;
	const attrs *a = sym_attrs(c, s);
	uint32_t align = 4;

	if (e->E->o->opt >= 1) {	/* -O1 and up */
		drop_fallthrough(e);
		if (!e->E->noopt)
			opt_function(e);
	}
	for (int r = 19; r <= 28; r++)
		if (e->usedcs & (1u << r))
			cs[ncs++] = r;
	csave = (ncs * 8 + 15) & ~15;
	locals = (e->fmax + 15) & ~15;
	outs = (e->outmax + 15) & ~15;
	total = 16 + csave + locals + outs;
	/* rewrite frame-slot references */
	for (uint32_t k = 0; k < e->nin; k++) {
		ins *i = &e->in[k];

		if (i->op == I_ADDI && i->rn == SP && i->cond == 0xee) {
			i->imm += outs;
			i->cond = 0;
			continue;
		}
		if (i->rn != FRAME)
			continue;
		i->rm = i->ra = 0;	/* slot tags */
		if (i->op != I_LD && i->op != I_ST)
			i->cond = i->sh = 0;
		if (e->dyn) {
			i->rn = R_FP;
			i->imm = i->imm - csave;
		} else {
			i->rn = SP;
			i->imm = i->imm - csave + (total - 16);
		}
	}
	if (a->aligned > align)
		align = a->aligned;
	if (c->syms[s].align > align)
		align = c->syms[s].align;
	if (direct(e)) {
		finish_direct(e, s, align, cs, ncs, csave, total, (a->constructor != 0) | (a->destructor != 0) << 1);
		return;
	}
	sym_header(e, b, s, true, "\t.text\n", align);
	/* prologue */
	tb_s(b, "\tstp\tx29, x30, [sp, #-16]!\n\tmov\tx29, sp\n");
	if (total > 16) {
		ins i = { .op = I_SUBI, .sz = 1, .rd = SP, .rn = SP, .imm = total - 16 };

		print_ins(c, b, &i, s, e->raw.p);
	}
	for (int k = 0; k + 1 < ncs; k += 2)
		tb_f(b, "\tstp\tx%d, x%d, [x29, #%d]\n", cs[k], cs[k + 1], -csave + 8 * k);
	if (ncs & 1)
		tb_f(b, "\tstr\tx%d, [x29, #%d]\n", cs[ncs - 1], -csave + 8 * (ncs - 1));
	for (uint32_t k = 0; k < e->nin; k++)
		print_ins(c, b, &e->in[k], s, e->raw.p);
	/* epilogue */
	for (int k = 0; k + 1 < ncs; k += 2)
		tb_f(b, "\tldp\tx%d, x%d, [x29, #%d]\n", cs[k], cs[k + 1], -csave + 8 * k);
	if (ncs & 1)
		tb_f(b, "\tldr\tx%d, [x29, #%d]\n", cs[ncs - 1], -csave + 8 * (ncs - 1));
	tb_s(b, "\tmov\tsp, x29\n\tldp\tx29, x30, [sp], #16\n\tret\n.Lfe");
	tb_u(b, s);
	tb_s(b, ":\n\t.size\t");
	sym_name(c, b, s);
	tb_s(b, ", .Lfe");
	tb_u(b, s);
	tb_c(b, '-');
	sym_name(c, b, s);
	tb_c(b, '\n');
	if (a->constructor) {
		tb_s(b, "\t.section\t.init_array,\"aw\",@init_array\n\t.p2align\t3\n\t.xword\t");
		sym_name(c, b, s);
		tb_c(b, '\n');
	}
	if (a->destructor) {
		tb_s(b, "\t.section\t.fini_array,\"aw\",@fini_array\n\t.p2align\t3\n\t.xword\t");
		sym_name(c, b, s);
		tb_c(b, '\n');
	}
	e->E->nins += e->nin;
	e->E->nfn++;
	out_chunk(e, (uint64_t)s << 1, start);
}

/*
 * The function's own scalar locals and params whose address is never
 * needed, most used first (loop depth counts 8x a level), live in
 * callee-saved registers x28 down for the whole function: no slot, a read
 * is a mov.  Those registers are then not expression temporaries here.
 */
static void choose_lregs(ecx *e, uint32_t s, int max)
{
	cc *c = e->c;
	emitter *E = e->E;
	uint32_t lo = E->lspan[2 * s], hi = E->lspan[2 * s + 1];
	int k = 0;
	uint32_t cand[MAXLREG];

	e->nlr = 0;
	if (!max || lo > hi || e->vararg || e->E->o->opt < 1)
		return;
	for (uint32_t v = lo; v <= hi; v++) {
		const sym *y = &c->syms[v];
		qty t = y->ty;
		int j;

		if (!E->luse[v] || E->lnoreg[v] || y->kind != S_VAR || !is_local_var(c, v) || y->sc == SC_STATIC ||
		    !(is_integer(c, t) || is_ptr(c, t)) || is_wide(c, t) || (QQ(t) & Q_VOLATILE) ||
		    tsize(c, t) - 1 >= 8 || (T(c, t)->flags & TF_VLA) || sym_attrs(c, v)->cleanup ||
		    regvar_reg(c, v) >= 0)
			continue;
		if (k == max && E->luse[cand[k - 1]] >= E->luse[v])
			continue;
		j = k < max ? k++ : k - 1;
		while (j > 0 && E->luse[cand[j - 1]] < E->luse[v]) {
			cand[j] = cand[j - 1];
			j--;
		}
		cand[j] = v;
	}
	for (int i = 0; i < k; i++) {
		int r = 28 - i;

		e->lrsym[i] = cand[i];
		e->lrreg[i] = (uint8_t)r;
		e->live |= 1u << (16 - i);	/* tempregs[16 - i] == x(28 - i) */
		e->resv |= 1u << (16 - i);
		e->usedcs |= 1u << r;
	}
	e->nlr = k;
}

/* true: out of temporaries with register variables, generate it again without */
static bool emit_function1(ecx *e, uint32_t s, int nlreg)
{
	cc *c = e->c;
	const sym *y = &c->syms[s];
	const node *body = N(y->body);
	const ctype *ft = T(c, y->ty);
	qty rty = ft->base;
	fnctx F;
	int ngrn = 0, nsaa = 0;

	walk_fn(e, s);
	e->nin = 0;
	e->raw.n = 0;
	e->nlabel = 0;
	e->nkv = 0, e->kvalid = 0, e->nkvs = 0, e->nael = 0;
	e->foff = e->fmax = e->outmax = e->cl_top = 0;
	e->nsla = e->nsstk = 0;
	e->dyn = e->calls = false;
	e->live = 0;
	e->resv = 0;
	e->usedcs = 0;
	e->nloop = e->nclean = 0;
	e->ncases = 0;
	e->reach = true;
	e->top = s;
	e->inline_depth = 0;
	e->vararg = (ft->flags & TF_VARIADIC) != 0;
	choose_lregs(e, s, nlreg);
	memset(&F, 0, sizeof(F));
	F.fn = s;
	F.inst = ++e->inst_next;
	F.rty = rty;
	F.retlbl = newlabel(e);
	e->f = &F;
	switch (_setjmp(e->bail)) {
	case 0:
		break;
	case 2:
		e->f = NULL;
		return true;
	default:
		e->f = NULL;
		return false;
	}
	if (is_sret(c, rty)) {
		F.retslot = slot(e, 8, 8);
		mem(e, I_ST, 3, 0, 8, FRAME, F.retslot);
	}
	/* parameters */
	e->nsrn = 0;
	for (uint32_t i = 0; i < body->aux; i++) {
		uint32_t p = c->lists[body->a + i];
		qty pt = c->syms[p].ty;
		abiarg ab;
		bind *bd = binding(e, p);
		uint64_t sz = tsize(c, pt);

		/* a K&R definition without a prototype gets a float as a double (default promotions) */
		bool kfl = (T(c, c->syms[s].ty)->flags & TF_NOPROTO) && KIND(c, pt) == TY_FLOAT;

		classify(e, kfl ? prim(TY_DOUBLE) : pt, &ngrn, &nsaa, &ab);
		if (kfl && ab.kind == A_VREG) {
			word(e, 0x1e624000u | (unsigned)ab.reg << 5 | ab.reg);	/* fcvt s, d */
			ab.size = 4;
		}
		bd->inst = F.inst;
		bd->reg = 0xff;
		if ((ab.kind == A_REG || ab.kind == A_STACK) && lreg_of(e, p) >= 0) {
			int r = lreg_of(e, p);

			bd->kind = B_LREG;
			bd->kv = 0;
			bd->v = vreg(r);
			if (ab.kind == A_REG)
				extend(e, r, ab.reg, (int)sz, is_signed(c, pt));	/* AAPCS64: high bits unspecified */
			else
				mem(e, I_LD, log2sz(sz), sz < 8 && is_signed(c, pt) ? 1 : 0, r, R_FP, 16 + ab.stk);
			continue;
		}
		switch (ab.kind) {
		case A_IGNORE:
			bd->kind = B_SLOT;
			bd->slot = slot(e, 8, 8);
			break;
		case A_REG:
			bd->kind = B_SLOT;
			bd->slot = slot(e, (sz + 7) & ~(uint64_t)7, talign(c, pt) > 8 ? 16 : 8);
			for (int k = 0; k < ab.nreg; k++)
				mem(e, I_ST, is_agg(c, pt) || sz == 16 ? 3 : log2sz(sz), 0, ab.reg + k, FRAME, bd->slot + 8 * k);
			break;
		case A_STACK:
			bd->kind = B_SLOT;
			bd->slot = slot(e, (sz + 7) & ~(uint64_t)7, talign(c, pt) > 8 ? 16 : 8);
			if (DARWIN(c) && sz < 8 && !is_agg(c, pt)) {	/* Apple: packed */
				mem(e, I_LD, log2sz(sz), 0, R_X16, R_FP, 16 + ab.stk);
				mem(e, I_ST, log2sz(sz), 0, R_X16, FRAME, bd->slot);
				break;
			}
			for (int k = 0; k < ab.nreg; k++) {
				mem(e, I_LD, 3, 0, R_X16, R_FP, 16 + ab.stk + 8 * k);
				mem(e, I_ST, 3, 0, R_X16, FRAME, bd->slot + 8 * k);
			}
			break;
		case A_REF_REG:
			bd->kind = B_INDIRECT;
			bd->slot = slot(e, 8, 8);
			mem(e, I_ST, 3, 0, ab.reg, FRAME, bd->slot);
			break;
		case A_REF_STACK:
			bd->kind = B_INDIRECT;
			bd->slot = slot(e, 8, 8);
			mem(e, I_LD, 3, 0, R_X16, R_FP, 16 + ab.stk);
			mem(e, I_ST, 3, 0, R_X16, FRAME, bd->slot);
			break;
		case A_VREG:
			bd->kind = B_SLOT;
			bd->slot = sz == 16 ? slot(e, 16, 16) : slot(e, 8, 8);
			vr_store(e, (int)ab.size, ab.reg, FRAME, bd->slot);
			break;
		case A_HFA:
			bd->kind = B_SLOT;
			bd->slot = slot(e, (sz + 7) & ~(uint64_t)7, talign(c, pt) > 8 ? 16 : 8);
			for (int k = 0; k < ab.nreg; k++)
				vr_store(e, ab.fsz, ab.reg + k, FRAME, bd->slot + ab.fsz * k);
			break;
		}
	}
	if (e->vararg && DARWIN(c)) {
		e->ngr_named = ngrn;
		e->nstk_named = nsaa;
	} else if (e->vararg) {
		e->ngr_named = ngrn;
		e->nstk_named = nsaa;
		e->vaslot = slot(e, 64, 16);
		for (int r = ngrn; r < 8; r++)
			mem(e, I_ST, 3, 0, r, FRAME, e->vaslot + 8 * r);
		if (c->o->general_regs_only) {
			/* as clang: no FP/SIMD save area (the kernel may not touch v regs),
			   __vr_offs 0 and __vr_top = __gr_top */
			e->nvr_named = 8;
			e->vrslot = e->vaslot - 64;
		} else {
			/* v0-v7: 16 bytes each (the low 8 hold a float or double) */
			e->nvr_named = e->nsrn;
			e->vrslot = slot(e, 128, 16);
			for (int r = e->nsrn; r < 8; r++)
				vr_store(e, 16, r, FRAME, e->vrslot + 16 * r);
		}
	}
	gen_stmt(e, y->body);
	/* falling off the end of main returns 0 */
	if (e->reach && y->name && vintern_ents[y->name].len == 4 && !memcmp(vintern_ents[y->name].s, "main", 4))
		movi(e, 0, 0, 0);
	deflabel(e, F.retlbl);
	e->f = NULL;
	if (unlikely(e->E->stats)) {
		double t = now();

		finish_function(e, s);
		e->E->print_ns += (uint64_t)((now() - t) * 1e9);
	} else {
		finish_function(e, s);
	}
	return false;
}

VPRIV void emit_function(ecx *e, uint32_t s)
{
	/* out of temporaries: again without register variables and with more of them free in inlined calls */
	e->inlfree = INLINE_FREE_FAST;
	if (emit_function1(e, s, e->E->nlreg)) {
		e->inlfree = INLINE_FREE_SAFE;
		emit_function1(e, s, 0);
	}
	e->nlr = 0, e->resv = 0, e->inlfree = 0;	/* out of a function: no register variables */
}
