/* vcc front end: _Static_assert and the notes of a failed one */
#include <ctype.h>

#include "cc-internal.h"

/* the layout of record t as a note at its definition (D5.3): its size, where the
   padding is, and the size in alignment order when that is smaller */
static __attribute__((cold)) void cc_static_assert_layout_note(cc *c, ast_qualified_type t)
{
	const tag *g = &c->tags[AST_TYPE(c, t)->aux];
	char b[96];
	int npad = 0;
	uint64_t end = 0, sorted = 0;
	bool flex = false, bits = false;
	diag_record *d;

	if (!g->nfield || !cc_type_complete(c, t))
		return;
	d = diag_record_begin(c->p, DIAG_LEVEL_NOTE, g->loc, "'%s' is %llu bytes (alignment %u)", tyname(c, t),
			  (unsigned long long)g->size, g->align);
	d->id = "vcc_record_layout";
	for (uint32_t i = 0; i < g->nfield; i++) {
		const field *f = &g->f[i];
		uint64_t sz = f->bitfield ? 0 : (cc_type_is_array(c, f->ty) && !cc_type_complete(c, f->ty) ? 0 : cc_type_size(c, f->ty));

		bits |= f->bitfield;
		flex |= !f->bitfield && cc_type_is_array(c, f->ty) && !cc_type_complete(c, f->ty);
		if (!(g->flags & AST_RECORD_FLAG_UNION) && !f->bitfield && f->off / 8 > end && i) {
			const field *pf = &g->f[i - 1];

			npad++;
			snprintf(b, sizeof b, "%llu bytes of padding after this", (unsigned long long)(f->off / 8 - end));
			diag_record_label_add(c->p, d, pf->loc, pp_token_end_find(c->p, pf->loc), b);
		}
		if (f->bitfield) {
			uint64_t e = (f->off + f->width + 7) / 8;

			if (e > end)
				end = e;
		} else if (f->off / 8 + sz > end)
			end = f->off / 8 + sz;
	}
	if (!(g->flags & AST_RECORD_FLAG_UNION) && g->size > end) {
		npad++;
		snprintf(b, sizeof b, "%llu bytes of padding at the end", (unsigned long long)(g->size - end));
		diag_record_label_add(c->p, d, g->f[g->nfield - 1].loc, pp_token_end_find(c->p, g->f[g->nfield - 1].loc), b);
	}
	/* the members in decreasing alignment (no bit-fields, no flexible array) */
	if (npad && !bits && !flex && !(g->flags & AST_RECORD_FLAG_UNION) && !g->maxalign) {
		uint32_t al[64], n = g->nfield < 64 ? g->nfield : 0;

		for (uint32_t i = 0; i < n; i++)
			al[i] = cc_type_alignment(c, g->f[i].ty);
		for (uint32_t a = 64; a && n; a /= 2)
			for (uint32_t i = 0; i < n; i++)
				if (al[i] == a)
					sorted = (sorted + a - 1) / a * a + cc_type_size(c, g->f[i].ty);
		sorted = (sorted + g->align - 1) / g->align * g->align;
		if (n && sorted < g->size)
			diag_record_help_set(c->p, d, "ordering its members by decreasing alignment makes it %llu bytes",
				     (unsigned long long)sorted);
	}
	diag_record_emit(c->p, d);
}

/* the records named by "sizeof(struct X)" / "sizeof(AST_TYPE)" in the source text of r */
static __attribute__((cold)) void cc_static_assert_sizeof_layouts(cc *c, diag_span r)
{
	size_t len = 0;
	const char *t = pp_source_text(c->p, r.b, r.e, &len), *e;
	int shown = 0;

	if (!t)
		return;
	e = t + len;
	for (const char *q = t; q + 6 <= e && shown < 2; q++) {
		const char *n;
		bool tagged = false;
		uint32_t id, s;
		ast_qualified_type ty = 0;

		if (memcmp(q, "sizeof", 6) || (q > t && (isalnum((unsigned char)q[-1]) || q[-1] == '_')))
			continue;
		q += 6;
		while (q < e && (*q == ' ' || *q == '\t' || *q == '('))
			q++;
		if (e - q > 7 && (!memcmp(q, "struct", 6) || !memcmp(q, "union ", 6)) && (q[6] == ' ' || q[6] == '\t'))
			tagged = true, q += 6;
		while (q < e && (*q == ' ' || *q == '\t'))
			q++;
		for (n = q; q < e && (isalnum((unsigned char)*q) || *q == '_'); q++)
			;
		if (q == n || !(id = vintern_find(n, (size_t)(q - n), vintern_hash(n, (size_t)(q - n)))))
			continue;
		if (tagged && (s = cc_tag_lookup(c, id)))
			ty = AST_QUALIFIED_TYPE(c->tags[c->syms[s].val].ty, 0);
		else if (!tagged && (s = cc_symbol_lookup(c, id)) && c->syms[s].kind == AST_SYMBOL_TYPEDEF)
			ty = c->syms[s].ty;
		if (ty && cc_type_is_record(c, ty)) {
			cc_static_assert_layout_note(c, ty);
			shown++;
		}
	}
}

/* an integer literal as written (clang's IntegerLiteral), not a folded constant */
static bool cc_static_assert_is_integer_literal(cc *c, uint32_t e)
{
	diag_span r = cc_node_range(c, e);
	size_t len = 0;
	const char *t = pp_source_text(c->p, r.b, r.e, &len);

	return AST_NODE(e)->op == AST_NODE_NUMBER && t && len && *t >= '0' && *t <= '9';
}

/* clang's findFailedBooleanCondition: the failing operand of a chain of && */
static uint32_t cc_static_assert_failed_term(cc *c, uint32_t e)
{
	for (;;) {
		i128 a;

		while (AST_NODE(e)->op == AST_NODE_CAST && !c->span[e].e)
			e = AST_NODE(e)->a;
		if (AST_NODE(e)->op != AST_NODE_LOGICAL_AND)
			return e;
		e = cc_constant_integer(c, AST_NODE(e)->a, &a) && !a ? AST_NODE(e)->a : AST_NODE(e)->b;
	}
}

/* "static assertion failed": the requirement that failed, its operands' values, and
   the layout of the records whose sizes it compares */
static __attribute__((cold, noinline)) void cc_static_assert_failure_report(cc *c, uint32_t e, const char *msg)
{
	diag_span r;
	size_t len = 0;
	const char *t;

	e = cc_static_assert_failed_term(c, e);
	r = cc_node_range(c, e);
	t = pp_source_text(c->p, r.b, r.e, &len);
	if (!t)
		t = "", len = 0;
	if (cc_static_assert_is_integer_literal(c, e)) {	/* clang: a literal is no requirement */
		report(c, "error", r.b, &r, 1, NULL, 0, "static assertion failed%s%s", msg[0] ? ": " : "", msg);
		return;
	}
	{
		diag_record *d;

		diag_report_site_pc = __builtin_return_address(0);
		cc_report_error_limit(c), c->errors++, c->ndiag++;
		d = diag_record_begin(c->p, DIAG_LEVEL_ERROR, r.b, "static assertion failed due to requirement '%.*s'%s%s",
				  (int)len, t, msg[0] ? ": " : "", msg);
		diag_record_label_add(c->p, d, r.b, r.e, NULL);
		/* a size that is the target's: say what it is on the other one */
		if (memmem(t, len, "long double", 11))
			diag_record_help_set(c->p, d, "'long double' has 8 bytes on Apple (it is 'double') and 16 on Arm Linux (binary128)");
		diag_record_emit(c->p, d);
	}
	while (AST_NODE(e)->op == AST_NODE_CAST && !c->span[e].e)
		e = AST_NODE(e)->a;
	if (AST_NODE(e)->op == AST_NODE_BINARY && cc_operator_is_comparison(AST_NODE(e)->aux)) {
		i128 a, b;

		if (cc_constant_integer(c, AST_NODE(e)->a, &a) && cc_constant_integer(c, AST_NODE(e)->b, &b) &&
		    !(cc_static_assert_is_integer_literal(c, AST_NODE(e)->a) && cc_static_assert_is_integer_literal(c, AST_NODE(e)->b))) {	/* clang: nothing to evaluate */
			diag_span rr[2] = { r, r };
			size_t ol;
			const char *op = pp_source_text(c->p, AST_NODE(e)->loc, pp_token_end_find(c->p, AST_NODE(e)->loc), &ol);

			if (op)
				report(c, "note", AST_NODE(e)->loc, rr, 2, NULL, 0, "expression evaluates to '%lld %.*s %lld'",
				       (long long)a, (int)ol, op, (long long)b);
		}
	}
	cc_static_assert_sizeof_layouts(c, r);
}

CC_PRIVATE void cc_static_assert_parse(cc *c)
{
	uint32_t loc = tok(c)->loc, n, e, open;
	i128 v;
	char msg[256] = "";

	next(c);
	open = cc_token_expect_left_paren(c);
	e = cc_assignment_parse(c);
	n = cc_node_rvalue(c, e);
	if (!cc_constant_integer(c, n, &v)) {
		cc_report_error(c, loc, "static assertion expression is not an integral constant expression");
		v = 1;
	}
	if (cc_token_accept(c, LEX_TOKEN_COMMA)) {
		size_t k = 0;

		while (cc_token_kind_is_string(tok(c)->kind)) {
			char b[256];
			size_t l;

			cc_token_spelling(tok(c), b, sizeof(b));
			l = strlen(b);
			/* the literal's text without its quotes (escapes as written) */
			if (l >= 2 && b[l - 1] == '"') {
				const char *q = strchr(b, '"') + 1;

				l = (size_t)(b + l - 1 - q);
				if (k + l < sizeof(msg))
					memcpy(msg + k, q, l), k += l;
			}
			next(c);
		}
		msg[k] = 0;
	}
	if (!v && !AST_NODE_IS_ERROR(e))	/* (clang: nothing for an invalid condition) */
		cc_static_assert_failure_report(c, e, msg);
	cc_token_expect_match(c, LEX_TOKEN_RIGHT_PAREN, open);
	cc_token_expect_semicolon(c, "after static_assert");
}
