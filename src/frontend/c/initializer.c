/* vcc front end: initializers */
#include "cc-internal.h"

/* at '{': "{}" before C23 (clang -pedantic) */
static inline void cc_initializer_empty_extension_check(cc *c)
{
	if (unlikely(cc_extension_is_enabled(c, "c23-extensions", cc_warning_group_pedantic)) && c->o->std != 23 && cc_token_peek(c, 1)->kind == LEX_TOKEN_RIGHT_BRACE)
		cc_report_extension(c, "c23-extensions", cc_warning_group_pedantic, tok(c)->loc, "use of an empty initializer is a C23 extension");
}

/* ---------------- initializers ---------------- */

typedef struct cc_initializer_run {
	uint32_t lo, hi;	/* element range */
	uint32_t node;		/* value (0 while sub is being built) */
	struct cc_initializer_list *sub;
} cc_initializer_run;

typedef struct cc_initializer_list {
	ast_qualified_type ty;
	bool array, isunion, unknown;
	bool shared;		/* range elements share this list until a later write */
	uint64_t n;		/* fields, or array length (unknown: grows) */
	uint64_t max;		/* array: highest index initialized + 1 */
	uint32_t *slot;		/* struct/union: value per field */
	struct cc_initializer_list **sub;	/* struct/union: sub-list per field */
	cc_initializer_run *runs;		/* array: in order, later runs override earlier ones */
	uint32_t nrun, caprun;
	uint32_t ufield;	/* union: the initialized field */
	bool uset;
	bool excess;		/* -Wexcess-initializers reported for this list */
	uint32_t ubase;	/* union expanded from this value (cc_initializer_list_expand), 0: none */
} cc_initializer_list;

static cc_initializer_list *cc_initializer_list_new(cc *c, ast_qualified_type ty)
{
	cc_initializer_list *l = cc_heap_allocate(c, sizeof(cc_initializer_list));

	memset(l, 0, sizeof(*l));
	l->ty = ty;
	if (cc_type_is_vector(c, ty)) {
		l->array = true;
		l->n = AST_TYPE(c, ty)->n;
	} else if (cc_type_is_array(c, ty)) {
		l->array = true;
		l->unknown = (AST_TYPE(c, ty)->flags & AST_TYPE_FLAG_INCOMPLETE) != 0;
		l->n = l->unknown ? 0 : cc_type_array_length(c, ty);
	} else {
		const tag *r = &c->tags[AST_TYPE(c, ty)->aux];

		l->isunion = (r->flags & AST_RECORD_FLAG_UNION) != 0;
		l->n = r->nfield;
		l->slot = cc_heap_allocate(c, (r->nfield + 1) * sizeof(uint32_t));
		l->sub = cc_heap_allocate(c, (r->nfield + 1) * sizeof(cc_initializer_list *));
		memset(l->slot, 0, (r->nfield + 1) * sizeof(uint32_t));
		memset(l->sub, 0, (r->nfield + 1) * sizeof(cc_initializer_list *));
	}
	return l;
}

/* Copy only the mutable level being entered. Descendants remain shared until
   a designator reaches them, so a large range does not duplicate its tree. */
static cc_initializer_list *cc_initializer_list_copy(cc *c, const cc_initializer_list *from)
{
	cc_initializer_list *s = cc_heap_allocate(c, sizeof(*s));
	*s = *from;
	s->shared = false;
	if (s->array) {
		s->runs = cc_heap_allocate(c, s->caprun * sizeof(cc_initializer_run));
		if (s->nrun) memcpy(s->runs, from->runs, s->nrun * sizeof(cc_initializer_run));
		for (uint32_t i = 0; i < s->nrun; i++)
			if (s->runs[i].sub) s->runs[i].sub->shared = true;
	} else {
		s->slot = cc_heap_allocate(c, (s->n + 1) * sizeof(uint32_t));
		s->sub = cc_heap_allocate(c, (s->n + 1) * sizeof(cc_initializer_list *));
		memcpy(s->slot, from->slot, (s->n + 1) * sizeof(uint32_t));
		memcpy(s->sub, from->sub, (s->n + 1) * sizeof(cc_initializer_list *));
		for (uint64_t i = 0; i < s->n; i++)
			if (s->sub[i]) s->sub[i]->shared = true;
	}
	return s;
}

static ast_qualified_type cc_initializer_list_element_type(cc *c, const cc_initializer_list *l, uint64_t i)
{
	if (l->array)
		return AST_TYPE(c, l->ty)->base;
	return c->tags[AST_TYPE(c, l->ty)->aux].f[i].ty;
}

static void cc_initializer_list_run_set(cc *c, cc_initializer_list *l, uint64_t lo, uint64_t hi, uint32_t node, cc_initializer_list *sub)
{
	if (l->nrun == l->caprun) {
		uint32_t ncap = l->caprun ? l->caprun * 2 : 8;
		cc_initializer_run *nr = cc_heap_allocate(c, ncap * sizeof(cc_initializer_run));

		if (l->nrun)
			memcpy(nr, l->runs, l->nrun * sizeof(cc_initializer_run));
		l->runs = nr;
		l->caprun = ncap;
	}
	/* earlier runs this one covers are dropped, never evaluated (clang);
	   none can be when the run starts past all of them (the usual in-order
	   case: no scan, so a long list stays linear) */
	if (lo < l->max) {
		uint32_t k = 0;

		for (uint32_t j = 0; j < l->nrun; j++)
			if (!(l->runs[j].lo >= lo && l->runs[j].hi <= hi))
				l->runs[k++] = l->runs[j];
		l->nrun = k;
	}
	if (sub && lo != hi) sub->shared = true;
	l->runs[l->nrun++] = (cc_initializer_run){ (uint32_t)lo, (uint32_t)hi, node, sub };
	if (hi + 1 > l->max)
		l->max = hi + 1;
}

static void cc_initializer_list_set(cc *c, cc_initializer_list *l, uint64_t i, uint32_t node, cc_initializer_list *sub)
{
	if (l->array) {
		cc_initializer_list_run_set(c, l, i, i, node, sub);
		return;
	}
	if (l->isunion) {
		l->ubase = 0;
		for (uint64_t k = 0; k < l->n; k++)
			l->slot[k] = 0, l->sub[k] = NULL;
		l->ufield = (uint32_t)i;
		l->uset = true;
	}
	l->slot[i] = node;
	l->sub[i] = sub;
}

/* a designator reaches into a sub-object initialized by one value: as
   clang, keep what the value gives the other parts.  A string literal becomes
   one element per character; any other array or struct/union value one
   element or member access per part (arrays inside expanded in turn), so the
   value is read once per part: only without side effects. */
static void cc_initializer_list_expand(cc *c, cc_initializer_list *s, uint32_t v);

static void cc_initializer_list_put(cc *c, cc_initializer_list *s, uint64_t k, uint32_t m)
{
	ast_qualified_type ft = AST_NODE(m)->ty;

	if (cc_type_is_array(c, ft) && AST_NODE(m)->op != AST_NODE_STRING) {
		cc_initializer_list *t = cc_initializer_list_new(c, AST_QUALIFIED_TYPE_UNQUALIFIED(ft));

		cc_initializer_list_expand(c, t, m);
		cc_initializer_list_set(c, s, k, 0, t);
	} else {
		cc_initializer_list_set(c, s, k, m, NULL);
	}
}

static void cc_initializer_list_expand(cc *c, cc_initializer_list *s, uint32_t v)
{
	uint32_t loc = AST_NODE(v)->loc;

	if (s->array && AST_NODE(v)->op == AST_NODE_STRING) {
		const cstr *str = &c->strs[AST_NODE(v)->a];
		ast_qualified_type et = AST_TYPE(c, s->ty)->base;

		for (uint32_t k = 0; k < str->n && (s->unknown || k < s->n); k++) {
			i128 x = 0;

			memcpy(&x, str->s + (size_t)k * str->esz, str->esz);
			if (cc_type_is_signed(c, et))
				x = str->esz == 1 ? (int8_t)x : str->esz == 2 ? (int16_t)x : (int32_t)x;
			cc_initializer_list_run_set(c, s, k, k, cc_node_integer_new(c, x, et, loc), NULL);
		}
		return;
	}
	if (cc_expression_has_side_effects(c, v)) {
		cc_report_error(c, loc, "unsupported: a designator overrides part of an initializer with side effects");
		return;
	}
	if (s->array) {
		ast_qualified_type et = AST_TYPE(c, s->ty)->base;

		if (s->unknown || cc_type_is_vector(c, s->ty)) {
			cc_report_error(c, loc, "unsupported: a designator overrides part of this initializer");
			return;
		}
		for (uint64_t k = 0; k < s->n; k++) {
			uint32_t e = cc_node_new_operands(c, AST_NODE_INDEX, et, loc, cc_node_rvalue(c, v), cc_node_integer_new(c, (i128)k, c->t_ptrdiff, loc));

			AST_NODE(e)->flags = AST_NODE_FLAG_LVALUE;
			cc_initializer_list_put(c, s, k, e);
		}
		return;
	}
	{
		ast_qualified_type bt = AST_NODE(v)->ty;
		const tag *r = &c->tags[AST_TYPE(c, bt)->aux];

		for (uint32_t k = 0; k < r->nfield; k++) {
			const field *f = &r->f[k];
			uint32_t m;

			if (f->bitfield && !f->name)
				continue;
			m = cc_node_new(c, AST_NODE_MEMBER, cc_type_qualify(c, f->ty, AST_QUALIFIED_TYPE_QUALIFIERS(bt)), loc);
			AST_NODE(m)->a = v, AST_NODE(m)->b = AST_TYPE(c, bt)->aux, AST_NODE(m)->c = k;
			AST_NODE(m)->flags = (uint8_t)((AST_NODE(v)->flags & AST_NODE_FLAG_LVALUE) | (f->bitfield ? AST_NODE_FLAG_BITFIELD : 0));
			cc_initializer_list_put(c, s, k, m);
			if (s->isunion)
				break;	/* (its first member; cc_initializer_list_child reads another through v) */
		}
		if (s->isunion)
			s->ubase = v;
	}
}

/* the sub-list at i, created (replacing a plain value) if needed */
static cc_initializer_list *cc_initializer_list_child(cc *c, cc_initializer_list *l, uint64_t i)
{
	ast_qualified_type et = cc_initializer_list_element_type(c, l, i);
	uint32_t old = 0;

	if (l->array) {
		for (uint32_t k = i < l->max ? l->nrun : 0; k-- > 0;)
			if (l->runs[k].lo <= i && i <= l->runs[k].hi) {
				if (l->runs[k].sub) {
					cc_initializer_list *s = l->runs[k].sub;
					if (!s->shared) return s;
					s = cc_initializer_list_copy(c, s);
					cc_initializer_list_set(c, l, i, 0, s);
					return s;
				}
				if (!l->runs[k].sub)
					old = l->runs[k].node;
				break;
			}
	} else if (l->sub[i] && (!l->isunion || l->ufield == i)) {
		cc_initializer_list *s = l->sub[i];
		if (s->shared) {
			s = cc_initializer_list_copy(c, s);
			cc_initializer_list_set(c, l, i, 0, s);
		}
		return s;
	} else if (!l->isunion || (l->uset && l->ufield == i)) {
		old = l->slot[i];
	} else if (l->ubase) {	/* the same bytes through the member the designator picks */
		uint32_t v = l->ubase;
		ast_qualified_type bt = AST_NODE(v)->ty;

		old = cc_node_new(c, AST_NODE_MEMBER, cc_type_qualify(c, c->tags[AST_TYPE(c, bt)->aux].f[i].ty, AST_QUALIFIED_TYPE_QUALIFIERS(bt)), AST_NODE(v)->loc);
		AST_NODE(old)->a = v, AST_NODE(old)->b = AST_TYPE(c, bt)->aux, AST_NODE(old)->c = (uint32_t)i;
		AST_NODE(old)->flags = (uint8_t)(AST_NODE(v)->flags & AST_NODE_FLAG_LVALUE);
	}
	{
		cc_initializer_list *s = cc_initializer_list_new(c, et);

		if (old && cc_type_is_initializer_aggregate(c, et))
			cc_initializer_list_expand(c, s, old);
		cc_initializer_list_set(c, l, i, 0, s);
		return s;
	}
}

static bool cc_initializer_list_is_full(const cc_initializer_list *l, uint64_t pos)
{
	if (l->array)
		return !l->unknown && pos >= l->n;
	/* Unnamed bit-fields can precede the first initializable union member. */
	if (l->isunion)
		return pos >= l->n || l->uset;
	return pos >= l->n;
}

/* skip unnamed bit-fields (they take no initializer) */
static uint64_t cc_initializer_list_next_position(cc *c, const cc_initializer_list *l, uint64_t pos)
{
	if (!l->array) {
		const tag *r = &c->tags[AST_TYPE(c, l->ty)->aux];

		while (pos < r->nfield && r->f[pos].bitfield && !r->f[pos].name)
			pos++;
	}
	return pos;
}

static uint32_t cc_initializer_list_finish(cc *c, cc_initializer_list *l, uint32_t loc);
static void cc_initializer_list_continue(cc *c, cc_initializer_list *l, uint64_t pos, bool st);
static void cc_initializer_list_fill(cc *c, cc_initializer_list *l, uint64_t pos, bool braced, uint32_t first, bool st);
static uint32_t cc_initializer_braced_scalar_parse(cc *c, ast_qualified_type ty, bool st);

/* a braced initializer skipped (excess elements): its range */
static diag_span cc_initializer_braces_skip(cc *c)
{
	int depth = 0;
	diag_span r = { tok(c)->loc, 0 };

	do {
		r.e = tok(c)->loc;
		if (cc_token_is(c, LEX_TOKEN_LEFT_BRACE))
			depth++;
		else if (cc_token_is(c, LEX_TOKEN_RIGHT_BRACE))
			depth--;
		else if (cc_token_is(c, LEX_TOKEN_EOF))
			cc_report_fatal(c, tok(c)->loc, "expected '}'");
		next(c);
	} while (depth > 0);
	r.e = pp_token_end_find(c->p, r.e);
	return r;
}

/* a string literal for a char array of known size: longer than it (the terminator may go) */
static void cc_initializer_string_fit_check(cc *c, ast_qualified_type arr, uint32_t s)
{
	if (unlikely(c->constexpr_init) && cc_type_is_array(c, arr) && AST_NODE(s)->op == AST_NODE_STRING) {
		const cstr *str = &c->strs[AST_NODE(s)->a];
		ast_qualified_type from = AST_TYPE(c, AST_NODE(s)->ty)->base, to = AST_TYPE(c, arr)->base;
		uint64_t count = str->n;

		if (!(AST_TYPE(c, arr)->flags & AST_TYPE_FLAG_INCOMPLETE) && count > cc_type_array_length(c, arr))
			count = cc_type_array_length(c, arr);
		for (uint64_t i = 0; i < count; i++) {
			i128 value = 0;

			for (unsigned k = 0; k < str->esz; k++)
				value |= (i128)(unsigned char)str->s[i * str->esz + k] << (8*k);
			value = cc_constant_truncate(c, value, from);
			if (cc_constant_truncate(c, value, to) != value) {
				char text[48];

				cc_format_i128(text, sizeof text, value);
				cc_report_error(c, AST_NODE(s)->loc, "constexpr initializer evaluates to %s which is not exactly representable in type '%s'", text, tyname(c, to));
				break;
			}
		}
	}
	if (cc_type_is_array(c, arr) && !(AST_TYPE(c, arr)->flags & (AST_TYPE_FLAG_INCOMPLETE | AST_TYPE_FLAG_VLA)) && AST_NODE(s)->op == AST_NODE_STRING &&
	    c->strs[AST_NODE(s)->a].esz == 1 && c->strs[AST_NODE(s)->a].n - 1 > AST_TYPE(c, arr)->n) {
		diag_span r = cc_node_range(c, s);

		cc_report_warning(c, "excess-initializers", true, NULL, r.b, &r, 1, NULL, 0, "initializer-string for char array is too long");
	}
}

/* clang's -Wexcess-initializers, once per list: at the first excess element */
static void cc_initializer_excess_warn(cc *c, ast_qualified_type ty, bool *done, diag_span r)
{
	const char *k = cc_type_is_array(c, ty) ? "array" : cc_type_is_vector(c, ty) ? "vector" : AST_TYPE_KIND(c, ty) == AST_TYPE_UNION ? "union" :
			cc_type_is_record(c, ty) ? "struct" : "scalar";

	if (*done)
		return;
	*done = true;
	cc_report_warning(c, "excess-initializers", true, NULL, r.b, &r, 1, NULL, 0, "excess elements in %s initializer", k);
}

static bool cc_type_is_char_array(cc *c, ast_qualified_type t)
{
	if (!cc_type_is_array(c, t))
		return false;
	t = AST_TYPE(c, t)->base;
	return cc_type_is_integer(c, t) && (cc_type_size(c, t) == 1 || cc_type_size(c, t) == 2 || cc_type_size(c, t) == 4) && AST_TYPE_KIND(c, t) != AST_TYPE_BOOL;
}

static bool cc_initializer_string_initializes(cc *c, ast_qualified_type arr, uint32_t v)
{
	uint32_t s = v;

	while (AST_NODE(s)->flags & AST_NODE_FLAG_PARENTHESIZED && AST_NODE(s)->op != AST_NODE_STRING)
		break;
	return AST_NODE(s)->op == AST_NODE_STRING && cc_type_is_char_array(c, arr) &&
	       c->strs[AST_NODE(s)->a].esz == cc_type_size(c, AST_TYPE(c, arr)->base);
}

/*
 * at '{': { "str" } follows, not { "q"[0] } (an element of it).  The token
 * queue holds 4: concatenated strings ("a" "b") are taken as the string.
 */
static bool cc_initializer_is_braced_string(cc *c)
{
	int k;

	if (!cc_token_kind_is_string(cc_token_peek(c, 1)->kind))
		return false;
	k = cc_token_peek(c, 2)->kind;
	if (cc_token_kind_is_string(k) || k == LEX_TOKEN_RIGHT_BRACE)
		return true;
	return k == LEX_TOKEN_COMMA && cc_token_peek(c, 3)->kind == LEX_TOKEN_RIGHT_BRACE;
}

/* one element at pos: braces, a value, or brace elision into a sub-aggregate */
static void cc_initializer_list_element_parse(cc *c, cc_initializer_list *l, uint64_t pos, uint32_t v, bool st)
{
	ast_qualified_type et = cc_initializer_list_element_type(c, l, pos);

	if (!v && cc_token_is(c, LEX_TOKEN_LEFT_BRACE)) {
		if (cc_type_is_char_array(c, et) && cc_initializer_is_braced_string(c)) {	/* { "string" } */
			uint32_t sv;

			next(c), c->ibr++;
			sv = cc_literal_string_parse(c);
			cc_token_accept(c, LEX_TOKEN_COMMA);
			cc_token_expect(c, LEX_TOKEN_RIGHT_BRACE, "'}'"), c->ibr--;
			cc_initializer_string_fit_check(c, et, sv);
			cc_initializer_list_set(c, l, pos, sv, NULL);
			return;
		}
		if (cc_type_is_initializer_aggregate(c, et)) {
			cc_initializer_list *s = cc_initializer_list_new(c, et);

			uint32_t ob = tok(c)->loc;

			cc_initializer_empty_extension_check(c);
			next(c), c->ibr++;
			cc_initializer_list_fill(c, s, 0, true, 0, st);
			cc_token_expect_match(c, LEX_TOKEN_RIGHT_BRACE, ob), c->ibr--;
			cc_initializer_list_set(c, l, pos, 0, s);
		} else {
			cc_initializer_list_set(c, l, pos, cc_initializer_braced_scalar_parse(c, et, st), NULL);
		}
		return;
	}
	if (!v)
		v = cc_assignment_parse(c);
	if ((cc_type_is_initializer_aggregate(c, et)) && !cc_initializer_string_initializes(c, et, v) &&
	    !((cc_type_is_record(c, et) || cc_type_is_vector(c, et)) && cc_type_is_compatible(c, cc_type_unatomic(c, AST_NODE(v)->ty), cc_type_unatomic(c, et)))) {
		/* Brace elision writes the first subobjects; unlike an explicit
		 * braced initializer it does not discard earlier sibling values. */
		cc_initializer_list *s = cc_initializer_list_child(c, l, pos);

		cc_initializer_list_fill(c, s, 0, false, v, st);
		return;
	}
	if (cc_type_is_array(c, et)) {
		cc_initializer_string_fit_check(c, et, v);
		cc_initializer_list_set(c, l, pos, v, NULL);	/* string literal */
		return;
	}
	if (!l->array && cc_type_is_record(c, l->ty) && c->tags[AST_TYPE(c, l->ty)->aux].f[pos].bitfield)
		cc_conversion_bitfield_constant_check(c, &c->tags[AST_TYPE(c, l->ty)->aux].f[pos], cc_conversion_decay(c, v), 0);
	if (unlikely(c->constexpr_init))
		cc_constexpr_conversion_check(c, cc_conversion_decay(c, v), et);
	cc_initializer_list_set(c, l, pos, cc_type_is_record(c, et) && !cc_type_is_complex(c, et) ? cc_conversion_decay(c, v) : cc_conversion_apply(c, cc_conversion_decay(c, v), et), NULL);
}

/* a designation starts here: ".f", "[i]" or the old GNU "f:" */
static bool cc_token_at_designator(cc *c, int k)
{
	int t = cc_token_peek(c, k)->kind;

	return t == LEX_TOKEN_PERIOD || t == LEX_TOKEN_LEFT_SQUARE || (t == LEX_TOKEN_IDENTIFIER && cc_token_peek(c, k + 1)->kind == LEX_TOKEN_COLON);
}

/* after an element of an unbraced (elided) list: go on with the next one if it belongs here */
static void cc_initializer_list_continue(cc *c, cc_initializer_list *l, uint64_t pos, bool st)
{
	pos = cc_initializer_list_next_position(c, l, pos);
	if (cc_initializer_list_is_full(l, pos) || !cc_token_is(c, LEX_TOKEN_COMMA))
		return;
	{
		if (cc_token_peek(c, 1)->kind == LEX_TOKEN_RIGHT_BRACE || cc_token_at_designator(c, 1))
			return;
	}
	next(c);
	cc_initializer_list_fill(c, l, pos, false, 0, st);
}

/* a designation; returns the position after the top-level designated element */
static uint64_t cc_initializer_list_designation_path(cc *c, cc_initializer_list *l, bool st, cc_id_list *cursor)
{
	uint64_t lo, hi;
	uint32_t path[CC_MEMBER_FIND_DEPTH_MAX];
	int depth = 1;

	if (cc_token_accept(c, LEX_TOKEN_PERIOD) || (cc_token_is(c, LEX_TOKEN_IDENTIFIER) && cc_token_peek(c, 1)->kind == LEX_TOKEN_COLON)) {
		uint32_t name;
		int d;

		if (!cc_token_is(c, LEX_TOKEN_IDENTIFIER))
			cc_report_fatal(c, tok(c)->loc, "expected a field designator, such as '.field = 4'");
		name = tok(c)->id;
		if (l->array) {
			cc_report_error(c, tok(c)->loc, "field designator cannot initialize a non-struct, non-union type '%s'", tyname(c, l->ty));
			next(c);
			return 0;
		}
		d = cc_member_find(c, AST_TYPE(c, l->ty)->aux, name, path, 0);
		if (d < 0) {
			cc_report_error(c, tok(c)->loc, "field designator '%.*s' does not refer to any field in type '%s'", CC_IDENTIFIER(name),
				 tyname(c, l->ty));
			next(c);
			cc_token_skip_balanced(c);
			return l->n;
		}
		if (unlikely(c->field_uses))
			cc_field_uses_path_record(c, FU_DESIGNATOR, AST_TYPE(c, l->ty)->aux, path, d);
		next(c);
		depth = d;
		lo = hi = path[0];
	} else {
		i128 a, b;

		cc_token_expect(c, LEX_TOKEN_LEFT_SQUARE, "'['");
		cc_constant_expression_parse(c, &a, NULL);
		b = a;
		if (cc_token_accept(c, LEX_TOKEN_ELLIPSIS))
			cc_constant_expression_parse(c, &b, NULL);
		cc_token_expect(c, LEX_TOKEN_RIGHT_SQUARE, "']'");
		if (!l->array) {
			cc_report_error(c, tok(c)->loc, "array designator cannot initialize non-array type '%s'", tyname(c, l->ty));
			return 0;
		}
		if (a < 0 || b < a || (!l->unknown && (uint64_t)b >= l->n)) {
			if (a < 0)
				cc_report_error(c, tok(c)->loc, "array designator value '%lld' is negative", (long long)a);
			else
				cc_report_error(c, tok(c)->loc, "array designator index (%lld) exceeds array bounds (%llu)",
					 (long long)(b < a ? a : b), (unsigned long long)l->n);
			return (uint64_t)(b < 0 ? 0 : b) + 1;
		}
		lo = (uint64_t)a, hi = (uint64_t)b;
	}
	/* Remember the final selected element at every level. Undesignated
	 * elements resume there after the entire range has been installed. */
	if (depth > 1) {
		for (int i = 0; i < depth; i++) cc_id_list_append(c, cursor, path[i]);
	} else {
		cc_id_list_append(c, cursor, (uint32_t)hi);
	}
	/* path through anonymous members acts as nested designators */
	if (depth > 1) {
		cc_initializer_list *s = l;

		for (int i = 0; i < depth - 1; i++)
			s = cc_initializer_list_child(c, s, path[i]);
		{
			uint64_t last = path[depth - 1];

			if (cc_token_is(c, LEX_TOKEN_PERIOD) || cc_token_is(c, LEX_TOKEN_LEFT_SQUARE)) {
				cc_initializer_list *t = cc_initializer_list_child(c, s, last);
				cc_initializer_list_designation_path(c, t, st, cursor);
			} else {
				if (!cc_token_accept(c, LEX_TOKEN_EQUAL))
					cc_token_accept(c, LEX_TOKEN_COLON);
				cc_initializer_list_element_parse(c, s, last, 0, st);
			}
		}
		return lo + 1;
	}
	if (cc_token_is(c, LEX_TOKEN_PERIOD) || cc_token_is(c, LEX_TOKEN_LEFT_SQUARE)) {
		cc_initializer_list *t = cc_initializer_list_child(c, l, lo);
		cc_initializer_list_designation_path(c, t, st, cursor);
		if (lo != hi) t->shared = true;
		for (uint64_t i = lo + 1; i <= hi; i++)
			cc_initializer_list_set(c, l, i, 0, t);	/* range of the same sub-object */
		return hi + 1;
	}
	if (!cc_token_accept(c, LEX_TOKEN_EQUAL))
		cc_token_accept(c, LEX_TOKEN_COLON);	/* old GNU "field: value" */
	if (lo == hi) {
		cc_initializer_list_element_parse(c, l, lo, 0, st);
	} else {
		/* [lo ... hi] = v: one value for the whole range */
		ast_qualified_type et = cc_initializer_list_element_type(c, l, lo);
		uint32_t v;

		if (cc_token_is(c, LEX_TOKEN_LEFT_BRACE)) {
			if (cc_type_is_initializer_aggregate(c, et)) {
				cc_initializer_list *s = cc_initializer_list_new(c, et);

				uint32_t ob = tok(c)->loc;

				cc_initializer_empty_extension_check(c);
				next(c), c->ibr++;
				cc_initializer_list_fill(c, s, 0, true, 0, st);
				cc_token_expect_match(c, LEX_TOKEN_RIGHT_BRACE, ob), c->ibr--;
				cc_initializer_list_run_set(c, l, lo, hi, 0, s);
			} else {
				cc_initializer_list_run_set(c, l, lo, hi, cc_initializer_braced_scalar_parse(c, et, st), NULL);
			}
		} else {
			v = cc_assignment_parse(c);
			if ((cc_type_is_initializer_aggregate(c, et)) && !cc_initializer_string_initializes(c, et, v) &&
			    !((cc_type_is_record(c, et) || cc_type_is_vector(c, et)) && cc_type_is_compatible(c, cc_type_unatomic(c, AST_NODE(v)->ty), cc_type_unatomic(c, et)))) {
				cc_initializer_list *s = cc_initializer_list_new(c, et);

				cc_initializer_list_fill(c, s, 0, false, v, st);
				cc_initializer_list_run_set(c, l, lo, hi, 0, s);
			} else {
				if (!(cc_type_is_array(c, et) || cc_type_is_record(c, et) || cc_type_is_vector(c, et))) {
					if (unlikely(c->constexpr_init))
						cc_constexpr_conversion_check(c, cc_conversion_decay(c, v), et);
					v = cc_conversion_apply(c, cc_conversion_decay(c, v), et);
				}
				cc_initializer_list_run_set(c, l, lo, hi, (AST_TYPE(c, AST_NODE(v)->ty)->flags & AST_TYPE_FLAG_ATOMIC) ? cc_conversion_decay(c, v) : v, NULL);
			}
		}
	}
	return hi + 1;
}

/* Continue inside only the last element selected by a designator chain.
   Copy shared ancestors before writing any positional continuation. */
static uint64_t cc_initializer_list_designation(cc *c, cc_initializer_list *l, bool st)
{
	cc_id_list cursor;
	cc_id_list_init(&cursor);
	uint64_t pos = cc_initializer_list_designation_path(c, l, st, &cursor);
	if (cursor.n > 1) {
		cc_initializer_list *small[16], **parents = cursor.n <= 16 ? small : cc_heap_allocate(c, cursor.n * sizeof(*parents));
		parents[0] = l;
		for (uint32_t i = 1; i < cursor.n; i++)
			parents[i] = cc_initializer_list_child(c, parents[i - 1], cursor.v[i - 1]);
		for (uint32_t i = cursor.n; --i > 0;)
			cc_initializer_list_continue(c, parents[i], (uint64_t)cursor.v[i] + 1, st);
	}
	return pos;
}

/* clang's -Wexcess-initializers, once per list; the elements are dropped */
static void cc_initializer_list_excess(cc *c, cc_initializer_list *l, uint32_t loc)
{
	const char *k = l->array ? (cc_type_is_vector(c, l->ty) ? "vector" : "array") : l->isunion ? "union" : "struct";

	if (l->excess)
		return;
	l->excess = true;
	cc_report_warning(c, "excess-initializers", true, NULL, loc, NULL, 0, NULL, 0, "excess elements in %s initializer", k);
}

static void cc_initializer_list_fill(cc *c, cc_initializer_list *l, uint64_t pos, bool braced, uint32_t first, bool st)
{
	bool excess = false;

	pos = cc_initializer_list_next_position(c, l, pos);
	if (first) {
		if (cc_initializer_list_is_full(l, pos)) {
			cc_initializer_list_excess(c, l, AST_NODE(first)->loc);
			return;
		}
		cc_initializer_list_element_parse(c, l, pos, first, st);
		pos = cc_initializer_list_next_position(c, l, pos + 1);
		if (!braced) {
			if (cc_initializer_list_is_full(l, pos) || !cc_token_is(c, LEX_TOKEN_COMMA) || cc_token_peek(c, 1)->kind == LEX_TOKEN_RIGHT_BRACE || cc_token_at_designator(c, 1))
				return;
			next(c);
		} else if (!cc_token_accept(c, LEX_TOKEN_COMMA)) {
			return;
		}
	}
	for (;;) {
		if (cc_token_is(c, LEX_TOKEN_RIGHT_BRACE))
			return;
		if (cc_token_at_designator(c, 0)) {
			if (!braced)
				return;
			pos = cc_initializer_list_designation(c, l, st);
		} else {
			if (!braced && cc_initializer_list_is_full(l, pos))
				return;
			if (cc_initializer_list_is_full(l, pos)) {
				/* excess elements: clang warns and drops them */
				if (cc_token_is(c, LEX_TOKEN_LEFT_BRACE)) {
					cc_initializer_excess_warn(c, l->ty, &excess, cc_initializer_braces_skip(c));
				} else {
					uint32_t v = cc_assignment_parse(c);

					cc_initializer_excess_warn(c, l->ty, &excess, cc_node_range(c, v));
				}
			} else {
				cc_initializer_list_element_parse(c, l, pos, 0, st);
				pos++;
			}
		}
		pos = cc_initializer_list_next_position(c, l, pos);
		if (!braced) {
			if (cc_initializer_list_is_full(l, pos) || !cc_token_is(c, LEX_TOKEN_COMMA) || cc_token_peek(c, 1)->kind == LEX_TOKEN_RIGHT_BRACE || cc_token_at_designator(c, 1))
				return;
			next(c);
			continue;
		}
		if (!cc_token_accept(c, LEX_TOKEN_COMMA))
			return;
	}
}

/* AST_NODE_INITIALIZER: struct/union: aux 0, list of nfield values; array: aux 1, list of (lo, hi, value) */
static uint32_t cc_initializer_list_finish(cc *c, cc_initializer_list *l, uint32_t loc)
{
	uint32_t n;
	ast_qualified_type ty = l->ty;
	cc_id_list v;

	cc_id_list_init(&v);
	if (l->array && l->unknown)
		ty = cc_type_array(c, AST_TYPE(c, ty)->base, l->max, 0);
	n = cc_node_new(c, AST_NODE_INITIALIZER, ty, loc);
	if (l->array) {
		for (uint32_t i = 0; i < l->nrun; i++) {
			cc_initializer_run *r = &l->runs[i];
			uint32_t x = r->sub ? cc_initializer_list_finish(c, r->sub, loc) : r->node;

			cc_id_list_append(c, &v, r->lo);
			cc_id_list_append(c, &v, r->hi);
			cc_id_list_append(c, &v, x);
		}
		AST_NODE(n)->aux = 1;
		AST_NODE(n)->c = v.n / 3;
	} else {
		for (uint64_t i = 0; i < l->n; i++)
			cc_id_list_append(c, &v, l->sub[i] ? cc_initializer_list_finish(c, l->sub[i], loc) : l->slot[i]);
		AST_NODE(n)->aux = l->isunion ? 2 : 0;
		AST_NODE(n)->c = v.n;
		AST_NODE(n)->a = l->ufield;
	}
	AST_NODE(n)->b = cc_id_list_freeze(c, &v);
	return n;
}

/* { ... } for a scalar: the first element */
static uint32_t cc_initializer_braced_scalar_parse(cc *c, ast_qualified_type ty, bool st)
{
	uint32_t v = 0, loc = tok(c)->loc;

	cc_initializer_empty_extension_check(c);
	cc_token_expect(c, LEX_TOKEN_LEFT_BRACE, "'{'");
	if (cc_token_is(c, LEX_TOKEN_RIGHT_BRACE)) {
		next(c);
		return cc_node_new(c, AST_NODE_ZERO, AST_QUALIFIED_TYPE_UNQUALIFIED(ty), loc);
	}
	c->ibr++;
	if (cc_token_is(c, LEX_TOKEN_LEFT_BRACE))
		v = cc_initializer_braced_scalar_parse(c, ty, st);
	else
		v = cc_conversion_apply_as(c, cc_conversion_decay(c, cc_assignment_parse(c)), ty, c->cv_what == CC_CONVERSION_CONTEXT_INITIALIZER ? CC_CONVERSION_CONTEXT_INITIALIZER : 0, c->cv_loc);
	{
		bool excess = false;

		while (cc_token_accept(c, LEX_TOKEN_COMMA)) {
			if (cc_token_is(c, LEX_TOKEN_RIGHT_BRACE))
				break;
			if (cc_token_is(c, LEX_TOKEN_LEFT_BRACE))
				cc_initializer_excess_warn(c, ty, &excess, cc_initializer_braces_skip(c));
			else
				cc_initializer_excess_warn(c, ty, &excess, cc_node_range(c, cc_assignment_parse(c)));
		}
	}
	cc_token_expect(c, LEX_TOKEN_RIGHT_BRACE, "'}'"), c->ibr--;
	return v;
}

/* Clang's GNU array-copy extension accepts nonconstant reads, but not
   observable effects in the compound literal (including volatile reads). */
static bool cc_initializer_array_copy_has_effects(cc *c, uint32_t n, bool load, unsigned depth)
{
	if (!n) return false;
	if (depth > 128) return true;
	const node *x = AST_NODE(n);
	if (load && (x->flags & AST_NODE_FLAG_LVALUE) && (AST_QUALIFIED_TYPE_QUALIFIERS(x->ty) & AST_QUALIFIER_VOLATILE))
		return true;
	switch (x->op) {
	case AST_NODE_INITIALIZER:
		for (uint32_t i = 0; i < x->c; i++)
			if (cc_initializer_array_copy_has_effects(c, c->lists[x->b + (x->aux == 1 ? 3 * i + 2 : i)], true, depth + 1))
				return true;
		return false;
	case AST_NODE_COMPOUND_LITERAL: case AST_NODE_UNARY: case AST_NODE_DEREFERENCE:
		return cc_initializer_array_copy_has_effects(c, x->a, true, depth + 1);
	case AST_NODE_ADDRESS: case AST_NODE_MEMBER:
		return cc_initializer_array_copy_has_effects(c, x->a, false, depth + 1);
	case AST_NODE_CAST:
		return cc_initializer_array_copy_has_effects(c, x->a, x->aux != AST_CAST_ARRAY_DECAY && x->aux != AST_CAST_FUNCTION_DECAY, depth + 1);
	case AST_NODE_CALL: {
		uint32_t fn = x->a;
		while (AST_NODE(fn)->op == AST_NODE_CAST) fn = AST_NODE(fn)->a;
		if (AST_NODE(fn)->op != AST_NODE_VARIABLE) return true;
		const sym *f = &c->syms[AST_NODE(fn)->a];
		if (!f->attrs || !c->attr[f->attrs].pure) return true;
		for (uint32_t i = 0; i < x->c; i++)
			if (cc_initializer_array_copy_has_effects(c, c->lists[x->b + i], true, depth + 1)) return true;
		return false;
	}
	case AST_NODE_BUILTIN: {
		cc_value v;
		return !cc_constant_eval(c, n, &v);
	}
	case AST_NODE_INDEX: case AST_NODE_BINARY: case AST_NODE_LOGICAL_AND: case AST_NODE_LOGICAL_OR: case AST_NODE_COMMA: case AST_NODE_COMPLEX:
		return cc_initializer_array_copy_has_effects(c, x->a, true, depth + 1) || cc_initializer_array_copy_has_effects(c, x->b, true, depth + 1);
	case AST_NODE_CONDITIONAL:
		return cc_initializer_array_copy_has_effects(c, x->a, true, depth + 1) || cc_initializer_array_copy_has_effects(c, x->b, true, depth + 1) ||
		       cc_initializer_array_copy_has_effects(c, x->c, true, depth + 1);
	default:
		return cc_expression_has_side_effects(c, n);
	}
}

CC_PRIVATE uint32_t cc_initializer_parse(cc *c, ast_qualified_type *ty, bool st)
{
	uint32_t loc = tok(c)->loc;
	ast_qualified_type t = *ty;

	if (cc_token_is(c, LEX_TOKEN_LEFT_BRACE)) {
		if (cc_type_is_char_array(c, t) && cc_initializer_is_braced_string(c)) {
			uint32_t s;

			next(c), c->ibr++;
			s = cc_literal_string_parse(c);
			cc_token_accept(c, LEX_TOKEN_COMMA);
			cc_token_expect(c, LEX_TOKEN_RIGHT_BRACE, "'}'"), c->ibr--;
			cc_initializer_string_fit_check(c, t, s);
			if (AST_TYPE(c, t)->flags & AST_TYPE_FLAG_INCOMPLETE)
				*ty = AST_QUALIFIED_TYPE(AST_QUALIFIED_TYPE_INDEX(cc_type_array(c, AST_TYPE(c, t)->base, c->strs[AST_NODE(s)->a].n, 0)), AST_QUALIFIED_TYPE_QUALIFIERS(t));
			return s;
		}
		if (cc_type_is_initializer_aggregate(c, t)) {
			cc_initializer_list *l;
			uint32_t n;

			if (cc_type_is_record(c, t) && !cc_type_complete(c, t)) {
				cc_report_error(c, c->cv_what == CC_CONVERSION_CONTEXT_INITIALIZER ? c->cv_loc : loc, "variable has incomplete type '%s'", tyname(c, t));
				cc_report_forward_note(c, t);
				cc_token_skip_balanced(c);
				return cc_node_new(c, AST_NODE_ZERO, t, loc);
			}
			l = cc_initializer_list_new(c, AST_QUALIFIED_TYPE_UNQUALIFIED(t));
			uint32_t ob = tok(c)->loc;

			cc_initializer_empty_extension_check(c);
			next(c), c->ibr++;
			cc_initializer_list_fill(c, l, 0, true, 0, st);
			cc_token_expect_match(c, LEX_TOKEN_RIGHT_BRACE, ob), c->ibr--;
			n = cc_initializer_list_finish(c, l, loc);
			*ty = AST_QUALIFIED_TYPE(AST_QUALIFIED_TYPE_INDEX(AST_NODE(n)->ty), AST_QUALIFIED_TYPE_QUALIFIERS(t));
			return n;
		}
		return cc_initializer_braced_scalar_parse(c, t, st);
	}
	{
		uint32_t v = cc_assignment_parse(c);

		if (cc_type_is_array(c, t)) {
			if (AST_NODE(v)->op == AST_NODE_COMPOUND_LITERAL && cc_type_is_array(c, AST_NODE(v)->ty)) {
				uint32_t at = c->cv_what == CC_CONVERSION_CONTEXT_INITIALIZER ? c->cv_loc : loc;
				if (!cc_type_is_compatible(c, t, AST_NODE(v)->ty)) {
					cc_report_error(c, at, "cannot initialize array of type '%s' with array of type '%s'",
						 tyname(c, t), tyname(c, AST_NODE(v)->ty));
					return v;
				}
				if (cc_initializer_array_copy_has_effects(c, AST_NODE(v)->a, true, 0)) {
					cc_report_error(c, at, "cannot initialize array of type '%s' with non-constant array of type '%s'",
						 tyname(c, t), tyname(c, AST_NODE(v)->ty));
					return v;
				}
				CC_REPORT_EXTENSION(c, "gnu-compound-literal-initializer", cc_warning_group_gnu, at,
					 "initialization of an array of type '%s' from a compound literal of type '%s' is a GNU extension",
					 tyname(c, t), tyname(c, AST_NODE(v)->ty));
				if (AST_TYPE(c, t)->flags & AST_TYPE_FLAG_INCOMPLETE)
					*ty = AST_QUALIFIED_TYPE(AST_QUALIFIED_TYPE_INDEX(AST_NODE(v)->ty), AST_QUALIFIED_TYPE_QUALIFIERS(t));
				return v;
			}
			if (!cc_initializer_string_initializes(c, t, v)) {
				cc_report_error(c, c->cv_what == CC_CONVERSION_CONTEXT_INITIALIZER ? c->cv_loc : loc, "array initializer must be an initializer list");
				return v;
			}
			cc_initializer_string_fit_check(c, t, v);
			if (AST_TYPE(c, t)->flags & AST_TYPE_FLAG_INCOMPLETE)
				*ty = AST_QUALIFIED_TYPE(AST_QUALIFIED_TYPE_INDEX(cc_type_array(c, AST_TYPE(c, t)->base, c->strs[AST_NODE(v)->a].n, 0)), AST_QUALIFIED_TYPE_QUALIFIERS(t));
			return v;
		}
		if (cc_type_is_record(c, t) && !cc_type_is_complex(c, t)) {
			if (!cc_type_is_compatible(c, cc_type_unatomic(c, AST_NODE(v)->ty), cc_type_unatomic(c, t)))
				cc_report_error(c, loc, "initializing with an expression of incompatible type");
			return cc_conversion_decay(c, v);
		}
		return cc_conversion_apply_as(c, cc_conversion_decay(c, v), t, c->cv_what == CC_CONVERSION_CONTEXT_INITIALIZER ? CC_CONVERSION_CONTEXT_INITIALIZER : 0, c->cv_loc);
	}
}

/* clang's location for a diagnostic about expression n (getExprLoc: a call's callee) */
CC_PRIVATE uint32_t cc_expression_location(cc *c, uint32_t n)
{
	while (AST_NODE(n)->op == AST_NODE_CAST && AST_NODE(n)->a && !c->span[n].e)
		n = AST_NODE(n)->a;
	if (AST_NODE(n)->op == AST_NODE_CALL && AST_NODE(n)->a)
		return cc_node_range(c, AST_NODE(n)->a).b;
	return AST_NODE(n)->loc;
}

/*
 * Whether init can initialise an object of static storage of type ty: the
 * walk of the emitter's data_init (codegen/data.c) without writing, so the error
 * comes from the front end, also for an object never emitted (clang Sema).
 * *bad: the first element that is not a constant.
 */
CC_PRIVATE bool cc_initializer_static_is_ok(cc *c, ast_qualified_type ty, uint32_t init, uint32_t *bad)
{
	const node *x;
	cc_value cv;

	if (!init)
		return true;
	x = AST_NODE(init);
	if (x->op == AST_NODE_ZERO || ((x->flags & AST_NODE_FLAG_ERROR) && !c->sie))
		return true;
	if (x->op == AST_NODE_INITIALIZER) {
		const uint32_t *v = &c->lists[x->b];

		if (x->aux == 1) {
			for (uint32_t i = 0; i < x->c; i++)
				if (!cc_initializer_static_is_ok(c, AST_TYPE(c, ty)->base, v[3 * i + 2], bad))
					return false;
			return true;
		}
		const tag *r = &c->tags[AST_TYPE(c, ty)->aux];

		for (uint32_t i = 0; i < x->c && i < r->nfield; i++) {
			const field *f = &r->f[i];

			if ((x->aux == 2 && i != x->a) || !v[i])
				continue;
			if (f->bitfield) {
				if (!cc_constant_eval(c, v[i], &cv) || (cv.kind != CC_VALUE_INTEGER && cv.kind != CC_VALUE_BITINT)) {
					*bad = v[i];
					return false;
				}
				continue;
			}
			if (!cc_initializer_static_is_ok(c, f->ty, v[i], bad))
				return false;
		}
		return true;
	}
	if (x->op == AST_NODE_COMMA) {
		if (cc_initializer_static_is_ok(c, AST_NODE(x->a)->ty, x->a, bad) && cc_initializer_static_is_ok(c, ty, x->b, bad))
			return true;
		*bad = init;
		return false;
	}
	{
		uint32_t selected;

		if (cc_initializer_subobject_find(c, init, &selected))
			return cc_initializer_static_is_ok(c, ty, selected, bad);
	}
	if (x->op == AST_NODE_STRING && cc_type_is_array(c, ty))
		return true;
	if (x->op == AST_NODE_COMPOUND_LITERAL)
		return cc_initializer_static_is_ok(c, ty, x->a, bad);
	if (x->op == AST_NODE_COMPLEX) {
		ast_qualified_type et = c->tags[AST_TYPE(c, x->ty)->aux].f[0].ty;

		return cc_initializer_static_is_ok(c, et, x->a, bad) && cc_initializer_static_is_ok(c, et, x->b, bad);
	}
	if (x->op == AST_NODE_CAST && x->aux == AST_CAST_NOP && (cc_type_is_record(c, ty) || cc_type_is_array(c, ty) || cc_type_is_vector(c, ty)))
		return cc_initializer_static_is_ok(c, ty, x->a, bad);
	if ((cc_type_is_record(c, ty) || cc_type_is_array(c, ty) || cc_type_is_vector(c, ty))) {
		uint32_t m = init;

		while (AST_NODE(m)->op == AST_NODE_CAST)
			m = AST_NODE(m)->a;
		if (AST_NODE(m)->op == AST_NODE_VARIABLE && c->syms[AST_NODE(m)->a].body)
			return true;
		*bad = init;
		return false;
	}
	if (!cc_constant_eval(c, init, &cv) || (cv.kind != CC_VALUE_INTEGER && cv.kind != CC_VALUE_BITINT && cv.kind != CC_VALUE_FLOAT && cv.kind != CC_VALUE_ADDRESS)) {
		*bad = init;
		return false;
	}
	return true;
}
