/* vcc front end: declarations, declare(), K&R parameters and the declaration list */
#include "ccimpl.h"

/* the braced initializer of a VLA, already diagnosed: skipped unchecked */
static void skip_vla_init(cc *c)
{
	int depth = 0;

	do {
		if (is(c, LEX_TOKEN_LEFT_BRACE))
			depth++;
		else if (is(c, LEX_TOKEN_RIGHT_BRACE))
			depth--;
		else if (is(c, LEX_TOKEN_EOF))
			fatal(c, tok(c)->loc, "expected '}'");
		next(c);
	} while (depth > 0);
}

/* ---------------- declarations ---------------- */

VPRIV uint32_t new_attrs(cc *c, const attrs *a)
{
	uint32_t i = c->nattr++;

	c->attr[i] = *a;
	return i;
}

/* clang's -Wdeprecated-declarations for a use at loc of name, a: the record */
VPRIV __attribute__((noinline, cold)) void deprecated_use(cc *c, uint32_t loc, uint32_t name, const attrs *a, uint32_t dloc)
{
	if (unlikely(a->unavail)) {	/* clang: an error, also inside an unavailable function */
		if (a->deprmsg)
			cc_error(c, loc, "'%.*s' is unavailable: %s", IDF(name), c->strs[a->deprmsg - 1].s);
		else
			cc_error(c, loc, "'%.*s' is unavailable", IDF(name));
		note(c, dloc, "'%.*s' has been explicitly marked unavailable here", IDF(name));	/* (clang: the declaration) */
		return;
	}
	if (c->fn && (c->syms[c->fn].flags & SF_DEPRECATED) && !c->attr[c->syms[c->fn].attrs].unavail)
		return;	/* clang: not inside a deprecated function */
	if (a->deprmsg)
		cc_warnx(c, "deprecated-declarations", true, NULL, loc, NULL, 0, NULL, 0, "'%.*s' is deprecated: %s",
			 IDF(name), c->strs[a->deprmsg - 1].s);
	else
		cc_warnx(c, "deprecated-declarations", true, NULL, loc, NULL, 0, NULL, 0, "'%.*s' is deprecated", IDF(name));
	note(c, a->depr, "'%.*s' has been explicitly marked deprecated here", IDF(name));
}

/* a deprecated tag: its record */
VPRIV void tag_deprecate(cc *c, uint32_t ti, const attrs *a)
{
	if (a->depr && !c->tags[ti].depr)
		c->tags[ti].depr = new_attrs(c, a);
}

VPRIV void apply_sym_attrs(cc *c, sym *s, const attrs *a)
{
	if (attrs_empty(a))
		return;		/* (no record: sym_attrs() in emit.c reads attrs 0 as none) */
	if (a->interrupt) {
		const ctype *ft = T(c, s->ty);
		if (!CC_X86_64(c))
			cc_error(c, s->loc, "interrupt calling convention is supported only for x86-64 Linux");
		else if (s->kind != S_FUNC || ft->kind != TY_FUNC)
			cc_error(c, s->loc, "'interrupt' attribute only applies to functions");
		else if (KIND(c, ft->base) != TY_VOID)
			cc_error(c, s->loc, "interrupt function must return void");
		else if ((ft->flags & TF_NOPROTO) || ft->n < 1 || ft->n > 2 || !is_ptr(c, c->params[ft->aux]))
			cc_error(c, s->loc, "interrupt function requires a pointer parameter optionally followed by an unsigned long");
		else if (ft->n == 2 && KIND(c, c->params[ft->aux + 1]) != TY_ULONG && KIND(c, c->params[ft->aux + 1]) != TY_ULLONG)
			cc_error(c, s->loc, "interrupt function second parameter must have unsigned long type");
	}
	if (a->tlsmodel && (s->kind != S_VAR || !(s->flags & SF_THREAD)))
		cc_error(c, a->tlsloc, "'tls_model' attribute only applies to thread-local variables");
	if (unlikely(a->dll == CC_DLLIMPORT) && s->sc == SC_STATIC)
		cc_error(c, s->loc, "'%.*s' must have external linkage when declared 'dllimport'", IDF(s->name));
	if (s->attrs)
		merge_attrs(&c->attr[s->attrs], a);
	else
		s->attrs = new_attrs(c, a);
	if (a->aligned > s->align)
		s->align = a->aligned;
	if (a->weak) s->flags |= SF_WEAK;
	if (a->depr) s->flags |= SF_DEPRECATED;
	if (a->wur) s->flags |= SF_WUR;
	if (a->noreturn) s->flags |= SF_NORETURN;
	if (a->always_inline) s->flags |= SF_ALWAYS_INLINE;
	if (a->noinline) s->flags |= SF_NOINLINE;
	if (a->noinstr) s->flags |= SF_NOINSTR;
	if (a->gnu_inline) s->flags |= SF_GNU_INLINE;
	if (a->used) s->flags |= SF_ATTR_USED;
	if (a->overloadable) s->flags |= SF_OVERLOAD;
}

/* the file-scope entity name refers to, if any */
VPRIV uint32_t file_scope_sym(cc *c, uint32_t name)
{
	for (uint32_t s = lookup(c, name); s; s = c->syms[s].shadow)
		if (c->syms[s].depth == 0)
			return s;
	return 0;
}

/* the file-scope sym named s (the emitter: alias targets), or 0 */
uint32_t cc_global_named(cc *c, const char *s)
{
	size_t n = strlen(s);
	uint32_t id = vintern_find(s, n, vintern_hash(s, n));

	return id ? file_scope_sym(c, id) : 0;
}

VPRIV uint32_t declare(cc *c, const dspec *ds, declr *d, qty t, bool file)
{
	attrs at = d->at;
	uint32_t s, prev;
	int kind;
	bool inherit_type = true;

	merge_attrs(&at, &ds->at);
	if (!at.visibility && c->nvis && file)
		at.visibility = c->visstk[c->nvis - 1];	/* #pragma GCC visibility push */
	if (ds->align > at.aligned)
		at.aligned = ds->align;
	if (d->at.mode && is_arith(c, t)) {
		int k = mode_kind(c, d->at.mode, is_signed(c, t));

		if (k)
			t = QT(k, QQ(t));
	}
	if (at.vecsize) t = vector_attribute(c, t, at.vecsize, d->loc);
	if (unlikely(at.fnabi)) {
		/* ms_abi / sysv_abi: on the function type (through pointers), the target's own as 0 */
		int conv = at.fnabi == (CC_WIN64(c) ? CC_FNABI_MS : CC_FNABI_SYSV) ? CC_FNABI_DEFAULT : (int)at.fnabi;
		qty ft = ty_fnabi(c, t, conv);

		if (ft)
			t = ft;
		else
			cc_warnx(c, "ignored-attributes", true, NULL, at.fnabi_loc, NULL, 0, NULL, 0,
				 "'%s' only applies to function types; type here is '%s'", at.fnabi == CC_FNABI_MS ? "ms_abi" : "sysv_abi", tyname(c, t));
	}
	if (at.extvec && ds->sc == SC_TYPEDEF && is_bigint(c, t))
		cc_error(c, d->loc, "vector integer elements wider than 128 bits are not supported");
	else if (at.extvec && ds->sc == SC_TYPEDEF && is_arith(c, t) && !is_complex(c, t)) {
		/* clang: n elements, the size rounded up to a power of 2 (float3: 16) */
		uint64_t sz = KIND(c, t) == TY_BOOL ? (at.extvec + 7) / 8 : (uint64_t)at.extvec * ty_size(c, t), p = 1;

		while (p < sz)
			p <<= 1;
		{
			ctype v = *T(c, ty_vector(c, t, (uint32_t)p));

			v.ext = (uint16_t)at.extvec;
			if (KIND(c, t) == TY_BOOL) {
				v.n = at.extvec;
				if (CC_X86_64(c)) v.align = (uint32_t)p;
			}
			t = QT(ty_intern(c, &v), QQ(t));
		}
	}
	if (ds->sc == SC_TYPEDEF) {
		if (at.aligned)
			t = ty_aligned(c, t, at.aligned);
		if (at.transparent && KIND(c, t) == TY_UNION) {
			DR_TAGWRITE(c, T(c, t)->aux);
			c->ty[QI(t)].flags |= TF_TRANSPARENT;
		}
		prev = lookup(c, d->name);
		if (prev && c->syms[prev].depth == c->depth && c->syms[prev].kind == S_TYPEDEF) {
			if (!compatible(c, c->syms[prev].ty, t) || QQ(c->syms[prev].ty) != QQ(t))
				cc_error(c, d->loc, "typedef redefinition with different types");
			return prev;
		}
		s = sym_new(c, S_TYPEDEF, d->name, t, d->loc);
		c->syms[s].sc = SC_TYPEDEF;
		c->syms[s].tdef = ds->tdef && QUNQ(t) == QUNQ(ds->ty) ? ds->tdef : 0;
		if (unlikely(at.depr)) {	/* only what a typedef's use needs */
			attrs da = { 0 };

			da.depr = at.depr, da.deprmsg = at.deprmsg;
			apply_sym_attrs(c, &c->syms[s], &da);
		}
		sym_bind(c, s);
		if (is_record(c, t) || KIND(c, t) == TY_ENUM) {
			tag *r = &c->tags[T(c, t)->aux];

			if (!r->name && !r->typedef_name && file) {
				DR_TAGWRITE(c, T(c, t)->aux);
				r->typedef_name = d->name;
			}
		}
		if (file)
			c->toplevel[c->ntoplevel++] = s;
		return s;
	}
	kind = is_func(c, t) ? S_FUNC : S_VAR;
	if (kind == S_VAR && is_void(c, t) && ds->sc != SC_EXTERN) {
		cc_error(c, d->loc, "variable has incomplete type 'void'");
		d->invalid = true;
	}
	/* redeclarations: the same entity in the same scope, or a block-scope extern */
	prev = 0;
	{
		uint32_t p = lookup(c, d->name);
		bool same = p && c->syms[p].depth == c->depth;
		bool linkage = file || ds->sc == SC_EXTERN || kind == S_FUNC;

		if (same && linkage) {
			prev = p;
		} else if (same && c->syms[p].kind != S_TYPEDEF) {
			cc_error(c, d->loc, "redefinition of '%.*s'", IDF(d->name));
			note(c, c->syms[p].loc, "previous definition is here");
		} else if (!file && linkage) {
			/* An extern declaration inherits the nearest visible declaration
			   with linkage, including one in an enclosing block. */
			inherit_type = p && (c->syms[p].flags & SF_GLOBAL);
			prev = inherit_type ? p : file_scope_sym(c, d->name);
		}
	}
	if (prev && kind == S_FUNC && c->syms[prev].kind == S_FUNC &&
	    (at.overloadable || (c->syms[prev].flags & SF_OVERLOAD))) {
		/* overloadable: redeclare the member with this type, or add one to the set */
		uint32_t head = prev, q;

		for (q = head; q; q = c->syms[q].ovl)
			if (compatible(c, c->syms[q].ty, t))
				break;
		if (q) {
			prev = q;
		} else if (at.overloadable && (c->syms[head].flags & SF_OVERLOAD)) {
			sym *y;

			s = sym_new(c, kind, d->name, t, d->loc);
			y = &c->syms[s];
			y->sc = (uint8_t)ds->sc;
			y->depth = c->syms[head].depth;
			y->flags |= SF_GLOBAL;
			if (ds->inline_)
				y->flags |= SF_INLINE | (ds->sc != SC_EXTERN ? SF_INLINE_NOEXTERN : 0);
			if (ds->noreturn)
				y->flags |= SF_NORETURN;
			apply_sym_attrs(c, y, &at);
			for (q = head; c->syms[q].ovl; q = c->syms[q].ovl)
				;
			c->syms[q].ovl = s;
			if (file)
				c->toplevel[c->ntoplevel++] = s;
			name_overload(c, s);
			return s;
		} else {
			cc_error(c, d->loc, at.overloadable ? "redeclaration of '%.*s' must not have the 'overloadable' attribute"
			      : "redeclaration of '%.*s' must have the 'overloadable' attribute", IDF(d->name));
			return head;
		}
	}
	if (prev && unlikely(c->syms[prev].flags & SF_INVALID) && c->syms[prev].kind != kind)
		prev = 0;	/* a placeholder left by parser recovery (rec_restore): replaced */
	if (unlikely(prev && kind == S_FUNC && is_func(c, c->syms[prev].ty) && is_func(c, t)) &&
	    T(c, c->syms[prev].ty)->ext != T(c, t)->ext) {
		/* x86-64 ms_abi / sysv_abi: a redeclaration without one keeps the earlier's (clang) */
		if (!T(c, t)->ext)
			t = ty_fnabi(c, t, T(c, c->syms[prev].ty)->ext);
		else if (!T(c, c->syms[prev].ty)->ext) {
			cc_error(c, d->loc, "function declared '%s' here was previously declared without calling convention",
				 T(c, t)->ext == CC_FNABI_MS ? "ms_abi" : "sysv_abi");
			note(c, c->syms[prev].loc, "previous declaration is here");
			t = ty_fnabi(c, t, 0);
		}
	}
	if (prev) {
		sym *y = &c->syms[prev];

		if (y->kind != kind) {
			cc_error(c, d->loc, "redefinition of '%.*s' as different kind of symbol", IDF(d->name));
			note(c, y->loc, "previous definition is here");
		} else if (!compatible(c, y->ty, t)) {
			cc_error(c, d->loc, "conflicting types for '%.*s'", IDF(d->name));
			if (kind == S_FUNC && (y->flags & SF_DEFINED) && y->defloc)	/* (clang: a defined function's definition) */
				note(c, y->defloc, "previous definition is here");
			else
				note(c, y->loc, "previous declaration is here");
		} else {
			qty merged = inherit_type ? QT(QI(composite(c, y->ty, t)), QQ(y->ty) | QQ(t)) : t;
			bool inner = !file && y->depth != c->depth;
			/* A composite bound is visible in the new declaration's scope;
			   completing an inner extern must not complete its outer binding. */
			if (!inner)
				y->ty = merged;
			if (ds->sc == SC_STATIC && kind == S_FUNC)
				y->sc = SC_STATIC;
			if (ds->inline_)
				y->flags |= SF_INLINE | (ds->sc != SC_EXTERN ? SF_INLINE_NOEXTERN : 0);
			if (kind == S_FUNC && file && (!ds->inline_ || ds->sc == SC_EXTERN))
				y->flags |= SF_EXTDECL;
			apply_sym_attrs(c, y, &at);
			if (inner) {
				/* block-scope extern: a local type, linked to the same entity */
				s = sym_new(c, kind, d->name, merged, d->loc);
				c->syms[s].sc = SC_EXTERN;
				c->syms[s].next = prev;
				c->syms[s].flags = y->flags;
				sym_bind(c, s);
				return s;
			}
			return prev;
		}
	}
	s = sym_new(c, kind, d->name, t, d->loc);
	{
		sym *y = &c->syms[s];

		y->tdef = decl_tdef(c, ds->tdef, ds->ty, t);
		y->sc = (uint8_t)(ds->sc == SC_TYPEDEF ? SC_NONE : ds->sc);
		if (file || ds->sc == SC_EXTERN || kind == S_FUNC)
			y->flags |= SF_GLOBAL;
		if (ds->inline_)
			y->flags |= SF_INLINE | (ds->sc != SC_EXTERN ? SF_INLINE_NOEXTERN : 0);
		if (kind == S_FUNC && file && (!ds->inline_ || ds->sc == SC_EXTERN))
			y->flags |= SF_EXTDECL;
		if (ds->noreturn)
			y->flags |= SF_NORETURN;
		if (ds->thread)
			y->flags |= SF_THREAD;
		apply_sym_attrs(c, y, &at);
		if (!file && kind == S_VAR && ds->sc != SC_EXTERN && ds->sc != SC_STATIC && !y->sc)
			y->sc = SC_AUTO;
		if (!file && prev == 0 && (ds->sc == SC_EXTERN || kind == S_FUNC)) {
			/* first declaration of a global from block scope: also the file-scope entity */
		}
	}
	sym_bind(c, s);
	if (file)
		c->toplevel[c->ntoplevel++] = s;
	if (kind == S_FUNC && at.overloadable)
		name_overload(c, s);
	if (unlikely(c->npweak))
		weak_declared(c, s);
	return s;
}

static void kr_params(cc *c, const declr *fd)
{
	uint8_t seen[256] = { 0 };

	/* tags declared here belong to the function, not the file (approximation: not seen by the body) */
	scope_open(c);
	c->in_proto++;
	while (!is(c, LEX_TOKEN_LEFT_BRACE)) {
		dspec ds;

		if (!decl_specs(c, &ds, true)) {
			if (!is(c, LEX_TOKEN_IDENTIFIER) || kwof(tok(c)))
				fatal(c, tok(c)->loc, "expected ';' after top level declarator");
			unknown_type(c, &ds, true);
		}
		for (;;) {
			declr d;
			uint32_t first, nops, i;
			qty t;

			declarator(c, &d, false, &first, &nops);
			attribute_list(c, &d.at);
			t = apply_declarator(c, ds.ty, first, nops);
			c->ndops = first;
			if (is_array(c, t))
				t = ty_ptr(c, T(c, t)->base);
			else if (is_func(c, t))
				t = ty_ptr(c, t);
			for (i = 0; i < fd->npsyms; i++)
				if (c->syms[c->lists[fd->psyms + i]].name == d.name)
					break;
			if (i == fd->npsyms) {
				cc_error(c, d.loc, "parameter named '%.*s' is missing", IDF(d.name));
			} else {
				sym *y = &c->syms[c->lists[fd->psyms + i]];

				if (i < sizeof(seen) && seen[i]++)
					cc_error(c, d.loc, "redefinition of parameter '%.*s'", IDF(d.name));
				y->ty = t;
				y->loc = d.loc;
				if (ds.sc == SC_REGISTER)
					y->sc = SC_REGISTER;
			}
			if (!accept(c, LEX_TOKEN_COMMA))
				break;
		}
		expect_semi(c, "at end of declaration");
	}
	c->in_proto--;
	scope_close(c);
	if (!c->o->implicit_int)
		for (uint32_t i = 0; i < fd->npsyms && i < sizeof(seen); i++)
			if (!seen[i]) {
				const sym *y = &c->syms[c->lists[fd->psyms + i]];

				cc_error(c, y->loc, "parameter '%.*s' was not declared, defaults to 'int'; ISO C99 and later do not support implicit int", IDF(y->name));
			}
}

/* a declaration; block-scope ones append N_DECL statements to out */
/* the ';' ending a declaration; clang's ParseDeclGroup when it is missing: unless a
   declaration starts here, the rest is skipped through the next ';' */
static void decl_semi(cc *c, bool file)
{
	if (likely(accept(c, LEX_TOKEN_SEMICOLON)))
		return;
	if (expect_semi(c, file ? "after top level declarator" : "at end of declaration") && !is_decl_start(c, tok(c)))
		skip_stmt(c, false);
}

VPRIV void declaration(cc *c, vec *out, bool file)
{
	dspec ds;
	uint32_t dbeg = tok(c)->loc;
	bool attr_decl = is(c, LEX_TOKEN_LEFT_SQUARE) && peek(c, 1)->kind == LEX_TOKEN_LEFT_SQUARE;

	if (iskw(c, KW__Static_assert)) {
		static_assert_decl(c);
		return;
	}
	if (!decl_specs(c, &ds, true)) {
		if (attr_decl && accept(c, LEX_TOKEN_SEMICOLON))
			return;
		int k = peek(c, 1)->kind;

		if (file && ((is(c, LEX_TOKEN_IDENTIFIER) && !kwof(tok(c)) &&
			      (k == LEX_TOKEN_LEFT_PAREN || k == LEX_TOKEN_SEMICOLON || k == LEX_TOKEN_COMMA || k == LEX_TOKEN_EQUAL || k == LEX_TOKEN_LEFT_SQUARE)) ||
			     is(c, LEX_TOKEN_STAR))) {
			/* C89 implicit int: "f(x) { ... }", "x;" at file scope */
			memset(&ds, 0, sizeof(ds));
			ds.ty = prim(TY_INT);
			ds.loc = tok(c)->loc;
			if (!c->o->implicit_int) {
				uint32_t i = 0;

				while (peek(c, i)->kind == LEX_TOKEN_STAR)	/* clang: at the name in "*p = 1;" */
					i++;
				cc_error(c, peek(c, i)->kind == LEX_TOKEN_IDENTIFIER ? peek(c, i)->loc : ds.loc,
					 "type specifier missing, defaults to 'int'; ISO C99 and later do not support implicit int [-Wimplicit-int]");
			}
		} else if (is(c, LEX_TOKEN_IDENTIFIER) && !kwof(tok(c))) {
			unknown_type(c, &ds, true);
		} else {
			fatal(c, tok(c)->loc, "expected identifier or '('");
		}
	}
	if (accept(c, LEX_TOKEN_SEMICOLON)) {
		if (ds.constexpr_)
			cc_error(c, ds.loc, "constexpr can only be used in variable declarations");
		return;
	}
	for (;;) {
		declr d;
		uint32_t first, nops, s;
		qty t;

		declarator(c, &d, false, &first, &nops);
		t = apply_declarator(c, ds.ty, first, nops);
		if (ds.constexpr_ && !is_func(c, t))
			t = qualify(c, t, Q_CONST);
		if (file) {
			qty vm = t;

			while (is_array(c, vm) || KIND(c, vm) == TY_PTR) {
				if (T(c, vm)->flags & TF_VLA) {
					cc_error(c, d.loc, "variably modified type declaration not allowed at file scope");
					ds.invalid = true;
					break;
				}
				vm = T(c, vm)->base;
			}
		}
		c->ndops = first;
		asm_label(c, &d.at);
		if (unlikely(file && (ds.sc == SC_AUTO || (ds.sc == SC_REGISTER && !d.at.asmname))) && d.name) {
			/* (GNU: a global register variable, "register T x asm("reg")", is fine) */
			/* (clang: the declaration is invalid; a function's at the declaration's start) */
			if (is_func(c, t))
				cc_error(c, dbeg, "illegal storage class on function");
			else
				cc_error(c, d.loc, "illegal storage class on file-scoped variable");
			ds.invalid = true;
		}
		attribute_list(c, &d.at);
		if (ds.auto_type) {
			uint32_t v;

			if (!is(c, LEX_TOKEN_EQUAL) && c->o->std == 23 && d.name) {
				/* clang's text for C23 'auto' (vcc cannot tell it from __auto_type here) */
				dspec d2 = ds;

				cc_error(c, d.loc, "declaration of variable '%.*s' with deduced type 'auto' requires an initializer",
					 IDF(d.name));
				d2.auto_type = false, d2.invalid = true, d2.ty = prim(TY_INT);
				declare(c, &d2, &d, d2.ty, file);	/* (invalid: its uses say nothing more) */
				if (accept(c, LEX_TOKEN_COMMA))
					continue;
				decl_semi(c, file);
				return;
			}
			expect(c, LEX_TOKEN_EQUAL, "'=' (__auto_type requires an initializer)");
			v = decay(c, assign_expr(c));
			t = QT(QI(QUNQ(N(v)->ty)), QQ(ds.ty) | (ds.constexpr_ ? Q_CONST : 0));
			{
				dspec d2 = ds;

				d2.ty = t;
				s = declare(c, &d2, &d, t, file);
			}
			if (ds.constexpr_) {
				c->syms[s].flags |= SF_CONSTEXPR;
				if (!constexpr_type_ok(c, t))
					cc_error(c, d.loc, "constexpr variable cannot have type '%s'", tyname(c, t));
				constexpr_conversion(c, v, t);
				if (!constexpr_init_ok(c, v))
					cc_error(c, d.loc, "constexpr variable '%.*s' must be initialized by a constant expression", IDF(d.name));
			}
			c->syms[s].body = v;
			c->syms[s].flags |= SF_DEFINED;
			if (out) {
				uint32_t n = mk2(c, N_DECL, prim(TY_VOID), d.loc, s, v);

				N(n)->aux = 1; /* inferred type: its bounds are evaluated by the initializer */
				vpush(c, out, n);
			}
			if (decl_comma_typo(c))
				return;
			if (accept(c, LEX_TOKEN_COMMA))
				continue;
			decl_semi(c, file);
			return;
		}
		if (file && d.kr && is_func(c, t) && !is(c, LEX_TOKEN_SEMICOLON) && !is(c, LEX_TOKEN_COMMA) && !is(c, LEX_TOKEN_EQUAL))
			kr_params(c, &d);
		else if (d.kr)
			cc_error(c, d.plb, "a parameter list without types is only allowed in a function definition");
		if (KIND(c, t) == TY_SVE && ds.sc != SC_TYPEDEF &&
		    (file || ds.sc == SC_STATIC || ds.sc == SC_EXTERN || ds.thread)) {
			cc_error(c, d.loc, "non-local variable with sizeless type '%s'", tyname(c, t));
			ds.invalid = true;
		}
		s = declare(c, &ds, &d, t, file);
		if (ds.constexpr_) {
			if (c->syms[s].kind != S_VAR)
				cc_error(c, d.loc, "constexpr can only be used in variable declarations");
			else {
				c->syms[s].flags |= SF_CONSTEXPR;
				if (file)
					c->syms[s].sc = SC_STATIC;	/* C23: file-scope constexpr has internal linkage */
				if (!constexpr_type_ok(c, t))
					cc_error(c, d.loc, "constexpr variable cannot have type '%s'", tyname(c, t));
			}
			if (ds.sc == SC_EXTERN || ds.thread)
				cc_error(c, d.loc, "constexpr cannot be combined with extern or thread-local storage");
		}
		if (unlikely(ds.invalid | d.invalid)) {
			c->syms[s].flags |= SF_INVALID;	/* its type was an error: uses say nothing more */
			delayed_bug(c, d.loc, "an invalid declaration without an error");
		}
		if (d.has_func && c->syms[s].kind == S_FUNC && is_func(c, c->syms[s].ty) &&
		    d.npsyms == T(c, c->syms[s].ty)->n)
		{
			c->syms[s].psyms = d.psyms + 1, c->syms[s].plb = d.plb, c->syms[s].ple = d.ple;
			for (uint32_t i = 0; unlikely(c->nnparam) && i < d.npsyms; i++)	/* a nonnull parameter: checked at calls */
				if (c->syms[c->lists[d.psyms + i]].flags & SF_NONNULL)
					c->syms[s].flags |= SF_NONNULL;
		}
		if (d.at.vecsize || ds.at.vecsize || d.at.mode || ds.at.mode || d.at.fnabi || ds.at.fnabi)
			t = c->syms[s].ty;	/* declare made it a vector / applied mode or ms_abi/sysv_abi: initialize that */
		if (is_func(c, t) && is(c, LEX_TOKEN_LEFT_BRACE)) {
			if (!file)
				fatal(c, tok(c)->loc, "function definition is not allowed here");
			if (unlikely(c->syms[s].attrs && c->attr[c->syms[s].attrs].dll == CC_DLLIMPORT) && !ds.inline_)
				cc_error(c, d.loc, "dllimport cannot be applied to non-inline function definition");
			if (lazy_ok(c, s, &d))
				lazy_capture(c, s, &d);
			else
				function_def(c, s, &d);
			return;
		}
		if (unlikely(file && d.has_func) && is_func(c, t) && !is(c, LEX_TOKEN_SEMICOLON) && !is(c, LEX_TOKEN_COMMA) &&
		    !is(c, LEX_TOKEN_EQUAL) && !at_asm(c) && !iskw(c, KW___attribute__) && !iskw(c, KW___attribute) &&
		    !is_decl_start(c, tok(c))) {	/* a declaration next: the ';' is missing (decl_semi) */
			/* clang: "int f(int x) return x; }": the '{' is missing */
			cc_error(c, tok(c)->loc, "expected function body after function declarator");
			skip_stmt(c, true);
			return;
		}
		if (accept(c, LEX_TOKEN_EQUAL)) {
			sym *y = &c->syms[s];
			/* int a[]: the size of an earlier declaration (the composite type), if it had one */
			qty it = is_array(c, t) && (T(c, t)->flags & TF_INCOMPLETE) && is_array(c, y->ty) &&
				 !(T(c, y->ty)->flags & (TF_INCOMPLETE | TF_VLA)) ? QT(QI(y->ty), QQ(t)) : t;
			bool st = file || ds.sc == SC_STATIC;
			uint32_t v;
			int nerr = c->errors;
			uint32_t nconv0 = c->nconv;

			if (ds.sc == SC_TYPEDEF || is_func(c, t))
				cc_error(c, d.loc, "illegal initializer");
			else if (unlikely(y->attrs && c->attr[y->attrs].dll == CC_DLLIMPORT))
				cc_error(c, d.loc, "definition of dllimport data");
			if (!file && ds.sc == SC_EXTERN)
				cc_error(c, d.loc, "declaration of block scope identifier with linkage cannot have an initializer");
			if (unlikely(T(c, t)->flags & TF_VLA)) {
				/* only = {} (C23, a GNU extension before): zero it at run time (emit gen_vla_decl) */
				if (is(c, LEX_TOKEN_LEFT_BRACE) && peek(c, 1)->kind == LEX_TOKEN_RIGHT_BRACE) {
					next(c), next(c);
					v = mk(c, N_ZERO, t, d.loc);
				} else if (is(c, LEX_TOKEN_LEFT_BRACE)) {
					/* clang: at the outermost size, no range */
					cc_error(c, N(T(c, t)->n)->loc, "variable-sized object may not be initialized");
					skip_vla_init(c);
					v = 0;
				} else {
					/* clang: at the name, the initializer underlined */
					diag_span r = rng(c, assign_expr(c));

					report(c, "error", d.loc, &r, 1, NULL, 0, "variable-sized object may not be initialized");
					v = 0;
				}
			} else if (!file && ds.sc != SC_EXTERN && ds.sc != SC_TYPEDEF && !is_func(c, t) && !is_array(c, t) &&
				   !complete(c, t)) {
				/* clang: the variable is invalid, its initializer not checked */
				cc_error(c, d.loc, "variable has incomplete type '%s'", tyname(c, t));
				c->syms[s].flags |= SF_INVALID;
				if (is_record(c, t) || KIND(c, t) == TY_ENUM)
					note(c, c->tags[T(c, t)->aux].loc, "forward declaration of '%s'", tyname(c, t));
				if (is(c, LEX_TOKEN_LEFT_BRACE))
					skip_vla_init(c);
				else
					(void)assign_expr(c);
				v = 0;
			} else {
				uint8_t sw = c->cv_what;
				uint32_t sl = c->cv_loc;

				c->cv_what = CX_INIT, c->cv_loc = d.loc;
				c->syms[s].defloc = d.loc;
				uint16_t nr0 = c->syms[s].nref;

				uint32_t saved_constexpr = c->constexpr_init;

				c->constexpr_init = ds.constexpr_ ? s : 0;
				c->defer_rt = st || ds.constexpr_, c->ndefer = 0;
				v = initializer(c, &it, st);
				c->constexpr_init = saved_constexpr;
				if (c->syms[s].nref != nr0 && !st)
					c->uv_any |= 2;	/* (a self-reference: -Wuninitialized) */
				c->defer_rt = 0;
				c->cv_what = sw, c->cv_loc = sl;
			}
			y = &c->syms[s];
			if (QI(it) != QI(t)) {
				/* array size from the initializer */
				y->ty = QT(QI(it), QQ(y->ty));
			}
			if (unlikely(is_record(c, it) && !complete(c, it))) {
				y->flags |= SF_INVALID;	/* initializer said so: its uses are silent */
				delayed_bug(c, d.loc, "an incomplete initializer without an error");
			}
			if (y->body && file) {
				cc_error(c, d.loc, "redefinition of '%.*s'", IDF(d.name));
				note(c, y->loc, "previous definition is here");
			}
			y->body = v;
			y->flags |= SF_DEFINED;
			if (ds.constexpr_ && c->errors == nerr && !constexpr_init_ok(c, v))
				cc_error(c, d.loc, "constexpr variable '%.*s' must be initialized by a constant expression", IDF(d.name));
			{
				uint32_t bad = 0;

				/* not after an error in the initializer (clang: no follow-on error), but after
				   conversion errors, which are clang's warnings defaulting to errors */
				bool ok = true;

				if (st && !ds.constexpr_ && v && c->errors - c->nconv == nerr - nconv0) {
					c->sie = c->errors != nerr;
					ok = static_init_ok(c, y->ty, v, &bad);
					c->sie = false;
				}
				if (!ok) {
					diag_span r = rng(c, bad);

					report(c, "error", expr_loc(c, bad), &r, 1, NULL, 0, "initializer element is not a compile-time constant");
					/* the runtime-behaviour warnings only for an initializer that runs (not a constant) */
					for (int k = 0; k < c->ndefer; k++)
						cc_warnx(c, "division-by-zero", true, NULL, c->defer[k].loc, &c->defer[k].r, 1, NULL, 0,
							 "%s by zero is undefined", c->defer[k].rem ? "remainder" : "division");
				}
				c->ndefer = 0;
			}
			y = &c->syms[s];
			if (out && !st) {
				uint32_t n = mk2(c, N_DECL, prim(TY_VOID), d.loc, s, v);

				vpush(c, out, n);
			}
		} else {
			sym *y = &c->syms[s];

			if (ds.constexpr_ && y->kind == S_VAR)
				cc_error(c, d.loc, "constexpr variable '%.*s' must be initialized by a constant expression", IDF(d.name));
			if (y->kind == S_VAR && ds.sc != SC_EXTERN && ds.sc != SC_TYPEDEF) {
				if (file && unlikely(y->attrs && c->attr[y->attrs].dll == CC_DLLIMPORT && !(y->flags & SF_DEFINED)))
					y->sc = y->sc == SC_NONE ? SC_EXTERN : y->sc;	/* Windows: dllimport int x; declares, as extern */
				else if (file)
					y->flags |= SF_TENTATIVE;
				else if (!complete(c, y->ty) && ds.sc != SC_STATIC) {
					if (is_array(c, y->ty))
						cc_error(c, d.loc, "definition of variable with array type needs an explicit size or an initializer");
					else if (!is_void(c, y->ty)) {	/* void: declare said so */
						cc_error(c, d.loc, "variable has incomplete type '%s'", tyname(c, y->ty));
						fwd_note(c, y->ty);
					}
					y->flags |= SF_INVALID;
				}
				if (out && (y->sc == SC_AUTO || y->sc == SC_REGISTER)) {
					uint32_t n = mk2(c, N_DECL, prim(TY_VOID), d.loc, s, 0);

					vpush(c, out, n);
					if (uv_tracked(c, y->ty)) {
						c->uv_any |= 1;
						y->flags |= SF_UVPEND;
						if (c->nuv < 64)
							c->uvc[c->nuv] = c->cnd, c->uv[c->nuv++] = s;
					}
				}
			}
		}
		if (out && ds.sc == SC_TYPEDEF) {
			for (qty vm = t; is_array(c, vm) || is_ptr(c, vm); vm = T(c, vm)->base)
				if (T(c, vm)->flags & TF_VLA) {
					vpush(c, out, mk2(c, N_DECL, prim(TY_VOID), d.loc, s, 0));
					break;
				}
		}
		if (c->syms[s].kind == S_VAR)	/* the declaration's range, for notes */
			c->syms[s].plb = dbeg, c->syms[s].ple = c->prevloc;
		if (decl_comma_typo(c))
			return;
		if (accept(c, LEX_TOKEN_COMMA))
			continue;
		decl_semi(c, file);
		return;
	}
}
