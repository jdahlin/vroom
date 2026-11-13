/* vcc code generator: expressions */
#include "../../../codegen/codegen-internal.h"
#include "port/stack-guard.h"

/* ---------------- expressions ---------------- */

static codegen_value const_global(codegen_context *e, uint32_t s)
{
	cc *c = e->compile_context;
	const sym *y = &c->syms[s];
	cc_value cv;

	if (y->kind == AST_SYMBOL_VARIABLE && (AST_QUALIFIED_TYPE_QUALIFIERS(y->ty) & (AST_QUALIFIER_CONST | AST_QUALIFIER_VOLATILE)) == AST_QUALIFIER_CONST && codegen_type_is_integer(c, y->ty) && !codegen_type_is_bitint_big(c, y->ty) && y->body &&
	    N(y->body)->op != AST_NODE_INITIALIZER && cc_eval(c, y->body, &cv) && cv.kind == CC_VALUE_INTEGER)
		return codegen_value_constant(codegen_integer_truncate(c, cv.i, y->ty));
	if (y->kind == AST_SYMBOL_VARIABLE && (AST_QUALIFIED_TYPE_QUALIFIERS(y->ty) & (AST_QUALIFIER_CONST | AST_QUALIFIER_VOLATILE)) == AST_QUALIFIER_CONST && codegen_type_is_float(c, y->ty) &&
	    codegen_type_size(c, y->ty) != 2 && y->body && N(y->body)->op != AST_NODE_INITIALIZER && cc_eval(c, y->body, &cv) && cv.kind == CC_VALUE_FLOAT)
		return f_const_cv(e, y->ty, &cv);
	return codegen_value_none();
}

static codegen_value gen_var(codegen_context *e, uint32_t n)
{
	cc *c = e->compile_context;
	const node *x = N(n);
	uint32_t s = x->a;
	const sym *y = &c->syms[s];
	codegen_lvalue l;
	codegen_value v;

	if (y->kind == AST_SYMBOL_FUNCTION) {
		uint32_t r = CODEGEN_REFERENCE_MAKE(CODEGEN_REFERENCE_SYMBOL, codegen_symbol_canonical(c, s));

		codegen_reference_note(e, r);
		if (unlikely(codegen_reference_is_weak(c, r))) {
			int rd = treg(e);

			addr_of_ref(e, rd, r, 0);
			return codegen_value_register(rd);
		}
		return codegen_value_address(r, 0);
	}
	if (y->kind == AST_SYMBOL_BUILTIN)
		return codegen_value_address(CODEGEN_REFERENCE_MAKE(CODEGEN_REFERENCE_NAME, (uint32_t)y->val), 0);
	if (codegen_symbol_is_local_variable(c, s)) {
		codegen_binding *b = codegen_symbol_binding_get(e, s);

		if (b->kind == CODEGEN_BINDING_CONSTANT && b->instance)
			return b->value;
		if (b->kind == CODEGEN_BINDING_REGISTER && b->instance) {
			int r = treg(e);

			mov(e, 1, r, b->value.register_number);
			return codegen_value_register(r);
		}
		if (b->known_value_index && (e->known_values_valid >> (b->known_value_index - 1) & 1))
			return codegen_value_constant(e->known_values[b->known_value_index - 1]);
	} else {
		v = const_global(e, codegen_symbol_canonical(c, s));
		if (v.kind == CODEGEN_VALUE_CONSTANT)
			return v;
	}
	l = gen_lval(e, n);
	v = load(e, &l, x->ty);
	if (!(codegen_type_is_aggregate(c, x->ty) && l.kind == CODEGEN_LVALUE_REGISTER))
		lfree(e, &l);
	return v;
}

/* result of a load through l, freeing the lvalue's base unless it became the value */
static codegen_value load_free(codegen_context *e, codegen_lvalue *l, ast_qualified_type t)
{
	codegen_value v = load(e, l, t);

	if (l->kind == CODEGEN_LVALUE_REGISTER && !(v.kind == CODEGEN_VALUE_REGISTER && v.register_number == l->register_number))
		tfree(e, l->register_number);
	return v;
}

/* move v into the fixed register r (for joins) */
VPRIV void move_to(codegen_context *e, int r, int r2, codegen_value *v, ast_qualified_type t)
{
	cc *c = e->compile_context;

	if (v->kind == CODEGEN_VALUE_NONE)
		return;
	if (r2 >= 0 && codegen_type_is_wide(c, t)) {
		toreg2(e, v, codegen_type_is_signed(c, t));
		mov(e, 1, r, v->register_number);
		mov(e, 1, r2, v->register_high);
		val_free(e, v);
		return;
	}
	switch (v->kind) {
	case CODEGEN_VALUE_CONSTANT:
		movi(e, 1, r, (int64_t)v->integer);
		break;
	case CODEGEN_VALUE_ADDRESS:
		addr_of_ref(e, r, v->reference, (int64_t)v->integer);
		break;
	default:
		toreg(e, v);
		mov(e, 1, r, v->register_number);
		val_free(e, v);
	}
}

/* In the pressure retry, a conditional's merge value waits in the frame
 * instead of reserving temporaries across arbitrarily nested alternatives. */
static void cond_spill_value(codegen_context *e, int64_t sl, ast_qualified_type t, codegen_value *v)
{
	bool wide = codegen_type_is_wide(e->compile_context, t);

	if (wide)
		toreg2(e, v, codegen_type_is_signed(e->compile_context, t));
	else
		toreg(e, v);
	mem(e, I_ST, 3, 0, v->register_number, FRAME, sl);
	if (wide)
		mem(e, I_ST, 3, 0, v->register_high, FRAME, sl + 8);
}

static codegen_value gen_cond_spill(codegen_context *e, uint32_t n)
{
	cc *c = e->compile_context;
	const node *x = N(n);
	ast_qualified_type t = x->ty;
	bool wide = codegen_type_is_wide(c, t);
	int64_t sl = slot(e, wide ? 16 : 8, 8);
	uint32_t lelse = newlabel(e), lend = newlabel(e);
	codegen_value v = gen(e, x->a);

	if (!x->b) {
		/* GNU a ?: c evaluates a once; save it before the truth test
		   consumes its registers. The false alternative may then reuse them. */
		cond_spill_value(e, sl, t, &v);
		test_zero(e, &v, t, lend, true);
	} else {
		test_zero(e, &v, N(x->a)->ty, lelse, false);
		v = gen(e, x->b);
		cond_spill_value(e, sl, t, &v);
		val_free(e, &v);
		jump(e, lend);
		deflabel(e, lelse);
	}
	v = gen(e, x->c);
	cond_spill_value(e, sl, t, &v);
	val_free(e, &v);
	deflabel(e, lend);
	v = codegen_value_register(treg(e));
	mem(e, I_LD, 3, 0, v.register_number, FRAME, sl);
	if (wide) {
		v.kind = CODEGEN_VALUE_REGISTER_PAIR;
		v.register_high = treg(e);
		mem(e, I_LD, 3, 0, v.register_high, FRAME, sl + 8);
	}
	return v;
}

static codegen_value gen_cond_expr(codegen_context *e, uint32_t n)
{
	cc *c = e->compile_context;
	const node *x = N(n);
	ast_qualified_type t = x->ty;
	bool vd = codegen_type_is_void(c, t), wide = codegen_type_is_wide(c, t);
	uint32_t lelse = newlabel(e), lend = newlabel(e);
	int r = -1, r2 = -1;
	codegen_value v;

	if (unlikely(e->spill) && !vd)
		return gen_cond_spill(e, n);
	if (!x->b) {		/* a ?: c */
		v = gen(e, x->a);
		if (!codegen_type_is_float(c, t) && (v.kind == CODEGEN_VALUE_CONSTANT || v.kind == CODEGEN_VALUE_ADDRESS)) {
			if (v.kind == CODEGEN_VALUE_ADDRESS || v.integer)
				return v;
			return gen(e, x->c);
		}
		if (wide) {
			toreg2(e, &v, codegen_type_is_signed(c, t));
			r = v.register_number, r2 = v.register_high;
		} else {
			r = toreg(e, &v);
		}
		if (codegen_type_is_bitint_big(c, t)) {
			codegen_value test = codegen_value_register(treg(e));
			mov(e, 1, test.register_number, r);
			test_zero(e, &test, t, lend, true);
		} else if (codegen_type_is_float(c, t)) {
			/* Keep the result value intact while testing its C truth value.
			 * Testing raw floating bits would incorrectly treat -0.0 as true. */
			codegen_value test = codegen_value_register(treg(e));
			mov(e, 1, test.register_number, r);
			if (wide) {
				test.kind = CODEGEN_VALUE_REGISTER_PAIR;
				test.register_high = treg(e);
				mov(e, 1, test.register_high, r2);
			}
			test_zero(e, &test, t, lend, true);
		} else if (wide) {
			op3(e, I_ORR, 1, R_X16, r, r2);
			cbz(e, true, 1, R_X16, lend);
		} else {
			cbz(e, true, X(c, t), r, lend);
		}
		v = gen(e, x->c);
		move_to(e, r, r2, &v, t);
		deflabel(e, lend);
		return wide ? (codegen_value){ .kind = CODEGEN_VALUE_REGISTER_PAIR, .register_number = (uint8_t)r, .register_high = (uint8_t)r2 } : codegen_value_register(r);
	}
	{
		int k = 0;
		codegen_value ca = gen(e, x->a);

		if (ca.kind == CODEGEN_VALUE_CONSTANT || ca.kind == CODEGEN_VALUE_ADDRESS)
			return gen(e, (ca.kind == CODEGEN_VALUE_ADDRESS || ca.integer) ? x->b : x->c);
		k = test_zero(e, &ca, N(x->a)->ty, lelse, false);
		(void)k;
	}
	if (!vd) {
		r = treg(e);
		if (wide)
			r2 = treg(e);
	}
	v = gen(e, x->b);
	if (!vd)
		move_to(e, r, r2, &v, t);
	else
		val_free(e, &v);
	jump(e, lend);
	deflabel(e, lelse);
	v = gen(e, x->c);
	if (!vd)
		move_to(e, r, r2, &v, t);
	else
		val_free(e, &v);
	deflabel(e, lend);
	if (vd)
		return codegen_value_none();
	return wide ? (codegen_value){ .kind = CODEGEN_VALUE_REGISTER_PAIR, .register_number = (uint8_t)r, .register_high = (uint8_t)r2 } : codegen_value_register(r);
}

static codegen_value gen_logical(codegen_context *e, uint32_t n)
{
	cc *c = e->compile_context;
	const node *x = N(n);
	bool isand = x->op == AST_NODE_LOGICAL_AND;
	codegen_value a = gen(e, x->a), b;
	uint32_t lshort, lend;
	int r;

	if (a.kind == CODEGEN_VALUE_CONSTANT || a.kind == CODEGEN_VALUE_ADDRESS) {
		bool t = a.kind == CODEGEN_VALUE_ADDRESS || a.integer;

		if (isand && !t)
			return codegen_value_constant(0);
		if (!isand && t)
			return codegen_value_constant(1);
		b = gen(e, x->b);
		return tobool(e, b, N(x->b)->ty);
	}
	lshort = newlabel(e);
	lend = newlabel(e);
	r = treg(e);
	test_zero(e, &a, N(x->a)->ty, lshort, !isand);
	b = tobool(e, gen(e, x->b), N(x->b)->ty);
	if ((b.kind == CODEGEN_VALUE_CONSTANT && (isand ? !b.integer : b.integer)) || (b.kind == CODEGEN_VALUE_ADDRESS && !isand)) {
		/* x && 0, x || 1: x's effects are done, the value is known (clang folds it,
		   and code it guards, such as calls to functions that do not exist, goes) */
		val_free(e, &b);
		deflabel(e, lshort);
		tfree(e, r);
		return codegen_value_constant(isand ? 0 : 1);
	}
	move_to(e, r, -1, &b, codegen_type_primitive(AST_TYPE_INT));
	jump(e, lend);
	deflabel(e, lshort);
	movi(e, 0, r, isand ? 0 : 1);
	deflabel(e, lend);
	return codegen_value_register(r);
}

/* a lane-wise vector operation on the vectors at ra and rb into a new frame temporary; returns its address */
static codegen_value gen_vbin(codegen_context *e, int op, int ra, int rb, ast_qualified_type t, uint32_t loc)
{
	cc *c = e->compile_context;
	uint64_t size = codegen_type_size(c, t);
	ast_qualified_type et = T(c, t)->base;
	unsigned es = (unsigned)codegen_type_size(c, et);
	bool q = size == 16;
	const char *mn, *arr;
	char ab[8];
	tbuf *rb_ = &e->raw;
	size_t st;
	int64_t sl;
	int rd;
	ins *i;

	if (codegen_type_is_float(c, et))
		codegen_fatal_report(e, loc, "floating point vectors are not supported");
	if (size != 8 && size != 16)
		codegen_fatal_report(e, loc, "unsupported vector size");
	switch (op) {
	case LEX_TOKEN_AMPERSAND: mn = "and"; break;
	case LEX_TOKEN_PIPE: mn = "orr"; break;
	case LEX_TOKEN_CARET: mn = "eor"; break;
	case LEX_TOKEN_PLUS: mn = "add"; break;
	case LEX_TOKEN_MINUS: mn = "sub"; break;
	default: codegen_fatal_report(e, loc, "unsupported vector operator");
	}
	if (op == LEX_TOKEN_AMPERSAND || op == LEX_TOKEN_PIPE || op == LEX_TOKEN_CARET) {
		arr = q ? "16b" : "8b";
	} else {
		snprintf(ab, sizeof ab, "%u%c", (unsigned)(size / es), es == 1 ? 'b' : es == 2 ? 'h' : es == 4 ? 's' : 'd');
		arr = ab;
	}
	sl = slot(e, size, 16);
	rd = treg(e);
	opi(e, I_ADDI, 1, rd, FRAME, sl);
	st = rb_->n;
	vtext_string_append(rb_, q ? "\tldr\tq16, [" : "\tldr\td16, [");
	reg(rb_, ra, 1);
	vtext_string_append(rb_, q ? "]\n\tldr\tq17, [" : "]\n\tldr\td17, [");
	reg(rb_, rb, 1);
	vtext_string_append(rb_, "]\n\t");
	vtext_string_append(rb_, mn);
	vtext_string_append(rb_, "\tv16.");
	vtext_string_append(rb_, arr);
	vtext_string_append(rb_, ", v16.");
	vtext_string_append(rb_, arr);
	vtext_string_append(rb_, ", v17.");
	vtext_string_append(rb_, arr);
	vtext_string_append(rb_, q ? "\n\tstr\tq16, [" : "\n\tstr\td16, [");
	reg(rb_, rd, 1);
	vtext_char_append(rb_, ']');
	i = put(e, I_RAW);
	i->off = (int32_t)st;
	i->ref = (uint32_t)(rb_->n - st);
	return codegen_value_register(rd);
}

/* operand k of a lane-wise operation: lane i of a vector at ra, or the scalar in rs (copied) */
static codegen_value vlane(codegen_context *e, ast_qualified_type t, int r, unsigned i)
{
	cc *c = e->compile_context;

	if (codegen_type_is_vector(c, t)) {
		ast_qualified_type et = T(c, t)->base;
		codegen_lvalue l = { .kind = CODEGEN_LVALUE_REGISTER, .register_number = (uint8_t)r, .offset = (int64_t)i * (int64_t)codegen_type_size(c, et), .align = codegen_type_alignment(c, et) };

		if (codegen_type_is_bool_vector(c, t)) l.offset = i / 8, l.is_bitfield = 1, l.width = 1, l.bit_offset = i % 8;
		return load(e, &l, et);
	}
	{
		int d = treg(e);

		mov(e, 1, d, r);
		return codegen_value_register(d);
	}
}

/*
 * Lane by lane through memory, for what the NEON path (gen_vbin) does not do:
 * other sizes, *, /, %, shifts, comparisons (-1 / 0 lanes), floating-point
 * lanes, a scalar operand (each lane against it), unary - and ~ (b NULL).
 */
static codegen_value gen_vlanes(codegen_context *e, int op, ast_qualified_type at, codegen_value *a, ast_qualified_type bt, codegen_value *b, ast_qualified_type rt, bool unary)
{
	cc *c = e->compile_context;
	ast_qualified_type vt = codegen_type_is_vector(c, at) ? at : bt, et = T(c, vt)->base, rte = T(c, rt)->base;
	unsigned n = T(c, vt)->n;
	int ra = toreg(e, a), rb = unary ? -1 : toreg(e, b), rd;
	int64_t sl = slot(e, codegen_type_size(c, rt), 16);

	rd = treg(e);
	opi(e, I_ADDI, 1, rd, FRAME, sl);
	for (unsigned i = 0; i < n; i++) {
		codegen_value x = vlane(e, at, ra, i), y = unary ? codegen_value_constant(0) : vlane(e, bt, rb, i), r;
		ast_qualified_type yt = unary ? et : codegen_type_is_vector(c, bt) ? T(c, bt)->base : bt;

		if (unary) {
			if (op == LEX_TOKEN_MINUS)
				r = codegen_type_is_float(c, et) ? f_neg(e, x, et) : codegen_type_is_wide(c, et) ? arith2(e, LEX_TOKEN_MINUS, et, codegen_value_constant(0), x)
										 : arith(e, LEX_TOKEN_MINUS, et, codegen_value_constant(0), x);
			else
				r = codegen_type_is_wide(c, et) ? arith2(e, LEX_TOKEN_CARET, et, x, codegen_value_constant(-1)) : arith(e, LEX_TOKEN_CARET, et, x, codegen_value_constant(-1));
		} else if (codegen_operator_is_comparison(op)) {
			if (codegen_type_is_float(c, et)) {
				r = f_compare(e, op, et, x, y);
			} else {
				int cond = codegen_type_is_wide(c, et) ? emit_cmp2(e, op, codegen_type_is_signed(c, et), &x, &y)
							  : emit_cmp(e, op, X(c, et), codegen_type_is_signed(c, et), &x, &y);
				int t;

				val_free(e, &y);
				t = toreg(e, &x);
				if (x.kind == CODEGEN_VALUE_REGISTER_PAIR)
					tfree(e, x.register_high);
				cset(e, t, cond);
				r = codegen_value_register(t);
			}
			if (codegen_type_is_wide(c, rte)) {
				/* 0 / 1 -> 0 / all ones */
				int h = treg(e);

				op3(e, I_SUB, 1, r.register_number, ZR, r.register_number);
				mov(e, 1, h, r.register_number);
				r.kind = CODEGEN_VALUE_REGISTER_PAIR;
				r.register_high = (uint8_t)h;
			} else {
				r = arith(e, LEX_TOKEN_MINUS, rte, codegen_value_constant(0), r);
			}
		} else if (codegen_type_is_float(c, et)) {
			r = f_arith(e, op, et, x, y);
		} else {
			(void)yt;
			r = codegen_type_is_wide(c, et) ? arith2(e, op, et, x, y) : arith(e, op, et, x, y);
		}
		{
			codegen_lvalue l = { .kind = CODEGEN_LVALUE_REGISTER, .register_number = (uint8_t)rd, .offset = (int64_t)i * (int64_t)codegen_type_size(c, rte), .align = codegen_type_alignment(c, rte) };

			if (codegen_type_is_bool_vector(c, rt)) l.offset = i / 8, l.is_bitfield = 1, l.width = 1, l.bit_offset = i % 8;
			store(e, &l, rte, &r);
			val_free(e, &r);
		}
	}
	tfree(e, ra);
	if (rb >= 0)
		tfree(e, rb);
	return codegen_value_register(rd);
}

static bool vbin_fast(cc *c, int op, ast_qualified_type lt, ast_qualified_type rt, ast_qualified_type ty)
{
	uint64_t size = codegen_type_size(c, ty);

	return !codegen_type_is_bool_vector(c, ty) && codegen_type_is_vector(c, lt) && codegen_type_is_vector(c, rt) && (size == 8 || size == 16) && !codegen_type_is_float(c, T(c, ty)->base) &&
	       (op == LEX_TOKEN_AMPERSAND || op == LEX_TOKEN_PIPE || op == LEX_TOKEN_CARET || op == LEX_TOKEN_PLUS || op == LEX_TOKEN_MINUS);
}

static codegen_value gen_binary(codegen_context *e, uint32_t n)
{
	cc *c = e->compile_context;
	const node *x = N(n);
	int op = x->aux;
	ast_qualified_type lt = N(x->a)->ty, rt = N(x->b)->ty, ty = x->ty;
	int known = codegen_operator_is_comparison(op) ? codegen_condition_fold(e, n) : -1;
	codegen_value a, b;
	uint32_t operand;
	int bound = em_cmpbounds(e, n, &operand);
	int repeated = em_repeated_integer(e, n);

	if (repeated == EM_ID_ZERO)
		return codegen_value_constant(0);
	if (repeated == EM_ID_TRUE)
		return codegen_value_constant(1);
	if (repeated == EM_ID_LEFT)
		return gen(e, x->a);

	if (bound >= 0) {
		codegen_value v = gen(e, operand);
		val_free(e, &v);
		return codegen_value_constant(bound);
	}
	if (known >= 0)
		return codegen_value_constant(known);
	if (em_sibling_values(e, n)) {
		int ra, rb;
		a = gen(e, x->a);
		if (a.kind == CODEGEN_VALUE_CONSTANT) {
			bool ok;
			codegen_value v = fold_int_bin(e, op, ty, ty, a.integer, a.integer, &ok);
			if (ok) return v;
		}
		ra = toreg(e, &a);
		rb = treg(e);
		mov(e, X(c, ty), rb, ra);
		return arith(e, op, ty, a, codegen_value_register(rb));
	}
	if (unlikely(vstack_guard_low()))	/* nested operators recurse here: an error before the stack runs out */
		codegen_fatal_report(e, x->loc, "expression nested too deeply");
	a = gen(e, x->a);
	if (unlikely(e->spill) && (a.kind == CODEGEN_VALUE_REGISTER || a.kind == CODEGEN_VALUE_REGISTER_PAIR) && codegen_temporary_index(a.register_number) >= 0 &&
	    !codegen_type_is_vector(c, lt) && N(x->b)->op != AST_NODE_NUMBER && N(x->b)->op != AST_NODE_VARIABLE) {
		/* out of registers before: the left value waits in the frame while the right is computed */
		int64_t sl = slot(e, 16, 8);
		bool two = a.kind == CODEGEN_VALUE_REGISTER_PAIR;

		mem(e, I_ST, 3, 0, a.register_number, FRAME, sl);
		if (two)
			mem(e, I_ST, 3, 0, a.register_high, FRAME, sl + 8);
		val_free(e, &a);
		b = gen(e, x->b);
		a.register_number = (uint8_t)treg(e);
		mem(e, I_LD, 3, 0, a.register_number, FRAME, sl);
		if (two) {
			a.register_high = (uint8_t)treg(e);
			mem(e, I_LD, 3, 0, a.register_high, FRAME, sl + 8);
		}
	} else {
		b = gen(e, x->b);
	}
	if (codegen_type_is_vector(c, ty) && !vbin_fast(c, op, lt, rt, ty))
		return gen_vlanes(e, op, lt, &a, rt, &b, ty, false);
	if (codegen_type_is_vector(c, ty)) {
		int ra = toreg(e, &a), rb = toreg(e, &b);
		codegen_value r = gen_vbin(e, op, ra, rb, ty, x->loc);

		val_free(e, &a);
		val_free(e, &b);
		return r;
	}

	if ((op == LEX_TOKEN_LESS_LESS || op == LEX_TOKEN_GREATER_GREATER) && (codegen_type_is_bitint_big(c, rt) || codegen_type_is_wide(c, rt)))
		b = cast(e, b, rt, codegen_type_primitive(AST_TYPE_INT), AST_CAST_INTEGER);
	if (codegen_type_is_bitint_big(c, lt))
		return codegen_operator_is_comparison(op) ? a64_bigcmp(e, op, lt, a, b) : a64_bigbin(e, op, ty, a, b);
	if (codegen_type_is_float(c, lt) || codegen_type_is_float(c, ty))
		return codegen_operator_is_comparison(op) ? f_compare(e, op, lt, a, b) : f_arith(e, op, ty, a, b);
	/* pointer arithmetic */
	if (codegen_type_is_pointer(c, ty) && (op == LEX_TOKEN_PLUS || op == LEX_TOKEN_MINUS) && codegen_type_is_pointer(c, lt))
		return codegen_type_is_pointer_to_vla(c, lt) ? ptr_add_vla(e, a, b, lt, op == LEX_TOKEN_MINUS)
					 : ptr_add(e, a, b, (int64_t)elem_size(c, lt), op == LEX_TOKEN_MINUS);
	if (op == LEX_TOKEN_MINUS && codegen_type_is_pointer(c, lt) && codegen_type_is_pointer(c, rt) && codegen_type_is_pointer_to_vla(c, lt)) {
		/* (p - q) / the element size, which is known only now */
		codegen_value es = vla_bytes(e, T(c, lt)->base);
		int ra = toreg(e, &a), rb = toreg(e, &b), rs = toreg(e, &es);

		op3(e, I_SUB, 1, ra, ra, rb);
		op3(e, I_SDIV, 1, ra, ra, rs);
		tfree(e, rb);
		val_free(e, &es);
		return a;
	}
	if (op == LEX_TOKEN_MINUS && codegen_type_is_pointer(c, lt) && codegen_type_is_pointer(c, rt)) {
		int64_t es = (int64_t)elem_size(c, lt);

		if ((a.kind == CODEGEN_VALUE_ADDRESS && b.kind == CODEGEN_VALUE_ADDRESS && a.reference == b.reference) || (a.kind == CODEGEN_VALUE_CONSTANT && b.kind == CODEGEN_VALUE_CONSTANT))
			return codegen_value_constant((a.integer - b.integer) / (es ? es : 1));
		{
			int ra = toreg(e, &a), rb = toreg(e, &b);

			op3(e, I_SUB, 1, ra, ra, rb);
			tfree(e, rb);
			if (es > 1 && !(es & (es - 1))) {
				shift_imm(e, I_ASRV, 1, ra, ra, __builtin_ctzll((uint64_t)es));
			} else if (es > 1) {
				movi(e, 1, R_X16, es);
				op3(e, I_SDIV, 1, ra, ra, R_X16);
			}
			return a;
		}
	}
	if (codegen_operator_is_comparison(op)) {
		bool sg = !codegen_type_is_pointer(c, lt) && codegen_type_is_signed(c, lt);
		int cond, r;

		if (a.kind == CODEGEN_VALUE_CONSTANT && b.kind == CODEGEN_VALUE_CONSTANT) {
			bool ok;
			codegen_value v = fold_int_bin(e, op, codegen_type_is_pointer(c, lt) ? codegen_type_primitive(AST_TYPE_UNSIGNED_LONG) : lt, ty, a.integer, b.integer, &ok);

			if (ok)
				return v;
		}
		if ((a.kind == CODEGEN_VALUE_ADDRESS && b.kind == CODEGEN_VALUE_ADDRESS && a.reference == b.reference) ||
		    (a.kind == CODEGEN_VALUE_ADDRESS && b.kind == CODEGEN_VALUE_CONSTANT && b.integer == 0) || (b.kind == CODEGEN_VALUE_ADDRESS && a.kind == CODEGEN_VALUE_CONSTANT && a.integer == 0)) {
			if (a.kind == CODEGEN_VALUE_ADDRESS && b.kind == CODEGEN_VALUE_ADDRESS) {
				bool ok;

				return fold_int_bin(e, op, codegen_type_primitive(AST_TYPE_LONG), ty, a.integer, b.integer, &ok);
			}
			if (op == LEX_TOKEN_EQUAL_EQUAL)
				return codegen_value_constant(0);
			if (op == LEX_TOKEN_EXCLAMATION_EQUAL)
				return codegen_value_constant(1);
		}
		if ((op == LEX_TOKEN_EQUAL_EQUAL || op == LEX_TOKEN_EXCLAMATION_EQUAL) && b.kind == CODEGEN_VALUE_CONSTANT && b.integer == 0 && last_cset(e, &a) >= 0) {
			if (op == LEX_TOKEN_EQUAL_EQUAL)	/* (cond) == 0: the inverse cset */
				e->records[e->record_count - 1].cond ^= 1;
			return a;
		}
		if (codegen_type_is_wide(c, lt))
			cond = emit_cmp2(e, op, sg, &a, &b);
		else
			cond = emit_cmp(e, op, X(c, lt), sg, &a, &b);
		val_free(e, &b);
		r = toreg(e, &a);
		if (a.kind == CODEGEN_VALUE_REGISTER_PAIR)
			tfree(e, a.register_high);
		cset(e, r, cond);
		return codegen_value_register(r);
	}
	if (a.kind == CODEGEN_VALUE_CONSTANT && b.kind == CODEGEN_VALUE_CONSTANT) {
		bool ok;
		codegen_value v = fold_int_bin(e, op, ty, ty, a.integer, b.integer, &ok);

		if (op == LEX_TOKEN_LESS_LESS || op == LEX_TOKEN_GREATER_GREATER)
			v = fold_int_bin(e, op, lt, ty, a.integer, b.integer, &ok);
		if (ok)
			return v;
	}
	/* 0 << x, 0 >> x, -1 >> x (signed): the constant, whatever x is */
	if ((op == LEX_TOKEN_LESS_LESS || op == LEX_TOKEN_GREATER_GREATER) && a.kind == CODEGEN_VALUE_CONSTANT && b.kind != CODEGEN_VALUE_CONSTANT &&
	    (codegen_integer_truncate(c, a.integer, ty) == 0 || (op == LEX_TOKEN_GREATER_GREATER && codegen_type_is_signed(c, ty) && codegen_integer_truncate(c, a.integer, ty) == -1))) {
		val_free(e, &b);
		return codegen_value_constant(codegen_integer_truncate(c, a.integer, ty));
	}
	if (a.kind == CODEGEN_VALUE_ADDRESS && b.kind == CODEGEN_VALUE_CONSTANT && (op == LEX_TOKEN_PLUS || op == LEX_TOKEN_MINUS) && codegen_type_size(c, ty) == 8) {
		a.integer += op == LEX_TOKEN_PLUS ? b.integer : -b.integer;
		return a;
	}
	if (codegen_type_is_wide(c, ty))
		return arith2(e, op, ty, a, b);
	return arith(e, op, ty, a, b);
}

static codegen_value gen_unary(codegen_context *e, uint32_t n)
{
	cc *c = e->compile_context;
	const node *x = N(n);
	ast_qualified_type t = N(x->a)->ty;
	uint32_t operand = em_double_unary(e, n);
	codegen_value v;

	if (operand)
		return gen(e, operand);
	v = gen(e, x->a);

	if (codegen_type_is_vector(c, x->ty) && (x->aux == LEX_TOKEN_MINUS || x->aux == LEX_TOKEN_TILDE))
		return gen_vlanes(e, x->aux, t, &v, t, NULL, x->ty, true);
	switch (x->aux) {
	case LEX_TOKEN_PLUS:
		return v;
	case LEX_TOKEN_MINUS:
		if (codegen_type_is_bitint_big(c, x->ty)) return a64_bigbin(e, LEX_TOKEN_MINUS, x->ty, codegen_value_constant(0), v);
		if (codegen_type_is_float(c, x->ty))
			return f_neg(e, v, x->ty);
		if (v.kind == CODEGEN_VALUE_CONSTANT)
			return codegen_value_constant(codegen_integer_truncate(c, (i128)((u128)0 - (u128)v.integer), x->ty));
		if (codegen_type_is_wide(c, x->ty)) {
			toreg2(e, &v, codegen_type_is_signed(c, x->ty));
			op3(e, I_SUBS, 1, v.register_number, ZR, v.register_number);
			op3(e, I_SBC, 1, v.register_high, ZR, v.register_high);
			return v;
		}
		toreg(e, &v);
		op3(e, I_SUB, X(c, x->ty), v.register_number, ZR, v.register_number);
		return v;
	case LEX_TOKEN_TILDE:
		if (codegen_type_is_bitint_big(c, x->ty)) return a64_bigbin(e, LEX_TOKEN_CARET, x->ty, v, codegen_value_constant(-1));
		if (v.kind == CODEGEN_VALUE_CONSTANT)
			return codegen_value_constant(codegen_integer_truncate(c, ~v.integer, x->ty));
		if (codegen_type_is_wide(c, x->ty)) {
			toreg2(e, &v, codegen_type_is_signed(c, x->ty));
			op3(e, I_ORN, 1, v.register_number, ZR, v.register_number);
			op3(e, I_ORN, 1, v.register_high, ZR, v.register_high);
			return v;
		}
		toreg(e, &v);
		op3(e, I_ORN, X(c, x->ty), v.register_number, ZR, v.register_number);
		return v;
	case LEX_TOKEN_EXCLAMATION:
		v = tobool(e, v, t);
		if (v.kind == CODEGEN_VALUE_CONSTANT)
			return codegen_value_constant(!v.integer);
		if (last_cset(e, &v) >= 0)
			e->records[e->record_count - 1].cond ^= 1;
		else
			opi(e, I_EORI, 0, v.register_number, v.register_number, 1);
		return v;
	}
	codegen_fatal_report(e, x->loc, "unsupported unary operator");
}

/* convert v from type `from` to `to` the way an assignment back to an integer lvalue does */
static codegen_value conv_back(codegen_context *e, codegen_value v, ast_qualified_type from, ast_qualified_type to)
{
	cc *c = e->compile_context;

	if (KIND(c, to) == AST_TYPE_BOOL)
		return tobool(e, v, from);
	if (codegen_type_is_pointer(c, to) || codegen_type_is_pointer(c, from))
		return v;
	return cast(e, v, from, to, AST_CAST_INTEGER);
}

/* Normalize a value loaded outside gen(), before a compound operation can
 * move padding into value bits or interpret it as a signed high word. */
static codegen_value bitint_value(codegen_context *e, codegen_value v, ast_qualified_type t)
{
	cc *c = e->compile_context;
	{
		unsigned bits = T(c, t)->n;
		bool sgn = codegen_type_is_signed(c, t);

		if (v.kind == CODEGEN_VALUE_CONSTANT)
			return codegen_value_constant(codegen_integer_truncate(c, v.integer, t));
		if (bits > 64) {
			toreg2(e, &v, sgn);
			if (bits < 128)
				opbf(e, sgn ? I_SBFM : I_UBFM, 1, v.register_high, v.register_high, 0, bits - 65);
		} else if (bits < 64) {
			int r = toreg(e, &v);

			opbf(e, sgn ? I_SBFM : I_UBFM, 1, r, r, 0, bits - 1);
		}
	}
	return v;
}

/* old op= v (x a compound assignment to an lvalue of type lt): the new value, of type lt */

static codegen_value assign_op(codegen_context *e, const node *x, ast_qualified_type lt, codegen_value old, codegen_value v)
{
	cc *c = e->compile_context;
	ast_qualified_type pt = x->c;
	int op = x->aux;

	if (codegen_type_is_bitint(c, lt) && !codegen_type_is_bitint_big(c, lt))
		old = bitint_value(e, old, lt);
	if (codegen_type_is_bitint_big(c, pt)) {
		if (codegen_type_is_bitint_big(c, lt)) old = a64_bigcopy(e, old, lt);
		old = a64_bigcvt(e, old, lt, pt);
		ast_qualified_type rt = N(x->b)->ty;
		if ((op == LEX_TOKEN_LESS_LESS || op == LEX_TOKEN_GREATER_GREATER) && (codegen_type_is_bitint_big(c, rt) || codegen_type_is_wide(c, rt)))
			v = cast(e, v, rt, codegen_type_primitive(AST_TYPE_INT), AST_CAST_INTEGER);
		old = a64_bigbin(e, op, pt, old, v);
		return a64_bigcvt(e, old, pt, lt);
	}
	if (codegen_type_is_pointer(c, lt)) {
		old = codegen_type_is_pointer_to_vla(c, lt) ? ptr_add_vla(e, old, v, lt, op == LEX_TOKEN_MINUS)
					: ptr_add(e, old, v, (int64_t)elem_size(c, lt), op == LEX_TOKEN_MINUS);
	} else if (codegen_type_is_float(c, pt) || codegen_type_is_float(c, lt)) {
		old = cast(e, old, lt, pt, AST_CAST_INTEGER);
		old = codegen_type_is_float(c, pt) ? f_arith(e, op, pt, old, v) : arith(e, op, pt, old, v);
		old = cast(e, old, pt, lt, AST_CAST_INTEGER);
	} else {
		old = conv_back(e, old, lt, pt);
		if (old.kind == CODEGEN_VALUE_CONSTANT && v.kind == CODEGEN_VALUE_CONSTANT) {
			bool ok;
			codegen_value f = fold_int_bin(e, op, (op == LEX_TOKEN_LESS_LESS || op == LEX_TOKEN_GREATER_GREATER) ? pt : pt, pt, old.integer, v.integer, &ok);

			if (ok)
				old = f;
			else
				old = codegen_type_is_wide(c, pt) ? arith2(e, op, pt, old, v) : arith(e, op, pt, old, v);
		} else {
			old = codegen_type_is_wide(c, pt) ? arith2(e, op, pt, old, v) : arith(e, op, pt, old, v);
		}
		old = conv_back(e, old, pt, lt);
	}
	return old;
}

static codegen_value incdec_op(codegen_context *e, ast_qualified_type t, bool inc, bool post, codegen_value *old);

/* an assignment operator or ++/-- whose operand is an _Atomic object */
static inline bool atomic_target(codegen_context *e, uint32_t a, ast_qualified_type t)
{
	(void)a;
	return unlikely(T(e->compile_context, t)->flags & AST_TYPE_FLAG_ATOMIC);
}

/*
 * x op= v, ++x, x++ ... on an _Atomic x: one atomic read-modify-write.
 *   ldar  ob, [p]
 * top:
 *   new = (copy of ob) op (copy of v)       the operator's usual code
 *   mov   cmp, ob
 *   casal cmp, new, [p]                     cmp = what memory held
 *   cmp   cmp, ob;  mov ob, cmp;  b.ne top
 * The operator runs outside any exclusive section, so anything it does
 * (calls included) is fine; v is evaluated once, before the loop.
 */
static codegen_value atomic_rmw(codegen_context *e, uint32_t n, bool inc, bool post)
{
	cc *c = e->compile_context;
	const node *x = N(n);
	bool asg = x->op == AST_NODE_ASSIGN;
	ast_qualified_type t = N(x->a)->ty;
	codegen_value v = codegen_value_none(), old, nv;
	codegen_lvalue l;
	int sz, ra, ob, cmp, nb;
	bool own;
	uint32_t top;

	if (asg) {
		v = gen(e, x->b);
		if (v.kind == CODEGEN_VALUE_REGISTER_PAIR)
			codegen_fatal_report(e, x->loc, "compound assignment to an _Atomic object with a 128-bit operand is not supported");
		if (v.kind != CODEGEN_VALUE_CONSTANT)
			toreg(e, &v);
	}
	l = gen_lval(e, x->a);
	if (!is_atomic_lv(e, &l, t))
		codegen_fatal_report(e, x->loc, "_Atomic register variables are not supported");
	sz = atomic_sz(e, t);
	if (codegen_type_is_float(c, t))
		fpk(e, t);
	ra = atomic_addr(e, &l, &own);
	ob = treg(e);
	word(e, (unsigned)sz << 30 | 0x08dffc00u | (unsigned)ra << 5 | (unsigned)ob);	/* ldar */
	top = newlabel(e);
	deflabel_loop(e, top, true);
	{
		int r = treg(e);

		if (sz < 3 && !codegen_type_is_float(c, t) && codegen_type_is_signed(c, t))
			extend(e, r, ob, 1 << sz, true);
		else
			mov(e, 1, r, ob);
		old = codegen_value_register(r);
	}
	if (asg) {
		codegen_value vc = v;

		if (v.kind == CODEGEN_VALUE_REGISTER) {
			int r = treg(e);

			mov(e, 1, r, v.register_number);
			vc = codegen_value_register(r);
		}
		nv = assign_op(e, x, t, old, vc);
	} else {
		nv = incdec_op(e, t, inc, post, &old);
	}
	nb = nv.kind == CODEGEN_VALUE_CONSTANT && nv.integer == 0 ? ZR : toreg(e, &nv);
	cmp = treg(e);
	mov(e, 1, cmp, ob);
	at_cas(e, (unsigned)sz, cmp, nb, ra);	/* casal */
	op3(e, I_SUBS, sz == 3, ZR, cmp, ob);	/* both zero-extended */
	mov(e, 1, ob, cmp);
	bcond(e, C_NE, top);
	tfree(e, cmp);
	tfree(e, ob);
	if (own)
		tfree(e, ra);
	lfree(e, &l);
	val_free(e, &v);
	if (!asg && post) {
		val_free(e, &nv);
		return old;
	}
	if (!asg && KIND(c, t) == AST_TYPE_BOOL)
		val_free(e, &old);
	return nv;
}

/* Updating a wide scalar keeps its lvalue alive through the final store.
 * Aggregate-style loads return its address, so give the arithmetic value
 * separate register ownership before normalization or arithmetic consumes it. */
static codegen_value load_for_update(codegen_context *e, codegen_lvalue *l, ast_qualified_type t)
{
	codegen_value v = load(e, l, t);
	if (codegen_type_is_bitint_big(e->compile_context, t) && l->kind == CODEGEN_LVALUE_REGISTER && v.kind == CODEGEN_VALUE_REGISTER && v.register_number == l->register_number) {
		int r = treg(e);
		mov(e, 1, r, v.register_number);
		v.register_number = (uint8_t)r;
	}
	return v;
}

static codegen_value gen_assign(codegen_context *e, uint32_t n)
{
	cc *c = e->compile_context;
	const node *x = N(n);
	ast_qualified_type lt = N(x->a)->ty;
	codegen_lvalue l;
	codegen_value v, old;

	if (x->aux && codegen_type_is_vector(c, lt)) {
		/* v op= w */
		codegen_value w = gen(e, x->b), av, r;
		int ra, rw;

		l = gen_lval(e, x->a);
		{
			codegen_lvalue l2 = l;

			av = laddr(e, &l2);
		}
		if (!vbin_fast(c, x->aux, lt, N(x->b)->ty, lt)) {
			ast_qualified_type bt = N(x->b)->ty;

			r = gen_vlanes(e, x->aux, lt, &av, bt, &w, lt, false);
			store(e, &l, lt, &r);
			return r;
		}
		ra = toreg(e, &av), rw = toreg(e, &w);
		r = gen_vbin(e, x->aux, ra, rw, lt, x->loc);
		val_free(e, &w);
		val_free(e, &av);
		store(e, &l, lt, &r);
		return r;
	}
	if (!x->aux) {
		int ak;

		v = gen(e, x->b);
		if ((ak = codegen_array_element_slot(e, x->a, true)) >= 0) {
			codegen_value cv = v;

			l = gen_lval(e, x->a);
			store(e, &l, lt, &v);
			lfree(e, &l);
			e->known_values_valid &= (uint16_t)~(1u << ak);
			if (cv.kind == CODEGEN_VALUE_CONSTANT) {
				e->known_values[ak] = (int64_t)codegen_integer_truncate(c, cv.integer, lt);
				e->known_values_valid |= (uint16_t)(1u << ak);
			}
			return v;
		}
		if (N(x->a)->op == AST_NODE_VARIABLE && (e->unit->symbol_flags[N(x->a)->a] & CODEGEN_SYMBOL_CONSTANT_WRITTEN)) {
			codegen_binding *b = codegen_symbol_binding_get(e, N(x->a)->a);

			if (b->kind == CODEGEN_BINDING_CONSTANT && b->instance) {
				/* the same constant again (codegen_prepass_assignment_note) */
				val_free(e, &v);
				return b->value;
			}
			if (b->known_value_index) {
				int k = b->known_value_index - 1;
				codegen_value cv = v;	/* store() may move v into a register */

				l = gen_lval(e, x->a);
				store(e, &l, lt, &v);
				lfree(e, &l);
				e->known_values_valid &= (uint16_t)~(1u << k);
				if (cv.kind == CODEGEN_VALUE_CONSTANT) {
					e->known_values[k] = (int64_t)codegen_integer_truncate(c, cv.integer, lt);
					e->known_values_valid |= (uint16_t)(1u << k);
				}
				return v;
			}
		}
		l = gen_lval(e, x->a);
		store(e, &l, lt, &v);
		lfree(e, &l);
		if (l.is_bitfield && v.kind == CODEGEN_VALUE_REGISTER) {
			/* the value of a bit-field assignment is the truncated value */
			opbf(e, l.is_signed ? I_SBFM : I_UBFM, 1, v.register_number, v.register_number, 0, l.width - 1);
		}
		return v;
	}
	if (atomic_target(e, x->a, lt))
		return atomic_rmw(e, n, false, false);
	v = gen(e, x->b);
	l = gen_lval(e, x->a);
	old = load_for_update(e, &l, lt);
	old = assign_op(e, x, lt, old, v);
	store(e, &l, lt, &old);
	lfree(e, &l);
	if (l.is_bitfield && l.width < 64 && old.kind != CODEGEN_VALUE_REGISTER_PAIR) {
		/* the value of a bit-field compound assignment is the truncated value */
		int r = toreg(e, &old);

		opbf(e, l.is_signed ? I_SBFM : I_UBFM, 1, r, r, 0, l.width - 1);
		old = codegen_value_register(r);
	}
	return old;
}

/* ++/-- on *old (of type t): returns the new value; *old becomes what a postfix operator yields */
static codegen_value incdec_op(codegen_context *e, ast_qualified_type t, bool inc, bool post, codegen_value *old)
{
	cc *c = e->compile_context;
	codegen_value nv;
	int64_t d = codegen_type_is_pointer(c, t) ? (int64_t)elem_size(c, t) : 1;

	if (codegen_type_is_bitint_big(c, t)) {
		if (post) {
			*old = a64_bigbin(e, LEX_TOKEN_PLUS, t, *old, codegen_value_constant(0));
			codegen_value copy = codegen_value_register(treg(e));
			mov(e, 1, copy.register_number, old->register_number);
			return a64_bigbin(e, inc ? LEX_TOKEN_PLUS : LEX_TOKEN_MINUS, t, copy, codegen_value_constant(1));
		}
		nv = a64_bigbin(e, inc ? LEX_TOKEN_PLUS : LEX_TOKEN_MINUS, t, *old, codegen_value_constant(1));
		*old = nv;
		return nv;
	}
	if (KIND(c, t) == AST_TYPE_BOOL) {
		nv = codegen_value_constant(1);
		if (!inc) {
			/* --b: b = b - 1 != 0, i.e. !b */
			int r = treg(e), ro = toreg(e, old);

			opi(e, I_EORI, 0, r, ro, 1);
			nv = codegen_value_register(r);
		}
	} else if (codegen_type_is_long_double(c, t)) {
		codegen_value one = codegen_value_constant(ld_of_d(1.0)), cp = *old;

		toreg2(e, &cp, false);
		if (post) {
			nv.kind = CODEGEN_VALUE_REGISTER_PAIR;
			nv.register_number = (uint8_t)treg(e);
			nv.register_high = (uint8_t)treg(e);
			mov(e, 1, nv.register_number, cp.register_number);
			mov(e, 1, nv.register_high, cp.register_high);
			nv = f_arith(e, inc ? LEX_TOKEN_PLUS : LEX_TOKEN_MINUS, t, nv, one);
			*old = cp;
		} else {
			nv = *old = f_arith(e, inc ? LEX_TOKEN_PLUS : LEX_TOKEN_MINUS, t, cp, one);
		}
	} else if (codegen_type_is_float(c, t)) {
		toreg(e, old);
		if (post) {
			int r = treg(e);

			mov(e, 1, r, old->register_number);
			nv = f_arith(e, inc ? LEX_TOKEN_PLUS : LEX_TOKEN_MINUS, t, codegen_value_register(r), f_const_t(e, t, 1.0));
		} else {
			nv = *old = f_arith(e, inc ? LEX_TOKEN_PLUS : LEX_TOKEN_MINUS, t, *old, f_const_t(e, t, 1.0));
		}
	} else if (codegen_type_is_wide(c, t)) {
		codegen_value one = codegen_value_constant(1), cp = *old;

		toreg2(e, &cp, codegen_type_is_signed(c, t));
		if (post) {
			nv.kind = CODEGEN_VALUE_REGISTER_PAIR;
			nv.register_number = (uint8_t)treg(e);
			nv.register_high = (uint8_t)treg(e);
			mov(e, 1, nv.register_number, cp.register_number);
			mov(e, 1, nv.register_high, cp.register_high);
			nv = arith2(e, inc ? LEX_TOKEN_PLUS : LEX_TOKEN_MINUS, t, nv, one);
			*old = cp;
		} else {
			nv = arith2(e, inc ? LEX_TOKEN_PLUS : LEX_TOKEN_MINUS, t, cp, one);
			*old = nv;
		}
	} else if (codegen_type_is_pointer(c, t) && codegen_type_is_pointer_to_vla(c, t)) {
		/* a pointer to a VLA steps by the size computed now */
		toreg(e, old);
		if (post) {
			int r = treg(e);

			mov(e, 1, r, old->register_number);
			nv = ptr_add_vla(e, codegen_value_register(r), codegen_value_constant(1), t, !inc);
		} else {
			nv = *old = ptr_add_vla(e, *old, codegen_value_constant(1), t, !inc);
		}
	} else {
		int ro = toreg(e, old);

		if (post) {
			int r = treg(e);

			opi(e, inc ? I_ADDI : I_SUBI, X(c, t), r, ro, d);
			nv = codegen_value_register(r);
		} else {
			opi(e, inc ? I_ADDI : I_SUBI, X(c, t), ro, ro, d);
			nv = *old;
		}
	}
	return nv;
}

static codegen_value gen_incdec(codegen_context *e, uint32_t n)
{
	cc *c = e->compile_context;
	const node *x = N(n);
	ast_qualified_type t = N(x->a)->ty;
	bool inc = x->op == AST_NODE_PRE_INCREMENT || x->op == AST_NODE_POST_INCREMENT, post = x->op == AST_NODE_POST_INCREMENT || x->op == AST_NODE_POST_DECREMENT;
	codegen_lvalue l;
	codegen_value old, nv;

	if (atomic_target(e, x->a, t))
		return atomic_rmw(e, n, inc, post);
	l = gen_lval(e, x->a);
	old = load_for_update(e, &l, t);
	nv = incdec_op(e, t, inc, post, &old);
	store(e, &l, t, &nv);
	lfree(e, &l);
	if (post) {
		val_free(e, &nv);
		return old;
	}
	if (KIND(c, t) == AST_TYPE_BOOL)
		val_free(e, &old);
	else if (l.is_bitfield && l.width < 64 && nv.kind != CODEGEN_VALUE_REGISTER_PAIR) {
		/* the value of ++/-- on a bit-field is the truncated value */
		int r = toreg(e, &nv);

		opbf(e, l.is_signed ? I_SBFM : I_UBFM, 1, r, r, 0, l.width - 1);
		nv = codegen_value_register(r);
	}
	return nv;
}

static codegen_value gen_inner(codegen_context *e, uint32_t n);

VPRIV codegen_value gen(codegen_context *e, uint32_t n)
{
	if (n && n == e->reuse_from) n = e->reuse_to;
	cc *c = e->compile_context;
	ast_qualified_type t = N(n)->ty;

	if (codegen_type_is_bitint_big(c, t)) {
		codegen_value v = gen_inner(e, n);
		switch (N(n)->op) {
		case AST_NODE_VARIABLE: case AST_NODE_MEMBER: case AST_NODE_INDEX: case AST_NODE_DEREFERENCE: case AST_NODE_COMPOUND_LITERAL:
		case AST_NODE_CALL: case AST_NODE_VA_ARG:
			return a64_bigcopy(e, v, t);
		default: return v;
		}
	}
	if (likely(!codegen_type_is_bitint(c, t)))
		return gen_inner(e, n);
	codegen_value v = gen_inner(e, n);
	return bitint_value(e, v, t);
}

static codegen_value gen_inner(codegen_context *e, uint32_t n)
{
	cc *c = e->compile_context;
	const node *x = N(n);

	switch (x->op) {
	case AST_NODE_BITINT_NUMBER:
		return a64_bigconst(e, x->ty, &c->lists[x->a], 0);
	case AST_NODE_NUMBER:
		return codegen_type_is_bitint_big(c, x->ty) ? a64_bigconst(e, x->ty, NULL, c->ipool[x->a]) : codegen_value_constant(c->ipool[x->a]);
	case AST_NODE_FLOAT_NUMBER:
		if (codegen_type_is_long_double(c, x->ty)) {
			u128 q;

			memcpy(&q, &c->fpool[x->a + 1], 16);	/* its binary128 bits, after its double */
			return codegen_value_constant((i128)q);
		}
		return f_const_t(e, x->ty, c->fpool[x->a]);
	case AST_NODE_STRING:
		codegen_reference_note(e, CODEGEN_REFERENCE_MAKE(CODEGEN_REFERENCE_STRING, x->a));
		return codegen_value_address(CODEGEN_REFERENCE_MAKE(CODEGEN_REFERENCE_STRING, x->a), 0);
	case AST_NODE_VARIABLE:
		return gen_var(e, n);
	case AST_NODE_MEMBER:
	case AST_NODE_INDEX:
	case AST_NODE_DEREFERENCE: {
		int k = x->op == AST_NODE_INDEX ? codegen_array_element_slot(e, n, false) : -1;

		if (k >= 0 && (e->known_values_valid >> k & 1))
			return codegen_value_constant(e->known_values[k]);
		codegen_lvalue l = gen_lval(e, n);

		return load_free(e, &l, x->ty);
	}
	case AST_NODE_COMPOUND_LITERAL: {
		codegen_lvalue l = gen_lval(e, n);

		return load_free(e, &l, x->ty);
	}
	case AST_NODE_ADDRESS: {
		const node *a = N(x->a);
		codegen_lvalue l;

		if (a->op == AST_NODE_LABEL_ADDRESS || codegen_type_is_function(c, a->ty))
			return gen(e, x->a);
		l = gen_lval(e, x->a);
		if (l.kind == CODEGEN_LVALUE_SYMBOL)
			codegen_reference_note(e, l.reference);
		return laddr(e, &l);
	}
	case AST_NODE_LABEL_ADDRESS: {
		uint32_t l = x->a;
		codegen_label *m = &e->label_map[l];
		int r = treg(e);

		if (m->instance != e->function_instance->instance) {
			m->instance = e->function_instance->instance;
			m->id = newlabel(e);
		}
		if (e->is_reachable) {
			ins *i = put(e, I_ADR);

			i->rd = (uint8_t)r;
			i->label = m->id;
		}
		e->label_reference_counts[m->id]++;
		e->label_reference_counts[m->id] |= LREF_EXT;
		return codegen_value_register(r);
	}
	case AST_NODE_CAST:
		if (x->c) return gen(e, x->c);
		switch (x->aux) {
		case AST_CAST_ARRAY_DECAY: {
			codegen_lvalue l = gen_lval(e, x->a);

			if (l.kind == CODEGEN_LVALUE_SYMBOL)
				codegen_reference_note(e, l.reference);
			return laddr(e, &l);
		}
		case AST_CAST_FUNCTION_DECAY:
			return gen(e, x->a);
		case AST_CAST_VECTOR_ADDRESS:
			return gen(e, x->a);	/* a vector value is its address */
		case AST_CAST_BIT_CAST: {
			ast_qualified_type from = N(x->a)->ty;
			codegen_lvalue d = { .kind = CODEGEN_LVALUE_FRAME, .offset = slot(e, codegen_type_size(c, x->ty), codegen_type_alignment(c, x->ty)), .align = codegen_type_alignment(c, x->ty) };
			if (N(x->a)->flags & AST_NODE_FLAG_LVALUE) {
				codegen_lvalue src = gen_lval(e, x->a);
				if (src.dynamic_bit_slot) {
					codegen_value v = load(e, &src, from);
					store(e, &d, from, &v); val_free(e, &v); lfree(e, &src);
				} else if (src.is_bitfield) {
					/* Bounded byte pieces cover wide values and avoid aligned
					 * container loads beyond a packed object's final byte. */
					for (unsigned i = 0; i < codegen_type_size(c, from); i++) {
						unsigned bit = i * 8;
						codegen_value v = codegen_value_constant(0);
						codegen_lvalue byte = d;
						byte.offset += i;
						if (bit < src.width || src.is_signed) {
							codegen_lvalue part = src;
							unsigned start = bit < src.width ? bit : src.width - 1;
							part.offset += (src.bit_offset + start) / 8;
							part.bit_offset = (src.bit_offset + start) % 8;
							part.width = bit >= src.width ? 1 : src.width - bit < 8 ? src.width - bit : 8;
							part.is_signed = src.is_signed && bit + 8 >= src.width;
							v = load(e, &part, codegen_type_primitive(AST_TYPE_UNSIGNED_CHAR));
						}
						store(e, &byte, codegen_type_primitive(AST_TYPE_UNSIGNED_CHAR), &v); val_free(e, &v);
					}
					lfree(e, &src);

				} else {
					codegen_value p = laddr(e, &src), q = laddr(e, &d);
					copy_mem(e, toreg(e, &q), toreg(e, &p), codegen_type_size(c, from));
					val_free(e, &p); val_free(e, &q);
				}
			} else {
				codegen_value v = gen(e, x->a);
				store(e, &d, from, &v); val_free(e, &v);
			}
			if (codegen_type_is_aggregate(c, x->ty)) return laddr(e, &d);
			return load(e, &d, x->ty);
		}
		case AST_CAST_VECTOR_BITS: {
			/* the vector's bits as a scalar: load from its address */
			codegen_value v = gen(e, x->a);
			codegen_lvalue l = { .kind = CODEGEN_LVALUE_REGISTER, .align = codegen_type_alignment(c, N(x->a)->ty) };

			l.register_number = (uint8_t)toreg(e, &v);
			v = load(e, &l, x->ty);
			lfree(e, &l);
			return v;
		}
		case AST_CAST_SPLAT: {	/* the scalar into every lane of a new vector */
			ast_qualified_type et = T(c, x->ty)->base;
			uint64_t es = codegen_type_size(c, et), nl = T(c, x->ty)->n;
			int64_t sl = slot(e, codegen_type_size(c, x->ty), codegen_type_alignment(c, x->ty));
			codegen_value v = gen(e, x->a);
			int r;

			for (uint64_t i = 0; i < nl; i++) {
				codegen_lvalue l = { .kind = CODEGEN_LVALUE_FRAME, .offset = sl + (int64_t)(i * es), .align = codegen_type_alignment(c, et) };

				if (codegen_type_is_bool_vector(c, x->ty)) l.offset = sl + i / 8, l.is_bitfield = 1, l.width = 1, l.bit_offset = i % 8;
				store(e, &l, et, &v);
			}
			val_free(e, &v);
			r = treg(e);
			opi(e, I_ADDI, 1, r, FRAME, sl);
			return codegen_value_register(r);
		}
		case AST_CAST_UNION: {
			int64_t sl = slot(e, codegen_type_size(c, x->ty), codegen_type_alignment(c, x->ty));
			codegen_lvalue l = { .kind = CODEGEN_LVALUE_FRAME, .offset = sl, .align = codegen_type_alignment(c, x->ty) };
			codegen_value v = gen(e, x->a);
			int r;

			if (codegen_type_size(c, x->ty) > codegen_type_size(c, N(x->a)->ty))
				for (uint64_t o = 0; o < codegen_type_size(c, x->ty); o += 8)
					mem(e, I_ST, codegen_type_size(c, x->ty) - o >= 8 ? 3 : 0, 0, ZR, FRAME, sl + (int64_t)o);
			store(e, &l, N(x->a)->ty, &v);
			val_free(e, &v);
			r = treg(e);
			opi(e, I_ADDI, 1, r, FRAME, sl);
			return codegen_value_register(r);
		}
		default: {
			codegen_value v = gen(e, x->a);

			return cast(e, v, N(x->a)->ty, x->ty, x->aux);
		}
		}
	case AST_NODE_UNARY:
		return gen_unary(e, n);
	case AST_NODE_PRE_INCREMENT: case AST_NODE_PRE_DECREMENT: case AST_NODE_POST_INCREMENT: case AST_NODE_POST_DECREMENT:
		return gen_incdec(e, n);
	case AST_NODE_BINARY:
		return gen_binary(e, n);
	case AST_NODE_LOGICAL_AND:
	case AST_NODE_LOGICAL_OR:
		return gen_logical(e, n);
	case AST_NODE_ASSIGN:
		return gen_assign(e, n);
	case AST_NODE_CONDITIONAL:
		return gen_cond_expr(e, n);
	case AST_NODE_COMMA: {
		if (codegen_expression_has_side_effects(c, x->a)) {
			codegen_value v = gen(e, x->a);

			val_free(e, &v);
		}
		return gen(e, x->b);
	}
	case AST_NODE_CALL:
		return gen_call(e, n);
	case AST_NODE_BUILTIN:
		return gen_builtin(e, n);
	case AST_NODE_STATEMENT_EXPRESSION:
		return gen_block(e, x->a, true);
	case AST_NODE_VA_ARG:
		return va_arg_(e, x);
	case AST_NODE_ZERO:
		return codegen_type_is_bitint_big(c, x->ty) ? a64_bigconst(e, x->ty, NULL, 0) : codegen_value_constant(0);
	case AST_NODE_COMPLEX: {	/* a _Complex value: both parts into a temporary, its address */
		ast_qualified_type et = c->tags[T(c, x->ty)->aux].f[0].ty;
		uint64_t es = codegen_type_size(c, et);
		int64_t sl = slot(e, 2 * es, codegen_type_alignment(c, x->ty));
		codegen_lvalue d = { .kind = CODEGEN_LVALUE_FRAME, .offset = sl, .align = codegen_type_alignment(c, et) };
		codegen_value v = gen(e, x->a);
		int r;

		store(e, &d, et, &v);
		val_free(e, &v);
		v = gen(e, x->b);
		d.offset += (int32_t)es;
		store(e, &d, et, &v);
		val_free(e, &v);
		r = treg(e);
		opi(e, I_ADDI, 1, r, FRAME, sl);
		return codegen_value_register(r);
	}
	case AST_NODE_SIZEOF_VLA: {
		if (x->aux == 1) {
			vla_fix(e, x->b, false);
			return vla_bytes(e, x->b);
		}
		const sym *y = x->a && N(x->a)->op == AST_NODE_VARIABLE ? &c->syms[N(x->a)->a] : NULL;

		if (y && y->kind == AST_SYMBOL_VARIABLE && codegen_symbol_binding_get(e, N(x->a)->a)->kind == CODEGEN_BINDING_INDIRECT &&
		    codegen_symbol_binding_get(e, N(x->a)->a)->instance == e->function_instance->instance && (T(c, y->ty)->flags & AST_TYPE_FLAG_VLA)) {
			int r = treg(e);

			mem(e, I_LD, 3, 0, r, FRAME, codegen_symbol_binding_get(e, N(x->a)->a)->slot + 16);
			return codegen_value_register(r);
		}
		if (x->a) {
			codegen_value operand = gen(e, x->a);

			val_free(e, &operand);
		}
		return vla_bytes(e, x->b);
	}
	default:
		codegen_fatal_report(e, x->loc, "unsupported expression (node %d)", x->op);
	}
}
