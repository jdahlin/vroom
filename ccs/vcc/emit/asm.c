/* vcc code generator: inline asm */
#include "emitimpl.h"

/* ---------------- inline asm ---------------- */

enum { AO_REG, AO_IMM, AO_MEM, AO_ZERO, AO_VREG };

typedef struct asmopnd {
	uint8_t kind, reg, fixed, out, inout, wide;
	val v;			/* AO_IMM: the constant / address */
	lval l;			/* outputs: where the result goes */
	qty t;
	uint32_t name;
} asmopnd;

static bool imm_fits(int ch, i128 v)
{
	switch (ch) {
	case 'I': return v >= 0 && v <= 4095;
	case 'J': return v >= -4095 && v <= 0;
	case 'K': return logimm_ok((uint64_t)v, 0) && v == (i128)(uint32_t)v;
	case 'L': return logimm_ok((uint64_t)v, 1);
	case 'M': return v == (i128)(uint32_t)v;
	case 'N': return true;
	case 'i': case 'n': case 's': return true;
	default: return false;
	}
}

/* the register named by a clobber string, or -1 */
static int clob_reg(const char *s)
{
	if ((s[0] == 'x' || s[0] == 'w') && s[1] >= '0' && s[1] <= '9') {
		int r = atoi(s + 1);

		return r <= 30 ? r : -1;
	}
	if (!strcmp(s, "lr"))
		return 30;
	return -1;
}

/* a vector register load or store as raw text: op qv|dv, [xr] */
static void vraw(ecx *e, const char *opn, bool q, int v, int xr)
{
	tbuf *rb = &e->raw;
	size_t st = rb->n;
	ins *i;

	tb_c(rb, '\t');
	tb_s(rb, opn);
	tb_c(rb, '\t');
	tb_c(rb, q ? 'q' : 'd');
	tb_u(rb, (uint64_t)v);
	tb_s(rb, ", [");
	reg(rb, xr, 1);
	tb_c(rb, ']');
	i = put(e, I_RAW);
	i->off = (int32_t)st;
	i->ref = (uint32_t)(rb->n - st);
}

VPRIV void gen_asm(ecx *e, uint32_t n)
{
	cc *c = e->c;
	const node *x = N(n);
	const casm *a = &c->asms[x->a];
	const char *tp = c->strs[a->tmpl - 1].s;
	uint32_t nops = a->nout + a->nin;
	asmopnd *op;
	uint32_t clobs = 0, reserved = 0;
	uint32_t tramp[64];
	bool basic = nops == 0 && a->nclob == 0 && a->nlabel == 0;
	spill sp = { 0 };
	tbuf *rb = &e->raw;
	size_t start;
	uint32_t uniq;
	int nvreg = 0;

	if (!e->reach)
		return;
	if (nops > 64 || a->nlabel > 64)
		efatal(e, x->loc, "too many asm operands");
	op = alloca((nops + 1) * sizeof(asmopnd));
	memset(op, 0, (nops + 1) * sizeof(asmopnd));
	/* clobbered registers: keep temporaries out of them */
	for (uint32_t i = 0; i < a->nclob; i++) {
		int r = clob_reg(c->strs[a->clob[i] - 1].s);

		if (r >= 0)
			clobs |= 1u << r;
	}
	for (int i = 0; i < NTEMP; i++)
		if ((clobs >> tempregs[i]) & 1) {
			if (e->live & (1u << i)) {
				/* live across a clobber: save it */
				if (!sp.mask)
					memset(&sp, 0, sizeof(sp));
				sp.slot[i] = slot(e, 8, 8);
				mem(e, I_ST, 3, 0, tempregs[i], FRAME, sp.slot[i]);
				sp.mask |= 1u << i;
			} else {
				e->live |= 1u << i;
				reserved |= 1u << i;
			}
		}
	/* outputs: lvalues and registers */
	for (uint32_t i = 0; i < a->nout; i++) {
		const asmop *ao = &a->ops[i];
		const char *cs = c->strs[ao->constraint - 1].s;
		asmopnd *o = &op[i];
		bool mem_ok = strpbrk(cs, "mQog") != NULL, reg_ok = strpbrk(cs, "rg") != NULL;
		uint32_t en = ao->expr;

		o->out = 1;
		o->inout = strchr(cs, '+') != NULL;
		o->t = N(en)->ty;
		o->name = ao->name;
		if (is_vec(c, o->t) && strpbrk(cs, "wx")) {
			/* a vector in v16+: loaded before the template if read, stored after it */
			o->kind = AO_VREG;
			o->reg = (uint8_t)(16 + nvreg++);
			o->l = gen_lval(e, en);
			continue;
		}
		if (strpbrk(cs, "wxy") && !reg_ok)
			efatal(e, x->loc, "floating point asm operands are not supported");
		if (mem_ok && !reg_ok) {
			lval l = gen_lval(e, en);
			val av = laddr(e, &l);

			o->kind = AO_MEM;
			o->reg = (uint8_t)toreg(e, &av);
			continue;
		}
		o->kind = AO_REG;
		o->l = gen_lval(e, en);
		if (N(en)->op == N_VAR && is_local_var(c, N(en)->a) && binding(e, N(en)->a)->reg != 0xff &&
		    binding(e, N(en)->a)->kind == B_SLOT) {
			o->fixed = 1;
			o->reg = binding(e, N(en)->a)->reg;
		}
		o->wide = is_wide(c, o->t);
		if (o->inout) {
			lval l2 = o->l;
			val v = load(e, &l2, o->t);

			o->v = v;
		}
	}
	/* inputs */
	for (uint32_t i = a->nout; i < nops; i++) {
		const asmop *ao = &a->ops[i];
		const char *cs = c->strs[ao->constraint - 1].s;
		asmopnd *o = &op[i];
		uint32_t en = ao->expr;
		bool reg_ok = strpbrk(cs, "rpg") != NULL, mem_ok = strpbrk(cs, "mQog") != NULL;	/* p: address in a register */
		int tied = -1;

		o->t = N(en)->ty;
		o->name = ao->name;
		for (const char *p = cs; *p; p++)
			if (*p >= '0' && *p <= '9')
				tied = atoi(p);
		if (is_vec(c, o->t) && strpbrk(cs, "wx") && tied < 0) {
			val v = gen(e, en);	/* the vector's address */

			toreg(e, &v);
			o->kind = AO_VREG;
			o->reg = (uint8_t)(16 + nvreg++);
			o->v = v;
			continue;
		}
		if (tied >= 0 && (uint32_t)tied < a->nout) {
			/* the input goes into the output's register */
			val v = gen(e, en);
			asmopnd *to = &op[tied];

			val_free(e, &to->v);
			to->v = v;
			to->inout = 1;
			o->kind = AO_REG;
			o->reg = 0xff;
			o->fixed = 2;	/* alias of the output */
			o->inout = (uint8_t)tied;
			continue;
		}
		if (mem_ok && !reg_ok && !strpbrk(cs, "inIJKLMNZ")) {
			lval l;

			if (N(en)->op == N_CAST && N(en)->aux == CK_DECAY)
				en = N(en)->a;	/* an array as a memory operand: the array itself */
			l = gen_lval(e, en);
			val av = laddr(e, &l);

			o->kind = AO_MEM;
			o->reg = (uint8_t)toreg(e, &av);
			continue;
		}
		{
			val v = gen(e, en);
			bool imm = false;

			if (v.k == V_CONST || v.k == V_ADDR) {
				if (strchr(cs, 'Z') && v.k == V_CONST && v.i == 0) {
					o->kind = AO_ZERO;
					continue;
				}
				for (const char *p = cs; *p; p++)
					if (strchr("iInJKLMNsg", *p) && (v.k == V_ADDR ? (*p == 'i' || *p == 's' || *p == 'g') : *p == 'g' || imm_fits(*p, v.i)))
						imm = true;
			}
			if (imm) {
				o->kind = AO_IMM;
				o->v = v;
				if (v.k == V_ADDR)
					note_ref(e, v.ref);
				continue;
			}
			if (!reg_ok && !strchr(cs, 'Z')) {
				if (mem_ok) {
					/* a value for a memory operand: put it in a slot */
					int32_t sl = slot(e, tsize(c, o->t) ? tsize(c, o->t) : 8, 16);
					lval l = { .k = L_FRAME, .off = sl, .align = 16 };
					int r;

					store(e, &l, o->t, &v);
					val_free(e, &v);
					r = treg(e);
					opi(e, I_ADDI, 1, r, FRAME, sl);
					o->kind = AO_MEM;
					o->reg = (uint8_t)r;
					continue;
				}
				efatal(e, x->loc, "impossible constraint in asm: '%s'", cs);
			}
			o->kind = AO_REG;
			o->wide = is_wide(c, o->t);
			if (o->wide)
				toreg2(e, &v, is_signed(c, o->t));
			else
				toreg(e, &v);
			o->v = v;
			if (N(en)->op == N_VAR && is_local_var(c, N(en)->a) && binding(e, N(en)->a)->reg != 0xff &&
			    binding(e, N(en)->a)->kind == B_SLOT) {
				o->fixed = 1;
				o->reg = binding(e, N(en)->a)->reg;
			} else {
				o->reg = v.r;
			}
		}
	}
	/* output registers */
	for (uint32_t i = 0; i < a->nout; i++) {
		asmopnd *o = &op[i];

		if (o->kind != AO_REG)
			continue;
		if (o->inout) {
			if (o->wide)
				toreg2(e, &o->v, false);
			else if (o->v.k != V_REG)
				toreg(e, &o->v);
			if (!o->fixed)
				o->reg = o->v.r;
		} else if (!o->fixed) {
			o->reg = (uint8_t)treg(e);
			o->v = vreg(o->reg);
			if (o->wide) {
				o->v.k = V_REG2;
				o->v.r2 = (uint8_t)treg(e);
			}
		}
	}
	/* fixed registers last, so evaluating the others cannot clobber them */
	for (uint32_t i = 0; i < nops; i++) {
		asmopnd *o = &op[i];

		if (o->fixed == 1 && (o->inout || !o->out) && o->v.k != V_NONE)
			mov(e, 1, o->reg, o->v.r);
	}
	/* asm goto with outputs: the labels go through trampolines that store them */
	for (uint32_t k = 0; k < a->nlabel; k++)
		tramp[k] = a->nout ? newlabel(e) : user_label(e, a->labels[k]);
	/* vector operands into their registers, last so no call in between clobbers them */
	for (uint32_t i = 0; i < nops; i++) {
		asmopnd *o = &op[i];
		bool q = tsize(c, o->t) == 16;

		if (o->kind != AO_VREG)
			continue;
		if (!o->out) {
			vraw(e, "ldr", q, o->reg, o->v.r);
			val_free(e, &o->v);
		} else if (o->inout) {
			lval l2 = o->l;
			val av = laddr(e, &l2);
			int r = toreg(e, &av);

			vraw(e, "ldr", q, o->reg, r);
			val_free(e, &av);
		}
	}
	/* the template */
	start = rb->n;
	uniq = atomic_fetch_add(&e->E->asmno, 1);
	if (basic) {
		tb_s(rb, tp);
	} else {
		for (const char *p = tp; *p; p++) {
			char mod = 0;
			int idx = -1;

			if (*p != '%') {
				tb_c(rb, *p);
				continue;
			}
			p++;
			if (*p == '%') { tb_c(rb, '%'); continue; }
			if (*p == '=') { tb_u(rb, uniq); continue; }
			if (*p == '{' || *p == '|' || *p == '}') { tb_c(rb, *p); continue; }
			if (*p && strchr("wxcalHnPQRsqdbh", *p) && (p[1] == '[' || (p[1] >= '0' && p[1] <= '9')))
				mod = *p++;
			if (*p == '[') {
				const char *q = strchr(p, ']');
				uint32_t len = q ? (uint32_t)(q - p - 1) : 0;

				for (uint32_t i = 0; i < nops; i++)
					if (op[i].name && vintern_ents[op[i].name].len == len &&
					    !memcmp(vintern_ents[op[i].name].s, p + 1, len))
						idx = (int)i;
				if (idx < 0)
					for (uint32_t k = 0; k < a->nlabel; k++) {
						const label *lb = &c->labels[a->labels[k]];

						if (vintern_ents[lb->name].len == len && !memcmp(vintern_ents[lb->name].s, p + 1, len))
							idx = (int)(nops + k);
					}
				if (idx < 0)
					efatal(e, x->loc, "unknown asm operand name '%.*s'", (int)len, p + 1);
				p = q;
			} else if (*p >= '0' && *p <= '9') {
				idx = 0;
				while (p[0] >= '0' && p[0] <= '9')
					idx = idx * 10 + (*p++ - '0');
				p--;
			} else {
				efatal(e, x->loc, "invalid %% in asm string");
			}
			if ((uint32_t)idx >= nops) {
				uint32_t k = (uint32_t)idx - nops;

				if (k >= a->nlabel)
					efatal(e, x->loc, "invalid operand number in asm string");
				p_lbl(rb, e->top, tramp[k]);
				continue;
			}
			{
				asmopnd *o = &op[idx];

				if (o->fixed == 2)
					o = &op[o->inout];
				switch (o->kind) {
				case AO_VREG:
					tb_c(rb, mod == 'q' || mod == 'd' || mod == 's' || mod == 'h' || mod == 'b' ? mod : 'v');
					tb_u(rb, o->reg);
					break;
				case AO_REG:
					if (mod == 'H' && o->wide) {
						reg(rb, o->v.r2, 1);
					} else if (mod == 'a') {	/* address operand: [xN] */
						tb_c(rb, '[');
						reg(rb, o->reg, 1);
						tb_c(rb, ']');
					} else {
						reg(rb, o->reg, mod != 'w');
					}
					break;
				case AO_ZERO:
					reg(rb, ZR, mod != 'w');
					break;
				case AO_MEM:
					tb_c(rb, '[');
					reg(rb, o->reg, 1);
					tb_c(rb, ']');
					break;
				case AO_IMM:
					if (o->v.k == V_ADDR) {
						ref_off(c, rb, o->v.ref, (int64_t)o->v.i);
					} else {	/* clang prints a bare number, as the assembler reads it */
						tb_i(rb, mod == 'n' ? -(int64_t)o->v.i : (int64_t)o->v.i);
					}
					break;
				}
			}
		}
	}
	{
		ins *i = put(e, I_RAW);

		i->off = (int32_t)start;
		i->ref = (uint32_t)(rb->n - start);
		i->cond = 1;
	}
	for (uint32_t i = 0; i < a->nout; i++) {
		asmopnd *o = &op[i];

		if (o->kind == AO_VREG) {
			lval l2 = o->l;
			val av = laddr(e, &l2);
			int r = toreg(e, &av);

			vraw(e, "str", tsize(c, o->t) == 16, o->reg, r);
			val_free(e, &av);
		}
	}
	/* fixed output registers back into their temporaries */
	for (uint32_t i = 0; i < a->nout; i++) {
		asmopnd *o = &op[i];

		if (o->kind == AO_REG && o->fixed == 1) {
			if (o->v.k != V_REG) {
				o->v = vreg(treg(e));
			}
			mov(e, 1, o->v.r, o->reg);
		}
	}
	if (sp.mask)
		unspill(e, &sp);
	e->live &= ~reserved;
	/* store the outputs (on the fallthrough path and through each trampoline) */
	{
		uint32_t lafter = a->nlabel && a->nout ? newlabel(e) : 0;

		for (int pass = 0; pass <= (int)(a->nout ? a->nlabel : 0); pass++) {
			if (pass > 0) {
				e->lrefc[tramp[pass - 1]] |= LREF_EXT;	/* (the asm jumps here) */
				deflabel_forced(e, tramp[pass - 1]);
			}
			for (uint32_t i = 0; i < a->nout; i++) {
				asmopnd *o = &op[i];

				if (o->kind != AO_REG)
					continue;
				{
					val v = o->v;
					lval l = o->l;

					store(e, &l, o->t, &v);
				}
			}
			if (pass > 0)
				jump(e, user_label(e, a->labels[pass - 1]));
			else if (lafter)
				jump(e, lafter);
		}
		if (lafter)
			deflabel(e, lafter);
	}
	for (uint32_t i = 0; i < nops; i++) {
		asmopnd *o = &op[i];

		if (o->kind == AO_REG && o->fixed != 2)
			val_free(e, &o->v);
		if (o->kind == AO_MEM)
			tfree(e, o->reg);
		if (o->out && o->kind == AO_REG)
			lfree(e, &o->l);
	}
	/* asm goto without outputs: the labels are branch targets */
	if (!a->nout)
		for (uint32_t k = 0; k < a->nlabel; k++) {
			kv_edge(e, tramp[k]);
			e->lrefc[tramp[k]]++;
			e->lrefc[tramp[k]] |= LREF_EXT;
		}
}
