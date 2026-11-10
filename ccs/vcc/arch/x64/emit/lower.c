/* vcc code generator, x86-64: the walker's operations as records (x64impl.h) */
#include "x64impl.h"

#define X_MAXREC (1u << 22)
#define X_BUFREC ((size_t)X_MAXREC * 3)	/* twoaddr.c adds at most two records per record; the buffers swap */

static const uint8_t x64_argreg[6] = { RDI, RSI, RDX, RCX, R8, R9 };
/* temporaries in the order they are handed out: the argument registers last */
static const uint8_t x64_order[8] = { R10, R9, R8, RDI, RSI, RDX, RCX, RAX };

VPRIV xst *x64_state(ecx *e)
{
	xst *X = e->x64;

	if (!X) {
		X = calloc(1, sizeof *X);
		X->in = reserve(X_BUFREC * sizeof(rec));
		X->tw = reserve(X_BUFREC * sizeof(rec));
		X->capin = X_MAXREC;
		e->x64 = X;
	}
	return X;
}

VPRIV void x64_ecx_free(ecx *e)
{
	xst *X = e->x64;

	if (!X)
		return;
	munmap(X->in, X_BUFREC * sizeof(rec));
	munmap(X->tw, X_BUFREC * sizeof(rec));
	free(X);
	e->x64 = NULL;
}

VPRIV rec *x64_put(ecx *e, int op)
{
	xst *X = e->x64;
	rec *r;

	if (unlikely(X->nin == X->capin))
		efatal(e, e->curloc, "function too large");
	r = &X->in[X->nin++];
	memset(r, 0, sizeof *r);
	r->op = (uint8_t)op;
	r->r[0] = r->r[1] = r->r[2] = r->r[3] = XNOREG;
	return r;
}

static rec *x64_op3(ecx *e, int op, int sz, int r0, int r1, int r2)
{
	rec *i = x64_put(e, op);

	i->sz = (uint8_t)sz, i->r[0] = (uint8_t)r0, i->r[1] = (uint8_t)r1, i->r[2] = (uint8_t)r2;
	return i;
}

static void x64_mov(ecx *e, int rd, int rs)
{
	if (rd != rs)
		x64_op3(e, X_MOV, 8, rd, rs, XNOREG);
}

static void x64_mem(ecx *e, int op, int sz, int r, const lval *l, bool sgn)
{
	rec *i = x64_put(e, op);

	if (l->off < INT32_MIN || l->off > INT32_MAX)
		efatal(e, e->curloc, "x86-64: offset out of range");
	i->sz = (uint8_t)sz;
	i->cond = sgn;
	i->off = (int32_t)l->off;
	i->r[1] = l->k == L_FRAME ? RBP : l->k == L_SYM ? RIP : l->r;
	if (l->k == L_SYM)
		i->ref = l->ref;
	if (op == X_ST)
		i->r[2] = (uint8_t)r;
	else
		i->r[0] = (uint8_t)r;
}

/* ---------------- registers ---------------- */

VPRIV int x64_treg(ecx *e)
{
	xst *X = e->x64;

	for (int k = 0; k < 8; k++)
		if (!(X->live & (1u << x64_order[k]))) {
			X->live |= 1u << x64_order[k];
			return x64_order[k];
		}
	efatal(e, e->curloc, "x86-64: expression too complex (out of registers)");
}

VPRIV void x64_tfree(ecx *e, int r)
{
	if (r < 16)
		e->x64->live &= ~(1u << r);
}

VPRIV void x64_tkeep(ecx *e, int r)
{
	if (r < 16)
		e->x64->live |= 1u << r;
}

VPRIV int x64_nfree(ecx *e) { return 8 - __builtin_popcount(e->x64->live & X_TEMPS); }

VPRIV int x64_toreg(ecx *e, val *v, int size)
{
	int r;

	(void)size;
	if (v->k == V_REG)
		return v->r;
	r = x64_treg(e);
	if (v->k == V_CONST) {
		rec *i = x64_op3(e, X_MOVI, 8, r, XNOREG, XNOREG);

		i->imm = (int64_t)v->i;
	} else {
		lval l = { .k = L_SYM, .ref = v->ref, .off = (int64_t)v->i };

		x64_mem(e, X_LEA, 8, r, &l, false);
	}
	*v = (val){ .k = V_REG, .r = (uint8_t)r };
	return r;
}

/* ---------------- memory ---------------- */

VPRIV void x64_load(ecx *e, int rd, const lval *l, int size, bool sgn)
{
	x64_mem(e, X_LD, size, rd, l, sgn);
}

VPRIV void x64_store(ecx *e, int rs, const lval *l, int size)
{
	x64_mem(e, X_ST, size, rs, l, false);
}

VPRIV int x64_laddr(ecx *e, const lval *l)
{
	int r = l->k == L_REG ? l->r : x64_treg(e);

	if (l->k != L_REG || l->off)
		x64_mem(e, X_LEA, 8, r, l, false);
	return r;
}

VPRIV void x64_ext(ecx *e, int r, int from, bool sgn)
{
	rec *i;

	if (from >= 8)
		return;
	i = x64_op3(e, X_EXT, from, r, r, XNOREG);
	i->cond = sgn;
}

/* ---------------- integer operations ---------------- */

static bool x64_imm32(val v) { return v.k == V_CONST && v.i >= INT32_MIN && v.i <= INT32_MAX; }

/* rax, rdx: saved around div if something else lives there */
static val x64_div(ecx *e, int op, int size, bool sgn, val a, val b)
{
	xst *X = e->x64;
	int ra = x64_toreg(e, &a, size), rb = x64_toreg(e, &b, size), d = rb;
	bool sv[2] = { false, false };
	static const uint8_t fx[2] = { RAX, RDX };

	for (int k = 0; k < 2; k++)
		if ((X->live & (1u << fx[k])) && fx[k] != ra && fx[k] != rb) {
			x64_op3(e, X_PUSH, 8, XNOREG, fx[k], XNOREG);
			X->pushes++;
			sv[k] = true;
		}
	if (rb == RAX || rb == RDX) {
		x64_mov(e, R11, rb);
		d = R11;
	}
	x64_mov(e, RAX, ra);
	if (sgn) {
		x64_op3(e, X_CQO, size, RDX, RAX, XNOREG);
	} else {
		rec *z = x64_op3(e, X_MOVI, 4, RDX, XNOREG, XNOREG);

		z->imm = 0;
	}
	x64_op3(e, sgn ? X_IDIV : X_DIV, size, XNOREG, d, XNOREG);
	x64_mov(e, ra, op == T_SLASH ? RAX : RDX);
	for (int k = 1; k >= 0; k--)
		if (sv[k]) {
			x64_op3(e, X_POP, 8, fx[k], XNOREG, XNOREG);
			X->pushes--;
		}
	x64_tfree(e, rb);
	return (val){ .k = V_REG, .r = (uint8_t)ra };
}

/* a shift by a register (op: X_SHL X_SHR X_SAR): the count in cl */
static val x64_shift(ecx *e, int op, int size, val a, val b)
{
	xst *X = e->x64;
	int ra = x64_toreg(e, &a, size), rb = x64_toreg(e, &b, size), dst = ra == RCX ? R11 : ra;
	bool sv = false;

	if (dst == R11)
		x64_mov(e, R11, ra);
	if (rb != RCX) {
		if ((X->live & (1u << RCX)) && ra != RCX) {
			x64_op3(e, X_PUSH, 8, XNOREG, RCX, XNOREG);
			X->pushes++;
			sv = true;
		}
		x64_mov(e, RCX, rb);
	}
	x64_op3(e, op, size, dst, dst, RCX);
	if (dst == R11)
		x64_mov(e, ra, R11);
	if (sv) {
		x64_op3(e, X_POP, 8, RCX, XNOREG, XNOREG);
		X->pushes--;
	}
	if (rb != ra)
		x64_tfree(e, rb);
	return (val){ .k = V_REG, .r = (uint8_t)ra };
}

VPRIV val x64_bin(ecx *e, int op, int size, bool sgn, val a, val b)
{
	int ra, rb, xop, xopi;

	switch (op) {
	case T_PLUS: xop = X_ADD, xopi = X_ADDI; break;
	case T_MINUS: xop = X_SUB, xopi = X_SUBI; break;
	case T_AMP: xop = X_AND, xopi = X_ANDI; break;
	case T_PIPE: xop = X_OR, xopi = X_ORI; break;
	case T_CARET: xop = X_XOR, xopi = X_XORI; break;
	case T_STAR: xop = X_IMUL, xopi = X_IMULI; break;
	case T_SLASH: case T_PERCENT:
		return x64_div(e, op, size, sgn, a, b);
	case T_LESSLESS: case T_GREATERGREATER:
		if (b.k == V_CONST) {
			rec *i;

			ra = x64_toreg(e, &a, size);
			i = x64_op3(e, op == T_LESSLESS ? X_SHLI : sgn ? X_SARI : X_SHRI, size, ra, ra, XNOREG);
			i->imm = (int64_t)(b.i & (size * 8 - 1));
			return a;
		}
		return x64_shift(e, op == T_LESSLESS ? X_SHL : sgn ? X_SAR : X_SHR, size, a, b);
	default:
		efatal(e, e->curloc, "x86-64: operator %d", op);
	}
	/* a constant on the left of a commutative operation goes right */
	if (a.k != V_REG && b.k == V_REG && op != T_MINUS) {
		val t = a;

		a = b, b = t;
	}
	ra = x64_toreg(e, &a, size);
	if (x64_imm32(b)) {
		rec *i = x64_op3(e, xopi, size, ra, ra, XNOREG);

		i->imm = (int64_t)b.i;
		return a;
	}
	rb = x64_toreg(e, &b, size);
	x64_op3(e, xop, size, ra, ra, rb);
	if (rb != ra)
		x64_tfree(e, rb);
	return a;
}

VPRIV val x64_un(ecx *e, int op, int size, val a)
{
	int ra = x64_toreg(e, &a, size);

	x64_op3(e, op == T_MINUS ? X_NEG : X_NOT, size, ra, ra, XNOREG);
	return a;
}

/* ---------------- comparisons ---------------- */

static int x64_cc(int op, bool sgn)
{
	switch (op) {
	case T_LESS: return sgn ? CC_L : CC_B;
	case T_LESSEQ: return sgn ? CC_LE : CC_BE;
	case T_GREATER: return sgn ? CC_G : CC_A;
	case T_GREATEREQ: return sgn ? CC_GE : CC_AE;
	case T_EQUALEQUAL: return CC_E;
	default: return CC_NE;
	}
}

static int x64_swapop(int op)
{
	return op == T_LESS ? T_GREATER : op == T_LESSEQ ? T_GREATEREQ : op == T_GREATER ? T_LESS : op == T_GREATEREQ ? T_LESSEQ : op;
}

/* flags for a op b; the condition code, and a's register (still live) */
static int x64_flags(ecx *e, int op, int size, bool sgn, val *a, val *b, int *ra)
{
	if (a->k != V_REG && b->k == V_REG) {
		val t = *a;

		*a = *b, *b = t;
		op = x64_swapop(op);
	}
	*ra = x64_toreg(e, a, size);
	if (x64_imm32(*b) && b->i == 0 && (op == T_EQUALEQUAL || op == T_EXCLAIMEQ)) {
		x64_op3(e, X_TEST, size, XNOREG, *ra, XNOREG);
	} else if (x64_imm32(*b)) {
		rec *i = x64_op3(e, X_CMPI, size, XNOREG, *ra, XNOREG);

		i->imm = (int64_t)b->i;
	} else {
		int rb = x64_toreg(e, b, size);

		x64_op3(e, X_CMP, size, XNOREG, *ra, rb);
		if (rb != *ra)
			x64_tfree(e, rb);
	}
	return x64_cc(op, sgn);
}

VPRIV val x64_cmp(ecx *e, int op, int size, bool sgn, val a, val b)
{
	int ra, cc = x64_flags(e, op, size, sgn, &a, &b, &ra);
	rec *i = x64_op3(e, X_SETCC, 4, ra, XNOREG, XNOREG);

	i->cond = (uint8_t)cc;
	return (val){ .k = V_REG, .r = (uint8_t)ra };
}

VPRIV void x64_cmpbr(ecx *e, int op, int size, bool sgn, val a, val b, uint32_t lbl)
{
	int ra, cc = x64_flags(e, op, size, sgn, &a, &b, &ra);
	rec *i = x64_put(e, X_JCC);

	i->cond = (uint8_t)cc;
	i->label = lbl;
	x64_tfree(e, ra);
}

VPRIV void x64_brz(ecx *e, val a, int size, bool nz, uint32_t lbl)
{
	int ra = x64_toreg(e, &a, size);
	rec *i;

	x64_op3(e, X_TEST, size, XNOREG, ra, XNOREG);
	i = x64_put(e, X_JCC);
	i->cond = nz ? CC_NE : CC_E;
	i->label = lbl;
	x64_tfree(e, ra);
}

/* ---------------- control ---------------- */

VPRIV void x64_label(ecx *e, uint32_t l) { x64_put(e, X_LABEL)->label = l; }
VPRIV void x64_jump(ecx *e, uint32_t l) { x64_put(e, X_JMP)->label = l; }

/* ---------------- calls ---------------- */

VPRIV int x64_nregargs(void) { return 6; }
VPRIV int32_t x64_stackarg(int i) { return 16 + 8 * (i - 6); }

VPRIV void x64_param(ecx *e, int i, int32_t slot, int size)
{
	lval l = { .k = L_FRAME, .off = slot, .align = 8 };

	(void)size;
	x64_store(e, x64_argreg[i], &l, 8);
}

VPRIV val x64_call(ecx *e, uint32_t ref, val *fn, const int32_t *argslot, int nargs, bool variadic, int retsize, bool retsgn)
{
	xst *X = e->x64;
	uint32_t saved = X->live & X_TEMPS;
	int nstk = nargs > 6 ? nargs - 6 : 0, pad;
	rec *i;

	for (int r = 0; r < 16; r++)
		if (saved & (1u << r)) {
			x64_op3(e, X_PUSH, 8, XNOREG, r, XNOREG);
			X->pushes++;
		}
	pad = (X->pushes + nstk) & 1;
	if (pad) {
		x64_put(e, X_ADJSP)->imm = -8;
		X->pushes++;
	}
	for (int k = nargs - 1; k >= 6; k--) {
		lval l = { .k = L_FRAME, .off = argslot[k], .align = 8 };

		x64_load(e, R11, &l, 8, false);
		x64_op3(e, X_PUSH, 8, XNOREG, R11, XNOREG);
		X->pushes++;
	}
	if (fn)
		x64_mov(e, R11, fn->r);
	for (int k = 0; k < nargs && k < 6; k++) {
		lval l = { .k = L_FRAME, .off = argslot[k], .align = 8 };

		x64_load(e, x64_argreg[k], &l, 8, false);
	}
	if (variadic)
		x64_op3(e, X_MOVI, 4, RAX, XNOREG, XNOREG)->imm = 0;	/* al: no vector registers */
	if (fn) {
		x64_op3(e, X_CALLR, 8, XNOREG, R11, XNOREG);
	} else {
		i = x64_put(e, X_CALL);
		i->ref = ref;
		i->cond = variadic;
	}
	if (nstk + pad) {
		x64_put(e, X_ADJSP)->imm = 8 * (nstk + pad);
		X->pushes -= nstk + pad;
	}
	if (retsize) {
		x64_mov(e, R11, RAX);
		x64_ext(e, R11, retsize, retsgn);
	}
	for (int r = 15; r >= 0; r--)
		if (saved & (1u << r)) {
			x64_op3(e, X_POP, 8, r, XNOREG, XNOREG);
			X->pushes--;
		}
	if (fn && fn->k == V_REG)
		x64_tfree(e, fn->r);
	if (!retsize)
		return vconst(0);
	{
		int r = x64_treg(e);

		x64_mov(e, r, R11);
		return (val){ .k = V_REG, .r = (uint8_t)r };
	}
}

VPRIV void x64_ret(ecx *e, val *v, int size, bool sgn, uint32_t lbl)
{
	if (v) {
		int r = x64_toreg(e, v, size);

		x64_ext(e, r, size, sgn);
		x64_mov(e, RAX, r);
		x64_tfree(e, r);
	}
	x64_jump(e, lbl);
}

/* ---------------- the function ---------------- */

VPRIV void x64_begin(ecx *e, uint32_t s)
{
	xst *X = x64_state(e);

	(void)s;
	X->nin = 0;
	X->live = 0;
	X->pushes = 0;
}

VPRIV void x64_finish(ecx *e, uint32_t s)
{
	tbuf *b = &e->out;
	size_t start;

	if (e->x64->live)
		efatal(e, e->curloc, "internal error: x86-64 temporaries left live (%#x)", e->x64->live);
	x64_twoaddr(e);
	b->n = (b->n + 7) & ~(size_t)7;
	start = b->n;
	sym_header(e, b, s, true, "\t.text\n", 16);
	x64_print(e, b, s);
	{
		const attrs *a = sym_attrs(e->c, s);

		if (a->constructor)
			ctor_entry(e->c, b, s, false);
		if (a->destructor)
			ctor_entry(e->c, b, s, true);
	}
	e->E->nfn++;
	out_chunk(e, (uint64_t)s << 1, start);
}
