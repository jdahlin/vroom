/* vcc front end: declaration specifiers */
#include <ctype.h>

#include "ccimpl.h"

/* const, volatile, restrict in any spelling */
static bool is_cvr(int k)
{
	return k == KW_const || k == KW___const__ || k == KW___const || k == KW_volatile || k == KW___volatile__ ||
	       k == KW___volatile || k == KW_restrict || k == KW___restrict__ || k == KW___restrict;
}

/* an identifier where a type name must be: clang's "unknown type name" (with typo
   correction among the typedefs and the type keywords), then on as if it were the
   correction (or int); a keyword is parsed as written so (allow_sc: storage classes) */
VPRIV void unknown_type(cc *c, dspec *ds, bool allow_sc)
{
	bool ud = c->undecl_ty;
	uint32_t kw, loc = tok(c)->loc, s;

	c->undecl_ty = 0;
	s = undeclared(c, ud ? "use of undeclared identifier" : "unknown type name", tok(c)->id, loc, true, ud ? 0 : KWS_TYPE,
		       ud ? NULL : &kw);
	if (ud)
		kw = 0;

	if (kw) {
		tok(c)->id = kw;
		if (decl_specs(c, ds, allow_sc))
			return;
	}
	next(c);
	memset(ds, 0, sizeof(*ds));
	ds->ty = s ? c->syms[s].ty : prim(TY_INT);
	ds->loc = loc;
	ds->has_type = true;
	ds->invalid = !s;
}

VPRIV bool is_type_start(cc *c, const ptok *t)
{
	if (t->kind != T_IDENT)
		return false;
	if (t->id < KW_END && kwmap[t->id])
		return kwclass[kwmap[t->id]] & KC_TYPE;
	return typedef_id(c, t->id);
}

VPRIV bool is_decl_start(cc *c, const ptok *t)
{
	if (t->kind != T_IDENT)
		return false;
	if (t->id < KW_END && kwmap[t->id])
		return kwclass[kwmap[t->id]] & KC_DECL;
	return typedef_id(c, t->id);
}

VPRIV uint32_t tag_new(cc *c, int kind, uint32_t name, uint32_t loc)
{
	uint32_t ti = c->ntag++, t;
	tag *r = &c->tags[ti];

	memset(r, 0, sizeof(*r));
	r->name = name;
	r->loc = loc;
	t = ty_new(c, kind);
	c->ty[t].aux = ti;
	c->ty[t].align = 1;
	if (kind == TY_ENUM) {
		c->ty[t].base = prim(TY_UINT);
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
static uint32_t tag_lookup_or_new(cc *c, int kind, uint32_t name, uint32_t loc, bool define, bool bare_decl)
{
	uint32_t s = name ? lookup_tag(c, name) : 0;

	if (s) {
		sym *y = &c->syms[s];
		uint32_t ti = (uint32_t)y->val;
		int k = c->ty[c->tags[ti].ty].kind;

		if ((define || bare_decl) && y->depth != c->depth)
			s = 0;	/* a new tag in this scope shadows the outer one */
		else {
			if (k != kind)
				cc_error(c, loc, "use of '%.*s' with tag type that does not match previous declaration", IDF(name));
			return ti;
		}
	}
	{
		uint32_t ti = tag_new(c, kind, name, loc);

		if (kind == TY_ENUM && !define)	/* (clang -pedantic) */
			EXT_WARN(c, "pedantic", NULL, loc, "ISO C forbids forward references to 'enum' types");
		if (name) {
			uint32_t ns = sym_new(c, S_TAG, name, QT(c->tags[ti].ty, 0), loc);

			c->syms[ns].val = ti;
			sym_bind(c, ns);
		}
		return ti;
	}
}

/* the layout of record t as a note at its definition (D5.3): its size, where the
   padding is, and the size in alignment order when that is smaller */
static __attribute__((cold)) void layout_note(cc *c, qty t)
{
	const tag *g = &c->tags[T(c, t)->aux];
	char b[96];
	int npad = 0;
	uint64_t end = 0, sorted = 0;
	bool flex = false, bits = false;
	vdiag *d;

	if (!g->nfield || !complete(c, t))
		return;
	d = pp_diag_begin(c->p, VDL_NOTE, g->loc, "'%s' is %llu bytes (alignment %u)", tyname(c, t),
			  (unsigned long long)g->size, g->align);
	d->id = "vcc_record_layout";
	for (uint32_t i = 0; i < g->nfield; i++) {
		const field *f = &g->f[i];
		uint64_t sz = f->bitfield ? 0 : (is_array(c, f->ty) && !complete(c, f->ty) ? 0 : ty_size(c, f->ty));

		bits |= f->bitfield;
		flex |= !f->bitfield && is_array(c, f->ty) && !complete(c, f->ty);
		if (!(g->flags & RF_UNION) && !f->bitfield && f->off / 8 > end && i) {
			const field *pf = &g->f[i - 1];

			npad++;
			snprintf(b, sizeof b, "%llu bytes of padding after this", (unsigned long long)(f->off / 8 - end));
			pp_diag_label(c->p, d, pf->loc, pp_tok_end(c->p, pf->loc), b);
		}
		if (f->bitfield) {
			uint64_t e = (f->off + f->width + 7) / 8;

			if (e > end)
				end = e;
		} else if (f->off / 8 + sz > end)
			end = f->off / 8 + sz;
	}
	if (!(g->flags & RF_UNION) && g->size > end) {
		npad++;
		snprintf(b, sizeof b, "%llu bytes of padding at the end", (unsigned long long)(g->size - end));
		pp_diag_label(c->p, d, g->f[g->nfield - 1].loc, pp_tok_end(c->p, g->f[g->nfield - 1].loc), b);
	}
	/* the members in decreasing alignment (no bit-fields, no flexible array) */
	if (npad && !bits && !flex && !(g->flags & RF_UNION) && !g->maxalign) {
		uint32_t al[64], n = g->nfield < 64 ? g->nfield : 0;

		for (uint32_t i = 0; i < n; i++)
			al[i] = ty_align(c, g->f[i].ty);
		for (uint32_t a = 64; a && n; a /= 2)
			for (uint32_t i = 0; i < n; i++)
				if (al[i] == a)
					sorted = (sorted + a - 1) / a * a + ty_size(c, g->f[i].ty);
		sorted = (sorted + g->align - 1) / g->align * g->align;
		if (n && sorted < g->size)
			pp_diag_help(c->p, d, "ordering its members by decreasing alignment makes it %llu bytes",
				     (unsigned long long)sorted);
	}
	pp_diag_emit(c->p, d);
}

/* the records named by "sizeof(struct X)" / "sizeof(T)" in the source text of r */
static __attribute__((cold)) void sizeof_layouts(cc *c, pp_span r)
{
	size_t len = 0;
	const char *t = pp_src(c->p, r.b, r.e, &len), *e;
	int shown = 0;

	if (!t)
		return;
	e = t + len;
	for (const char *q = t; q + 6 <= e && shown < 2; q++) {
		const char *n;
		bool tagged = false;
		uint32_t id, s;
		qty ty = 0;

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
		if (tagged && (s = lookup_tag(c, id)))
			ty = QT(c->tags[c->syms[s].val].ty, 0);
		else if (!tagged && (s = lookup(c, id)) && c->syms[s].kind == S_TYPEDEF)
			ty = c->syms[s].ty;
		if (ty && is_record(c, ty)) {
			layout_note(c, ty);
			shown++;
		}
	}
}

/* an integer literal as written (clang's IntegerLiteral), not a folded constant */
static bool int_literal(cc *c, uint32_t e)
{
	pp_span r = rng(c, e);
	size_t len = 0;
	const char *t = pp_src(c->p, r.b, r.e, &len);

	return N(e)->op == N_NUM && t && len && *t >= '0' && *t <= '9';
}

/* clang's findFailedBooleanCondition: the failing operand of a chain of && */
static uint32_t failed_term(cc *c, uint32_t e)
{
	for (;;) {
		i128 a;

		while (N(e)->op == N_CAST && !c->span[e].e)
			e = N(e)->a;
		if (N(e)->op != N_LOGAND)
			return e;
		e = const_int(c, N(e)->a, &a) && !a ? N(e)->a : N(e)->b;
	}
}

/* "static assertion failed": the requirement that failed, its operands' values, and
   the layout of the records whose sizes it compares */
static __attribute__((cold, noinline)) void static_assert_failed(cc *c, uint32_t e, const char *msg)
{
	pp_span r;
	size_t len = 0;
	const char *t;

	e = failed_term(c, e);
	r = rng(c, e);
	t = pp_src(c->p, r.b, r.e, &len);
	if (!t)
		t = "", len = 0;
	if (int_literal(c, e)) {	/* clang: a literal is no requirement */
		report(c, "error", r.b, &r, 1, NULL, 0, "static assertion failed%s%s", msg[0] ? ": " : "", msg);
		return;
	}
	report(c, "error", r.b, &r, 1, NULL, 0, "static assertion failed due to requirement '%.*s'%s%s",
	       (int)len, t, msg[0] ? ": " : "", msg);
	while (N(e)->op == N_CAST && !c->span[e].e)
		e = N(e)->a;
	if (N(e)->op == N_BINARY && is_cmp(N(e)->aux)) {
		i128 a, b;

		if (const_int(c, N(e)->a, &a) && const_int(c, N(e)->b, &b) &&
		    !(int_literal(c, N(e)->a) && int_literal(c, N(e)->b))) {	/* clang: nothing to evaluate */
			pp_span rr[2] = { r, r };
			size_t ol;
			const char *op = pp_src(c->p, N(e)->loc, pp_tok_end(c->p, N(e)->loc), &ol);

			if (op)
				report(c, "note", N(e)->loc, rr, 2, NULL, 0, "expression evaluates to '%lld %.*s %lld'",
				       (long long)a, (int)ol, op, (long long)b);
		}
	}
	sizeof_layouts(c, r);
}

VPRIV void static_assert_decl(cc *c)
{
	uint32_t loc = tok(c)->loc, n, e, open;
	i128 v;
	char msg[256] = "";

	next(c);
	open = expect_lparen(c);
	e = assign_expr(c);
	n = rvalue(c, e);
	if (!const_int(c, n, &v)) {
		cc_error(c, loc, "static assertion expression is not an integral constant expression");
		v = 1;
	}
	if (accept(c, T_COMMA)) {
		size_t k = 0;

		while (is_strtok(tok(c)->kind)) {
			char b[256];
			size_t l;

			tokspell(tok(c), b, sizeof(b));
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
	if (!v && !ISERR(e))	/* (clang: nothing for an invalid condition) */
		static_assert_failed(c, e, msg);
	expect_match(c, T_RPAREN, open);
	expect_semi(c, "after static_assert");
}

static qty int_type(cc *c, int nlong, int sgn, int base, uint32_t loc)
{
	(void)c;
	switch (base) {
	case KW_char:
		return prim(sgn == 0 ? TY_CHAR : sgn > 0 ? TY_SCHAR : TY_UCHAR);
	case KW_short:
		return prim(sgn < 0 ? TY_USHORT : TY_SHORT);
	case KW___int128:
		return prim(sgn < 0 ? TY_UINT128 : TY_INT128);
	default:
		if (nlong == 1)
			return prim(sgn < 0 ? TY_ULONG : TY_LONG);
		if (nlong >= 2) {
			if (nlong > 2)
				cc_error(c, loc, "'long long long' is invalid");
			return prim(sgn < 0 ? TY_ULLONG : TY_LLONG);
		}
		return prim(sgn < 0 ? TY_UINT : TY_INT);
	}
}

static qty typeof_spec(cc *c, bool unqual)
{
	qty t;
	uint32_t open;

	next(c);
	open = expect_lparen(c);
	if (is_type_start(c, tok(c))) {
		t = type_name(c);
	} else {
		uint32_t n;

		c->unevaluated++;
		n = expr(c);
		c->unevaluated--;
		t = N(n)->ty;
	}
	expect_match(c, T_RPAREN, open);
	return unqual ? QUNQ(t) : t;
}

/* a token that may follow the name in a declarator (clang's
   isValidAfterIdentifierInDeclarator): "static x = 4" is implicit int, not a typo */
static bool after_declarator_id(const ptok *t)
{
	int kw = kwof(t);

	switch (t->kind) {
	case T_LSQUARE: case T_LPAREN: case T_RPAREN: case T_SEMI: case T_COMMA: case T_EQUAL:
	case T_LBRACE: case T_COLON:
		return true;
	default:
		return kw == KW_asm || kw == KW___asm__ || kw == KW___asm;
	}
}

/* clang: signed/unsigned or long on a type that has none: "'void' cannot be signed or
   unsigned", "'long struct' is invalid"; returns WS_SGN / WS_LONG for what to drop
   (on as the type without them).  No pointers: sgn and nlong stay in registers */
enum { WS_SGN = 1, WS_LONG = 2 };
static __attribute__((noinline, cold)) int bad_width_sign(cc *c, int sgn, int nlong, int kw, dspec *ds)
{
	int r = 0;

	if (sgn && kw != KW_int && kw != KW_char && kw != KW_short && kw != KW___int128) {
		cc_error(c, ds->sgnloc, "'%.*s' cannot be signed or unsigned", IDF(kw));
		r |= WS_SGN;
	}
	if (nlong && kw != KW_int && !(kw == KW_double && nlong == 1)) {
		cc_error(c, ds->longloc, "'%s%.*s' is invalid", nlong > 1 ? "long long " : "long ", IDF(kw));
		r |= WS_LONG;
	}
	ds->invalid |= r != 0;	/* clang: an invalid type, the declaration is invalid */
	return r;
}

/* signed/unsigned or long (lng) after the base type specifier base: clang's errors;
   true if it is dropped */
static __attribute__((noinline, cold)) bool late_width_sign(cc *c, int base, int nlong, bool lng, uint32_t loc, dspec *ds)
{
	if (lng) {
		if (base == KW_int || (base == KW_double && !nlong))
			return false;
		if (base == KW_short)
			cc_error(c, loc, "cannot combine with previous 'short' declaration specifier");
		else
			cc_error(c, loc, "'long %.*s' is invalid", IDF(base)), ds->invalid = true;
		return true;
	}
	if (base == KW_int || base == KW_char || base == KW_short || base == KW___int128)
		return false;
	cc_error(c, loc, "'%.*s' cannot be signed or unsigned", IDF(base));
	ds->invalid = true;
	return true;
}

/* long/short/signed/unsigned after a struct, union or enum specifier: clang's error,
   the word dropped, the declaration invalid */
static void tag_semi(cc *c, int kind);

/* "struct S" ending its line and a type specifier starting the next: clang's
   missing ';' (a forward declaration), checked here, where vcc's own error would
   be, so that the many "struct S" of a valid TU pay nothing for it */
static bool tag_line_end(cc *c, const dspec *ds)
{
	if (!ds->tag_bare || !(tok(c)->flags & TF_SOL) || c->in_tyname)
		return false;
	tag_semi(c, KIND(c, QT(c->tags[ds->tagdecl].ty, 0)));
	return true;
}

static __attribute__((noinline, cold)) void tag_width(cc *c, dspec *ds, int kw)
{
	int k = KIND(c, QT(c->tags[ds->tagdecl].ty, 0));
	const char *tn = k == TY_ENUM ? "enum" : k == TY_UNION ? "union" : "struct";

	if (tag_line_end(c, ds))
		return;
	if (kw == KW_long || kw == KW_short)
		cc_error(c, tok(c)->loc, "'%.*s %s' is invalid", IDF(kw), tn);
	else
		cc_error(c, tok(c)->loc, "'%s' cannot be signed or unsigned", tn);
	ds->invalid = true;
	next(c);
}

/* a second base type specifier kw after base (or short after long): clang's errors;
   true to go on without kw ("short int" too, which is fine) */
static __attribute__((noinline)) bool base_clash(cc *c, int base, int kw, int nlong, uint32_t baseloc, uint32_t loc)
{
	if (kw == KW_short && nlong) {
		cc_error(c, loc, "cannot combine with previous 'long' declaration specifier");
		return true;
	}
	if (base == KW_short && kw != KW_int && kw != KW_short) {
		/* clang: short is a width, "short double" one invalid type */
		cc_error(c, baseloc, "'short %.*s' is invalid", IDF(kw));
		return false;
	}
	if (kw == KW_short && base != KW_int) {
		cc_error(c, loc, "'short %.*s' is invalid", IDF(base));
		return true;
	}
	if (!(base == KW_int && kw == KW_short) && !(kw == KW_int && base == KW_short) && !(base == KW_double && kw == KW_double)) {
		cc_error(c, loc, "cannot combine with previous '%.*s' declaration specifier", IDF(base));
		return true;	/* clang: on with the first */
	}
	return kw == KW_int;
}

/* clang's isValidAfterTypeSpecifier (C): what may follow "struct S { ... }" */
static __attribute__((noinline, cold)) bool valid_after_tag(cc *c)
{
	const ptok *t = tok(c);
	int kw;

	switch (t->kind) {
	case T_SEMI: case T_STAR: case T_AMP: case T_AMPAMP: case T_RPAREN: case T_LPAREN:
	case T_COMMA: case T_LSQUARE: case T_ELLIPSIS: case T_RBRACE:
		return true;
	case T_COLON:
		return c->in_struct;
	case T_IDENT:
		break;
	default:
		return false;
	}
	kw = kwof(t);
	switch (kw) {
	case 0: case KW___attribute__: case KW___attribute:
		return true;
	case KW_const: case KW___const__: case KW___const: case KW_volatile: case KW___volatile__: case KW___volatile:
	case KW_restrict: case KW___restrict__: case KW___restrict: case KW__Atomic: case KW_inline: case KW___inline__:
	case KW___inline: case KW_static: case KW_extern: case KW_typedef: case KW_register: case KW_auto:
	case KW__Thread_local: case KW_constexpr: {
		/* likely the next declaration's, unless a type specifier follows (isKnownToBeTypeSpecifier) */
		const ptok *n = peek(c, 1);
		int k = n->kind == T_IDENT ? kwof(n) : 0;

		return !(k && (kwclass[k] & KC_TYPE) && !(k == KW_const || k == KW___const__ || k == KW___const ||
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
static __attribute__((noinline, cold)) void tag_semi(cc *c, int kind)
{
	uint32_t e = c->prevflags & PF_MACRO ? tok(c)->loc : pp_tok_end(c->p, c->prevloc);
	pp_fixit fx = { e, e, ";" };
	ptok *t = c->tk;

	report(c, "error", e, NULL, 0, &fx, !(c->prevflags & PF_MACRO), "expected ';' after %s",
	       kind == TY_ENUM ? "enum" : kind == TY_UNION ? "union" : "struct");
	peek(c, 0);
	if (c->ntk >= 4)
		return;
	if (c->ntk < 1)
		c->ntk = 1;
	memmove(t + 1, t, c->ntk * sizeof(*t));
	c->ntk++;
	t[0] = (ptok){ .s = ";", .len = 1, .loc = e, .kind = T_SEMI };
}

/* after a struct, union or enum body: clang's missing ';' when what follows cannot
   follow a type specifier (out of decl_specs' loop, which it would cost registers) */
static __attribute__((noinline)) void tag_body_end(cc *c, int kind)
{
	if (likely(is(c, T_SEMI)) || c->in_tyname || valid_after_tag(c))
		return;
	tag_semi(c, kind);
}

/* "int struct S x", "struct S int x": clang's error at the second one, which is
   skipped (a tag's name and body too); the declaration goes on with the first */
static __attribute__((noinline, cold)) void spec_clash(cc *c, const dspec *ds, int base)
{
	const ptok *t = tok(c);
	int kw = kwof(t);

	if (!base && tag_line_end(c, ds))
		return;
	if (base)
		cc_error(c, t->loc, "cannot combine with previous '%.*s' declaration specifier", IDF(base));
	else
		cc_error(c, t->loc, "cannot combine with previous '%s' declaration specifier",
			 KIND(c, QT(c->tags[ds->tagdecl].ty, 0)) == TY_ENUM ? "enum" :
			 KIND(c, QT(c->tags[ds->tagdecl].ty, 0)) == TY_UNION ? "union" : "struct");
	next(c);
	if (kw == KW_struct || kw == KW_union || kw == KW_enum) {
		attrs a = { 0 };

		attribute_list(c, &a);
		if (is(c, T_IDENT) && !kwof(tok(c)))
			next(c);
		if (is(c, T_LBRACE))
			skip_balanced(c);
	}
}

/* clang's ParseImplicitInt: "static in f(void)", "const unsigne long *p": after
   specifiers without a type, an unknown name where only a type fits is an unknown
   type name.  0: not one; QTY_KW: a keyword typo, rewritten; else the type */
#define QTY_KW ((qty)~0u)
static __attribute__((noinline, cold)) qty implicit_int_name(cc *c, dspec *ds)
{
	ptok *t;
	uint32_t kwc, s;

	if (kwof(tok(c)) || after_declarator_id(peek(c, 1)))
		return 0;
	t = tok(c);
	s = undeclared(c, "unknown type name", t->id, t->loc, true, KWS_TYPE, &kwc);
	if (kwc) {
		t->id = kwc;	/* on as the keyword */
		return QTY_KW;
	}
	next(c);
	ds->invalid = !s;
	return s ? c->syms[s].ty : prim(TY_INT);
}

/* no type specifier: clang reports it at the declarator's name ("static *p", "volatile (v)") */
static __attribute__((noinline, cold)) void implicit_int_error(cc *c)
{
	uint32_t i = 0, at = tok(c)->loc;

	while (i < 16 && (peek(c, i)->kind == T_STAR || peek(c, i)->kind == T_LPAREN || is_cvr(kwof(peek(c, i)))))
		i++;
	if (peek(c, i)->kind == T_IDENT && !kwof(peek(c, i)))
		at = peek(c, i)->loc;
	if (c->o->std == 23)
		cc_error(c, at, "a type specifier is required for all declarations");
	else
		cc_error(c, at, "type specifier missing, defaults to 'int'; ISO C99 and later do not support implicit int [-Wimplicit-int]");
}

VPRIV bool decl_specs(cc *c, dspec *ds, bool allow_sc)
{
	int nlong = 0, sgn = 0, base = 0, q = 0;
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
		int kw = kwof(t);

		if (at_std_attr(c)) {
			attribute_list(c, &ds->at);
			continue;
		}
		if (!kw) {
			if (is_nullability(c, t)) {
				next(c);
				continue;
			}
			if (!have_named && !base && !nlong && !sgn && t->kind == T_IDENT && typedef_id(c, t->id)) {
				uint32_t s = lookup(c, t->id);

				named = c->syms[s].ty;
				c->syms[s].flags |= SF_REF;	/* (-Wunused-local-typedef) */
				if (unlikely(c->syms[s].flags & SF_DEPR))
					depr_use(c, s, t->loc);
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
				cc_error(c, t->loc, "invalid storage class specifier");
			if (kw == KW_auto && c->o->std == 23) {	/* C23: maybe type inference (decided at the end) */
				c23auto = true;
				break;
			}
			if (ds->sc && ds->sc != sc) {
				cc_error(c, t->loc, "cannot combine with previous '%.*s' declaration specifier", IDF(ds->sckw));
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
		case KW_const: case KW___const__: case KW___const:
		case KW_constexpr:	/* C23: a const object with a constant initializer (not checked) */
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
			if (is(c, T_LPAREN)) {
				uint32_t open = tok(c)->loc;

				next(c);
				named = ty_atomic(c, type_name(c), aloc);
				have_named = true;
				expect_match(c, T_RPAREN, open);
				any = true;
			} else {
				atomic = aloc ? aloc : 1;	/* the qualifier: applied to the type below */
			}
			continue;
		}
		case KW___attribute__: case KW___attribute:
			attribute_list(c, &ds->at);
			continue;
		case KW___extension__:
			break;
		case KW__Alignas: {
			uint32_t a, open;

			next(c);
			open = expect_lparen(c);
			if (is_type_start(c, tok(c))) {
				a = ty_align(c, type_name(c));
			} else {
				i128 v;

				const_expr(c, &v, NULL);
				a = (uint32_t)v;
			}
			expect_match(c, T_RPAREN, open);
			if (a > ds->align)
				ds->align = a;
			continue;
		}
		case KW_void: case KW__Bool: case KW_char: case KW_short: case KW_int: case KW_float:
		case KW_double: case KW___int128: case KW__Float16: case KW___fp16:
			if (have_named) {
				if (unlikely(ds->tagdecl) && !base) {
					spec_clash(c, ds, 0);
					continue;
				}
				goto done;
			}
			if (unlikely(base || (kw == KW_short && nlong)) && base_clash(c, base, kw, nlong, ds->baseloc, t->loc))
				break;
			if (unlikely((sgn | nlong) && kw != KW_int)) {
				int w = bad_width_sign(c, sgn, nlong, kw, ds);

				sgn = w & WS_SGN ? 0 : sgn;
				nlong = w & WS_LONG ? 0 : nlong;
			}
			if (kw == KW_short)
				ds->baseloc = t->loc;
			base = kw;
			any = true;
			break;
		case KW_long:
			if (have_named) {
				if (unlikely(ds->tagdecl) && !base) {
					tag_width(c, ds, kw);
					continue;
				}
				goto done;
			}
			if (unlikely(base) && late_width_sign(c, base, nlong, true, t->loc, ds))
				break;
			if (!nlong++)
				ds->longloc = t->loc;
			any = true;
			break;
		case KW_signed: case KW___signed__: case KW___signed:
			if (have_named) {
				if (unlikely(ds->tagdecl) && !base) {
					tag_width(c, ds, kw);
					continue;
				}
				goto done;
			}
			if (unlikely(base) && late_width_sign(c, base, nlong, false, t->loc, ds))
				break;
			ds->sgnloc = t->loc;
			sgn = 1;
			any = true;
			break;
		case KW_unsigned:
			if (have_named) {
				if (unlikely(ds->tagdecl) && !base) {
					tag_width(c, ds, kw);
					continue;
				}
				goto done;
			}
			if (unlikely(base) && late_width_sign(c, base, nlong, false, t->loc, ds))
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
			named = prim(TY_INT);
			break;
		case KW_struct: case KW_union: case KW_enum: {
			int kind = kw == KW_struct ? TY_STRUCT : kw == KW_union ? TY_UNION : TY_ENUM;
			uint32_t loc = t->loc, name = 0, ti;
			attrs pre = { 0 }, post = { 0 };
			bool body;

			if (unlikely(base) && !have_named) {
				spec_clash(c, ds, base);
				continue;
			}
			if (have_named || base)
				goto done;
			if (unlikely(sgn | nlong)) {
				bad_width_sign(c, sgn, nlong, kw, ds);
				sgn = nlong = 0;
			}
			next(c);
			attribute_list(c, &pre);
			if (is(c, T_IDENT) && !kwof(tok(c))) {
				name = tok(c)->id;
				loc = tok(c)->loc;
				next(c);
			}
			body = is(c, T_LBRACE) || (kind == TY_ENUM && is(c, T_COLON));
			ti = tag_lookup_or_new(c, kind, name, loc, body, !body && is(c, T_SEMI) && !c->in_struct);
			if (name && unlikely(attrs_depr(c, &pre) || c->has_depr_tag)) {	/* deprecated tags */
				uint32_t ts = lookup_tag(c, name);

				if (ts && attrs_depr(c, &pre)) {
					apply_sym_attrs(c, &c->syms[ts], &pre);
					c->has_depr_tag = true;
				} else if (ts && !body && (c->syms[ts].flags & SF_DEPR))
					depr_use(c, ts, loc);
			}
			if (body) {
				tag *r = &c->tags[ti];

				if (r->flags & RF_COMPLETE) {
					cc_error(c, loc, "redefinition of '%.*s'", IDF(name));
					note(c, r->loc, "previous definition is here");
				}
				if (kind == TY_ENUM) {
					merge_attrs(c, &post, &pre);
					enum_body(c, ti, &post);
					tag_body_end(c, kind);
				} else {
					c->tags[ti].flags |= (pre.packed ? RF_PACKED : 0);
					c->tags[ti].maxalign = pre.aligned;
					struct_body(c, ti);
					tag_body_end(c, kind);
					if (unlikely(ext_on(c, "gnu-empty-struct", w_gnu))) {
						const tag *r = &c->tags[ti];
						bool named = false;

						for (uint32_t i = 0; i < r->nfield && !named; i++)
							named = r->f[i].name || (!r->f[i].bitfield && is_record(c, r->f[i].ty));
						if (!named)
							ext_warn(c, "gnu-empty-struct", w_gnu, loc, r->nfield ? "%s without named members is a GNU extension"
								 : "empty %s is a GNU extension", kind == TY_UNION ? "union" : "struct");
					}
				}
			} else {
				ds->tag_bare = true;
			}
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
				EXT_WARN(c, "language-extension-token", w_ped, t->loc, "extension used");
			named = typeof_spec(c, kw == KW___typeof_unqual__);
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
	if (unlikely(!any) && is(c, T_IDENT) && (q || atomic || ds->sc || ds->inline_ || ds->noreturn || ds->thread)) {
		qty it = implicit_int_name(c, ds);

		if (it) {
			if (it != QTY_KW)
				named = it, have_named = any = true;
			goto again;
		}
	}
	ds->has_type = any;
	if (!any && !q && !atomic && !ds->sc && !c23auto && !ds->inline_ && !ds->noreturn && !ds->thread && !ds->align)
		return false;
	if (have_named) {
		ds->ty = named;
	} else if (base == KW_void) {
		ds->ty = prim(TY_VOID);
	} else if (base == KW__Bool) {
		ds->ty = prim(TY_BOOL);
	} else if (base == KW_float) {
		ds->ty = prim(TY_FLOAT);
	} else if (base == KW__Float16 || base == KW___fp16) {
		ds->ty = prim(TY_FLOAT16);
	} else if (base == KW_double) {
		ds->ty = prim(nlong ? TY_LDOUBLE : TY_DOUBLE);
	} else {
		if (unlikely(!any)) {
			if (c23auto && !nlong && !sgn && !cplx) {
				/* C23: 'auto' without a type specifier infers the type, as __auto_type */
				ds->auto_type = true;
			} else if (!c->o->implicit_int && !pp_in_system_header(c->p, ds->loc))
				implicit_int_error(c);
		}
		ds->ty = int_type(c, nlong, sgn, base, ds->loc);
	}
	if (unlikely(c23auto) && !ds->auto_type && !ds->sc)	/* C23 "auto int x": the storage class */
		ds->sc = SC_AUTO, ds->sckw = KW_auto;
	if (cplx) {
		if (!base && !nlong && !sgn && !have_named)
			ds->ty = prim(TY_DOUBLE);	/* plain _Complex: double (GNU) */
		if (!is_arith(c, ds->ty))
			cc_error(c, ds->loc, "_Complex requires an arithmetic element type");
		else
			ds->ty = ty_complex(c, ds->ty);
	}
	if (ds->at.mode) {
		int k = mode_kind(c, ds->at.mode, is_signed(c, ds->ty));

		if (k)
			ds->ty = prim(k);
	}
	if (atomic)
		ds->ty = ty_atomic(c, ds->ty, atomic);
	ds->ty = qualify(c, ds->ty, q);
	if (ds->inline_ && c->o->gnu89_inline)
		ds->at.gnu_inline = 1;	/* -std=gnu89: gnu_inline semantics */
	return true;
}
