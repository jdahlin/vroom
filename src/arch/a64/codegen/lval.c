/* vcc code generator: lvalues from expressions, typed loads and stores */
#include "../../../codegen/codegen-internal.h"

/* ---------------- lvalues from expressions ---------------- */

VPRIV uint32_t align_at(uint64_t off, uint32_t a)
{
	while (a > 1 && (off & (a - 1)))
		a >>= 1;
	return a ? a : 1;
}

VPRIV uint64_t elem_size(cc *c, ast_qualified_type ptr)
{
	ast_qualified_type b = T(c, ptr)->base;
	int k = KIND(c, b);

	if (k == AST_TYPE_VOID || k == AST_TYPE_FUNCTION)
		return 1;
	return codegen_type_size(c, b);
}

/* p + i * scale, as a value */
VPRIV codegen_value ptr_add(codegen_context *e, codegen_value p, codegen_value i, int64_t scale, bool sub)
{
	if (i.k == CODEGEN_VALUE_CONSTANT) {
		i128 d = i.i * scale;

		if (sub)
			d = -d;
		if (p.k == CODEGEN_VALUE_CONSTANT || p.k == CODEGEN_VALUE_ADDRESS) {
			p.i = p.k == CODEGEN_VALUE_CONSTANT ? (i128)(uint64_t)(p.i + d) : p.i + d;
			return p;
		}
		{
			int r = toreg(e, &p);

			if (d)
				opi(e, I_ADDI, 1, r, r, (int64_t)d);
			return p;
		}
	}
	{
		int rp = toreg(e, &p), ri = toreg(e, &i);

		if (scale > 0 && !(scale & (scale - 1))) {
			op3s(e, sub ? I_SUB : I_ADD, 1, rp, rp, ri, 0, __builtin_ctzll((uint64_t)scale));
		} else {
			movi(e, 1, R_X16, scale);
			op4(e, I_MADD, 1, ri, ri, R_X16, ZR);
			op3(e, sub ? I_SUB : I_ADD, 1, rp, rp, ri);
		}
		tfree(e, ri);
		return p;
	}
}

/* p + i elements of a pointer to a VLA: the element size is computed now */
VPRIV codegen_value ptr_add_vla(codegen_context *e, codegen_value p, codegen_value i, ast_qualified_type ptr, bool sub)
{
	codegen_value b = vla_bytes(e, e->c->ty[AST_QUALIFIED_TYPE_INDEX(ptr)].base);
	int ri = toreg(e, &i), rb = toreg(e, &b);

	op4(e, I_MADD, 1, ri, ri, rb, ZR);
	val_free(e, &b);
	return ptr_add(e, p, i, 1, sub);
}

VPRIV codegen_lvalue gen_lval(codegen_context *e, uint32_t n)
{
	cc *c = e->c;
	const node *x = N(n);
	codegen_lvalue l = { 0 };

	switch (x->op) {
	case AST_NODE_VARIABLE: {
		uint32_t s = x->a, cs;

		if (codegen_symbol_is_local_variable(c, s))
			return local_lval(e, s);
		cs = codegen_symbol_canonical(c, s);
		if (c->syms[cs].sc == AST_STORAGE_REGISTER && c->syms[cs].depth == 0) {
			int r = regvar_reg(c, cs);

			if (r < 0)
				codegen_fatal_report(e, x->loc, "unsupported global register variable");
			l.k = CODEGEN_LVALUE_GLOBAL_REGISTER;
			l.r = (uint8_t)r;
			return l;
		}
		if (!WASM(c) && ((c->syms[cs].flags | c->syms[s].flags) & AST_SYMBOL_FLAG_THREAD)) {
			/*
			 * x0 = the address.  Darwin: the TLV descriptor's thunk.  ELF as
			 * clang: -fPIC TLSDESC (a call), else local-exec for a symbol
			 * defined here and initial-exec for the others.
			 */
			int r, model = 0;

			if (!CODEGEN_IS_DARWIN(c)) {
				/* clang: tls_model(), else -ftls-model=; the default if that is stronger */
				const attrs *ta = codegen_symbol_attributes(c, cs);
				int want = ta && ta->tlsmodel ? ta->tlsmodel : c->o->tlsmodel, def;

				def = c->o->pic == 2 ? CC_TLS_GLOBAL_DYNAMIC :
				      (c->syms[cs].flags & (AST_SYMBOL_FLAG_DEFINED | AST_SYMBOL_FLAG_TENTATIVE)) ? CC_TLS_LOCAL_EXEC : CC_TLS_INITIAL_EXEC;
				if (want > def)
					def = want;
				model = def == CC_TLS_LOCAL_EXEC ? TLS_LE : def == CC_TLS_INITIAL_EXEC ? TLS_IE : TLS_DESC;
			}
			codegen_reference_note(e, CODEGEN_REFERENCE_MAKE(CODEGEN_REFERENCE_SYMBOL, cs));
			if (model == 0 || model == TLS_DESC)
				e->calls = true;
			if (e->reach) {
				ins *t = put(e, I_TLV);

				t->ref = CODEGEN_REFERENCE_MAKE(CODEGEN_REFERENCE_SYMBOL, cs);
				t->cond = (uint8_t)model;
			}
			r = treg(e);
			mov(e, 1, r, 0);
			l.k = CODEGEN_LVALUE_REGISTER;
			l.r = (uint8_t)r;
			l.align = codegen_symbol_alignment(c, cs);
			return l;
		}
		l.k = CODEGEN_LVALUE_SYMBOL;
		l.ref = CODEGEN_REFERENCE_MAKE(CODEGEN_REFERENCE_SYMBOL, cs);
		l.align = codegen_symbol_alignment(c, cs);
		return l;
	}
	case AST_NODE_STRING:
		l.k = CODEGEN_LVALUE_SYMBOL;
		l.ref = CODEGEN_REFERENCE_MAKE(CODEGEN_REFERENCE_STRING, x->a);
		l.align = c->strs[x->a].esz;
		return l;
	case AST_NODE_DEREFERENCE: {
		codegen_value p = gen(e, x->a);
		ast_qualified_type bt = T(c, N(x->a)->ty)->base;

		return lfromval(e, p, KIND(c, bt) == AST_TYPE_VOID ? 1 : codegen_type_alignment(c, bt));
	}
	case AST_NODE_MEMBER: {
		const field *f = &c->tags[x->b].f[x->c];
		const node *b = N(x->a);

		if (b->flags & AST_NODE_FLAG_LVALUE || b->op == AST_NODE_VARIABLE || b->op == AST_NODE_DEREFERENCE || b->op == AST_NODE_MEMBER || b->op == AST_NODE_INDEX ||
		    b->op == AST_NODE_COMPOUND_LITERAL || b->op == AST_NODE_STRING)
			l = gen_lval(e, x->a);
		else
			l = lfromval(e, gen(e, x->a), codegen_type_alignment(c, b->ty));
		l.off += (int64_t)(f->off / 8);
		l.align = align_at((uint64_t)l.off, l.align ? l.align : 1);
		if (l.k != CODEGEN_LVALUE_SYMBOL && l.k != CODEGEN_LVALUE_FRAME)
			l.align = align_at(f->off / 8, codegen_type_alignment(c, f->ty) < l.align ? codegen_type_alignment(c, f->ty) : l.align);
		if (f->bitfield) {
			l.bf = 1;
			l.bitoff = (uint16_t)(f->off % 8);
			l.width = f->width;
			l.sgn = codegen_type_is_signed(c, f->ty);
		}
		return l;
	}
	case AST_NODE_INDEX: {
		codegen_value p = gen(e, x->a), i = gen(e, x->b);
		uint64_t es = elem_size(c, N(x->a)->ty);
		ast_qualified_type et = T(c, N(x->a)->ty)->base;

		if (x->aux == 1) {
			if (i.k == CODEGEN_VALUE_CONSTANT) {
				l = lfromval(e, p, 1);
				l.off += (int64_t)(i.i / 8);
				l.bf = 1, l.width = 1, l.bitoff = (uint16_t)(i.i & 7);
				return l;
			}
			int ri = toreg(e, &i), rb = treg(e);
			mov(e, 1, rb, ri);
			codegen_value bit = arith(e, LEX_TOKEN_AMPERSAND, codegen_type_primitive(AST_TYPE_UNSIGNED_LONG), codegen_value_register(rb), codegen_value_constant(7));
			codegen_lvalue bs = { .k = CODEGEN_LVALUE_FRAME, .off = slot(e, 8, 8), .align = 8 };
			store(e, &bs, codegen_type_primitive(AST_TYPE_UNSIGNED_LONG), &bit);
			val_free(e, &bit);
			i = arith(e, LEX_TOKEN_GREATER_GREATER, codegen_type_primitive(AST_TYPE_UNSIGNED_LONG), codegen_value_register(ri), codegen_value_constant(3));
			l = lfromval(e, ptr_add(e, p, i, 1, false), 1);
			l.bf = 1, l.width = 1, l.dynbit_slot = bs.off;
			return l;
		}

		if (codegen_type_is_pointer_to_vla(c, N(x->a)->ty))
			return lfromval(e, ptr_add_vla(e, p, i, N(x->a)->ty, false), codegen_type_alignment(c, et));
		if (i.k == CODEGEN_VALUE_CONSTANT && (p.k == CODEGEN_VALUE_ADDRESS)) {
			l.k = CODEGEN_LVALUE_SYMBOL;
			l.ref = p.ref;
			l.off = (int64_t)(p.i + i.i * (i128)es);
			l.align = codegen_type_alignment(c, et);
			return l;
		}
		if (i.k == CODEGEN_VALUE_CONSTANT && p.k == CODEGEN_VALUE_REGISTER) {
			l.k = CODEGEN_LVALUE_REGISTER;
			l.r = p.r;
			l.off = (int64_t)(i.i * (i128)es);
			l.align = codegen_type_alignment(c, et);
			return l;
		}
		p = ptr_add(e, p, i, (int64_t)es, false);
		return lfromval(e, p, codegen_type_alignment(c, et));
	}
	case AST_NODE_COMPOUND_LITERAL: {
		uint32_t s = x->b;
		const sym *y = &c->syms[s];

		if (y->sc == AST_STORAGE_STATIC || (y->flags & AST_SYMBOL_FLAG_GLOBAL)) {
			l.k = CODEGEN_LVALUE_SYMBOL;
			l.ref = CODEGEN_REFERENCE_MAKE(CODEGEN_REFERENCE_SYMBOL, s);
			l.align = codegen_symbol_alignment(c, s);
			return l;
		}
		{
			int64_t sl = bind_slot(e, s);

			/* it lives until the end of the block, past the statement's temporaries */
			if (e->foff > e->cl_top)
				e->cl_top = e->foff;
			codegen_lvalue d = { .k = CODEGEN_LVALUE_FRAME, .off = sl, .align = codegen_symbol_alignment(c, s) };

			gen_init_local(e, &d, y->ty, x->a);
			return d;
		}
	}
	case AST_NODE_CAST:
		if (x->aux == AST_CAST_NOP && (N(x->a)->flags & AST_NODE_FLAG_LVALUE))
			return gen_lval(e, x->a);
		__attribute__((fallthrough));
	default: {
		codegen_value v = gen(e, n);

		if (!codegen_type_is_aggregate(c, x->ty) && !codegen_type_is_function(c, x->ty))
			codegen_fatal_report(e, x->loc, "not an lvalue (node %d)", x->op);
		return lfromval(e, v, codegen_type_alignment(c, x->ty));
	}
	}
}
static int bf_bytes(int bits) { return bits <= 8 ? 1 : bits <= 16 ? 2 : bits <= 32 ? 4 : 8; }

/*
 * _Atomic objects (AST_TYPE_FLAG_ATOMIC) in memory: every access is sequentially
 * consistent through LSE, as clang does for armv8.1-a: ldar, stlr, and
 * casal loops for read-modify-write (expr.c atomic_rmw).
 */
VPRIV bool is_atomic_lv(codegen_context *e, const codegen_lvalue *l, ast_qualified_type t)
{
	return unlikely(T(e->c, t)->flags & AST_TYPE_FLAG_ATOMIC) && l->k != CODEGEN_LVALUE_GLOBAL_REGISTER && !l->bf;
}

/* log2 of an atomic access's size; fatal for what has no single instruction */
VPRIV int atomic_sz(codegen_context *e, ast_qualified_type t)
{
	cc *c = e->c;
	uint64_t n = codegen_type_size(c, t);

	if (n != 1 && n != 2 && n != 4 && n != 8)
		codegen_fatal_report(e, 0, "%u-byte _Atomic objects are not supported", (unsigned)n);
	return log2sz(n);
}

/* the exact address of l in a register; *own: the caller frees it */
VPRIV int atomic_addr(codegen_context *e, codegen_lvalue *l, bool *own)
{
	codegen_value a;

	if (l->k == CODEGEN_LVALUE_REGISTER) {
		if (l->off)
			opi(e, I_ADDI, 1, l->r, l->r, l->off);
		l->off = 0;
		*own = false;
		return l->r;
	}
	a = laddr(e, l);
	*own = true;
	return toreg(e, &a);
}

static codegen_value ldar_load(codegen_context *e, codegen_lvalue *l, ast_qualified_type t)
{
	cc *c = e->c;
	int sz = atomic_sz(e, t), r, ra;
	bool own;

	if (codegen_type_is_float(c, t))
		fpk(e, t);
	ra = atomic_addr(e, l, &own);
	r = treg(e);
	word(e, (unsigned)sz << 30 | 0x08dffc00u | (unsigned)ra << 5 | (unsigned)r);	/* ldar */
	if (own)
		tfree(e, ra);
	if (codegen_type_is_aggregate(c, t)) {
		/* Aggregate values are addresses. Materialize one atomic snapshot,
		 * rather than returning the address of mutable atomic storage. */
		codegen_lvalue tmp = { .k = CODEGEN_LVALUE_FRAME, .off = slot(e, 1u << sz, 1u << sz), .align = 1u << sz };
		store_n(e, r, &tmp, 0, 1u << sz);
		tfree(e, r);
		return laddr(e, &tmp);
	}
	if (sz < 3 && !codegen_type_is_float(c, t) && codegen_type_is_signed(c, t))
		extend(e, r, r, 1 << sz, true);
	return codegen_value_register(r);
}

static void stlr_store(codegen_context *e, codegen_lvalue *l, ast_qualified_type t, codegen_value *v)
{
	cc *c = e->c;
	int sz = atomic_sz(e, t), r, ra;
	bool own;
	bool aggregate = codegen_type_is_aggregate(c, t);

	if (aggregate) {
		ast_qualified_type plain = AST_QUALIFIED_TYPE(T(c, t)->n, 0);
		uint64_t bytes = codegen_type_size(c, plain);
		codegen_lvalue src = { .k = CODEGEN_LVALUE_REGISTER, .r = toreg(e, v), .align = codegen_type_alignment(c, plain) };
		if (bytes != (1u << sz)) {
			/* Atomic layout may round a three-byte record up to four bytes.
			 * Do not read the rounded padding past the source object. */
			codegen_lvalue tmp = { .k = CODEGEN_LVALUE_FRAME, .off = slot(e, 1u << sz, 1u << sz), .align = 1u << sz };
			store_n(e, ZR, &tmp, 0, 1u << sz);
			codegen_value addr = laddr(e, &tmp);
			copy_mem(e, toreg(e, &addr), src.r, bytes);
			val_free(e, &addr);
			src = tmp;
		}
		r = treg(e);
		load_n(e, r, &src, 1u << sz, 0);
	} else {
		r = v->k == CODEGEN_VALUE_CONSTANT && v->i == 0 ? ZR : toreg(e, v);
	}
	ra = atomic_addr(e, l, &own);
	word(e, (unsigned)sz << 30 | 0x089ffc00u | (unsigned)ra << 5 | (unsigned)r); /* stlr */
	if (own)
		tfree(e, ra);
	if (aggregate)
		tfree(e, r);
}


/* Read/write one word of a wide bit-field. Use only bytes covered by the
   field, including packed fields whose final byte is at the object's end. */
static int bf_piece(int pos, int width)
{
	int bytes = (pos + width + 7) / 8;
	return bytes >= 8 ? 8 : bytes >= 4 ? 4 : bytes >= 2 ? 2 : 1;
}

static int bf_read_word(codegen_context *e, const codegen_lvalue *l, int start, int width)
{
	codegen_lvalue u = *l;
	int pos = (l->bitoff + start) % 8, done = 0;
	int out = treg(e), part = treg(e);
	u.off += (l->bitoff + start) / 8;
	movi(e, 1, out, 0);
	while (width) {
		int size = bf_piece(pos, width), bits = size * 8 - pos;
		if (bits > width) bits = width;
		load_n(e, part, &u, size, 0);
		if (pos) opbf(e, I_UBFM, 1, part, part, pos, 63);
		opbf(e, I_BFM, 1, out, part, (64 - done) & 63, bits - 1);
		done += bits, width -= bits, pos = 0, u.off += size;
	}
	tfree(e, part);
	return out;
}

static void bf_write_word(codegen_context *e, const codegen_lvalue *l, int start, int width, int value)
{
	codegen_lvalue u = *l;
	int pos = (l->bitoff + start) % 8, done = 0;
	int old = treg(e), part = treg(e);
	u.off += (l->bitoff + start) / 8;
	while (width) {
		int size = bf_piece(pos, width), bits = size * 8 - pos;
		if (bits > width) bits = width;
		if (done) shift_imm(e, I_LSRV, 1, part, value, done);
		else mov(e, 1, part, value);
		if (bits == size * 8) {
			store_n(e, part, &u, 0, size);
		} else {
			load_n(e, old, &u, size, 0);
			opbf(e, I_BFM, 1, old, part, (64 - pos) & 63, bits - 1);
			store_n(e, old, &u, 0, size);
		}
		done += bits, width -= bits, pos = 0, u.off += size;
	}
	tfree(e, part);
	tfree(e, old);
}

static void bf_extend_wide(codegen_context *e, const codegen_lvalue *l, codegen_value *v)
{
	if (l->width > 64) {
		opbf(e, l->sgn ? I_SBFM : I_UBFM, 1, v->r2, v->r2, 0, l->width - 65);
	} else {
		opbf(e, l->sgn ? I_SBFM : I_UBFM, 1, v->r, v->r, 0, l->width - 1);
		if (l->sgn) opbf(e, I_SBFM, 1, v->r2, v->r, 63, 63);
		else movi(e, 1, v->r2, 0);
	}
}

static codegen_value bf_load_wide(codegen_context *e, const codegen_lvalue *l)
{
	int low = l->width < 64 ? l->width : 64;
	codegen_value v = { .k = CODEGEN_VALUE_REGISTER_PAIR, .r = (uint8_t)bf_read_word(e, l, 0, low) };
	v.r2 = (uint8_t)(l->width > 64 ? bf_read_word(e, l, 64, l->width - 64) : treg(e));
	bf_extend_wide(e, l, &v);
	return v;
}

/* Materialize a wide scalar field without reading outside its covered bytes.
 * Extend from the field precision, rather than the declared storage width. */
static codegen_value bf_load_big(codegen_context *e, const codegen_lvalue *l, ast_qualified_type t)
{
	uint64_t size = codegen_type_size(e->c, t);
	codegen_lvalue dst = { .k = CODEGEN_LVALUE_FRAME, .off = slot(e, size, codegen_type_alignment(e->c, t)), .align = 8 };
	int fill = treg(e);
	movi(e, 1, fill, 0);
	for (uint64_t off = 0; off < size; off += 8) {
		int bits = (int)l->width - (int)(off * 8);
		if (bits > 0) {
			if (bits > 64) bits = 64;
			int r = bf_read_word(e, l, (int)(off * 8), bits);
			if (off * 8 + bits == l->width) {
				if (l->sgn) {
					opbf(e, I_SBFM, 1, r, r, 0, bits - 1);
					shift_imm(e, I_ASRV, 1, fill, r, 63);
				}
			}
			store_n(e, r, &dst, off, 8);
			tfree(e, r);
		} else store_n(e, fill, &dst, off, 8);
	}
	tfree(e, fill);
	return laddr(e, &dst);
}

static codegen_value load1(codegen_context *e, codegen_lvalue *l, ast_qualified_type t);

/* a volatile access stays untagged: opt.c then leaves its slot alone */
VPRIV codegen_value load(codegen_context *e, codegen_lvalue *l, ast_qualified_type t)
{
	codegen_value v;
	int vol = (AST_QUALIFIED_TYPE_QUALIFIERS(t) & AST_QUALIFIER_VOLATILE) != 0;

	e->novtag += vol;
	v = load1(e, l, t);
	e->novtag -= vol;
	return v;
}

static codegen_value load1(codegen_context *e, codegen_lvalue *l, ast_qualified_type t)
{
	cc *c = e->c;
	uint64_t size = codegen_type_size(c, t);
	int r;

	if (is_atomic_lv(e, l, t))
		return ldar_load(e, l, t);
	if (l->bf && codegen_type_is_bitint_big(c, t))
		return bf_load_big(e, l, t);
	if (codegen_type_is_aggregate(c, t) || codegen_type_is_function(c, t))
		return laddr(e, l);
	if (codegen_type_is_float(c, t) && !codegen_type_is_long_double(c, t))
		fpk(e, t);	/* bits, zero-extended (below: not signed) */
	if (l->k == CODEGEN_LVALUE_GLOBAL_REGISTER || l->k == CODEGEN_LVALUE_LOCAL_REGISTER) {
		r = treg(e);
		mov(e, 1, r, l->r);
		return codegen_value_register(r);
	}
	if (l->dynbit_slot) {
		codegen_lvalue bs = { .k = CODEGEN_LVALUE_FRAME, .off = l->dynbit_slot, .align = 8 };
		r = treg(e);
		load_n(e, r, l, 1, 0);
		codegen_value v = arith(e, LEX_TOKEN_GREATER_GREATER, codegen_type_primitive(AST_TYPE_UNSIGNED_LONG), codegen_value_register(r), load(e, &bs, codegen_type_primitive(AST_TYPE_UNSIGNED_LONG)));
		return arith(e, LEX_TOKEN_AMPERSAND, codegen_type_primitive(AST_TYPE_UNSIGNED_LONG), v, codegen_value_constant(1));
	}

	if (l->bf && size == 16)
		return bf_load_wide(e, l);
	if (l->bf) {
		int bits = l->bitoff + l->width, nb = bf_bytes(bits);

		if (bits > 64) {
			/* straddles 8 bytes (packed): the low 64 - bitoff bits, then the rest from byte 8 on */
			codegen_lvalue l2 = *l;
			int h = treg(e);

			if (bits - 64 > 64)
				codegen_fatal_report(e, 0, "unsupported bit-field layout");
			r = treg(e);
			load_n(e, r, l, 8, 0);
			l2.off += 8;
			load_n(e, h, &l2, bf_bytes(bits - 64), 0);
			opbf(e, I_UBFM, 1, r, r, l->bitoff, 63);
			opbf(e, I_BFM, 1, r, h, l->bitoff, bits - 64 - 1);	/* bfi r, h, #(64 - bitoff), #(bits - 64) */
			tfree(e, h);
			opbf(e, l->sgn ? I_SBFM : I_UBFM, 1, r, r, 0, l->width - 1);
			return codegen_value_register(r);
		}
		r = treg(e);
		load_n(e, r, l, nb, 0);
		opbf(e, l->sgn ? I_SBFM : I_UBFM, 1, r, r, l->bitoff, l->bitoff + l->width - 1);
		return codegen_value_register(r);
	}
	if (size == 16) {
		codegen_value v = { .k = CODEGEN_VALUE_REGISTER_PAIR };
		codegen_lvalue h = *l;

		v.r = (uint8_t)treg(e);
		v.r2 = (uint8_t)treg(e);
		load_n(e, v.r, l, 8, 0);
		h.off += 8;
		load_n(e, v.r2, &h, 8, 0);
		return v;
	}
	r = treg(e);
	if (size == 0)
		movi(e, 1, r, 0);
	else
		load_n(e, r, l, (int)size, size < 8 && !codegen_type_is_float(c, t) && codegen_type_is_signed(c, t) ? 1 : 0);
	return codegen_value_register(r);
}

/* store v (of type t) to l; v is consumed unless keep; returns v (in a register if kept) */
static void store1(codegen_context *e, codegen_lvalue *l, ast_qualified_type t, codegen_value *v);

VPRIV void store(codegen_context *e, codegen_lvalue *l, ast_qualified_type t, codegen_value *v)
{
	int vol = (AST_QUALIFIED_TYPE_QUALIFIERS(t) & AST_QUALIFIER_VOLATILE) != 0;

	e->novtag += vol;
	store1(e, l, t, v);
	e->novtag -= vol;
}

static void store1(codegen_context *e, codegen_lvalue *l, ast_qualified_type t, codegen_value *v)
{
	cc *c = e->c;
	uint64_t size = codegen_type_size(c, t);

	if (is_atomic_lv(e, l, t)) {
		stlr_store(e, l, t, v);
		return;
	}
	if (l->bf && codegen_type_is_bitint_big(c, t)) {
		codegen_lvalue src = { .k = CODEGEN_LVALUE_REGISTER, .r = (uint8_t)toreg(e, v), .align = 8 };
		int r = treg(e);
		for (unsigned bit = 0; bit < l->width; bit += 64) {
			unsigned bits = l->width - bit;
			if (bits > 64) bits = 64;
			src.off = bit / 8;
			load_n(e, r, &src, 8, 0);
			bf_write_word(e, l, bit, bits, r);
		}
		tfree(e, r);
		/* Assignment yields the converted field value without rereading a
		 * potentially volatile destination. */
		src.off = 0;
		src.width = l->width;
		src.sgn = l->sgn;
		codegen_value result = bf_load_big(e, &src, t);
		val_free(e, v);
		*v = result;
		return;
	}
	if (codegen_type_is_aggregate(c, t)) {
		codegen_value d = laddr(e, l);
		int rs = toreg(e, v), rd = toreg(e, &d);

		if (codegen_type_is_sve(c, t)) {
			/* the bytes of this vector length, through z31 or p15: ldr, str */
			uint32_t z = codegen_type_is_sve_predicate(c, t) ? 15 : 0x4000 | 31;

			word(e, 0x85800000u | z | (unsigned)rs << 5);
			word(e, 0xe5800000u | z | (unsigned)rd << 5);
		} else {
			copy_mem(e, rd, rs, size);
		}
		val_free(e, &d);
		return;
	}
	if (l->k == CODEGEN_LVALUE_GLOBAL_REGISTER) {
		int r = toreg(e, v);

		mov(e, 1, l->r, r);
		return;
	}
	if (l->k == CODEGEN_LVALUE_LOCAL_REGISTER) {
		/* the register holds what a load from a slot would give */
		if (v->k == CODEGEN_VALUE_CONSTANT) {
			movi(e, 1, l->r, (int64_t)codegen_integer_truncate(c, v->i, t));
		} else {
			int r = toreg(e, v);

			if (size < 8)
				extend(e, l->r, r, (int)size, codegen_type_is_signed(c, t));
			else
				mov(e, 1, l->r, r);
		}
		return;
	}
	if (l->dynbit_slot) {
		codegen_lvalue bs = { .k = CODEGEN_LVALUE_FRAME, .off = l->dynbit_slot, .align = 8 };
		codegen_value mask = arith(e, LEX_TOKEN_LESS_LESS, codegen_type_primitive(AST_TYPE_UNSIGNED_LONG), codegen_value_constant(1), load(e, &bs, codegen_type_primitive(AST_TYPE_UNSIGNED_LONG)));
		int rm = treg(e), rv = toreg(e, v), ro = treg(e), rn = treg(e);
		mov(e, 1, rm, mask.r);
		load_n(e, ro, l, 1, 0);
		codegen_value old = arith(e, LEX_TOKEN_AMPERSAND, codegen_type_primitive(AST_TYPE_UNSIGNED_LONG), codegen_value_register(ro), arith(e, LEX_TOKEN_CARET, codegen_type_primitive(AST_TYPE_UNSIGNED_LONG), mask, codegen_value_constant(-1)));
		mov(e, 1, rn, rv);
		codegen_value nv = arith(e, LEX_TOKEN_MINUS, codegen_type_primitive(AST_TYPE_UNSIGNED_LONG), codegen_value_constant(0), codegen_value_register(rn));
		nv = arith(e, LEX_TOKEN_AMPERSAND, codegen_type_primitive(AST_TYPE_UNSIGNED_LONG), nv, codegen_value_register(rm));
		old = arith(e, LEX_TOKEN_PIPE, codegen_type_primitive(AST_TYPE_UNSIGNED_LONG), old, nv);
		codegen_lvalue byte = *l;
		byte.bf = 0, byte.dynbit_slot = 0;
		store(e, &byte, codegen_type_primitive(AST_TYPE_UNSIGNED_CHAR), &old);
		val_free(e, &old);
		return;
	}

	if (l->bf && size == 16) {
		toreg2(e, v, l->sgn);
		bf_write_word(e, l, 0, l->width < 64 ? l->width : 64, v->r);
		if (l->width > 64) bf_write_word(e, l, 64, l->width - 64, v->r2);
		bf_extend_wide(e, l, v);
		return;
	}
	if (l->bf && l->bitoff + l->width > 64) {
		/* straddles 8 bytes: see load() */
		int bits = l->bitoff + l->width, nb = bf_bytes(bits - 64);
		int r = toreg(e, v), w = treg(e), lo = 64 - l->bitoff;
		codegen_lvalue l2 = *l;

		load_n(e, w, l, 8, 0);
		opbf(e, I_BFM, 1, w, r, (64 - l->bitoff) & 63, lo - 1);
		store_n(e, w, l, 0, 8);
		l2.off += 8;
		load_n(e, w, &l2, nb, 0);
		shift_imm(e, I_LSRV, 1, R_X16, r, lo);
		opbf(e, I_BFM, 1, w, R_X16, 0, bits - 64 - 1);
		store_n(e, w, l, 8, nb);
		tfree(e, w);
		return;
	}
	if (l->bf) {
		int bits = l->bitoff + l->width, nb = bf_bytes(bits);
		int r = toreg(e, v), w = treg(e);
		codegen_lvalue l2 = *l;

		load_n(e, w, &l2, nb, 0);
		/* bfi w, r, #bitoff, #width = bfm w, r, #(-bitoff mod 64), #(width-1) */
		opbf(e, I_BFM, 1, w, r, (64 - l->bitoff) & 63, l->width - 1);
		store_n(e, w, l, 0, nb);
		tfree(e, w);
		return;
	}
	if (size == 16) {
		toreg2(e, v, true);
		store_n(e, v->r, l, 0, 8);
		store_n(e, v->r2, l, 8, 8);
		return;
	}
	if (size == 0)
		return;
	if (v->k == CODEGEN_VALUE_CONSTANT && v->i == 0) {
		store_n(e, ZR, l, 0, (int)size);
		return;
	}
	{
		int r = toreg(e, v);

		store_n(e, r, l, 0, (int)size);
	}
}
