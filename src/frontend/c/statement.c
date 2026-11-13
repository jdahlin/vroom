/* vcc front end: statements (asm statements and switch checks included) */
#include "cc-internal.h"
#include <ctype.h>

/* ---------------- statements ---------------- */

CC_PRIVATE uint32_t cc_label_reference(cc *c, uint32_t name, uint32_t loc)
{
	uint32_t l = cc_scope_get(c->ldir, name);

	if (l)
		return l;
	l = c->nlabel++;
	memset(&c->labels[l], 0, sizeof(label));
	c->labels[l].name = name;
	c->labels[l].loc = loc;
	c->labels[l].shadow = 0;
	*cc_scope_slot(c, c->ldir, name) = l;
	return l;
}

CC_PRIVATE uint32_t cc_statement_new(cc *c, int op, uint32_t loc, uint32_t a, uint32_t b, uint32_t cc_)
{
	uint32_t n = cc_node_new(c, op, cc_type_basic(TY_VOID), loc);

	AST_NODE(n)->a = a, AST_NODE(n)->b = b, AST_NODE(n)->c = cc_;
	return n;
}

/* -Wparentheses: "if (x = 1)" */
static void cc_statement_assignment_condition_warn(cc *c, uint32_t e)
{
	if (AST_NODE(e)->op == N_ASSIGN && !AST_NODE(e)->aux && !(AST_NODE(e)->flags & (NF_PAREN | NF_MACRO | NF_ERR))) {
		uint32_t op = AST_NODE(e)->loc;
		diag_span r[2] = { cc_node_range(c, AST_NODE(e)->a), cc_node_range(c, AST_NODE(e)->b) };
		diag_span all = { r[0].b, r[1].e };
		diag_fixit p2[2] = { { all.b, all.b, "(", DIAG_APPLICABILITY_MAYBE }, { all.e, all.e, ")", DIAG_APPLICABILITY_MAYBE } };
		diag_fixit eq = { op, op + 1, "==", DIAG_APPLICABILITY_MAYBE };

		if (cc_report_warning(c, "parentheses", true, cc_warning_group_all, op, &all, 1, NULL, 0,
			  "using the result of an assignment as a condition without parentheses")) {
			diag_info d1 = { "note", op, NULL, 0, p2, 2, NULL, 0 }, d2 = { "note", op, NULL, 0, &eq, 1, NULL, 0 };

			diag_report_extended(c->p, &d1, "place parentheses around the assignment to silence this warning");
			diag_report_extended(c->p, &d2, "use '==' to turn this assignment into an equality comparison");
		}
	}
}

/* Large switches are checked with a sorted interval sweep, rather than a
 * quadratic scan at every label. Keep the small-switch diagnostic order. */
typedef struct cc_switch_case_order {
	struct swcase v;
	uint32_t index;
} cc_switch_case_order;

static int cc_statement_case_compare(const void *ap, const void *bp)
{
	const cc_switch_case_order *a = ap, *b = bp;

	if (a->v.lo != b->v.lo)
		return a->v.lo < b->v.lo ? -1 : 1;
	return (a->index > b->index) - (a->index < b->index);
}

static void cc_statement_switch_duplicates_check(cc *c)
{
	uint32_t n = c->ncase - c->case_base, count = 0;
	cc_switch_case_order *a, *high;

	if (n <= 64)
		return;
	a = cc_heap_allocate(c, n * sizeof(*a));
	for (uint32_t i = 0; i < n; i++) {
		struct swcase v = c->cases[c->case_base + i];

		if (v.lo <= v.hi)
			a[count++] = (cc_switch_case_order){v, i};
	}
	qsort(a, count, sizeof(*a), cc_statement_case_compare);
	if (!count)
		return;
	high = &a[0];
	for (uint32_t i = 1; i < count; i++) {
		cc_switch_case_order *cur = &a[i];

		if (cur->v.lo <= high->v.hi) {
			cc_switch_case_order *later = cur->index > high->index ? cur : high;
			cc_switch_case_order *prior = later == cur ? high : cur;

			if (later->index >= 64) {
				char b[48];

				snprintf(b, sizeof b, "%lld", (long long)cur->v.lo);
				report(c, "error", later->v.loc, NULL, 0, NULL, 0,
				       later->v.lo == later->v.hi && prior->v.lo == prior->v.hi ?
				       "duplicate case value '%s'" : "duplicate case value '%s' in case range", b);
				cc_report_note(c, prior->v.loc, "previous case defined here");
			}
		}
		if (cur->v.hi > high->v.hi)
			high = cur;
	}
}

/* -Wswitch: "enumeration value 'CC_COMPLEX_BINARY' not handled in switch" (no default label) */
static void cc_statement_switch_enum_warn(cc *c, qty et, uint32_t cnd)
{
	const tag *t = &c->tags[AST_TYPE(c, et)->aux];
	uint32_t miss[3], nmiss = 0;
	char b[512];
	size_t k = 0;

	for (uint32_t i = 0; i < t->nenum; i++) {
		const sym *y = &c->syms[c->lists[t->enums + i]];
		bool seen = false;

		for (uint32_t j = c->case_base; j < c->ncase && !seen; j++)
			seen = c->cases[j].lo <= y->val && y->val <= c->cases[j].hi;
		/* an enumerator repeating an earlier one's value counts once */
		for (uint32_t j = 0; j < i && !seen; j++)
			seen = c->syms[c->lists[t->enums + j]].val == y->val;
		if (!seen) {
			if (nmiss < 3)
				miss[nmiss] = c->lists[t->enums + i];
			nmiss++;
		}
	}
	if (!nmiss)
		return;
	for (uint32_t i = 0; i < nmiss && i < 3; i++) {
		const char *sep = i == 0 ? "" : nmiss == 2 ? " and " : (i == 2 && nmiss == 3) ? ", and " : ", ";

		k += (size_t)snprintf(b + k, sizeof(b) - k, "%s'%.*s'", sep, CC_IDENTIFIER(c->syms[miss[i]].name));
		if (k >= sizeof(b))
			return;
	}
	if (nmiss > 3)
		snprintf(b + k, sizeof(b) - k, ", and %u more", nmiss - 3);
	{
		diag_span r = cc_node_range(c, cnd);

		cc_report_warning(c, "switch", true, cc_warning_group_all, r.b, NULL, 0, NULL, 0, "enumeration value%s %s not handled in switch",
		      nmiss > 1 ? "s" : "", b);
	}
}

static uint32_t cc_statement_condition_parse(cc *c, const char *kw)
{
	uint32_t e, open = tok(c)->loc;

	if (unlikely(!cc_token_is(c, LEX_TOKEN_LEFT_PAREN)))
		cc_token_expect_left_paren_fail(c, kw);
	next(c);
	e = cc_conversion_decay(c, expr(c));
	cc_statement_assignment_condition_warn(c, e);
	cc_token_expect_close(c, LEX_TOKEN_RIGHT_PAREN, open, e);
	return cc_condition_scalar(c, e, "statement");
}

static uint32_t cc_asm_string_parse(cc *c)
{
	return cc_attribute_string_parse(c, 0);
}

static void cc_asm_operands_parse(cc *c, cc_id_list *ops, bool out)
{
	if (cc_token_is(c, LEX_TOKEN_COLON) || cc_token_is(c, LEX_TOKEN_COLON_COLON) || cc_token_is(c, LEX_TOKEN_RIGHT_PAREN))
		return;
	for (;;) {
		uint32_t name = 0, cons, e;

		if (cc_token_accept(c, LEX_TOKEN_LEFT_SQUARE)) {
			if (cc_token_is(c, LEX_TOKEN_IDENTIFIER))
				name = tok(c)->id;
			next(c);
			cc_token_expect(c, LEX_TOKEN_RIGHT_SQUARE, "']'");
		}
		cons = cc_asm_string_parse(c);
		cc_token_expect(c, LEX_TOKEN_LEFT_PAREN, "'('");
		e = expr(c);
		cc_token_expect(c, LEX_TOKEN_RIGHT_PAREN, "')'");
		if (out) {
			if (!(AST_NODE(e)->flags & NF_LVALUE))
				cc_report_error(c, AST_NODE(e)->loc, "invalid lvalue in asm output");
		} else
			e = cc_conversion_decay(c, e);
		cc_id_list_append(c, ops, name);
		cc_id_list_append(c, ops, cons);
		cc_id_list_append(c, ops, e);
		if (!cc_token_accept(c, LEX_TOKEN_COMMA))
			break;
	}
}

/* one ':' of an asm statement; "::" counts as two */
static int cc_asm_colons_parse(cc *c, int *pending)
{
	if (*pending) {
		(*pending)--;
		return 1;
	}
	if (cc_token_accept(c, LEX_TOKEN_COLON))
		return 1;
	if (cc_token_accept(c, LEX_TOKEN_COLON_COLON)) {
		*pending = 1;
		return 1;
	}
	return 0;
}

CC_PRIVATE uint32_t cc_asm_statement_parse(cc *c, bool toplevel)
{
	uint32_t loc = tok(c)->loc, ai = c->nasm++, n;
	casm *a = &c->asms[ai];

	if (cc_token_keyword(tok(c)) == KW_asm)	/* (clang -pedantic: the GNU spelling) */
		CC_REPORT_EXTENSION_MICROSOFT(c, "asm", "language-extension-token", cc_warning_group_pedantic, loc, "extension used");
	cc_id_list outs, ins, clob, labs;
	int pending = 0;

	memset(a, 0, sizeof(*a));
	next(c);
	for (;;) {
		int kw = cc_token_keyword(tok(c));

		if (kw == KW_volatile || kw == KW___volatile__ || kw == KW___volatile)
			a->vol = 1;
		else if (kw == KW_inline || kw == KW___inline__ || kw == KW___inline)
			a->inl = 1;
		else if (kw == KW_goto)
			a->go = 1;
		else
			break;
		next(c);
	}
	cc_id_list_init(&outs), cc_id_list_init(&ins), cc_id_list_init(&clob), cc_id_list_init(&labs);
	cc_token_expect(c, LEX_TOKEN_LEFT_PAREN, "'('");
	a->tmpl = cc_asm_string_parse(c);
	if (!toplevel && cc_asm_colons_parse(c, &pending)) {
		a->extended = 1;
		if (!pending)
			cc_asm_operands_parse(c, &outs, true);
		if (cc_asm_colons_parse(c, &pending)) {
			if (!pending)
				cc_asm_operands_parse(c, &ins, false);
			if (cc_asm_colons_parse(c, &pending)) {
				if (!pending && !cc_token_is(c, LEX_TOKEN_COLON) && !cc_token_is(c, LEX_TOKEN_COLON_COLON) && !cc_token_is(c, LEX_TOKEN_RIGHT_PAREN)) {
					do
						cc_id_list_append(c, &clob, cc_asm_string_parse(c));
					while (cc_token_accept(c, LEX_TOKEN_COMMA));
				}
				if (cc_asm_colons_parse(c, &pending)) {
					if (!cc_token_is(c, LEX_TOKEN_RIGHT_PAREN)) {
						do {
							if (!cc_token_is(c, LEX_TOKEN_IDENTIFIER))
								cc_report_fatal(c, tok(c)->loc, "expected identifier");
							cc_id_list_append(c, &labs, cc_label_reference(c, tok(c)->id, tok(c)->loc));
							next(c);
						} while (cc_token_accept(c, LEX_TOKEN_COMMA));
					}
				}
			}
		}
	}
	cc_token_expect(c, LEX_TOKEN_RIGHT_PAREN, "')'");
	a = &c->asms[ai];
	a->nout = outs.n / 3;
	a->nin = ins.n / 3;
	a->ops = cc_heap_allocate(c, (a->nout + a->nin + 1) * sizeof(asmop));
	for (uint32_t i = 0; i < a->nout; i++)
		a->ops[i] = (asmop){ outs.v[3 * i], outs.v[3 * i + 1], outs.v[3 * i + 2] };
	for (uint32_t i = 0; i < a->nin; i++)
		a->ops[a->nout + i] = (asmop){ ins.v[3 * i], ins.v[3 * i + 1], ins.v[3 * i + 2] };
	a->nclob = clob.n;
	a->clob = cc_heap_allocate(c, (clob.n + 1) * sizeof(uint32_t));
	memcpy(a->clob, clob.v, clob.n * sizeof(uint32_t));
	a->nlabel = labs.n;
	a->labels = cc_heap_allocate(c, (labs.n + 1) * sizeof(uint32_t));
	memcpy(a->labels, labs.v, labs.n * sizeof(uint32_t));
	cc_token_expect_semicolon(c, "after asm statement");
	n = cc_statement_new(c, N_ASM, loc, ai, 0, 0);
	return n;
}

CC_PRIVATE bool cc_token_at_asm(cc *c)
{
	int kw = cc_token_keyword(tok(c));

	return kw == KW_asm || kw == KW___asm__ || kw == KW___asm;
}

/* clang: an unknown identifier at the start of a statement that is a typo of a
   keyword ("retur x;", "stati int n;") is reported with the keyword and parsed as
   it (decls: a declaration may stand here).  Only before a token that cannot go on
   an expression, so a valid statement costs a lookup */
static bool cc_statement_keyword_typo(cc *c, bool decls)
{
	ptok *t = tok(c);
	uint32_t kw;
	int k;

	if (t->kind != LEX_TOKEN_IDENTIFIER || cc_token_keyword(t) || cc_symbol_lookup(c, t->id))	/* declared: no peek */
		return false;
	k = cc_token_peek(c, 1)->kind;
	if (k != LEX_TOKEN_IDENTIFIER && k != LEX_TOKEN_NUMBER && k != LEX_TOKEN_CHARACTER && !cc_token_kind_is_string(k) && k != LEX_TOKEN_LEFT_BRACE &&
	    k != LEX_TOKEN_SEMICOLON && k != LEX_TOKEN_STAR)
		return false;
	t = tok(c);
	if (!(kw = cc_typo_keyword_find(c, t->id, CC_TYPO_KEYWORDS_STATEMENT)))
		return false;
	if (!decls && (cc_keyword_class[cc_keyword_map[kw]] & CC_KEYWORD_CLASS_STARTS_DECLARATION))
		return false;
	cc_undeclared_report(c, "use of undeclared identifier", t->id, t->loc, false, CC_TYPO_KEYWORDS_STATEMENT, &kw);
	tok(c)->id = kw;
	return true;
}

/* "size_t n = 1;", "FILE *fp;" with the type's header missing: clang reports the
   undeclared identifier and then each declarator's uses; vcc parses a declaration of an
   invalid type, so the uses say nothing more.  Only where an expression cannot go on
   (a name after a name), or "AST_TYPE *p" with AST_TYPE a standard type (frontend/c/libnames.inc) */
static __attribute__((cold, noinline)) bool cc_statement_undeclared_type_declaration(cc *c, cc_id_list *items)
{
	const ptok *t = tok(c);
	int i = 1, k;

	if (cc_symbol_lookup(c, t->id) || (t->flags & PP_TOKEN_FLAG_FROM_MACRO))
		return false;
	if (cc_token_peek(c, 1)->kind == LEX_TOKEN_STAR) {
		if (!cc_library_header(c, t->id))
			return false;
		i = 2;
	}
	if (cc_token_peek(c, i)->kind != LEX_TOKEN_IDENTIFIER || cc_token_keyword(cc_token_peek(c, i)))
		return false;
	k = cc_token_peek(c, i + 1)->kind;
	if (k != LEX_TOKEN_EQUAL && k != LEX_TOKEN_SEMICOLON && k != LEX_TOKEN_COMMA && k != LEX_TOKEN_LEFT_SQUARE)
		return false;
	c->undecl_ty = 1;
	cc_declaration_parse(c, items, false);
	c->undecl_ty = 0;
	return true;
}

/* the ';' after a part (e) of a for header; clang when it is missing: reported
   (unless e was invalid), then the rest of the part skipped up to a ';' or the ')' */
static void cc_statement_for_semicolon_expect(cc *c, uint32_t e)
{
	if (likely(cc_token_accept(c, LEX_TOKEN_SEMICOLON)))
		return;
	if (!e || !AST_NODE_IS_ERROR(e))
		cc_token_expect_semicolon(c, "in 'for' statement specifier");
	cc_recovery_skip_close(c, -1);
	cc_token_accept(c, LEX_TOKEN_SEMICOLON);
}

/* one declaration or statement of a block (at loc), appended to items */
static void cc_compound_statement_item_parse(cc *c, uint32_t loc, cc_id_list *items, bool se, uint32_t *sedone)
{
	if (se && items->n > *sedone) {	/* a statement expression: the one before was not the last */
		uint32_t ls = items->v[items->n - 1];

		while (AST_NODE(ls)->op == N_LABEL)
			ls = AST_NODE(ls)->c;
		if (AST_NODE(ls)->op == N_EXPR && AST_NODE(ls)->a)
			cc_unused_set_only_check(c, AST_NODE(ls)->a), cc_unused_value_check(c, AST_NODE(ls)->a, false);
		*sedone = items->n;
	}
	if (cc_token_is(c, LEX_TOKEN_EOF)) {
		report(c, "error", pp_main_file_end(c->p), NULL, 0, NULL, 0, "expected '}'");
		cc_report_note(c, loc, "to match this '{'");
		longjmp(c->bail, 1);
	}
	if (cc_token_is_keyword(c, KW___extension__) && cc_token_is_declaration_start(c, cc_token_peek(c, 1)))
		next(c);
	if (cc_token_is_keyword(c, KW___attribute__) || cc_token_is_keyword(c, KW___attribute) || cc_token_at_standard_attribute(c)) {
		attrs a = { 0 };

		cc_attribute_list_parse(c, &a);
		if (cc_token_accept(c, LEX_TOKEN_SEMICOLON))
			return;	/* __attribute__((fallthrough)); */
		if (cc_token_is_declaration_start(c, tok(c))) {
			c->pend = a;
			c->has_pend = true;
			cc_declaration_parse(c, items, false);
			return;
		}
	}
	if (cc_token_is_declaration_start(c, tok(c)) && !(cc_token_is(c, LEX_TOKEN_IDENTIFIER) && cc_token_peek(c, 1)->kind == LEX_TOKEN_COLON)) {
		cc_declaration_parse(c, items, false);
		return;
	}
	if (cc_token_is(c, LEX_TOKEN_IDENTIFIER) && !cc_token_keyword(tok(c)) && cc_statement_keyword_typo(c, true) && cc_token_is_declaration_start(c, tok(c))) {
		cc_declaration_parse(c, items, false);
		return;
	}
	if (cc_token_is(c, LEX_TOKEN_IDENTIFIER) && !cc_token_keyword(tok(c)) && (cc_token_peek(c, 1)->kind == LEX_TOKEN_IDENTIFIER || cc_token_peek(c, 1)->kind == LEX_TOKEN_STAR) &&
	    cc_statement_undeclared_type_declaration(c, items))
		return;
	c->se_direct = se;
	uint32_t n = cc_statement_parse(c);

	if (AST_NODE(n)->op == N_BLOCK && AST_NODE(n)->aux == 1) {
		/* A flat label list is part of this block: in particular, its last
		 * labeled expression still supplies a statement expression's value. */
		for (uint32_t i = 0; i < AST_NODE(n)->c; i++)
			cc_id_list_append(c, items, c->lists[AST_NODE(n)->b + i]);
	} else {
		cc_id_list_append(c, items, n);
	}
}

CC_PRIVATE uint32_t cc_compound_statement_parse(cc *c, qty *last)
{
	uint32_t loc = tok(c)->loc, n, sedone = 0;
	cc_id_list items, locals;

	cc_id_list_init(&items);
	cc_id_list_init(&locals);
	cc_token_expect(c, LEX_TOKEN_LEFT_BRACE, "'{'");
	cc_scope_open(c);
	/* __label__ a, b; */
	while (cc_token_is_keyword(c, KW___label__)) {
		next(c);
		do {
			uint32_t l;

			if (!cc_token_is(c, LEX_TOKEN_IDENTIFIER))
				cc_report_fatal(c, tok(c)->loc, "expected identifier");
			l = c->nlabel++;
			memset(&c->labels[l], 0, sizeof(label));
			c->labels[l].name = tok(c)->id;
			c->labels[l].loc = tok(c)->loc;
			c->labels[l].local = 1;
			c->labels[l].shadow = cc_scope_get(c->ldir, tok(c)->id);
			*cc_scope_slot(c, c->ldir, tok(c)->id) = l;
			cc_id_list_append(c, &locals, l);
			next(c);
		} while (cc_token_accept(c, LEX_TOKEN_COMMA));
		cc_token_expect_semicolon(c, "at end of declaration");
	}
	{
		recov r;

		if (CC_RECOVERY_ARM(c, &r))
			cc_recovery_restore(c, &r, CC_RECOVERY_BLOCK);	/* after a parse error: on with the next statement */
		while (!cc_token_is(c, LEX_TOKEN_RIGHT_BRACE))
			cc_compound_statement_item_parse(c, loc, &items, last != NULL, &sedone);	/* (sedone: in memory across the longjmp) */
		CC_RECOVERY_POP(c, &r);
	}
	next(c);
	if (last) {
		*last = cc_type_basic(TY_VOID);
		uint32_t ls = items.n ? items.v[items.n - 1] : 0;

		while (ls && AST_NODE(ls)->op == N_LABEL)	/* clang: a labeled statement gives its value */
			ls = AST_NODE(ls)->c;
		if (ls && AST_NODE(ls)->op == N_EXPR && AST_NODE(ls)->a) {
			uint32_t e = cc_conversion_decay(c, AST_NODE(ls)->a);

			*last = QUNQ(AST_NODE(e)->ty);
		}
	}
	for (uint32_t i = locals.n; i-- > 0;) {
		label *l = &c->labels[locals.v[i]];

		*cc_scope_slot(c, c->ldir, l->name) = l->shadow;
	}
	cc_scope_close(c);
	n = cc_statement_new(c, N_BLOCK, loc, 0, cc_id_list_freeze(c, &items), items.n);
	return n;
}

/* the body of an if, switch or loop: a block of its own (C99 6.8.4p3, 6.8.5p5) */
static uint32_t cc_statement_sub_parse(cc *c)
{
	uint32_t n;

	if (c->o->std != 89) cc_scope_open(c);
	n = cc_statement_parse(c);
	if (c->o->std != 89) cc_scope_close(c);
	return n;
}

/* what follows a label, case or default: nothing before '}', a declaration (C23) or a statement */
static uint32_t cc_labeled_statement_body_parse(cc *c, uint32_t loc)
{
	if (cc_token_is(c, LEX_TOKEN_RIGHT_BRACE))	/* label at end of compound statement (C23) */
		return cc_statement_new(c, N_EXPR, loc, 0, 0, 0);
	uint32_t dl = tok(c)->loc;
	bool pend = false;

	if (cc_token_is_keyword(c, KW___attribute__) || cc_token_is_keyword(c, KW___attribute)) {
		attrs a = { 0 };

		cc_attribute_list_parse(c, &a);
		if (cc_token_accept(c, LEX_TOKEN_SEMICOLON))
			return cc_statement_new(c, N_EXPR, loc, 0, 0, 0);	/* fallthrough; */
		if (!cc_token_is_declaration_start(c, tok(c)))
			return cc_statement_parse(c);
		c->pend = a, c->has_pend = true, pend = true;	/* __attribute__((unused)) int x; */
	}
	if (pend || (cc_token_is_declaration_start(c, tok(c)) && !(cc_token_is(c, LEX_TOKEN_IDENTIFIER) && cc_token_peek(c, 1)->kind == LEX_TOKEN_COLON))) {
		cc_id_list items;

		if (cc_keyword_map != cc_keyword_map_c23)
			cc_report_warning(c, "c23-extensions", true, NULL, dl, NULL, 0, NULL, 0,
				 "label followed by a declaration is a C23 extension");
		cc_id_list_init(&items);
		cc_declaration_parse(c, &items, false);
		/* Declaration packing does not introduce a lexical scope. */
		uint32_t n = cc_statement_new(c, N_BLOCK, loc, 0, cc_id_list_freeze(c, &items), items.n);

		AST_NODE(n)->aux = 1;
		return n;
	}
	return cc_statement_parse(c);
}

/* Flat label sequences are one block, not a chain of recursive statements.
 * This also bounds recursion in the semantic and machine-code AST walkers. */
static bool cc_token_at_label(cc *c)
{
	return cc_token_is_keyword(c, KW_case) || cc_token_is_keyword(c, KW_default) ||
	       (cc_token_is(c, LEX_TOKEN_IDENTIFIER) && !cc_token_keyword(tok(c)) && cc_token_peek(c, 1)->kind == LEX_TOKEN_COLON);
}

/* Wide switches currently retain case bounds in signed i128 storage.  The
 * source signedness must be checked before that storage loses it. */
static void cc_statement_case_constant(cc *c, i128 *value)
{
	uint32_t n;
	if (cc_constant_expression_parse(c, value, &n) && c->switch_ty && cc_type_is_bitint(c, c->switch_ty) &&
	    !cc_type_is_signed(c, AST_NODE(n)->ty) && *value < 0)
		cc_report_error(c, AST_NODE(n)->loc, "wide integer constant is not representable in this 128-bit constant-expression context");
}

static uint32_t cc_labeled_statement_head_parse(cc *c)
{
	ptok *t = tok(c);
	uint32_t loc = t->loc;
	int kw = cc_token_keyword(t);

	switch (t->kind) {
	case LEX_TOKEN_IDENTIFIER:
		if (!kw && cc_token_peek(c, 1)->kind == LEX_TOKEN_COLON) {
			uint32_t l = cc_label_reference(c, t->id, loc), n, s;
			attrs la = { 0 };

			next(c), next(c);
			cc_attribute_list_parse(c, &la);
			if (c->labels[l].defined)
				cc_report_error(c, loc, "redefinition of label '%.*s'", CC_IDENTIFIER(c->labels[l].name));
			s = 0;
			n = cc_statement_new(c, N_LABEL, loc, l, 0, s);
			c->labels[l].defined = n;
			return n;
		}
		break;
	default: break;
	}
	switch (kw) {
	case KW_case: {
		i128 lo, hi;
		uint32_t s, n;

		uint32_t vloc;

		next(c);
		vloc = tok(c)->loc;
		if (!c->in_switch)
			cc_report_error(c, loc, "'case' statement not in switch statement");
		cc_statement_case_constant(c, &lo);
		hi = lo;
		if (cc_token_is(c, LEX_TOKEN_ELLIPSIS)) {
			static const char *const g[] = { "pedantic", NULL };

			if (!c->o->c2y)
				CC_REPORT_EXTENSION_MICROSOFT(c, "...", "c2y-extensions", g, tok(c)->loc, "case ranges are a C2y extension");
			next(c);
			cc_statement_case_constant(c, &hi);
		}
		cc_token_expect(c, LEX_TOKEN_COLON, "':'");
		if (c->switch_ty)
			lo = cc_constant_truncate(c, lo, c->switch_ty), hi = cc_constant_truncate(c, hi, c->switch_ty);
		if (c->in_switch) {
			for (uint32_t i = c->case_base; c->ncase - c->case_base < 64 && i < c->ncase; i++)
				if (lo <= c->cases[i].hi && c->cases[i].lo <= hi) {
					char b[48];
					i128 d = lo > c->cases[i].lo ? lo : c->cases[i].lo;

					snprintf(b, sizeof(b), "%lld", (long long)d);
					report(c, "error", vloc, NULL, 0, NULL, 0, lo == hi && c->cases[i].lo == c->cases[i].hi
					       ? "duplicate case value '%s'" : "duplicate case value '%s' in case range", b);
					cc_report_note(c, c->cases[i].loc, "previous case defined here");
					break;
				}
			if (c->ncase == c->capcase) {
				c->capcase = c->capcase ? c->capcase * 2 : 64;
				c->cases = realloc(c->cases, c->capcase * sizeof(*c->cases));
			}
			c->cases[c->ncase++] = (struct swcase){ lo, hi, vloc };
		}
		s = 0;
		c->ipool[c->nipool] = lo;
		c->ipool[c->nipool + 1] = hi;
		if (c->uv_any & 1)
			c->uv_any |= 8;	/* (a jump from the switch: run the analysis) */
		n = cc_statement_new(c, N_CASE, loc, c->nipool, c->nipool + 1, s);
		c->nipool += 2;
		return n;
	}
	case KW_default: {
		uint32_t s;

		next(c);
		cc_token_expect(c, LEX_TOKEN_COLON, "':'");
		if (!c->in_switch)
			cc_report_error(c, loc, "'default' statement not in switch statement");
		c->sw_default = true;
		s = 0;
		if (c->uv_any & 1)
			c->uv_any |= 8;
		return cc_statement_new(c, N_DEFAULT, loc, 0, 0, s);
	}
	default: return 0;
	}
}

static uint32_t cc_labeled_statement_parse(cc *c, bool sed)
{
	cc_id_list labels;
	uint32_t loc = tok(c)->loc, last, body;
	bool named = true;

	cc_id_list_init(&labels);
	do {
		last = cc_labeled_statement_head_parse(c);
		named &= AST_NODE(last)->op == N_LABEL;
		cc_id_list_append(c, &labels, last);
	} while (cc_token_at_label(c));
	c->se_direct = sed;
	body = cc_labeled_statement_body_parse(c, AST_NODE(last)->loc);
	if (AST_NODE(body)->op == N_BLOCK && AST_NODE(body)->aux == 1) {
		/* Keep labeled declarations in the enclosing block, including all
		 * declarators and their cleanup/stack lifetimes. */
		for (uint32_t i = 0; i < AST_NODE(body)->c; i++)
			cc_id_list_append(c, &labels, c->lists[AST_NODE(body)->b + i]);
	} else if (sed && named) {
		/* In a statement expression the value is the final expression,
		 * with its labels emitted immediately before it. */
		cc_id_list_append(c, &labels, body);
	} else {
		AST_NODE(last)->c = body;
	}
	if (labels.n == 1)
		return last;
	uint32_t n = cc_statement_new(c, N_BLOCK, loc, 0, cc_id_list_freeze(c, &labels), labels.n);

	AST_NODE(n)->aux = 1; /* synthetic label list, flattened by cc_compound_statement_item_parse */
	return n;
}

CC_PRIVATE uint32_t cc_statement_parse(cc *c)
{
	ptok *t = tok(c);
	uint32_t loc = t->loc;
	int kw = cc_token_keyword(t);
	bool sed = c->se_direct;	/* a statement of a statement expression's body: checked there (not the last) */

	c->se_direct = 0;
	if (vstack_guard_low())	/* { { { ... and if/while bodies nest statements (cc_scope_open's limit may come later) */
		cc_report_fatal(c, loc, "statements nested too deeply");
	if (cc_token_at_label(c))
		return cc_labeled_statement_parse(c, sed);
	switch (t->kind) {
	case LEX_TOKEN_LEFT_BRACE:
		return cc_compound_statement_parse(c, NULL);
	case LEX_TOKEN_LEFT_SQUARE:
		if (cc_token_at_standard_attribute(c)) {	/* [[fallthrough]]; and friends: no effect on the statement */
			attrs a = { 0 };

			cc_attribute_list_parse(c, &a);
			return cc_statement_parse(c);
		}
		break;
	case LEX_TOKEN_SEMICOLON:
		next(c);
		return cc_statement_new(c, N_EXPR, loc, 0, 0, 0);
	default:
		break;
	}
	switch (kw) {
	case KW_if: {
		uint32_t cnd, th, el = 0;

		next(c);
		if (c->o->std != 89) cc_scope_open(c);	/* C99 6.8.4p3: a selection statement is a block, and so is each substatement */
		cnd = cc_statement_condition_parse(c, "if");
		{
			/* clang: runtime-behaviour warnings only in reachable code */
			cval cv;
			int k = cc_eval(c, cnd, &cv) && cv.kind == CV_INT ? cv.i != 0 : -1;
			uint32_t rp = c->prevloc, tb = tok(c)->loc, eb, n;

			c->cnd++;
			c->dead += k == 0;
			th = cc_statement_sub_parse(c);
			c->dead -= k == 0;
			cc_node_span_set(c, th, tb);	/* (for -Wsometimes-uninitialized's notes) */
			if (cc_token_is_keyword(c, KW_else)) {
				next(c);
				eb = tok(c)->loc;
				c->dead += k == 1;
				el = cc_statement_sub_parse(c);
				c->dead -= k == 1;
				cc_node_span_set(c, el, eb);
			}
			c->cnd--;
			if (c->o->std != 89) cc_scope_close(c);
			n = cc_statement_new(c, N_IF, loc, cnd, th, el);
			c->span[n] = (diag_span){ loc, rp };
			return n;
		}
	}
	case KW_while: {
		uint32_t cnd, body;

		next(c);
		if (c->o->std != 89) cc_scope_open(c);
		cnd = cc_statement_condition_parse(c, "while");
		c->in_loop++;
		c->cnd++, body = cc_statement_sub_parse(c), c->cnd--;
		c->in_loop--;
		if (c->o->std != 89) cc_scope_close(c);
		return cc_statement_new(c, N_WHILE, loc, cnd, body, 0);
	}
	case KW_do: {
		uint32_t cnd, body;

		next(c);
		if (c->o->std != 89) cc_scope_open(c);
		c->in_loop++;
		c->cnd++, body = cc_statement_sub_parse(c), c->cnd--;
		c->in_loop--;
		if (!cc_token_is_keyword(c, KW_while))
			cc_report_fatal(c, tok(c)->loc, "expected 'while' in do/while loop");
		next(c);
		cnd = cc_statement_condition_parse(c, "while");
		if (c->o->std != 89) cc_scope_close(c);
		cc_token_expect_semicolon(c, "after do/while statement");
		return cc_statement_new(c, N_DO, loc, cnd, body, 0);
	}
	case KW_for: {
		uint32_t init = 0, cnd = 0, step = 0, body, n;
		cc_id_list sb;
		recov r;
		uint32_t open;

		next(c);
		open = tok(c)->loc;
		if (unlikely(!cc_token_is(c, LEX_TOKEN_LEFT_PAREN)))
			cc_token_expect_left_paren_fail(c, "for");
		next(c);
		if (c->o->std != 89) cc_scope_open(c);
		if (CC_RECOVERY_ARM(c, &r)) {	/* an error in the header: on with the body */
			cc_recovery_restore(c, &r, CC_RECOVERY_FOR_HEAD);
			init = cnd = step = 0;
			goto for_body;
		}
		c->for_head = true;
		if (cc_token_is_declaration_start(c, tok(c))) {
			cc_id_list items;

			cc_id_list_init(&items);
			cc_declaration_parse(c, &items, false);
			init = cc_statement_new(c, N_BLOCK, loc, 0, cc_id_list_freeze(c, &items), items.n);
		} else {
			uint32_t e = 0;

			if (!cc_token_is(c, LEX_TOKEN_SEMICOLON))
				init = cc_statement_new(c, N_EXPR, loc, e = expr(c), 0, 0);
			cc_statement_for_semicolon_expect(c, e);
		}
		if (!cc_token_is(c, LEX_TOKEN_SEMICOLON)) {
			cnd = cc_conversion_decay(c, expr(c));
			cc_statement_assignment_condition_warn(c, cnd);
			cnd = cc_condition_scalar(c, cnd, "for");
		}
		cc_statement_for_semicolon_expect(c, cnd);
		c->for_head = false;
		if (!cc_token_is(c, LEX_TOKEN_RIGHT_PAREN))
			step = expr(c);
		cc_token_expect_close(c, LEX_TOKEN_RIGHT_PAREN, open, step);
	for_body:
		CC_RECOVERY_POP(c, &r);
		c->in_loop++;
		c->cnd++, body = cc_statement_sub_parse(c), c->cnd--;
		c->in_loop--;
		if (init && AST_NODE(init)->op == N_EXPR && AST_NODE(init)->a)	/* clang's ActOnForStmt: the first and third clauses */
			cc_unused_set_only_check(c, AST_NODE(init)->a), cc_unused_value_check(c, AST_NODE(init)->a, false);
		if (step)
			cc_unused_set_only_check(c, step);
		if (c->o->std != 89) cc_scope_close(c);
		cc_id_list_init(&sb);
		cc_id_list_append(c, &sb, step);
		cc_id_list_append(c, &sb, body);
		n = cc_statement_new(c, N_FOR, loc, init, cnd, cc_id_list_freeze(c, &sb));
		return n;
	}
	case KW_switch: {
		uint32_t cnd, body, open;
		qty et;
		qty saved = c->switch_ty;

		next(c);
		if (c->o->std != 89) cc_scope_open(c);
		open = tok(c)->loc;
		if (unlikely(!cc_token_is(c, LEX_TOKEN_LEFT_PAREN)))
			cc_token_expect_left_paren_fail(c, "switch");
		next(c);
		cnd = expr(c);
		et = AST_NODE(cnd)->ty;
		while (AST_NODE(cnd)->op == N_CAST && AST_NODE(cnd)->a && !c->span[cnd].e && AST_TYPE_KIND(c, AST_NODE(AST_NODE(cnd)->a)->ty) == TY_ENUM)
			cnd = AST_NODE(cnd)->a, et = AST_NODE(cnd)->ty;	/* -Wswitch looks at the enum before promotion */
		cnd = cc_node_rvalue(c, cnd);
		cc_token_expect_close(c, LEX_TOKEN_RIGHT_PAREN, open, cnd);
		if (!cc_type_is_integer(c, AST_NODE(cnd)->ty)) {
			diag_span r = cc_node_range(c, cnd);

			report(c, "error", loc, &r, 1, NULL, 0, "statement requires expression of integer type ('%s' invalid)",
			       tyname(c, AST_NODE(cnd)->ty));
		}
		c->switch_ty = AST_NODE(cnd)->ty;
		c->in_switch++;
		{
			uint32_t sb = c->case_base;
			bool sd = c->sw_default;

			c->case_base = c->ncase;
			c->sw_default = false;
			c->cnd++, body = cc_statement_sub_parse(c), c->cnd--;
			if (!c->sw_default && AST_TYPE_KIND(c, et) == TY_ENUM)
				cc_statement_switch_enum_warn(c, et, cnd);
			if (!c->sw_default) {
				cval cv;

				if (cc_eval(c, cnd, &cv) && cv.kind == CV_INT) {
					bool hit = false;

					for (uint32_t i = c->case_base; i < c->ncase && !hit; i++)
						hit = cv.i >= c->cases[i].lo && cv.i <= c->cases[i].hi;
					if (!hit) {
						diag_span r = cc_node_range(c, cnd);

						report(c, "warning", r.b, NULL, 0, NULL, 0, cc_type_is_signed(c, AST_NODE(cnd)->ty) ?
						       "no case matching constant switch condition '%lld'" : "no case matching constant switch condition '%llu'",
						       (long long)cv.i);
					}
				}
			}
			cc_statement_switch_duplicates_check(c);
			c->ncase = c->case_base, c->case_base = sb;
			c->sw_default = sd;
		}
		c->in_switch--;
		c->switch_ty = saved;
		if (c->o->std != 89) cc_scope_close(c);
		return cc_statement_new(c, N_SWITCH, loc, cnd, body, 0);
	}
	case KW_break:
		next(c);
		if (!c->in_loop && !c->in_switch)
			cc_report_error(c, loc, "'break' statement not in loop or switch statement");
		cc_token_expect_semicolon(c, "after break statement");
		return cc_statement_new(c, N_BREAK, loc, 0, 0, 0);
	case KW_continue:
		next(c);
		if (!c->in_loop)
			cc_report_error(c, loc, "'continue' statement not in loop statement");
		cc_token_expect_semicolon(c, "after continue statement");
		return cc_statement_new(c, N_CONTINUE, loc, 0, 0, 0);
	case KW_return: {
		uint32_t e = 0;
		qty rt = c->fn ? AST_TYPE(c, c->syms[c->fn].ty)->base : cc_type_basic(TY_INT);

		next(c);
		if (!cc_token_is(c, LEX_TOKEN_SEMICOLON)) {
			e = cc_conversion_decay(c, expr(c));
			if (!cc_type_is_void(c, rt) && !cc_type_is_void(c, AST_NODE(e)->ty))
				e = cc_type_is_record(c, rt) && !cc_type_is_complex(c, rt) ? e : cc_conversion_apply_as(c, e, rt, CC_CONVERSION_CONTEXT_RETURN, loc);
			else if (!cc_type_is_void(c, rt) && cc_type_is_void(c, AST_NODE(e)->ty) && !(AST_NODE(e)->flags & NF_ERR)) {
				diag_span r = cc_node_range(c, e);
				char f[256];

				snprintf(f, sizeof f, "%s", tyname(c, AST_NODE(e)->ty));	/* tyname: one buffer */
				report(c, "error", r.b, &r, 1, NULL, 0, "returning '%s' from a function with incompatible result type '%s'", f,
				       tyname(c, rt));
			}
			else if (cc_type_is_void(c, rt) && !cc_type_is_void(c, AST_NODE(e)->ty) && c->fn && !(AST_NODE(e)->flags & NF_ERR)) {
				diag_span r = cc_node_range(c, e);

				cc_report_flag_error(c, "return-mismatch", (const char *const[]){"return-type", NULL}, loc, &r, 1, "void function '%.*s' should not return a value",
				       CC_IDENTIFIER(c->syms[c->fn].name));
			} else if (cc_type_is_void(c, rt) && c->fn)	/* (clang -pedantic) */
				CC_REPORT_EXTENSION(c, "pedantic", NULL, loc, "void function '%.*s' should not return void expression",
					 CC_IDENTIFIER(c->syms[c->fn].name));
		} else if (c->fn && !cc_type_is_void(c, rt) && !c->o->implicit_int) {
			cc_report_flag_error(c, "return-mismatch", (const char *const[]){"return-type", NULL}, loc, NULL, 0, "non-void function '%.*s' should return a value",
			       CC_IDENTIFIER(c->syms[c->fn].name));
		}
		if (unlikely(e && AST_NODE_IS_ERROR(e)) && !cc_token_is(c, LEX_TOKEN_SEMICOLON))
			cc_recovery_skip_semicolon(c);	/* clang: after an invalid expression, silently */
		else
			cc_token_expect_semicolon(c, "after return statement");
		if (e && c->fn && !AST_NODE_IS_ERROR(e))
			cc_return_stack_address_check(c, e, false);
		if (c->fn && (c->syms[c->fn].flags & SF_NORETURN))
			cc_report_warning(c, "invalid-noreturn", true, NULL, loc, NULL, 0, NULL, 0, "function '%.*s' declared 'noreturn' should not return",
				 CC_IDENTIFIER(c->syms[c->fn].name));
		return cc_statement_new(c, N_RETURN, loc, e, 0, 0);
	}
	case KW_goto:
		next(c);
		if (cc_token_is(c, LEX_TOKEN_STAR)) {
			uint32_t e;

			CC_REPORT_EXTENSION_MICROSOFT(c, "*", "gnu-label-as-value", cc_warning_group_gnu, tok(c)->loc, "use of GNU indirect-goto extension");
			next(c);
			e = cc_conversion_decay(c, expr(c));

			cc_token_expect_semicolon(c, "after goto statement");
			return cc_statement_new(c, N_GOTO_IND, loc, e, 0, 0);
		}
		if (!cc_token_is(c, LEX_TOKEN_IDENTIFIER))
			cc_report_fatal(c, tok(c)->loc, "expected identifier");
		{
			uint32_t l = cc_label_reference(c, tok(c)->id, tok(c)->loc);

			next(c);
			cc_token_expect_semicolon(c, "after goto statement");
			if (c->uv_any & 1)
				c->uv_any |= 8;	/* (a jump past a later assignment: run the analysis) */
			return cc_statement_new(c, N_GOTO, loc, l, 0, 0);
		}
	case KW_asm: case KW___asm__: case KW___asm:
		return cc_asm_statement_parse(c, false);
	case KW___attribute__: case KW___attribute: {
		attrs a = { 0 };

		cc_attribute_list_parse(c, &a);
		if (cc_token_accept(c, LEX_TOKEN_SEMICOLON))
			return cc_statement_new(c, N_EXPR, loc, 0, 0, 0);	/* fallthrough etc. */
		return cc_statement_parse(c);
	}
	default:
		break;
	}
	if (cc_statement_keyword_typo(c, false))
		return cc_statement_parse(c);
	if (cc_token_is(c, LEX_TOKEN_IDENTIFIER) && !cc_token_keyword(tok(c)) && cc_token_peek(c, 1)->kind == LEX_TOKEN_IDENTIFIER && !cc_token_keyword(cc_token_peek(c, 1)) &&
	    !cc_symbol_lookup(c, tok(c)->id)) {
		/* "foo_t x;" with foo_t unknown: clang reports the identifier and skips the statement */
		cc_undeclared_report(c, "use of undeclared identifier", tok(c)->id, tok(c)->loc, false, 0, NULL);
		while (!cc_token_is(c, LEX_TOKEN_SEMICOLON) && !cc_token_is(c, LEX_TOKEN_RIGHT_BRACE) && !cc_token_is(c, LEX_TOKEN_EOF))
			next(c);
		cc_token_accept(c, LEX_TOKEN_SEMICOLON);
		return cc_statement_new(c, N_EXPR, loc, 0, 0, 0);
	}
	{
		int er = c->errors;
		uint32_t e = expr(c);

		if (likely(!AST_NODE_IS_ERROR(e) && c->errors == er)) {	/* (an invalid expression: reported already) */
			bool cmp = !c->in_stmtexpr && cc_unused_comparison_check(c, e);

			if (!c->in_stmtexpr)
				cc_unused_result_check(c, e, true);
			if (!sed)
				cc_unused_set_only_check(c, e);
			if (!sed && !cmp && !(c->in_stmtexpr && AST_NODE(e)->op == N_BINARY && cc_operator_is_comparison(AST_NODE(e)->aux) &&
					      !(AST_NODE(e)->flags & (NF_PAREN | NF_MACRO))))
				cc_unused_value_check(c, e, false);
		}
		if (unlikely(!cc_token_is(c, LEX_TOKEN_SEMICOLON)) && AST_NODE_IS_ERROR(e))
			cc_recovery_skip_semicolon(c);	/* clang: after an invalid expression, silently to the ';' */
		else
			cc_token_expect_semicolon(c, "after expression");
		return cc_statement_new(c, N_EXPR, loc, e, 0, 0);
	}
}
