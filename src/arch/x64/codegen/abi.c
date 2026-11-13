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
static bool x64_cls(cc *c, ast_qualified_type t, uint64_t off, int cls[2])
{
	const ctype *x = T(c, t);
	int k = x->kind;

	if (k == AST_TYPE_STRUCT || k == AST_TYPE_UNION) {
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
	if (k == AST_TYPE_ARRAY) {
		uint64_t es = codegen_type_size(c, x->base), n = es ? codegen_type_size(c, t) / es : 0;

		for (uint64_t i = 0; i < n; i++)
			if (!x64_cls(c, x->base, off + i * es, cls))
				return false;
		return true;
	}
	if (k == AST_TYPE_VECTOR) {
		/* clang's classes for vectors: up to 4 bytes INTEGER; 8 bytes SSE (<1 x double>
		   memory, <1 x i64> SSE); 16 bytes SSE + SSEUP; larger: memory (no AVX) */
		uint64_t sz = codegen_type_size(c, t), es = codegen_type_size(c, x->base);

		if (sz <= 4) {
			cls[off / 8] = x64_merge(cls[off / 8], XC_INT);
			return (off % 8) + sz <= 8;
		}
		if (sz == 8 && off % 8 == 0) {
			if (es == 8 && codegen_type_is_float(c, x->base))
				return false;
			cls[off / 8] = x64_merge(cls[off / 8], XC_SSE);
			return true;
		}
		if (codegen_type_is_bool_vector(c, t) && sz == 16 && off == 0) {
			cls[0] = x64_merge(cls[0], XC_SSE);
			cls[1] = x64_merge(cls[1], XC_SSEUP);
			return true;
		}
		if (sz == 16 && off == 0 && cls[0] == XC_NONE && cls[1] == XC_NONE) {
			cls[0] = XC_SSE, cls[1] = XC_SSEUP;
			return true;
		}
		return false;	/* (a 32-byte vector alone: see x64_classify) */
	}
	if (k == AST_TYPE_FLOAT128) {
		if (off % 16 || cls[off / 8] != XC_NONE)
			return false;
		cls[off / 8] = XC_SSE, cls[off / 8 + 1] = XC_SSEUP;
		return true;
	}
	if (k == AST_TYPE_LONG_DOUBLE && off == 0) {
		cls[0] = x64_merge(cls[0], XC_X87);
		cls[1] = x64_merge(cls[1], XC_X87UP);
		return true;
	}
	if (k == AST_TYPE_LONG_DOUBLE || k == AST_TYPE_INT128 || k == AST_TYPE_UINT128)
		return k == AST_TYPE_INT128 || k == AST_TYPE_UINT128 ? (cls[off / 8] = x64_merge(cls[off / 8], XC_INT),
							    cls[off / 8 + 1] = x64_merge(cls[off / 8 + 1], XC_INT), true) : false;
	if (codegen_type_size(c, t) == 0)
		return true;
	cls[off / 8] = x64_merge(cls[off / 8], codegen_type_is_float(c, t) ? XC_SSE : XC_INT);
	return true;
}

/* Wide records use AVX registers only for SSE followed entirely by SSEUP.
   Track all eightbytes, including union merges and padding, without changing
   the existing two-eightbyte transport classifier. */
static bool x64_wide_cls(cc *c, ast_qualified_type t, uint64_t off, int cls[8])
{
	const ctype *x = T(c, t);
	uint64_t size = codegen_type_size(c, t);

	if (off + size > 64)
		return false;
	if (x->kind == AST_TYPE_STRUCT || x->kind == AST_TYPE_UNION) {
		const tag *r = &c->tags[x->aux];
		int fields[8] = {0};

		for (uint32_t i = 0; i < r->nfield; i++) {
			const field *f = &r->f[i];
			uint64_t fo = off + f->off / 8;

			if (f->bitfield && !f->name) continue;
			if (size > 16 && x->kind != AST_TYPE_UNION && codegen_type_size(c, f->ty) != size)
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
	if (x->kind == AST_TYPE_ARRAY) {
		uint64_t es = codegen_type_size(c, x->base);

		/* Clang's wide-array early exit leaves NoClass after resetting
		   Current. In a union this contributes nothing to a wide vector. */
		if (size > 16 && size != es) return true;

		for (uint64_t i = 0; es && i < size / es; i++)
			if (!x64_wide_cls(c, x->base, off + i * es, cls))
				return false;
		return true;
	}
	if (!size) return true;
	if (x->kind == AST_TYPE_VECTOR && size >= 16) {
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

static bool x64_wide_record(cc *c, ast_qualified_type t)
{
	uint64_t size = codegen_type_size(c, t);
	int cls[8] = {0};

	if (!codegen_type_is_record(c, t) || (size != 32 && size != 64) || !x64_wide_cls(c, t, 0, cls) || cls[0] != XC_SSE)
		return false;
	for (unsigned i = 1; i < size / 8; i++)
		if (cls[i] != XC_SSEUP) return false;
	return true;
}

static void x64_check_vector_abi(codegen_context *e, uint64_t size, bool vector, bool wide_record, x86_features features)
{
	bool unsupported = vector && size > 16 && x86_feature_has(features, X86_FEATURE_AVX);

	unsupported |= wide_record && x86_feature_has(features, size == 64 ? X86_FEATURE_AVX512F : X86_FEATURE_AVX);
	if (unsupported)
		codegen_fatal_report(e, e->current_location, "x86-64: AVX vector argument or return ABI is not supported yet (PLAN-x86.md)");
}

/* Clang retains the i1-vector ABI through singleton aggregate wrappers. */
static uint32_t x64_bool_lanes(cc *c, ast_qualified_type t)
{
	const ctype *ty = T(c, t);
	if (codegen_type_is_bool_vector(c, t)) return ty->n;
	if (codegen_type_is_array(c, t) && codegen_type_size(c, t) == codegen_type_size(c, ty->base))
		return x64_bool_lanes(c, ty->base);
	if (codegen_type_is_record(c, t)) {
		const tag *r = &c->tags[ty->aux];
		const field *only = NULL;
		for (uint32_t k = 0; k < r->nfield; k++) {
			const field *f = &r->f[k];
			if (!codegen_type_size(c, f->ty) || (f->bitfield && !f->width)) continue;
			if (only || f->off || f->bitfield) return 0;
			only = f;
		}
		if (only && codegen_type_size(c, only->ty) == codegen_type_size(c, t)) return x64_bool_lanes(c, only->ty);
	}
	return 0;
}

VPRIV void x64_classify0(codegen_context *e, ast_qualified_type t, walk_argument *a);

/* -mgeneral-regs-only, -mno-sse: no value may travel in a vector or x87 register */
VPRIV void x64_classify(codegen_context *e, ast_qualified_type t, walk_argument *a, bool unnamed)
{
	x64_classify0(e, t, a);
	a->is_unnamed = unnamed;
	uint32_t fn = e->walk_state->inline_call ? e->walk_state->inline_call->function : e->walk_state->function_symbol;
	/* Unnamed wide vectors/records use the overflow area, never YMM/ZMM. */
	if (!unnamed)
		x64_check_vector_abi(e, a->size, a->is_vector, a->is_wide_record, cc_x86_features_get(e->compile_context, fn));
	if (x64_nosimd(e) && a->is_in_memory && a->is_sse_up)
		a->is_sse_up = false;	/* a 32-byte vector: returned in memory, not xmm0:xmm1 */
	if (x64_nosimd(e) && (a->is_x87 || (!a->is_in_memory && a->eightbyte_count && (a->classes[0] == WALK_ARGUMENT_SSE || (a->eightbyte_count > 1 && a->classes[1] == WALK_ARGUMENT_SSE)))))
		x64_nofp(e);
}

VPRIV void x64_classify0(codegen_context *e, ast_qualified_type t, walk_argument *a)
{
	cc *c = e->compile_context;
	uint64_t size = codegen_type_size(c, t);
	int cls[2] = { XC_NONE, XC_NONE };

	memset(a, 0, sizeof *a);
	a->size = size;
	a->is_vector = codegen_type_is_vector(c, t);
	a->is_wide_record = x64_wide_record(c, t);
	a->align = codegen_type_alignment(c, t) > 8 ? codegen_type_alignment(c, t) : 8;
	/* SysV classifies _BitInt wider than 128 bits as MEMORY, including
	 * unnamed variadic arguments. Returns use the ordinary hidden pointer. */
	if (codegen_type_is_bitint_big(c, t)) {
		a->align = 8; /* typedef alignment does not change the scalar ABI */
		a->is_in_memory = true;
		return;
	}
	if (!codegen_type_is_record(c, t) && !codegen_type_is_array(c, t) && !codegen_type_is_vector(c, t)) {
		a->eightbyte_count = size > 8 ? 2 : 1;
		a->classes[0] = a->classes[1] = codegen_type_is_float(c, t) ? WALK_ARGUMENT_SSE : WALK_ARGUMENT_INTEGER;
		if (KIND(c, t) == AST_TYPE_LONG_DOUBLE)
			a->is_in_memory = true, a->eightbyte_count = 0, a->is_x87 = true;	/* passed in memory, returned in st(0) */
		if (KIND(c, t) == AST_TYPE_FLOAT128)
			a->eightbyte_count = 1, a->classes[0] = WALK_ARGUMENT_SSE, a->is_sse_up = true;	/* SSE + SSEUP: one xmm register */
		return;
	}
	if (size == 0)
		return;	/* an empty record: nothing is passed */
	uint32_t blanes = x64_bool_lanes(c, t);
	if (blanes && size > 8) {
		/* LLVM scalarizes <65..128 x i1> arguments into individual integer
		   arguments. Results use a hidden pointer; larger arguments are
		   packed by-value memory rather than ordinary SIMD vectors. */
		a->is_in_memory = true;
		a->bool_lane_count = blanes <= 128 ? blanes : 0;
		return;
	}
	if (codegen_type_is_vector(c, t) && (size == 32 || size == 64)) {
		/* clang without AVX: passed in memory, returned in 2 or 4 xmm registers (-Wpsabi) */
		a->is_in_memory = true, a->is_sse_up = true;
		return;
	}
	if (codegen_type_is_record(c, t) && (c->tags[T(c, t)->aux].flags & AST_RECORD_FLAG_COMPLEX) && KIND(c, c->tags[T(c, t)->aux].f[0].ty) == AST_TYPE_LONG_DOUBLE) {
		a->is_in_memory = true, a->is_x87 = true;	/* COMPLEX_X87: passed in memory, returned in st(0) (real), st(1) */
		return;
	}
	if (size > 16 || !x64_cls(c, t, 0, cls) || cls[0] == XC_MEM || cls[1] == XC_MEM) {
		a->is_in_memory = true;
		return;
	}
	if (cls[0] == XC_X87 && cls[1] == XC_X87UP) {
		a->is_in_memory = true, a->is_x87 = true;
		return;
	}
	if (cls[0] == XC_X87 || cls[1] == XC_X87UP) {
		a->is_in_memory = true;
		return;
	}
	/* An orphan SSEUP eightbyte (e.g. vector/integer union) is SSE. */
	if (cls[1] == XC_SSEUP && cls[0] != XC_SSE) cls[1] = XC_SSE;
	if (cls[1] == XC_SSEUP) {
		a->eightbyte_count = 1, a->classes[0] = WALK_ARGUMENT_SSE, a->is_sse_up = true;	/* one xmm register */
		return;
	}
	a->eightbyte_count = cls[1] != XC_NONE ? 2 : cls[0] != XC_NONE ? 1 : 0;
	for (int i = 0; i < a->eightbyte_count; i++)
		a->classes[i] = cls[i] == XC_SSE ? WALK_ARGUMENT_SSE : WALK_ARGUMENT_INTEGER;
}

static int x64_need(const walk_argument *a, int cl)
{
	int k = 0;

	for (int i = 0; i < a->eightbyte_count; i++)
		k += a->classes[i] == cl;
	return k;
}


/* fldt / fstpt of l (fp.c's x87 records) */
VPRIV void x64_x87ld(codegen_context *e, const codegen_lvalue *l);
VPRIV void x64_x87st(codegen_context *e, const codegen_lvalue *l);

/* ---------------- calls ---------------- */

static codegen_lvalue x64_fr(int64_t off) { return (codegen_lvalue){ .kind = CODEGEN_LVALUE_FRAME, .offset = off, .align = 8 }; }

VPRIV codegen_value x64_call(codegen_context *e, uint32_t ref, codegen_value *fn, const walk_argument *a, int n, bool variadic, const walk_argument *r, ast_qualified_type rt)
{
	cc *c = e->compile_context;

	if (CODEGEN_REFERENCE_KIND(ref) == CODEGEN_REFERENCE_SYMBOL) {
		x86_features features = cc_x86_features_get(c, CODEGEN_REFERENCE_INDEX(ref));
		for (int k = 0; k < n; k++)
			if (!a[k].is_unnamed) x64_check_vector_abi(e, a[k].size, a[k].is_vector, a[k].is_wide_record, features);
		x64_check_vector_abi(e, codegen_type_size(c, rt), codegen_type_is_vector(c, rt), x64_wide_record(c, rt), features);
	}

	if (variadic) {
		/* The walker knows fixed parameters for direct and indirect calls. */
		for (int k = 0; k < n; k++)
			if (a[k].bool_lane_count && a[k].is_unnamed)
				codegen_fatal_report(e, e->current_location, "x86-64: variadic transport of 65..128-lane packed boolean vectors is not supported");
	}

	/* An explicit builtin needs no libm symbol. Copy the x87 representation
	 * and clear only its sign, without an FP load that could alter a NaN. */
	if (CODEGEN_REFERENCE_KIND(ref) == CODEGEN_REFERENCE_NAME && CODEGEN_REFERENCE_INDEX(ref) < 0x8000000u && n == 1 && r && r->is_x87 &&
	    KIND(c, rt) == AST_TYPE_LONG_DOUBLE && cc_builtin_library_kind(CODEGEN_REFERENCE_INDEX(ref)) == CC_LIBRARY_KIND_FABSL) {
		int q = x64_treg(e);
		codegen_lvalue src = x64_fr(a[0].slot), dst = x64_fr(r->slot);

		x64_mem(e, X_LD, 8, q, &src, false);
		x64_mem(e, X_ST, 8, q, &dst, false);
		src.offset += 8;
		dst.offset += 8;
		x64_mem(e, X_LD, 2, q, &src, false);
		x64_op3(e, X_ANDI, 8, q, q, XNOREG)->immediate = 0x7fff;
		x64_mem(e, X_ST, 2, q, &dst, false);
		x64_tfree(e, q);
		return codegen_value_constant(0);
	}
	xst *X = e->x64_context;
	uint32_t saved = X->live & X_TEMPS, fsaved = X->live & X_FTEMPS;
	int retsize = KIND(c, rt) == AST_TYPE_VOID ? 0 : walk_type_size(c, rt);
	bool retsgn = walk_type_is_signed(c, rt), retf = codegen_type_is_float(c, rt) && !r;
	int gp = r && r->is_in_memory && !r->is_x87 && !r->is_sse_up ? 1 : 0, fp = 0;
	int64_t total = 0, pad;
	uint32_t stackalign = 16;
	int64_t savedsp = 0;
	int64_t *place = calloc((size_t)(n ? n : 1), sizeof *place);	/* stack offset, or -1: registers */
	int *bregs = calloc((size_t)(n ? n : 1), sizeof *bregs);
	record *i;

	for (int k = 0; k < n; k++) {
		int ng = x64_need(&a[k], WALK_ARGUMENT_INTEGER), nf = x64_need(&a[k], WALK_ARGUMENT_SSE);

		if (a[k].bool_lane_count) {
			bregs[k] = 6 - gp;
			gp = 6;
			place[k] = total;
			total += ((int64_t)a[k].bool_lane_count - bregs[k]) * 8;
			continue;
		}
		if (!a[k].is_in_memory && a[k].eightbyte_count == 0) {
			place[k] = -1;
			continue;
		}
		if (!a[k].is_in_memory && gp + ng <= 6 && fp + nf <= 8) {
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
			codegen_lvalue d = { .kind = CODEGEN_LVALUE_REGISTER, .register_number = RSP };

			x64_put(e, X_ADJSP)->immediate = -8;
			x64_mem(e, X_ST, 8, q, &d, false);
			X->pushes++;
		}
	if (stackalign > 16) {
		codegen_lvalue slot = x64_fr(savedsp = walk_slot_temporary(e, 8, 8));

		x64_mem(e, X_ST, 8, RSP, &slot, false);
	}
	total = (total + 15) & ~(int64_t)15;
	pad = (X->pushes * 8 + total) % 16 ? 8 : 0;
	if (total + pad) {
		x64_put(e, X_ADJSP)->immediate = -(total + pad);
		X->pushes += (int)((total + pad) / 8);
	}
	if (stackalign > 16)
		x64_op3(e, X_ANDI, 8, RSP, RSP, XNOREG)->immediate = -(int64_t)stackalign;
	/* stack arguments: their slots' words into the outgoing area */
	for (int k = 0; k < n; k++) {
		if (place[k] < 0)
			continue;
		if (a[k].bool_lane_count) {
			for (uint32_t bit = (uint32_t)bregs[k]; bit < a[k].bool_lane_count; bit++) {
				codegen_lvalue src = x64_fr(a[k].slot + bit / 8);
				codegen_lvalue dst = { .kind = CODEGEN_LVALUE_REGISTER, .register_number = RSP, .offset = place[k] + 8 * (bit - bregs[k]) };
				x64_mem(e, X_LD, 1, R11, &src, false);
				if (bit % 8) x64_op3(e, X_SHRI, 8, R11, R11, XNOREG)->immediate = bit % 8;
				x64_op3(e, X_ANDI, 8, R11, R11, XNOREG)->immediate = 1;
				x64_mem(e, X_ST, 8, R11, &dst, false);
			}
			continue;
		}
		if (a[k].size > 128) {
			/* A large by-value aggregate must not expand to one record per
			   word. Preserve live values (including an indirect callee)
			   while using the same REP copy as ordinary record assignment. */
			static const uint8_t regs[] = { RDI, RSI, RCX };
			uint32_t sv = x64_save(e, regs, 3, -1, -1);
			codegen_lvalue s = x64_fr(a[k].slot);
			codegen_lvalue d = { .kind = CODEGEN_LVALUE_REGISTER, .register_number = RSP, .offset = place[k] + 8 * __builtin_popcount(sv) };

			x64_mem(e, X_LEA, 8, RSI, &s, false);
			x64_mem(e, X_LEA, 8, RDI, &d, false);
			x64_movi(e, RCX, (int64_t)a[k].size);
			x64_put(e, X_REP)->condition = 0;
			x64_restore(e, sv);
			continue;
		}
		for (uint64_t o = 0; o < a[k].size; o += 8) {
			codegen_lvalue s = x64_fr(a[k].slot + (int64_t)o), d = { .kind = CODEGEN_LVALUE_REGISTER, .register_number = RSP, .offset = place[k] + (int64_t)o };

			x64_mem(e, X_LD, 8, R11, &s, false);
			x64_mem(e, X_ST, 8, R11, &d, false);
		}
	}
	if (fn)
		x64_mov(e, R11, fn->register_number);
	/* register arguments */
	gp = 0, fp = 0;
	if (r && r->is_in_memory && !r->is_x87 && !r->is_sse_up) {
		codegen_lvalue s = x64_fr(r->slot);

		x64_mem(e, X_LEA, 8, x64_argreg(gp++), &s, false);
	}
	for (int k = 0; k < n; k++) {
		if (a[k].bool_lane_count) {
			for (int bit = 0; bit < bregs[k]; bit++) {
				codegen_lvalue src = x64_fr(a[k].slot + bit / 8);
				int reg = x64_argreg(gp++);
				x64_mem(e, X_LD, 1, reg, &src, false);
				if (bit % 8) x64_op3(e, X_SHRI, 8, reg, reg, XNOREG)->immediate = bit % 8;
				x64_op3(e, X_ANDI, 8, reg, reg, XNOREG)->immediate = 1;
			}
			continue;
		}
		if (place[k] >= 0)
			continue;
		for (int w = 0; w < a[k].eightbyte_count; w++) {
			codegen_lvalue s = x64_fr(a[k].slot + 8 * w);

			x64_mem(e, X_LD, a[k].is_sse_up ? 16 : 8, a[k].classes[w] == WALK_ARGUMENT_SSE ? XMM0 + fp++ : x64_argreg(gp++), &s, false);
		}
	}
	free(place);
	free(bregs);
	if (variadic)
		x64_op3(e, X_MOVI, 4, RAX, XNOREG, XNOREG)->immediate = fp;	/* al: vector registers used */
	if (fn) {
		x64_op3(e, X_CALLR, 8, XNOREG, R11, XNOREG);
	} else {
		i = x64_put(e, X_CALL);
		i->reference = ref;
		i->condition = variadic;
	}
	if (savedsp) {
		codegen_lvalue slot = x64_fr(savedsp);

		x64_mem(e, X_LD, 8, RSP, &slot, false);
		X->pushes -= (int)((total + pad) / 8);
	} else if (total + pad) {
		x64_put(e, X_ADJSP)->immediate = total + pad;
		X->pushes -= (int)((total + pad) / 8);
	}
	if (r && r->is_x87) {
		codegen_lvalue d = x64_fr(r->slot);

		x64_x87st(e, &d);
		if (r->size == 32) {
			d.offset += 16;
			x64_x87st(e, &d);	/* _Complex long double: the imaginary part from st(1) */
		}
	} else if (r && r->is_in_memory && r->is_sse_up) {
		for (int w = 0; w < (int)(r->size / 16); w++) {
			codegen_lvalue d = x64_fr(r->slot + 16 * w);

			x64_mem(e, X_ST, 16, XMM0 + w, &d, false);
		}
	} else if (r && !r->is_in_memory && r->eightbyte_count) {
		/* a record in rax, rdx / xmm0, xmm1 (by class, in order) into its slot */
		int ng = 0, nf = 0;

		for (int w = 0; w < r->eightbyte_count; w++) {
			codegen_lvalue d = x64_fr(r->slot + 8 * w);

			x64_mem(e, X_ST, r->is_sse_up ? 16 : 8, r->classes[w] == WALK_ARGUMENT_SSE ? XMM0 + nf++ : (ng++ ? RDX : RAX), &d, false);
		}
	} else if (retsize && !retf && !r) {
		x64_mov(e, R11, RAX);
		x64_ext(e, R11, retsize, retsgn);
	}
	for (int q = XMM15 - 1; q >= XMM8; q--)
		if (fsaved & (1u << q)) {
			codegen_lvalue s = { .kind = CODEGEN_LVALUE_REGISTER, .register_number = RSP };

			x64_mem(e, X_LD, 8, q, &s, false);
			x64_put(e, X_ADJSP)->immediate = 8;
			X->pushes--;
		}
	for (int q = 15; q >= 0; q--)
		if (saved & (1u << q)) {
			x64_op3(e, X_POP, 8, q, XNOREG, XNOREG);
			X->pushes--;
		}
	if (fn && fn->kind == CODEGEN_VALUE_REGISTER)
		x64_tfree(e, fn->register_number);
	if (!retsize || r)
		return codegen_value_constant(0);
	if (retf) {
		int q = x64_ftreg(e);

		x64_mov(e, q, XMM0);
		return (codegen_value){ .kind = CODEGEN_VALUE_REGISTER, .register_number = (uint8_t)q };
	}
	{
		int q = x64_treg(e);

		x64_mov(e, q, R11);
		return (codegen_value){ .kind = CODEGEN_VALUE_REGISTER, .register_number = (uint8_t)q };
	}
}

/* ---------------- the incoming side ---------------- */

VPRIV void x64_params(codegen_context *e, walk_argument *a, int n, const walk_argument *r, bool variadic)
{
	xst *X = e->x64_context;
	cc *c = e->compile_context;
	const node *body = N(c->syms[e->walk_state->function_symbol].body);
	int gp = 0, fp = 0;
	int64_t stk = 0;

	const attrs *fa = codegen_symbol_attributes(e->compile_context, e->walk_state->function_symbol);
	X->interrupt = fa && fa->interrupt ? n : 0;
	X->sret = 0;
	if (X->interrupt) {
		/* The printer saves every GPR and establishes an aligned R12 frame
		   before these records execute; RBP retains the hardware frame. */
		if (e->walk_state->frame_alignment < 32) e->walk_state->frame_alignment = 32;
		codegen_lvalue frame = { .kind = CODEGEN_LVALUE_REGISTER, .register_number = RBP, .offset = n == 2 ? 16 : 8 };
		codegen_lvalue dst = x64_fr(a[0].slot);
		x64_mem(e, X_LEA, 8, R11, &frame, false);
		x64_mem(e, X_ST, 8, R11, &dst, false);
		if (n == 2) {
			frame.offset = 8;
			dst = x64_fr(a[1].slot);
			x64_mem(e, X_LD, 8, R11, &frame, false);
			x64_mem(e, X_ST, 8, R11, &dst, false);
		}
		return;
	}
	if (r && r->is_in_memory && !r->is_x87 && !r->is_sse_up) {
		codegen_lvalue d = x64_fr(r->slot);

		x64_mem(e, X_ST, 8, x64_argreg(gp++), &d, false);
		X->sret = r->slot;
	}
	for (int k = 0; k < n; k++) {
		int ng = x64_need(&a[k], WALK_ARGUMENT_INTEGER), nf = x64_need(&a[k], WALK_ARGUMENT_SSE);
		uint32_t sym = c->lists[body->a + (uint32_t)k];
		ast_qualified_type ty = c->syms[sym].ty;
		codegen_lvalue home;
		bool local = x64_local(e, sym, &home);

		if (a[k].bool_lane_count) {
			for (uint32_t bit = 0; bit < a[k].bool_lane_count; bit++) {
				if (!(bit % 8)) x64_movi(e, R10, 0);
				if (gp < 6) x64_mov(e, R11, x64_argreg(gp++));
				else {
					codegen_lvalue src = x64_fr(16 + stk);
					x64_mem(e, X_LD, 8, R11, &src, false);
					stk += 8;
				}
				x64_op3(e, X_ANDI, 8, R11, R11, XNOREG)->immediate = 1;
				if (bit % 8) x64_op3(e, X_SHLI, 8, R11, R11, XNOREG)->immediate = bit % 8;
				x64_op3(e, X_OR, 8, R10, R10, R11);
				if (bit % 8 == 7 || bit + 1 == a[k].bool_lane_count) {
					codegen_lvalue dst = x64_fr(a[k].slot + bit / 8);
					x64_mem(e, X_ST, 1, R10, &dst, false);
				}
			}
			continue;
		}
		if (!a[k].is_in_memory && a[k].eightbyte_count == 0)
			continue;
		if (!a[k].is_in_memory && gp + ng <= 6 && fp + nf <= 8) {
			if (local) {
				/* Selected homes are scalar integer/pointer words. Bind directly
				   without creating a stack copy; preserve the usual ABI count. */
				codegen_lvalue from = { .kind = CODEGEN_LVALUE_LOCAL_REGISTER, .register_number = (uint8_t)x64_argreg(gp++) };

				x64_load(e, home.register_number, &from, walk_type_size(c, ty), walk_type_is_signed(c, ty));
				continue;
			}
			for (int w = 0; w < a[k].eightbyte_count; w++) {
				codegen_lvalue d = x64_fr(a[k].slot + 8 * w);

				x64_mem(e, X_ST, a[k].is_sse_up ? 16 : 8, a[k].classes[w] == WALK_ARGUMENT_SSE ? XMM0 + fp++ : x64_argreg(gp++), &d, false);
			}
			continue;
		}
		/* on the stack: the parameter is where the caller put it */
		stk = (stk + a[k].align - 1) & ~(int64_t)(a[k].align - 1);
		/* Overaligned wide scalars arrive at ABI alignment eight, but need
		 * their declared alignment when addressed in the function body.
		 * R11 is not an argument register, so later arguments stay intact. */
		uint32_t home_align = codegen_symbol_alignment(c, sym);
		if (codegen_type_is_bitint_big(c, ty) && home_align > 8) {
			a[k].slot = walk_slot_temporary(e, a[k].size, home_align);
			for (uint64_t off = 0; off < a[k].size; off += 8) {
				codegen_lvalue src = x64_fr(16 + stk + (int64_t)off), dst = x64_fr(a[k].slot + (int64_t)off);
				x64_mem(e, X_LD, 8, R11, &src, false);
				x64_mem(e, X_ST, 8, R11, &dst, false);
			}
		} else {
			a[k].slot = 16 + stk;
			if (local) {
				codegen_lvalue from = x64_fr(a[k].slot);

				x64_load(e, home.register_number, &from, walk_type_size(c, ty), walk_type_is_signed(c, ty));
			}
		}
		stk += (int64_t)((a[k].size + 7) & ~(uint64_t)7);
	}
	X->ngp = gp, X->nfp = fp, X->stk = (int32_t)stk;
	X->save = 0;
	if (variadic) {
		/* the register save area: rdi..r9, then xmm0..xmm7 if al says vector registers were used */
		uint32_t skip = walk_label_new(e);
		record *j;

		X->save = walk_slot_permanent(e, 48 + 128, 16);
		for (int k = gp; k < 6; k++) {
			codegen_lvalue d = x64_fr(X->save + 8 * k);

			x64_mem(e, X_ST, 8, x64_argreg(k), &d, false);
		}
		if (x64_nosimd(e))
			return;	/* no vector registers to save (-mgeneral-regs-only, -mno-sse): as clang */
		x64_op3(e, X_TEST, 1, XNOREG, RAX, XNOREG);
		j = x64_put(e, X_JCC);
		j->condition = CC_E;
		j->label = skip;
		for (int k = fp; k < 8; k++) {
			codegen_lvalue d = x64_fr(X->save + 48 + 16 * k);

			x64_mem(e, X_ST, 16, XMM0 + k, &d, false);
		}
		x64_label(e, skip);
	}
}

/* a record result: its first words (from the object at raddr) in rax, rdx, or
   copied to the caller's object (the hidden pointer, also the result) */
VPRIV void x64_retrec(codegen_context *e, int raddr, const walk_argument *r, uint32_t lbl)
{
	if (r->is_x87) {
		codegen_lvalue s = { .kind = CODEGEN_LVALUE_REGISTER, .register_number = (uint8_t)raddr };

		if (r->size == 32) {
			codegen_lvalue im = s;

			im.offset = 16;
			x64_x87ld(e, &im);	/* _Complex long double: the imaginary part to st(1) */
		}
		x64_x87ld(e, &s);	/* long double: in st(0) */
	} else if (r->is_in_memory && r->is_sse_up) {
		for (int w = 0; w < (int)(r->size / 16); w++) {
			codegen_lvalue s = { .kind = CODEGEN_LVALUE_REGISTER, .register_number = (uint8_t)raddr, .offset = 16 * w };

			x64_mem(e, X_LD, 16, XMM0 + w, &s, false);
		}
	} else if (r->is_in_memory) {
		codegen_lvalue s = x64_fr(e->x64_context->sret);
		int rd = x64_treg(e);

		x64_mem(e, X_LD, 8, rd, &s, false);
		x64_copy(e, rd, raddr, r->size);
		x64_mov(e, RAX, rd);
		x64_tfree(e, rd);
	} else if (r->eightbyte_count) {
		/* the words by class: rax, rdx / xmm0, xmm1; a word for rax read last when raddr is rax */
		int ng = 0, nf = 0, dst[2];

		for (int w = 0; w < r->eightbyte_count; w++)
			dst[w] = r->classes[w] == WALK_ARGUMENT_SSE ? XMM0 + nf++ : (ng++ ? RDX : RAX);
		for (int pass = 0; pass < 2; pass++)
			for (int w = 0; w < r->eightbyte_count; w++)
				if ((dst[w] == raddr) == (pass == 1)) {
					codegen_lvalue s = { .kind = CODEGEN_LVALUE_REGISTER, .register_number = (uint8_t)raddr, .offset = 8 * w };

					x64_mem(e, X_LD, r->is_sse_up ? 16 : 8, dst[w], &s, false);
				}
	}
	x64_tfree(e, raddr);
	x64_jump(e, lbl);
}

/* ---------------- varargs ---------------- */

/* va_list at rap: gp_offset, fp_offset, overflow_arg_area, reg_save_area */
VPRIV void x64_va_start(codegen_context *e, int rap)
{
	xst *X = e->x64_context;
	codegen_lvalue gpo = { .kind = CODEGEN_LVALUE_REGISTER, .register_number = (uint8_t)rap, .offset = 0 }, fpo = { .kind = CODEGEN_LVALUE_REGISTER, .register_number = (uint8_t)rap, .offset = 4 };
	codegen_lvalue ov = { .kind = CODEGEN_LVALUE_REGISTER, .register_number = (uint8_t)rap, .offset = 8 }, rs = { .kind = CODEGEN_LVALUE_REGISTER, .register_number = (uint8_t)rap, .offset = 16 };
	codegen_lvalue sa = x64_fr(X->save), oa = x64_fr(16 + X->stk);

	if (!X->save)
		codegen_fatal_report(e, e->current_location, "va_start in a function that is not variadic");
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
VPRIV int x64_va_arg(codegen_context *e, int rap, ast_qualified_type t)
{
	walk_argument a;
	codegen_lvalue gpo = { .kind = CODEGEN_LVALUE_REGISTER, .register_number = (uint8_t)rap, .offset = 0 }, fpo = { .kind = CODEGEN_LVALUE_REGISTER, .register_number = (uint8_t)rap, .offset = 4 };
	codegen_lvalue ov = { .kind = CODEGEN_LVALUE_REGISTER, .register_number = (uint8_t)rap, .offset = 8 }, rsa = { .kind = CODEGEN_LVALUE_REGISTER, .register_number = (uint8_t)rap, .offset = 16 };
	int ra = x64_treg(e), ng, nf;
	uint32_t lstk = walk_label_new(e), ldone = walk_label_new(e);

	x64_classify(e, t, &a, true);
	if (a.bool_lane_count)
		codegen_fatal_report(e, e->current_location, "x86-64: variadic transport of 65..128-lane packed boolean vectors is not supported");
	ng = x64_need(&a, WALK_ARGUMENT_INTEGER), nf = x64_need(&a, WALK_ARGUMENT_SSE);
	if (!a.is_in_memory && a.eightbyte_count) {
		int rg = x64_treg(e), rf = x64_treg(e), rs = x64_treg(e);
		codegen_lvalue tmp = x64_fr(walk_slot_temporary(e, 16, 16));
		record *j;

		x64_mem(e, X_LD, 4, rg, &gpo, false);
		x64_mem(e, X_LD, 4, rf, &fpo, false);
		if (ng) {
			x64_op3(e, X_CMPI, 4, XNOREG, rg, XNOREG)->immediate = 48 - 8 * ng;
			j = x64_put(e, X_JCC);
			j->condition = CC_A;
			j->label = lstk;
		}
		if (nf) {
			x64_op3(e, X_CMPI, 4, XNOREG, rf, XNOREG)->immediate = 176 - 16 * nf;
			j = x64_put(e, X_JCC);
			j->condition = CC_A;
			j->label = lstk;
		}
		x64_mem(e, X_LEA, 8, ra, &tmp, false);
		x64_mem(e, X_LD, 8, rs, &rsa, false);
		for (int w = 0; w < a.eightbyte_count; w++) {
			int ro = a.classes[w] == WALK_ARGUMENT_SSE ? rf : rg;
			codegen_lvalue src = { .kind = CODEGEN_LVALUE_REGISTER, .register_number = R11 }, dst = { .kind = CODEGEN_LVALUE_REGISTER, .register_number = (uint8_t)ra, .offset = 8 * w };

			x64_mov(e, R11, rs);
			x64_op3(e, X_ADD, 8, R11, R11, ro);
			x64_mem(e, X_LD, 8, R11, &src, false);
			x64_mem(e, X_ST, 8, R11, &dst, false);
			if (a.is_sse_up) {	/* a 16-byte vector: the whole register's save slot */
				x64_mov(e, R11, rs);
				x64_op3(e, X_ADD, 8, R11, R11, ro);
				src.offset = 8, dst.offset += 8;
				x64_mem(e, X_LD, 8, R11, &src, false);
				x64_mem(e, X_ST, 8, R11, &dst, false);
			}
			x64_op3(e, X_ADDI, 4, ro, ro, XNOREG)->immediate = a.classes[w] == WALK_ARGUMENT_SSE ? 16 : 8;
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
		x64_op3(e, X_ADDI, 8, ra, ra, XNOREG)->immediate = (int64_t)a.align - 1;
		x64_op3(e, X_ANDI, 8, ra, ra, XNOREG)->immediate = -(int64_t)a.align;
	}
	{
		codegen_lvalue nx = { .kind = CODEGEN_LVALUE_REGISTER, .register_number = (uint8_t)ra, .offset = (int64_t)((a.size + 7) & ~(uint64_t)7) };

		x64_mem(e, X_LEA, 8, R11, &nx, false);
		x64_mem(e, X_ST, 8, R11, &ov, false);
	}
	x64_label(e, ldone);
	return ra;
}

VPRIV void x64_ret(codegen_context *e, codegen_value *v, ast_qualified_type t, uint32_t lbl)
{
	cc *c = e->compile_context;
	int size = walk_type_size(c, t);
	bool sgn = walk_type_is_signed(c, t);

	if (v && codegen_type_is_float(c, t)) {
		x64_mov(e, XMM0, v->register_number);
		x64_tfree(e, v->register_number);
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
