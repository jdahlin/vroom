/* vcc code generator: AAPCS64 classification, spilling around calls */
#include "../../../codegen/codegen-internal.h"

/* ---------------- AAPCS64 (floating point and HFAs in v0-v7) ---------------- */

/*
 * A homogeneous floating-point or short-vector aggregate (HFA/HVA): a
 * record (nested records and arrays included) whose leaves are 1-4 of
 * the same floating type, or 1-4 short vectors (8 or 16 bytes) of the same
 * size, with no padding; a short vector alone counts as one.  Returns the
 * member size (4, 8 or 16) and sets *n, or 0.  In *base a vector size
 * carries HV, so floats and vectors never mix.
 */
enum { HV = 0x100 };

static int hfa_walk(cc *c, ast_qualified_type t, int *base, uint64_t *n)
{
	const ctype *y = T(c, t);

	if (y->kind == AST_TYPE_VECTOR) {
		int b = (int)y->size | HV;

		if ((y->size != 8 && y->size != 16) || (*base && *base != b))
			return 0;
		*base = b;
		*n = 1;
		return 1;
	}
	if (y->kind == AST_TYPE_BFLOAT16 || y->kind == AST_TYPE_FLOAT16 || y->kind == AST_TYPE_FLOAT || y->kind == AST_TYPE_DOUBLE || y->kind == AST_TYPE_LONG_DOUBLE) {	/* Apple: long double is 8 bytes */
		int b = (int)y->size;

		if (*base && *base != b)
			return 0;
		*base = b;
		*n = 1;
		return 1;
	}
	if (y->kind == AST_TYPE_ARRAY) {
		uint64_t m;

		if (y->flags & (AST_TYPE_FLAG_VLA | AST_TYPE_FLAG_INCOMPLETE) || !hfa_walk(c, y->base, base, &m))
			return 0;
		*n = m * y->n;
		return y->n > 0;
	}
	if (y->kind == AST_TYPE_STRUCT || y->kind == AST_TYPE_UNION) {
		const tag *g = &c->tags[y->aux];
		uint64_t tot = 0;

		for (uint32_t k = 0; k < g->nfield; k++) {
			uint64_t m;

			/* Empty fields and zero-width bit-fields contribute no
			   homogeneous members. Any padding they introduce is rejected
			   by the size check below. */
			if ((g->f[k].bitfield && !g->f[k].width) ||
			    (!g->f[k].bitfield && codegen_type_size(c, g->f[k].ty) == 0))
				continue;
			if (g->f[k].bitfield || !hfa_walk(c, g->f[k].ty, base, &m))
				return 0;
			if (y->kind == AST_TYPE_UNION)
				tot = m > tot ? m : tot;
			else
				tot += m;
		}
		*n = tot;
		return tot > 0 && *base && tot * (uint64_t)(*base & ~HV) == g->size;
	}
	return 0;
}

VPRIV int hfa(cc *c, ast_qualified_type t, int *n)
{
	int base = 0;
	uint64_t m = 0;

	if (codegen_type_is_bool_vector(c, t) && (T(c, t)->n == 64 || T(c, t)->n == 128)) return 0;
	if (!codegen_type_is_record_like(c, t) || !hfa_walk(c, t, &base, &m) || m > 4)
		return 0;
	*n = (int)m;
	return base & ~HV;
}

/*
 * A vector under 8 bytes is returned in v0 the way LLVM legalizes it
 * (clang; AAPCS64 says nothing): with more than one lane, each lane is
 * widened to 8/n bytes (v4qi as 4h, v2hi and v2qi as 2s), one lane as it
 * is.  As an argument it goes in a GPR as its bytes (classify).
 */
VPRIV bool vec_small(cc *c, ast_qualified_type t) { return codegen_type_is_vector(c, t) && !codegen_type_is_bool_vector(c, t) && codegen_type_size(c, t) < 8; }

/* the vector at [rs] into v0 */
VPRIV void vec_small_ret(codegen_context *e, ast_qualified_type t, int rs)
{
	cc *c = e->compile_context;
	uint32_t n = T(c, t)->n, size = (uint32_t)codegen_type_size(c, t), es = n ? size / n : size, w = n > 1 ? 8 / n : 0;
	int acc = treg(e), x = treg(e);

	if (n <= 1) {
		mem(e, I_LD, log2sz(size), 0, acc, rs, 0);
		word(e, f_in(0, 0, acc));	/* fmov s0, w */
	} else {
		movi(e, 1, acc, 0);
		for (uint32_t k = 0; k < n; k++) {
			mem(e, I_LD, log2sz(es), 0, x, rs, (int32_t)(k * es));
			op3s(e, I_ORR, 1, acc, acc, x, 0, (int)(8 * w * k));
		}
		word(e, f_in(1, 0, acc));	/* fmov d0, x */
	}
	tfree(e, x);
	tfree(e, acc);
}

/* v0 (a call's result) into the slot at sl */
VPRIV void vec_small_res(codegen_context *e, ast_qualified_type t, int64_t sl)
{
	cc *c = e->compile_context;
	uint32_t n = T(c, t)->n, size = (uint32_t)codegen_type_size(c, t), es = n ? size / n : size, w = n > 1 ? 8 / n : 0;
	int acc = treg(e), x = treg(e);

	if (n <= 1) {
		word(e, f_out(0, acc, 0));	/* fmov w, s0 */
		mem(e, I_ST, log2sz(size), 0, acc, FRAME, sl);
	} else {
		word(e, f_out(1, acc, 0));	/* fmov x, d0 */
		for (uint32_t k = 0; k < n; k++) {
			opbf(e, I_UBFM, 1, x, acc, (int)(8 * w * k), (int)(8 * w * k + 8 * es - 1));
			mem(e, I_ST, log2sz(es), 0, x, FRAME, sl + (int32_t)(k * es));
		}
	}
	tfree(e, x);
	tfree(e, acc);
}

/* LLVM legalizes small boolean-vector results independently of packed storage.
 * Power-of-two lane counts 2/4/8/16 use one SIMD register; other counts
 * through eight use one integer register per lane. Larger results use x8. */
VPRIV void bvec_ret(codegen_context *e, ast_qualified_type t, int rs)
{
	unsigned n = T(e->compile_context, t)->n;
	bool simd = n == 2 || n == 4 || n == 8 || n == 16;
	unsigned stride = n == 16 ? 1 : simd ? 8 / n : 0;
	int64_t sl = simd ? slot(e, 16, 16) : 0;
	int r = treg(e);
	for (unsigned i = 0; i < n; i++) {
		mem(e, I_LD, 0, 0, r, rs, i / 8);
		opbf(e, I_UBFM, 1, r, r, i % 8, i % 8);
		if (simd) mem(e, I_ST, log2sz(stride), 0, r, FRAME, sl + i * stride);
		else mov(e, 0, i, r);
	}
	if (simd) vr_load(e, n == 16 ? 16 : 8, 0, FRAME, sl);
	tfree(e, r);
}

VPRIV void bvec_res(codegen_context *e, ast_qualified_type t, int64_t sl)
{
	unsigned n = T(e->compile_context, t)->n;
	bool simd = n == 2 || n == 4 || n == 8 || n == 16;
	unsigned stride = n == 16 ? 1 : simd ? 8 / n : 0;
	int64_t tmp = simd ? slot(e, 16, 16) : 0;
	int r = treg(e), acc = treg(e);
	if (simd) vr_store(e, n == 16 ? 16 : 8, 0, FRAME, tmp);
	movi(e, 1, acc, 0);
	for (unsigned i = 0; i < n; i++) {
		if (simd) mem(e, I_LD, log2sz(stride), 0, r, FRAME, tmp + i * stride);
		else mov(e, 0, r, i);
		opbf(e, I_UBFM, 1, r, r, 0, 0);
		op3s(e, I_ORR, 1, acc, acc, r, 0, i);
	}
	mem(e, I_ST, n > 8 ? 1 : 0, 0, acc, FRAME, sl);
	tfree(e, acc);
	tfree(e, r);
}

/* a record result returned through x8 */
VPRIV bool is_sret(cc *c, ast_qualified_type t)
{
	int n;

	if (codegen_type_is_bool_vector(c, t)) return T(c, t)->n > 8 && T(c, t)->n != 16;
	return codegen_type_is_record_like(c, t) && codegen_type_size(c, t) > 16 && !hfa(c, t, &n);
}

/* Apple: an anonymous (variadic) argument always goes on the stack in 8-byte slots */
VPRIV void classify_va(codegen_context *e, ast_qualified_type t, int *nsaa, a64_abi_argument *a)
{
	cc *c = e->compile_context;
	uint64_t size = codegen_type_size(c, t);
	uint32_t al = codegen_type_alignment(c, t) > 8 ? 16 : 8;

	memset(a, 0, sizeof(*a));
	a->size = (uint32_t)size;
	if (codegen_type_is_record(c, t) && size > 16) {
		a->kind = A64_ABI_ARGUMENT_REFERENCE_STACK;
		a->stack_offset = *nsaa;
		*nsaa += 8;
		return;
	}
	*nsaa = (int)(((uint32_t)*nsaa + al - 1) & ~(al - 1));
	a->kind = A64_ABI_ARGUMENT_STACK;
	a->stack_offset = *nsaa;
	a->register_count = (uint8_t)(size > 8 ? 2 : 1);
	*nsaa += a->register_count * 8;
}

/* the alignment AAPCS64 passes t by: clang's unadjusted alignment (a
 * record's fields without its own aligned(), a scalar's without a typedef's);
 * Apple's ABI takes the type's alignment */
VPRIV uint32_t abi_align(cc *c, ast_qualified_type t)
{
	const ctype *ct = T(c, t);

	if (CODEGEN_IS_DARWIN(c))
		return codegen_type_alignment(c, t);
	if (codegen_type_is_record(c, t))
		return c->tags[ct->aux].ualign;
	return c->ty[ct->canon].align;
}

VPRIV void classify(codegen_context *e, ast_qualified_type t, int *ngrn, int *nsaa, a64_abi_argument *a)
{
	cc *c = e->compile_context;
	uint64_t size = codegen_type_size(c, t);
	int n;
	uint32_t al = abi_align(c, t);

	memset(a, 0, sizeof(*a));
	a->size = (uint32_t)size;
	if (codegen_type_is_float(c, t)) {
		if (!codegen_type_is_long_double(c, t))
			(void)fpk(e, t);
		if (e->next_vector_register < 8) {
			a->kind = A64_ABI_ARGUMENT_VECTOR_REGISTER;
			a->register_number = (uint8_t)e->next_vector_register++;
			a->register_count = 1;
			return;
		}
		e->next_vector_register = 8;
		n = size == 16 ? 2 : 1;
		goto stack;
	}
	if (codegen_type_is_bool_vector(c, t) && (T(c, t)->n == 64 || T(c, t)->n == 128)) {
		a->kind = A64_ABI_ARGUMENT_BOOL_LANES;
		a->register_number = (uint8_t)*ngrn;
		a->register_count = (uint8_t)(8 - *ngrn);
		a->stack_offset = *nsaa;
		*nsaa += (int)(T(c, t)->n - a->register_count) * 8;
		*ngrn = 8;
		return;
	}
	if (codegen_type_is_bool_vector(c, t) && size >= 8 && size <= 16) {
		if (e->next_vector_register < 8) {
			a->kind = A64_ABI_ARGUMENT_HFA, a->register_number = e->next_vector_register++, a->register_count = 1, a->float_member_size = size;
			return;
		}
		n = size / 8;
		goto stack;
	}
	if (codegen_type_is_record_like(c, t)) {
		int hn, hb = hfa(c, t, &hn);

		if (hb) {
			if (e->next_vector_register + hn <= 8) {
				a->kind = A64_ABI_ARGUMENT_HFA;
				a->register_number = (uint8_t)e->next_vector_register;
				a->register_count = (uint8_t)hn;
				a->float_member_size = (uint8_t)hb;
				e->next_vector_register += hn;
				return;
			}
			e->next_vector_register = 8;
			if (CODEGEN_IS_DARWIN(c)) {
				/* Apple: on the stack at its element's alignment and size, as clang */
				*nsaa = (int)(((uint32_t)*nsaa + (uint32_t)hb - 1) & ~((uint32_t)hb - 1));
				a->kind = A64_ABI_ARGUMENT_STACK;
				a->stack_offset = *nsaa;
				a->register_count = (uint8_t)((size + 7) / 8);
				a->float_member_size = (uint8_t)hb;	/* the last part may be 4 bytes */
				*nsaa += (int)size;
				return;
			}
			n = (int)((size + 7) / 8);
			goto stack;
		}
		if (size == 0) {
			a->kind = A64_ABI_ARGUMENT_IGNORE;
			return;
		}
		if (size > 16) {
			if (*ngrn < 8) {
				a->kind = A64_ABI_ARGUMENT_REFERENCE_REGISTER;
				a->register_number = (uint8_t)(*ngrn)++;
				a->register_count = 1;
			} else {
				a->kind = A64_ABI_ARGUMENT_REFERENCE_STACK;
				a->stack_offset = *nsaa;
				*nsaa += 8;
			}
			return;
		}
		n = (int)((size + 7) / 8);
	} else {
		n = size == 16 ? 2 : 1;
	}
	if (n == 2 && al == 16 && (*ngrn & 1) && !CODEGEN_IS_DARWIN(c))
		(*ngrn)++;	/* AAPCS64: an even pair; Apple does not round */
	if (*ngrn + n <= 8) {
		a->kind = A64_ABI_ARGUMENT_REGISTER;
		a->register_number = (uint8_t)*ngrn;
		a->register_count = (uint8_t)n;
		*ngrn += n;
		return;
	}
	*ngrn = 8;
stack:
	if (CODEGEN_IS_DARWIN(c) && !codegen_type_is_record(c, t)) {
		/* Apple: scalars on the stack take their natural size and alignment */
		*nsaa = (int)(((uint32_t)*nsaa + al - 1) & ~(al - 1));
		a->kind = A64_ABI_ARGUMENT_STACK;
		a->stack_offset = *nsaa;
		a->register_count = (uint8_t)n;
		a->size = (uint32_t)size;
		*nsaa += (int)size;
		return;
	}
	if (al < 8)
		al = 8;
	if (al > 16)
		al = 16;
	*nsaa = (int)(((uint32_t)*nsaa + al - 1) & ~(al - 1));
	a->kind = A64_ABI_ARGUMENT_STACK;
	a->stack_offset = *nsaa;
	a->register_count = (uint8_t)n;
	*nsaa += n * 8;
}

VPRIV void spill_live(codegen_context *e, a64_abi_spill *s)
{
	s->mask = 0;
	for (int i = 0; i < CODEGEN_TEMPORARY_COUNT; i++)
		if ((e->temporaries_in_use & (1u << i)) && codegen_temporary_registers[i] < 19) {
			s->slots[i] = slot(e, 8, 8);
			mem(e, I_ST, 3, 0, codegen_temporary_registers[i], FRAME, s->slots[i]);
			if (e->is_reachable)
				e->records[e->record_count - 1].sh = A64_SPILL_RECORD;
			s->mask |= 1u << i;
		}
}

VPRIV void unspill(codegen_context *e, const a64_abi_spill *s)
{
	for (int i = 0; i < CODEGEN_TEMPORARY_COUNT; i++)
		if (s->mask & (1u << i)) {
			mem(e, I_LD, 3, 0, codegen_temporary_registers[i], FRAME, s->slots[i]);
			if (e->is_reachable)
				e->records[e->record_count - 1].sh = A64_SPILL_RECORD;
		}
}

/* memcpy(a0, a1, n) / memset(a0, 0, n) with a0/a1 in temporaries */
VPRIV void call_fixed(codegen_context *e, int fn, int a0, int a1, int a2, int64_t n)
{
	a64_abi_spill sp;
	ins *i;

	(void)a2;
	if (!e->is_reachable)
		return;
	e->has_calls = true;
	spill_live(e, &sp);
	mov(e, 1, 0, a0);
	if (a1 == -2)
		movi(e, 0, 1, 0);
	else
		mov(e, 1, 1, a1);
	movi(e, 1, 2, n);
	i = put(e, I_BL);
	i->ref = CODEGEN_FIXED_REFERENCE(fn);
	unspill(e, &sp);
}
