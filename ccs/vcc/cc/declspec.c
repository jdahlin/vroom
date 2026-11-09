/* vcc front end: declaration specifiers */
#include "ccimpl.h"

/* an identifier where a type name must be: clang's "unknown type name" (with typo
   correction among the typedefs and the type keywords), then on as if it were the
   correction (or int); a keyword is parsed as written so (allow_sc: storage classes) */
VPRIV void unknown_type(cc *c, dspec *ds, bool allow_sc)
{
	uint32_t kw, loc = tok(c)->loc, s = undeclared(c, "unknown type name", tok(c)->id, loc, true, KWS_TYPE, &kw);

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

		if (name) {
			uint32_t ns = sym_new(c, S_TAG, name, QT(c->tags[ti].ty, 0), loc);

			c->syms[ns].val = ti;
			sym_bind(c, ns);
		}
		return ti;
	}
}

VPRIV void static_assert_decl(cc *c)
{
	uint32_t loc = tok(c)->loc, n, e;
	i128 v;
	char msg[256] = "";

	next(c);
	expect(c, T_LPAREN, "'('");
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
	if (!v) {
		pp_span r = rng(c, e);
		size_t len = 0;
		const char *t = pp_src(c->p, r.b, r.e, &len);

		if (!t)
			t = "", len = 0;
		report(c, "error", r.b, &r, 1, NULL, 0, "static assertion failed due to requirement '%.*s'%s%s",
		       (int)len, t, msg[0] ? ": " : "", msg);
		while (N(e)->op == N_CAST && !c->span[e].e)
			e = N(e)->a;
		if (N(e)->op == N_BINARY && is_cmp(N(e)->aux)) {
			i128 a, b;

			if (const_int(c, N(e)->a, &a) && const_int(c, N(e)->b, &b)) {
				pp_span rr[2] = { r, r };
				size_t ol;
				const char *op = pp_src(c->p, N(e)->loc, pp_tok_end(c->p, N(e)->loc), &ol);

				if (op)
					report(c, "note", N(e)->loc, rr, 2, NULL, 0, "expression evaluates to '%lld %.*s %lld'",
					       (long long)a, (int)ol, op, (long long)b);
			}
		}
	}
	expect(c, T_RPAREN, "')'");
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

	next(c);
	expect(c, T_LPAREN, "'('");
	if (is_type_start(c, tok(c))) {
		t = type_name(c);
	} else {
		uint32_t n;

		c->unevaluated++;
		n = expr(c);
		c->unevaluated--;
		t = N(n)->ty;
	}
	expect(c, T_RPAREN, "')'");
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

VPRIV bool decl_specs(cc *c, dspec *ds, bool allow_sc)
{
	int nlong = 0, sgn = 0, base = 0, q = 0;
	bool any = false, cplx = false;
	qty named = 0;
	bool have_named = false;
	uint32_t atomic = 0;	/* the _Atomic qualifier's location */

	memset(ds, 0, sizeof(*ds));
	ds->loc = tok(c)->loc;
	if (c->has_pend) {
		ds->at = c->pend;
		c->has_pend = false;
	}
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
				named = c->syms[lookup(c, t->id)].ty;
				have_named = true;
				any = true;
				next(c);
				continue;
			}
			if (t->kind == T_IDENT && !any && !have_named && !base && !nlong && !sgn && !cplx &&
			    (q || atomic || ds->sc || ds->inline_ || ds->noreturn || ds->thread) &&
			    !after_declarator_id(peek(c, 1))) {
				/* clang's ParseImplicitInt: "static in f(void)", "const unsigne long *p":
				   an unknown name where only a type fits is an unknown type name */
				uint32_t kwc, s;

				t = tok(c);
				s = undeclared(c, "unknown type name", t->id, t->loc, true, KWS_TYPE, &kwc);
				if (kwc) {
					t->id = kwc;	/* on as the keyword */
					continue;
				}
				next(c);
				named = s ? c->syms[s].ty : prim(TY_INT);
				have_named = any = true;
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
			if (ds->sc && ds->sc != sc)
				cc_error(c, t->loc, "cannot combine with previous storage class specifier");
			ds->sc = sc;
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
				next(c);
				named = ty_atomic(c, type_name(c), aloc);
				have_named = true;
				expect(c, T_RPAREN, "')'");
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
			uint32_t a;

			next(c);
			expect(c, T_LPAREN, "'('");
			if (is_type_start(c, tok(c))) {
				a = ty_align(c, type_name(c));
			} else {
				i128 v;

				const_expr(c, &v, NULL);
				a = (uint32_t)v;
			}
			expect(c, T_RPAREN, "')'");
			if (a > ds->align)
				ds->align = a;
			continue;
		}
		case KW_void: case KW__Bool: case KW_char: case KW_short: case KW_int: case KW_float:
		case KW_double: case KW___int128: case KW__Float16: case KW___fp16:
			if (have_named)
				goto done;
			if (base && !(base == KW_int && (kw == KW_short || kw == KW_char)) && !(kw == KW_int && (base == KW_short || base == KW_char)) &&
			    !(base == KW_double && kw == KW_double))
				cc_error(c, t->loc, "cannot combine with previous type specifier");
			if (kw == KW_int && base)
				break;
			base = kw;
			any = true;
			break;
		case KW_long:
			if (have_named)
				goto done;
			nlong++;
			any = true;
			break;
		case KW_signed: case KW___signed__: case KW___signed:
			if (have_named)
				goto done;
			sgn = 1;
			any = true;
			break;
		case KW_unsigned:
			if (have_named)
				goto done;
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

			if (have_named || base)
				goto done;
			next(c);
			attribute_list(c, &pre);
			if (is(c, T_IDENT) && !kwof(tok(c))) {
				name = tok(c)->id;
				loc = tok(c)->loc;
				next(c);
			}
			body = is(c, T_LBRACE) || (kind == TY_ENUM && is(c, T_COLON));
			ti = tag_lookup_or_new(c, kind, name, loc, body, !body && is(c, T_SEMI) && !c->in_struct);
			if (body) {
				tag *r = &c->tags[ti];

				if (r->flags & RF_COMPLETE) {
					cc_error(c, loc, "redefinition of '%.*s'", IDF(name));
					note(c, r->loc, "previous definition is here");
				}
				if (kind == TY_ENUM) {
					merge_attrs(&post, &pre);
					enum_body(c, ti, &post);
				} else {
					c->tags[ti].flags |= (pre.packed ? RF_PACKED : 0);
					c->tags[ti].maxalign = pre.aligned;
					struct_body(c, ti);
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
	ds->has_type = any;
	if (!any && !q && !atomic && !ds->sc && !ds->inline_ && !ds->noreturn && !ds->thread && !ds->align)
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
		if (!any && !c->o->implicit_int && !pp_in_system_header(c->p, ds->loc))
			cc_error(c, ds->loc, "type specifier missing, defaults to 'int'; ISO C99 and later do not support implicit int");
		ds->ty = int_type(c, nlong, sgn, base, ds->loc);
	}
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
