/* vcc front end: warnings of the expression parsers: division and shift, tautological compares, integer overflow, self-assignment */
#include "cc-internal.h"

/* -Wdivision-by-zero, -Wshift-count-*: l op r with a constant r */
CC_PRIVATE void cc_expression_runtime_warn(cc *c, int op, uint32_t l, uint32_t r, uint32_t loc)
{
	cval cv;
	diag_span rr;

	if (c->dead || (op != LEX_TOKEN_SLASH && op != LEX_TOKEN_PERCENT && op != LEX_TOKEN_LESS_LESS && op != LEX_TOKEN_GREATER_GREATER) ||
	    !cc_type_is_integer(c, AST_NODE(l)->ty) || !cc_type_is_integer(c, AST_NODE(r)->ty) || !cc_eval(c, r, &cv) || cv.kind != CV_INT)
		return;
	if (op == LEX_TOKEN_SLASH || op == LEX_TOKEN_PERCENT ? cv.i != 0 : cv.i >= 0 && cv.i < (i128)cc_type_size(c, cc_node_promoted_type(c, l)) * 8)
		return;	/* no warning: no range (pp_token_end_find) either */
	rr = cc_node_range(c, r);
	if (op == LEX_TOKEN_SLASH || op == LEX_TOKEN_PERCENT) {
		if (!cv.i && c->unevaluated)
			;	/* (sizeof, typeof: never runs) */
		else if (!cv.i && c->defer_rt) {
			if (c->ndefer < 8)
				c->defer[c->ndefer].loc = loc, c->defer[c->ndefer].r = rr, c->defer[c->ndefer++].rem = op == LEX_TOKEN_PERCENT;
		} else if (!cv.i && c->fn)	/* (clang's DiagRuntimeBehavior: at the end of the function) */
			cc_report_runtime_defer(c, "division-by-zero", NULL, loc, &rr, 1, NULL, 0, "%s by zero is undefined",
				 op == LEX_TOKEN_SLASH ? "division" : "remainder");
		else if (!cv.i)
			cc_report_warning(c, "division-by-zero", true, NULL, loc, &rr, 1, NULL, 0, "%s by zero is undefined",
			      op == LEX_TOKEN_SLASH ? "division" : "remainder");
		return;
	}
	{
		qty pt = cc_node_promoted_type(c, l);
		uint64_t w = cc_type_size(c, pt) * 8;
		diag_record *d = cv.i < 0 ? cc_report_warning_begin(c, "shift-count-negative", true, NULL, loc, "shift count is negative")
			   : cc_report_warning_begin(c, "shift-count-overflow", true, NULL, loc, "shift count >= width of type");

		if (!d)
			return;
		diag_record_label_add(c->p, d, rr.b, rr.e, NULL);
		if (cv.i < 0)
			diag_record_help_set(c->p, d, "a shift by a negative count is undefined");
		else
			diag_record_help_set(c->p, d, "'%s' is %llu bits wide: a shift by %lld or more is undefined%s", tyname(c, pt),
				     (unsigned long long)w, (long long)w,
				     w < 64 && cv.i < 64 && op == LEX_TOKEN_LESS_LESS ? "; shift a 64-bit value (1ULL << n)" : "");
		cc_report_runtime(c, d);
	}
}

/* clang's -Wconstant-logical-operand: 'x && 4' where '&' was likely meant (the right
   operand a constant other than 0 and 1, an integer non-bool left one, no enumerator) */
static __attribute__((cold, noinline)) void cc_expression_logical_constant_warn(cc *c, int op, uint32_t l, uint32_t r, uint32_t loc, i128 v)
{
	const char *o = op == LEX_TOKEN_AMPERSAND_AMPERSAND ? "&&" : "||", *bo = op == LEX_TOKEN_AMPERSAND_AMPERSAND ? "&" : "|";
	diag_span lr = cc_node_range(c, l), rr = cc_node_range(c, r);
	diag_record *d = cc_report_warning_begin(c, "constant-logical-operand", true, NULL, loc, "use of logical '%s' with constant operand", o), *n;

	(void)v;
	if (!d)
		return;
	diag_record_label_add(c->p, d, rr.b, rr.e, NULL);
	n = diag_record_note_add(c->p, d, loc, "use '%s' for a bitwise operation", bo);
	if (n)
		diag_record_fixit_add(c->p, n, loc, loc + 2, bo, DIAG_APPLICABILITY_MAYBE);
	if (op == LEX_TOKEN_AMPERSAND_AMPERSAND) {
		n = diag_record_note_add(c->p, d, loc, "remove constant to silence this warning");
		if (n)
			diag_record_fixit_add(c->p, n, lr.e, rr.e, "", DIAG_APPLICABILITY_MAYBE);
	}
	diag_record_emit(c->p, d);
}

/* clang's isSameComparisonOperand: the same variable, member chain or subscript once
   parentheses and implicit conversions are gone (an explicit cast is another operand) */
CC_PRIVATE uint32_t cc_node_without_implicit(cc *c, uint32_t n)
{
	while (AST_NODE(n)->op == N_CAST && !c->span[n].e)
		n = AST_NODE(n)->a;
	return n;
}

/* the same object on both sides (clang's diagnoseTautologicalComparison:
   the same variable, member chain or constant-index element; implicit conversions and parentheses
   aside; nothing from a macro) */
CC_PRIVATE bool cc_expression_is_same_reference(cc *c, uint32_t a, uint32_t b, int depth)
{
	while (AST_NODE(a)->op == N_CAST && !c->span[a].e && AST_NODE(a)->a)
		a = AST_NODE(a)->a;
	while (AST_NODE(b)->op == N_CAST && !c->span[b].e && AST_NODE(b)->a)
		b = AST_NODE(b)->a;
	if (AST_NODE(a)->op != AST_NODE(b)->op || depth > 16)
		return false;
	switch (AST_NODE(a)->op) {
	case N_VAR: {
		uint32_t nm = c->syms[AST_NODE(a)->a].name;

		return AST_NODE(a)->a == AST_NODE(b)->a && c->syms[AST_NODE(a)->a].kind == S_VAR && nm &&
		       pp_location_spells(c->p, AST_NODE(a)->loc, cc_identifier_string(nm), (uint32_t)cc_identifier_length(nm)) &&
		       pp_location_spells(c->p, AST_NODE(b)->loc, cc_identifier_string(nm), (uint32_t)cc_identifier_length(nm));
	}
	case N_MEMBER: {
		uint32_t x = AST_NODE(a)->a, y = AST_NODE(b)->a;

		if (AST_NODE(a)->b != AST_NODE(b)->b || AST_NODE(a)->c != AST_NODE(b)->c)
			return false;
		if (AST_NODE(x)->op == N_DEREF && AST_NODE(y)->op == N_DEREF)	/* p->m */
			x = AST_NODE(x)->a, y = AST_NODE(y)->a;
		return cc_expression_is_same_reference(c, x, y, depth + 1);
	}
	case N_INDEX: {
		uint32_t i = AST_NODE(a)->b, j = AST_NODE(b)->b;

		while (AST_NODE(i)->op == N_CAST && !c->span[i].e && AST_NODE(i)->a)
			i = AST_NODE(i)->a;
		while (AST_NODE(j)->op == N_CAST && !c->span[j].e && AST_NODE(j)->a)
			j = AST_NODE(j)->a;
		return AST_NODE(i)->op == N_NUM && AST_NODE(j)->op == N_NUM && c->ipool[AST_NODE(i)->a] == c->ipool[AST_NODE(j)->a] &&
		       cc_expression_is_same_reference(c, AST_NODE(a)->a, AST_NODE(b)->a, depth + 1);
	}
	default:
		return false;
	}
}

/* clang's -Wtautological-compare for x == x and friends (not floating: NaN) */
CC_PRIVATE __attribute__((cold, noinline)) void cc_expression_self_compare_warn(cc *c, int op, uint32_t l, uint32_t r, uint32_t loc)
{
	bool t = op == LEX_TOKEN_EQUAL_EQUAL || op == LEX_TOKEN_LESS_EQUAL || op == LEX_TOKEN_GREATER_EQUAL;
	diag_record *d;

	(void)l, (void)r;
	if (c->dead && c->fn)
		return;
	d = cc_report_warning_begin(c, "tautological-compare", true, NULL, loc, "self-comparison always evaluates to %s", t ? "true" : "false");
	if (!d)
		return;
	diag_record_help_set(c->p, d, "both sides are the same %s: was another one meant?",
		     AST_NODE(cc_node_without_implicit(c, l))->op == N_VAR ? "variable" : "object");
	diag_record_emit(c->p, d);
}

/* an operand as clang prints it in these messages: v, s.f, p->f (appended at b[*k]) */
static void cc_expression_operand_name(cc *c, uint32_t n, char *b, size_t sz, size_t *k)
{
	const node *x = AST_NODE(n = cc_node_without_implicit(c, n));
	int l;
	const char *nm;

#define CC_EXPRESSION_OPERAND_NAME_APPEND(p, len) do { size_t q_ = (size_t)(len); if (*k + q_ < sz) memcpy(b + *k, (p), q_), *k += q_; } while (0)
	if (x->op == N_VAR) {
		nm = cc_identifier_name(c->syms[x->a].name, &l);
		CC_EXPRESSION_OPERAND_NAME_APPEND(nm, l);
	} else if (x->op == N_MEMBER) {
		const field *f = &c->tags[x->b].f[x->c];
		uint32_t base = cc_node_without_implicit(c, x->a);

		if (AST_NODE(base)->op == N_DEREF) {
			cc_expression_operand_name(c, AST_NODE(base)->a, b, sz, k);
			CC_EXPRESSION_OPERAND_NAME_APPEND("->", 2);
		} else {
			cc_expression_operand_name(c, base, b, sz, k);
			CC_EXPRESSION_OPERAND_NAME_APPEND(".", 1);
		}
		if (f->name) {
			nm = cc_identifier_name(f->name, &l);
			CC_EXPRESSION_OPERAND_NAME_APPEND(nm, l);
		}
	}
#undef CC_EXPRESSION_OPERAND_NAME_APPEND
	b[*k < sz ? *k : sz - 1] = 0;
}

/* clang's -Wtautological-pointer-compare: an array, a function or &object against null */
CC_PRIVATE __attribute__((cold, noinline)) void cc_expression_null_compare_warn(cc *c, int op, uint32_t x, uint32_t nul)
{
	uint32_t n = cc_node_without_implicit(c, x), at;
	const node *y = AST_NODE(n);
	const char *what;
	bool fn = false, eq = op == LEX_TOKEN_EQUAL_EQUAL;
	char nm[160];
	size_t nl = 0;
	diag_span xr, nr = cc_node_range(c, nul);
	diag_record *d;

	if (y->op == N_ADDR) {
		uint32_t in = cc_node_without_implicit(c, y->a);

		if ((AST_NODE(in)->op != N_VAR && AST_NODE(in)->op != N_MEMBER) || cc_type_is_function(c, AST_NODE(in)->ty) ||
		    (AST_NODE(in)->op == N_VAR && (c->syms[AST_NODE(in)->a].flags & SF_WEAK)))
			return;
		what = "address of ", n = in;
	} else if (y->op == N_VAR && cc_type_is_function(c, y->ty)) {
		if (c->syms[y->a].flags & SF_WEAK)
			return;
		what = "function ", fn = true;
	} else if ((y->op == N_VAR || y->op == N_MEMBER) && cc_type_is_array(c, y->ty)) {
		if (y->op == N_VAR && (c->syms[y->a].flags & SF_WEAK))
			return;
		what = "array ";
	} else {
		return;
	}
	cc_expression_operand_name(c, n, nm, sizeof nm, &nl);
	if (!nl)
		return;
	xr = cc_node_range(c, n), at = AST_NODE(n)->loc;
	if (AST_NODE(n)->op == N_MEMBER)	/* clang: at the member's name, after '.' or '->' */
		at += AST_NODE(cc_node_without_implicit(c, AST_NODE(n)->a))->op == N_DEREF ? 2 : 1;
	d = cc_report_warning_begin(c, "tautological-pointer-compare", true, NULL, at,
			  "comparison of %s'%s' %s to a null pointer is always %s", what, nm, eq ? "equal" : "not equal",
			  eq ? "false" : "true");
	if (d) {
		diag_record_label_add(c->p, d, xr.b, xr.e, NULL);
		diag_record_label_add(c->p, d, nr.b, nr.e, NULL);
		if (fn) {
			const ctype *ft = AST_TYPE(c, y->ty);
			diag_record *k = diag_record_note_add(c->p, d, xr.b, "prefix with the address-of operator to silence this warning");

			if (k)
				diag_record_fixit_add(c->p, k, xr.b, xr.b, "&", DIAG_APPLICABILITY_MAYBE);
			if (!ft->n && !(ft->flags & TF_VARIADIC) && !cc_type_is_void(c, ft->base)) {
				k = diag_record_note_add(c->p, d, xr.b, "suffix with parentheses to turn this into a function call");
				if (k)
					diag_record_fixit_add(c->p, k, xr.e, xr.e, "()", DIAG_APPLICABILITY_MAYBE);
			}
		} else {
			diag_record_help_set(c->p, d, "%s'%s' is never null", what, nm);
		}
		diag_record_emit(c->p, d);
	}
}


/* clang's -Wtautological-constant-out-of-range-compare: a constant the other operand's own type
   (implicit conversions stripped) cannot hold, compared in the comparison's type */
CC_PRIVATE __attribute__((cold, noinline)) void cc_expression_range_compare_warn(cc *c, uint32_t n, int nk, uint32_t loc)
{
	const node *x = AST_NODE(n);
	int op = x->aux;
	uint32_t k = nk ? x->b : x->a, o = nk ? x->a : x->b, os = cc_node_without_implicit(c, o);
	qty ot = AST_NODE(os)->ty;
	cval v, ov;
	i128 lo, hi;
	uint64_t w;
	bool above, res;
	char vs[48];
	diag_span orr = cc_node_range(c, os), kr = cc_node_range(c, cc_node_without_implicit(c, k));
	diag_record *d;

	if (!cc_type_is_integer(c, ot) || (cc_eval(c, os, &ov) && ov.kind == CV_INT) || !cc_eval(c, k, &v) || v.kind != CV_INT)
		return;
	w = cc_type_size(c, ot) * 8;
	if (cc_type_is_signed(c, ot) && !cc_type_is_signed(c, AST_NODE(k)->ty) && AST_TYPE_KIND(c, ot) != TY_BOOL)
		return;	/* a signed operand in an unsigned comparison: not one interval (clang) */
	if (AST_TYPE_KIND(c, ot) == TY_BOOL)
		lo = 0, hi = 1;
	else if (w >= 64 || w >= cc_type_size(c, AST_NODE(k)->ty) * 8)
		return;	/* as wide as the comparison: every converted constant fits */
	else if (cc_type_is_signed(c, ot))
		lo = -((i128)1 << (w - 1)), hi = ((i128)1 << (w - 1)) - 1;
	else
		lo = 0, hi = ((i128)1 << w) - 1;
	if (v.i >= lo && v.i <= hi)
		return;
	above = v.i > hi;
	if (!nk)	/* constant on the left: K op X is X op' K */
		op = op == LEX_TOKEN_LESS ? LEX_TOKEN_GREATER : op == LEX_TOKEN_GREATER ? LEX_TOKEN_LESS : op == LEX_TOKEN_LESS_EQUAL ? LEX_TOKEN_GREATER_EQUAL : op == LEX_TOKEN_GREATER_EQUAL ? LEX_TOKEN_LESS_EQUAL : op;
	switch (op) {
	case LEX_TOKEN_EQUAL_EQUAL: res = false; break;
	case LEX_TOKEN_EXCLAMATION_EQUAL: res = true; break;
	case LEX_TOKEN_LESS: case LEX_TOKEN_LESS_EQUAL: res = above; break;
	default: res = !above; break;
	}
	cc_format_i128(vs, sizeof vs, v.i);
	d = cc_report_warning_begin(c, "tautological-constant-out-of-range-compare", true, NULL, loc,
			  "result of comparison of constant %s with expression of type '%s' is always %s", vs, tyname(c, ot),
			  res ? "true" : "false");
	if (!d)
		return;
	if (nk)
		diag_record_label_add(c->p, d, orr.b, orr.e, NULL), diag_record_label_add(c->p, d, kr.b, kr.e, NULL);
	else
		diag_record_label_add(c->p, d, kr.b, kr.e, NULL), diag_record_label_add(c->p, d, orr.b, orr.e, NULL);
	{
		char ls[48], hs[48];

		cc_format_i128(ls, sizeof ls, lo), cc_format_i128(hs, sizeof hs, hi);
		diag_record_help_set(c->p, d, "'%s' holds %s to %s%s", tyname(c, ot), ls, hs, cc_conversion_char_signedness(c, ot));
	}
	cc_report_runtime(c, d);
}


CC_PRIVATE void cc_expression_logical_constant_check(cc *c, int op, uint32_t l, uint32_t r, uint32_t loc)
{
	cval v;

	if (!cc_type_is_integer(c, AST_NODE(r)->ty) || !cc_type_is_integer(c, AST_NODE(l)->ty) || AST_TYPE_KIND(c, AST_NODE(l)->ty) == TY_BOOL ||
	    !cc_eval(c, r, &v) || v.kind != CV_INT || v.i == 0 || v.i == 1)
		return;
	if (AST_NODE(r)->op == N_NUM && (AST_NODE(r)->aux & NUM_ENUM))
		return;
	if (AST_NODE(l)->op == N_NUM && (AST_NODE(l)->aux & NUM_ENUM)) {
		cval lv;

		if (cc_eval(c, l, &lv) && lv.kind == CV_INT && lv.i != 0 && lv.i != 1)
			return;
	}
	cc_expression_logical_constant_warn(c, op, l, r, loc, v.i);
}


/* -Winteger-overflow: n, a signed integer +, -, * or unary - of constants,
   whose exact value does not fit its type.  Once per expression (NF_OVF). */
CC_PRIVATE void cc_expression_overflow_warn(cc *c, int op, uint32_t n, uint32_t loc, uint32_t sub0, uint32_t sub1)
{
	cval a, b;
	i128 v;
	int bits;
	qty t = AST_NODE(n)->ty;

	if ((AST_NODE(sub0)->flags & NF_OVF) || (sub1 && (AST_NODE(sub1)->flags & NF_OVF))) {
		AST_NODE(n)->flags |= NF_OVF;
		return;
	}
	if (c->dead || c->unevaluated || !cc_type_is_integer(c, t) || !cc_type_is_signed(c, t) || (bits = (int)cc_type_size(c, t) * 8) > 64 ||
	    !cc_eval(c, sub0, &a) || a.kind != CV_INT || (sub1 && (!cc_eval(c, sub1, &b) || b.kind != CV_INT)))
		return;
	if (!sub1)
		v = -a.i;
	else if (op == LEX_TOKEN_PLUS)
		v = a.i + b.i;
	else if (op == LEX_TOKEN_MINUS)
		v = a.i - b.i;
	else
		v = a.i * b.i;
	if (v >= -((i128)1 << (bits - 1)) && v < ((i128)1 << (bits - 1)))
		return;
	AST_NODE(n)->flags |= NF_OVF;
	{
		char d[48], o[64];
		int64_t w = bits == 64 ? (int64_t)(uint64_t)v : (int64_t)(int32_t)(uint32_t)v;
		uint64_t m = w < 0 ? -(uint64_t)w : (uint64_t)w;
		int nd = 0, k = 0;
		diag_span r = cc_node_range(c, n);

		do
			d[nd++] = (char)('0' + m % 10), m /= 10;
		while (m);
		if (w < 0)
			o[k++] = '-';
		while (nd) {
			o[k++] = d[--nd];
			if (nd && nd % 3 == 0)
				o[k++] = '\'';
		}
		o[k] = 0;
		cc_report_warning(c, "integer-overflow", true, NULL, loc, &r, 1, NULL, 0,
			 "overflow in expression; result is %s with type '%s'", o, tyname(c, t));
	}
}

/* -Wself-assign: x = x (parentheses and implicit conversions aside), not volatile */
CC_PRIVATE void cc_expression_self_assign_warn(cc *c, uint32_t l, uint32_t r, uint32_t loc)
{
	while (AST_NODE(r)->op == N_CAST && !c->span[r].e && AST_NODE(r)->a)
		r = AST_NODE(r)->a;
	if (AST_NODE(l)->op != N_VAR || AST_NODE(r)->op != N_VAR || AST_NODE(l)->a != AST_NODE(r)->a || (QQ(AST_NODE(l)->ty) & Q_VOLATILE) ||
	    c->syms[AST_NODE(l)->a].kind != S_VAR)
		return;
	{
		uint32_t nm = c->syms[AST_NODE(l)->a].name;

		if (!pp_location_spells(c->p, AST_NODE(l)->loc, cc_identifier_string(nm), (uint32_t)cc_identifier_length(nm)) ||
		    !pp_location_spells(c->p, AST_NODE(r)->loc, cc_identifier_string(nm), (uint32_t)cc_identifier_length(nm)))
			return;	/* (clang: not from a macro) */
	}
	{
		diag_span rr[2] = { { AST_NODE(l)->loc, pp_token_end_find(c->p, AST_NODE(l)->loc) }, { AST_NODE(r)->loc, pp_token_end_find(c->p, AST_NODE(r)->loc) } };

		cc_report_warning(c, "self-assign", false, cc_warning_group_self_assign, loc, rr, 2, NULL, 0,
			 "explicitly assigning value of variable of type '%s' to itself", tyname(c, AST_NODE(l)->ty));
	}
}
