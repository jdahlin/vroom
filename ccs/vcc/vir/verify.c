/*
 * The verifier (SPEC-vir.md 12): every rule S1-S5, T1-T6, C1-C7, D1, A1-A4,
 * F1-F2, M1, N1, each failure reported with its rule id, the function, the
 * block and the instruction as `@f bN.I: RULE: message` (I: the index in
 * the block).
 *
 * Phase 1 needs no graph (structure, types, flags, signatures); it stops
 * there when the blocks do not tile the function or a reference is out of
 * range, since the graph rules read them.  Phase 2 builds the graph and the
 * dominators and checks C, D and N.
 */
#include "virimpl.h"

typedef struct vctx {
	vir_fn *f;
	const vir_unit *u;
	vbuf *out;
	uint32_t max, nerr;
	uint32_t *vb;		/* value -> its block (VIR_NOBLK: a constant) */
	bool stop;		/* phase 1 found what makes the graph unusable */
} vctx;

static void vfail(vctx *v, const char *rule, uint32_t blk, uint32_t ins, const char *fmt, ...)
	__attribute__((format(printf, 5, 6)));
static void vfail(vctx *v, const char *rule, uint32_t blk, uint32_t ins, const char *fmt, ...)
{
	va_list ap;
	char t[300];

	if (v->nerr++ >= v->max)
		return;
	vbuf_f(v->out, "@%s", vir_fname(v->u, v->f));
	if (blk != VIR_NOBLK && ins != VIR_NOBLK)
		vbuf_f(v->out, " b%u.%u", blk, ins);
	else if (blk != VIR_NOBLK)
		vbuf_f(v->out, " b%u", blk);
	vbuf_f(v->out, ": %s: ", rule);
	va_start(ap, fmt);
	vsnprintf(t, sizeof(t), fmt, ap);
	va_end(ap);
	vbuf_s(v->out, t);
	vbuf_s(v->out, "\n");
}

const char *vir_fname(const vir_unit *u, const vir_fn *f)
{
	return u && f->sym < u->nsym ? u->sym[f->sym].name : "?";
}

static const char *tyname(uint32_t ty) { return ty < VT_N ? vir_tyname[ty] : "?"; }
static const char *opname(const vir_ins *in) { return vir_optab[in->op].name ? vir_optab[in->op].name : "?"; }

static bool is_valty(uint32_t ty) { return (ty >= VT_I32 && ty <= VT_PTR) || vir_isvec(ty); }

/* does a value of type ty satisfy operand kind k of an instruction whose result is rty? */
static bool opnd_ok(uint32_t k, uint32_t ty, uint32_t rty)
{
	switch (k) {
	case OK_T: return ty == rty;
	case OK_I: return ty == VT_I32 || ty == VT_I64;
	case OK_I32: return ty == VT_I32;
	case OK_I64: case OK_CONSTV: return ty == VT_I64;
	case OK_PTR: return ty == VT_PTR;
	case OK_F32: return ty == VT_F32;
	case OK_F64: return ty == VT_F64;
	case OK_F: return ty == VT_F32 || ty == VT_F64;
	case OK_ANY: return is_valty(ty);
	case OK_X: return ty == VT_I32 || ty == VT_I64 || ty == VT_PTR;
	case OK_FP: return ty == VT_F32 || ty == VT_F64;
	case OK_VI: return ty == VT_I32X4;
	case OK_VF: return ty == VT_F32X4;
	case OK_ELEM: return vir_isvec(rty) && ty == vir_velem(rty);
	case OK_MULTI: return ty == VT_MULTI;
	case OK_BITC:
		return (rty == VT_I32 && ty == VT_F32) || (rty == VT_F32 && ty == VT_I32) ||
		       (rty == VT_I64 && ty == VT_F64) || (rty == VT_F64 && ty == VT_I64);
	}
	return false;
}

static bool vk_is_value(uint32_t k) { return (k >= OK_T && k <= OK_BITC) || k == OK_CONSTV; }

/* ---------------- signatures ---------------- */

/* are the words of a signature at off well formed? (reports nothing) */
static const char *sig_check(const vir_fn *f, uint32_t off)
{
	uint32_t ng;

	if (off == 0 || off >= f->nextra)
		return "the signature offset is out of range";
	ng = VSIG_NGROUPS(f->extra[off]);
	if ((uint64_t)off + 3 + 2 * (uint64_t)ng > f->nextra)
		return "the signature is cut off";
	for (uint32_t g = 0; g <= ng; g++) {
		uint32_t w0 = f->extra[off + 1 + 2 * g], w1 = f->extra[off + 2 + 2 * g], k = VG_KIND(w0), n = VG_NPIECE(w0);

		if (k == VG_REG) {
			if (n < 1 || n > 4)
				return "a register group has 1 to 4 pieces";
			for (uint32_t i = 0; i < 4; i++)
				if ((i < n) != (VG_CLASS(w0, i) != VC_NONE) || VG_CLASS(w0, i) > VC_F64)
					return "the piece classes do not match the piece count";
		} else if (k == VG_MEM) {
			if (n || VG_SIZE(w1) == 0)
				return "a memory group has a size and no pieces";
		} else if (k != VG_NONE && k != VG_SRET) {
			return "unknown group kind";
		}
		if (k == VG_SRET && g > 0)
			return "sret is only the result group";
		if (g == 0 && k == VG_MEM)
			return "a result is not a memory group";
	}
	return NULL;
}

/* the value types a group's words must have, in order: appends to ty[]; returns the count */
static uint32_t group_types(uint32_t w0, uint8_t *ty, bool sret_first)
{
	uint32_t n = 0;

	switch (VG_KIND(w0)) {
	case VG_REG:
		for (uint32_t i = 0; i < VG_NPIECE(w0); i++)
			ty[n++] = (uint8_t)(VG_CLASS(w0, i) == VC_F32 ? VT_F32 : VG_CLASS(w0, i) == VC_F64 ? VT_F64 : 0xfe);
		break;
	case VG_MEM:
		ty[n++] = VT_PTR;
		break;
	case VG_SRET:
		if (sret_first)
			ty[n++] = VT_PTR;
		break;
	}
	return n;
}

/* 0xfe: a full register, i64 or ptr */
static bool word_ty_ok(uint32_t want, uint32_t have)
{
	return want == 0xfe ? have == VT_I64 || have == VT_PTR : want == have;
}

/* ---------------- exposure (6.4) ---------------- */

/* Backward exposure edges: a derived value -> each pointer it depends on.
 * A single traversal classifies every slot, including cycles and backedges. */
typedef struct vex_edge { uint32_t input, next; } vex_edge;
typedef struct vex_graph { void *mem; uint32_t *head, *queue; vex_edge *edge; uint8_t *slot; } vex_graph;
enum { VEX_SAFE, VEX_SINK, VEX_FLOW };

/* Classify an operand use exactly as the exposure contract in 6.4. For a
 * propagation, out is the derived value; for a branch it is its parameter. */
static int exposure_use(vir_fn *f, const vir_ins *x, uint32_t ins, uint32_t pos, uint32_t *out)
{
	switch (x->op) {
	case VO_LOAD: case VO_ALOAD: case VO_ASTORE: case VO_AXCHG: case VO_AXADD: case VO_ACAS:
	case VO_VA_START: case VO_VA_ARG: case VO_MEMSET:
		return pos == 0 ? VEX_SAFE : VEX_SINK;
	case VO_STORE:
		return pos == 1 ? VEX_SINK : VEX_SAFE;
	case VO_MEMCPY: case VO_PSUB: case VO_ICMP:
		return pos > 1 ? VEX_SINK : VEX_SAFE;
	case VO_PTRADD:
		if (pos != 0) return VEX_SAFE;
		*out = ins;
		return VEX_FLOW;
	case VO_SELECT:
		if (pos == 0) return VEX_SINK;
		*out = ins;
		return VEX_FLOW;
	case VO_BR:
		if (x->a >= f->nblk || pos >= f->blk[x->a].nparam) return VEX_SAFE;
		*out = f->blk[x->a].first + pos;
		return *out < f->nins ? VEX_FLOW : VEX_SAFE;
	default:
		return VEX_SINK;
	}
}

static void exposure_enqueue(uint8_t *mark, uint32_t *queue, uint32_t *tail, uint32_t v)
{
	if (!mark[v]) {
		mark[v] = 1;
		queue[(*tail)++] = v;
	}
}

/* Build the propagation edges in two passes so one checked allocation holds
 * all scratch. Allocation or representation failure rejects verification;
 * it can never turn an unknown exposure into an accepted claim. */
static bool exposure_build(vir_fn *f, uint8_t *mark, vex_graph *g)
{
	size_t nedges = 0, heads, edges, total;
	uint32_t at = 0, first = 0, last = 0;

	memset(g, 0, sizeof(*g));
	for (uint32_t i = f->nconst; i < f->nins; i++) {
		vir_ins x = f->ins[i];
		vir_oit it;
		uint32_t *p, out;

		if (vir_is_const_op(x.op)) continue;
		vir_oit_init(&it, f, &x, false);
		while ((p = vir_oit_next(&it))) {
			uint32_t pos = x.op == VO_BR ? (uint32_t)(p - &f->extra[x.c]) : it.pos;
			if (*p < f->nins && exposure_use(f, &x, i, pos, &out) == VEX_FLOW && ++nedges >= UINT32_MAX)
				return false;
		}
	}
	if (__builtin_mul_overflow((size_t)f->nins, sizeof(*g->head), &heads) ||
	    __builtin_mul_overflow(nedges, sizeof(*g->edge), &edges) ||
	    __builtin_add_overflow(heads, heads, &total) ||
	    __builtin_add_overflow(total, edges, &total) ||
	    __builtin_add_overflow(total, (size_t)f->nslot, &total)) return false;
	g->mem = malloc(total);
	if (!g->mem) return false;
	g->head = g->mem;
	g->queue = g->head + f->nins;
	g->edge = (vex_edge *)(g->queue + f->nins);
	g->slot = (uint8_t *)(g->edge + nedges);
	memset(g->head, 0xff, heads);
	memset(g->slot, 0, f->nslot);
	memset(mark, 0, f->nins);
	for (uint32_t i = f->nconst; i < f->nins; i++) {
		vir_ins x = f->ins[i];
		vir_oit it;
		uint32_t *p, out;

		if (vir_is_const_op(x.op)) continue;
		vir_oit_init(&it, f, &x, false);
		while ((p = vir_oit_next(&it))) {
			uint32_t v = *p, pos = x.op == VO_BR ? (uint32_t)(p - &f->extra[x.c]) : it.pos;
			int kind;

			if (v >= f->nins) continue;
			kind = exposure_use(f, &x, i, pos, &out);
			if (kind == VEX_SINK) exposure_enqueue(mark, g->queue, &last, v);
			else if (kind == VEX_FLOW) {
				g->edge[at] = (vex_edge){ v, g->head[out] };
				g->head[out] = at++;
			}
		}
	}
	while (first < last) {
		uint32_t v = g->queue[first++];

		for (uint32_t i = g->head[v]; i != UINT32_MAX; i = g->edge[i].next)
			exposure_enqueue(mark, g->queue, &last, g->edge[i].input);
	}
	for (uint32_t i = 1; i < f->nconst; i++)
		if (mark[i] && f->ins[i].op == VO_SLOTADDR && f->ins[i].a < f->nslot)
			g->slot[f->ins[i].a] = 1;
	return true;
}

/* ---------------- phase 1: per instruction ---------------- */

/* the words of the list of in that hold values, with a bounds check: reports T2 once */
static bool check_lists_in_range(vctx *v, uint32_t b, uint32_t i, const vir_ins *in)
{
	vir_fn *f = v->f;
	uint32_t op = in->op, ne = f->nextra;
	bool bad = false;

	switch (op) {
	case VO_CALL: case VO_RET: bad = in->c == 0 || in->c >= ne || (uint64_t)in->c + 1 + f->extra[in->c] > ne; break;
	case VO_BR: {
		uint32_t np = in->a < f->nblk ? f->blk[in->a].nparam : 0;

		bad = np && (in->c == 0 || (uint64_t)in->c + np > ne);
		break;
	}
	case VO_SWITCH: {
		uint32_t ty = in->a < f->nins ? f->ins[in->a].ty : 0, st = ty == VT_I64 ? 5 : 3;

		bad = in->c == 0 || in->c >= ne || (uint64_t)in->c + 1 + (uint64_t)f->extra[in->c] * st > ne;
		break;
	}
	case VO_IBR: bad = in->c == 0 || in->c >= ne || (uint64_t)in->c + 1 + f->extra[in->c] > ne; break;
	case VO_ASM: case VO_ASMGOTO:
		bad = in->b == 0 || (uint64_t)in->b + 2 > ne ||
		      (uint64_t)in->b + 2 + f->extra[in->b] + 2 * (uint64_t)f->extra[in->b + 1] > ne;
		if (!bad && op == VO_ASMGOTO)
			bad = in->c == 0 || in->c >= ne || (uint64_t)in->c + 2 + f->extra[in->c] > ne;
		break;
	case VO_VA_ARG: bad = in->b == 0 || (uint64_t)in->b + 2 > ne; break;
	}
	if (bad) {
		vfail(v, "T2", b, i, "%s: an operand list is out of range", opname(in));
		v->stop = true;
	}
	return !bad;
}

static void check_ins(vctx *v, uint32_t b, uint32_t i, uint32_t idx)
{
	vir_fn *f = v->f;
	vir_ins *in = &f->ins[idx];
	const vir_opinfo *oi = &vir_optab[in->op];
	uint32_t op = in->op, opty[3] = { 0, 0, 0 };

	/* T1 */
	if (op >= VO_TARGET0) {
		vfail(v, "T1", b, i, "opcode %u is reserved for target instructions", op);
		return;
	}
	if (!oi->name) {
		vfail(v, "T1", b, i, "%u is not an opcode", op);
		return;
	}
	if (in->ty >= VT_N || !(oi->tmask >> in->ty & 1)) {
		vfail(v, "T1", b, i, "%s cannot have result type %s", oi->name, tyname(in->ty));
		return;
	}
	if (!check_lists_in_range(v, b, i, in))
		return;
	/* T2: value operands exist and have the type the table requires */
	for (uint32_t k = 0; k < 3; k++) {
		uint32_t kd = oi->k[k], val = k == 0 ? in->a : k == 1 ? in->b : in->c;

		if (kd == OK_NONE) {
			if (val) {
				vfail(v, "T2", b, i, "%s: operand %c must be 0", oi->name, 'a' + k);
				return;
			}
			continue;
		}
		if (!vk_is_value(kd))
			continue;
		if (val == 0 || val >= f->nins || (!is_valty(f->ins[val].ty) && f->ins[val].ty != VT_MULTI)) {
			vfail(v, "T2", b, i, "%s: operand %c is not a value", oi->name, 'a' + k);
			return;
		}
		opty[k] = f->ins[val].ty;
		if (!opnd_ok(kd, opty[k], in->ty)) {
			vfail(v, "T2", b, i, "%s: operand %c has type %s", oi->name, 'a' + k, tyname(opty[k]));
			return;
		}
	}
	if ((op == VO_ICMP || op == VO_FCMP) && opty[0] != opty[1]) {
		vfail(v, "T2", b, i, "%s: the operands have different types (%s, %s)", oi->name, tyname(opty[0]),
		      tyname(opty[1]));
		return;
	}
	/* the list operands exist and are values (their types: A1, A2, C3, A4) */
	{
		vir_ins x = *in;
		vir_oit it;
		uint32_t *p;

		vir_oit_init(&it, f, &x, false);
		while ((p = vir_oit_next(&it)))
			if (it.pos == 3 && (*p == 0 || *p >= f->nins || !is_valty(f->ins[*p].ty))) {
				vfail(v, "T2", b, i, "%s: an operand in a list is not a value", oi->name);
				return;
			}
	}
	/* T3: flags */
	if (in->fl & ~oi->flmask) {
		vfail(v, "T3", b, i, "%s: flag bits 0x%x are not allowed", oi->name, in->fl & ~oi->flmask);
		return;
	}
	if (op == VO_VICMP && (in->fl & 15) > 9) {
		vfail(v, "T3", b, i, "vicmp: condition code %u is not allowed", in->fl & 15);
		return;
	}
	if (op == VO_VFCMP && (in->fl & 15) > 7) {
		vfail(v, "T3", b, i, "vfcmp: condition code %u is not allowed", in->fl & 15);
		return;
	}
	if (op == VO_ICMP && ((in->fl & 15) > 9 || (opty[0] == VT_PTR && (in->fl & 15) >= 2 && (in->fl & 15) <= 5))) {
		vfail(v, "T3", b, i, "icmp: condition code %u is not allowed%s", in->fl & 15,
		      opty[0] == VT_PTR ? " on ptr" : "");
		return;
	}
	if (op == VO_FCMP && (in->fl & 15) > 7) {
		vfail(v, "T3", b, i, "fcmp: condition code %u is not allowed", in->fl & 15);
		return;
	}
	/* T4: widths */
	{
		uint32_t w = 1u << VIF_W(in->fl), vt = 0;
		bool chk = false;

		if (op == VO_LOAD || op == VO_ALOAD || op == VO_AXCHG || op == VO_AXADD || op == VO_ACAS)
			vt = in->ty, chk = true;
		else if (op == VO_STORE || op == VO_ASTORE)
			vt = opty[1], chk = true;
		else if (op == VO_EXT)
			vt = opty[0], chk = true;
		if (chk) {
			uint32_t sz = vir_tysize(vir_velem(vt));	/* (a vector's width is its lanes') */
			bool fp = vt == VT_F32 || vt == VT_F64 || vt == VT_PTR || vir_isvec(vt);

			if ((fp && w != sz) || (!fp && w > sz)) {
				vfail(v, "T4", b, i, "%s: width %u does not fit %s", oi->name, w, tyname(vt));
				return;
			}
			if (op == VO_LOAD && (in->fl & VIF_MS) && (vt == VT_F32 || vt == VT_F64 || vt == VT_PTR || vir_isvec(vt))) {
				vfail(v, "T4", b, i, "load: sign extension of %s", tyname(vt));
				return;
			}
		}
	}
	/* T5 */
	if (op == VO_MEMCPY || op == VO_MEMSET) {
		if (f->ins[in->c].op != VO_ICONST) {
			vfail(v, "T5", b, i, "%s: the size is not a constant", oi->name);
			return;
		}
	}
	/* T6 */
	if (op == VO_PROJ) {
		const vir_ins *pr = &f->ins[in->a];
		uint32_t n = 0;
		uint8_t pt[8];

		if (pr->op == VO_CALL) {
			uint32_t off = pr->b;
			const char *e = sig_check(f, off);

			if (e) {
				vfail(v, "T6", b, i, "proj: the producer's signature is bad");
				return;
			}
			n = group_types(f->extra[off + 1], pt, false);
			if (n > 4)
				n = 4;
			if (in->b >= n) {
				vfail(v, "T6", b, i, "proj: index %u, the result has %u pieces", in->b, n);
				return;
			}
			if (!word_ty_ok(pt[in->b], in->ty)) {
				vfail(v, "T6", b, i, "proj: the piece is %s, not %s",
				      pt[in->b] == 0xfe ? "i64 or ptr" : tyname(pt[in->b]), tyname(in->ty));
				return;
			}
		} else if ((pr->op == VO_ASM || pr->op == VO_ASMGOTO) && pr->b < f->nextra) {
			uint32_t no = f->extra[pr->b];

			if (in->b >= no) {
				vfail(v, "T6", b, i, "proj: index %u, the result has %u pieces", in->b, no);
				return;
			}
		} else {
			vfail(v, "T6", b, i, "proj: the producer is %s, not a call or asm", opname(pr));
			return;
		}
	}
}

/* ---------------- phase 2 ---------------- */

static bool asm_is_mem(const char *c)
{
	for (; *c; c++)
		if (*c == 'm' || *c == 'o' || *c == 'V' || *c == 'Q')
			return true;
	return false;
}

static void check_asm(vctx *v, uint32_t b, uint32_t i, const vir_ins *in)
{
	vir_fn *f = v->f;
	const vir_uasm *a;
	uint32_t no = f->extra[in->b], ni = f->extra[in->b + 1], memout = 0, regout = 0, nops;

	if (!v->u)
		return;
	if (in->a >= v->u->nas) {
		vfail(v, "A4", b, i, "asm template !%u does not exist", in->a);
		return;
	}
	a = &v->u->as[in->a];
	nops = a->nout + a->nin;
	for (uint32_t k = 0; k < a->nout; k++)
		asm_is_mem(a->outs[k]) ? memout++ : regout++;
	if (no != regout || ni != a->nin + memout) {
		vfail(v, "A4", b, i, "asm !%u has %u register outputs and %u inputs, the instruction %u and %u", in->a, regout,
		      a->nin + memout, no, ni);
		return;
	}
	if (no == 0 ? in->ty != VT_VOID : no == 1 ? !is_valty(in->ty) : in->ty != VT_MULTI) {
		vfail(v, "A4", b, i, "asm with %u outputs has result type %s", no, tyname(in->ty));
		return;
	}
	for (uint32_t k = 0; k < ni; k++) {
		uint32_t d = f->extra[in->b + 2 + no + 2 * k], val = f->extra[in->b + 3 + no + 2 * k];
		const char *c = d < a->nout ? a->outs[d] : d < nops ? a->ins[d - a->nout] : NULL;

		if (!c) {
			vfail(v, "A4", b, i, "operand %u does not exist in the template", d);
			return;
		}
		if (asm_is_mem(c) && val < f->nins && f->ins[val].ty != VT_PTR) {
			vfail(v, "A4", b, i, "memory operand %u has a value of type %s, not ptr", d, tyname(f->ins[val].ty));
			return;
		}
	}
	if (in->op == VO_ASMGOTO) {
		if (f->extra[in->c] != a->labels)
			vfail(v, "A4", b, i, "asmgoto lists %u labels, the template has %u", f->extra[in->c], a->labels);
	} else if (a->labels) {
		vfail(v, "A4", b, i, "asm !%u has labels: it must be asmgoto", in->a);
	}
}

/* does a call result of type ty fit the result group w0? */
static bool res_ty_ok(uint32_t w0, uint32_t ty)
{
	uint32_t k = VG_KIND(w0), np = VG_NPIECE(w0);

	if (k == VG_REG && np == 1)
		return VG_CLASS(w0, 0) == VC_INT ? ty == VT_I64 || ty == VT_PTR : VG_CLASS(w0, 0) == VC_F32 ? ty == VT_F32 : ty == VT_F64;
	if (k == VG_REG)
		return ty == VT_MULTI;
	return ty == VT_VOID;
}

static void check_call(vctx *v, uint32_t b, uint32_t i, const vir_ins *in)
{
	vir_fn *f = v->f;
	const char *e = sig_check(f, in->b);
	uint32_t off = in->b, ng, n, expect = 0;
	uint8_t want[1024];

	if (e) {
		vfail(v, "A1", b, i, "call: %s", e);
		return;
	}
	ng = VSIG_NGROUPS(f->extra[off]);
	n = f->extra[in->c];
	if (!res_ty_ok(f->extra[off + 1], in->ty)) {
		vfail(v, "A1", b, i, "call: the result type %s does not fit the result group", tyname(in->ty));
		return;
	}
	if (VG_KIND(f->extra[off + 1]) == VG_SRET)
		want[expect++] = VT_PTR;
	for (uint32_t g = 0; g < ng; g++) {
		uint8_t pt[8];
		uint32_t m = group_types(f->extra[off + 3 + 2 * g], pt, false);

		if (expect + m > sizeof(want)) {
			vfail(v, "A1", b, i, "call: too many argument words for the checker");
			return;
		}
		for (uint32_t k = 0; k < m; k++)
			want[expect++] = pt[k];
	}
	if (n != expect) {
		vfail(v, "A1", b, i, "call: the signature takes %u words, the call passes %u", expect, n);
		return;
	}
	for (uint32_t k = 0; k < n; k++) {
		uint32_t val = f->extra[in->c + 1 + k];

		if (!word_ty_ok(want[k], f->ins[val].ty)) {
			vfail(v, "A1", b, i, "call: argument %u has type %s, the signature wants %s", k, tyname(f->ins[val].ty),
			      want[k] == 0xfe ? "i64 or ptr" : tyname(want[k]));
			return;
		}
	}
}

static void check_ret(vctx *v, uint32_t b, uint32_t i, const vir_ins *in)
{
	vir_fn *f = v->f;
	uint32_t n = f->extra[in->c], m;
	uint8_t pt[8];

	if (sig_check(f, f->sig)) {
		vfail(v, "A2", b, i, "ret: the function's signature is bad");
		return;
	}
	m = group_types(f->extra[f->sig + 1], pt, false);
	if (n != m) {
		vfail(v, "A2", b, i, "ret: the result group has %u pieces, ret returns %u values", m, n);
		return;
	}
	for (uint32_t k = 0; k < n; k++) {
		uint32_t val = f->extra[in->c + 1 + k];

		if (!word_ty_ok(pt[k], f->ins[val].ty)) {
			vfail(v, "A2", b, i, "ret: value %u has type %s, the result wants %s", k, tyname(f->ins[val].ty),
			      pt[k] == 0xfe ? "i64 or ptr" : tyname(pt[k]));
			return;
		}
	}
}

static void check_switch(vctx *v, uint32_t b, uint32_t i, const vir_ins *in)
{
	vir_fn *f = v->f;
	uint32_t ty = f->ins[in->a].ty, st = ty == VT_I64 ? 5 : 3, n = f->extra[in->c];
	uint64_t prev_hi = 0;

	for (uint32_t k = 0; k < n; k++) {
		const uint32_t *w = &f->extra[in->c + 1 + (size_t)k * st];
		uint64_t lo = ty == VT_I64 ? (uint64_t)w[0] | (uint64_t)w[1] << 32 : w[0];
		uint64_t hi = ty == VT_I64 ? (uint64_t)w[2] | (uint64_t)w[3] << 32 : w[1];

		if (lo > hi) {
			vfail(v, "C5", b, i, "switch: case %u has lo above hi", k);
			return;
		}
		if (k && lo <= prev_hi) {
			vfail(v, "C5", b, i, "switch: case %u overlaps or is out of order", k);
			return;
		}
		prev_hi = hi;
	}
}

/* the dominance and use rules for one instruction */
static void check_uses(vctx *v, uint32_t b, uint32_t i, uint32_t idx)
{
	vir_fn *f = v->f;
	vir_ins x = f->ins[idx];
	vir_oit it;
	uint32_t *p;

	if (f->blk[b].idom == VIR_NOBLK)
		return;
	vir_oit_init(&it, f, &x, false);
	while ((p = vir_oit_next(&it))) {
		uint32_t d = *p, db;

		if (d == 0 || d >= f->nins || vir_is_const_op(f->ins[d].op))
			continue;
		db = v->vb[d];
		if (db == VIR_NOBLK || f->blk[db].idom == VIR_NOBLK)
			continue;
		if (f->ins[d].op == VO_ASMGOTO) {	/* only blocks the terminator's block strictly dominates */
			if (db == b || !vir_dominates(f, db, b))
				vfail(v, "D1", b, i, "%s uses the result of an asmgoto in a block that b%u does not strictly dominate",
				      opname(&x), db);
		} else if (db == b) {
			if (d >= idx)
				vfail(v, "D1", b, i, "%s uses a value defined later in the block", opname(&x));
		} else if (!vir_dominates(f, db, b)) {
			vfail(v, "D1", b, i, "%s uses a value of b%u, which does not dominate b%u", opname(&x), db, b);
		} else {
			continue;
		}
		return;
	}
}

static void check_graph(vctx *v)
{
	vir_fn *f = v->f;
	vir_ctx *c = vir_ctx_get();
	uint32_t *mark = c->bs[0];	/* a stamp per block (the successors are in sc, so bs[0] is free) */

	/* C1 */
	if (f->blk[0].nparam)
		vfail(v, "C1", 0, VIR_NOBLK, "block 0 has parameters");
	if (f->blk[0].npred)
		vfail(v, "C1", 0, VIR_NOBLK, "block 0 has predecessors");
	for (uint32_t b = 1; b < f->nblk; b++)
		if (f->blk[b].idom == VIR_NOBLK)
			vfail(v, "C1", b, VIR_NOBLK, "unreachable from block 0");
	/* C2 */
	for (uint32_t b = 1; b < f->nblk; b++)
		if (f->blk[b].idom != VIR_NOBLK && f->blk[b].idom > b)
			vfail(v, "C2", b, VIR_NOBLK, "comes before its immediate dominator b%u", f->blk[b].idom);
	for (uint32_t b = 0; b < f->nblk; b++) {
		const vir_blk *k = &f->blk[b];

		for (uint32_t i = 0; i < k->n; i++) {
			uint32_t idx = k->first + i;
			vir_ins x = f->ins[idx];
			vir_oit it;
			uint32_t *p;

			if (!vir_optab[x.op].name || x.op >= VO_TARGET0)
				continue;
			/* C3 */
			if (x.op == VO_BR && x.a < f->nblk) {
				const vir_blk *t = &f->blk[x.a];

				for (uint32_t j = 0; j < t->nparam; j++) {
					uint32_t val = f->extra[x.c + j], want = f->ins[t->first + j].ty;

					if (f->ins[val].ty != want) {
						vfail(v, "C3", b, i, "br b%u: argument %u has type %s, the parameter %s", x.a, j,
						      tyname(f->ins[val].ty), tyname(want));
						break;
					}
				}
			}
			/* C4, C5, C6, C7 */
			if (x.op == VO_BRIF || x.op == VO_SWITCH || x.op == VO_IBR || x.op == VO_ASMGOTO) {
				vir_oit_init(&it, f, &x, true);
				while ((p = vir_oit_next(&it)))
					if (*p < f->nblk && f->blk[*p].nparam) {
						vfail(v, "C4", b, i, "%s targets b%u, which has parameters", opname(&x), *p);
						break;
					}
			}
			if (x.op == VO_SWITCH)
				check_switch(v, b, i, &x);
			if (x.op == VO_IBR) {
				uint32_t n = f->extra[x.c], stamp = idx + 1;
				bool bad = false;

				for (uint32_t j = 0; j < f->nblk; j++)
					mark[j] = 0;
				for (uint32_t j = 0; j < n && !bad; j++) {
					uint32_t t = f->extra[x.c + 1 + j];

					mark[t] = stamp;
					if (!(f->blk[t].fl & VB_ADDR)) {
						vfail(v, "C6", b, i, "ibr lists b%u, which has no VB_ADDR", t);
						bad = true;
					}
				}
				for (uint32_t j = 0; j < f->nblk && !bad; j++)
					if ((f->blk[j].fl & VB_ADDR) && mark[j] != stamp) {
						vfail(v, "C6", b, i, "ibr does not list b%u, whose address is taken", j);
						bad = true;
					}
			}
			if (x.op == VO_ASMGOTO) {
				vir_oit_init(&it, f, &x, true);
				while ((p = vir_oit_next(&it))) {
					const vir_blk *t = &f->blk[*p];
					bool bad = false;

					for (uint32_t j = 0; j < t->npred; j++)
						if (f->extra[t->pred + j] != b)
							bad = true;
					if (bad || !t->npred) {
						vfail(v, "C7", b, i, "asmgoto target b%u has another predecessor", *p);
						break;
					}
				}
			}
			/* A3, A4, D1 */
			if (x.op == VO_CALL && (x.fl & VIF_CALL_NORETURN) &&
			    (i + 1 >= k->n || f->ins[idx + 1].op != VO_TRAP))
				vfail(v, "A3", b, i, "a noreturn call is not followed by trap");
			if (x.op == VO_CALL)
				check_call(v, b, i, &x);
			if (x.op == VO_RET)
				check_ret(v, b, i, &x);
			if (x.op == VO_ASM || x.op == VO_ASMGOTO)
				check_asm(v, b, i, &x);
			check_uses(v, b, i, idx);
		}
	}
	/* C6 also: a labeladdr must name a VB_ADDR block (S2 reports that) */
}

static void check_misc(vctx *v)
{
	vir_fn *f = v->f;
	vir_ctx *c = vir_ctx_get();
	uint8_t *mark = (uint8_t *)c->aux;

	/* F1 */
	if (f->fl & VF_NOFP)
		for (uint32_t b = 0; b < f->nblk; b++)
			for (uint32_t i = 0; i < f->blk[b].n; i++) {
				const vir_ins *x = &f->ins[f->blk[b].first + i];

				if (x->ty == VT_F32 || x->ty == VT_F64) {
					vfail(v, "F1", b, i, "%s has type %s in a nofp function", opname(x), tyname(x->ty));
					return;
				}
			}
	for (uint32_t i = 1; i < f->nconst && (f->fl & VF_NOFP); i++)
		if (f->ins[i].op == VO_FCONST) {
			vfail(v, "F1", VIR_NOBLK, VIR_NOBLK, "fconst in a nofp function");
			break;
		}
	/* F2 */
	if (!(f->fl & VF_VARIADIC))
		for (uint32_t b = 0; b < f->nblk; b++)
			for (uint32_t i = 0; i < f->blk[b].n; i++)
				if (f->ins[f->blk[b].first + i].op == VO_VA_START) {
					vfail(v, "F2", b, i, "va_start in a function that is not variadic");
					return;
				}
	(void)mark;
}

static void check_slots(vctx *v)
{
	vir_fn *f = v->f;
	uint8_t *mark = (uint8_t *)vir_ctx_get()->aux;
	vex_graph graph = {0};
	bool have_slotaddr = false;

	if ((f->fl & VF_EXPOSURE) && f->nslot)
		for (uint32_t i = 1; i < f->nconst; i++)
			if (f->ins[i].op == VO_SLOTADDR) { have_slotaddr = true; break; }
	if (have_slotaddr && !exposure_build(f, mark, &graph)) {
		vfail(v, "M1", VIR_NOBLK, VIR_NOBLK, "cannot analyze slot exposure: too many uses or out of memory");
		return;
	}
	for (uint32_t s = 0; s < f->nslot; s++) {
		const vir_slot *sl = &f->slot[s];

		if (!sl->align || (sl->align & (sl->align - 1))) {
			vfail(v, "M1", VIR_NOBLK, VIR_NOBLK, "slot $%u: the alignment %u is not a power of two", s, sl->align);
			continue;
		}
		if (sl->size >= ((uint64_t)1 << 40)) {
			vfail(v, "M1", VIR_NOBLK, VIR_NOBLK, "slot $%u: the size %llu is too large", s, (unsigned long long)sl->size);
			continue;
		}
		if (f->fl & VF_EXPOSURE) {
			bool e = have_slotaddr && graph.slot[s], claim = (sl->fl & VS_EXPOSED) != 0;

			if (e != claim)
				vfail(v, "M1", VIR_NOBLK, VIR_NOBLK, "slot $%u is %sexposed, the function says %sexposed", s,
				      e ? "" : "not ", claim ? "" : "not ");
		}
	}
	free(graph.mem);
}

/* N1: canonical form (2.4) */
static void check_canon(vctx *v)
{
	vir_fn *f = v->f;
	uint32_t nxt = 1;
	bool order_bad = false;

	for (uint32_t b = 0; b < f->nblk; b++)
		for (uint32_t i = 0; i < f->blk[b].n; i++) {
			uint32_t idx = f->blk[b].first + i;
			vir_ins x = f->ins[idx];
			vir_oit it;
			uint32_t *p;

			if (x.op == VO_NOP) {
				vfail(v, "N1", b, i, "a nop in the block region");
				return;
			}
			vir_oit_init(&it, f, &x, false);
			while ((p = vir_oit_next(&it))) {
				uint32_t d = *p;

				if (d == 0 || d >= f->nconst)
					continue;
				if (d == nxt)
					nxt++;
				else if (d > nxt && !order_bad) {
					vfail(v, "N1", b, i, "constant %u is used before constant %u: not in order of first use", d, nxt);
					order_bad = true;
				}
			}
		}
	if (!order_bad && nxt < f->nconst)
		vfail(v, "N1", VIR_NOBLK, VIR_NOBLK, "%u constants are not used", f->nconst - nxt);
}

uint32_t vir_verify(vir_fn *f, const vir_unit *u, vbuf *out, uint32_t max)
{
	vctx v = { f, u, out, max, 0, f->tmp, false };
	bool tiled = true;
	uint32_t expect;

	/* S1 */
	if (f->version != 1)
		vfail(&v, "S1", VIR_NOBLK, VIR_NOBLK, "the version is %u, not 1", f->version);
	if (!f->nins || f->ins[0].op != VO_NOP || f->ins[0].ty || f->ins[0].a || f->ins[0].b || f->ins[0].c) {
		vfail(&v, "S1", VIR_NOBLK, VIR_NOBLK, "index 0 is not a nop");
		return v.nerr;
	}
	if (f->nconst < 1 || f->nconst > f->nins) {
		vfail(&v, "S1", VIR_NOBLK, VIR_NOBLK, "the constant region ends at %u (nins %u): not the layout of 2.3", f->nconst,
		      f->nins);
		return v.nerr;
	}
	if (!f->nblk) {
		vfail(&v, "S3", VIR_NOBLK, VIR_NOBLK, "the function has no blocks");
		return v.nerr;
	}
	/* S2: the constant region */
	vir_ctab_reset(vir_ctx_get());
	for (uint32_t i = 1; i < f->nconst; i++) {
		const vir_ins *c = &f->ins[i];
		uint32_t slot = 0;
		const char *why = NULL;

		if (!vir_is_const_op(c->op)) {
			vfail(&v, "S2", VIR_NOBLK, VIR_NOBLK, "index %u in the constant region is %s", i,
			      vir_optab[c->op].name ? vir_optab[c->op].name : "not an opcode");
			continue;
		}
		if (c->fl)
			why = "flags are set";
		switch (c->op) {
		case VO_ICONST:
			if (c->ty != VT_I32 && c->ty != VT_I64 && c->ty != VT_PTR) why = "iconst has type i32, i64 or ptr";
			else if (c->ty == VT_I32 && c->c) why = "an i32 iconst has c 0";
			else if (c->a) why = "iconst has a 0";
			break;
		case VO_FCONST:
			if (c->ty != VT_F32 && c->ty != VT_F64) why = "fconst has type f32 or f64";
			else if (c->ty == VT_F32 && c->c) why = "an f32 fconst has c 0";
			else if (c->a) why = "fconst has a 0";
			break;
		case VO_SYMADDR:
			if (c->ty != VT_PTR) why = "symaddr has type ptr";
			else if (u && c->a >= u->nsym) why = "the symbol does not exist";
			break;
		case VO_SLOTADDR:
			if (c->ty != VT_PTR) why = "slotaddr has type ptr";
			else if (c->a >= f->nslot) why = "the slot does not exist";
			break;
		case VO_LABELADDR:
			if (c->ty != VT_PTR) why = "labeladdr has type ptr";
			else if (c->a >= f->nblk) why = "the block does not exist";
			else if (!(f->blk[c->a].fl & VB_ADDR)) why = "the block has no VB_ADDR";
			else if (c->b || c->c) why = "labeladdr has b and c 0";
			break;
		}
		if (why) {
			vfail(&v, "S2", VIR_NOBLK, VIR_NOBLK, "constant %u (%s): %s", i, vir_optab[c->op].name, why);
			continue;
		}
		if (vir_cfind(vir_ctx_get(), f, c->op, c->ty, c->a, c->b, c->c, &slot)) {
			vfail(&v, "S2", VIR_NOBLK, VIR_NOBLK, "constant %u (%s) is a duplicate", i, vir_optab[c->op].name);
		} else {
			vir_ctx *x = vir_ctx_get();

			x->ctab[slot].gen = x->gen;
			x->ctab[slot].val = i;
		}
	}
	vir_ctab_rebuild(f);
	/* S3: the blocks tile the block region */
	expect = f->nconst;
	for (uint32_t b = 0; b < f->nblk; b++) {
		const vir_blk *k = &f->blk[b];

		if (k->first != expect || (uint64_t)k->first + k->n > f->nins) {
			vfail(&v, "S3", b, VIR_NOBLK, "the block starts at %u, not at %u (or runs past the function)", k->first,
			      expect);
			tiled = false;
			break;
		}
		if (k->n < 1) {
			vfail(&v, "S3", b, VIR_NOBLK, "the block is empty");
			tiled = false;
			break;
		}
		expect += k->n;
	}
	if (tiled && expect != f->nins) {
		vfail(&v, "S3", VIR_NOBLK, VIR_NOBLK, "the blocks end at %u, the function at %u", expect, f->nins);
		tiled = false;
	}
	if (!tiled)
		return v.nerr;
	for (uint32_t b = 0; b < f->nblk; b++) {
		const vir_blk *k = &f->blk[b];

		for (uint32_t i = 0; i < k->n; i++) {
			const vir_ins *x = &f->ins[k->first + i];
			bool last = i + 1 == k->n;

			v.vb[k->first + i] = b;
			if (vir_is_const_op(x->op)) {
				vfail(&v, "S1", b, i, "a constant in the block region");
				v.stop = true;
			} else if (vir_is_term(x->op) != last) {
				vfail(&v, "S3", b, i, last ? "the block does not end in a terminator" :
				      "a terminator before the end of the block");
				v.stop = true;
			}
		}
	}
	if (v.stop)
		return v.nerr;
	for (uint32_t i = 0; i < f->nconst; i++)
		v.vb[i] = VIR_NOBLK;
	/* S4, S5, T, per instruction */
	for (uint32_t b = 0; b < f->nblk; b++) {
		const vir_blk *k = &f->blk[b];

		for (uint32_t i = 0; i < k->n; i++) {
			const vir_ins *x = &f->ins[k->first + i];

			if (k->nparam > k->n) {
				vfail(&v, "S4", b, VIR_NOBLK, "the block has %u parameters and %u instructions", k->nparam, k->n);
				break;
			}
			if ((i < k->nparam) != (x->op == VO_BPARAM)) {
				if (i < k->nparam)
					vfail(&v, "S4", b, i, "%s where a block parameter belongs", opname(x));
				else
					vfail(&v, "S4", b, i, "a block parameter after the first instruction");
				continue;
			}
			if (x->op == VO_PARAM) {
				const char *e = sig_check(f, f->sig);
				uint32_t ng, w0 = 0, w1 = 0;
				bool ok = false;

				if (b != 0) {
					vfail(&v, "S5", b, i, "param outside block 0");
					continue;
				}
				if (e) {
					vfail(&v, "S5", b, i, "the function's signature is bad: %s", e);
					continue;
				}
				ng = VSIG_NGROUPS(f->extra[f->sig]);
				if (x->a == VG_RESULT) {
					w0 = f->extra[f->sig + 1];
					ok = VG_KIND(w0) == VG_SRET && x->b == 0 && x->ty == VT_PTR;
					if (!ok)
						vfail(&v, "S5", b, i, "param 0xffff is the sret pointer: piece 0, type ptr, sret result group");
				} else if (x->a >= ng) {
					vfail(&v, "S5", b, i, "group %u does not exist (%u groups)", x->a, ng);
				} else {
					uint32_t cl;

					w0 = f->extra[f->sig + 3 + 2 * x->a];
					w1 = f->extra[f->sig + 4 + 2 * x->a];
					(void)w1;
					if (VG_KIND(w0) == VG_REG) {
						cl = x->b < VG_NPIECE(w0) ? VG_CLASS(w0, x->b) : 0;
						ok = cl && (cl == VC_INT ? x->ty == VT_I64 || x->ty == VT_PTR :
							    cl == VC_F32 ? x->ty == VT_F32 : x->ty == VT_F64);
						if (!ok)
							vfail(&v, "S5", b, i, "piece %u of group %u does not exist or has type %s", x->b, x->a,
							      tyname(x->ty));
					} else if (VG_KIND(w0) == VG_MEM) {
						ok = x->b == 0 && x->ty == VT_PTR;
						if (!ok)
							vfail(&v, "S5", b, i, "a memory group's param is piece 0 of type ptr");
					} else {
						vfail(&v, "S5", b, i, "group %u passes nothing", x->a);
					}
				}
				if (!ok)
					continue;
			}
			check_ins(&v, b, i, k->first + i);
		}
	}
	if (v.stop)
		return v.nerr;
	/* the graph: block targets in range (T2), then C, D, A */
	for (uint32_t b = 0; b < f->nblk; b++) {
		const vir_ins *x = &f->ins[f->blk[b].first + f->blk[b].n - 1];
		vir_ins y = *x;
		vir_oit it;
		uint32_t *p;

		vir_oit_init(&it, f, &y, true);
		while ((p = vir_oit_next(&it)))
			if (*p >= f->nblk) {
				vfail(&v, "T2", b, f->blk[b].n - 1, "%s: block %u does not exist", opname(x), *p);
				return v.nerr;
			}
	}
	if (!vir_dom(f)) {
		vfail(&v, "T2", VIR_NOBLK, VIR_NOBLK, "the control-flow graph cannot be built");
		return v.nerr;
	}
	check_graph(&v);
	check_misc(&v);
	check_slots(&v);
	if (!v.nerr)
		check_canon(&v);
	return v.nerr;
}
