/* vcc code generator, x86-64: the System V ABI: classification, calls,
 * parameters, results, varargs (x64impl.h) */
#include "x64impl.h"

/* ---------------- classification (System V 3.2.3) ---------------- */

enum { XC_NONE, XC_INT, XC_SSE, XC_MEM, XC_SSEUP, XC_X87, XC_X87UP };

static int x64_merge(int a, int b)
{
	if (a == b || b == XC_NONE)
		return a;
	if (a == XC_NONE)
		return b;
	if (a == XC_MEM || b == XC_MEM)
		return XC_MEM;
	if (a == XC_INT || b == XC_INT)
		return XC_INT;
	if (a == XC_X87 || a == XC_X87UP || b == XC_X87 || b == XC_X87UP)
		return XC_MEM;
	return XC_SSE;
}

/* the classes of the eightbytes of t at byte offset off; false: memory */
static bool x64_cls(cc *c, qty t, uint64_t off, int cls[2])
{
	const ctype *x = T(c, t);
	int k = x->kind;

	if (k == TY_STRUCT || k == TY_UNION) {
		const tag *r = &c->tags[x->aux];

		for (uint32_t i = 0; i < r->nfield; i++) {
			const field *f = &r->f[i];
			uint64_t fo = off + f->off / 8;

			if (f->bitfield) {
				if (!f->width)
					continue;
				/* the eightbytes its first and last bytes are in */
				cls[fo / 8] = x64_merge(cls[fo / 8], XC_INT);
				fo += (f->off % 8 + f->width - 1) / 8;
				cls[fo / 8] = x64_merge(cls[fo / 8], XC_INT);
				continue;
			}
			if (!x64_cls(c, f->ty, fo, cls))
				return false;
		}
		return true;
	}
	/* Natural alignment applies even to zero-length arrays: a packed
	   array at an unaligned offset still gives the record MEMORY class. */
	uint32_t align = c->ty[x->canon].align;
	if (align && off % align)
		return false;
	if (k == TY_ARRAY) {
		uint64_t es = tsize(c, x->base), n = es ? tsize(c, t) / es : 0;

		for (uint64_t i = 0; i < n; i++)
			if (!x64_cls(c, x->base, off + i * es, cls))
				return false;
		return true;
	}
	if (k == TY_VECTOR) {
		/* clang's classes for vectors: up to 4 bytes INTEGER; 8 bytes SSE (<1 x double>
		   memory, <1 x i64> SSE); 16 bytes SSE + SSEUP; larger: memory (no AVX) */
		uint64_t sz = tsize(c, t), es = tsize(c, x->base);

		if (sz <= 4) {
			cls[off / 8] = x64_merge(cls[off / 8], XC_INT);
			return (off % 8) + sz <= 8;
		}
		if (sz == 8 && off % 8 == 0) {
			if (es == 8 && is_flt(c, x->base))
				return false;
			cls[off / 8] = x64_merge(cls[off / 8], XC_SSE);
			return true;
		}
		if (sz == 16 && off == 0 && cls[0] == XC_NONE && cls[1] == XC_NONE) {
			cls[0] = XC_SSE, cls[1] = XC_SSEUP;
			return true;
		}
		return false;	/* (a 32-byte vector alone: see x64_classify) */
	}
	if (k == TY_FLOAT128) {
		if (off % 16 || cls[off / 8] != XC_NONE)
			return false;
		cls[off / 8] = XC_SSE, cls[off / 8 + 1] = XC_SSEUP;
		return true;
	}
	if (k == TY_LDOUBLE && off == 0) {
		cls[0] = x64_merge(cls[0], XC_X87);
		cls[1] = x64_merge(cls[1], XC_X87UP);
		return true;
	}
	if (k == TY_LDOUBLE || k == TY_INT128 || k == TY_UINT128)
		return k == TY_INT128 || k == TY_UINT128 ? (cls[off / 8] = x64_merge(cls[off / 8], XC_INT),
							    cls[off / 8 + 1] = x64_merge(cls[off / 8 + 1], XC_INT), true) : false;
	if (tsize(c, t) == 0)
		return true;
	cls[off / 8] = x64_merge(cls[off / 8], is_flt(c, t) ? XC_SSE : XC_INT);
	return true;
}

/* Wide records use AVX registers only for SSE followed entirely by SSEUP.
   Track all eightbytes, including union merges and padding, without changing
   the existing two-eightbyte transport classifier. */
static bool x64_wide_cls(cc *c, qty t, uint64_t off, int cls[8])
{
	const ctype *x = T(c, t);
	uint64_t size = tsize(c, t);

	if (off + size > 64)
		return false;
	if (x->kind == TY_STRUCT || x->kind == TY_UNION) {
		const tag *r = &c->tags[x->aux];
		int fields[8] = {0};

		for (uint32_t i = 0; i < r->nfield; i++) {
			const field *f = &r->f[i];
			uint64_t fo = off + f->off / 8;

			if (f->bitfield && !f->name) continue;
			if (size > 16 && x->kind != TY_UNION && tsize(c, f->ty) != size)
				return false;
			if (f->bitfield) {
				if (f->width) {
					uint64_t last = fo + (f->off % 8 + f->width - 1) / 8;

					if (last >= 64) return false;
					for (uint64_t j = fo / 8; j <= last / 8; j++)
						fields[j] = x64_merge(fields[j], XC_INT);
				}
			} else if (!x64_wide_cls(c, f->ty, fo, fields))
				return false;
		}
		if (size > 16) {
			if (fields[off / 8] != XC_SSE) return false;
			for (uint64_t i = 1; i < size / 8; i++)
				if (fields[off / 8 + i] != XC_SSEUP) return false;
		}
		for (unsigned i = 0; i < 8; i++) cls[i] = x64_merge(cls[i], fields[i]);
		return true;
	}
	uint32_t align = c->ty[x->canon].align;
	if (align && off % align)
		return false;
	if (x->kind == TY_ARRAY) {
		uint64_t es = tsize(c, x->base);

		/* Clang's wide-array early exit leaves NoClass after resetting
		   Current. In a union this contributes nothing to a wide vector. */
		if (size > 16 && size != es) return true;

		for (uint64_t i = 0; es && i < size / es; i++)
			if (!x64_wide_cls(c, x->base, off + i * es, cls))
				return false;
		return true;
	}
	if (!size) return true;
	if (x->kind == TY_VECTOR && size >= 16) {
		if (off % 8 || size % 8) return false;
		for (uint64_t i = 0; i < size / 8; i++)
			cls[off / 8 + i] = x64_merge(cls[off / 8 + i], i ? XC_SSEUP : XC_SSE);
		return true;
	}
	int leaf[2] = { XC_NONE, XC_NONE };
	if (size > 16 || !x64_cls(c, t, off % 8, leaf))
		return false;
	for (unsigned i = 0; i < 2 && off / 8 + i < 8; i++)
		cls[off / 8 + i] = x64_merge(cls[off / 8 + i], leaf[i]);
	return true;
}

static bool x64_wide_record(cc *c, qty t)
{
	uint64_t size = tsize(c, t);
	int cls[8] = {0};

	if (!is_record(c, t) || (size != 32 && size != 64) || !x64_wide_cls(c, t, 0, cls) || cls[0] != XC_SSE)
		return false;
	for (unsigned i = 1; i < size / 8; i++)
		if (cls[i] != XC_SSEUP) return false;
	return true;
}

static void x64_check_vector_abi(ecx *e, uint64_t size, bool vector, bool wide_record, x86_features features)
{
	bool unsupported = vector && size > 16 && x86_feature_has(features, X86_FEATURE_AVX);

	unsupported |= wide_record && x86_feature_has(features, size == 64 ? X86_FEATURE_AVX512F : X86_FEATURE_AVX);
	if (unsupported)
		efatal(e, e->curloc, "x86-64: AVX vector argument or return ABI is not supported yet (PLAN-x86.md)");
}

VPRIV void x64_classify0(ecx *e, qty t, wkarg *a);

/* -mgeneral-regs-only, -mno-sse: no value may travel in a vector or x87 register */
VPRIV void x64_classify(ecx *e, qty t, wkarg *a, bool unnamed)
{
	x64_classify0(e, t, a);
	a->unnamed = unnamed;
	uint32_t fn = e->wk->inl ? e->wk->inl->fn : e->wk->fsym;
	/* Unnamed wide vectors/records use the overflow area, never YMM/ZMM. */
	if (!unnamed)
		x64_check_vector_abi(e, a->size, a->vector, a->wide_record, cc_x86_features(e->c, fn));
	if (x64_nosimd(e) && a->mem && a->up)
		a->up = false;	/* a 32-byte vector: returned in memory, not xmm0:xmm1 */
	if (x64_nosimd(e) && (a->x87 || (!a->mem && a->n && (a->cls[0] == WA_SSE || (a->n > 1 && a->cls[1] == WA_SSE)))))
		x64_nofp(e);
}

VPRIV void x64_classify0(ecx *e, qty t, wkarg *a)
{
	cc *c = e->c;
	uint64_t size = tsize(c, t);
	int cls[2] = { XC_NONE, XC_NONE };

	memset(a, 0, sizeof *a);
	a->size = size;
	a->vector = is_vec(c, t);
	a->wide_record = x64_wide_record(c, t);
	a->align = talign(c, t) > 8 ? talign(c, t) : 8;
	if (!is_record(c, t) && !is_array(c, t) && !is_vec(c, t)) {
		a->n = size > 8 ? 2 : 1;
		a->cls[0] = a->cls[1] = is_flt(c, t) ? WA_SSE : WA_INT;
		if (KIND(c, t) == TY_LDOUBLE)
			a->mem = true, a->n = 0, a->x87 = true;	/* passed in memory, returned in st(0) */
		if (KIND(c, t) == TY_FLOAT128)
			a->n = 1, a->cls[0] = WA_SSE, a->up = true;	/* SSE + SSEUP: one xmm register */
		return;
	}
	if (size == 0)
		return;	/* an empty record: nothing is passed */
	if (is_vec(c, t) && (size == 32 || size == 64)) {
		/* clang without AVX: passed in memory, returned in 2 or 4 xmm registers (-Wpsabi) */
		a->mem = true, a->up = true;
		return;
	}
	if (is_record(c, t) && (c->tags[T(c, t)->aux].flags & RF_COMPLEX) && KIND(c, c->tags[T(c, t)->aux].f[0].ty) == TY_LDOUBLE) {
		a->mem = true, a->x87 = true;	/* COMPLEX_X87: passed in memory, returned in st(0) (real), st(1) */
		return;
	}
	if (size > 16 || !x64_cls(c, t, 0, cls) || cls[0] == XC_MEM || cls[1] == XC_MEM) {
		a->mem = true;
		return;
	}
	if (cls[0] == XC_X87 && cls[1] == XC_X87UP) {
		a->mem = true, a->x87 = true;
		return;
	}
	if (cls[0] == XC_X87 || cls[1] == XC_X87UP) {
		a->mem = true;
		return;
	}
	if (cls[1] == XC_SSEUP) {
		if (cls[0] != XC_SSE) {
			a->mem = true;
			return;
		}
		a->n = 1, a->cls[0] = WA_SSE, a->up = true;	/* one xmm register */
		return;
	}
	a->n = cls[1] != XC_NONE ? 2 : cls[0] != XC_NONE ? 1 : 0;
	for (int i = 0; i < a->n; i++)
		a->cls[i] = cls[i] == XC_SSE ? WA_SSE : WA_INT;
}

static int x64_need(const wkarg *a, int cl)
{
	int k = 0;

	for (int i = 0; i < a->n; i++)
		k += a->cls[i] == cl;
	return k;
}


/* fldt / fstpt of l (fp.c's x87 records) */
VPRIV void x64_x87ld(ecx *e, const lval *l);
VPRIV void x64_x87st(ecx *e, const lval *l);

/* ---------------- calls ---------------- */

static lval x64_fr(int64_t off) { return (lval){ .k = L_FRAME, .off = off, .align = 8 }; }

VPRIV val x64_call(ecx *e, uint32_t ref, val *fn, const wkarg *a, int n, bool variadic, const wkarg *r, qty rt)
{
	cc *c = e->c;

	if (RKIND(ref) == RK_SYM) {
		x86_features features = cc_x86_features(c, RIDX(ref));
		for (int k = 0; k < n; k++)
			if (!a[k].unnamed) x64_check_vector_abi(e, a[k].size, a[k].vector, a[k].wide_record, features);
		x64_check_vector_abi(e, tsize(c, rt), is_vec(c, rt), x64_wide_record(c, rt), features);
	}

	/* An explicit builtin needs no libm symbol. Copy the x87 representation
	 * and clear only its sign, without an FP load that could alter a NaN. */
	if (RKIND(ref) == RK_NAME && RIDX(ref) < 0x8000000u && n == 1 && r && r->x87 &&
	    KIND(c, rt) == TY_LDOUBLE && !strcmp(cc_builtin_name(RIDX(ref)), "__builtin_fabsl")) {
		int q = x64_treg(e);
		lval src = x64_fr(a[0].slot), dst = x64_fr(r->slot);

		x64_mem(e, X_LD, 8, q, &src, false);
		x64_mem(e, X_ST, 8, q, &dst, false);
		src.off += 8;
		dst.off += 8;
		x64_mem(e, X_LD, 2, q, &src, false);
		x64_op3(e, X_ANDI, 8, q, q, XNOREG)->imm = 0x7fff;
		x64_mem(e, X_ST, 2, q, &dst, false);
		x64_tfree(e, q);
		return vconst(0);
	}
	xst *X = e->x64;
	uint32_t saved = X->live & X_TEMPS, fsaved = X->live & X_FTEMPS;
	int retsize = KIND(c, rt) == TY_VOID ? 0 : wk_size(c, rt);
	bool retsgn = wk_signed(c, rt), retf = is_flt(c, rt) && !r;
	int gp = r && r->mem && !r->x87 && !r->up ? 1 : 0, fp = 0;
	int64_t total = 0, pad;
	uint32_t stackalign = 16;
	int64_t savedsp = 0;
	int64_t *place = calloc((size_t)(n ? n : 1), sizeof *place);	/* stack offset, or -1: registers */
	rec *i;

	for (int k = 0; k < n; k++) {
		int ng = x64_need(&a[k], WA_INT), nf = x64_need(&a[k], WA_SSE);

		if (!a[k].mem && a[k].n == 0) {
			place[k] = -1;
			continue;
		}
		if (!a[k].mem && gp + ng <= 6 && fp + nf <= 8) {
			gp += ng, fp += nf;
			place[k] = -1;
			continue;
		}
		if (a[k].align > stackalign)
			stackalign = a[k].align;
		total = (total + a[k].align - 1) & ~(int64_t)(a[k].align - 1);
		place[k] = total;
		total += (int64_t)((a[k].size + 7) & ~(uint64_t)7);
	}
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
	if (stackalign > 16) {
		lval slot = x64_fr(savedsp = wk_slot(e, 8, 8));

		x64_mem(e, X_ST, 8, RSP, &slot, false);
	}
	total = (total + 15) & ~(int64_t)15;
	pad = (X->pushes * 8 + total) % 16 ? 8 : 0;
	if (total + pad) {
		x64_put(e, X_ADJSP)->imm = -(total + pad);
		X->pushes += (int)((total + pad) / 8);
	}
	if (stackalign > 16)
		x64_op3(e, X_ANDI, 8, RSP, RSP, XNOREG)->imm = -(int64_t)stackalign;
	/* stack arguments: their slots' words into the outgoing area */
	for (int k = 0; k < n; k++) {
		if (place[k] < 0)
			continue;
		if (a[k].size > 128) {
			/* A large by-value aggregate must not expand to one record per
			   word. Preserve live values (including an indirect callee)
			   while using the same REP copy as ordinary record assignment. */
			static const uint8_t regs[] = { RDI, RSI, RCX };
			uint32_t sv = x64_save(e, regs, 3, -1, -1);
			lval s = x64_fr(a[k].slot);
			lval d = { .k = L_REG, .r = RSP, .off = place[k] + 8 * __builtin_popcount(sv) };

			x64_mem(e, X_LEA, 8, RSI, &s, false);
			x64_mem(e, X_LEA, 8, RDI, &d, false);
			x64_movi(e, RCX, (int64_t)a[k].size);
			x64_put(e, X_REP)->cond = 0;
			x64_restore(e, sv);
			continue;
		}
		for (uint64_t o = 0; o < a[k].size; o += 8) {
			lval s = x64_fr(a[k].slot + (int64_t)o), d = { .k = L_REG, .r = RSP, .off = place[k] + (int64_t)o };

			x64_mem(e, X_LD, 8, R11, &s, false);
			x64_mem(e, X_ST, 8, R11, &d, false);
		}
	}
	if (fn)
		x64_mov(e, R11, fn->r);
	/* register arguments */
	gp = 0, fp = 0;
	if (r && r->mem && !r->x87 && !r->up) {
		lval s = x64_fr(r->slot);

		x64_mem(e, X_LEA, 8, x64_argreg(gp++), &s, false);
	}
	for (int k = 0; k < n; k++) {
		if (place[k] >= 0)
			continue;
		for (int w = 0; w < a[k].n; w++) {
			lval s = x64_fr(a[k].slot + 8 * w);

			x64_mem(e, X_LD, a[k].up ? 16 : 8, a[k].cls[w] == WA_SSE ? XMM0 + fp++ : x64_argreg(gp++), &s, false);
		}
	}
	free(place);
	if (variadic)
		x64_op3(e, X_MOVI, 4, RAX, XNOREG, XNOREG)->imm = fp;	/* al: vector registers used */
	if (fn) {
		x64_op3(e, X_CALLR, 8, XNOREG, R11, XNOREG);
	} else {
		i = x64_put(e, X_CALL);
		i->ref = ref;
		i->cond = variadic;
	}
	if (savedsp) {
		lval slot = x64_fr(savedsp);

		x64_mem(e, X_LD, 8, RSP, &slot, false);
		X->pushes -= (int)((total + pad) / 8);
	} else if (total + pad) {
		x64_put(e, X_ADJSP)->imm = total + pad;
		X->pushes -= (int)((total + pad) / 8);
	}
	if (r && r->x87) {
		lval d = x64_fr(r->slot);

		x64_x87st(e, &d);
		if (r->size == 32) {
			d.off += 16;
			x64_x87st(e, &d);	/* _Complex long double: the imaginary part from st(1) */
		}
	} else if (r && r->mem && r->up) {
		for (int w = 0; w < (int)(r->size / 16); w++) {
			lval d = x64_fr(r->slot + 16 * w);

			x64_mem(e, X_ST, 16, XMM0 + w, &d, false);
		}
	} else if (r && !r->mem && r->n) {
		/* a record in rax, rdx / xmm0, xmm1 (by class, in order) into its slot */
		int ng = 0, nf = 0;

		for (int w = 0; w < r->n; w++) {
			lval d = x64_fr(r->slot + 8 * w);

			x64_mem(e, X_ST, r->up ? 16 : 8, r->cls[w] == WA_SSE ? XMM0 + nf++ : (ng++ ? RDX : RAX), &d, false);
		}
	} else if (retsize && !retf && !r) {
		x64_mov(e, R11, RAX);
		x64_ext(e, R11, retsize, retsgn);
	}
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
	if (fn && fn->k == V_REG)
		x64_tfree(e, fn->r);
	if (!retsize || r)
		return vconst(0);
	if (retf) {
		int q = x64_ftreg(e);

		x64_mov(e, q, XMM0);
		return (val){ .k = V_REG, .r = (uint8_t)q };
	}
	{
		int q = x64_treg(e);

		x64_mov(e, q, R11);
		return (val){ .k = V_REG, .r = (uint8_t)q };
	}
}

/* ---------------- the incoming side ---------------- */

VPRIV void x64_params(ecx *e, wkarg *a, int n, const wkarg *r, bool variadic)
{
	xst *X = e->x64;
	int gp = 0, fp = 0;
	int64_t stk = 0;

	const attrs *fa = sym_attrs(e->c, e->wk->fsym);
	X->interrupt = fa && fa->interrupt ? n : 0;
	X->sret = 0;
	if (X->interrupt) {
		/* The printer saves every GPR and establishes an aligned R12 frame
		   before these records execute; RBP retains the hardware frame. */
		if (e->wk->falign < 32) e->wk->falign = 32;
		lval frame = { .k = L_REG, .r = RBP, .off = n == 2 ? 16 : 8 };
		lval dst = x64_fr(a[0].slot);
		x64_mem(e, X_LEA, 8, R11, &frame, false);
		x64_mem(e, X_ST, 8, R11, &dst, false);
		if (n == 2) {
			frame.off = 8;
			dst = x64_fr(a[1].slot);
			x64_mem(e, X_LD, 8, R11, &frame, false);
			x64_mem(e, X_ST, 8, R11, &dst, false);
		}
		return;
	}
	if (r && r->mem && !r->x87 && !r->up) {
		lval d = x64_fr(r->slot);

		x64_mem(e, X_ST, 8, x64_argreg(gp++), &d, false);
		X->sret = r->slot;
	}
	for (int k = 0; k < n; k++) {
		int ng = x64_need(&a[k], WA_INT), nf = x64_need(&a[k], WA_SSE);

		if (!a[k].mem && a[k].n == 0)
			continue;
		if (!a[k].mem && gp + ng <= 6 && fp + nf <= 8) {
			for (int w = 0; w < a[k].n; w++) {
				lval d = x64_fr(a[k].slot + 8 * w);

				x64_mem(e, X_ST, a[k].up ? 16 : 8, a[k].cls[w] == WA_SSE ? XMM0 + fp++ : x64_argreg(gp++), &d, false);
			}
			continue;
		}
		/* on the stack: the parameter is where the caller put it */
		stk = (stk + a[k].align - 1) & ~(int64_t)(a[k].align - 1);
		a[k].slot = 16 + stk;
		stk += (int64_t)((a[k].size + 7) & ~(uint64_t)7);
	}
	X->ngp = gp, X->nfp = fp, X->stk = (int32_t)stk;
	X->save = 0;
	if (variadic) {
		/* the register save area: rdi..r9, then xmm0..xmm7 if al says vector registers were used */
		uint32_t skip = wk_newlabel(e);
		rec *j;

		X->save = wk_slot(e, 48 + 128, 16);
		for (int k = gp; k < 6; k++) {
			lval d = x64_fr(X->save + 8 * k);

			x64_mem(e, X_ST, 8, x64_argreg(k), &d, false);
		}
		if (x64_nosimd(e))
			return;	/* no vector registers to save (-mgeneral-regs-only, -mno-sse): as clang */
		x64_op3(e, X_TEST, 1, XNOREG, RAX, XNOREG);
		j = x64_put(e, X_JCC);
		j->cond = CC_E;
		j->label = skip;
		for (int k = fp; k < 8; k++) {
			lval d = x64_fr(X->save + 48 + 16 * k);

			x64_mem(e, X_ST, 16, XMM0 + k, &d, false);
		}
		x64_label(e, skip);
	}
}

/* a record result: its first words (from the object at raddr) in rax, rdx, or
   copied to the caller's object (the hidden pointer, also the result) */
VPRIV void x64_retrec(ecx *e, int raddr, const wkarg *r, uint32_t lbl)
{
	if (r->x87) {
		lval s = { .k = L_REG, .r = (uint8_t)raddr };

		if (r->size == 32) {
			lval im = s;

			im.off = 16;
			x64_x87ld(e, &im);	/* _Complex long double: the imaginary part to st(1) */
		}
		x64_x87ld(e, &s);	/* long double: in st(0) */
	} else if (r->mem && r->up) {
		for (int w = 0; w < (int)(r->size / 16); w++) {
			lval s = { .k = L_REG, .r = (uint8_t)raddr, .off = 16 * w };

			x64_mem(e, X_LD, 16, XMM0 + w, &s, false);
		}
	} else if (r->mem) {
		lval s = x64_fr(e->x64->sret);
		int rd = x64_treg(e);

		x64_mem(e, X_LD, 8, rd, &s, false);
		x64_copy(e, rd, raddr, r->size);
		x64_mov(e, RAX, rd);
		x64_tfree(e, rd);
	} else if (r->n) {
		/* the words by class: rax, rdx / xmm0, xmm1; a word for rax read last when raddr is rax */
		int ng = 0, nf = 0, dst[2];

		for (int w = 0; w < r->n; w++)
			dst[w] = r->cls[w] == WA_SSE ? XMM0 + nf++ : (ng++ ? RDX : RAX);
		for (int pass = 0; pass < 2; pass++)
			for (int w = 0; w < r->n; w++)
				if ((dst[w] == raddr) == (pass == 1)) {
					lval s = { .k = L_REG, .r = (uint8_t)raddr, .off = 8 * w };

					x64_mem(e, X_LD, r->up ? 16 : 8, dst[w], &s, false);
				}
	}
	x64_tfree(e, raddr);
	x64_jump(e, lbl);
}

/* ---------------- varargs ---------------- */

/* va_list at rap: gp_offset, fp_offset, overflow_arg_area, reg_save_area */
VPRIV void x64_va_start(ecx *e, int rap)
{
	xst *X = e->x64;
	lval gpo = { .k = L_REG, .r = (uint8_t)rap, .off = 0 }, fpo = { .k = L_REG, .r = (uint8_t)rap, .off = 4 };
	lval ov = { .k = L_REG, .r = (uint8_t)rap, .off = 8 }, rs = { .k = L_REG, .r = (uint8_t)rap, .off = 16 };
	lval sa = x64_fr(X->save), oa = x64_fr(16 + X->stk);

	if (!X->save)
		efatal(e, e->curloc, "va_start in a function that is not variadic");
	x64_movi(e, R11, 8 * X->ngp);
	x64_mem(e, X_ST, 4, R11, &gpo, false);
	x64_movi(e, R11, 48 + 16 * X->nfp);
	x64_mem(e, X_ST, 4, R11, &fpo, false);
	x64_mem(e, X_LEA, 8, R11, &oa, false);
	x64_mem(e, X_ST, 8, R11, &ov, false);
	x64_mem(e, X_LEA, 8, R11, &sa, false);
	x64_mem(e, X_ST, 8, R11, &rs, false);
}

/* the address of the next argument of type t, in a temporary (System V 3.5.7):
   from the register save area while its registers last (the words gathered
   into a slot, general and vector ones apart there), else the overflow area */
VPRIV int x64_va_arg(ecx *e, int rap, qty t)
{
	wkarg a;
	lval gpo = { .k = L_REG, .r = (uint8_t)rap, .off = 0 }, fpo = { .k = L_REG, .r = (uint8_t)rap, .off = 4 };
	lval ov = { .k = L_REG, .r = (uint8_t)rap, .off = 8 }, rsa = { .k = L_REG, .r = (uint8_t)rap, .off = 16 };
	int ra = x64_treg(e), ng, nf;
	uint32_t lstk = wk_newlabel(e), ldone = wk_newlabel(e);

	x64_classify(e, t, &a, true);
	ng = x64_need(&a, WA_INT), nf = x64_need(&a, WA_SSE);
	if (!a.mem && a.n) {
		int rg = x64_treg(e), rf = x64_treg(e), rs = x64_treg(e);
		lval tmp = x64_fr(wk_slot(e, 16, 16));
		rec *j;

		x64_mem(e, X_LD, 4, rg, &gpo, false);
		x64_mem(e, X_LD, 4, rf, &fpo, false);
		if (ng) {
			x64_op3(e, X_CMPI, 4, XNOREG, rg, XNOREG)->imm = 48 - 8 * ng;
			j = x64_put(e, X_JCC);
			j->cond = CC_A;
			j->label = lstk;
		}
		if (nf) {
			x64_op3(e, X_CMPI, 4, XNOREG, rf, XNOREG)->imm = 176 - 16 * nf;
			j = x64_put(e, X_JCC);
			j->cond = CC_A;
			j->label = lstk;
		}
		x64_mem(e, X_LEA, 8, ra, &tmp, false);
		x64_mem(e, X_LD, 8, rs, &rsa, false);
		for (int w = 0; w < a.n; w++) {
			int ro = a.cls[w] == WA_SSE ? rf : rg;
			lval src = { .k = L_REG, .r = R11 }, dst = { .k = L_REG, .r = (uint8_t)ra, .off = 8 * w };

			x64_mov(e, R11, rs);
			x64_op3(e, X_ADD, 8, R11, R11, ro);
			x64_mem(e, X_LD, 8, R11, &src, false);
			x64_mem(e, X_ST, 8, R11, &dst, false);
			if (a.up) {	/* a 16-byte vector: the whole register's save slot */
				x64_mov(e, R11, rs);
				x64_op3(e, X_ADD, 8, R11, R11, ro);
				src.off = 8, dst.off += 8;
				x64_mem(e, X_LD, 8, R11, &src, false);
				x64_mem(e, X_ST, 8, R11, &dst, false);
			}
			x64_op3(e, X_ADDI, 4, ro, ro, XNOREG)->imm = a.cls[w] == WA_SSE ? 16 : 8;
		}
		x64_mem(e, X_ST, 4, rg, &gpo, false);
		x64_mem(e, X_ST, 4, rf, &fpo, false);
		x64_tfree(e, rg);
		x64_tfree(e, rf);
		x64_tfree(e, rs);
		x64_jump(e, ldone);
	}
	x64_label(e, lstk);
	x64_mem(e, X_LD, 8, ra, &ov, false);
	/* An empty aggregate consumes no argument area, including alignment
	   padding. Keep the overflow cursor unchanged for the following value. */
	if (a.size && a.align > 8) {
		x64_op3(e, X_ADDI, 8, ra, ra, XNOREG)->imm = (int64_t)a.align - 1;
		x64_op3(e, X_ANDI, 8, ra, ra, XNOREG)->imm = -(int64_t)a.align;
	}
	{
		lval nx = { .k = L_REG, .r = (uint8_t)ra, .off = (int64_t)((a.size + 7) & ~(uint64_t)7) };

		x64_mem(e, X_LEA, 8, R11, &nx, false);
		x64_mem(e, X_ST, 8, R11, &ov, false);
	}
	x64_label(e, ldone);
	return ra;
}

VPRIV void x64_ret(ecx *e, val *v, qty t, uint32_t lbl)
{
	cc *c = e->c;
	int size = wk_size(c, t);
	bool sgn = wk_signed(c, t);

	if (v && is_flt(c, t)) {
		x64_mov(e, XMM0, v->r);
		x64_tfree(e, v->r);
		v = NULL;
	}
	if (v) {
		int r = x64_toreg(e, v, size);

		x64_ext(e, r, size, sgn);
		x64_mov(e, RAX, r);
		x64_tfree(e, r);
	}
	x64_jump(e, lbl);
}
