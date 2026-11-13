/* vcc front end: the parser's token window over pp_token_next, and the diagnostics of a missing token */
#include "ccimpl.h"

uint8_t kwclass[KW_END];
uint16_t kwmap_gnu[KW_END], kwmap_c23[KW_END], kwmap_iso[KW_END];
_Thread_local const uint16_t *kwmap = kwmap_gnu;

VPRIV void init_keywords(void)
{
	static const uint16_t types[] = {
		KW_void, KW_char, KW_short, KW_int, KW_long, KW_float, KW_double, KW_signed, KW_unsigned,
		KW__Bool, KW__Complex, KW___complex__, KW___complex, KW_struct, KW_union, KW_enum, KW_const, KW_volatile, KW_restrict,
		KW__Atomic, KW_typeof, KW___typeof__, KW___typeof, KW___typeof_unqual__, KW___const__,
		KW___const, KW___signed__, KW___signed, KW___volatile__, KW___volatile, KW___restrict__,
		KW___restrict, KW___int128, KW__BitInt, KW__ExtInt, KW__Float16, KW___bf16, KW___fp16, KW___float128, KW__Alignas, KW___attribute__,
		KW___attribute, KW___auto_type,
	};
	static const uint16_t decls[] = {
		KW_typedef, KW_extern, KW_static, KW_auto, KW_register, KW_inline, KW___inline__, KW___inline,
		KW__Noreturn, KW__Thread_local, KW___thread, KW__Static_assert,
	};

	for (int i = KW_auto; i <= KW_thread_local; i++)
		kwclass[i] = KC_KW;
	/* C23 keywords that are plain identifiers in gnu11 */
	kwclass[KW_bool] = kwclass[KW_true] = kwclass[KW_false] = 0;
	kwclass[KW_nullptr] = kwclass[KW_alignof] = kwclass[KW_alignas] = 0;
	kwclass[KW_thread_local] = kwclass[KW_static_assert] = 0;
	kwclass[KW_typeof_unqual] = kwclass[KW_constexpr] = 0;
	for (size_t i = 0; i < sizeof(types) / sizeof(types[0]); i++)
		kwclass[types[i]] |= KC_TYPE | KC_DECL;
	for (size_t i = 0; i < sizeof(decls) / sizeof(decls[0]); i++)
		kwclass[decls[i]] |= KC_DECL;
	for (int i = 0; i < KW_END; i++)
		kwmap_gnu[i] = kwmap_c23[i] = (kwclass[i] & KC_KW) ? (uint16_t)i : 0;
	/* Legacy Clang spelling is the same type in every language mode. */
	kwmap_gnu[KW__ExtInt] = kwmap_c23[KW__ExtInt] = KW__BitInt;
	/* C23 keywords: the GNU keyword with the same meaning, or their own */
	kwmap_c23[KW_bool] = KW__Bool;
	kwmap_c23[KW_alignas] = KW__Alignas;
	kwmap_c23[KW_alignof] = KW__Alignof;
	kwmap_c23[KW_static_assert] = KW__Static_assert;
	kwmap_c23[KW_thread_local] = KW__Thread_local;
	kwmap_c23[KW_typeof_unqual] = KW___typeof_unqual__;
	kwmap_c23[KW_true] = KW_true;
	kwmap_c23[KW_false] = KW_false;
	kwmap_c23[KW_nullptr] = KW_nullptr;
	kwmap_c23[KW_constexpr] = KW_constexpr;
	kwclass[KW_constexpr] = KC_KW | KC_DECL;	/* (reached only through kwmap_c23) */
	kwclass[KW_true] = kwclass[KW_false] = kwclass[KW_nullptr] = KC_KW;
	/* -std=c89..c17: GNU's own spellings are identifiers (clang: typeof, asm) */
	memcpy(kwmap_iso, kwmap_gnu, sizeof(kwmap_iso));
	kwmap_iso[KW_typeof] = kwmap_iso[KW_asm] = 0;
}


VPRIV __attribute__((noinline)) void fetch_directive(cc *c, ptok *t)
{
	do {
		if (unlikely(dc_stale(c)))
			dc_materialize(c);	/* (the pragma acts on the real state) */
		pragma(c, t);
		if (unlikely(c->lz_on))
			lz_fetch(c, t);
		else
			pp_token_next(c->p, t);
	} while (t->kind == PP_TOKEN_KIND_DIRECTIVE_LINE);
}

/* a lazy body's next token (lazy.c), LEX_TOKEN_EOF after its '}' */
VPRIV __attribute__((noinline)) void lz_fetch(cc *c, ptok *t)
{
	const lztok *z;

	if (c->lz_hc && c->lzz.k <= c->lzz.close) {
		pp_lazy_body_token_get(c->p, &c->lzz, t);
		c->lze = t->loc;	/* (the LEX_TOKEN_EOF's location) */
		return;
	}
	if (c->lz_hc) {
		memset(t, 0, sizeof(*t));
		t->kind = LEX_TOKEN_EOF;
		t->s = "";
		t->loc = c->lze;
		return;
	}
	if (c->lzc >= c->lze) {
		memset(t, 0, sizeof(*t));
		t->kind = LEX_TOKEN_EOF;
		t->s = "";
		t->loc = c->lze ? c->lzt[c->lze - 1].loc : 0;
		return;
	}
	z = &c->lzt[c->lzc++];
	t->kind = z->kind;
	t->flags = z->flags;
	t->loc = z->loc;
	t->ctr = 0;
	t->len = z->len;
	if (z->kind == LEX_TOKEN_IDENTIFIER) {
		t->id = z->v;
		t->s = vintern_ents[z->v].s;
	} else {
		t->id = 0;
		t->s = c->lzs + z->v;
	}
}

void cc_fetch(cc *c, ptok *t) { fetch(c, t); }

VPRIV ptok *peek(cc *c, int k)
{
	/* tk[0..ntk) may point into pp's tmp arena: keep it while looking ahead */
	if (c->ntk <= k) {
		pp_temporary_hold(c->p, 1);
		while (c->ntk <= k)
			fetch(c, &c->tk[c->ntk++]);
		pp_temporary_hold(c->p, 0);
	}
	return &c->tk[k];
}

/* drop tk[0] with lookahead queued: fixed-size copies (a libc memmove costs more) */
VPRIV __attribute__((noinline)) void next_shift(cc *c)
{
	c->tk[0] = c->tk[1];
	if (c->ntk > 2) {
		c->tk[1] = c->tk[2];
		if (c->ntk > 3)
			c->tk[2] = c->tk[3];
	}
	c->ntk--;
}

VPRIV const char *tokspell(const ptok *t, char *buf, size_t cap)
{
	size_t n;
	char tmp[t->len + 1];
	const char *s;

	if (t->kind == LEX_TOKEN_EOF)
		return "end of file";
	s = pp_token_spell(t, tmp, &n);
	if (n >= cap)
		n = cap - 1;
	memcpy(buf, s, n);
	buf[n] = 0;
	return buf;
}

/* the failure path out of line: tokspell's buffer (a VLA) made every call set up a frame */
/* clang's ExpectAndConsume: a missing punctuator is reported at the end of the
   previous token, with the insertion as a fix-it (the closers of a delimiter pair
   are expect_match: at the token, as clang's BalancedDelimiterTracker) */
VPRIV __attribute__((noinline, cold, noreturn)) void expect_fail(cc *c, const char *what)
{
	size_t n = strlen(what);

	if (n >= 3 && n <= 5 && what[0] == '\'' && what[n - 1] == '\'' && c->prevloc && !(c->prevflags & PP_TOKEN_FLAG_FROM_MACRO)) {
		uint32_t e = pp_token_end_find(c->p, c->prevloc);
		char *s = halloc(c, n - 1);
		diag_fixit fx = { e, e, s, DIAG_APPLICABILITY_MAYBE };

		memcpy(s, what + 1, n - 2), s[n - 2] = 0;
		report(c, "error", e, NULL, 0, &fx, 1, "expected %s", what);
		parse_jump(c);
	}
	fatal(c, c->tk[0].loc, "expected %s", what);
	abort();
}

VPRIV __attribute__((noinline, cold, noreturn)) void expect_here_fail(cc *c, const char *what)
{
	report(c, "error", c->tk[0].loc, NULL, 0, NULL, 0, "expected %s", what);
	parse_jump(c);
}

/* clang's "expected '(' after 'if'": at the token, for the statements' conditions */
VPRIV __attribute__((noinline, cold, noreturn)) void lparen_fail(cc *c, const char *kw)
{
	report(c, "error", c->tk[0].loc, NULL, 0, NULL, 0, "expected '(' after '%s'", kw);
	parse_jump(c);
}

/* "expected ';' after X": reported at the end of the previous token with a
   fix-it, like clang; parsing goes on as if the ';' were there.  True when the ';'
   is missing and nothing stands in for it (clang's ExpectAndConsumeSemi failing) */
/* a ';' inserted after the previous token: machine-applicable at the end of a line (the next
   token starts one, and is no '{' that would begin a function's body), a guess otherwise
   ("struct s a b", "int fint x)": the mutation corpus, D6) */
VPRIV int semi_app(cc *c)
{
	return (c->tk[0].flags & LEX_TOKEN_FLAG_LINE_START) && c->tk[0].kind != LEX_TOKEN_LEFT_BRACE ? DIAG_APPLICABILITY_MACHINE : DIAG_APPLICABILITY_MAYBE;
}

VPRIV bool expect_semi(cc *c, const char *after)
{
	uint32_t e;
	diag_fixit fx;

	if (likely(c->tk[0].kind == LEX_TOKEN_SEMICOLON)) {
		next(c);
		return false;
	}
	if ((c->tk[0].kind == LEX_TOKEN_RIGHT_PAREN || c->tk[0].kind == LEX_TOKEN_RIGHT_SQUARE) && peek(c, 1)->kind == LEX_TOKEN_SEMICOLON) {
		/* clang: "f);": the stray ')' (or ']') and the ';' are consumed */
		diag_fixit fx = { c->tk[0].loc, c->tk[0].loc + 1, "", DIAG_APPLICABILITY_MAYBE };	/* (or a '(' was lost) */

		report(c, "error", c->tk[0].loc, NULL, 0, &fx, !(c->tk[0].flags & PP_TOKEN_FLAG_FROM_MACRO), "extraneous '%s' before ';'",
		       c->tk[0].kind == LEX_TOKEN_RIGHT_PAREN ? ")" : "]");
		next(c), next(c);
		return false;
	}
	if (c->for_head) {
		/* clang: at the token, no fix-it, on as if it were there */
		report(c, "error", c->tk[0].loc, NULL, 0, NULL, 0, "expected ';' in 'for' statement specifier");
		return false;
	}
	if (c->tk[0].kind == LEX_TOKEN_COMMA || c->tk[0].kind == LEX_TOKEN_COLON) {
		/* clang's common typo: ',' or ':' for ';', replaced; parsing goes on after it */
		e = c->tk[0].loc;
		fx = (diag_fixit){ e, pp_token_end_find(c->p, e), ";", DIAG_APPLICABILITY_MACHINE };
		report(c, "error", e, NULL, 0, &fx, 1, "expected ';' %s", after);
		next(c);
		return false;
	}
	e = pp_token_end_find(c->p, c->prevloc);
	fx = (diag_fixit){ e, e, ";", semi_app(c) };
	report(c, "error", e, NULL, 0, &fx, 1, "expected ';' %s", after);
	return true;
}

/* clang's isNotExpressionStart: after a ',' this ends the expression, the ',' being a
   typo for ';' (expect_semi reports it) */
VPRIV bool not_expr_start(cc *c, const ptok *t)
{
	int kw;

	if (t->kind == LEX_TOKEN_LEFT_BRACE || t->kind == LEX_TOKEN_RIGHT_BRACE)
		return true;
	if (t->kind != LEX_TOKEN_IDENTIFIER)
		return false;
	kw = kwof(t);
	if (kw == KW_for || kw == KW_while || kw == KW_if || kw == KW_else || kw == KW_goto)
		return true;
	if (kw == KW___extension__ || kw == KW___attribute__ || kw == KW___attribute)
		return false;
	return is_decl_start(c, t);
}

/* could t start a declarator after a ',' in a declaration (clang's MightBeDeclarator) */
static bool might_be_declarator(cc *c, const ptok *t)
{
	int kw;

	if (t->kind == LEX_TOKEN_STAR || t->kind == LEX_TOKEN_LEFT_PAREN || t->kind == LEX_TOKEN_LEFT_SQUARE)
		return true;
	if (t->kind != LEX_TOKEN_IDENTIFIER)
		return false;
	kw = kwof(t);
	if (kw == KW___attribute__ || kw == KW___attribute)
		return true;
	return !kw && !typedef_id(c, t->id);
}

/* clang's ParseDeclGroup: a ',' that ends a line, followed by something that cannot
   start a declarator, was meant as ';'.  Reported and consumed: the declaration ends */
VPRIV bool decl_comma_typo(cc *c)
{
	uint32_t l = c->tk[0].loc;
	const ptok *t;
	diag_fixit fx;

	if (c->tk[0].kind != LEX_TOKEN_COMMA)
		return false;
	t = peek(c, 1);
	if (!(t->flags & LEX_TOKEN_FLAG_LINE_START) || might_be_declarator(c, t))
		return false;
	fx = (diag_fixit){ l, pp_token_end_find(c->p, l), ";", DIAG_APPLICABILITY_MACHINE };
	report(c, "error", l, NULL, 0, &fx, 1, "expected ';' at end of declaration");
	next(c);
	return true;
}
