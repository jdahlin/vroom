/* vcc code generator: inline asm */
#include "../../../emit/emitimpl.h"

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
/*
 * Known bits of an integer expression of at most 64 bits: *k has a 1 for each
 * bit whose value is known, *v those values (0 elsewhere).  What clang gets
 * from LLVM's known-bits analysis at -O1 and above for an "i" operand that is
 * not a constant expression: the kernel's PIE_E1 ORs in
 * `arm64_use_ng_mappings ? PTE_NG : 0` and then masks those bits away.
 * The operand has been generated already (side effects, calls); this only
 * decides the value.
 */
static void kbits(ecx *e, uint32_t n, uint64_t *k, uint64_t *v)
{
	cc *c = e->c;
	const node *x = N(n);
	int w = is_integer(c, x->ty) ? (int)tsize(c, x->ty) * 8 : 0;
	uint64_t wm = w >= 64 ? ~0ull : w ? (1ull << w) - 1 : 0;
	uint64_t ka, va, kb, vb;

	*k = *v = 0;
	if (!w || w > 64)
		return;
	switch (x->op) {
	case N_NUM:
		*k = ~0ull;
		*v = (uint64_t)c->ipool[x->a];
		break;
	case N_COMMA:
		kbits(e, x->b, k, v);
		break;
	case N_CAST: {
		const node *a = N(x->a);
		int aw = is_integer(c, a->ty) ? (int)tsize(c, a->ty) * 8 : 0;

		if (x->aux != CK_NOP && x->aux != CK_INT && x->aux != CK_BOOL)
			return;
		kbits(e, x->a, &ka, &va);
		if (!aw || aw > 64)
			return;
		if (x->aux == CK_BOOL) {
			uint64_t am = aw >= 64 ? ~0ull : (1ull << aw) - 1;

			*k = ~1ull;	/* 0 or 1 */
			if (va & ka & am)
				*k = ~0ull, *v = 1;
			else if ((ka & am) == am)
				*k = ~0ull;
			break;
		}
		if (aw < 64) {
			/* the bits above the source: zero, or copies of its sign bit */
			uint64_t hi = ~((1ull << aw) - 1), sb = 1ull << (aw - 1);

			ka &= ~hi, va &= ~hi;
			if (!is_signed(c, a->ty))
				ka |= hi;
			else if (ka & sb)
				ka |= hi, va |= va & sb ? hi : 0;
		}
		*k = ka, *v = va;
		break;
	}
	case N_UNARY:
		if (x->aux != T_TILDE)
			return;
		kbits(e, x->a, &ka, &va);
		*k = ka, *v = ~va & ka;
		break;
	case N_BINARY:
		kbits(e, x->a, &ka, &va);
		kbits(e, x->b, &kb, &vb);
		switch (x->aux) {
		case T_AMP:	/* known where both are known or either is a known 0 */
			*k = (ka & kb) | (ka & ~va) | (kb & ~vb);
			*v = va & vb;
			break;
		case T_PIPE:	/* known where both are known or either is a known 1 */
			*k = (ka & kb) | (ka & va) | (kb & vb);
			*v = va | vb;
			break;
		case T_CARET:
			*k = ka & kb;
			*v = (va ^ vb) & *k;
			break;
		case T_LESSLESS:
		case T_GREATERGREATER: {
			int bw = (int)tsize(c, N(x->b)->ty) * 8;
			uint64_t s = vb;

			if (~kb & (bw >= 64 ? ~0ull : (1ull << bw) - 1))
				return;	/* an unknown shift count */
			if (bw < 64 && is_signed(c, N(x->b)->ty) && (s >> (bw - 1) & 1))
				return;	/* negative */
			if (s >= (uint64_t)w)
				return;
			if (x->aux == T_LESSLESS) {
				*k = ka << s | ((1ull << s) - 1);
				*v = va << s;
			} else {
				/* the left operand extended to 64 bits: zeros, or copies of its sign bit */
				uint64_t sb = 1ull << (w - 1);

				ka &= wm, va &= ka;
				if (!is_signed(c, x->ty))
					ka |= ~wm;
				else if (ka & sb)
					ka |= ~wm, va |= va & sb ? ~wm : 0;
				if (is_signed(c, x->ty)) {	/* arithmetic: the top bit's knowledge moves down */
					*k = (uint64_t)((int64_t)ka >> s);
					*v = (uint64_t)((int64_t)va >> s);
				} else {
					*k = ka >> s | (s ? ~(~0ull >> s) : 0);
					*v = va >> s;
				}
			}
			break;
		}
		case T_PLUS:
		case T_MINUS:
		case T_STAR:
			if ((ka & wm) != wm || (kb & wm) != wm)
				return;
			*k = ~0ull;
			*v = x->aux == T_PLUS ? va + vb : x->aux == T_MINUS ? va - vb : va * vb;
			break;
		default:
			return;
		}
		break;
	case N_COND:
		if (!x->b)
			return;
		kbits(e, x->a, &ka, &va);
		{
			const node *cn = N(x->a);
			int cw = is_integer(c, cn->ty) ? (int)tsize(c, cn->ty) * 8 : 0;
			uint64_t cm = cw >= 64 ? ~0ull : cw ? (1ull << cw) - 1 : 0;

			if (cm && (va & ka & cm)) {
				kbits(e, x->b, k, v);
				break;
			}
			if (cm && (ka & cm) == cm) {
				kbits(e, x->c, k, v);
				break;
			}
		}
		kbits(e, x->b, &ka, &va);
		kbits(e, x->c, &kb, &vb);
		*k = ka & kb & ~(va ^ vb);
		*v = va & *k;
		break;
	default:
		return;
	}
	*k &= wm, *v &= *k;
}

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
	uint64_t uniq;
	int nvreg = 0;

	e->hasasm = true;
	NOT_LEAF(e);
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
	e->usedcs |= clobs & ((1u << 29) - (1u << 19));	/* callee-saved ones it clobbers: the prologue saves them */
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

			if (v.k != V_CONST && v.k != V_ADDR && !reg_ok && !mem_ok && strpbrk(cs, "iInJKLMN")) {
				/* not a constant expression, but every bit known (clang: LLVM's known bits) */
				uint64_t k, kv;
				int w = is_integer(c, N(en)->ty) ? (int)tsize(c, N(en)->ty) * 8 : 0;

				kbits(e, en, &k, &kv);
				if (w && w <= 64 && k == (w >= 64 ? ~0ull : (1ull << w) - 1)) {
					val_free(e, &v);
					v = vconst(em_trunc_to(c, (i128)kv, N(en)->ty));
				}
			}
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
					int64_t sl = slot(e, tsize(c, o->t) ? tsize(c, o->t) : 8, 16);
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
	/* %=: unique in the object and the same with any thread count: the
	   function's symbol and the place among its emitted asms that use %=
	   (inlined copies counted) */
	uniq = UINT64_MAX;	/* taken at the first %= */
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
			if (*p == '=') {
				if (uniq == UINT64_MAX) {
					if (e->asmk >= 1024)
						efatal(e, x->loc, "more than 1024 inline asm statements with %%= in one function");
					uniq = (uint64_t)e->top * 1024 + e->asmk++;
				}
				tb_u(rb, uniq);
				continue;
			}
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
				efatal(e, x->loc, "invalid %% escape in inline assembly string");
			}
			if ((uint32_t)idx >= nops) {
				uint32_t k = (uint32_t)idx - nops;

				if (k >= a->nlabel)
					efatal(e, x->loc, "invalid operand number %d in inline asm string (%u operands, %u labels)", idx, nops, a->nlabel);
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
		/*
		 * Extended asm reads and writes only its operands and clobbers:
		 * the registers go after the text (cond 2, RAW_MASKS) for opt.c.
		 * Basic asm and asm goto (its labels need what is live there)
		 * stay unknown.
		 */
		if (!basic && !a->nlabel) {
			uint64_t m[2] = { (1ull << 18) | (1ull << 29) | (1ull << 30), clobs };	/* reads, writes */

			for (uint32_t k = 0; k < nops; k++) {
				const asmopnd *o = &op[k];
				uint64_t r;

				if (o->kind == AO_MEM) {
					m[0] |= 1ull << o->reg;
					continue;
				}
				if (o->kind != AO_REG || o->reg == 0xff)
					continue;
				r = 1ull << o->reg;
				if (o->v.k == V_REG2)
					r |= 1ull << o->v.r2;
				if (o->out)
					m[1] |= r;
				if (!o->out || o->inout)
					m[0] |= r;
			}
			tb_put(rb, (const char *)m, sizeof(m));
			i->cond = RAW_MASKS;
		}
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
