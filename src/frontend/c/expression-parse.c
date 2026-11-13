/* vcc front end: expression parsers: unary, cast, binary, conditional, assignment, comma, sizeof */
#include "cc-internal.h"

/* ---------------- expressions: unary, cast, binary ---------------- */

/* r: the operand's range for a diagnostic; e (if any) gives it, else r.e is its last token's start */
static uint32_t cc_sizeof_type_build(cc *c, ast_qualified_type t, uint32_t loc, uint32_t e, diag_span r)
{
	int k = AST_TYPE_KIND(c, t);

	if (k == AST_TYPE_SVE_BUILTIN) {
		cc_report_error(c, loc, "invalid application of 'sizeof' to sizeless type '%s'", tyname(c, t));
		return cc_node_integer_new(c, 0, c->t_size, loc);
	}
	if (k == AST_TYPE_ARRAY && (AST_TYPE(c, t)->flags & AST_TYPE_FLAG_VLA)) {
		uint32_t n = cc_node_new(c, AST_NODE_SIZEOF_VLA, c->t_size, loc);

		AST_NODE(n)->a = e;
		AST_NODE(n)->b = t;
		return n;
	}
	if (k == AST_TYPE_VOID || k == AST_TYPE_FUNCTION) {
		CC_REPORT_EXTENSION_MICROSOFT(c, "sizeof", "pointer-arith", cc_warning_group_pedantic, loc, "invalid application of 'sizeof' to a %s type", k == AST_TYPE_VOID ? "void" : "function");
		return cc_node_integer_new(c, 1, c->t_size, loc);
	}
	if (!cc_type_complete(c, t)) {
		r = e ? cc_node_range(c, e) : (diag_span){ r.b, pp_token_end_find(c->p, r.e) };
		report(c, "error", loc, &r, 1, NULL, 0, "invalid application of 'sizeof' to an incomplete type '%s'", tyname(c, t));
		if (cc_type_is_record(c, t) || k == AST_TYPE_ENUM)
			cc_report_note(c, c->tags[AST_TYPE(c, t)->aux].loc, "forward declaration of '%s'", tyname(c, t));
	}
	return cc_node_integer_new(c, (i128)cc_type_size(c, t), c->t_size, loc);
}

static uint32_t cc_alignof_expression_build(cc *c, uint32_t e)
{
	const node *x = AST_NODE(e);
	uint32_t a = cc_type_alignment(c, x->ty);

	if (x->op == AST_NODE_MEMBER)
		return c->tags[x->b].f[x->c].align;
	if (x->op == AST_NODE_VARIABLE && c->syms[x->a].align > a)
		a = c->syms[x->a].align;
	if (cc_type_is_array(c, x->ty)) {
		ast_qualified_type t = x->ty;

		while (cc_type_is_array(c, t))
			t = AST_TYPE(c, t)->base;
		if (cc_type_alignment(c, t) > a)
			a = cc_type_alignment(c, t);
	}
	return a;
}

static uint32_t unary(cc *c);

/* the operand of sizeof/_Alignof: "( type-name )" gives UINT32_MAX, *ty and its
   span in *r (if r), anything else the (unevaluated) unary expression; '(' is
   consumed before looking at the type */
static uint32_t cc_sizeof_operand_parse(cc *c, ast_qualified_type *ty, diag_span *r)
{
	uint32_t n;

	if (cc_token_is(c, LEX_TOKEN_LEFT_PAREN)) {
		uint32_t ploc = tok(c)->loc;

		next(c);
		if (cc_token_is_type_start(c, tok(c))) {
			*ty = cc_type_name_parse(c);
			cc_token_expect_match(c, LEX_TOKEN_RIGHT_PAREN, ploc);
			if (r)
				*r = (diag_span){ ploc, c->prevloc };	/* resolved by cc_sizeof_type_build if needed */
			return UINT32_MAX;
		}
		c->unevaluated++;
		n = cc_postfix_parse(c, cc_parenthesized_parse(c, ploc));
		c->unevaluated--;
		return n;
	}
	c->unevaluated++;
	n = unary(c);
	c->unevaluated--;
	return n;
}

static __attribute__((noinline)) uint32_t cc_unary_operator_parse(cc *c)
{
	ptok *t = tok(c);
	uint32_t loc = t->loc, n;
	int kw = cc_token_keyword(t);

	switch (t->kind) {
	case LEX_TOKEN_PLUS_PLUS:
	case LEX_TOKEN_MINUS_MINUS: {
		int op = t->kind == LEX_TOKEN_PLUS_PLUS ? AST_NODE_PRE_INCREMENT : AST_NODE_PRE_DECREMENT;

		next(c);
		/* Parenthesized type-names may begin an lvalue compound literal. */
		n = cc_cast_expression_parse(c);
		if (AST_NODE_IS_ERROR(n))
			return n;
		cc_node_require_lvalue(c, n, t->loc);
		if (cc_type_is_complex(c, AST_NODE(n)->ty))
			return cc_complex_increment(c, n, op, loc);
		if ((AST_TYPE(c, AST_NODE(n)->ty)->flags & AST_TYPE_FLAG_ATOMIC) && cc_atomic_wide_type_is_wide(c, AST_NODE(n)->ty))
			return cc_atomic_wide_assignment_build(c, n, 0, op == AST_NODE_PRE_INCREMENT ? LEX_TOKEN_PLUS : LEX_TOKEN_MINUS, false, loc);
		cc_node_require_scalar(c, n, "increment");
		if (cc_type_is_pointer(c, AST_NODE(n)->ty) && AST_TYPE_KIND(c, AST_TYPE(c, AST_NODE(n)->ty)->base) == AST_TYPE_SVE_BUILTIN)
			cc_report_error(c, loc, "arithmetic on a pointer to sizeless type '%s'", tyname(c, AST_TYPE(c, AST_NODE(n)->ty)->base));
		{
			uint32_t r = cc_node_new(c, op, AST_QUALIFIED_TYPE_UNQUALIFIED(cc_type_unatomic(c, AST_NODE(n)->ty)), loc);

			AST_NODE(r)->a = n;
			return r;
		}
	}
	case LEX_TOKEN_AMPERSAND:
		next(c);
		c->after_amp = cc_token_is(c, LEX_TOKEN_STAR);
		n = cc_cast_expression_parse(c);
		if (AST_NODE_IS_ERROR(n))
			return n;
		cc_array_bounds_warn(c, n, true);
		return cc_address_of_build(c, n, loc);	/* (cc_address_of_build: &r of a register variable) */
	case LEX_TOKEN_STAR: {
		bool amp = c->after_amp;

		c->after_amp = false;
		next(c);
		n = cc_cast_expression_parse(c);
		if (AST_TYPE_KIND(c, AST_NODE(n)->ty) == AST_TYPE_FUNCTION || AST_NODE_IS_ERROR(n))
			return n;
		n = cc_dereference_build(c, n, loc);
		if (!amp && !c->unevaluated && AST_NODE(n)->op == AST_NODE_DEREFERENCE && AST_TYPE_KIND(c, AST_NODE(n)->ty) == AST_TYPE_VOID) {
			diag_span r = cc_node_range(c, AST_NODE(n)->a);

			cc_report_warning(c, "void-ptr-dereference", true, NULL, loc, &r, 1, NULL, 0,
				 "ISO C does not allow indirection on operand of type '%s'", tyname(c, AST_NODE(AST_NODE(n)->a)->ty));
		}
		return n;
	}
	case LEX_TOKEN_PLUS:
	case LEX_TOKEN_MINUS:
	case LEX_TOKEN_TILDE: {
		int op = t->kind;

		next(c);
		n = cc_cast_expression_parse(c);
		if (AST_NODE_IS_ERROR(n))
			return n;
		n = cc_node_rvalue(c, n);
		if (cc_type_is_complex(c, AST_NODE(n)->ty))
			return cc_complex_unary(c, op, n, loc);
		if (cc_type_is_vector(c, AST_NODE(n)->ty) ? op == LEX_TOKEN_TILDE && !cc_type_is_integer(c, AST_TYPE(c, AST_NODE(n)->ty)->base)
		    : !cc_type_is_arithmetic(c, AST_NODE(n)->ty) || (op == LEX_TOKEN_TILDE && !cc_type_is_integer(c, AST_NODE(n)->ty))) {
			diag_span r = cc_node_range(c, n);

			report(c, "error", loc, &r, 1, NULL, 0, "invalid argument type '%s' to unary expression", tyname(c, AST_NODE(n)->ty));
			return cc_node_error_new(c, loc);
		}
		{
			uint32_t r = cc_node_new(c, AST_NODE_UNARY, AST_NODE(n)->ty, loc);

			AST_NODE(r)->aux = (uint16_t)op;
			AST_NODE(r)->a = n;
			if (op == LEX_TOKEN_MINUS) {
				cc_node_span_set(c, r, loc);
				cc_expression_overflow_warn(c, op, r, loc, n, 0);
			}
			return cc_bitint_node_fold(c, r);
		}
	}
	case LEX_TOKEN_EXCLAMATION: {
		uint32_t r;

		next(c);
		n = cc_cast_expression_parse(c);
		if (AST_NODE_IS_ERROR(n))
			return n;
		if (cc_type_is_bool_vector(c, AST_NODE(n)->ty)) {
			r = cc_node_new(c, AST_NODE_UNARY, AST_QUALIFIED_TYPE_UNQUALIFIED(AST_NODE(n)->ty), loc);
			AST_NODE(r)->aux = LEX_TOKEN_TILDE, AST_NODE(r)->a = cc_node_rvalue(c, n);
			return r;
		}
		n = cc_condition_scalar(c, cc_node_rvalue(c, n), "'!'");
		r = cc_node_new(c, AST_NODE_UNARY, cc_type_basic(AST_TYPE_INT), loc);
		AST_NODE(r)->aux = LEX_TOKEN_EXCLAMATION;
		AST_NODE(r)->a = n;
		return cc_bitint_node_fold(c, r);
	}
	case LEX_TOKEN_AMPERSAND_AMPERSAND: {	/* &&label */
		uint32_t r;

		next(c);
		if (!cc_token_is(c, LEX_TOKEN_IDENTIFIER))
			cc_report_fatal(c, tok(c)->loc, "expected identifier");
		CC_REPORT_EXTENSION_MICROSOFT(c, "&&", "gnu-label-as-value", cc_warning_group_gnu, loc, "use of GNU address-of-label extension");
		r = cc_node_new(c, AST_NODE_LABEL_ADDRESS, cc_type_pointer(c, cc_type_basic(AST_TYPE_VOID)), loc);
		AST_NODE(r)->a = cc_label_reference(c, tok(c)->id, tok(c)->loc);
		c->labels[AST_NODE(r)->a].addr_taken = 1;
		next(c);
		return r;
	}
	default:
		break;
	}
	switch (kw) {
	case KW_sizeof: {
		ast_qualified_type ty;
		diag_span r;

		next(c);
		n = cc_sizeof_operand_parse(c, &ty, &r);
		if (n == UINT32_MAX) {
			if (cc_token_is(c, LEX_TOKEN_LEFT_BRACE)) {	/* sizeof (AST_TYPE){...} */
				uint32_t init, s, cl;

				init = cc_initializer_parse(c, &ty, !c->fn);
				s = cc_symbol_new(c, AST_SYMBOL_VARIABLE, 0, ty, loc);
				cl = cc_node_new_operands(c, AST_NODE_COMPOUND_LITERAL, ty, loc, init, s);
				AST_NODE(cl)->flags = AST_NODE_FLAG_LVALUE;
				n = cc_postfix_parse(c, cl);
				return cc_sizeof_type_build(c, AST_NODE(n)->ty, loc, n, (diag_span){ 0, 0 });
			}
			return cc_sizeof_type_build(c, ty, loc, 0, r);
		}
		if (cc_node_bitfield(c, n))
			cc_report_error(c, loc, "invalid application of 'sizeof' to bit-field");
		return cc_sizeof_type_build(c, AST_NODE(n)->ty, loc, n, (diag_span){ 0, 0 });
	}
	case KW__Alignof:
	case KW___alignof__:
	case KW___alignof: {
		ast_qualified_type ty;

		next(c);
		n = cc_sizeof_operand_parse(c, &ty, NULL);
		if (n == UINT32_MAX) {
			while (cc_type_is_array(c, ty))
				ty = AST_TYPE(c, ty)->base;
			if (AST_TYPE_KIND(c, ty) == AST_TYPE_VOID || AST_TYPE_KIND(c, ty) == AST_TYPE_FUNCTION)
				CC_REPORT_EXTENSION(c, "pointer-arith", cc_warning_group_pedantic, loc, "invalid application of 'alignof' to a %s type",
					 AST_TYPE_KIND(c, ty) == AST_TYPE_VOID ? "void" : "function");
			if (AST_TYPE_KIND(c, ty) == AST_TYPE_SVE_BUILTIN)
				cc_report_error(c, loc, "invalid application of 'alignof' to sizeless type '%s'", tyname(c, ty));
			return cc_node_integer_new(c, cc_type_alignment(c, ty), c->t_size, loc);
		}
		if (AST_TYPE_KIND(c, AST_NODE(n)->ty) == AST_TYPE_SVE_BUILTIN)
			cc_report_error(c, loc, "invalid application of 'alignof' to sizeless type '%s'", tyname(c, AST_NODE(n)->ty));
		return cc_node_integer_new(c, cc_alignof_expression_build(c, n), c->t_size, loc);
	}
	case KW___extension__:
		next(c);
		return cc_cast_expression_parse(c);
	case KW___real__: case KW___real:
	case KW___imag__: case KW___imag: {
		bool im = kw == KW___imag__ || kw == KW___imag;

		next(c);
		n = cc_cast_expression_parse(c);
		if (AST_NODE_IS_ERROR(n))
			return n;
		if (cc_type_is_complex(c, AST_NODE(n)->ty))
			return cc_complex_part(c, n, im);
		n = cc_node_rvalue(c, n);
		if (!cc_type_is_arithmetic(c, AST_NODE(n)->ty))
			cc_report_error(c, loc, "invalid type '%s' to %s operator", "", im ? "__imag" : "__real");
		if (im)	/* of a real value: 0 (the operand still evaluated) */
			return cc_node_new_operands(c, AST_NODE_COMMA, AST_QUALIFIED_TYPE_UNQUALIFIED(AST_NODE(n)->ty), loc, n, cc_lowering_zero(c, AST_NODE(n)->ty, loc));
		return n;
	}
	default:
		return cc_postfix_parse(c, cc_primary_parse(c));
	}
}

static uint32_t unary(cc *c)
{
	const ptok *t = tok(c);
	uint32_t b = t->loc, n;

	switch (t->kind) {
	case LEX_TOKEN_PLUS_PLUS: case LEX_TOKEN_MINUS_MINUS: case LEX_TOKEN_AMPERSAND: case LEX_TOKEN_STAR: case LEX_TOKEN_PLUS: case LEX_TOKEN_MINUS:
	case LEX_TOKEN_TILDE: case LEX_TOKEN_EXCLAMATION: case LEX_TOKEN_AMPERSAND_AMPERSAND:
		n = cc_unary_operator_parse(c);
		break;
	case LEX_TOKEN_IDENTIFIER:
		switch (cc_token_keyword(t)) {
		case KW_sizeof: case KW__Alignof: case KW___alignof__: case KW___alignof:
		case KW___extension__: case KW___real__: case KW___real: case KW___imag__: case KW___imag:
			n = cc_unary_operator_parse(c);
			break;
		default:
			n = cc_postfix_parse(c, cc_primary_parse(c));
			break;
		}
		break;
	default:
		n = cc_postfix_parse(c, cc_primary_parse(c));
		break;
	}
	cc_node_span_set(c, n, b);	/* the whole unary expression, for diagnostics */
	return n;
}

static uint32_t cc_explicit_cast_build(cc *c, uint32_t n, ast_qualified_type to, uint32_t loc)
{
	int k;
	uint32_t r;

	if (AST_NODE_IS_ERROR(n))
		return n;
	n = cc_conversion_decay(c, n);
	if (cc_type_is_void(c, to))
		return cc_cast_insert(c, n, AST_QUALIFIED_TYPE_UNQUALIFIED(to), AST_CAST_VOID);
	if (AST_TYPE_KIND(c, to) == AST_TYPE_UNION && !cc_type_is_compatible(c, to, AST_NODE(n)->ty)) {
		if (!cc_conversion_is_union_target(c, to, n)) {
			diag_span sp = cc_node_range(c, n);

			report(c, "error", loc, &sp, 1, NULL, 0, "cast to union type from type '%s' not present in union", tyname(c, AST_NODE(n)->ty));
		}
		return cc_cast_insert(c, n, AST_QUALIFIED_TYPE_UNQUALIFIED(to), AST_CAST_UNION);
	}
	if (cc_type_is_bool_vector(c, to) && cc_type_is_arithmetic(c, AST_NODE(n)->ty))
		return cc_conversion_apply(c, n, to); /* explicit scalar casts splat the converted boolean */
	if ((cc_type_is_bool_vector(c, to) || cc_type_is_bool_vector(c, AST_NODE(n)->ty)) && !cc_type_is_compatible(c, to, AST_NODE(n)->ty)) {
		cc_report_error(c, loc, "invalid conversion between vector type '%s' and type '%s'", tyname(c, to), tyname(c, AST_NODE(n)->ty));
		return cc_cast_insert(c, n, AST_QUALIFIED_TYPE_UNQUALIFIED(to), AST_CAST_NOP);
	}
	if (cc_type_is_vector(c, to) && cc_type_is_vector(c, AST_NODE(n)->ty) && AST_TYPE(c, to)->size == AST_TYPE(c, AST_NODE(n)->ty)->size)
		return cc_cast_insert(c, n, AST_QUALIFIED_TYPE_UNQUALIFIED(to), AST_CAST_NOP);
	if (cc_type_is_vector(c, to) != cc_type_is_vector(c, AST_NODE(n)->ty) && (cc_type_is_vector(c, to) || cc_type_is_scalar(c, to))) {
		/* GNU: a vector and a scalar of the same size reinterpret the bits */
		uint32_t from = cc_node_rvalue(c, n);

		if (cc_type_size(c, to) != cc_type_size(c, AST_NODE(from)->ty) || (!cc_type_is_vector(c, to) && !cc_type_is_arithmetic(c, to) && !cc_type_is_pointer(c, to))) {
			ast_qualified_type vt = cc_type_is_vector(c, to) ? to : AST_NODE(n)->ty, st = cc_type_is_vector(c, to) ? AST_NODE(n)->ty : to;

			cc_report_error(c, loc, "invalid conversion between vector type '%s' and %s type '%s' of different size",
				 tyname(c, vt), cc_type_is_integer(c, st) ? "integer" : "scalar", tyname(c, st));
			return cc_cast_insert(c, from, AST_QUALIFIED_TYPE_UNQUALIFIED(to), AST_CAST_NOP);	/* the cast's type: no follow-on errors (no code is generated) */
		}
		return cc_cast_insert(c, from, AST_QUALIFIED_TYPE_UNQUALIFIED(to), cc_type_is_vector(c, to) ? AST_CAST_UNION : AST_CAST_VECTOR_BITS);
	}
	if (cc_type_is_complex(c, to) || (cc_type_is_complex(c, AST_NODE(n)->ty) && cc_type_is_arithmetic(c, to))) {
		uint32_t r;

		if (!cc_type_is_arithmetic(c, AST_NODE(n)->ty) && !cc_type_is_complex(c, AST_NODE(n)->ty)) {
			cc_report_error(c, loc, "used type '%s' where arithmetic or pointer type is required", tyname(c, AST_NODE(n)->ty));
			return n;
		}
		r = cc_conversion_apply(c, cc_node_rvalue(c, n), AST_QUALIFIED_TYPE_UNQUALIFIED(to));
		return r;
	}
	if (!cc_type_is_scalar(c, to) && !cc_type_is_compatible(c, to, AST_NODE(n)->ty)) {
		cc_report_error(c, loc, "used type '%s' where arithmetic or pointer type is required", tyname(c, AST_NODE(n)->ty));
		return n;
	}
	if (cc_type_is_integer(c, AST_NODE(n)->ty))
		n = cc_node_rvalue(c, n);
	k = cc_conversion_cast_kind(c, AST_NODE(n)->ty, AST_QUALIFIED_TYPE_UNQUALIFIED(to), n);
	if (k < 0 && cc_type_is_scalar(c, to) && !cc_type_is_scalar(c, AST_NODE(n)->ty)) {
		diag_span r = cc_node_range(c, n);

		report(c, "error", r.b, &r, 1, NULL, 0, "operand of type '%s' where arithmetic or pointer type is required",
		       tyname(c, AST_NODE(n)->ty));
		n = cc_node_integer_new(c, 0, AST_QUALIFIED_TYPE_UNQUALIFIED(to), loc);
		AST_NODE(n)->flags |= AST_NODE_FLAG_ERROR;
		return n;
	}
	if (k < 0) {
		cc_report_error(c, loc, "invalid cast");
		return n;
	}
	if (k == AST_CAST_NULL_POINTER)
		k = AST_CAST_INTEGER_TO_POINTER;	/* keep (void *)0 recognisable as a cast */
	r = cc_cast_insert(c, n, AST_QUALIFIED_TYPE_UNQUALIFIED(to), k);
	AST_NODE(r)->loc = loc;
	return r;
}

/* Evaluate and capture only bounds introduced by this type-name. Typedef
   bounds already have their saved value from the declaration. A synthetic
   one-dimensional char array lets the existing VLA cache retain each bound. */
CC_PRIVATE uint32_t cc_vla_bounds_capture(cc *c, ast_qualified_type ty, uint32_t first, uint32_t value, uint32_t loc)
{
	uint32_t effects = 0;

	for (ast_qualified_type t = ty; cc_type_is_pointer(c, t) || cc_type_is_array(c, t); t = AST_TYPE(c, t)->base) {
		const ctype *a = AST_TYPE(c, t);
		if (a->kind == AST_TYPE_ARRAY && (a->flags & AST_TYPE_FLAG_VLA) && a->n >= first && a->n) {
			uint32_t bound = a->n;
			ast_qualified_type bt = cc_type_vla(c, cc_type_basic(AST_TYPE_CHAR), bound);
			uint32_t fix = cc_node_new(c, AST_NODE_SIZEOF_VLA, c->t_size, loc);

			AST_NODE(fix)->a = bound;
			AST_NODE(fix)->b = bt;
			AST_NODE(fix)->aux = 1; /* capture the bound before reading the cached size */
			effects = effects ? cc_node_new_operands(c, AST_NODE_COMMA, c->t_size, loc, effects, fix) : fix;
		}
	}
	if (!effects)
		return value;
	if (AST_NODE(value)->flags & AST_NODE_FLAG_LVALUE) {
		/* Sequence through the address so a compound literal stays an lvalue,
		   including when a later postfix operator modifies it. */
		ast_qualified_type vt = AST_NODE(value)->ty, pt = cc_type_pointer(c, vt);
		uint32_t address = cc_node_new_operands(c, AST_NODE_ADDRESS, pt, loc, value, 0);
		uint32_t sequence = cc_node_new_operands(c, AST_NODE_COMMA, pt, loc, effects, address);
		uint32_t result = cc_node_new_operands(c, AST_NODE_DEREFERENCE, vt, loc, sequence, 0);

		AST_NODE(result)->flags = AST_NODE(value)->flags;
		return result;
	}
	return cc_node_new_operands(c, AST_NODE_COMMA, AST_NODE(value)->ty, loc, effects, value);
}

/* (type-name) cast-expression or compound literal; '(' (at loc) is consumed */
static __attribute__((noinline)) uint32_t cc_cast_parenthesized_parse(cc *c, uint32_t loc)
{
	{
		uint32_t n, first_bound = c->nnode;
		int e0 = c->errors;
		ast_qualified_type ty;
		bool bad;

		ty = cc_type_name_parse(c);
		bad = c->errors != e0;	/* the type had its error: clang's invalid cast, silent */
		cc_token_expect_match(c, LEX_TOKEN_RIGHT_PAREN, loc);
		if (cc_token_is(c, LEX_TOKEN_LEFT_BRACE)) {	/* compound literal */
			uint32_t init, s;
			bool st = !c->fn;

			init = cc_initializer_parse(c, &ty, st);
			s = cc_symbol_new(c, AST_SYMBOL_VARIABLE, 0, ty, loc);
			c->syms[s].body = init;
			c->syms[s].sc = st ? AST_STORAGE_STATIC : AST_STORAGE_AUTO;
			if (st)
				c->syms[s].flags |= AST_SYMBOL_FLAG_GLOBAL | AST_SYMBOL_FLAG_DEFINED;
			n = cc_node_new_operands(c, AST_NODE_COMPOUND_LITERAL, ty, loc, init, s);
			AST_NODE(n)->flags = AST_NODE_FLAG_LVALUE;
			return cc_postfix_parse(c, cc_vla_bounds_capture(c, ty, first_bound, n, loc));
		}
		{
			uint32_t td = c->tn_tdef, r;

			n = cc_cast_expression_parse(c);
			if (unlikely(bad))
				return cc_node_error_new(c, loc);
			r = cc_explicit_cast_build(c, n, ty, loc);
			/* an explicit cast to a typedef in a body: its name on the node's free b (-Wformat) */
			if (td && c->fn && r != n && AST_NODE(r)->op == AST_NODE_CAST && !AST_NODE(r)->b)
				AST_NODE(r)->b = td;
			return cc_vla_bounds_capture(c, ty, first_bound, r, loc);
		}
	}
}

/* clang: "(doubl)x", a typo of a type keyword in parentheses before an operand,
   is reported with the keyword and parsed as a cast; '(' is consumed */
static bool cc_cast_is_parenthesized_type_typo(cc *c)
{
	ptok *t = tok(c);
	uint32_t kw;
	int k;

	if (t->kind != LEX_TOKEN_IDENTIFIER || cc_token_keyword(t) || cc_symbol_lookup(c, t->id) || cc_token_peek(c, 1)->kind != LEX_TOKEN_RIGHT_PAREN)
		return false;	/* declared: no peek */
	k = cc_token_peek(c, 2)->kind;
	if (k != LEX_TOKEN_IDENTIFIER && k != LEX_TOKEN_NUMBER && k != LEX_TOKEN_CHARACTER && k != LEX_TOKEN_LEFT_PAREN)
		return false;
	t = tok(c);
	if (!(kw = cc_typo_keyword_find(c, t->id, CC_TYPO_KEYWORDS_TYPE)) || !(cc_keyword_class[cc_keyword_map[kw]] & CC_KEYWORD_CLASS_STARTS_TYPE))
		return false;
	cc_undeclared_report(c, "use of undeclared identifier", t->id, t->loc, true, CC_TYPO_KEYWORDS_TYPE, &kw);
	tok(c)->id = kw;
	return true;
}

CC_PRIVATE uint32_t cc_cast_expression_parse(cc *c)
{
	uint32_t b = tok(c)->loc, n;

	if (vstack_guard_low())	/* every nested expression comes through here */
		cc_report_fatal(c, b, "expression nested too deeply");

	if (cc_token_is(c, LEX_TOKEN_LEFT_PAREN)) {
		/* consumed first: no lookahead token to shift out later */
		next(c);
		if (cc_token_is_type_start(c, tok(c)) || cc_cast_is_parenthesized_type_typo(c))
			n = cc_cast_parenthesized_parse(c, b);
		else
			n = cc_postfix_parse(c, cc_parenthesized_parse(c, b));
	} else {
		n = unary(c);
	}
	cc_node_span_set(c, n, b);	/* the whole cast expression, for diagnostics */
	return n;
}

static int cc_binary_precedence(int k)
{
	switch (k) {
	case LEX_TOKEN_STAR: case LEX_TOKEN_SLASH: case LEX_TOKEN_PERCENT: return 10;
	case LEX_TOKEN_PLUS: case LEX_TOKEN_MINUS: return 9;
	case LEX_TOKEN_LESS_LESS: case LEX_TOKEN_GREATER_GREATER: return 8;
	case LEX_TOKEN_LESS: case LEX_TOKEN_LESS_EQUAL: case LEX_TOKEN_GREATER: case LEX_TOKEN_GREATER_EQUAL: return 7;
	case LEX_TOKEN_EQUAL_EQUAL: case LEX_TOKEN_EXCLAMATION_EQUAL: return 6;
	case LEX_TOKEN_AMPERSAND: return 5;
	case LEX_TOKEN_CARET: return 4;
	case LEX_TOKEN_PIPE: return 3;
	case LEX_TOKEN_AMPERSAND_AMPERSAND: return 2;
	case LEX_TOKEN_PIPE_PIPE: return 1;
	default: return 0;
	}
}

static inline void cc_binary_self_compare_check(cc *c, int op, uint32_t l, uint32_t r, uint32_t loc)
{
	if (cc_type_is_float(c, AST_NODE(l)->ty) || cc_type_is_float(c, AST_NODE(r)->ty) || !cc_expression_is_same_reference(c, l, r, 0))
		return;
	cc_expression_self_compare_warn(c, op, l, r, loc);
}

static inline void cc_binary_pointer_compare_check(cc *c, int op, uint32_t l, uint32_t r)
{
	if (op != LEX_TOKEN_EQUAL_EQUAL && op != LEX_TOKEN_EXCLAMATION_EQUAL)
		return;
	if (cc_expression_is_null_pointer_constant(c, r) && !cc_expression_is_null_pointer_constant(c, l))
		cc_expression_null_compare_warn(c, op, l, r);
	else if (cc_expression_is_null_pointer_constant(c, l) && !cc_expression_is_null_pointer_constant(c, r))
		cc_expression_null_compare_warn(c, op, r, l);
}

static inline void cc_binary_range_compare_check(cc *c, uint32_t n, uint32_t loc)
{
	const node *x = AST_NODE(n);
	cc_value v;

	if (x->op != AST_NODE_BINARY || !cc_type_is_integer(c, AST_NODE(x->a)->ty) || !cc_type_is_integer(c, AST_NODE(x->b)->ty))
		return;
	{	/* cheap first: an operand narrower than the comparison (or bool) */
		uint64_t cw = cc_type_size(c, AST_NODE(x->a)->ty);
		ast_qualified_type ta = AST_NODE(cc_node_without_implicit(c, x->a))->ty, tb = AST_NODE(cc_node_without_implicit(c, x->b))->ty;
		bool na = cc_type_is_integer(c, ta) && (cc_type_size(c, ta) < cw || AST_TYPE_KIND(c, ta) == AST_TYPE_BOOL);
		bool nb = cc_type_is_integer(c, tb) && (cc_type_size(c, tb) < cw || AST_TYPE_KIND(c, tb) == AST_TYPE_BOOL);

		if (likely(!na && !nb))
			return;
		if (na && cc_eval(c, x->b, &v) && v.kind == CC_VALUE_INTEGER)
			cc_expression_range_compare_warn(c, n, 1, loc);
		else if (nb && cc_eval(c, x->a, &v) && v.kind == CC_VALUE_INTEGER)
			cc_expression_range_compare_warn(c, n, 0, loc);
	}
}

static uint32_t cc_binary_parse(cc *c, int minp);

static __attribute__((noinline)) uint32_t cc_binary_operators_parse(cc *c, int minp, uint32_t l, uint32_t b)
{
	for (;;) {
		int op = tok(c)->kind, p = cc_binary_precedence(op);
		uint32_t loc, r;

		if (!p || p < minp)
			return l;
		bool mac = tok(c)->flags & PP_TOKEN_FLAG_FROM_MACRO;

		loc = tok(c)->loc;
		next(c);
		{
			/* 0 && r, 1 || r: r is not evaluated (no runtime-behaviour warnings) */
			cc_value cv;
			bool dead = (op == LEX_TOKEN_AMPERSAND_AMPERSAND || op == LEX_TOKEN_PIPE_PIPE) && cc_eval(c, l, &cv) && cv.kind == CC_VALUE_INTEGER &&
				    (cv.i != 0) == (op == LEX_TOKEN_PIPE_PIPE);

			bool lg = op == LEX_TOKEN_AMPERSAND_AMPERSAND || op == LEX_TOKEN_PIPE_PIPE;

			c->dead += dead;
			c->cnd += lg;	/* (a conditional branch: -Wsometimes-uninitialized's filter) */
			r = cc_binary_parse(c, p + 1);
			c->cnd -= lg;
			c->dead -= dead;
		}
		if (unlikely(AST_NODE_IS_ERROR(l) || AST_NODE_IS_ERROR(r))) {
			l = cc_node_error_new(c, loc);
			if (mac)
				AST_NODE(l)->flags |= AST_NODE_FLAG_FROM_MACRO;
			cc_node_span_set(c, l, b);
		} else {
			uint32_t l0 = l;

			if ((op == LEX_TOKEN_AMPERSAND_AMPERSAND || op == LEX_TOKEN_PIPE_PIPE) && !mac)
				cc_expression_logical_constant_check(c, op, l, r, loc);
			else if (cc_operator_is_comparison(op) && !mac) {
				cc_binary_self_compare_check(c, op, l, r, loc);
				if (unlikely(cc_type_is_pointer(c, AST_NODE(l)->ty) || cc_type_is_pointer(c, AST_NODE(r)->ty) || cc_type_is_array(c, AST_NODE(l)->ty) || cc_type_is_array(c, AST_NODE(r)->ty) ||
					     cc_type_is_function(c, AST_NODE(l)->ty) || cc_type_is_function(c, AST_NODE(r)->ty)))
					cc_binary_pointer_compare_check(c, op, l, r);
			}
			cc_expression_runtime_warn(c, op, l, r, loc);
			l = cc_bitint_node_fold(c, cc_binary_build(c, op, l, r, loc));
			if (mac)
				AST_NODE(l)->flags |= AST_NODE_FLAG_FROM_MACRO;
			cc_node_span_set(c, l, b);
			if (cc_operator_is_comparison(op) && !mac && !cc_report_runtime_is_quiet(c))
				cc_binary_range_compare_check(c, l, loc);
			if (op == LEX_TOKEN_PLUS || op == LEX_TOKEN_MINUS || op == LEX_TOKEN_STAR)
				cc_expression_overflow_warn(c, op, l, loc, l0, r);
			else if ((AST_NODE(l0)->flags | AST_NODE(r)->flags) & AST_NODE_FLAG_OVERFLOW_REPORTED)
				AST_NODE(l)->flags |= AST_NODE_FLAG_OVERFLOW_REPORTED;
		}
	}
}

static uint32_t cc_binary_parse(cc *c, int minp)
{
	uint32_t b = tok(c)->loc, l = cc_cast_expression_parse(c);
	int p = cc_binary_precedence(tok(c)->kind);

	if (likely(!p || p < minp))
		return l;
	return cc_binary_operators_parse(c, minp, l, b);
}

static __attribute__((noinline)) uint32_t cc_conditional_rest_parse(cc *c, uint32_t cnd)
{
	{
		uint32_t loc = tok(c)->loc, a = 0, b;

		cc_value cv;
		int k;

		next(c);
		if (!cc_type_is_bool_vector(c, AST_NODE(cnd)->ty))
			cnd = cc_condition_scalar(c, cc_conversion_decay(c, cnd), "conditional");
		k = cc_eval(c, cnd, &cv) && cv.kind == CC_VALUE_INTEGER ? cv.i != 0 : -1;
		c->dead += k == 0;
		c->cnd++;
		if (!cc_token_is(c, LEX_TOKEN_COLON))
			a = expr(c);
		else
			CC_REPORT_EXTENSION_MICROSOFT(c, ":", "gnu-conditional-omitted-operand", cc_warning_group_gnu, tok(c)->loc,
				 "use of GNU ?: conditional expression extension, omitting middle operand");
		c->dead -= k == 0;
		cc_token_expect(c, LEX_TOKEN_COLON, "':'");
		c->dead += k == 1;
		b = cc_conditional_parse(c);
		c->cnd--;
		c->dead -= k == 1;
		if (AST_NODE_IS_ERROR(cnd) || (a && AST_NODE_IS_ERROR(a)) || AST_NODE_IS_ERROR(b))
			return cc_node_error_new(c, loc);
		return cc_conditional_build(c, cnd, a, b, loc);
	}
}

CC_PRIVATE uint32_t cc_conditional_parse(cc *c)
{
	uint32_t cnd = cc_binary_parse(c, 1);

	if (likely(!cc_token_is(c, LEX_TOKEN_QUESTION)))
		return cnd;
	{
		uint32_t b = cc_node_range(c, cnd).b, n = cc_conditional_rest_parse(c, cnd);

		cc_node_span_set(c, n, b);	/* (clang: a ?: from its condition) */
		return n;
	}
}

static int cc_assignment_operator(int k)
{
	switch (k) {
	case LEX_TOKEN_EQUAL: return -1;
	case LEX_TOKEN_STAR_EQUAL: return LEX_TOKEN_STAR;
	case LEX_TOKEN_SLASH_EQUAL: return LEX_TOKEN_SLASH;
	case LEX_TOKEN_PERCENT_EQUAL: return LEX_TOKEN_PERCENT;
	case LEX_TOKEN_PLUS_EQUAL: return LEX_TOKEN_PLUS;
	case LEX_TOKEN_MINUS_EQUAL: return LEX_TOKEN_MINUS;
	case LEX_TOKEN_LESS_LESS_EQUAL: return LEX_TOKEN_LESS_LESS;
	case LEX_TOKEN_GREATER_GREATER_EQUAL: return LEX_TOKEN_GREATER_GREATER;
	case LEX_TOKEN_AMPERSAND_EQUAL: return LEX_TOKEN_AMPERSAND;
	case LEX_TOKEN_CARET_EQUAL: return LEX_TOKEN_CARET;
	case LEX_TOKEN_PIPE_EQUAL: return LEX_TOKEN_PIPE;
	default: return 0;
	}
}

static __attribute__((noinline)) uint32_t cc_assignment_rest_parse(cc *c, uint32_t l, int op)
{
	uint32_t loc = tok(c)->loc, r;
	bool mac = tok(c)->flags & PP_TOKEN_FLAG_FROM_MACRO;

	next(c);
	{
		uint32_t pend = c->uv_asg;

		c->uv_asg = 0;
		r = cc_assignment_parse(c);
		if (unlikely(pend)) {
			c->syms[pend].flags &= ~AST_SYMBOL_FLAG_UNINITIALIZED_PENDING;	/* (after the right side: "z = z + 1" reads z first) */
			c->uv_asg = 0;
		}
	}
	if (AST_NODE_IS_ERROR(l) || AST_NODE_IS_ERROR(r))
		return cc_node_error_new(c, loc);
	if (op < 0 && !mac && !(c->prevflags & PP_TOKEN_FLAG_FROM_MACRO))	/* (the right side's last token) */
		cc_expression_self_assign_warn(c, l, r, loc);
	r = cc_assignment_build(c, op < 0 ? 0 : op, l, r, loc);
	if (mac)
		AST_NODE(r)->flags |= AST_NODE_FLAG_FROM_MACRO;
	return r;
}

CC_PRIVATE uint32_t cc_assignment_parse(cc *c)
{
	uint32_t l = cc_conditional_parse(c);
	int op = cc_assignment_operator(tok(c)->kind);

	if (likely(!op))
		return l;
	return cc_assignment_rest_parse(c, l, op);
}

static __attribute__((noinline)) uint32_t cc_comma_rest_parse(cc *c, uint32_t l)
{
	while (cc_token_is(c, LEX_TOKEN_COMMA) && !cc_token_is_not_expression_start(c, cc_token_peek(c, 1))) {
		uint32_t loc = tok(c)->loc, r;

		next(c);
		r = cc_assignment_parse(c);
		if (unlikely(AST_NODE_IS_ERROR(l) || AST_NODE_IS_ERROR(r))) {
			l = cc_node_error_new(c, loc);
			continue;
		}
		cc_unused_value_check(c, l, true);
		cc_unused_set_only_check(c, l);	/* (a discarded value: clang's IgnoredValueConversions) */
		l = cc_node_new_operands(c, AST_NODE_COMMA, AST_QUALIFIED_TYPE_UNQUALIFIED(AST_NODE(cc_conversion_decay(c, r))->ty), loc, l, cc_conversion_decay(c, r));
	}
	return l;
}

CC_PRIVATE uint32_t expr(cc *c)
{
	uint32_t l = cc_assignment_parse(c);

	if (likely(!cc_token_is(c, LEX_TOKEN_COMMA)))
		return l;
	return cc_comma_rest_parse(c, l);
}

/* a constant expression as an int (array sizes, widths, enum values, cases) */
CC_PRIVATE bool cc_constant_expression_parse(cc *c, i128 *v, uint32_t *np)
{
	uint32_t n;

	c->cexpr++;
	n = cc_node_rvalue(c, cc_conditional_parse(c));
	c->cexpr--;
	if (np)
		*np = n;
	cc_value wide;
	if (cc_type_is_bitint(c, AST_NODE(n)->ty) && cc_constant_eval(c, n, &wide) && wide.kind == CC_VALUE_BITINT) {
		if (wide.ice && cc_bitint_conversion_is_exact(c, &wide, AST_NODE(n)->ty, cc_type_basic(AST_TYPE_INT128))) {
			*v = wide.i;
			return true;
		}
		cc_report_error(c, AST_NODE(n)->loc, "wide integer constant is not representable in this 128-bit constant-expression context");
		*v = 0;
		return false;
	}
	if (!cc_constant_integer(c, n, v)) {
		cc_report_error(c, AST_NODE(n)->loc, "expression is not an integer constant expression");
		*v = 0;
		return false;
	}
	return true;
}
