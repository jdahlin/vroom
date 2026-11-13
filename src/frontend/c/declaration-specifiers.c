/* vcc front end: declaration specifiers, tags and typeof */
#include <ctype.h>

#include "cc-internal.h"

/* const, volatile, restrict in any spelling */
static bool cc_token_kind_is_qualifier(int k)
{
	return k == KW_const || k == KW___const__ || k == KW___const || k == KW_volatile || k == KW___volatile__ ||
	       k == KW___volatile || k == KW_restrict || k == KW___restrict__ || k == KW___restrict;
}

/* an identifier where a type name must be: clang's "unknown type name" (with typo
   correction among the typedefs and the type keywords), then on as if it were the
   correction (or int); a keyword is parsed as written so (allow_sc: storage classes) */
CC_PRIVATE void cc_declaration_specifiers_unknown_type_report(cc *c, cc_declaration_specifiers *ds, bool allow_sc)
{
	bool ud = c->undecl_ty;
	uint32_t kw, loc = tok(c)->loc, s;

	c->undecl_ty = 0;
	s = cc_undeclared_report(c, ud ? "use of undeclared identifier" : "unknown type name", tok(c)->id, loc, true, ud ? 0 : CC_TYPO_KEYWORDS_TYPE,
		       ud ? NULL : &kw);
	if (ud)
		kw = 0;

	if (kw) {
		tok(c)->id = kw;
		if (cc_declaration_specifiers_parse(c, ds, allow_sc))
			return;
	}
	next(c);
	memset(ds, 0, sizeof(*ds));
	ds->ty = s ? c->syms[s].ty : cc_type_basic(TY_INT);
	ds->loc = loc;
	ds->has_type = true;
	ds->invalid = !s;
}

CC_PRIVATE bool cc_token_is_type_start(cc *c, const ptok *t)
{
	if (t->kind != LEX_TOKEN_IDENTIFIER)
		return false;
	if (t->id < KW_END && cc_keyword_map[t->id])
		return cc_keyword_class[cc_keyword_map[t->id]] & CC_KEYWORD_CLASS_STARTS_TYPE;
	return cc_symbol_is_typedef_identifier(c, t->id);
}

CC_PRIVATE bool cc_token_is_declaration_start(cc *c, const ptok *t)
{
	if (t->kind != LEX_TOKEN_IDENTIFIER)
		return false;
	if (t->id < KW_END && cc_keyword_map[t->id])
		return cc_keyword_class[cc_keyword_map[t->id]] & CC_KEYWORD_CLASS_STARTS_DECLARATION;
	return cc_symbol_is_typedef_identifier(c, t->id);
}

CC_PRIVATE uint32_t cc_tag_new(cc *c, int kind, uint32_t name, uint32_t loc)
{
	uint32_t ti = c->ntag++, t;
	tag *r = &c->tags[ti];

	memset(r, 0, sizeof(*r));
	r->name = name;
	r->loc = loc;
	t = cc_type_new(c, kind);
	c->ty[t].aux = ti;
	c->ty[t].align = 1;
	if (kind == TY_ENUM) {
		c->ty[t].base = cc_type_basic(TY_UINT);
		c->ty[t].size = 4;
		c->ty[t].align = 4;
		r->flags |= RF_ENUM;
	}
	if (kind == TY_UNION)
		r->flags |= RF_UNION;
	r->ty = t;
	return ti;
}

/* tag binding for "struct S" (define: in the current scope only) */
static uint32_t cc_tag_lookup_or_new(cc *c, int kind, uint32_t name, uint32_t loc, bool define, bool bare_decl)
{
	uint32_t s = name ? cc_tag_lookup(c, name) : 0;

	if (s) {
		sym *y = &c->syms[s];
		uint32_t ti = (uint32_t)y->val;
		int k = c->ty[c->tags[ti].ty].kind;

		if ((define || bare_decl) && y->depth != c->depth)
			s = 0;	/* a new tag in this scope shadows the outer one */
		else {
			if (k != kind)
				cc_report_error(c, loc, "use of '%.*s' with tag type that does not match previous declaration", CC_IDENTIFIER(name));
			return ti;
		}
	}
	{
		uint32_t ti = cc_tag_new(c, kind, name, loc);

		if (kind == TY_ENUM && !define)	/* (clang -pedantic) */
			CC_REPORT_EXTENSION(c, "pedantic", NULL, loc, "ISO C forbids forward references to 'enum' types");
		if (name && c->in_proto)
			cc_report_warning(c, "visibility", true, NULL, loc, NULL, 0, NULL, 0,
				 "declaration of '%s %.*s' will not be visible outside of this function",
				 kind == TY_STRUCT ? "struct" : kind == TY_UNION ? "union" : "enum", CC_IDENTIFIER(name));
		if (name) {
			uint32_t ns = cc_symbol_new(c, CC_SYMBOL_NAMESPACE_TAG, name, QT(c->tags[ti].ty, 0), loc);

			c->syms[ns].val = ti;
			cc_symbol_bind(c, ns);
			cc_scope_prototype_bindings_save(c, ns);
		}
		return ti;
	}
}

static qty cc_declaration_specifiers_integer_type(cc *c, int nlong, int sgn, int base, uint32_t loc)
{
	(void)c;
	switch (base) {
	case KW_char:
		return cc_type_basic(sgn == 0 ? TY_CHAR : sgn > 0 ? TY_SCHAR : TY_UCHAR);
	case KW_short:
		return cc_type_basic(sgn < 0 ? TY_USHORT : TY_SHORT);
	case KW___int128:
		return cc_type_basic(sgn < 0 ? TY_UINT128 : TY_INT128);
	default:
		if (nlong == 1)
			return cc_type_basic(sgn < 0 ? TY_ULONG : TY_LONG);
		if (nlong >= 2) {
			if (nlong > 2)
				cc_report_error(c, loc, "'long long long' is invalid");
			return cc_type_basic(sgn < 0 ? TY_ULLONG : TY_LLONG);
		}
		return cc_type_basic(sgn < 0 ? TY_UINT : TY_INT);
	}
}

/* A variably modified typeof expression is evaluated once. Fresh capture
   nodes read the original dimensions' saved values after that evaluation,
   even when the operand changes their bound variables. */
static qty cc_declaration_specifiers_typeof_evaluated(cc *c, qty ty, uint32_t *operand, uint32_t loc)
{
	ctype t = *AST_TYPE(c, ty);
	bool changed = false;

	if (t.kind != TY_PTR && t.kind != TY_ARRAY)
		return ty;
	if (t.kind == TY_ARRAY && (t.flags & TF_VLA) && t.n) {
		qty dimension = cc_type_vla(c, cc_type_basic(TY_CHAR), t.n);

		t.n = cc_node_new_operands(c, N_SIZEOF_VLA, c->t_size, loc, *operand, dimension);
		*operand = 0;
		changed = true;
	}
	qty base = cc_declaration_specifiers_typeof_evaluated(c, t.base, operand, loc);

	if (!changed && base == t.base)
		return ty;
	t.base = base;
	return QT(cc_type_intern(c, &t), QQ(ty));
}

static qty cc_declaration_specifiers_typeof_parse(cc *c, bool unqual)
{
	qty t;
	uint32_t open;

	next(c);
	uint32_t loc = tok(c)->loc;	/* clang: at the '(' */

	open = cc_token_expect_left_paren(c);
	if (cc_token_is_type_start(c, tok(c))) {
		t = cc_type_name_parse(c);
	} else {
		uint32_t n;

		c->unevaluated++;
		n = expr(c);
		c->unevaluated--;
		if (cc_node_bitfield(c, n))
			cc_report_error(c, loc, "invalid application of 'typeof' to bit-field");
		t = cc_declaration_specifiers_typeof_evaluated(c, AST_NODE(n)->ty, &n, loc);
	}
	cc_token_expect_match(c, LEX_TOKEN_RIGHT_PAREN, open);
	return unqual ? QUNQ(t) : t;
}

/* a token that may follow the name in a declarator (clang's
   isValidAfterIdentifierInDeclarator): "static x = 4" is implicit int, not a typo */
static bool cc_token_is_valid_after_declarator_identifier(const ptok *t)
{
	int kw = cc_token_keyword(t);

	switch (t->kind) {
	case LEX_TOKEN_LEFT_SQUARE: case LEX_TOKEN_LEFT_PAREN: case LEX_TOKEN_RIGHT_PAREN: case LEX_TOKEN_SEMICOLON: case LEX_TOKEN_COMMA: case LEX_TOKEN_EQUAL:
	case LEX_TOKEN_LEFT_BRACE: case LEX_TOKEN_COLON:
		return true;
	default:
		return kw == KW_asm || kw == KW___asm__ || kw == KW___asm;
	}
}

/* clang: signed/unsigned or long on a type that has none: "'void' cannot be signed or
   unsigned", "'long struct' is invalid"; returns CC_WIDTH_SIGN_DROP_SIGN / CC_WIDTH_SIGN_DROP_LONG for what to drop
   (on as the type without them).  No pointers: sgn and nlong stay in registers */
enum { CC_WIDTH_SIGN_DROP_SIGN = 1, CC_WIDTH_SIGN_DROP_LONG = 2 };
static __attribute__((noinline, cold)) int cc_declaration_specifiers_width_sign_report(cc *c, int sgn, int nlong, int kw, cc_declaration_specifiers *ds)
{
	int r = 0;

	if (sgn && kw != KW_int && kw != KW_char && kw != KW_short && kw != KW___int128) {
		cc_report_error(c, ds->sgnloc, "'%.*s' cannot be signed or unsigned", CC_IDENTIFIER(kw));
		r |= CC_WIDTH_SIGN_DROP_SIGN;
	}
	if (nlong && kw != KW_int && !(kw == KW_double && nlong == 1)) {
		cc_report_error(c, ds->longloc, "'%s%.*s' is invalid", nlong > 1 ? "long long " : "long ", CC_IDENTIFIER(kw));
		r |= CC_WIDTH_SIGN_DROP_LONG;
	}
	ds->invalid |= r != 0;	/* clang: an invalid type, the declaration is invalid */
	return r;
}

/* signed/unsigned or long (lng) after the base type specifier base: clang's errors;
   true if it is dropped */
static __attribute__((noinline, cold)) bool cc_declaration_specifiers_late_width_sign(cc *c, int base, int nlong, bool lng, uint32_t loc, cc_declaration_specifiers *ds)
{
	if (lng) {
		if (base == KW_int || (base == KW_double && !nlong))
			return false;
		if (base == KW_short)
			cc_report_error(c, loc, "cannot combine with previous 'short' declaration specifier");
		else
			cc_report_error(c, loc, "'long %.*s' is invalid", CC_IDENTIFIER(base)), ds->invalid = true;
		return true;
	}
	if (base == KW_int || base == KW_char || base == KW_short || base == KW___int128 || base == KW__BitInt)
		return false;
	cc_report_error(c, loc, "'%.*s' cannot be signed or unsigned", CC_IDENTIFIER(base));
	ds->invalid = true;
	return true;
}

/* long/short/signed/unsigned after a struct, union or enum specifier: clang's error,
   the word dropped, the declaration invalid */
static void cc_declaration_specifiers_tag_semicolon_recover(cc *c, int kind);

/* "struct S" ending its line and a type specifier starting the next: clang's
   missing ';' (a forward declaration), checked here, where vcc's own error would
   be, so that the many "struct S" of a valid TU pay nothing for it */
static bool cc_declaration_specifiers_tag_ends_line(cc *c, const cc_declaration_specifiers *ds)
{
	if (!ds->tag_bare || !(tok(c)->flags & LEX_TOKEN_FLAG_LINE_START) || c->in_tyname)
		return false;
	cc_declaration_specifiers_tag_semicolon_recover(c, AST_TYPE_KIND(c, QT(c->tags[ds->tagdecl].ty, 0)));
	return true;
}

static __attribute__((noinline, cold)) void cc_declaration_specifiers_tag_width_report(cc *c, cc_declaration_specifiers *ds, int kw)
{
	int k = AST_TYPE_KIND(c, QT(c->tags[ds->tagdecl].ty, 0));
	const char *tn = k == TY_ENUM ? "enum" : k == TY_UNION ? "union" : "struct";

	if (cc_declaration_specifiers_tag_ends_line(c, ds))
		return;
	if (kw == KW_long || kw == KW_short)
		cc_report_error(c, tok(c)->loc, "'%.*s %s' is invalid", CC_IDENTIFIER(kw), tn);
	else
		cc_report_error(c, tok(c)->loc, "'%s' cannot be signed or unsigned", tn);
	ds->invalid = true;
	next(c);
}

/* a second base type specifier kw after base (or short after long): clang's errors;
   true to go on without kw ("short int" too, which is fine) */
static __attribute__((noinline)) bool cc_declaration_specifiers_base_clash(cc *c, int base, int kw, int nlong, uint32_t baseloc, uint32_t loc)
{
	if (kw == KW_short && nlong) {
		cc_report_error(c, loc, "cannot combine with previous 'long' declaration specifier");
		return true;
	}
	if (base == KW_short && kw != KW_int && kw != KW_short) {
		/* clang: short is a width, "short double" one invalid type */
		cc_report_error(c, baseloc, "'short %.*s' is invalid", CC_IDENTIFIER(kw));
		return false;
	}
	if (kw == KW_short && base != KW_int) {
		cc_report_error(c, loc, "'short %.*s' is invalid", CC_IDENTIFIER(base));
		return true;
	}
	if (!(base == KW_int && kw == KW_short) && !(kw == KW_int && base == KW_short) && !(base == KW_double && kw == KW_double)) {
		cc_report_error(c, loc, "cannot combine with previous '%.*s' declaration specifier", CC_IDENTIFIER(base));
		return true;	/* clang: on with the first */
	}
	return kw == KW_int;
}

/* clang's isValidAfterTypeSpecifier (C): what may follow "struct S { ... }" */
static __attribute__((noinline, cold)) bool cc_declaration_specifiers_is_valid_after_tag(cc *c)
{
	const ptok *t = tok(c);
	int kw;

	switch (t->kind) {
	case LEX_TOKEN_SEMICOLON: case LEX_TOKEN_STAR: case LEX_TOKEN_AMPERSAND: case LEX_TOKEN_AMPERSAND_AMPERSAND: case LEX_TOKEN_RIGHT_PAREN: case LEX_TOKEN_LEFT_PAREN:
	case LEX_TOKEN_COMMA: case LEX_TOKEN_LEFT_SQUARE: case LEX_TOKEN_ELLIPSIS: case LEX_TOKEN_RIGHT_BRACE:
		return true;
	case LEX_TOKEN_COLON:
		return c->in_struct;
	case LEX_TOKEN_IDENTIFIER:
		break;
	default:
		return false;
	}
	kw = cc_token_keyword(t);
	switch (kw) {
	case 0: case KW___attribute__: case KW___attribute:
		return true;
	case KW_const: case KW___const__: case KW___const: case KW_volatile: case KW___volatile__: case KW___volatile:
	case KW_restrict: case KW___restrict__: case KW___restrict: case KW__Atomic: case KW_inline: case KW___inline__:
	case KW___inline: case KW_static: case KW_extern: case KW_typedef: case KW_register: case KW_auto:
	case KW__Thread_local: case KW_constexpr: {
		/* likely the next declaration's, unless a type specifier follows (isKnownToBeTypeSpecifier) */
		const ptok *n = cc_token_peek(c, 1);
		int k = n->kind == LEX_TOKEN_IDENTIFIER ? cc_token_keyword(n) : 0;

		return !(k && (cc_keyword_class[k] & CC_KEYWORD_CLASS_STARTS_TYPE) && !(k == KW_const || k == KW___const__ || k == KW___const ||
			 k == KW_volatile || k == KW___volatile__ || k == KW___volatile || k == KW_restrict ||
			 k == KW___restrict__ || k == KW___restrict || k == KW__Atomic || k == KW__Alignas ||
			 k == KW___attribute__ || k == KW___attribute));
	}
	default:
		return false;
	}
}

/* clang: "struct S { ... }" (or "struct S" at the end of a line) followed by what
   cannot follow a type specifier is a missing ';': reported at the end of the
   previous token, with the insertion, and parsing goes on as if it were there */
static __attribute__((noinline, cold)) void cc_declaration_specifiers_tag_semicolon_recover(cc *c, int kind)
{
	uint32_t e = c->prevflags & PP_TOKEN_FLAG_FROM_MACRO ? tok(c)->loc : pp_token_end_find(c->p, c->prevloc);
	diag_fixit fx = { e, e, ";", DIAG_APPLICABILITY_MAYBE };	/* (a guess: often two names in a row) */
	ptok *t = c->tk;

	report(c, "error", e, NULL, 0, &fx, !(c->prevflags & PP_TOKEN_FLAG_FROM_MACRO), "expected ';' after %s",
	       kind == TY_ENUM ? "enum" : kind == TY_UNION ? "union" : "struct");
	cc_token_peek(c, 0);
	if (c->ntk >= 4)
		return;
	if (c->ntk < 1)
		c->ntk = 1;
	memmove(t + 1, t, c->ntk * sizeof(*t));
	c->ntk++;
	t[0] = (ptok){ .s = ";", .len = 1, .loc = e, .kind = LEX_TOKEN_SEMICOLON };
}

/* after a struct, union or enum body: clang's missing ';' when what follows cannot
   follow a type specifier (out of cc_declaration_specifiers_parse' loop, which it would cost registers) */
static __attribute__((noinline)) void cc_declaration_specifiers_tag_body_end(cc *c, int kind)
{
	if (likely(cc_token_is(c, LEX_TOKEN_SEMICOLON)) || c->in_tyname || cc_declaration_specifiers_is_valid_after_tag(c))
		return;
	cc_declaration_specifiers_tag_semicolon_recover(c, kind);
}

/* "int struct S x", "struct S int x": clang's error at the second one, which is
   skipped (a tag's name and body too); the declaration goes on with the first */
static __attribute__((noinline, cold)) void cc_declaration_specifiers_clash_report(cc *c, const cc_declaration_specifiers *ds, int base)
{
	const ptok *t = tok(c);
	int kw = cc_token_keyword(t);

	if (!base && cc_declaration_specifiers_tag_ends_line(c, ds))
		return;
	if (base)
		cc_report_error(c, t->loc, "cannot combine with previous '%.*s' declaration specifier", CC_IDENTIFIER(base));
	else
		cc_report_error(c, t->loc, "cannot combine with previous '%s' declaration specifier",
			 AST_TYPE_KIND(c, QT(c->tags[ds->tagdecl].ty, 0)) == TY_ENUM ? "enum" :
			 AST_TYPE_KIND(c, QT(c->tags[ds->tagdecl].ty, 0)) == TY_UNION ? "union" : "struct");
	next(c);
	if (kw == KW_struct || kw == KW_union || kw == KW_enum) {
		attrs a = { 0 };

		cc_attribute_list_parse(c, &a);
		if (cc_token_is(c, LEX_TOKEN_IDENTIFIER) && !cc_token_keyword(tok(c)))
			next(c);
		if (cc_token_is(c, LEX_TOKEN_LEFT_BRACE))
			cc_token_skip_balanced(c);
	}
}

/* clang's ParseImplicitInt: "static in f(void)", "const unsigne long *p": after
   specifiers without a type, an unknown name where only a type fits is an unknown
   type name.  0: not one; CC_DECLARATION_SPECIFIERS_KEYWORD_ONLY: a keyword typo, rewritten; else the type */
#define CC_DECLARATION_SPECIFIERS_KEYWORD_ONLY ((qty)~0u)
static __attribute__((noinline, cold)) qty cc_declaration_specifiers_implicit_int_name(cc *c, cc_declaration_specifiers *ds)
{
	ptok *t;
	uint32_t kwc, s;

	if (cc_token_keyword(tok(c)) || cc_token_is_valid_after_declarator_identifier(cc_token_peek(c, 1)))
		return 0;
	t = tok(c);
	s = cc_undeclared_report(c, "unknown type name", t->id, t->loc, true, CC_TYPO_KEYWORDS_TYPE, &kwc);
	if (kwc) {
		t->id = kwc;	/* on as the keyword */
		return CC_DECLARATION_SPECIFIERS_KEYWORD_ONLY;
	}
	next(c);
	ds->invalid = !s;
	return s ? c->syms[s].ty : cc_type_basic(TY_INT);
}

/* no type specifier: clang reports it at the declarator's name ("static *p", "volatile (v)") */
static __attribute__((noinline, cold)) void cc_declaration_specifiers_implicit_int_error(cc *c)
{
	uint32_t i = 0, at = tok(c)->loc;

	while (i < 16 && (cc_token_peek(c, i)->kind == LEX_TOKEN_STAR || cc_token_peek(c, i)->kind == LEX_TOKEN_LEFT_PAREN || cc_token_kind_is_qualifier(cc_token_keyword(cc_token_peek(c, i)))))
		i++;
	if (cc_token_peek(c, i)->kind == LEX_TOKEN_IDENTIFIER && !cc_token_keyword(cc_token_peek(c, i)))
		at = cc_token_peek(c, i)->loc;
	if (c->o->std == 23)
		cc_report_error(c, at, "a type specifier is required for all declarations");
	else
		cc_report_error(c, at, "type specifier missing, defaults to 'int'; ISO C99 and later do not support implicit int [-Wimplicit-int]");
}

CC_PRIVATE bool cc_declaration_specifiers_parse(cc *c, cc_declaration_specifiers *ds, bool allow_sc)
{
	int nlong = 0, sgn = 0, base = 0, q = 0;
	unsigned bitwidth = 0;
	bool any = false, cplx = false, c23auto = false;
	qty named = 0;
	bool have_named = false;
	uint32_t atomic = 0;	/* the _Atomic qualifier's location */

	memset(ds, 0, sizeof(*ds));
	ds->loc = tok(c)->loc;
	if (c->has_pend) {
		ds->at = c->pend;
		c->has_pend = false;
	}
again:
	for (;;) {
		ptok *t = tok(c);
		int kw = cc_token_keyword(t);

		if (cc_token_at_standard_attribute(c)) {
			cc_attribute_list_parse(c, &ds->at);
			continue;
		}
		if (!kw) {
			if (cc_token_is_nullability(c, t)) {
				next(c);
				continue;
			}
			if (!have_named && !base && !nlong && !sgn && t->kind == LEX_TOKEN_IDENTIFIER && cc_symbol_is_typedef_identifier(c, t->id)) {
				uint32_t ts = cc_symbol_lookup(c, t->id);

				if (unlikely(c->syms[ts].flags & SF_DEPRECATED))
					cc_deprecated_use_report(c, t->loc, t->id, &c->attr[c->syms[ts].attrs], c->syms[ts].loc);
				named = c->syms[ts].ty;
				ds->tdef = ts;
				c->syms[ts].flags |= SF_REF;	/* (-Wunused-local-typedef) */
				have_named = true;
				any = true;
				next(c);
				continue;
			}
			break;
		}
		switch (kw) {
		case KW_typedef: case KW_extern: case KW_static: case KW_auto: case KW_register: {
			int sc = kw == KW_typedef ? SC_TYPEDEF : kw == KW_extern ? SC_EXTERN : kw == KW_static ? SC_STATIC :
				 kw == KW_auto ? SC_AUTO : SC_REGISTER;

			if (!allow_sc)
				cc_report_error(c, t->loc, "invalid storage class specifier");
			if (kw == KW_auto && c->o->std == 23) {	/* C23: maybe type inference (decided at the end) */
				c23auto = true;
				break;
			}
			if (ds->sc && ds->sc != sc) {
				cc_report_error(c, t->loc, "cannot combine with previous '%.*s' declaration specifier", CC_IDENTIFIER(ds->sckw));
				break;
			}
			ds->sc = sc;
			ds->sckw = (uint16_t)kw;
			break;
		}
		case KW_inline: case KW___inline__: case KW___inline:
			ds->inline_ = true;
			break;
		case KW__Noreturn:
			ds->noreturn = true;
			break;
		case KW__Thread_local: case KW___thread:
			ds->thread = true;
			break;
		case KW_constexpr:
			ds->constexpr_ = true;
			if (!allow_sc)
				cc_report_error(c, t->loc, "type name does not allow constexpr");
			break;
		case KW_const: case KW___const__: case KW___const:
			q |= Q_CONST;
			break;
		case KW_volatile: case KW___volatile__: case KW___volatile:
			q |= Q_VOLATILE;
			break;
		case KW_restrict: case KW___restrict__: case KW___restrict:
			q |= Q_RESTRICT;
			break;
		case KW__Atomic: {
			uint32_t aloc = t->loc;

			next(c);
			if (cc_token_is(c, LEX_TOKEN_LEFT_PAREN)) {
				uint32_t open = tok(c)->loc;

				next(c);
				qty operand = cc_type_name_parse(c);

				if (AST_TYPE(c, operand)->flags & TF_ATOMIC) {
					cc_report_error(c, aloc, "_Atomic cannot be applied to atomic type '%s'", tyname(c, operand));
					ds->invalid = true;
				} else if (QQ(operand)) {
					cc_report_error(c, aloc, "_Atomic cannot be applied to qualified type '%s'", tyname(c, operand));
					ds->invalid = true;
				}
				int errors = c->errors;
				named = cc_type_atomic(c, operand, aloc);
				ds->invalid |= c->errors != errors;
				have_named = true;
				cc_token_expect_match(c, LEX_TOKEN_RIGHT_PAREN, open);
				any = true;
			} else {
				atomic = aloc ? aloc : 1;	/* the qualifier: applied to the type below */
			}
			continue;
		}
		case KW___attribute__: case KW___attribute:
			cc_attribute_list_parse(c, &ds->at);
			continue;
		case KW___extension__:
			break;
		case KW__Alignas: {
			uint32_t a, open, aloc = t->loc;

			next(c);
			open = cc_token_expect_left_paren(c);
			if (cc_token_is_type_start(c, tok(c))) {
				a = cc_type_alignment(c, cc_type_name_parse(c));
			} else {
				i128 v;
				uint32_t vn = 0;

				cc_constant_expression_parse(c, &v, &vn);
				if (v < 0 || (v & (v - 1))) {	/* 0: no effect (C11) */
					diag_span r = cc_node_range(c, vn);

					report(c, "error", aloc, &r, vn ? 1 : 0, NULL, 0, "requested alignment is not a power of 2");
				}
				a = v < 0 || (v & (v - 1)) ? 0 : (uint32_t)v;
			}
			cc_token_expect_match(c, LEX_TOKEN_RIGHT_PAREN, open);
			if (a > ds->align)
				ds->align = a;
			continue;
		}
		case KW__BitInt: {
			i128 width = 0;
			uint32_t loc = t->loc, open;

			if (t->id == KW__ExtInt)
				cc_report_warning(c, "deprecated-type", true, NULL, loc, NULL, 0, NULL, 0,
					 "'_ExtInt' is deprecated; use '_BitInt' instead");
			if (base || have_named || nlong)
				cc_report_error(c, loc, "invalid combination of type specifiers with _BitInt");
			base = KW__BitInt;
			any = true;
			next(c);
			open = cc_token_expect_left_paren(c);
			cc_constant_expression_parse(c, &width, NULL);
			cc_token_expect_match(c, LEX_TOKEN_RIGHT_PAREN, open);
			if (width < 1 || width > 8388608) {
				cc_report_error(c, loc, "_BitInt width must be between 1 and 8388608");
				width = 2;
			}
			bitwidth = (unsigned)width;
			continue;
		}
		case KW_void: case KW__Bool: case KW_char: case KW_short: case KW_int: case KW_float:
		case KW_double: case KW___int128: case KW__Float16: case KW___bf16: case KW___fp16: case KW___float128:
			if (have_named) {
				if (unlikely(ds->tagdecl) && !base) {
					cc_declaration_specifiers_clash_report(c, ds, 0);
					continue;
				}
				goto done;
			}
			if (unlikely(base || (kw == KW_short && nlong)) && cc_declaration_specifiers_base_clash(c, base, kw, nlong, ds->baseloc, t->loc))
				break;
			if (unlikely((sgn | nlong) && kw != KW_int)) {
				int w = cc_declaration_specifiers_width_sign_report(c, sgn, nlong, kw, ds);

				sgn = w & CC_WIDTH_SIGN_DROP_SIGN ? 0 : sgn;
				nlong = w & CC_WIDTH_SIGN_DROP_LONG ? 0 : nlong;
			}
			if (kw == KW_short)
				ds->baseloc = t->loc;
			base = kw;
			any = true;
			break;
		case KW_long:
			if (have_named) {
				if (unlikely(ds->tagdecl) && !base) {
					cc_declaration_specifiers_tag_width_report(c, ds, kw);
					continue;
				}
				goto done;
			}
			if (unlikely(base) && cc_declaration_specifiers_late_width_sign(c, base, nlong, true, t->loc, ds))
				break;
			if (!nlong++)
				ds->longloc = t->loc;
			any = true;
			break;
		case KW_signed: case KW___signed__: case KW___signed:
			if (have_named) {
				if (unlikely(ds->tagdecl) && !base) {
					cc_declaration_specifiers_tag_width_report(c, ds, kw);
					continue;
				}
				goto done;
			}
			if (unlikely(base) && cc_declaration_specifiers_late_width_sign(c, base, nlong, false, t->loc, ds))
				break;
			ds->sgnloc = t->loc;
			sgn = 1;
			any = true;
			break;
		case KW_unsigned:
			if (have_named) {
				if (unlikely(ds->tagdecl) && !base) {
					cc_declaration_specifiers_tag_width_report(c, ds, kw);
					continue;
				}
				goto done;
			}
			if (unlikely(base) && cc_declaration_specifiers_late_width_sign(c, base, nlong, false, t->loc, ds))
				break;
			ds->sgnloc = t->loc;
			sgn = -1;
			any = true;
			break;
		case KW__Complex: case KW___complex__: case KW___complex:
			cplx = true;
			any = true;
			break;
		case KW___auto_type:
			ds->auto_type = true;
			any = true;
			have_named = true;
			named = cc_type_basic(TY_INT);
			break;
		case KW_struct: case KW_union: case KW_enum: {
			int kind = kw == KW_struct ? TY_STRUCT : kw == KW_union ? TY_UNION : TY_ENUM;
			uint32_t loc = t->loc, name = 0, ti;
			attrs pre = { 0 }, post = { 0 };
			bool body;

			if (unlikely(base) && !have_named) {
				cc_declaration_specifiers_clash_report(c, ds, base);
				continue;
			}
			if (have_named || base)
				goto done;
			if (unlikely(sgn | nlong)) {
				cc_declaration_specifiers_width_sign_report(c, sgn, nlong, kw, ds);
				sgn = nlong = 0;
			}
			next(c);
			cc_attribute_list_parse(c, &pre);
			if (cc_token_is(c, LEX_TOKEN_IDENTIFIER) && !cc_token_keyword(tok(c))) {
				name = tok(c)->id;
				loc = tok(c)->loc;
				next(c);
			}
			body = cc_token_is(c, LEX_TOKEN_LEFT_BRACE) || (kind == TY_ENUM && cc_token_is(c, LEX_TOKEN_COLON));
			ti = cc_tag_lookup_or_new(c, kind, name, loc, body, !body && cc_token_is(c, LEX_TOKEN_SEMICOLON) && !c->in_struct);
			if (!body && c->tags[ti].depr)
				cc_deprecated_use_report(c, loc, name, &c->attr[c->tags[ti].depr], c->tags[ti].loc);
			if (body) {
				tag *r = &c->tags[ti];

				cc_tag_deprecate(c, ti, &pre);
				if ((r->flags & RF_COMPLETE) && kind != TY_ENUM) {
					cc_report_error(c, loc, "redefinition of '%.*s'", CC_IDENTIFIER(name));
					cc_report_note(c, r->loc, "previous definition is here");
				}
				if (kind == TY_ENUM) {
					cc_attribute_merge(&post, &pre);
					cc_enum_body_parse(c, ti, &post, loc);
					cc_declaration_specifiers_tag_body_end(c, kind);
				} else {
					CC_REPLAY_TAG_WRITE_LOG(c, ti);
					c->tags[ti].flags |= (pre.packed ? RF_PACKED : 0) | ((pre.ms_struct || c->ms_struct) ? RF_MS_STRUCT : 0);
					c->tags[ti].maxalign = pre.aligned;
					cc_record_body_parse(c, ti);
					if (kind == TY_UNION && pre.transparent)
						c->ty[c->tags[ti].ty].flags |= TF_TRANSPARENT;
					cc_declaration_specifiers_tag_body_end(c, kind);
					if (unlikely(cc_extension_is_enabled(c, "gnu-empty-struct", cc_warning_group_gnu))) {
						const tag *r = &c->tags[ti];
						bool named = false;

						for (uint32_t i = 0; i < r->nfield && !named; i++)
							named = r->f[i].name || (!r->f[i].bitfield && cc_type_is_record(c, r->f[i].ty));
						if (!named)
							cc_report_extension(c, "gnu-empty-struct", cc_warning_group_gnu, loc, r->nfield ? "%s without named members is a GNU extension"
								 : "empty %s is a GNU extension", kind == TY_UNION ? "union" : "struct");
					}
				}
			} else {
				ds->tag_bare = true;
			}
			if (kind == TY_ENUM)
				c->tags[ti].flags |= RF_ENUM_DECLARED;
			ds->tagdecl = ti;
			named = QT(c->tags[ti].ty, 0);
			have_named = true;
			any = true;
			continue;
		}
		case KW_typeof: case KW___typeof__: case KW___typeof: case KW___typeof_unqual__:
			if (have_named || base)
				goto done;
			if (kw == KW_typeof && c->o->std != 23)	/* (clang -pedantic: the GNU spelling) */
				CC_REPORT_EXTENSION_MICROSOFT(c, "typeof", "language-extension-token", cc_warning_group_pedantic, t->loc, "extension used");
			named = cc_declaration_specifiers_typeof_parse(c, kw == KW___typeof_unqual__);
			have_named = true;
			any = true;
			continue;
		default:
			goto done;
		}
		next(c);
	}
done:
	/* any: also a base, width, sign, _Complex or named type */
	if (unlikely(!any) && cc_token_is(c, LEX_TOKEN_IDENTIFIER) && (q || atomic || ds->sc || ds->inline_ || ds->noreturn || ds->thread)) {
		qty it = cc_declaration_specifiers_implicit_int_name(c, ds);

		if (it) {
			if (it != CC_DECLARATION_SPECIFIERS_KEYWORD_ONLY)
				named = it, have_named = any = true;
			goto again;
		}
	}
	ds->has_type = any;
	if (!any && !q && !atomic && !ds->sc && !c23auto && !ds->inline_ && !ds->noreturn && !ds->thread && !ds->align)
		return false;
	if (have_named) {
		ds->ty = named;
		if (ds->tdef && c->syms[ds->tdef].ty != named)
			ds->tdef = 0;
	} else if (base == KW__BitInt) {
		if (bitwidth == 1 && sgn >= 0) {
			cc_report_error(c, ds->loc, "signed _BitInt must have a bit size of at least 2");
			bitwidth = 2;
		}
		ds->ty = cc_type_bitint(c, bitwidth, sgn < 0);
	} else if (base == KW_void) {
		ds->ty = cc_type_basic(TY_VOID);
	} else if (base == KW__Bool) {
		ds->ty = cc_type_basic(TY_BOOL);
	} else if (base == KW_float) {
		ds->ty = cc_type_basic(TY_FLOAT);
	} else if (base == KW___bf16) {
		ds->ty = cc_type_basic(TY_BFLOAT16);
	} else if (base == KW__Float16 || base == KW___fp16) {
		ds->ty = cc_type_basic(TY_FLOAT16);
	} else if (base == KW_double) {
		ds->ty = cc_type_basic(nlong ? TY_LDOUBLE : TY_DOUBLE);
	} else if (base == KW___float128) {
		if (c->o->target != PP_LINUX_X86_64)
			cc_report_error(c, ds->loc, "__float128 is not supported on this target");
		ds->ty = cc_type_basic(c->o->target == PP_LINUX_X86_64 ? TY_FLOAT128 : TY_LDOUBLE);
	} else {
		if (unlikely(!any)) {
			if (c23auto && !nlong && !sgn && !cplx) {
				/* C23: 'auto' without a type specifier infers the type, as __auto_type */
				ds->auto_type = true;
			} else if (!c->o->implicit_int && !pp_location_is_in_system_header(c->p, ds->loc))
				cc_declaration_specifiers_implicit_int_error(c);
		}
		ds->ty = cc_declaration_specifiers_integer_type(c, nlong, sgn, base, ds->loc);
	}
	if (unlikely(c23auto) && !ds->auto_type && !ds->sc)	/* C23 "auto int x": the storage class */
		ds->sc = SC_AUTO, ds->sckw = KW_auto;
	if (cplx) {
		if (!base && !nlong && !sgn && !have_named)
			ds->ty = cc_type_basic(TY_DOUBLE);	/* plain _Complex: double (GNU) */
		if (cc_type_is_integer(c, ds->ty) && AST_TYPE(c, ds->ty)->n)
			cc_report_error(c, ds->loc, "'_Complex _BitInt' is invalid");
		else if (AST_TYPE_KIND(c, ds->ty) == TY_BFLOAT16)
			cc_report_error(c, ds->loc, "_Complex __bf16 is invalid");
		else if (!cc_type_is_arithmetic(c, ds->ty))
			cc_report_error(c, ds->loc, "_Complex requires an arithmetic element type");
		else
			ds->ty = cc_type_complex(c, ds->ty);
	}
	if (ds->at.mode) {
		int k = cc_attribute_mode_kind(c, ds->at.mode, cc_type_is_signed(c, ds->ty));

		if (k)
			ds->ty = cc_type_basic(k);
	}
	if (atomic)
		ds->ty = cc_type_atomic(c, ds->ty, atomic);
	ds->ty = cc_type_qualify(c, ds->ty, q);
	if (ds->inline_ && c->o->gnu89_inline)
		ds->at.gnu_inline = 1;	/* -std=gnu89: gnu_inline semantics */
	return true;
}
