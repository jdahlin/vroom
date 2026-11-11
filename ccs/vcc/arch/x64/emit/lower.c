/* vcc code generator, x86-64: the walker's operations as records (x64impl.h) */
#include "x64impl.h"

#define X_MAXREC (1u << 22)
#define X_BUFREC ((size_t)X_MAXREC * 3)	/* twoaddr.c adds at most two records per record; the buffers swap */

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

VPRIV rec *x64_op3(ecx *e, int op, int sz, int r0, int r1, int r2)
{
	rec *i = x64_put(e, op);

	i->sz = (uint8_t)sz, i->r[0] = (uint8_t)r0, i->r[1] = (uint8_t)r1, i->r[2] = (uint8_t)r2;
	return i;
}

VPRIV void x64_mov(ecx *e, int rd, int rs)
{
	if (rd != rs)
		x64_op3(e, X_MOV, 8, rd, rs, XNOREG);
}

/* the base register of l's memory operand (ref: its symbol, RIP-relative); a
   symbol reached through the GOT (-fPIC/-fPIE, an undefined weak) is loaded into r11 */
VPRIV int x64_base(ecx *e, const lval *l, uint32_t *ref)
{
	*ref = 0;
	if (l->k == L_FRAME)
		return RBP;
	if (l->k != L_SYM)
		return l->r;
	if (sym_needs_got(e->c, l->ref)) {
		rec *g = x64_put(e, X_GOT);

		g->sz = 8;
		g->r[0] = R11;
		g->ref = l->ref;
		return R11;
	}
	*ref = l->ref;
	return RIP;
}

VPRIV void x64_mem(ecx *e, int op, int sz, int r, const lval *l, bool sgn)
{
	uint32_t ref;
	int base = x64_base(e, l, &ref);
	rec *i;

	if (base == R11 && l->k == L_SYM && op == X_ST && r == R11)
		efatal(e, e->curloc, "internal error: a store through the GOT from r11");
	i = x64_put(e, op);

	if (l->off < INT32_MIN || l->off > INT32_MAX)
		efatal(e, e->curloc, "x86-64: offset out of range");
	i->sz = (uint8_t)sz;
	i->cond = sgn;
	i->off = (int32_t)l->off;
	i->r[1] = (uint8_t)base;
	i->ref = ref;
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
	if (r < 32)
		e->x64->live &= ~(1u << r);
}

VPRIV void x64_tkeep(ecx *e, int r)
{
	if (r < 32)
		e->x64->live |= 1u << r;
}

VPRIV void x64_nofp(ecx *e)
{
	if (e->c->o->general_regs_only)
		efatal(e, e->curloc, "x86-64: floating point with -mgeneral-regs-only or -mno-sse is not supported (PLAN-x86.md X3)");
}

VPRIV int x64_ftreg(ecx *e)
{
	xst *X = e->x64;

	x64_nofp(e);
	for (int r = XMM8; r < XMM15; r++)
		if (!(X->live & (1u << r))) {
			X->live |= 1u << r;
			return r;
		}
	efatal(e, e->curloc, "x86-64: expression too complex (out of vector registers)");
}

VPRIV int x64_nfree(ecx *e) { return 8 - __builtin_popcount(e->x64->live & X_TEMPS); }
VPRIV int x64_nffree(ecx *e) { return XMM15 - XMM8 - __builtin_popcount(e->x64->live & X_FTEMPS); }

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

/* registers a block operation needs (rep movsb/stosb), saved if live and not one of keep */
VPRIV uint32_t x64_save(ecx *e, const uint8_t *regs, int n, int k0, int k1)
{
	xst *X = e->x64;
	uint32_t sv = 0;

	for (int k = 0; k < n; k++)
		if ((X->live & (1u << regs[k])) && regs[k] != k0 && regs[k] != k1) {
			x64_op3(e, X_PUSH, 8, XNOREG, regs[k], XNOREG);
			X->pushes++;
			sv |= 1u << regs[k];
		}
	return sv;
}

VPRIV void x64_restore(ecx *e, uint32_t sv)
{
	xst *X = e->x64;

	for (int r = 15; r >= 0; r--)
		if (sv & (1u << r)) {
			x64_op3(e, X_POP, 8, r, XNOREG, XNOREG);
			X->pushes--;
		}
}

VPRIV void x64_movi(ecx *e, int r, int64_t v)
{
	x64_op3(e, X_MOVI, 8, r, XNOREG, XNOREG)->imm = v;
}

VPRIV void x64_copy(ecx *e, int rdst, int rsrc, uint64_t n)
{
	if (n <= 128) {
		for (uint64_t o = 0; o < n;) {
			int k = n - o >= 8 ? 8 : n - o >= 4 ? 4 : n - o >= 2 ? 2 : 1;
			lval s = { .k = L_REG, .r = (uint8_t)rsrc, .off = (int64_t)o }, d = { .k = L_REG, .r = (uint8_t)rdst, .off = (int64_t)o };

			x64_mem(e, X_LD, k, R11, &s, false);
			x64_mem(e, X_ST, k, R11, &d, false);
			o += (uint64_t)k;
		}
		return;
	}
	{
		static const uint8_t rr[3] = { RDI, RSI, RCX };
		uint32_t sv = x64_save(e, rr, 3, -1, -1);

		/* rdst, rsrc may be any of rdi rsi rcx: through r11 and the stack */
		x64_op3(e, X_PUSH, 8, XNOREG, rsrc, XNOREG);
		x64_mov(e, R11, rdst);
		x64_op3(e, X_POP, 8, RSI, XNOREG, XNOREG);
		x64_mov(e, RDI, R11);
		x64_movi(e, RCX, (int64_t)n);
		x64_put(e, X_REP)->cond = 0;
		x64_restore(e, sv);
	}
}

VPRIV void x64_zero(ecx *e, int rdst, uint64_t n)
{
	if (n <= 128) {
		x64_op3(e, X_MOVI, 4, R11, XNOREG, XNOREG)->imm = 0;
		for (uint64_t o = 0; o < n;) {
			int k = n - o >= 8 ? 8 : n - o >= 4 ? 4 : n - o >= 2 ? 2 : 1;
			lval d = { .k = L_REG, .r = (uint8_t)rdst, .off = (int64_t)o };

			x64_mem(e, X_ST, k, R11, &d, false);
			o += (uint64_t)k;
		}
		return;
	}
	{
		static const uint8_t rr[3] = { RDI, RAX, RCX };
		uint32_t sv = x64_save(e, rr, 3, -1, -1);

		x64_mov(e, RDI, rdst);
		x64_op3(e, X_MOVI, 4, RAX, XNOREG, XNOREG)->imm = 0;
		x64_movi(e, RCX, (int64_t)n);
		x64_put(e, X_REP)->cond = 1;
		x64_restore(e, sv);
	}
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
/* buf: rbp, the resume label, rsp (as clang); 0 now, 1 at the label */
VPRIV val x64_setjmp(ecx *e, int rb)
{
	uint32_t resume = wk_newlabel(e), done = wk_newlabel(e);
	lval b0 = { .k = L_REG, .r = (uint8_t)rb }, b1 = b0, b2 = b0;
	rec *i;
	int r;

	b1.off = 8, b2.off = 16;
	x64_mem(e, X_ST, 8, RBP, &b0, false);
	i = x64_put(e, X_LEAL);
	i->r[0] = R11;
	i->label = resume;
	x64_mem(e, X_ST, 8, R11, &b1, false);
	x64_mem(e, X_ST, 8, RSP, &b2, false);
	x64_tfree(e, rb);
	r = x64_treg(e);
	x64_movi(e, r, 0);
	x64_jump(e, done);
	x64_label(e, resume);
	x64_movi(e, r, 1);
	x64_label(e, done);
	return (val){ .k = V_REG, .r = (uint8_t)r };
}

VPRIV void x64_longjmp(ecx *e, int rb)
{
	lval b0 = { .k = L_REG, .r = (uint8_t)rb }, b1 = b0, b2 = b0;

	b1.off = 8, b2.off = 16;
	x64_mem(e, X_LD, 8, R11, &b1, false);
	x64_mem(e, X_LD, 8, RSP, &b2, false);
	x64_mem(e, X_LD, 8, RBP, &b0, false);
	x64_op3(e, X_JMPR, 8, XNOREG, R11, XNOREG);
	x64_tfree(e, rb);
}

VPRIV void x64_jumpr(ecx *e, int r) { x64_op3(e, X_JMPR, 8, XNOREG, r, XNOREG); x64_tfree(e, r); }
VPRIV void x64_named(ecx *e, uint32_t ref) { x64_put(e, X_NAMED)->ref = ref; }

/* ---------------- the function ---------------- */

VPRIV void x64_begin(ecx *e, uint32_t s)
{
	xst *X = x64_state(e);

	(void)s;
	X->nin = 0;
	X->live = 0;
	X->pushes = 0;
	memset(X->csslot, 0, sizeof X->csslot);
	e->raw.n = 0;
	e->asmk = 0;
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

/* ---------------- builtins ---------------- */

VPRIV int x64_tlsaddr(ecx *e, uint32_t ref, int model)
{
	xst *X = e->x64;
	int r;
	rec *i;

	if (model < 2) {
		r = x64_treg(e);
		i = x64_put(e, X_TLS);
		i->r[0] = (uint8_t)r;
		i->ref = ref;
		i->cond = (uint8_t)model;
		return r;
	}
	/* general dynamic: a call; the live temporaries saved around it, rsp 16-aligned */
	{
		uint32_t saved = X->live & X_TEMPS, fsaved = X->live & X_FTEMPS;
		int pad;

		for (int q = 0; q < 16; q++)
			if (saved & (1u << q)) {
				x64_op3(e, X_PUSH, 8, XNOREG, q, XNOREG);
				X->pushes++;
			}
		for (int q = XMM8; q < XMM15; q++)
			if (fsaved & (1u << q)) {
				lval d = { .k = L_REG, .r = RSP };

				x64_put(e, X_ADJSP)->imm = -8;
				x64_mem(e, X_ST, 8, q, &d, false);
				X->pushes++;
			}
		pad = X->pushes % 2 ? 8 : 0;
		if (pad)
			x64_put(e, X_ADJSP)->imm = -pad;
		i = x64_put(e, X_TLS);
		i->r[0] = RAX;
		i->ref = ref;
		i->cond = 2;
		if (pad)
			x64_put(e, X_ADJSP)->imm = pad;
		x64_mov(e, R11, RAX);
		for (int q = XMM15 - 1; q >= XMM8; q--)
			if (fsaved & (1u << q)) {
				lval s = { .k = L_REG, .r = RSP };

				x64_mem(e, X_LD, 8, q, &s, false);
				x64_put(e, X_ADJSP)->imm = 8;
				X->pushes--;
			}
		for (int q = 15; q >= 0; q--)
			if (saved & (1u << q)) {
				x64_op3(e, X_POP, 8, q, XNOREG, XNOREG);
				X->pushes--;
			}
		r = x64_treg(e);
		x64_mov(e, r, R11);
		return r;
	}
}

/* ---------------- atomics (sequentially consistent: x86 orders the rest) ---------------- */

static rec *x64_atrec(ecx *e, int op, int size, int r, int rp)
{
	rec *i = x64_put(e, op);

	i->sz = (uint8_t)size;
	i->r[0] = (uint8_t)r;
	i->r[1] = (uint8_t)rp;
	return i;
}

VPRIV int x64_atomic(ecx *e, int op, int size, int rp, int rv, int rexp)
{
	xst *X = e->x64;
	int r;

	switch (op) {
	case WK_AFENCE:
		x64_put(e, X_MFENCE);
		return -1;
	case WK_ALOAD: {
		lval l = { .k = L_REG, .r = (uint8_t)rp };

		r = x64_treg(e);
		x64_mem(e, X_LD, size, r, &l, false);	/* a plain load is sequentially consistent */
		return r;
	}
	case WK_ASTORE:
		x64_mov(e, R11, rv);
		x64_atrec(e, X_XCHG, size, R11, rp);
		return -1;
	case WK_AXCHG: case WK_AXADD:
		r = x64_treg(e);
		x64_mov(e, r, rv);
		x64_atrec(e, op == WK_AXCHG ? X_XCHG : X_XADD, size, r, rp);
		x64_ext(e, r, size, false);
		return r;
	default: {	/* WK_ACAS: rax is the comparand (saved if it holds something) */
		bool save = (X->live & (1u << RAX)) != 0;
		rec *i;

		if (save) {
			x64_op3(e, X_PUSH, 8, XNOREG, RAX, XNOREG);
			X->pushes++;
		}
		if (rv == RAX || rp == RAX) {
			x64_mov(e, R11, RAX);
			rv = rv == RAX ? R11 : rv;
			rp = rp == RAX ? R11 : rp;
		}
		x64_mov(e, RAX, rexp);
		i = x64_atrec(e, X_CMPXCHG, size, XNOREG, rp);
		i->r[2] = (uint8_t)rv;
		if (save) {
			x64_mov(e, R11, RAX);
			x64_op3(e, X_POP, 8, RAX, XNOREG, XNOREG);
			X->pushes--;
			r = x64_treg(e);
			x64_mov(e, r, R11);
		} else {
			x64_tkeep(e, RAX);
			r = RAX;
		}
		x64_ext(e, r, size, false);	/* (cmpxchg leaves the upper bits as they were) */
		return r;
	}
	}
}

VPRIV void x64_trap(ecx *e) { x64_put(e, X_UD2); }

/* level frames up through the saved rbp chain (every function keeps a frame pointer) */
VPRIV val x64_frame(ecx *e, bool ret, int level)
{
	int r = x64_treg(e);

	x64_mov(e, r, RBP);
	for (int k = 0; k < level; k++) {
		lval l = { .k = L_REG, .r = (uint8_t)r };

		x64_mem(e, X_LD, 8, r, &l, false);
	}
	if (ret) {
		lval l = { .k = L_REG, .r = (uint8_t)r, .off = 8 };

		x64_mem(e, X_LD, 8, r, &l, false);
	}
	return (val){ .k = V_REG, .r = (uint8_t)r };
}

/* a's bits counted SWAR-style (no popcnt in the x86-64 baseline): the count in a's register */
static void x64_popcount(ecx *e, int r, int size)
{
	int t = x64_treg(e);

	/* r -= (r >> 1) & 0x55..; r = (r & 0x33..) + ((r >> 2) & 0x33..); r = (r + (r >> 4)) & 0x0f..; r = (r * 0x01..) >> (size * 8 - 8) */
	x64_mov(e, t, r);
	x64_op3(e, X_SHRI, 8, t, t, XNOREG)->imm = 1;
	x64_movi(e, R11, 0x5555555555555555);
	x64_op3(e, X_AND, 8, t, t, R11);
	x64_op3(e, X_SUB, 8, r, r, t);
	x64_mov(e, t, r);
	x64_op3(e, X_SHRI, 8, t, t, XNOREG)->imm = 2;
	x64_movi(e, R11, 0x3333333333333333);
	x64_op3(e, X_AND, 8, t, t, R11);
	x64_op3(e, X_AND, 8, r, r, R11);
	x64_op3(e, X_ADD, 8, r, r, t);
	x64_mov(e, t, r);
	x64_op3(e, X_SHRI, 8, t, t, XNOREG)->imm = 4;
	x64_op3(e, X_ADD, 8, r, r, t);
	x64_movi(e, R11, 0x0f0f0f0f0f0f0f0f);
	x64_op3(e, X_AND, 8, r, r, R11);
	x64_movi(e, R11, 0x0101010101010101);
	x64_op3(e, X_IMUL, 8, r, r, R11);
	x64_op3(e, X_SHRI, 8, r, r, XNOREG)->imm = 56;
	x64_tfree(e, t);
	(void)size;
}

VPRIV val x64_bitop(ecx *e, int bi, int size, val a)
{
	int r = x64_toreg(e, &a, size), bits = size * 8;

	if (size == 4)
		x64_ext(e, r, 4, false);	/* (the count sees the 32 bits only) */
	switch (bi) {
	case BI_CLZ: case BI_CLZL: case BI_CLZLL:
		x64_op3(e, X_BSR, size, r, r, XNOREG);
		x64_op3(e, X_XORI, 4, r, r, XNOREG)->imm = bits - 1;
		break;
	case BI_CTZ: case BI_CTZL: case BI_CTZLL:
		x64_op3(e, X_BSF, size, r, r, XNOREG);
		break;
	case BI_FFS: case BI_FFSL: case BI_FFSLL: {
		/* x ? ctz(x) + 1 : 0 */
		uint32_t lz = wk_newlabel(e);
		rec *j;

		x64_op3(e, X_TEST, size, XNOREG, r, XNOREG);
		j = x64_put(e, X_JCC);
		j->cond = CC_E;
		j->label = lz;
		x64_op3(e, X_BSF, size, r, r, XNOREG);
		x64_op3(e, X_ADDI, 4, r, r, XNOREG)->imm = 1;
		x64_label(e, lz);
		break;
	}
	case BI_POPCOUNT: case BI_POPCOUNTL: case BI_POPCOUNTLL:
		x64_popcount(e, r, size);
		break;
	case BI_PARITY: case BI_PARITYL: case BI_PARITYLL:
		x64_popcount(e, r, size);
		x64_op3(e, X_ANDI, 4, r, r, XNOREG)->imm = 1;
		break;
	case BI_BSWAP16:
		x64_op3(e, X_BSWAP, 4, r, r, XNOREG);
		x64_op3(e, X_SHRI, 4, r, r, XNOREG)->imm = 16;
		break;
	case BI_BSWAP32: case BI_BSWAP64:
		x64_op3(e, X_BSWAP, size, r, r, XNOREG);
		break;
	default:
		efatal(e, e->curloc, "x86-64: builtin %d is not supported yet (PLAN-x86.md X3)", bi);
	}
	return (val){ .k = V_REG, .r = (uint8_t)r };
}

/* n bytes below the stack pointer (nothing is pushed here: the stack stays 16-aligned) */
VPRIV val x64_alloca(ecx *e, val n)
{
	int r = x64_toreg(e, &n, 8);

	x64_op3(e, X_ADDI, 8, r, r, XNOREG)->imm = 15;
	x64_op3(e, X_ANDI, 8, r, r, XNOREG)->imm = -16;
	x64_op3(e, X_SUB, 8, RSP, RSP, r);
	x64_mov(e, r, RSP);
	return (val){ .k = V_REG, .r = (uint8_t)r };
}

VPRIV int x64_spget(ecx *e)
{
	int r = x64_treg(e);

	x64_mov(e, r, RSP);
	return r;
}

VPRIV void x64_spset(ecx *e, int r)
{
	x64_mov(e, RSP, r);
	x64_tfree(e, r);
}

VPRIV val x64_fbits(ecx *e, val v, int size)
{
	int r = x64_treg(e);

	x64_op3(e, X_MOV, size == 4 ? 4 : 8, r, v.r, XNOREG);
	x64_tfree(e, v.r);
	return (val){ .k = V_REG, .r = (uint8_t)r };
}
