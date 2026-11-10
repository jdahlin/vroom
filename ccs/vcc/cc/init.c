/* vcc front end: initializers */
#include "ccimpl.h"

/* ---------------- initializers ---------------- */

typedef struct irun {
	uint32_t lo, hi;	/* element range */
	uint32_t node;		/* value (0 while sub is being built) */
	struct ilist *sub;
} irun;

typedef struct ilist {
	qty ty;
	bool array, isunion, unknown;
	uint64_t n;		/* fields, or array length (unknown: grows) */
	uint64_t max;		/* array: highest index initialized + 1 */
	uint32_t *slot;		/* struct/union: value per field */
	struct ilist **sub;	/* struct/union: sub-list per field */
	irun *runs;		/* array: in order, later runs override earlier ones */
	uint32_t nrun, caprun;
	uint32_t ufield;	/* union: the initialized field */
	bool uset;
	uint32_t ubase;	/* union expanded from this value (il_expand), 0: none */
} ilist;

static ilist *il_new(cc *c, qty ty)
{
	ilist *l = halloc(c, sizeof(ilist));

	memset(l, 0, sizeof(*l));
	l->ty = ty;
	if (is_vector(c, ty)) {
		l->array = true;
		l->n = T(c, ty)->n;
	} else if (is_array(c, ty)) {
		l->array = true;
		l->unknown = (T(c, ty)->flags & TF_INCOMPLETE) != 0;
		l->n = l->unknown ? 0 : arr_len(c, ty);
	} else {
		const tag *r = &c->tags[T(c, ty)->aux];

		l->isunion = (r->flags & RF_UNION) != 0;
		l->n = r->nfield;
		l->slot = halloc(c, (r->nfield + 1) * sizeof(uint32_t));
		l->sub = halloc(c, (r->nfield + 1) * sizeof(ilist *));
		memset(l->slot, 0, (r->nfield + 1) * sizeof(uint32_t));
		memset(l->sub, 0, (r->nfield + 1) * sizeof(ilist *));
	}
	return l;
}

static qty il_elem_type(cc *c, const ilist *l, uint64_t i)
{
	if (l->array)
		return T(c, l->ty)->base;
	return c->tags[T(c, l->ty)->aux].f[i].ty;
}

static void il_run(cc *c, ilist *l, uint64_t lo, uint64_t hi, uint32_t node, ilist *sub)
{
	if (l->nrun == l->caprun) {
		uint32_t ncap = l->caprun ? l->caprun * 2 : 8;
		irun *nr = halloc(c, ncap * sizeof(irun));

		if (l->nrun)
			memcpy(nr, l->runs, l->nrun * sizeof(irun));
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
	l->runs[l->nrun++] = (irun){ (uint32_t)lo, (uint32_t)hi, node, sub };
	if (hi + 1 > l->max)
		l->max = hi + 1;
}

static void il_set(cc *c, ilist *l, uint64_t i, uint32_t node, ilist *sub)
{
	if (l->array) {
		il_run(c, l, i, i, node, sub);
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
static void il_expand(cc *c, ilist *s, uint32_t v);

static void il_put(cc *c, ilist *s, uint64_t k, uint32_t m)
{
	qty ft = N(m)->ty;

	if (is_array(c, ft) && N(m)->op != N_STR) {
		ilist *t = il_new(c, QUNQ(ft));

		il_expand(c, t, m);
		il_set(c, s, k, 0, t);
	} else {
		il_set(c, s, k, m, NULL);
	}
}

static void il_expand(cc *c, ilist *s, uint32_t v)
{
	uint32_t loc = N(v)->loc;

	if (s->array && N(v)->op == N_STR) {
		const cstr *str = &c->strs[N(v)->a];
		qty et = T(c, s->ty)->base;

		for (uint32_t k = 0; k < str->n && (s->unknown || k < s->n); k++) {
			i128 x = 0;

			memcpy(&x, str->s + (size_t)k * str->esz, str->esz);
			if (is_signed(c, et))
				x = str->esz == 1 ? (int8_t)x : str->esz == 2 ? (int16_t)x : (int32_t)x;
			il_run(c, s, k, k, mk_int(c, x, et, loc), NULL);
		}
		return;
	}
	if (has_side_effects(c, v)) {
		cc_error(c, loc, "unsupported: a designator overrides part of an initializer with side effects");
		return;
	}
	if (s->array) {
		qty et = T(c, s->ty)->base;

		if (s->unknown || is_vector(c, s->ty)) {
			cc_error(c, loc, "unsupported: a designator overrides part of this initializer");
			return;
		}
		for (uint64_t k = 0; k < s->n; k++) {
			uint32_t e = mk2(c, N_INDEX, et, loc, rvalue(c, v), mk_int(c, (i128)k, prim(TY_LONG), loc));

			N(e)->flags = NF_LVALUE;
			il_put(c, s, k, e);
		}
		return;
	}
	{
		qty bt = N(v)->ty;
		const tag *r = &c->tags[T(c, bt)->aux];

		for (uint32_t k = 0; k < r->nfield; k++) {
			const field *f = &r->f[k];
			uint32_t m;

			if (f->bitfield && !f->name)
				continue;
			m = mk(c, N_MEMBER, qualify(c, f->ty, QQ(bt)), loc);
			N(m)->a = v, N(m)->b = T(c, bt)->aux, N(m)->c = k;
			N(m)->flags = (uint8_t)((N(v)->flags & NF_LVALUE) | (f->bitfield ? NF_BITFIELD : 0));
			il_put(c, s, k, m);
			if (s->isunion)
				break;	/* (its first member; il_child reads another through v) */
		}
		if (s->isunion)
			s->ubase = v;
	}
}

/* the sub-list at i, created (replacing a plain value) if needed */
static ilist *il_child(cc *c, ilist *l, uint64_t i)
{
	qty et = il_elem_type(c, l, i);
	uint32_t old = 0;

	if (l->array) {
		for (uint32_t k = i < l->max ? l->nrun : 0; k-- > 0;)
			if (l->runs[k].lo <= i && i <= l->runs[k].hi) {
				if (l->runs[k].sub && l->runs[k].lo == i && l->runs[k].hi == i)
					return l->runs[k].sub;
				if (!l->runs[k].sub)
					old = l->runs[k].node;
				break;
			}
	} else if (l->sub[i] && (!l->isunion || l->ufield == i)) {
		return l->sub[i];
	} else if (!l->isunion || (l->uset && l->ufield == i)) {
		old = l->slot[i];
	} else if (l->ubase) {	/* the same bytes through the member the designator picks */
		uint32_t v = l->ubase;
		qty bt = N(v)->ty;

		old = mk(c, N_MEMBER, qualify(c, c->tags[T(c, bt)->aux].f[i].ty, QQ(bt)), N(v)->loc);
		N(old)->a = v, N(old)->b = T(c, bt)->aux, N(old)->c = (uint32_t)i;
		N(old)->flags = (uint8_t)(N(v)->flags & NF_LVALUE);
	}
	{
		ilist *s = il_new(c, et);

		if (old && is_ilagg(c, et))
			il_expand(c, s, old);
		il_set(c, l, i, 0, s);
		return s;
	}
}

static bool il_full(const ilist *l, uint64_t pos)
{
	if (l->array)
		return !l->unknown && pos >= l->n;
	if (l->isunion)
		return pos >= 1 || l->uset;
	return pos >= l->n;
}

/* skip unnamed bit-fields (they take no initializer) */
static uint64_t il_next_pos(cc *c, const ilist *l, uint64_t pos)
{
	if (!l->array) {
		const tag *r = &c->tags[T(c, l->ty)->aux];

		while (pos < r->nfield && r->f[pos].bitfield && !r->f[pos].name)
			pos++;
	}
	return pos;
}

static uint32_t il_finish(cc *c, ilist *l, uint32_t loc);
static void il_continue(cc *c, ilist *l, uint64_t pos, bool st);
static void il_fill(cc *c, ilist *l, uint64_t pos, bool braced, uint32_t first, bool st);
static uint32_t init_braced(cc *c, qty ty, bool st);

/* a braced initializer skipped (excess elements): its range */
static pp_span skip_braces(cc *c)
{
	int depth = 0;
	pp_span r = { tok(c)->loc, 0 };

	do {
		r.e = tok(c)->loc;
		if (is(c, T_LBRACE))
			depth++;
		else if (is(c, T_RBRACE))
			depth--;
		else if (is(c, T_EOF))
			fatal(c, tok(c)->loc, "expected '}'");
		next(c);
	} while (depth > 0);
	r.e = pp_tok_end(c->p, r.e);
	return r;
}

/* a string literal for a char array of known size: longer than it (the terminator may go) */
static void str_fit(cc *c, qty arr, uint32_t s)
{
	if (is_array(c, arr) && !(T(c, arr)->flags & (TF_INCOMPLETE | TF_VLA)) && N(s)->op == N_STR &&
	    c->strs[N(s)->a].esz == 1 && c->strs[N(s)->a].n - 1 > T(c, arr)->n) {
		pp_span r = rng(c, s);

		cc_warnx(c, "excess-initializers", true, NULL, r.b, &r, 1, NULL, 0, "initializer-string for char array is too long");
	}
}

/* clang's -Wexcess-initializers, once per list: at the first excess element */
static void excess_warn(cc *c, qty ty, bool *done, pp_span r)
{
	const char *k = is_array(c, ty) ? "array" : is_vector(c, ty) ? "vector" : KIND(c, ty) == TY_UNION ? "union" :
			is_record(c, ty) ? "struct" : "scalar";

	if (*done)
		return;
	*done = true;
	cc_warnx(c, "excess-initializers", true, NULL, r.b, &r, 1, NULL, 0, "excess elements in %s initializer", k);
}

static bool is_char_array(cc *c, qty t)
{
	if (!is_array(c, t))
		return false;
	t = T(c, t)->base;
	return is_integer(c, t) && (ty_size(c, t) == 1 || ty_size(c, t) == 2 || ty_size(c, t) == 4) && KIND(c, t) != TY_BOOL;
}

static bool string_initializes(cc *c, qty arr, uint32_t v)
{
	uint32_t s = v;

	while (N(s)->flags & NF_PAREN && N(s)->op != N_STR)
		break;
	return N(s)->op == N_STR && is_char_array(c, arr) &&
	       c->strs[N(s)->a].esz == ty_size(c, T(c, arr)->base);
}

/*
 * at '{': { "str" } follows, not { "q"[0] } (an element of it).  The token
 * queue holds 4: concatenated strings ("a" "b") are taken as the string.
 */
static bool brace_string(cc *c)
{
	int k;

	if (!is_strtok(peek(c, 1)->kind))
		return false;
	k = peek(c, 2)->kind;
	if (is_strtok(k) || k == T_RBRACE)
		return true;
	return k == T_COMMA && peek(c, 3)->kind == T_RBRACE;
}

/* one element at pos: braces, a value, or brace elision into a sub-aggregate */
static void il_elem(cc *c, ilist *l, uint64_t pos, uint32_t v, bool st)
{
	qty et = il_elem_type(c, l, pos);

	if (!v && is(c, T_LBRACE)) {
		if (is_char_array(c, et) && brace_string(c)) {	/* { "string" } */
			uint32_t sv;

			next(c);
			sv = string_lit(c);
			accept(c, T_COMMA);
			expect(c, T_RBRACE, "'}'");
			str_fit(c, et, sv);
			il_set(c, l, pos, sv, NULL);
			return;
		}
		if (is_ilagg(c, et)) {
			ilist *s = il_new(c, et);

			next(c);
			il_fill(c, s, 0, true, 0, st);
			expect(c, T_RBRACE, "'}'");
			il_set(c, l, pos, 0, s);
		} else {
			il_set(c, l, pos, init_braced(c, et, st), NULL);
		}
		return;
	}
	if (!v)
		v = assign_expr(c);
	if ((is_ilagg(c, et)) && !string_initializes(c, et, v) &&
	    !((is_record(c, et) || is_vector(c, et)) && compatible(c, N(v)->ty, et))) {
		ilist *s = il_new(c, et);

		il_fill(c, s, 0, false, v, st);
		il_set(c, l, pos, 0, s);
		return;
	}
	if (is_array(c, et)) {
		str_fit(c, et, v);
		il_set(c, l, pos, v, NULL);	/* string literal */
		return;
	}
	if (!l->array && is_record(c, l->ty) && c->tags[T(c, l->ty)->aux].f[pos].bitfield)
		bitfield_const(c, &c->tags[T(c, l->ty)->aux].f[pos], decay(c, v), 0);
	il_set(c, l, pos, is_record(c, et) && !is_complex(c, et) ? v : convert(c, decay(c, v), et), NULL);
}

/* a designation starts here: ".f", "[i]" or the old GNU "f:" */
static bool at_designator(cc *c, int k)
{
	int t = peek(c, k)->kind;

	return t == T_PERIOD || t == T_LSQUARE || (t == T_IDENT && peek(c, k + 1)->kind == T_COLON);
}

/* after an element of an unbraced (elided) list: go on with the next one if it belongs here */
static void il_continue(cc *c, ilist *l, uint64_t pos, bool st)
{
	pos = il_next_pos(c, l, pos);
	if (il_full(l, pos) || !is(c, T_COMMA))
		return;
	{
		if (peek(c, 1)->kind == T_RBRACE || at_designator(c, 1))
			return;
	}
	next(c);
	il_fill(c, l, pos, false, 0, st);
}

/* a designation; returns the position after the top-level designated element */
static uint64_t il_designation(cc *c, ilist *l, bool st)
{
	uint64_t lo, hi;
	uint32_t path[FM_DEPTH];
	int depth = 1;

	if (accept(c, T_PERIOD) || (is(c, T_IDENT) && peek(c, 1)->kind == T_COLON)) {
		uint32_t name;
		int d;

		if (!is(c, T_IDENT))
			fatal(c, tok(c)->loc, "expected a field designator");
		name = tok(c)->id;
		if (l->array) {
			cc_error(c, tok(c)->loc, "field designator cannot initialize a non-struct, non-union type");
			next(c);
			return 0;
		}
		d = find_member(c, T(c, l->ty)->aux, name, path, 0);
		if (d < 0) {
			cc_error(c, tok(c)->loc, "field designator '%.*s' does not refer to any field", IDF(name));
			next(c);
			skip_balanced(c);
			return l->n;
		}
		next(c);
		depth = d;
		lo = hi = path[0];
	} else {
		i128 a, b;

		expect(c, T_LSQUARE, "'['");
		const_expr(c, &a, NULL);
		b = a;
		if (accept(c, T_ELLIPSIS))
			const_expr(c, &b, NULL);
		expect(c, T_RSQUARE, "']'");
		if (!l->array) {
			cc_error(c, tok(c)->loc, "array designator cannot initialize non-array type");
			return 0;
		}
		if (a < 0 || b < a || (!l->unknown && (uint64_t)b >= l->n)) {
			cc_error(c, tok(c)->loc, "array designator index exceeds array bounds");
			return (uint64_t)(b < 0 ? 0 : b) + 1;
		}
		lo = (uint64_t)a, hi = (uint64_t)b;
	}
	/* path through anonymous members acts as nested designators */
	if (depth > 1) {
		ilist *s = l;

		for (int i = 0; i < depth - 1; i++)
			s = il_child(c, s, path[i]);
		{
			uint64_t last = path[depth - 1];

			if (is(c, T_PERIOD) || is(c, T_LSQUARE)) {
				ilist *t = il_child(c, s, last);
				uint64_t np = il_designation(c, t, st);

				il_continue(c, t, np, st);
			} else {
				if (!accept(c, T_EQUAL))
					accept(c, T_COLON);
				il_elem(c, s, last, 0, st);
				il_continue(c, s, last + 1, st);
			}
		}
		return lo + 1;
	}
	if (is(c, T_PERIOD) || is(c, T_LSQUARE)) {
		ilist *t = il_child(c, l, lo);
		uint64_t np = il_designation(c, t, st);

		il_continue(c, t, np, st);
		for (uint64_t i = lo + 1; i <= hi; i++)
			il_set(c, l, i, 0, t);	/* range of the same sub-object */
		return hi + 1;
	}
	if (!accept(c, T_EQUAL))
		accept(c, T_COLON);	/* old GNU "field: value" */
	if (lo == hi) {
		il_elem(c, l, lo, 0, st);
	} else {
		/* [lo ... hi] = v: one value for the whole range */
		qty et = il_elem_type(c, l, lo);
		uint32_t v;

		if (is(c, T_LBRACE)) {
			if (is_ilagg(c, et)) {
				ilist *s = il_new(c, et);

				next(c);
				il_fill(c, s, 0, true, 0, st);
				expect(c, T_RBRACE, "'}'");
				il_run(c, l, lo, hi, 0, s);
			} else {
				il_run(c, l, lo, hi, init_braced(c, et, st), NULL);
			}
		} else {
			v = assign_expr(c);
			if ((is_ilagg(c, et)) && !string_initializes(c, et, v) &&
			    !((is_record(c, et) || is_vector(c, et)) && compatible(c, N(v)->ty, et))) {
				ilist *s = il_new(c, et);

				il_fill(c, s, 0, false, v, st);
				il_run(c, l, lo, hi, 0, s);
			} else {
				il_run(c, l, lo, hi, is_array(c, et) || is_record(c, et) || is_vector(c, et) ? v : convert(c, decay(c, v), et), NULL);
			}
		}
	}
	return hi + 1;
}

static void il_fill(cc *c, ilist *l, uint64_t pos, bool braced, uint32_t first, bool st)
{
	bool excess = false;

	pos = il_next_pos(c, l, pos);
	if (first) {
		if (il_full(l, pos)) {
			cc_error(c, N(first)->loc, "excess elements in initializer");
			return;
		}
		il_elem(c, l, pos, first, st);
		pos = il_next_pos(c, l, pos + 1);
		if (!braced) {
			if (il_full(l, pos) || !is(c, T_COMMA) || peek(c, 1)->kind == T_RBRACE || at_designator(c, 1))
				return;
			next(c);
		} else if (!accept(c, T_COMMA)) {
			return;
		}
	}
	for (;;) {
		if (is(c, T_RBRACE))
			return;
		if (at_designator(c, 0)) {
			if (!braced)
				return;
			pos = il_designation(c, l, st);
		} else {
			if (!braced && il_full(l, pos))
				return;
			if (il_full(l, pos)) {
				/* excess elements: clang warns and drops them */
				if (is(c, T_LBRACE)) {
					excess_warn(c, l->ty, &excess, skip_braces(c));
				} else {
					uint32_t v = assign_expr(c);

					excess_warn(c, l->ty, &excess, rng(c, v));
				}
			} else {
				il_elem(c, l, pos, 0, st);
				pos++;
			}
		}
		pos = il_next_pos(c, l, pos);
		if (!braced) {
			if (il_full(l, pos) || !is(c, T_COMMA) || peek(c, 1)->kind == T_RBRACE || at_designator(c, 1))
				return;
			next(c);
			continue;
		}
		if (!accept(c, T_COMMA))
			return;
	}
}

/* N_INIT: struct/union: aux 0, list of nfield values; array: aux 1, list of (lo, hi, value) */
static uint32_t il_finish(cc *c, ilist *l, uint32_t loc)
{
	uint32_t n;
	qty ty = l->ty;
	vec v;

	vinit(&v);
	if (l->array && l->unknown)
		ty = ty_array(c, T(c, ty)->base, l->max, 0);
	n = mk(c, N_INIT, ty, loc);
	if (l->array) {
		for (uint32_t i = 0; i < l->nrun; i++) {
			irun *r = &l->runs[i];
			uint32_t x = r->sub ? il_finish(c, r->sub, loc) : r->node;

			vpush(c, &v, r->lo);
			vpush(c, &v, r->hi);
			vpush(c, &v, x);
		}
		N(n)->aux = 1;
		N(n)->c = v.n / 3;
	} else {
		for (uint64_t i = 0; i < l->n; i++)
			vpush(c, &v, l->sub[i] ? il_finish(c, l->sub[i], loc) : l->slot[i]);
		N(n)->aux = l->isunion ? 2 : 0;
		N(n)->c = v.n;
		N(n)->a = l->ufield;
	}
	N(n)->b = vfreeze(c, &v);
	return n;
}

/* { ... } for a scalar: the first element */
static uint32_t init_braced(cc *c, qty ty, bool st)
{
	uint32_t v = 0, loc = tok(c)->loc;

	expect(c, T_LBRACE, "'{'");
	if (is(c, T_RBRACE)) {
		next(c);
		return mk(c, N_ZERO, QUNQ(ty), loc);
	}
	if (is(c, T_LBRACE))
		v = init_braced(c, ty, st);
	else
		v = convert_as(c, decay(c, assign_expr(c)), ty, c->cv_what == CX_INIT ? CX_INIT : 0, c->cv_loc);
	{
		bool excess = false;

		while (accept(c, T_COMMA)) {
			if (is(c, T_RBRACE))
				break;
			if (is(c, T_LBRACE))
				excess_warn(c, ty, &excess, skip_braces(c));
			else
				excess_warn(c, ty, &excess, rng(c, assign_expr(c)));
		}
	}
	expect(c, T_RBRACE, "'}'");
	return v;
}

VPRIV uint32_t initializer(cc *c, qty *ty, bool st)
{
	uint32_t loc = tok(c)->loc;
	qty t = *ty;

	if (is(c, T_LBRACE)) {
		if (is_char_array(c, t) && brace_string(c)) {
			uint32_t s;

			next(c);
			s = string_lit(c);
			accept(c, T_COMMA);
			expect(c, T_RBRACE, "'}'");
			str_fit(c, t, s);
			if (T(c, t)->flags & TF_INCOMPLETE)
				*ty = QT(QI(ty_array(c, T(c, t)->base, c->strs[N(s)->a].n, 0)), QQ(t));
			return s;
		}
		if (is_ilagg(c, t)) {
			ilist *l;
			uint32_t n;

			if (is_record(c, t) && !complete(c, t)) {
				cc_error(c, loc, "variable has incomplete type");
				skip_balanced(c);
				return mk(c, N_ZERO, t, loc);
			}
			l = il_new(c, QUNQ(t));
			next(c);
			il_fill(c, l, 0, true, 0, st);
			expect(c, T_RBRACE, "'}'");
			n = il_finish(c, l, loc);
			*ty = QT(QI(N(n)->ty), QQ(t));
			return n;
		}
		return init_braced(c, t, st);
	}
	{
		uint32_t v = assign_expr(c);

		if (is_array(c, t)) {
			if (!string_initializes(c, t, v)) {
				cc_error(c, c->cv_what == CX_INIT ? c->cv_loc : loc, "array initializer must be an initializer list");
				return v;
			}
			if (T(c, t)->flags & TF_INCOMPLETE)
				*ty = QT(QI(ty_array(c, T(c, t)->base, c->strs[N(v)->a].n, 0)), QQ(t));
			else
				str_fit(c, t, v);
			return v;
		}
		if (is_record(c, t) && !is_complex(c, t)) {
			if (!compatible(c, N(v)->ty, t))
				cc_error(c, loc, "initializing with an expression of incompatible type");
			return v;
		}
		return convert_as(c, decay(c, v), t, c->cv_what == CX_INIT ? CX_INIT : 0, c->cv_loc);
	}
}

/* clang's location for a diagnostic about expression n (getExprLoc: a call's callee) */
VPRIV uint32_t expr_loc(cc *c, uint32_t n)
{
	while (N(n)->op == N_CAST && N(n)->a && !c->span[n].e)
		n = N(n)->a;
	if (N(n)->op == N_CALL && N(n)->a)
		return rng(c, N(n)->a).b;
	return N(n)->loc;
}

/*
 * Whether init can initialise an object of static storage of type ty: the
 * walk of the emitter's data_init (emit/data.c) without writing, so the error
 * comes from the front end, also for an object never emitted (clang Sema).
 * *bad: the first element that is not a constant.
 */
VPRIV bool static_init_ok(cc *c, qty ty, uint32_t init, uint32_t *bad)
{
	const node *x;
	cval cv;

	if (!init)
		return true;
	x = N(init);
	if (x->op == N_ZERO || ((x->flags & NF_ERR) && !c->sie))
		return true;
	if (x->op == N_INIT) {
		const uint32_t *v = &c->lists[x->b];

		if (x->aux == 1) {
			for (uint32_t i = 0; i < x->c; i++)
				if (!static_init_ok(c, T(c, ty)->base, v[3 * i + 2], bad))
					return false;
			return true;
		}
		const tag *r = &c->tags[T(c, ty)->aux];

		for (uint32_t i = 0; i < x->c && i < r->nfield; i++) {
			const field *f = &r->f[i];

			if ((x->aux == 2 && i != x->a) || !v[i])
				continue;
			if (f->bitfield) {
				if (!eval(c, v[i], &cv) || cv.kind != CV_INT) {
					*bad = v[i];
					return false;
				}
				continue;
			}
			if (!static_init_ok(c, f->ty, v[i], bad))
				return false;
		}
		return true;
	}
	if (x->op == N_STR && is_array(c, ty))
		return true;
	if (x->op == N_COMPLIT)
		return static_init_ok(c, ty, x->a, bad);
	if (x->op == N_CPLX) {
		qty et = c->tags[T(c, x->ty)->aux].f[0].ty;

		return static_init_ok(c, et, x->a, bad) && static_init_ok(c, et, x->b, bad);
	}
	if (x->op == N_CAST && x->aux == CK_NOP && (is_record(c, ty) || is_array(c, ty) || is_vector(c, ty)))
		return static_init_ok(c, ty, x->a, bad);
	if ((is_record(c, ty) || is_array(c, ty) || is_vector(c, ty))) {
		uint32_t m = init;

		while (N(m)->op == N_CAST)
			m = N(m)->a;
		if (N(m)->op == N_VAR && c->syms[N(m)->a].body)
			return true;
		*bad = init;
		return false;
	}
	if (!eval(c, init, &cv) || (cv.kind != CV_INT && cv.kind != CV_FLT && cv.kind != CV_ADDR)) {
		*bad = init;
		return false;
	}
	return true;
}
