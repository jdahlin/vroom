/*
 * expression.c: C++ expressions (cxx-internal.h), by precedence climbing.
 *
 * Inside template arguments (c->in_template_args) a > or >> ends the
 * argument instead of being an operator.  A name followed by < starts
 * template arguments only if lookup says it is a template, or (C++20) if
 * lookup finds nothing and the arguments parse and are followed by (.
 */
#include "cxx-internal.h"

static void cxx_cast_expression_parse(cxx *c);
static void cxx_unary_expression_parse(cxx *c);

static bool cxx_lookahead_fn(cxx *c, void (*fn)(cxx *))
{
	cxx_mark m;
	jmp_buf jb;
	volatile bool ok = false;

	cxx_mark_set(c, &m);
	c->tentative++;
	c->fail = &jb;
	if (!setjmp(jb)) {
		fn(c);
		ok = true;
	}
	c->tentative--;
	cxx_mark_rewind(c, &m);
	return ok;
}

/* ---- names in expressions ---------------------------------------------------------------- */

static void cxx_template_arguments_then_call(cxx *c)
{
	cxx_template_arguments_parse(c);
	if (!cxx_is(c, LEX_TOKEN_LEFT_PAREN))
		cxx_fail(c, "not a template-id call");
}

static cxx_entity *cxx_entity_kind_find(cxx_entity *e, int kind)
{
	for (; e; e = e->next)
		if (e->kind == kind)
			return e;
	return NULL;
}

static bool cxx_entity_has_template(cxx_entity *e)
{
	for (; e; e = e->next)
		if (e->kind == CXX_ENTITY_TEMPLATE || e->kind == CXX_ENTITY_FUNCTION_TEMPLATE ||
		    e->kind == CXX_ENTITY_VARIABLE_TEMPLATE || e->kind == CXX_ENTITY_CONCEPT)
			return true;
	return false;
}

/* operator, conversion and destructor names after a nested-name-specifier */
static void cxx_unqualified_id_rest_parse(cxx *c)
{
	if (cxx_accept_kw(c, CXX_KW_operator)) {
		int k = cxx_kind(c);

		if (cxx_is_kw(c, CXX_KW_new) || cxx_is_kw(c, CXX_KW_delete)) {
			cxx_advance(c);
			if (cxx_accept(c, LEX_TOKEN_LEFT_SQUARE))
				cxx_expect(c, LEX_TOKEN_RIGHT_SQUARE);
		} else if (k == LEX_TOKEN_LEFT_PAREN) {
			cxx_advance(c);
			cxx_expect(c, LEX_TOKEN_RIGHT_PAREN);
		} else if (k == LEX_TOKEN_LEFT_SQUARE) {
			cxx_advance(c);
			cxx_expect(c, LEX_TOKEN_RIGHT_SQUARE);
		} else if (k == LEX_TOKEN_STRING) {
			cxx_advance(c);
			cxx_accept(c, LEX_TOKEN_IDENTIFIER);
		} else if ((k >= LEX_TOKEN_PERIOD && k <= LEX_TOKEN_COMMA && k != LEX_TOKEN_SEMICOLON && k != LEX_TOKEN_COLON &&
			    k != LEX_TOKEN_QUESTION && k != LEX_TOKEN_COLON_COLON && k != LEX_TOKEN_PERIOD && k != LEX_TOKEN_ELLIPSIS) ||
			   k == CXX_TOKEN_ARROW_STAR || k == CXX_TOKEN_SPACESHIP) {
			uint32_t n;

			cxx_operator_at(c, &n);	/* operator>> and friends: joined pieces */
			while (n--)
				cxx_advance(c);
		} else
			cxx_type_id_parse(c);	/* conversion function */
		if (cxx_is(c, LEX_TOKEN_LESS) && cxx_lookahead_fn(c, cxx_template_arguments_then_call))
			cxx_template_arguments_parse(c);
		return;
	}
	if (cxx_accept(c, LEX_TOKEN_TILDE)) {
		if (cxx_is_kw(c, CXX_KW_decltype)) {
			cxx_advance(c);
			cxx_skip_balanced(c);
			return;
		}
		cxx_expect(c, LEX_TOKEN_IDENTIFIER);
		if (cxx_is(c, LEX_TOKEN_LESS))
			cxx_template_arguments_parse(c);
		return;
	}
	cxx_fail(c, "expected an identifier");
}

/* what a name used as a value is: a variable's type (an lvalue), or a set of functions for a call */
static void cxx_name_type(cxx *c, cxx_entity *e)
{
	cxx_entity *v = NULL;

	for (cxx_entity *x = e; x; x = x->next) {
		if (x->kind == CXX_ENTITY_FUNCTION_TEMPLATE || (x->kind == CXX_ENTITY_VALUE && x->ty != CXX_TY_UNKNOWN &&
								  AST_TYPE_KIND(c->ast, x->ty) == TY_FUNC)) {
			cxx_et(c, CXX_TY_UNKNOWN, false);
			c->ecallee = e;	/* functions: the call picks one */
			return;
		}
		if (x->kind == CXX_ENTITY_VALUE && !v)
			v = x;
	}
	cxx_et(c, v ? v->ty : CXX_TY_UNKNOWN, true);
	c->edecl_name = 1;
	if (v && v->has_value && !v->placeholder)
		c->evalue = v->value, c->econst = 1;	/* a constant (a const or constexpr integer, a value parameter) */
	if (v)
		cxx_gen_variable(c, v, c->tok[c->pos ? c->pos - 1 : 0].loc);
	if (v && v->has_value && !v->placeholder && !v->sym && !c->enode && c->gen && v->ty != CXX_TY_UNKNOWN) {
		/* an enumerator (or a constant with no object): its value, of the enumeration's underlying type */
		qty vt = AST_TYPE_KIND(c->ast, v->ty) == TY_ENUM ? AST_TYPE(c->ast, v->ty)->base : QUNQ(v->ty);

		cxx_gen_integer(c, v->value, vt, c->tok[c->pos ? c->pos - 1 : 0].loc);
	}
	if (v && v->field_of && c->gen_this && !c->enode && cxx_gen_on(c)) {	/* a data member of this */
		uint32_t loc = c->tok[c->pos ? c->pos - 1 : 0].loc;

		cxx_gen_this(c, loc);
		cxx_gen_member(c, c->enode, true, v->name, loc);
	}
}

/*
 * id-expression (possibly qualified, a template-id), or a type followed by ( or { (a
 * functional cast).  Called at an identifier, :: or a keyword that may start one.
 */
static void cxx_id_expression_parse(cxx *c)
{
	cxx_scope *q;
	bool dep, template_kw;
	uint32_t save = c->pos;
	cxx_entity *e;
	cxx_token *t;

	uint32_t ename = 0;

	cxx_et(c, CXX_TY_UNKNOWN, false);
	cxx_nested_name_parse(c, &q, &dep);
	template_kw = cxx_accept_kw(c, CXX_KW_template);
	t = cxx_peek(c, 0);
	if (t->kind != LEX_TOKEN_IDENTIFIER || (t->kw && t->kw != CXX_KW_operator)) {
		if (t->kind == LEX_TOKEN_TILDE || t->kw == CXX_KW_operator) {
			cxx_unqualified_id_rest_parse(c);
			cxx_et(c, CXX_TY_UNKNOWN, false);
			return;
		}
		cxx_fail(c, "expected an expression");
	}
	if (t->kw == CXX_KW_operator) {
		/* operator<<(a, b), written as a call: the operator functions by that name (and, at the call,
		   those in the arguments' namespaces); no member, no built-in */
		uint32_t oid = 0;

		if (!dep && !template_kw) {
			uint32_t at = c->pos, on;

			c->pos++;
			if (cxx_kind(c) >= LEX_TOKEN_LEFT_SQUARE && cxx_kind(c) < LEX_TOKEN_HASH && !cxx_is(c, LEX_TOKEN_LEFT_PAREN) &&
			    !cxx_is(c, LEX_TOKEN_LEFT_SQUARE)) {
				cxx_operator_at(c, &on);
				oid = cxx_operator_id_at(c, on);
			}
			c->pos = at;
		}
		cxx_unqualified_id_rest_parse(c);
		cxx_et(c, CXX_TY_UNKNOWN, false);
		if (oid > 1 && !c->tentative) {
			cxx_entity *oe = q ? cxx_lookup_in(c, q, oid, 0) : cxx_lookup(c, oid);

			if (oe && oe->kind != CXX_ENTITY_DEPENDENT)
				cxx_name_type(c, oe);
			if (!q)
				c->ename = oid, c->ename_missing = !oe;
		}
		return;
	}
	if (dep)
		e = cxx_entity_dependent();
	else if (q) {
		e = cxx_lookup_in(c, q, t->id, 0);
		if (!e && q->owner)
			for (uint32_t i = 0; i < q->owner->nspecs && !e; i++)
				e = cxx_lookup_in(c, q->owner->specs[i], t->id, 0);
	} else
		e = cxx_lookup(c, t->id);
	cxx_advance(c);
	if (cxx_is(c, LEX_TOKEN_LESS)) {
		bool is_template = template_kw || cxx_entity_has_template(e) ||
				   (!q && !dep && cxx_builtin_kind(t->id) == CXX_BUILTIN_TEMPLATE);

		/* C++20: an unqualified name lookup does not find (or finds a function) followed by <...>( */
		if (!is_template && !q && !dep && (!e || cxx_entity_kind_find(e, CXX_ENTITY_VALUE)) &&
		    cxx_lookahead_fn(c, cxx_template_arguments_then_call))
			is_template = true;
		if (is_template) {
			cxx_template_arguments_parse(c);
			cxx_et(c, CXX_TY_UNKNOWN, false);
			/* a variable template: its value for these arguments */
			if (!dep && e && !c->tentative && !c->template_depth) {
				cxx_entity *ve = NULL;
				int64_t v;

				for (cxx_entity *x = e; x && !ve; x = x->next)
					if (x->kind == CXX_ENTITY_VARIABLE_TEMPLATE && x->ftmpl)
						ve = x;
				if (ve && cxx_vtemplate_value(c, ve, c->targs, c->ntargs, &v)) {
					if (ve->ty != CXX_TY_UNKNOWN && AST_TYPE_KIND(c->ast, ve->ty) >= TY_BOOL && AST_TYPE_KIND(c->ast, ve->ty) <= TY_ULLONG)
						cxx_ec(c, QUNQ(ve->ty), v);
					else
						c->evalue = v, c->econst = 1;	/* (its type: the parameter's, not known here) */
					return;
				}
			}
			/* Concept<args>: a bool, its value when the arguments are known */
			if (!dep && e && cxx_entity_kind_find(e, CXX_ENTITY_CONCEPT)) {
				int v = c->template_depth || c->tentative ? -1 :
					cxx_concept_value(c, cxx_entity_kind_find(e, CXX_ENTITY_CONCEPT), c->targs, c->ntargs);

				if (v >= 0)
					cxx_ec(c, cc_type_basic(TY_BOOL), v);
				else
					cxx_et(c, cc_type_basic(TY_BOOL), false);
				return;
			}
			/* f<args>: a call takes the explicit arguments (a function template's) */
			if (!dep && e && cxx_entity_kind_find(e, CXX_ENTITY_FUNCTION_TEMPLATE) && !c->tentative) {
				if (c->ntargs > c->cxargs)
					c->xargs = realloc(c->xargs, (c->cxargs = c->ntargs + 8) * sizeof *c->xargs);
				memcpy(c->xargs, c->targs, c->ntargs * sizeof *c->xargs);
				c->nxargs = c->ntargs, c->xargs_set = 1, c->ecallee = e;
			}
			return;
		}
	}
	if (!q && !dep && !template_kw)
		ename = t->id;
	/* X::y with X a complete class that has no y: ill-formed (a substitution fails) */
	if (q && !dep && !e && q->kind == CXX_SCOPE_CLASS && !q->dependent && !q->has_dependent_base && !c->template_depth)
		CXX_INVALID(c);
	if (!dep && e)
		cxx_name_type(c, e);
	else if (!e && !q && t->len > 10 && !memcmp(t->s, "__builtin_", 10) && !c->tentative &&
		 ((t->len == 18 && !memcmp(t->s + 10, "va_start", 8)) || (t->len == 16 && !memcmp(t->s + 10, "va_end", 6)) ||
		  (t->len == 17 && !memcmp(t->s + 10, "va_copy", 7)) || (t->len == 21 && !memcmp(t->s + 10, "unreachable", 11)) ||
		  (t->len == 14 && !memcmp(t->s + 10, "trap", 4)) || (t->len == 18 && !memcmp(t->s + 10, "prefetch", 8))))
		cxx_et(c, cxx_type_function(c, cc_type_basic(TY_VOID), NULL, 0, true), false);	/* builtins that return void */
	else
		cxx_et(c, CXX_TY_UNKNOWN, false);
	(void)save;
	c->ename = ename;	/* (after the cxx_et calls above, which clear it) */
	c->ename_missing = ename && !e;
}

/* is this the start of a type (for casts, sizeof, functional casts)? */
static bool cxx_starts_type(cxx *c)
{
	int kw = cxx_kw(c);

	if (kw == CXX_KW_typename || kw == CXX_KW_decltype || kw == CXX_KW___decltype || kw == CXX_KW_const ||
	    kw == CXX_KW_volatile || kw == CXX_KW_unsigned || kw == CXX_KW_signed || kw == CXX_KW_short || kw == CXX_KW_long ||
	    kw == CXX_KW_int || kw == CXX_KW_char || kw == CXX_KW_bool || kw == CXX_KW_float || kw == CXX_KW_double ||
	    kw == CXX_KW_void || kw == CXX_KW_wchar_t || kw == CXX_KW_char8_t || kw == CXX_KW_char16_t || kw == CXX_KW_char32_t ||
	    kw == CXX_KW_auto || kw == CXX_KW___int128 || kw == CXX_KW_struct || kw == CXX_KW_class || kw == CXX_KW_union ||
	    kw == CXX_KW_enum || kw == CXX_KW__Atomic || kw == CXX_KW___typeof__ || kw == CXX_KW___typeof || kw == CXX_KW_typeof ||
	    kw == CXX_KW___float128 || kw == CXX_KW___bf16 || kw == CXX_KW__Float16 || kw == CXX_KW___fp16 ||
	    kw == CXX_KW___signed || kw == CXX_KW___signed__ || kw == CXX_KW___const || kw == CXX_KW__Complex || kw == CXX_KW__BitInt)
		return true;
	return (cxx_is(c, LEX_TOKEN_IDENTIFIER) && !kw) || cxx_is(c, LEX_TOKEN_COLON_COLON);
}

static void cxx_type_id_then_close(cxx *c)
{
	cxx_type_id_parse(c);
	cxx_expect(c, LEX_TOKEN_RIGHT_PAREN);
}

/* ( type-id ) at the current (: a cast, sizeof(T), alignof(T) */
static bool cxx_paren_type_id_follows(cxx *c)
{
	cxx_mark m;
	bool ok;

	if (cxx_kind(c) != LEX_TOKEN_LEFT_PAREN)
		return false;
	cxx_mark_set(c, &m);
	cxx_advance(c);
	ok = cxx_starts_type(c) && cxx_lookahead_fn(c, cxx_type_id_then_close);
	cxx_mark_rewind(c, &m);
	return ok;
}

/* a type, then ( or { : a functional cast or a temporary */
static void cxx_type_then_init(cxx *c)
{
	cxx_type_specifiers_parse(c);
	if (!cxx_is(c, LEX_TOKEN_LEFT_PAREN) && !cxx_is(c, LEX_TOKEN_LEFT_BRACE))
		cxx_fail(c, "not a functional cast");
}


/* T(x), T{x}, T(): a prvalue of type T; an integer constant when x is one (T(): zero) */
static void cxx_functional_cast_type(cxx *c, qty cast_to)
{
	bool k = c->carg_const, zero = c->ncargs == 0;
	int64_t v = zero ? 0 : c->carg_value;
	uint32_t xn = c->ncargs == 1 ? c->carg_nodes[0] : 0;

	cxx_et(c, cast_to, false);
	if ((k || zero) && cast_to != CXX_TY_UNKNOWN &&
	    ((AST_TYPE_KIND(c->ast, cast_to) >= TY_BOOL && AST_TYPE_KIND(c->ast, cast_to) <= TY_ULLONG) || AST_TYPE_KIND(c->ast, cast_to) == TY_ENUM))
		cxx_ec(c, cast_to, v);
	else
		cxx_gen_cast(c, xn, cast_to, c->tok[c->pos ? c->pos - 1 : 0].loc);
}

/* ( arguments ) or { arguments }: their types in c->cargs (unknown if any is a pack expansion) */
static void cxx_arguments_parse(cxx *c, int open, int close);

VPRIV void cxx_call_arguments_parse(cxx *c)
{
	cxx_arguments_parse(c, LEX_TOKEN_LEFT_PAREN, LEX_TOKEN_RIGHT_PAREN);
}

VPRIV void cxx_braced_arguments_parse(cxx *c)
{
	cxx_arguments_parse(c, LEX_TOKEN_LEFT_BRACE, LEX_TOKEN_RIGHT_BRACE);
}

static void cxx_arguments_parse(cxx *c, int open, int close)
{
	qty args[16];
	uint8_t lval[16];
	uint32_t n = 0;
	uint32_t nodes[16] = { 0 };
	bool unknown = false, first_const = false;
	int64_t first_value = 0;

	cxx_expect(c, open);
	while (!cxx_is(c, close)) {
		uint32_t start = c->pos, seen = c->npacks_seen;
		cxx_entity *saved[8];

		uint8_t was_invalid = c->einvalid, was_unsure = c->eunsure, was_missing = c->member_missing;

		memcpy(saved, c->packs_seen, sizeof saved);
		c->npacks_seen = 0, c->packs_overflow = 0;
		cxx_initializer_parse(c);
		/* f(std::forward<Args>(args)...) in an instance: the argument once per element of its packs */
		if (cxx_is(c, LEX_TOKEN_ELLIPSIS) && c->npacks_seen && !c->packs_overflow && c->instantiating && !c->tentative &&
		    !c->template_depth) {
			cxx_entity *packs[8];
			uint32_t np = c->npacks_seen, len = c->packs_seen[0]->npack, end = c->pos;
			bool ok = true;

			memcpy(packs, c->packs_seen, sizeof packs);
			for (uint32_t k = 0; k < np; k++)
				ok &= packs[k]->npack == len && !packs[k]->pack_placeholder;
			if (ok) {
				/* (what the pattern said with its packs unexpanded says nothing of the elements) */
				c->einvalid = was_invalid, c->eunsure = was_unsure, c->member_missing = was_missing;
				for (uint32_t i = 0; i < len; i++) {
					for (uint32_t k = 0; k < np; k++)
						cxx_pack_element(c, packs[k], (int32_t)i);
					c->pos = start;
					cxx_initializer_parse(c);
					if (c->pos != end)
						unknown = true;
					if (c->etype == CXX_TY_UNKNOWN && !c->tentative)
						c->eunsure = 1;
					if (n < 16)
						args[n] = c->etype, lval[n] = c->elval;
					n++;
				}
				for (uint32_t k = 0; k < np; k++)
					cxx_pack_element(c, packs[k], -1);
				c->pos = end + 1;
				c->npacks_seen = seen;
				memcpy(c->packs_seen, saved, sizeof saved);
				if (!cxx_accept(c, LEX_TOKEN_COMMA))
					break;
				continue;
			}
		}
		/* not expanded here: its packs belong to an enclosing expansion, with the ones seen before */
		{
			cxx_entity *mine[8];
			uint32_t nmine = c->npacks_seen;

			memcpy(mine, c->packs_seen, sizeof mine);
			c->npacks_seen = seen;
			memcpy(c->packs_seen, saved, sizeof saved);
			for (uint32_t k = 0; k < nmine && k < 8; k++) {
				uint32_t j = 0;

				while (j < c->npacks_seen && c->packs_seen[j] != mine[k])
					j++;
				if (j == c->npacks_seen && j < 8)
					c->packs_seen[c->npacks_seen++] = mine[k];
			}
		}
		if (c->etype == CXX_TY_UNKNOWN && !c->tentative)
			c->eunsure = 1;	/* (the call may get a type whatever this argument is) */
		if (n < 16)
			args[n] = c->etype, lval[n] = c->elval, nodes[n] = c->enode;
		if (!n)
			first_const = c->econst, first_value = c->evalue;
		n++;
		if (cxx_accept(c, LEX_TOKEN_ELLIPSIS))
			unknown = true;
		if (!cxx_accept(c, LEX_TOKEN_COMMA))
			break;
	}
	cxx_expect(c, close);
	if (n > 16)
		unknown = true;
	c->carg_const = n == 1 && !unknown && first_const, c->carg_value = first_value;
	memcpy(c->carg_nodes, nodes, sizeof nodes);
	if (n > c->ccargs)
		c->cargs = realloc(c->cargs, (c->ccargs = n + 16) * sizeof *c->cargs),
		c->cargs_lval = realloc(c->cargs_lval, c->ccargs);
	memcpy(c->cargs, args, (n < 16 ? n : 16) * sizeof *args);
	if (n)
		memcpy(c->cargs_lval, lval, n < 16 ? n : 16);
	c->ncargs = unknown ? UINT32_MAX : n;
}

/* a builtin trait: __is_same(T, U), __builtin_xxx(args); arguments are types or expressions */
static void cxx_builtin_call_parse(cxx *c)
{
	qty types[2];
	uint32_t n = 0;
	uint32_t bn[4], nb = 0;	/* (code generation: the expression arguments' nodes) */
	qty first = CXX_TY_UNKNOWN;

	cxx_advance(c);
	cxx_expect(c, LEX_TOKEN_LEFT_PAREN);
	while (!cxx_is(c, LEX_TOKEN_RIGHT_PAREN)) {
		/* Args...: a bound pack of types, each a type argument (__is_constructible(T, Args...)) */
		if (cxx_is(c, LEX_TOKEN_IDENTIFIER) && !cxx_kw(c) && cxx_peek(c, 1)->kind == LEX_TOKEN_ELLIPSIS && !c->tentative &&
		    !c->template_depth) {
			cxx_entity *pk = cxx_lookup(c, cxx_peek(c, 0)->id);

			if (pk && pk->kind == CXX_ENTITY_TYPE && pk->is_pack && !pk->pack_placeholder && pk->pack_index < 0) {
				for (uint32_t i = 0; i < pk->npack; i++)
					if (n < 2 && pk->pack[i].kind == 1 && pk->pack[i].t != CXX_TY_UNKNOWN)
						types[n++] = pk->pack[i].t;
					else
						n = 3;
				c->npacks_seen = 0;	/* (expanded here) */
				cxx_advance(c);
				cxx_advance(c);
				if (!cxx_accept(c, LEX_TOKEN_COMMA))
					break;
				continue;
			}
		}
		if (cxx_try_type_id(c)) {
			uint32_t start = c->pos, seen = c->npacks_seen;

			c->npacks_seen = 0;
			cxx_type_id_parse(c);
			if (cxx_is(c, LEX_TOKEN_ELLIPSIS) && c->npacks_seen == 1 && !c->packs_seen[0]->pack_placeholder && c->instantiating &&
			    !c->tentative && !c->template_depth) {
				/* __is_constructible(T, Args...): the pack's elements, each a type argument */
				cxx_entity *pk = c->packs_seen[0];
				uint32_t end = c->pos;

				for (uint32_t i = 0; i < pk->npack; i++) {
					cxx_pack_element(c, pk, (int32_t)i);
					c->pos = start;
					cxx_type_id_parse(c);
					if (n < 2 && c->type_out != CXX_TY_UNKNOWN && c->pos == end)
						types[n++] = c->type_out;
					else
						n = 3;
				}
				cxx_pack_element(c, pk, -1);
				c->pos = end + 1;
				c->npacks_seen = seen;
				if (!cxx_accept(c, LEX_TOKEN_COMMA))
					break;
				continue;
			}
			c->npacks_seen = seen;
			if (n < 2 && c->type_out != CXX_TY_UNKNOWN)
				types[n++] = c->type_out;
			else
				n = 3;
		} else {
			cxx_initializer_parse(c);
			if (!nb && n == 0)
				first = c->etype;
			if (nb < 4 && nb != UINT32_MAX)
				bn[nb++] = c->enode;
			else
				nb = UINT32_MAX;
			n = 3;
		}
		if (cxx_accept(c, LEX_TOKEN_ELLIPSIS))
			n = 3, nb = UINT32_MAX;
		if (!cxx_accept(c, LEX_TOKEN_COMMA))
			break;
	}
	cxx_expect(c, LEX_TOKEN_RIGHT_PAREN);
	c->nbnodes = n == 3 || n == 0 ? nb : UINT32_MAX;
	if (c->nbnodes != UINT32_MAX)
		memcpy(c->bnodes, bn, sizeof bn);
	c->bfirst = first;
	/* the type arguments, for the traits that are evaluated (3: something else) */
	c->nbtypes = n;
	if (n <= 2)
		memcpy(c->btypes, types, n * sizeof *types);
}

/* ---- lambdas and requires-expressions ---------------------------------------------------- */

static void cxx_lambda_parse(cxx *c)
{
	cxx_scope *ls = cxx_scope_new(c, CXX_SCOPE_BLOCK, c->scope, NULL);
	cxx_scope *outer = c->scope;
	uint32_t depth = c->class_depth, targs = c->in_template_args;
	/* a closure type is built for a lambda outside templates whose parameter types are known */
	bool typed = false, generic = false;
	qty params[16], ret = CXX_TY_UNKNOWN;
	uint32_t nparams = 0;
	uint8_t o_capture = c->ret_capture, o_lval = c->ret_lval, o_seen = c->ret_seen;
	qty o_type = c->ret_type;

	c->lambda_type = CXX_TY_UNKNOWN;
	cxx_skip_balanced(c);	/* [captures]: init-captures parse later with the body (names are values) */
	c->in_template_args = 0;
	cxx_scope_push(c, ls);
	cxx_attributes_skip(c);
	if (cxx_is(c, LEX_TOKEN_LESS)) {	/* []<typename T>(...) */
		generic = true;
		cxx_advance(c);
		c->template_depth++;
		while (!cxx_accept_greater(c)) {
			int kw = cxx_kw(c);

			if ((kw == CXX_KW_typename || kw == CXX_KW_class) && cxx_peek(c, 1)->kind != LEX_TOKEN_COLON_COLON) {
				cxx_advance(c);
				cxx_accept(c, LEX_TOKEN_ELLIPSIS);
				if (cxx_is(c, LEX_TOKEN_IDENTIFIER)) {
					cxx_entity *e = cxx_declare(c, ls, cxx_peek(c, 0)->id, CXX_ENTITY_TYPE);

					if (e)
						e->dependent = 1;
					cxx_advance(c);
				}
			} else
				cxx_advance(c);
			cxx_accept(c, LEX_TOKEN_COMMA);
		}
		c->template_depth--;
		cxx_requires_clause_parse(c);
	}
	if (!generic && !c->template_depth && !c->tentative &&
	    (!cxx_is(c, LEX_TOKEN_LEFT_PAREN) || cxx_lookahead_fn(c, cxx_lambda_parameters_parse))) {
		/* parameters as a function's: their names with their types */
		if (cxx_is(c, LEX_TOKEN_LEFT_PAREN)) {
			cxx_lambda_parameters_parse(c);
			typed = !c->fn_unknown && c->fn_nparams <= 16 && !c->fn_variadic;
			nparams = c->fn_nparams;
			for (uint32_t i = 0; i < nparams && i < 16; i++) {
				params[i] = c->fn_params[i];
				if (c->fn_pnames[i] > 1) {
					cxx_entity *e = cxx_declare(c, ls, c->fn_pnames[i], CXX_ENTITY_VALUE);

					if (e)
						e->ty = c->fn_pdecl[i];
				}
			}
		} else
			typed = true;	/* [] { ... }: no parameters */
	} else if (cxx_is(c, LEX_TOKEN_LEFT_PAREN)) {
		/* parameters: declare their names (for the body) */
		cxx_advance(c);
		while (!cxx_is(c, LEX_TOKEN_RIGHT_PAREN)) {
			uint32_t last_id = 0;
			int depth2 = 0;

			while (!(depth2 == 0 && (cxx_is(c, LEX_TOKEN_COMMA) || cxx_is(c, LEX_TOKEN_RIGHT_PAREN)))) {
				if (cxx_is(c, LEX_TOKEN_EOF))
					cxx_fail(c, "unterminated lambda parameters");
				if (cxx_is(c, LEX_TOKEN_LEFT_PAREN) || cxx_is(c, LEX_TOKEN_LEFT_SQUARE) || cxx_is(c, LEX_TOKEN_LEFT_BRACE) ||
				    cxx_is(c, LEX_TOKEN_LESS))
					depth2++;
				else if (cxx_is(c, LEX_TOKEN_RIGHT_PAREN) || cxx_is(c, LEX_TOKEN_RIGHT_SQUARE) ||
					 cxx_is(c, LEX_TOKEN_RIGHT_BRACE) || cxx_is(c, LEX_TOKEN_GREATER))
					depth2--;
				else if (cxx_is(c, LEX_TOKEN_GREATER_GREATER))
					depth2 -= 2;
				if (depth2 == 0 && cxx_is(c, LEX_TOKEN_IDENTIFIER) && !cxx_kw(c))
					last_id = cxx_peek(c, 0)->id;
				if (cxx_is(c, LEX_TOKEN_EQUAL) && depth2 == 0) {
					/* default argument: skip to , or ) */
					while (!(depth2 == 0 && (cxx_is(c, LEX_TOKEN_COMMA) || cxx_is(c, LEX_TOKEN_RIGHT_PAREN)))) {
						if (cxx_is(c, LEX_TOKEN_LEFT_PAREN) || cxx_is(c, LEX_TOKEN_LEFT_BRACE))
							depth2++;
						else if (cxx_is(c, LEX_TOKEN_RIGHT_PAREN) || cxx_is(c, LEX_TOKEN_RIGHT_BRACE))
							depth2--;
						cxx_advance(c);
					}
					break;
				}
				cxx_advance(c);
			}
			if (last_id)
				cxx_declare(c, ls, last_id, CXX_ENTITY_VALUE);
			if (!cxx_accept(c, LEX_TOKEN_COMMA))
				break;
		}
		cxx_expect(c, LEX_TOKEN_RIGHT_PAREN);
	}
	for (;;) {
		if (cxx_is_kw(c, CXX_KW_mutable) || cxx_is_kw(c, CXX_KW_constexpr) || cxx_is_kw(c, CXX_KW_consteval) ||
		    cxx_is_kw(c, CXX_KW_static)) {
			cxx_advance(c);
			continue;
		}
		if (cxx_is_kw(c, CXX_KW_noexcept)) {
			cxx_advance(c);
			if (cxx_is(c, LEX_TOKEN_LEFT_PAREN))
				cxx_skip_balanced(c);
			continue;
		}
		if ((cxx_is(c, LEX_TOKEN_LEFT_SQUARE) && cxx_peek(c, 1)->kind == LEX_TOKEN_LEFT_SQUARE) || cxx_is_kw(c, CXX_KW___attribute__)) {
			cxx_attributes_skip(c);
			continue;
		}
		break;
	}
	if (cxx_accept(c, LEX_TOKEN_ARROW)) {
		cxx_type_id_parse(c);
		ret = c->type_out;
		typed &= ret != CXX_TY_UNKNOWN;
	}
	cxx_requires_clause_parse(c);
	c->class_depth = 0;
	/* no trailing type: the body's first return (none: void); a lambda inside has its own */
	c->ret_capture = typed && ret == CXX_TY_UNKNOWN, c->ret_type = CXX_TY_UNKNOWN, c->ret_seen = 0;
	cxx_compound_statement_parse(c);
	if (typed && ret == CXX_TY_UNKNOWN) {
		if (c->ret_type == CXX_TY_UNKNOWN && !c->ret_seen)
			ret = cc_type_basic(TY_VOID);
		else if (c->ret_type != CXX_TY_UNKNOWN)
			ret = AST_TYPE_KIND(c->ast, c->ret_type) == TY_VOID ? c->ret_type :
			      cxx_auto_deduce(c, cxx_auto_type(c), c->ret_type, c->ret_lval);
	}
	c->ret_capture = o_capture, c->ret_lval = o_lval, c->ret_seen = o_seen, c->ret_type = o_type;
	c->class_depth = depth;
	c->scope = outer;
	c->in_template_args = targs;
	if (typed && ret != CXX_TY_UNKNOWN) {
		/* the closure type: a class of its own with operator()(parameters) (its captures: not laid out) */
		uint32_t tag = cc_tag_new(c->ast, TY_STRUCT, vintern_cstr("lambda"), 0);
		cxx_entity *owner = calloc(1, sizeof *owner), *call;
		cxx_scope *cs;

		owner->kind = CXX_ENTITY_TYPE, owner->name = vintern_cstr("lambda"), owner->pack_index = -1;
		owner->ty = QT(c->ast->tags[tag].ty, 0);
		cs = cxx_scope_new(c, CXX_SCOPE_CLASS, outer, owner);
		owner->members = cs;
		cxx_tag_scope_set(c, tag, cs);
		call = cxx_declare(c, cs, vintern_cstr("operator()"), CXX_ENTITY_VALUE);
		if (call) {
			call->ty = cxx_type_function(c, ret, params, nparams, false);
			call->fn_const = 1;
			c->lambda_type = owner->ty;
		}
	}
}

static void cxx_requires_expression_parse(cxx *c)
{
	cxx_scope *rs = cxx_scope_new(c, CXX_SCOPE_BLOCK, c->scope, NULL);
	cxx_scope *outer = c->scope;
	uint32_t targs = c->in_template_args;

	/* its value: every requirement holds (1), one fails for sure (0), or vcc cannot tell (-1) */
	bool eval = !c->template_depth && !c->tentative;
	int value = 1;

	cxx_advance(c);	/* requires */
	c->in_template_args = 0;
	cxx_scope_push(c, rs);
	if (eval && cxx_is(c, LEX_TOKEN_LEFT_PAREN) && cxx_lookahead_fn(c, cxx_lambda_parameters_parse)) {
		/* parameters as a function's: names with their types (lvalues in the requirements) */
		cxx_lambda_parameters_parse(c);
		for (uint32_t i = 0; i < c->fn_nparams; i++)
			if (c->fn_pnames[i] > 1) {
				cxx_entity *e = cxx_declare(c, rs, c->fn_pnames[i], CXX_ENTITY_VALUE);

				if (e)
					e->ty = c->fn_pdecl[i];
			}
	} else if (cxx_is(c, LEX_TOKEN_LEFT_PAREN)) {
		/* parameters: type and name pairs */
		cxx_advance(c);
		while (!cxx_is(c, LEX_TOKEN_RIGHT_PAREN)) {
			uint32_t last_id = 0;
			int depth = 0;

			while (!(depth == 0 && (cxx_is(c, LEX_TOKEN_COMMA) || cxx_is(c, LEX_TOKEN_RIGHT_PAREN)))) {
				if (cxx_is(c, LEX_TOKEN_EOF))
					cxx_fail(c, "unterminated requires parameters");
				if (cxx_is(c, LEX_TOKEN_LEFT_PAREN) || cxx_is(c, LEX_TOKEN_LESS))
					depth++;
				else if (cxx_is(c, LEX_TOKEN_RIGHT_PAREN) || cxx_is(c, LEX_TOKEN_GREATER))
					depth--;
				else if (cxx_is(c, LEX_TOKEN_GREATER_GREATER))
					depth -= 2;
				if (depth == 0 && cxx_is(c, LEX_TOKEN_IDENTIFIER) && !cxx_kw(c))
					last_id = cxx_peek(c, 0)->id;
				cxx_advance(c);
			}
			if (last_id)
				cxx_declare(c, rs, last_id, CXX_ENTITY_VALUE);
			if (!cxx_accept(c, LEX_TOKEN_COMMA))
				break;
		}
		cxx_expect(c, LEX_TOKEN_RIGHT_PAREN);
	}
	cxx_expect(c, LEX_TOKEN_LEFT_BRACE);
	while (!cxx_accept(c, LEX_TOKEN_RIGHT_BRACE)) {
		int r = -1;	/* this requirement */

		c->einvalid = 0;
		if (cxx_is_kw(c, CXX_KW_typename)) {	/* type requirement */
			c->member_missing = 0;
			cxx_type_id_parse(c);
			r = c->member_missing ? 0 : c->type_out != CXX_TY_UNKNOWN ? 1 : -1;
		} else if (cxx_is(c, LEX_TOKEN_LEFT_BRACE)) {	/* compound requirement */
			qty et;
			bool lv;

			cxx_advance(c);
			cxx_expression_parse(c);
			et = c->etype, lv = c->elval;
			r = c->einvalid ? 0 : et != CXX_TY_UNKNOWN ? 1 : -1;
			cxx_expect(c, LEX_TOKEN_RIGHT_BRACE);
			cxx_accept_kw(c, CXX_KW_noexcept);
			if (cxx_accept(c, LEX_TOKEN_ARROW)) {
				/* type-constraint: Concept<decltype((expr)), args> */
				cxx_entity *ce = cxx_constraint_name_parse(c);

				if (!ce)
					cxx_fail(c, "expected a concept after '->'");
				if (r == 1 && eval) {
					cxx_targ a[32];

					if (c->ntargs < 31) {
						a[0] = (cxx_targ){ .kind = 1, .t = lv ? cxx_type_reference(c, et, false) : et };
						memcpy(a + 1, c->targs, c->ntargs * sizeof *a);
						r = cxx_concept_value(c, ce, a, c->ntargs + 1);
					} else
						r = -1;
				}
			}
		} else if (cxx_is_kw(c, CXX_KW_requires)) {	/* nested requirement */
			cxx_advance(c);
			cxx_conditional_expression_parse(c);
			r = c->econst ? c->evalue != 0 : -1;
		} else {
			cxx_expression_parse(c);
			r = c->einvalid ? 0 : c->etype != CXX_TY_UNKNOWN ? 1 : -1;
		}
		cxx_expect(c, LEX_TOKEN_SEMICOLON);
		if (r == 0)
			value = 0;
		else if (r < 0 && value == 1)
			value = -1;
	}
	c->req_value = (int8_t)(eval ? value : -1);
	c->scope = outer;
	c->in_template_args = targs;
}

/* ---- primary and postfix ----------------------------------------------------------------- */

static bool cxx_is_fold_operator(int k)
{
	switch (k) {
	case LEX_TOKEN_PLUS: case LEX_TOKEN_MINUS: case LEX_TOKEN_STAR: case LEX_TOKEN_SLASH: case LEX_TOKEN_PERCENT:
	case LEX_TOKEN_CARET: case LEX_TOKEN_AMPERSAND: case LEX_TOKEN_PIPE: case LEX_TOKEN_LESS_LESS:
	case LEX_TOKEN_GREATER_GREATER: case LEX_TOKEN_PLUS_EQUAL: case LEX_TOKEN_MINUS_EQUAL: case LEX_TOKEN_STAR_EQUAL:
	case LEX_TOKEN_SLASH_EQUAL: case LEX_TOKEN_PERCENT_EQUAL: case LEX_TOKEN_CARET_EQUAL: case LEX_TOKEN_AMPERSAND_EQUAL:
	case LEX_TOKEN_PIPE_EQUAL: case LEX_TOKEN_LESS_LESS_EQUAL: case LEX_TOKEN_GREATER_GREATER_EQUAL: case LEX_TOKEN_EQUAL:
	case LEX_TOKEN_EQUAL_EQUAL: case LEX_TOKEN_EXCLAMATION_EQUAL: case LEX_TOKEN_LESS: case LEX_TOKEN_GREATER:
	case LEX_TOKEN_LESS_EQUAL: case LEX_TOKEN_GREATER_EQUAL: case LEX_TOKEN_AMPERSAND_AMPERSAND: case LEX_TOKEN_PIPE_PIPE:
	case LEX_TOKEN_COMMA: case CXX_TOKEN_PERIOD_STAR: case CXX_TOKEN_ARROW_STAR:
		return true;
	}
	return false;
}

/* ( ... ) : a parenthesized expression or a fold expression */
static void cxx_paren_expression_parse(cxx *c)
{
	uint32_t targs = c->in_template_args;

	uint32_t implicit = c->implicit_typename;

	cxx_expect(c, LEX_TOKEN_LEFT_PAREN);
	c->in_template_args = 0;
	c->implicit_typename = 0;
	if (cxx_is(c, LEX_TOKEN_ELLIPSIS)) {	/* ( ... op e ) */
		uint32_t n;

		cxx_advance(c);
		if (!cxx_is_fold_operator(cxx_operator_at(c, &n)))
			cxx_fail(c, "expected a fold operator");
		while (n--)
			cxx_advance(c);
		cxx_cast_expression_parse(c);
		cxx_expect(c, LEX_TOKEN_RIGHT_PAREN);
		c->in_template_args = targs;
		c->implicit_typename = implicit;
		return;
	}
	if (cxx_is(c, LEX_TOKEN_LEFT_BRACE)) {	/* GNU statement expression ({ ... }) */
		cxx_compound_statement_parse(c);
		cxx_expect(c, LEX_TOKEN_RIGHT_PAREN);
		c->in_template_args = targs;
		c->implicit_typename = implicit;
		return;
	}
	cxx_expression_parse(c);
	uint32_t n;

	if (cxx_is_fold_operator(cxx_operator_at(c, &n)) && cxx_peek(c, n)->kind == LEX_TOKEN_ELLIPSIS) {	/* ( e op ... [op e] ) */
		while (n--)
			cxx_advance(c);
		cxx_advance(c);
		if (!cxx_is(c, LEX_TOKEN_RIGHT_PAREN)) {
			cxx_operator_at(c, &n);
			while (n--)
				cxx_advance(c);
			cxx_cast_expression_parse(c);
		}
	}
	cxx_expect(c, LEX_TOKEN_RIGHT_PAREN);
	c->in_template_args = targs;
	c->implicit_typename = implicit;
}

static void cxx_primary_expression_parse(cxx *c)
{
	cxx_token *t = cxx_peek(c, 0);

	cxx_et(c, CXX_TY_UNKNOWN, false);
	switch (t->kind) {
	case LEX_TOKEN_NUMBER:
		cxx_advance(c);
		{
			int64_t v;
			qty nt = cxx_number_type(c, t);

			if (cxx_number_value(t, &v))
				cxx_ec(c, nt, v);
			else if (!c->tentative) {
				cxx_et(c, nt, false);
				cxx_gen_floating(c, t->s, t->len, nt, t->loc);
			}
		}
		return;
	case LEX_TOKEN_CHARACTER: case LEX_TOKEN_WIDE_CHARACTER: case LEX_TOKEN_UTF8_CHARACTER:
	case LEX_TOKEN_UTF16_CHARACTER: case LEX_TOKEN_UTF32_CHARACTER:
		cxx_advance(c);
		if (t->kind == LEX_TOKEN_CHARACTER && t->s[t->len - 1] == '\'') {	/* (no user-defined suffix) */
			int64_t cv = -1;

			/* 'x' and the simple escapes: a constant (the others: a char of a value not computed here) */
			if (t->len == 3 && t->s[1] != '\\')
				cv = (signed char)t->s[1];
			else if (t->len == 4 && t->s[1] == '\\') {
				static const char from[] = "ntr0\\'\"abfv", to[] = "\n\t\r\0\\'\"\a\b\f\v";
				const char *p = memchr(from, t->s[2], sizeof from - 1);

				if (p)
					cv = to[p - from];
			}
			if (cv != -1 || (t->len == 4 && t->s[2] == '0'))
				cxx_ec(c, cc_type_basic(TY_CHAR), cv == -1 ? 0 : cv);
			else
				cxx_et(c, cc_type_basic(TY_CHAR), false);
		}
		return;
	case LEX_TOKEN_STRING: case LEX_TOKEN_WIDE_STRING: case LEX_TOKEN_UTF8_STRING: case LEX_TOKEN_UTF16_STRING:
	case LEX_TOKEN_UTF32_STRING: {
		uint32_t len = 0, pieces = 0;
		bool plain = true;
		const cxx_token *first_piece = cxx_peek(c, 0);

		while (cxx_kind(c) >= LEX_TOKEN_STRING && cxx_kind(c) <= LEX_TOKEN_UTF32_STRING) {
			const cxx_token *x = cxx_peek(c, 0);

			pieces++;
			plain &= x->kind == LEX_TOKEN_STRING && x->s[0] == '"' && x->s[x->len - 1] == '"';
			for (uint32_t i = 1; i + 1 < x->len; i++)
				plain &= x->s[i] != '\\';	/* (escapes: the length is not counted here) */
			len += x->len - 2;
			cxx_advance(c);
		}
		/* "abc": an lvalue of const char[4] */
		if (plain && !c->tentative) {
			cxx_et(c, cc_type_array(c->ast, cc_type_qualify(c->ast, cc_type_basic(TY_CHAR), Q_CONST), len + 1, 0), true);
			if (pieces == 1)	/* (concatenation and escapes: not built yet) */
				cxx_gen_string(c, first_piece->s + 1, first_piece->len - 2, first_piece->loc);
		}
		return;
	}
	case LEX_TOKEN_LEFT_PAREN:
		cxx_paren_expression_parse(c);
		c->edecl_name = 0;	/* decltype((x)) is a reference */
		return;
	case LEX_TOKEN_LEFT_SQUARE:
		cxx_lambda_parse(c);	/* (its returns are its own: the enclosing function's capture is kept) */
		cxx_et(c, c->lambda_type, false);	/* its closure type, when built (not its body's last expression's) */
		return;
	case LEX_TOKEN_LEFT_BRACE:
		cxx_braced_init_list_parse(c);
		cxx_et(c, CXX_TY_UNKNOWN, false);
		return;
	case LEX_TOKEN_IDENTIFIER: case LEX_TOKEN_COLON_COLON: case LEX_TOKEN_TILDE:
		break;
	default:
		cxx_fail(c, "expected an expression");
	}
	switch (t->kw) {
	case CXX_KW_true: case CXX_KW_false: case CXX_KW_nullptr: case CXX_KW___null: case CXX_KW_this: case CXX_KW___nullptr:
	case CXX_KW___builtin_LINE: case CXX_KW___builtin_FILE: case CXX_KW___builtin_FUNCTION: case CXX_KW___builtin_COLUMN:
	case CXX_KW___builtin_FILE_NAME:
		cxx_advance(c);
		if (t->kw >= CXX_KW___builtin_LINE && t->kw <= CXX_KW___builtin_FILE_NAME && cxx_is(c, LEX_TOKEN_LEFT_PAREN))
			cxx_skip_balanced(c);
		if (t->kw == CXX_KW_true || t->kw == CXX_KW_false)
			cxx_ec(c, cc_type_basic(TY_BOOL), t->kw == CXX_KW_true);
		else if (t->kw == CXX_KW___null)
			cxx_et(c, cc_type_basic(TY_LONG), false);
		else if ((t->kw == CXX_KW_nullptr || t->kw == CXX_KW___nullptr) && !c->tentative) {
			cxx_et(c, cxx_nullptr_type(c), false);
			cxx_gen_nullptr(c, t->loc);
		}
		else if (t->kw == CXX_KW_this && c->this_type != CXX_TY_UNKNOWN && c->this_type && !c->tentative) {
			cxx_et(c, cc_type_pointer(c->ast, c->this_type), false);
			cxx_gen_this(c, t->loc);
		}
		else if (t->kw == CXX_KW___builtin_LINE || t->kw == CXX_KW___builtin_COLUMN)
			cxx_et(c, cc_type_basic(TY_UINT), false);
		return;
	case CXX_KW___builtin_source_location:
		cxx_advance(c);
		cxx_skip_balanced(c);
		return;
	case CXX_KW_static_cast: case CXX_KW_dynamic_cast: case CXX_KW_const_cast: case CXX_KW_reinterpret_cast:
		cxx_advance(c);
		cxx_expect(c, LEX_TOKEN_LESS);
		qty cast_to;
		{
			uint32_t targs = c->in_template_args;

			c->in_template_args = 0;
			cxx_type_id_parse(c);
			cast_to = c->type_out;
			c->in_template_args = targs;
		}
		if (!cxx_accept_greater(c))
			cxx_fail(c, "expected '>'");
		cxx_paren_expression_parse(c);
		{
			bool k = c->econst;
			int64_t v = c->evalue;

			uint32_t xn = c->enode;

			if (c->etype == CXX_TY_UNKNOWN && !c->tentative)
				c->eunsure = 1;
			cxx_et(c, cast_to, false);	/* (a reference type: an lvalue of its referent) */
			if (k && cast_to != CXX_TY_UNKNOWN && AST_TYPE_KIND(c->ast, cast_to) >= TY_BOOL && AST_TYPE_KIND(c->ast, cast_to) <= TY_ULLONG)
				cxx_ec(c, cast_to, v);
			else if (t->kw == CXX_KW_static_cast)
				cxx_gen_cast(c, xn, cast_to, t->loc);
			else if (t->kw == CXX_KW_reinterpret_cast)
				cxx_gen_reinterpret(c, xn, cast_to, t->loc);
		}
		return;
	case CXX_KW_typeid:
		cxx_advance(c);
		cxx_expect(c, LEX_TOKEN_LEFT_PAREN);
		if (cxx_try_type_id(c))
			cxx_type_id_parse(c);
		else
			cxx_expression_parse(c);
		cxx_expect(c, LEX_TOKEN_RIGHT_PAREN);
		cxx_et(c, CXX_TY_UNKNOWN, false);	/* (const std::type_info: not looked up) */
		return;
	case CXX_KW_requires:
		c->unevaluated++;
		cxx_requires_expression_parse(c);
		c->unevaluated--;
		if (c->req_value >= 0)
			cxx_ec(c, cc_type_basic(TY_BOOL), c->req_value);
		else
			cxx_et(c, cc_type_basic(TY_BOOL), false);
		return;
	case CXX_KW___builtin_offsetof: {	/* (type, member): a constant when the class is laid out */
		qty ot;
		uint32_t member = 0;
		uint64_t bits;
		bool simple;

		cxx_advance(c);
		cxx_advance(c);
		cxx_type_id_parse(c);
		ot = c->type_out;
		cxx_expect(c, LEX_TOKEN_COMMA);
		if (cxx_is(c, LEX_TOKEN_IDENTIFIER))
			member = cxx_peek(c, 0)->id;
		simple = member && cxx_peek(c, 1)->kind == LEX_TOKEN_RIGHT_PAREN;
		while (!cxx_is(c, LEX_TOKEN_RIGHT_PAREN)) {	/* (a.b[i]: not evaluated) */
			if (cxx_is(c, LEX_TOKEN_LEFT_SQUARE))
				cxx_skip_balanced(c);
			else
				cxx_advance(c);
		}
		cxx_advance(c);
		if (simple && !c->template_depth && !c->tentative && cxx_class_field_offset(c, ot, member, &bits, 0) && !(bits & 7))
			cxx_ec(c, cc_type_basic(TY_ULONG), (int64_t)(bits / 8));
		else
			cxx_et(c, cc_type_basic(TY_ULONG), false);
		return;
	}
	case CXX_KW___builtin_va_arg: case CXX_KW___builtin_bit_cast:
	case CXX_KW___builtin_convertvector: case CXX_KW___builtin_types_compatible_p: case CXX_KW___builtin_choose_expr:
	case CXX_KW___builtin_shufflevector: case CXX_KW___builtin_addressof: case CXX_KW___is_identifier:
		cxx_builtin_call_parse(c);
		cxx_et(c, CXX_TY_UNKNOWN, false);
		{
			qty rt;
			uint32_t bn[4], nb = c->nbnodes;

			memcpy(bn, c->bnodes, sizeof bn);
			if (nb != UINT32_MAX && !c->tentative && cxx_builtin_function_type(c, t->s, t->len, nb, c->bfirst, &rt)) {
				cxx_et(c, rt, false);
				cxx_gen_builtin(c, t->s, t->len, bn, nb, t->loc);
			}
		}
		return;
	case CXX_KW_operator:
		cxx_id_expression_parse(c);
		return;
	case CXX_KW___real__: case CXX_KW___imag__: case CXX_KW___extension__: {
		bool ext = t->kw == CXX_KW___extension__;

		cxx_advance(c);
		cxx_cast_expression_parse(c);
		if (!ext)
			cxx_et(c, CXX_TY_UNKNOWN, false);
		return;
	}
	case CXX_KW_typename: case CXX_KW_decltype: case CXX_KW___decltype:
		if (cxx_lookahead_fn(c, cxx_type_then_init)) {
			cxx_type_specifiers_parse(c);
			qty cast_to = c->ds_type;

			bool gen_class = false;
			uint32_t gen_node = 0;

			{
				bool braced = cxx_is(c, LEX_TOKEN_LEFT_BRACE);

				if (braced)
					cxx_braced_arguments_parse(c);
				else
					cxx_call_arguments_parse(c);
				if (!c->tentative && !c->template_depth && cast_to != CXX_TY_UNKNOWN && cc_type_is_record(c->ast, cast_to)) {
					uint32_t an[16], n = c->ncargs;

					memcpy(an, c->carg_nodes, sizeof an);
					cxx_ctor_use(c, cast_to, c->cargs, c->cargs_lval, c->ncargs, braced);	/* T(args): a temporary */
					gen_class = !braced || (c->ctor_found && c->ctor_picked);	/* (a list for an aggregate: not built yet) */
					if (gen_class)
						cxx_gen_construct_temp(c, cast_to, an, n, t->loc);
					gen_node = c->enode;
				}
			}
			cxx_functional_cast_type(c, cast_to);
			if (gen_class)
				c->enode = gen_node;
			return;
		}
		if (t->kw == CXX_KW_decltype || t->kw == CXX_KW___decltype) {	/* decltype(x)::member */
			cxx_id_expression_parse(c);
			return;
		}
		cxx_fail(c, "expected a type after 'typename'");
	case 0: case CXX_KW_template:
		break;
	default:
		/* a simple type keyword: int(x), unsigned{x} */
		if (cxx_starts_type(c) && cxx_lookahead_fn(c, cxx_type_then_init)) {
			cxx_type_specifiers_parse(c);
			qty cast_to = c->ds_type;

			bool gen_class = false;
			uint32_t gen_node = 0;

			{
				bool braced = cxx_is(c, LEX_TOKEN_LEFT_BRACE);

				if (braced)
					cxx_braced_arguments_parse(c);
				else
					cxx_call_arguments_parse(c);
				if (!c->tentative && !c->template_depth && cast_to != CXX_TY_UNKNOWN && cc_type_is_record(c->ast, cast_to)) {
					uint32_t an[16], n = c->ncargs;

					memcpy(an, c->carg_nodes, sizeof an);
					cxx_ctor_use(c, cast_to, c->cargs, c->cargs_lval, c->ncargs, braced);	/* T(args): a temporary */
					gen_class = !braced || (c->ctor_found && c->ctor_picked);	/* (a list for an aggregate: not built yet) */
					if (gen_class)
						cxx_gen_construct_temp(c, cast_to, an, n, t->loc);
					gen_node = c->enode;
				}
			}
			cxx_functional_cast_type(c, cast_to);
			if (gen_class)
				c->enode = gen_node;
			return;
		}
		cxx_fail(c, "unexpected '%.*s' in an expression", (int)t->len, t->s);
	}
	/* identifiers: builtin traits, types used as functional casts, id-expressions */
	if (t->kind == LEX_TOKEN_IDENTIFIER && !t->kw && cxx_peek(c, 1)->kind == LEX_TOKEN_LEFT_PAREN &&
	    cxx_builtin_kind(t->id) == CXX_BUILTIN_TRAIT && !cxx_lookup(c, t->id)) {
		cxx_builtin_call_parse(c);
		cxx_et(c, t->len > 5 && (!memcmp(t->s, "__is_", 5) || !memcmp(t->s, "__has_", 6)) ? cc_type_basic(TY_BOOL) : CXX_TY_UNKNOWN, false);
		{
			/* a builtin that is a library function or a simple operation: its type, and its code */
			qty rt;
			uint32_t bn[4], nb = c->nbnodes;

			memcpy(bn, c->bnodes, sizeof bn);
			if (nb != UINT32_MAX && !c->tentative && cxx_builtin_function_type(c, t->s, t->len, nb, c->bfirst, &rt)) {
				cxx_et(c, rt, false);
				cxx_gen_builtin(c, t->s, t->len, bn, nb, t->loc);
				return;
			}
		}
		{
			int64_t v;

			/* a trait of known types (not a template's, not a pattern's placeholders): its value */
			if (!c->template_depth && !c->tentative && c->nbtypes && c->nbtypes <= 2 &&
			    !cxx_type_has_placeholder(c, c->btypes[0]) && !(c->nbtypes == 2 && cxx_type_has_placeholder(c, c->btypes[1])) &&
			    cxx_trait_value(c, t->s, t->len, c->btypes, c->nbtypes, &v))
				cxx_ec(c, t->len == 12 && !memcmp(t->s, "__datasizeof", 12) ? cc_type_basic(TY_ULONG) : cc_type_basic(TY_BOOL), v);
			else if (t->len == 12 && !memcmp(t->s, "__datasizeof", 12))
				cxx_et(c, cc_type_basic(TY_ULONG), false);
		}
		return;
	}
	{
		/* a type followed by ( or { */
		cxx_mark m;
		bool is_type;

		cxx_mark_set(c, &m);
		is_type = cxx_lookahead_fn(c, cxx_type_then_init);
		if (is_type) {
			/* only if the name is a type (a value followed by ( is a call) */
			cxx_scope *q;
			bool dep;
			cxx_entity *e;

			cxx_nested_name_parse(c, &q, &dep);
			cxx_accept_kw(c, CXX_KW_template);
			e = cxx_is(c, LEX_TOKEN_IDENTIFIER) ? (dep ? cxx_entity_dependent() : q ? cxx_lookup_in(c, q, cxx_peek(c, 0)->id, 0)
									       : cxx_lookup(c, cxx_peek(c, 0)->id)) : NULL;
			if (e && e->kind != CXX_ENTITY_TYPE && e->kind != CXX_ENTITY_TEMPLATE &&
			    !(cxx_builtin_kind(cxx_peek(c, 0)->id) == CXX_BUILTIN_TYPE_TRANSFORM))
				is_type = cxx_entity_kind_find(e, CXX_ENTITY_TYPE) && !cxx_entity_kind_find(e, CXX_ENTITY_VALUE);
			if (e && e->kind == CXX_ENTITY_TEMPLATE && e->next)
				is_type = true;
			cxx_mark_rewind(c, &m);
		}
		if (is_type) {
			cxx_type_specifiers_parse(c);
			qty cast_to = c->ds_type;

			bool gen_class = false;
			uint32_t gen_node = 0;

			{
				bool braced = cxx_is(c, LEX_TOKEN_LEFT_BRACE);

				if (braced)
					cxx_braced_arguments_parse(c);
				else
					cxx_call_arguments_parse(c);
				if (!c->tentative && !c->template_depth && cast_to != CXX_TY_UNKNOWN && cc_type_is_record(c->ast, cast_to)) {
					uint32_t an[16], n = c->ncargs;

					memcpy(an, c->carg_nodes, sizeof an);
					cxx_ctor_use(c, cast_to, c->cargs, c->cargs_lval, c->ncargs, braced);	/* T(args): a temporary */
					gen_class = !braced || (c->ctor_found && c->ctor_picked);	/* (a list for an aggregate: not built yet) */
					if (gen_class)
						cxx_gen_construct_temp(c, cast_to, an, n, t->loc);
					gen_node = c->enode;
				}
			}
			cxx_functional_cast_type(c, cast_to);
			if (gen_class)
				c->enode = gen_node;
			return;
		}
	}
	cxx_id_expression_parse(c);
}

static void cxx_postfix_expression_parse1(cxx *c);

/* VCC_CXX_GEN=2: the innermost postfix expression of each function that code generation did not build */
static void cxx_postfix_expression_parse(cxx *c)
{
	static __thread int trace = -1;
	uint32_t start = c->pos;

	cxx_postfix_expression_parse1(c);
	if (trace < 0)
		trace = getenv("VCC_CXX_GEN") && atoi(getenv("VCC_CXX_GEN")) >= 2;
	if (trace && !c->enode && cxx_gen_on(c) && !c->gen_why) {
		c->gen_why = 1;
		fprintf(stderr, "no node:");
		for (uint32_t i = start; i < c->pos && i < start + 12; i++)
			fprintf(stderr, " %.*s", (int)c->tok[i].len, c->tok[i].s);
		fprintf(stderr, " -> %s\n", c->etype == CXX_TY_UNKNOWN ? "untyped" : cc_type_is_record(c->ast, c->etype) ? "class" : "typed");
	}
}

static void cxx_postfix_expression_parse1(cxx *c)
{
	cxx_primary_expression_parse(c);
	for (;;) {
		switch (cxx_kind(c)) {
		case LEX_TOKEN_LEFT_SQUARE:
			if (cxx_peek(c, 1)->kind == LEX_TOKEN_LEFT_SQUARE)
				return;	/* [[ attribute ]] */
			cxx_advance(c);
			{
				uint32_t targs = c->in_template_args;
				qty base = c->etype;
				uint32_t bn = c->enode, in, iloc = cxx_peek(c, 0)->loc;

				c->in_template_args = 0;
				if (cxx_is(c, LEX_TOKEN_LEFT_BRACE))
					cxx_braced_init_list_parse(c);
				else
					cxx_expression_parse(c);
				c->in_template_args = targs;
				in = c->enode;
				/* a[i]: an element of an array or pointer, or a class's operator[] */
				if (!c->tentative && cxx_operator_call_type(c, vintern_cstr("operator[]"), base, &c->etype, 1))
					;
				else {
					cxx_et(c, base != CXX_TY_UNKNOWN && (AST_TYPE_KIND(c->ast, base) == TY_PTR || AST_TYPE_KIND(c->ast, base) == TY_ARRAY) &&
					       !AST_TYPE(c->ast, base)->ext ? AST_TYPE(c->ast, base)->base : CXX_TY_UNKNOWN, true);
					if (c->etype != CXX_TY_UNKNOWN)
						cxx_gen_index(c, bn, in, iloc);
				}
			}
			cxx_expect(c, LEX_TOKEN_RIGHT_SQUARE);
			continue;
		case LEX_TOKEN_LEFT_PAREN:
			{
				uint32_t targs = c->in_template_args;
				cxx_entity *set = c->ecallee;
				qty callee = c->etype;
				uint8_t callee_lval = c->elval;
				uint32_t obj_node = c->eobj_node;
				bool obj_arrow = c->eobj_arrow;
				cxx_targ xa[8];
				uint32_t nx = c->xargs_set && c->nxargs <= 8 ? c->nxargs : UINT32_MAX;
				uint8_t call_obj = c->call_obj;
				uint32_t ename = c->ename;
				bool missing = c->ename_missing;

				if (c->xargs_set && nx == UINT32_MAX)
					set = NULL, callee = CXX_TY_UNKNOWN;	/* (too many explicit arguments) */
				if (nx != UINT32_MAX)
					memcpy(xa, c->xargs, nx * sizeof *xa);

				/* (a virtual function named with a qualifier is called directly; a callee that is not a plain name: not known) */
				uint8_t vmode = c->pos >= 2 && c->tok[c->pos - 1].kind == LEX_TOKEN_IDENTIFIER ?
						(c->tok[c->pos - 2].kind == LEX_TOKEN_COLON_COLON ? 1 : 0) : 2;

				c->in_template_args = 0;
				cxx_call_arguments_parse(c);
				c->in_template_args = targs;
				if (c->ncargs == UINT32_MAX || c->tentative)
					cxx_et(c, CXX_TY_UNKNOWN, false);
				else if (nx != UINT32_MAX && set) {
					uint32_t anodes[16], an = c->ncargs, cloc = c->tok[c->pos ? c->pos - 1 : 0].loc;

					memcpy(anodes, c->carg_nodes, sizeof anodes);
					c->call_obj = call_obj, cxx_call_type_x(c, set, xa, nx, c->cargs, c->cargs_lval, c->ncargs);
					if (c->call_resolved && c->call_picked && c->etype != CXX_TY_UNKNOWN && (!call_obj || obj_node))
						c->gen_vmode = vmode, cxx_gen_call(c, c->call_picked, obj_node, obj_arrow, anodes, an, cloc), c->gen_vmode = 0;
				}
				else if (!set && (c->op_lv_set = c->ncargs <= 16, c->op_lv[0] = callee_lval,
						  memcpy(c->op_lv + 1, c->cargs_lval, c->ncargs <= 16 ? c->ncargs : 0),
						  cxx_operator_call_type(c, vintern_cstr("operator()"), callee, c->cargs, c->ncargs)))
					;	/* an object of class type: its operator() */
				else {
					uint8_t was = c->einvalid;	/* (an earlier part of the expression may be ill-formed already) */
					uint32_t anodes[16], an = c->ncargs, cloc = c->tok[c->pos ? c->pos - 1 : 0].loc;

					memcpy(anodes, c->carg_nodes, sizeof anodes);
					c->call_obj = call_obj, cxx_call_type(c, set, callee, c->cargs, c->cargs_lval, c->ncargs);
					if (set && c->call_resolved && c->call_picked && c->etype != CXX_TY_UNKNOWN && (!call_obj || obj_node))
						c->gen_vmode = vmode, cxx_gen_call(c, c->call_picked, obj_node, obj_arrow, anodes, an, cloc), c->gen_vmode = 0;
					/* f(x) with no f that fits: the functions named f in the namespaces of the
					   arguments' classes (argument-dependent lookup) */
					if (c->etype == CXX_TY_UNKNOWN && ename && (!set || c->none_viable)) {
						bool none = c->none_viable || (!set && missing), known = true;

						for (uint32_t i = 0; i < c->ncargs && i < 16; i++)
							known &= c->cargs[i] != CXX_TY_UNKNOWN;
						cxx_adl_call_type(c, ename, c->cargs, c->cargs_lval, c->ncargs);
						/* nothing by that name fits, here or in the arguments' namespaces */
						c->einvalid = was || (c->etype == CXX_TY_UNKNOWN && none && known && !c->einvalid_unsure);
						if (c->einvalid && !was)
							c->einvalid_at = c->pos;
					}
				}
			}
			continue;
		case LEX_TOKEN_PERIOD: case LEX_TOKEN_ARROW: {
			/* the object's class: its scope, for the member's lookup */
			qty obj = c->etype;
			cxx_scope *cls = NULL;
			uint32_t obj_node = c->enode, mloc = cxx_peek(c, 0)->loc;
			bool arrow = cxx_is(c, LEX_TOKEN_ARROW);

			if (cxx_is(c, LEX_TOKEN_ARROW))
				obj = obj != CXX_TY_UNKNOWN && AST_TYPE_KIND(c->ast, obj) == TY_PTR && !AST_TYPE(c->ast, obj)->ext ?
				      AST_TYPE(c->ast, obj)->base : CXX_TY_UNKNOWN;
			if (obj != CXX_TY_UNKNOWN && !c->tentative)
				cls = cxx_type_class_scope(c, QUNQ(obj));
			if (obj != CXX_TY_UNKNOWN && !c->tentative && !cc_type_is_record(c->ast, obj) && !cxx_is(c, LEX_TOKEN_TILDE))
				CXX_INVALID(c);	/* x.m on what is not a class (a requirement fails) */
			cxx_et(c, CXX_TY_UNKNOWN, false);
			cxx_advance(c);
			{
				/* a member: names are looked up in the object's class, which the parser does not know;
				   template-ids need 'template' when dependent, else lookup by name finds member templates */
				cxx_scope *q;
				bool dep, template_kw;
				uint32_t id;

				template_kw = cxx_accept_kw(c, CXX_KW_template);
				cxx_nested_name_parse(c, &q, &dep);
				template_kw |= cxx_accept_kw(c, CXX_KW_template);
				if (cxx_is(c, LEX_TOKEN_TILDE) || cxx_is_kw(c, CXX_KW_operator)) {
					/* x.operator->(), x.operator+=(y): the member operator by its name */
					uint32_t oid = 0;

					if (cxx_is_kw(c, CXX_KW_operator) && cls && !q && !dep) {
						uint32_t save = c->pos, on;

						c->pos++;
						if (cxx_is(c, LEX_TOKEN_LEFT_PAREN) && cxx_peek(c, 1)->kind == LEX_TOKEN_RIGHT_PAREN)
							oid = vintern_cstr("operator()");
						else if (cxx_is(c, LEX_TOKEN_LEFT_SQUARE) && cxx_peek(c, 1)->kind == LEX_TOKEN_RIGHT_SQUARE)
							oid = vintern_cstr("operator[]");
						else if (cxx_kind(c) >= LEX_TOKEN_LEFT_SQUARE && cxx_kind(c) < LEX_TOKEN_HASH) {
							cxx_operator_at(c, &on);
							oid = cxx_operator_id_at(c, on);
						}
						c->pos = save;
					}
					cxx_unqualified_id_rest_parse(c);
					cxx_et(c, CXX_TY_UNKNOWN, false);
					if (oid > 1) {
						cxx_entity *m = cxx_lookup_in(c, cls, oid, 0);

						if (m && m->kind != CXX_ENTITY_DEPENDENT) {
							cxx_name_type(c, m);
							c->call_obj = (QQ(obj) & Q_CONST) ? 2 : 1;
						} else if (!m && !cls->dependent && !cls->has_dependent_base)
							CXX_INVALID(c);
					}
					continue;
				}
				if (!cxx_is(c, LEX_TOKEN_IDENTIFIER))
					cxx_fail(c, "expected a member name");
				id = cxx_peek(c, 0)->id;
				cxx_advance(c);
				/* the object's type is not known here: name<...>( is a member template call if it parses */
				if (cxx_is(c, LEX_TOKEN_LESS) && (template_kw || cxx_lookahead_fn(c, cxx_template_arguments_then_call))) {
					cxx_entity *m = cls && !q && !dep ? cxx_lookup_in(c, cls, id, 0) : NULL;

					cxx_template_arguments_parse(c);
					cxx_et(c, CXX_TY_UNKNOWN, false);
					/* x.f<args>(...): the member function templates named f, with the explicit arguments */
					if (m && m->kind != CXX_ENTITY_DEPENDENT && cxx_entity_kind_find(m, CXX_ENTITY_FUNCTION_TEMPLATE) && !c->tentative) {
						if (c->ntargs > c->cxargs)
							c->xargs = realloc(c->xargs, (c->cxargs = c->ntargs + 8) * sizeof *c->xargs);
						memcpy(c->xargs, c->targs, c->ntargs * sizeof *c->xargs);
						c->nxargs = c->ntargs, c->xargs_set = 1, c->ecallee = m;
						c->call_obj = (QQ(obj) & Q_CONST) ? 2 : 1;
						c->eobj_node = obj_node, c->eobj_arrow = arrow;	/* (code generation: the call's object) */
					} else if (cls && !q && !dep && !m && !cls->dependent && !cls->has_dependent_base && !c->tentative)
						CXX_INVALID(c);	/* no such member template */
					continue;
				}
				/* the member: a data member's type, or the member functions for the call */
				if (cls && !q && !dep) {
					cxx_entity *m = cxx_lookup_in(c, cls, id, 0);

					if (m && m->kind != CXX_ENTITY_DEPENDENT) {
						cxx_name_type(c, m);
						c->call_obj = (QQ(obj) & Q_CONST) ? 2 : 1;
						if (c->etype != CXX_TY_UNKNOWN && c->edecl_name && !c->ecallee)
							cxx_gen_member(c, obj_node, arrow, id, mloc);	/* a data member */
						else
							c->eobj_node = obj_node, c->eobj_arrow = arrow;	/* member functions: the call's object */
					} else if (!m && !cls->dependent && !cls->has_dependent_base)
						CXX_INVALID(c);	/* no such member (a requirement fails) */
				}
			}
			continue;
		}
		case LEX_TOKEN_PLUS_PLUS: case LEX_TOKEN_MINUS_MINUS: {
			int op = cxx_kind(c);
			uint32_t xn = c->enode, oploc = cxx_peek(c, 0)->loc;
			bool built_in = c->etype != CXX_TY_UNKNOWN && (cc_type_is_arithmetic(c->ast, c->etype) || AST_TYPE_KIND(c->ast, c->etype) == TY_PTR);

			cxx_advance(c);
			cxx_et(c, built_in ? QUNQ(c->etype) : CXX_TY_UNKNOWN, false);
			if (built_in)
				cxx_gen_postfix(c, op, xn, oploc);
			continue;
		}
		}
		return;
	}
}

/* ---- unary, cast, binary ----------------------------------------------------------------- */

static void cxx_new_expression_parse(cxx *c)
{
	cxx_accept(c, LEX_TOKEN_COLON_COLON);
	cxx_expect_kw(c, CXX_KW_new);
	if (cxx_is(c, LEX_TOKEN_LEFT_PAREN)) {
		/* placement arguments, or a parenthesized type */
		if (cxx_paren_type_id_follows(c)) {
			cxx_advance(c);
			cxx_type_id_parse(c);
			cxx_expect(c, LEX_TOKEN_RIGHT_PAREN);
			goto init;
		}
		cxx_call_arguments_parse(c);
	}
	if (cxx_is(c, LEX_TOKEN_LEFT_PAREN)) {
		cxx_advance(c);
		cxx_type_id_parse(c);
		cxx_expect(c, LEX_TOKEN_RIGHT_PAREN);
	} else
		cxx_new_type_id_parse(c);
init:
	if (cxx_is(c, LEX_TOKEN_LEFT_PAREN))
		cxx_call_arguments_parse(c);
	else if (cxx_is(c, LEX_TOKEN_LEFT_BRACE))
		cxx_braced_init_list_parse(c);
}

static void cxx_unary_expression_parse(cxx *c)
{
	switch (cxx_kind(c)) {
	case LEX_TOKEN_PLUS_PLUS: case LEX_TOKEN_MINUS_MINUS: case LEX_TOKEN_STAR: case LEX_TOKEN_AMPERSAND: case LEX_TOKEN_PLUS:
	case LEX_TOKEN_MINUS: case LEX_TOKEN_EXCLAMATION: case LEX_TOKEN_TILDE:
		if (cxx_kind(c) == LEX_TOKEN_TILDE && cxx_peek(c, 1)->kind == LEX_TOKEN_IDENTIFIER && cxx_peek(c, 2)->kind == LEX_TOKEN_LEFT_PAREN &&
		    cxx_peek(c, 3)->kind == LEX_TOKEN_RIGHT_PAREN) {
			cxx_entity *e = cxx_lookup(c, cxx_peek(c, 1)->id);

			if (e && (e->kind == CXX_ENTITY_TYPE || e->kind == CXX_ENTITY_TEMPLATE)) {	/* ~T() pseudo-destructor */
				cxx_postfix_expression_parse(c);
				return;
			}
		}
		{
			int op = cxx_kind(c);

			uint32_t opid = cxx_operator_id_at(c, 1);

			uint32_t oploc = cxx_peek(c, 0)->loc;

			cxx_advance(c);
			cxx_cast_expression_parse(c);
			{
				bool k = c->econst;
				int64_t v = c->evalue;
				uint32_t xn = c->enode;

				if (!c->tentative && !cxx_operator_call_type(c, opid, c->etype, NULL, 0)) {
					cxx_unary_type(c, op, c->etype, c->elval);
					cxx_gen_unary(c, op, xn, oploc);
				}
				if (k && (op == LEX_TOKEN_EXCLAMATION || op == LEX_TOKEN_MINUS || op == LEX_TOKEN_PLUS || op == LEX_TOKEN_TILDE)) {
					c->evalue = op == LEX_TOKEN_EXCLAMATION ? !v : op == LEX_TOKEN_MINUS ? (int64_t)(0 - (uint64_t)v) :
						    op == LEX_TOKEN_TILDE ? ~v : v;
					c->econst = 1;
				}
			}
		}
		return;
	case LEX_TOKEN_AMPERSAND_AMPERSAND:	/* GNU &&label */
		cxx_advance(c);
		cxx_expect(c, LEX_TOKEN_IDENTIFIER);
		cxx_et(c, CXX_TY_UNKNOWN, false);
		return;
	case LEX_TOKEN_COLON_COLON:
		if (cxx_peek(c, 1)->kw == CXX_KW_new) {
			cxx_new_expression_parse(c);
			return;
		}
		if (cxx_peek(c, 1)->kw == CXX_KW_delete) {
			cxx_advance(c);
			break;
		}
		break;
	}
	switch (cxx_kw(c)) {
	case CXX_KW_sizeof:
		cxx_advance(c);
		if (cxx_accept(c, LEX_TOKEN_ELLIPSIS)) {	/* sizeof...(pack) */
			cxx_expect(c, LEX_TOKEN_LEFT_PAREN);
			cxx_expect(c, LEX_TOKEN_IDENTIFIER);
			cxx_expect(c, LEX_TOKEN_RIGHT_PAREN);
		} else if (cxx_paren_type_id_follows(c)) {
			uint64_t size;
			uint32_t align;

			cxx_advance(c);
			cxx_type_id_parse(c);
			cxx_expect(c, LEX_TOKEN_RIGHT_PAREN);
			if (!c->template_depth && !c->tentative && cxx_type_size_known(c, c->type_out, &size, &align)) {
				cxx_ec(c, cc_type_basic(TY_ULONG), (int64_t)size);
				return;
			}
		} else {
			uint64_t size;
			uint32_t align;

			c->unevaluated++;
			cxx_unary_expression_parse(c);
			c->unevaluated--;
			/* sizeof expr: its type's size */
			if (!c->template_depth && !c->tentative && cxx_type_size_known(c, c->etype, &size, &align)) {
				cxx_ec(c, cc_type_basic(TY_ULONG), (int64_t)size);
				return;
			}
		}
		cxx_et(c, cc_type_basic(TY_ULONG), false);	/* size_t */
		return;
	case CXX_KW_alignof: case CXX_KW__Alignof: case CXX_KW___alignof__: case CXX_KW___alignof:
		cxx_advance(c);
		if (cxx_paren_type_id_follows(c)) {
			uint64_t size;
			uint32_t align;

			cxx_advance(c);
			cxx_type_id_parse(c);
			cxx_expect(c, LEX_TOKEN_RIGHT_PAREN);
			if (!c->template_depth && !c->tentative && cxx_type_size_known(c, c->type_out, &size, &align)) {
				cxx_ec(c, cc_type_basic(TY_ULONG), (int64_t)align);
				return;
			}
		} else
			cxx_unary_expression_parse(c);
		cxx_et(c, cc_type_basic(TY_ULONG), false);
		return;
	case CXX_KW_noexcept:
		cxx_advance(c);
		c->unevaluated++;
		cxx_paren_expression_parse(c);
		c->unevaluated--;
		cxx_et(c, cc_type_basic(TY_BOOL), false);
		return;
	case CXX_KW_new:
		cxx_new_expression_parse(c);
		cxx_et(c, CXX_TY_UNKNOWN, false);	/* (the new-type-id's pointer: not built yet) */
		return;
	case CXX_KW_delete:
		cxx_advance(c);
		if (cxx_accept(c, LEX_TOKEN_LEFT_SQUARE))
			cxx_expect(c, LEX_TOKEN_RIGHT_SQUARE);
		cxx_cast_expression_parse(c);
		cxx_et(c, cc_type_basic(TY_VOID), false);
		return;
	case CXX_KW_co_await:
		cxx_advance(c);
		cxx_cast_expression_parse(c);
		cxx_et(c, CXX_TY_UNKNOWN, false);
		return;
	}
	cxx_postfix_expression_parse(c);
}

/* after ( type-id ): can a cast's operand start here? (else the parentheses held a value: (T())) */
static bool cxx_cast_operand_follows(cxx *c)
{
	cxx_mark m;
	bool ok;
	int k;

	cxx_mark_set(c, &m);
	cxx_advance(c);
	c->tentative++;
	{
		jmp_buf jb;

		c->fail = &jb;
		if (setjmp(jb)) {
			c->tentative--;
			cxx_mark_rewind(c, &m);
			return false;
		}
		cxx_type_id_parse(c);
		cxx_expect(c, LEX_TOKEN_RIGHT_PAREN);
	}
	c->tentative--;
	k = cxx_kind(c);
	ok = !(k == LEX_TOKEN_RIGHT_PAREN || k == LEX_TOKEN_COMMA || k == LEX_TOKEN_SEMICOLON || k == LEX_TOKEN_RIGHT_SQUARE ||
	       k == LEX_TOKEN_RIGHT_BRACE || k == LEX_TOKEN_EOF || k == LEX_TOKEN_QUESTION || k == LEX_TOKEN_COLON ||
	       k == LEX_TOKEN_EQUAL || k == LEX_TOKEN_PERIOD || k == LEX_TOKEN_ARROW || k == LEX_TOKEN_GREATER ||
	       k == LEX_TOKEN_EQUAL_EQUAL || k == LEX_TOKEN_EXCLAMATION_EQUAL || k == LEX_TOKEN_PIPE_PIPE ||
	       k == LEX_TOKEN_AMPERSAND_AMPERSAND || k == LEX_TOKEN_SLASH || k == LEX_TOKEN_PERCENT || k == LEX_TOKEN_LESS_EQUAL ||
	       k == LEX_TOKEN_GREATER_EQUAL || k == LEX_TOKEN_PIPE || k == LEX_TOKEN_CARET);
	cxx_mark_rewind(c, &m);
	return ok;
}

static void cxx_cast_expression_parse(cxx *c)
{
	if (cxx_is(c, LEX_TOKEN_LEFT_PAREN) && cxx_paren_type_id_follows(c) && cxx_cast_operand_follows(c)) {
		/* (T)x, (T){...}; but (T)(...) is also a call of a parenthesized value: the type wins */
		qty cast_to;

		cxx_advance(c);
		cxx_type_id_parse(c);
		cast_to = c->type_out;
		cxx_expect(c, LEX_TOKEN_RIGHT_PAREN);
		if (cxx_is(c, LEX_TOKEN_LEFT_BRACE)) {	/* compound literal */
			cxx_braced_init_list_parse(c);
			cxx_et(c, cast_to, false);
			return;
		}
		cxx_cast_expression_parse(c);
		{
			bool k = c->econst;
			int64_t v = c->evalue;

			uint32_t xn = c->enode, cloc = c->tok[c->pos ? c->pos - 1 : 0].loc;

			if (c->etype == CXX_TY_UNKNOWN && !c->tentative)
				c->eunsure = 1;	/* ((void)x: a type whatever x is; a tentative parse types nothing) */
			cxx_et(c, cast_to, false);
			if (k && cast_to != CXX_TY_UNKNOWN && AST_TYPE_KIND(c->ast, cast_to) >= TY_BOOL && AST_TYPE_KIND(c->ast, cast_to) <= TY_ULLONG)
				cxx_ec(c, cast_to, v);
			else
				cxx_gen_cast(c, xn, cast_to, cloc);
		}
		return;
	}
	cxx_unary_expression_parse(c);
}

static int cxx_binary_precedence(cxx *c, uint32_t *ntok)
{
	switch (cxx_operator_at(c, ntok)) {
	case CXX_TOKEN_PERIOD_STAR: case CXX_TOKEN_ARROW_STAR: return 14;
	case LEX_TOKEN_STAR: case LEX_TOKEN_SLASH: case LEX_TOKEN_PERCENT: return 13;
	case LEX_TOKEN_PLUS: case LEX_TOKEN_MINUS: return 12;
	case LEX_TOKEN_LESS_LESS: return 11;
	case LEX_TOKEN_GREATER_GREATER: return c->in_template_args ? 0 : 11;
	case CXX_TOKEN_SPACESHIP: return 10;
	case LEX_TOKEN_LESS: case LEX_TOKEN_LESS_EQUAL: return 9;
	case LEX_TOKEN_GREATER: return c->in_template_args ? 0 : 9;
	case LEX_TOKEN_GREATER_EQUAL: return 9;	/* written as one token: an operator even in template arguments */
	case LEX_TOKEN_EQUAL_EQUAL: case LEX_TOKEN_EXCLAMATION_EQUAL: return 8;
	case LEX_TOKEN_AMPERSAND: return 7;
	case LEX_TOKEN_CARET: return 6;
	case LEX_TOKEN_PIPE: return 5;
	case LEX_TOKEN_AMPERSAND_AMPERSAND: return 4;
	case LEX_TOKEN_PIPE_PIPE: return 3;
	}
	return 0;
}

static void cxx_binary_expression_parse(cxx *c, int min)
{
	cxx_cast_expression_parse(c);
	for (;;) {
		uint32_t n;
		int p = cxx_binary_precedence(c, &n);

		if (!p || p < min)
			return;
		/* a fold: e op ... (the caller, a parenthesized expression, takes it) */
		if (cxx_peek(c, n)->kind == LEX_TOKEN_ELLIPSIS)
			return;
		{
			qty l = c->etype;
			bool ll = c->elval;
			uint32_t ln = c->enode, oploc = cxx_peek(c, 0)->loc;
			bool built_in = false;
			uint32_t k;
			int op = cxx_operator_at(c, &k);
			uint32_t opid = cxx_operator_id_at(c, k);

			while (n--)
				cxx_advance(c);
			bool lk = c->econst, rk;
			int64_t lv = c->evalue, rv, v;

			c->gen_notemp += op == LEX_TOKEN_AMPERSAND_AMPERSAND || op == LEX_TOKEN_PIPE_PIPE;
			cxx_binary_expression_parse(c, p + 1);
			c->gen_notemp -= op == LEX_TOKEN_AMPERSAND_AMPERSAND || op == LEX_TOKEN_PIPE_PIPE;
			rk = c->econst, rv = c->evalue;
			qty rt = c->etype;
			uint32_t rn = c->enode;

			c->op_lv_set = 1, c->op_lv[0] = ll, c->op_lv[1] = c->elval;
			if (!c->tentative && !cxx_operator_call_type(c, opid, l, &c->etype, 1))
				cxx_binary_type(c, op, l, rt), built_in = true;
			c->op_lv_set = 0;
			if (built_in)
				cxx_gen_binary(c, op, ln, rn, oploc);
			/* constants: both operands, or the left alone when it decides && and || */
			if (lk && rk) {
				bool uns = c->etype != CXX_TY_UNKNOWN && AST_TYPE_KIND(c->ast, c->etype) >= TY_CHAR &&
					   AST_TYPE_KIND(c->ast, c->etype) <= TY_UINT128 && !cc_type_is_signed(c->ast, c->etype);

				/* a comparison's result is bool: unsigned when the operands' common type is (an
				   unsigned operand of at least int's size, no narrower than the other) */
				if (op == LEX_TOKEN_LESS || op == LEX_TOKEN_GREATER || op == LEX_TOKEN_LESS_EQUAL || op == LEX_TOKEN_GREATER_EQUAL) {
					qty o[2] = { l, rt };

					uns = false;
					for (int i = 0; i < 2 && l != CXX_TY_UNKNOWN && rt != CXX_TY_UNKNOWN; i++) {
						const ctype *a = AST_TYPE(c->ast, o[i]), *b = AST_TYPE(c->ast, o[!i]);

						uns |= a->kind >= TY_CHAR && a->kind <= TY_UINT128 && !cc_type_is_signed(c->ast, o[i]) &&
						       a->size >= 4 && a->size >= b->size;
					}
				}

				if (cxx_binary_value(op, lv, rv, uns, &v))
					c->evalue = v, c->econst = 1;
			} else if (lk && ((op == LEX_TOKEN_AMPERSAND_AMPERSAND && !lv) || (op == LEX_TOKEN_PIPE_PIPE && lv)))
				c->evalue = lv != 0, c->econst = 1;
		}
	}
}

VPRIV void cxx_conditional_expression_parse(cxx *c)
{
	cxx_binary_expression_parse(c, 1);
	if (cxx_accept(c, LEX_TOKEN_QUESTION)) {
		uint32_t targs = c->in_template_args;

		qty t2 = c->etype;	/* (GNU ?: : the condition's) */
		bool l2 = c->elval;
		bool ck = c->econst, k2 = c->econst, k3;
		int64_t cv = c->evalue, v2 = c->evalue, v3;
		uint32_t cn = c->enode, n2 = 0, n3, qloc = c->tok[c->pos - 1].loc;

		c->in_template_args = 0;
		c->gen_notemp++;	/* (an operand that may not be evaluated: no temporaries with destructors) */
		if (!cxx_is(c, LEX_TOKEN_COLON))	/* GNU ?: */
			cxx_expression_parse(c), t2 = c->etype, l2 = c->elval, k2 = c->econst, v2 = c->evalue, n2 = c->enode;
		c->in_template_args = targs;
		cxx_expect(c, LEX_TOKEN_COLON);
		cxx_assignment_expression_parse(c);
		c->gen_notemp--;
		k3 = c->econst, v3 = c->evalue;
		n3 = c->enode;
		{
			qty t3 = c->etype;

			if (t2 != CXX_TY_UNKNOWN && t2 == t3)
				cxx_et(c, t2, l2 && c->elval);
			else if (t2 != CXX_TY_UNKNOWN && t3 != CXX_TY_UNKNOWN && cc_type_is_arithmetic(c->ast, t2) && cc_type_is_arithmetic(c->ast, t3))
				cxx_et(c, cc_type_arithmetic_conversion(c->ast, QUNQ(t2), QUNQ(t3)), false);
			else
				cxx_et(c, CXX_TY_UNKNOWN, false);
			/* (built as a value: where the result is used as an lvalue, a ? x : y = 1, the assignment's
			   builder finds no lvalue and the function gets no code) */
			if (c->etype != CXX_TY_UNKNOWN && (cc_type_is_arithmetic(c->ast, c->etype) || AST_TYPE_KIND(c->ast, c->etype) == TY_PTR))
				cxx_gen_conditional(c, cn, n2, n3, qloc);
			if (ck && (cv ? k2 : k3))
				c->evalue = cv ? v2 : v3, c->econst = 1;	/* a constant condition: the branch taken */
		}
	}
}

static bool cxx_is_assignment_operator(cxx *c, uint32_t *ntok)
{
	switch (cxx_operator_at(c, ntok)) {
	case LEX_TOKEN_EQUAL: case LEX_TOKEN_STAR_EQUAL: case LEX_TOKEN_SLASH_EQUAL: case LEX_TOKEN_PERCENT_EQUAL:
	case LEX_TOKEN_PLUS_EQUAL: case LEX_TOKEN_MINUS_EQUAL: case LEX_TOKEN_LESS_LESS_EQUAL: case LEX_TOKEN_AMPERSAND_EQUAL:
	case LEX_TOKEN_CARET_EQUAL: case LEX_TOKEN_PIPE_EQUAL:
		return true;
	case LEX_TOKEN_GREATER_GREATER_EQUAL:
		return !c->in_template_args;
	}
	return false;
}

VPRIV void cxx_assignment_expression_parse(cxx *c)
{
	if (cxx_is_kw(c, CXX_KW_throw)) {
		cxx_advance(c);
		if (!cxx_is(c, LEX_TOKEN_SEMICOLON) && !cxx_is(c, LEX_TOKEN_RIGHT_PAREN) && !cxx_is(c, LEX_TOKEN_COMMA) &&
		    !cxx_is(c, LEX_TOKEN_COLON) && !cxx_is(c, LEX_TOKEN_RIGHT_BRACE))
			cxx_assignment_expression_parse(c);
		return;
	}
	if (cxx_is_kw(c, CXX_KW_co_yield)) {
		cxx_advance(c);
		cxx_initializer_parse(c);
		return;
	}
	cxx_conditional_expression_parse(c);
	uint32_t n;

	if (cxx_is_assignment_operator(c, &n)) {
		qty l = c->etype;
		bool built_in = l != CXX_TY_UNKNOWN && (cc_type_is_arithmetic(c->ast, l) || AST_TYPE_KIND(c->ast, l) == TY_PTR);
		uint32_t ln = c->enode, oploc = cxx_peek(c, 0)->loc, ntok = n;
		int optok = cxx_operator_at(c, &ntok);

		uint32_t opid = cxx_operator_id_at(c, n);

		if (cxx_peek(c, n)->kind == LEX_TOKEN_ELLIPSIS)
			return;	/* a fold over = */
		while (n--)
			cxx_advance(c);
		cxx_initializer_parse(c);
		{
			uint32_t rn = c->enode;

			if (built_in || c->tentative || !cxx_operator_call_type(c, opid, l, &c->etype, 1))
				cxx_et(c, built_in ? l : CXX_TY_UNKNOWN, built_in);
			if (built_in)
				cxx_gen_assignment(c, optok, ln, rn, oploc);
		}
	}
}

VPRIV void cxx_expression_parse(cxx *c)
{
	cxx_assignment_expression_parse(c);
	while (cxx_is(c, LEX_TOKEN_COMMA) && !c->in_template_args) {
		if (cxx_peek(c, 1)->kind == LEX_TOKEN_ELLIPSIS)
			return;	/* fold over , */
		/* a, b: b's type whatever a is; whether a is valid is then not known (decltype(f(x), void())) */
		if (c->etype == CXX_TY_UNKNOWN && !c->tentative && !c->template_depth)
			c->eunsure = 1;
		uint32_t ln = c->enode, cloc = cxx_peek(c, 0)->loc;

		cxx_advance(c);
		cxx_assignment_expression_parse(c);
		if (c->etype != CXX_TY_UNKNOWN && !cc_type_is_record(c->ast, c->etype)) {
			uint32_t rn = c->enode;

			c->enode = 0;
			cxx_gen_comma(c, ln, rn, cloc);
		} else
			c->enode = 0;
	}
}

VPRIV void cxx_constant_expression_parse(cxx *c)
{
	cxx_conditional_expression_parse(c);
}
