/*
 * The text form, printing (SPEC-vir.md 11): constants inline with a `:type`
 * suffix exactly where the position does not fix the type (11.5); values
 * and blocks named v0.. and b0.. in order of appearance (11.6); instruction
 * flags in the order of 11.3.
 */
#include "virimpl.h"

typedef struct pctx {
	vbuf *b;
	const vir_unit *u;
	const vir_fn *f;
	const uint32_t *nm;	/* value index -> N of vN (VIR_NOBLK: none) */
} pctx;

static const char *const target_name[VTG_N] = { "none", "x86_64-linux", "arm64-linux", "arm64-darwin" };

const char *vir_target_name(uint32_t t) { return t < VTG_N ? target_name[t] : "none"; }

static void put(pctx *p, const char *s) { vbuf_s(p->b, s); }

__attribute__((format(printf, 2, 3)))
static void putf(pctx *p, const char *fmt, ...)
{
	char t[512];
	va_list ap;
	int n;

	va_start(ap, fmt);
	n = vsnprintf(t, sizeof(t), fmt, ap);
	va_end(ap);
	vbuf_put(p->b, t, n < (int)sizeof(t) ? (size_t)n : sizeof(t) - 1);
}

/* ---------------- numbers and strings ---------------- */

static void fmt_hexfloat(char *o, uint64_t bits, int ebits, int mbits)
{
	uint64_t mant = bits & (((uint64_t)1 << mbits) - 1);
	int e = (int)((bits >> mbits) & (((uint64_t)1 << ebits) - 1)), sign = (int)(bits >> (mbits + ebits)) & 1;
	int bias = (1 << (ebits - 1)) - 1;

	if (e == 0 && mant == 0) {
		sprintf(o, "%s0x0p+0", sign ? "-" : "");
	} else if (e == 0 || e == (1 << ebits) - 1) {
		sprintf(o, "bits:0x%llx", (unsigned long long)bits);
	} else {
		int nd = (mbits + 3) / 4;
		char digs[20];

		mant <<= nd * 4 - mbits;
		for (int i = nd - 1; i >= 0; i--, mant >>= 4)
			digs[i] = "0123456789abcdef"[mant & 15];
		digs[nd] = 0;
		while (nd && digs[nd - 1] == '0')
			digs[--nd] = 0;
		sprintf(o, "%s0x1%s%s" "p%+d", sign ? "-" : "", nd ? "." : "", digs, e - bias);
	}
}

static void put_str(pctx *p, const char *s)
{
	vbuf_put(p->b, "\"", 1);
	for (const unsigned char *c = (const unsigned char *)s; *c; c++) {
		if (*c == '"' || *c == '\\')
			putf(p, "\\%c", *c);
		else if (*c == '\n')
			put(p, "\\n");
		else if (*c == '\t')
			put(p, "\\t");
		else if (*c >= 32 && *c < 127)
			vbuf_put(p->b, c, 1);
		else
			putf(p, "\\%03o", *c);
	}
	vbuf_put(p->b, "\"", 1);
}

static void put_bytes(pctx *p, const char *s, uint64_t n)
{
	vbuf_put(p->b, "\"", 1);
	for (uint64_t i = 0; i < n; i++) {
		unsigned char c = (unsigned char)s[i];

		if (c == '"' || c == '\\')
			putf(p, "\\%c", c);
		else if (c == '\n')
			put(p, "\\n");
		else if (c == '\t')
			put(p, "\\t");
		else if (c >= 32 && c < 127)
			vbuf_put(p->b, &c, 1);
		else
			putf(p, "\\%03o", c);
	}
	vbuf_put(p->b, "\"", 1);
}

/* ---------------- signatures and groups ---------------- */

void vir_print_group(vbuf *b, uint32_t w0, uint32_t w1)
{
	switch (VG_KIND(w0)) {
	case VG_NONE: vbuf_s(b, "none"); break;
	case VG_SRET: vbuf_s(b, "sret"); break;
	case VG_MEM: vbuf_f(b, "mem %u align %u", VG_SIZE(w1), 1u << VG_LALIGN(w1)); break;
	default:
		for (uint32_t i = 0; i < VG_NPIECE(w0) && i < 4; i++) {
			uint32_t c = VG_CLASS(w0, i);

			vbuf_f(b, "%s%s", i ? ":" : "", c == VC_INT ? "int" : c == VC_F32 ? "f32" : c == VC_F64 ? "f64" : "?");
		}
		break;
	}
}

void vir_print_sig(vbuf *b, const uint32_t *w)
{
	uint32_t ng = VSIG_NGROUPS(w[0]);

	vbuf_s(b, "(");
	for (uint32_t i = 0; i < ng; i++) {
		if (i)
			vbuf_s(b, ", ");
		vir_print_group(b, w[3 + 2 * i], w[4 + 2 * i]);
	}
	if (VSIG_VARIADIC(w[0]))
		vbuf_s(b, ng ? ", ..." : "...");
	vbuf_s(b, ") -> ");
	vir_print_group(b, w[1], w[2]);
}

static uint32_t sig_words(const uint32_t *w) { return 3 + 2 * VSIG_NGROUPS(w[0]); }

/* the unit's index of the signature at extra offset off, or ~0 */
static uint32_t sig_index(const pctx *p, uint32_t off)
{
	const vir_fn *f = p->f;
	const vir_unit *u = p->u;

	if (!u || off == 0 || off >= f->nextra || (uint64_t)off + sig_words(&f->extra[off]) > f->nextra)
		return VIR_NOBLK;
	for (uint32_t i = 0; i < u->nsig; i++)
		if (u->sig[i].n == sig_words(&f->extra[off]) &&
		    !memcmp(&u->sigw[u->sig[i].off], &f->extra[off], (size_t)u->sig[i].n * 4))
			return i;
	return VIR_NOBLK;
}

/* ---------------- operands ---------------- */

static void put_sym(pctx *p, uint32_t s, int64_t add)
{
	if (p->u && s < p->u->nsym)
		putf(p, "@%s", p->u->sym[s].name);
	else
		putf(p, "@s%u", s);
	if (add)
		putf(p, "%+lld", (long long)add);
}

static void put_const(pctx *p, const vir_ins *c)
{
	uint64_t v = (uint64_t)c->b | (uint64_t)c->c << 32;
	char t[48];

	switch (c->op) {
	case VO_ICONST:
		if (c->ty == VT_I32)
			putf(p, "%d", (int32_t)c->b);
		else if (c->ty == VT_PTR)
			putf(p, "0x%llx", (unsigned long long)v);
		else
			putf(p, "%lld", (long long)v);
		break;
	case VO_FCONST:
		if (c->ty == VT_F32)
			fmt_hexfloat(t, c->b, 8, 23);
		else
			fmt_hexfloat(t, v, 11, 52);
		put(p, t);
		break;
	case VO_SYMADDR: put_sym(p, c->a, (int64_t)v); break;
	case VO_SLOTADDR:
		putf(p, "$%u", c->a);
		if (v)
			putf(p, "%+lld", (long long)v);
		break;
	case VO_LABELADDR: putf(p, "&b%u", c->a); break;
	}
}

/* an operand: a value name or an inline constant; typed: the suffix is required */
static void put_val(pctx *p, uint32_t v, bool typed)
{
	const vir_fn *f = p->f;

	if (v >= f->nins) {
		putf(p, "v?%u", v);
	} else if (vir_is_const_op(f->ins[v].op)) {
		put_const(p, &f->ins[v]);
		if (typed)
			putf(p, ":%s", f->ins[v].ty < VT_N ? vir_tyname[f->ins[v].ty] : "?");
	} else if (p->nm[v] == VIR_NOBLK) {
		putf(p, "v?%u", v);
	} else {
		putf(p, "v%u", p->nm[v]);
	}
}

static bool is_const(const pctx *p, uint32_t v) { return v < p->f->nins && vir_is_const_op(p->f->ins[v].op); }

/* operand k (0 a, 1 b, 2 c) of in: does the text need the type suffix? */
static bool need_ty(const pctx *p, const vir_ins *in, uint32_t k)
{
	uint32_t other = k == 0 ? in->b : in->a;

	if (vir_opnd_fixed(in->op, in->ty, k) != VT_VOID)
		return false;
	if ((in->op == VO_ICMP || in->op == VO_FCMP) && !is_const(p, other))
		return false;
	return true;
}

static void put_opnd(pctx *p, const vir_ins *in, uint32_t k)
{
	uint32_t v = k == 0 ? in->a : k == 1 ? in->b : in->c;

	put_val(p, v, is_const(p, v) && need_ty(p, in, k));
}

/* ---------------- instructions ---------------- */

static void put_flags(pctx *p, const vir_ins *in)
{
	uint32_t fl = in->fl, op = in->op;

	switch (op) {
	case VO_ADD: case VO_SUB: case VO_MUL: case VO_SHL: case VO_NEG:
		if (fl & VIF_NSW) put(p, " nsw");
		if (fl & VIF_NUW) put(p, " nuw");
		break;
	case VO_SDIV: case VO_UDIV: case VO_LSHR: case VO_ASHR:
		if (fl & VIF_EXACT) put(p, " exact");
		break;
	case VO_CLZ: case VO_CTZ:
		if (fl & VIF_NZ) put(p, " nz");
		break;
	case VO_LOAD: case VO_STORE: case VO_ALOAD: case VO_ASTORE: case VO_AXCHG: case VO_AXADD: case VO_ACAS:
		if (fl & VIF_VOL) put(p, " volatile");
		if (VIF_AL(fl)) putf(p, " align %u", 1u << VIF_AL(fl));
		if (op == VO_LOAD && in->b) putf(p, " tag %u", in->b);
		if (op == VO_STORE && in->c) putf(p, " tag %u", in->c);
		break;
	case VO_MEMCPY: case VO_MEMSET:
		if (VIF_AL(fl)) putf(p, " align %u", 1u << VIF_AL(fl));
		if (op == VO_MEMCPY && VIF_AL2(fl) != VIF_AL(fl)) putf(p, " align %u", 1u << VIF_AL2(fl));
		break;
	case VO_CALL:
		if (fl & VIF_CALL_NORETURN) put(p, " noreturn");
		if (fl & VIF_CALL_PURE) put(p, " pure");
		if (fl & VIF_CALL_CONST) put(p, " const");
		break;
	case VO_BRIF:
		if (fl & VIF_LIKELY) put(p, " likely");
		if (fl & VIF_UNLIKELY) put(p, " unlikely");
		break;
	}
}

static const char *cc_name(uint32_t op, uint32_t cc, char *t)
{
	if ((op == VO_ICMP || op == VO_VICMP) && cc < 10)
		return vir_icc[cc];
	if ((op == VO_FCMP || op == VO_VFCMP) && cc < 8)
		return vir_fcc[cc];
	sprintf(t, "cc%u", cc);
	return t;
}

static void put_group_at(pctx *p, uint32_t off)
{
	const vir_fn *f = p->f;

	if (off && (uint64_t)off + 2 <= f->nextra)
		vir_print_group(p->b, f->extra[off], f->extra[off + 1]);
	else
		put(p, "?");
}

static void put_list_vals(pctx *p, uint32_t base, uint32_t n, const uint32_t *tys)
{
	const vir_fn *f = p->f;

	for (uint32_t i = 0; i < n; i++) {
		uint32_t v = (uint64_t)base + i < f->nextra ? f->extra[base + i] : 0;

		if (i)
			put(p, ", ");
		put_val(p, v, is_const(p, v) && !(tys && tys[i]));
	}
}

static void put_ins(pctx *p, const vir_ins *in, uint32_t idx)
{
	const vir_opinfo *oi = &vir_optab[in->op];
	const vir_fn *f = p->f;
	char t[16];
	uint32_t ne = f->nextra, nop = 0;

	put(p, "  ");
	if (!oi->name) {
		putf(p, "op%u", in->op);
		return;
	}
	if (in->ty != VT_VOID && p->nm[idx] != VIR_NOBLK)
		putf(p, "v%u:%s = ", p->nm[idx], in->ty < VT_N ? vir_tyname[in->ty] : "?");
	switch (oi->form) {
	case FM_CC:
		putf(p, "%s.%s ", oi->name, cc_name(in->op, in->fl & 15, t));
		put_opnd(p, in, 0);
		put(p, ", ");
		put_opnd(p, in, 1);
		return;
	case FM_EXT:
		putf(p, "ext.%u%c ", 1u << VIF_W(in->fl), in->fl & VIF_MS ? 's' : 'u');
		put_opnd(p, in, 0);
		return;
	case FM_LOAD:
		putf(p, "load.%u", 1u << VIF_W(in->fl));
		if (in->ty == VT_I32 || in->ty == VT_I64 || (in->fl & VIF_MS))
			putf(p, "%c", in->fl & VIF_MS ? 's' : 'u');
		put(p, " ");
		put_opnd(p, in, 0);
		put_flags(p, in);
		return;
	case FM_STORE:
		putf(p, "store.%u ", 1u << VIF_W(in->fl));
		put_opnd(p, in, 0);
		put(p, ", ");
		put_opnd(p, in, 1);
		put_flags(p, in);
		return;
	case FM_ATOM:
		putf(p, "%s.%u ", oi->name, 1u << VIF_W(in->fl));
		for (uint32_t k = 0; k < 3 && oi->k[k] != OK_NONE; k++) {
			if (k)
				put(p, ", ");
			put_opnd(p, in, k);
		}
		put_flags(p, in);
		return;
	case FM_MEMCPY:
		putf(p, "%s ", oi->name);
		put_opnd(p, in, 0);
		put(p, ", ");
		put_opnd(p, in, 1);
		put(p, ", ");
		put_opnd(p, in, 2);
		put_flags(p, in);
		return;
	case FM_PARAM:
		if (in->a == VG_RESULT)
			putf(p, "param 0xffff.%u", in->b);
		else
			putf(p, "param %u.%u", in->a, in->b);
		return;
	case FM_TLS:
		putf(p, "tlsaddr ");
		put_sym(p, in->a, 0);
		putf(p, ", %u", in->b);
		return;
	case FM_VAARG:
		put(p, "va_arg ");
		put_opnd(p, in, 0);
		put(p, ", ");
		put_group_at(p, in->b);
		return;
	case FM_CALL: {
		uint32_t si = sig_index(p, in->b), n = in->c < ne ? f->extra[in->c] : 0;

		put(p, "call ");
		put_opnd(p, in, 0);
		if (si != VIR_NOBLK)
			putf(p, " %%s%u (", si);
		else
			put(p, " %?? (");
		if ((uint64_t)in->c + 1 + n > ne)
			n = 0;
		put_list_vals(p, in->c + 1, n, NULL);
		put(p, ")");
		put_flags(p, in);
		return;
	}
	case FM_ASM: case FM_ASMGOTO: {
		uint32_t no = 0, ni = 0, b = in->b;

		putf(p, "%s !%u outs(", oi->name, in->a);
		if ((uint64_t)b + 2 <= ne) {
			no = f->extra[b];
			ni = f->extra[b + 1];
		}
		if ((uint64_t)b + 2 + no + 2 * (uint64_t)ni > ne)
			no = ni = 0;
		for (uint32_t i = 0; i < no; i++)
			putf(p, "%s%u", i ? ", " : "", f->extra[b + 2 + i]);
		put(p, ") ins(");
		for (uint32_t i = 0; i < ni; i++) {
			uint32_t d = f->extra[b + 2 + no + 2 * i], v = f->extra[b + 3 + no + 2 * i];

			putf(p, "%s%u ", i ? ", " : "", d);
			put_val(p, v, is_const(p, v));
		}
		put(p, ")");
		if (oi->form == FM_ASMGOTO) {
			uint32_t n = in->c < ne ? f->extra[in->c] : 0;

			if ((uint64_t)in->c + 2 + n > ne)
				n = 0, nop = 1;
			putf(p, " -> b%u [", nop ? 0 : f->extra[in->c + 1]);
			for (uint32_t i = 0; i < n; i++)
				putf(p, "%sb%u", i ? ", " : "", f->extra[in->c + 2 + i]);
			put(p, "]");
		}
		return;
	}
	case FM_BR: {
		uint32_t n = in->a < f->nblk ? f->blk[in->a].nparam : 0;

		putf(p, "br b%u", in->a);
		if (n && (uint64_t)in->c + n <= ne) {
			put(p, "(");
			for (uint32_t i = 0; i < n; i++) {
				uint32_t v = f->extra[in->c + i], want = f->blk[in->a].first + i < f->nins ?
					f->ins[f->blk[in->a].first + i].ty : 0;
				bool typed = is_const(p, v) && (!want || want >= VT_N || f->ins[v].ty != want);

				if (i)
					put(p, ", ");
				put_val(p, v, typed);
			}
			put(p, ")");
		}
		return;
	}
	case FM_SWITCH: {
		uint32_t n = in->c < ne ? f->extra[in->c] : 0, ty = in->a < f->nins ? f->ins[in->a].ty : 0;
		uint32_t st = ty == VT_I64 ? 5 : 3;

		if ((uint64_t)in->c + 1 + (uint64_t)n * st > ne)
			n = 0;
		put(p, "switch ");
		put_opnd(p, in, 0);
		putf(p, ", b%u [", in->b);
		for (uint32_t i = 0; i < n; i++) {
			const uint32_t *w = &f->extra[in->c + 1 + i * st];
			uint64_t lo, hi;
			bool wide = ty == VT_I64;

			lo = wide ? (uint64_t)w[0] | (uint64_t)w[1] << 32 : w[0];
			hi = wide ? (uint64_t)w[2] | (uint64_t)w[3] << 32 : w[1];
			if (i)
				put(p, ", ");
			if (wide)
				putf(p, "%lld", (long long)lo);
			else
				putf(p, "%d", (int32_t)lo);
			if (hi != lo) {
				if (wide)
					putf(p, "..%lld", (long long)hi);
				else
					putf(p, "..%d", (int32_t)hi);
			}
			putf(p, ": b%u", w[st - 1]);
		}
		put(p, "]");
		return;
	}
	case FM_IBR: {
		uint32_t n = in->c < ne ? f->extra[in->c] : 0;

		if ((uint64_t)in->c + 1 + n > ne)
			n = 0;
		put(p, "ibr ");
		put_opnd(p, in, 0);
		put(p, " [");
		for (uint32_t i = 0; i < n; i++)
			putf(p, "%sb%u", i ? ", " : "", f->extra[in->c + 1 + i]);
		put(p, "]");
		return;
	}
	case FM_RET: {
		uint32_t n = in->c < ne ? f->extra[in->c] : 0;

		if ((uint64_t)in->c + 1 + n > ne)
			n = 0;
		put(p, "ret");
		if (n)
			put(p, " ");
		put_list_vals(p, in->c + 1, n, NULL);
		return;
	}
	case FM_NONE:
		put(p, oi->name);
		return;
	default:
		break;
	}
	/* FM_STD: the operands the table lists, in order */
	put(p, oi->name);
	for (uint32_t k = 0; k < 3; k++) {
		uint32_t kd = oi->k[k], v = k == 0 ? in->a : k == 1 ? in->b : in->c;

		if (kd == OK_NONE)
			continue;
		put(p, nop++ ? ", " : " ");
		if (kd == OK_IMM)
			putf(p, "%u", v);
		else if (kd == OK_BLK)
			putf(p, "b%u", v);
		else
			put_opnd(p, in, k);
	}
	put_flags(p, in);
}

/* ---------------- functions and units ---------------- */

static void put_fn(pctx *p, const vir_fn *f)
{
	const vir_unit *u = p->u;
	uint32_t si = sig_index(p, f->sig);

	putf(p, "func ");
	put_sym(p, f->sym, 0);
	if (si != VIR_NOBLK)
		putf(p, " %%s%u", si);
	else
		put(p, " %??");
	if (f->fl & VF_NOFP) put(p, " nofp");
	if (f->fl & VF_FENV) put(p, " fenv");
	if (f->fl & VF_NULLINVALID) put(p, " nullinvalid");
	if (f->fl & VF_NORETURN) put(p, " noreturn");
	if (f->fl & VF_LOCAL) put(p, " local");
	if (f->fl & VF_EXPOSURE) put(p, " exposure");
	put(p, " {\n");
	(void)u;
	for (uint32_t i = 0; i < f->nslot; i++) {
		const vir_slot *s = &f->slot[i];

		putf(p, "  slot $%u size %llu align %u", i, (unsigned long long)s->size, s->align);
		if (s->hint != INT64_MIN)
			putf(p, " hint %lld", (long long)s->hint);
		if (s->fl & VS_PARAM)
			put(p, " param");
		if (s->fl & VS_EXPOSED)
			put(p, " exposed");
		put(p, "\n");
	}
	for (uint32_t b = 0; b < f->nblk; b++) {
		const vir_blk *k = &f->blk[b];
		uint32_t np = 0;

		putf(p, "b%u", b);
		for (uint32_t i = k->first; i < k->first + k->n && i < f->nins; i++) {
			if (vir_is_const_op(f->ins[i].op))
				continue;
			if (f->ins[i].op != VO_BPARAM || np >= k->nparam)
				break;
			put(p, np++ ? ", " : "(");
			putf(p, "v%u:%s", p->nm[i], f->ins[i].ty < VT_N ? vir_tyname[f->ins[i].ty] : "?");
		}
		if (np)
			put(p, ")");
		if (k->fl & VB_LOOP) put(p, " loop");
		if (k->fl & VB_ADDR) put(p, " addr");
		if (k->fl & VB_COLD) put(p, " cold");
		put(p, ":\n");
		{
			uint32_t skip = np;

			for (uint32_t i = k->first; i < k->first + k->n && i < f->nins; i++) {
				if (vir_is_const_op(f->ins[i].op))
					continue;
				if (skip) {
					skip--;
					continue;
				}
				put_ins(p, &f->ins[i], i);
				put(p, "\n");
			}
		}
	}
	put(p, "}\n");
}

void vir_print_fn(vbuf *b, const vir_unit *u, const vir_fn *f)
{
	pctx p = { b, u, f, f->tmp };
	uint32_t *nm = f->tmp, n = 0;

	/* names: v0.. in order of appearance */
	for (uint32_t i = 0; i < f->nins; i++)
		nm[i] = VIR_NOBLK;
	for (uint32_t k = 0; k < f->nblk; k++)
		for (uint32_t i = f->blk[k].first; i < f->blk[k].first + f->blk[k].n && i < f->nins; i++)
			if (!vir_is_const_op(f->ins[i].op) && f->ins[i].ty != VT_VOID)
				nm[i] = n++;
	put_fn(&p, f);
}

static void put_items(pctx *p, const vir_unit *u, const vir_usym *s)
{
	for (uint32_t i = 0; i < s->nitem; i++) {
		const vir_item *it = &u->item[s->item0 + i];

		put(p, i ? ", " : " ");
		if (it->ty == 0xff) {
			putf(p, "zero %llu", (unsigned long long)it->v);
		} else if (it->ty == 0xfe) {
			put(p, "bytes ");
			put_bytes(p, it->bytes, it->v);
		} else if (it->ty == VT_I32) {
			putf(p, "i32 %d", (int32_t)it->v);
		} else if (it->ty == VT_I64) {
			putf(p, "i64 %lld", (long long)it->v);
		} else if (it->ty == VT_F32 || it->ty == VT_F64) {
			char t[48];

			fmt_hexfloat(t, it->v, it->ty == VT_F32 ? 8 : 11, it->ty == VT_F32 ? 23 : 52);
			putf(p, "%s %s", vir_tyname[it->ty], t);
		} else if (it->sym) {
			put(p, "ptr ");
			put_sym(p, it->sym - 1, (int64_t)it->v);
		} else {
			putf(p, "ptr 0x%llx", (unsigned long long)it->v);
		}
	}
}

void vir_print_unit(vbuf *b, const vir_unit *u)
{
	pctx p = { b, u, NULL, NULL };

	putf(&p, "vir %u\ntarget %s\n", u->version, vir_target_name(u->target));
	for (uint32_t i = 0; i < u->nsym; i++)
		if (u->sym[i].kind == VU_EXTERN)
			putf(&p, "extern @%s\n", u->sym[i].name);
	for (uint32_t i = 0; i < u->nsym; i++) {
		const vir_usym *s = &u->sym[i];

		if (s->kind != VU_DATA && s->kind != VU_TDATA)
			continue;
		putf(&p, "%s @%s align %u = {", s->kind == VU_TDATA ? "tdata" : "data", s->name, s->align);
		put_items(&p, u, s);
		put(&p, " }\n");
	}
	for (uint32_t i = 0; i < u->nsig; i++) {
		putf(&p, "sig %%s%u = ", i);
		vir_print_sig(b, &u->sigw[u->sig[i].off]);
		put(&p, "\n");
	}
	for (uint32_t i = 0; i < u->nas; i++) {
		const vir_uasm *a = &u->as[i];

		putf(&p, "asm !%u ", i);
		put_str(&p, a->tmpl);
		put(&p, " outs(");
		for (uint32_t k = 0; k < a->nout; k++) {
			put(&p, k ? ", " : "");
			put_str(&p, a->outs[k]);
		}
		put(&p, ") ins(");
		for (uint32_t k = 0; k < a->nin; k++) {
			put(&p, k ? ", " : "");
			put_str(&p, a->ins[k]);
		}
		put(&p, ") clobber(");
		for (uint32_t k = 0; k < a->nclob; k++) {
			put(&p, k ? ", " : "");
			put_str(&p, a->clob[k]);
		}
		put(&p, ")");
		if (a->labels)
			putf(&p, " labels %u", a->labels);
		put(&p, "\n");
	}
	for (uint32_t i = 0; i < u->nfn; i++) {
		vir_fn f;

		vir_fn_load(&f, u, i);
		put(&p, "\n");
		vir_print_fn(b, u, &f);
	}
}
