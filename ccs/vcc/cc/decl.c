/* vcc front end: declarations */
#include "ccimpl.h"

/* the braced initializer of a VLA, already diagnosed: skipped unchecked */
static void skip_vla_init(cc *c)
{
	int depth = 0;

	do {
		if (is(c, T_LBRACE))
			depth++;
		else if (is(c, T_RBRACE))
			depth--;
		else if (is(c, T_EOF))
			fatal(c, tok(c)->loc, "expected '}'");
		next(c);
	} while (depth > 0);
}

/* ---------------- declarations ---------------- */

static uint32_t new_attrs(cc *c, const attrs *a)
{
	uint32_t i = c->nattr++;

	c->attr[i] = *a;
	return i;
}

VPRIV void apply_sym_attrs(cc *c, sym *s, const attrs *a)
{
	if (attrs_empty(a))
		return;		/* (no record: sym_attrs() in emit.c reads attrs 0 as none) */
	if (s->attrs)
		merge_attrs(&c->attr[s->attrs], a);
	else
		s->attrs = new_attrs(c, a);
	if (a->aligned > s->align)
		s->align = a->aligned;
	if (a->weak) s->flags |= SF_WEAK;
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

/* ---------------- #pragma weak ---------------- */

/*
 * clang's rules: "#pragma weak n" makes the external function or variable n
 * weak, now or at its first declaration; "#pragma weak a = t" declares a, a
 * weak alias of t, now or when t is first declared.  Pending ones left at the
 * end are reported.  Like clang's code generator, a function or variable
 * already defined or named before the pragma keeps its binding.
 */
static void weak_push(cc *c, uint32_t key, uint32_t alias, uint32_t loc, uint32_t s)
{
	if (c->npweak == c->cpweak) {
		c->cpweak = c->cpweak ? 2 * c->cpweak : 8;
		c->pweak = realloc(c->pweak, c->cpweak * sizeof(*c->pweak));
	}
	c->pweak[c->npweak++] = (struct pweak){ key, alias, loc, s };
}

static uint32_t id_string(cc *c, uint32_t id)
{
	int l;
	const char *s = idname(id, &l);
	char *k = halloc(c, (size_t)l + 1);

	memcpy(k, s, (size_t)l);
	k[l] = 0;
	c->strs[c->nstr++] = (cstr){ k, (uint32_t)l + 1, 1, T_STRING };
	return c->nstr;		/* (attrs hold index + 1) */
}

/* alias: a weak alias of t (clang: a clone of t's declaration) */
static void weak_alias(cc *c, uint32_t t, uint32_t alias, uint32_t loc)
{
	attrs at = { 0 };
	uint32_t a = file_scope_sym(c, alias);

	at.weak = 1;
	at.alias = id_string(c, c->syms[t].name);
	if (!a) {
		a = sym_new(c, c->syms[t].kind, alias, c->syms[t].ty, loc);
		c->syms[a].flags |= SF_GLOBAL;
		c->syms[a].depth = 0;
		if (c->depth == 0)
			sym_bind(c, a);
		c->toplevel[c->ntoplevel++] = a;
	}
	apply_sym_attrs(c, &c->syms[a], &at);
	weak_push(c, t, alias, loc, a);
}

static bool weak_idc(char ch)
{
	return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == '_' || ch == '$';
}

static const char *weak_skip(const char *s, const char *e)
{
	while (s < e && (*s == ' ' || *s == '\t'))
		s++;
	return s;
}

/* the identifier at *s, interned, or 0 */
static uint32_t weak_ident(const char **s, const char *e)
{
	const char *b = *s, *q = b;
	char buf[256 + 16] = { 0 };

	if (q < e && !(*q >= '0' && *q <= '9'))
		while (q < e && weak_idc(*q))
			q++;
	if (q == b || q - b > 256)
		return 0;
	memcpy(buf, b, (size_t)(q - b));
	*s = q;
	return vintern(buf, (size_t)(q - b), vintern_hash(buf, (size_t)(q - b)));
}

static bool fn_or_var(const sym *y)
{
	return y->kind == S_FUNC || y->kind == S_VAR;
}

/* s .. e: the text after "weak"; base: the directive's text, at loc + 1 */
VPRIV void pragma_weak(cc *c, const char *s, const char *e, const char *base, uint32_t loc)
{
	uint32_t name, target = 0, nl, k;

#define WLOC(p) (loc + 1 + (uint32_t)((p) - base))
	s = weak_skip(s, e);
	nl = WLOC(s);
	if (!(name = weak_ident(&s, e))) {
		cc_warnx(c, "ignored-pragmas", true, NULL, WLOC(s), NULL, 0, NULL, 0, "expected identifier in '#pragma weak' - ignored");
		return;
	}
	s = weak_skip(s, e);
	if (s < e && *s == '=') {
		s = weak_skip(s + 1, e);
		if (!(target = weak_ident(&s, e))) {
			cc_warnx(c, "ignored-pragmas", true, NULL, WLOC(s), NULL, 0, NULL, 0, "expected identifier in '#pragma weak' - ignored");
			return;
		}
		s = weak_skip(s, e);
	}
	if (s < e) {
		cc_warnx(c, "ignored-pragmas", true, NULL, WLOC(s), NULL, 0, NULL, 0, "extra tokens at end of '#pragma weak' - ignored");
		return;
	}
#undef WLOC
	dc_kill(c);	/* (the pending list is not in the declaration cache's snapshots) */
	k = file_scope_sym(c, target ? target : name);
	if (k && fn_or_var(&c->syms[k])) {
		sym *y = &c->syms[k];

		if (target)
			weak_alias(c, k, name, nl);
		else if (y->sc != SC_STATIC && !(y->flags & (SF_DEFINED | SF_REF)))
			y->flags |= SF_WEAK;
		return;
	}
	weak_push(c, target ? target : name, target ? name : 0, nl, 0);
}

/* s was just declared: the pragmas waiting for it */
VPRIV void weak_declared(cc *c, uint32_t s)
{
	const sym *y = &c->syms[s];

	if (!fn_or_var(y) || !(y->flags & SF_GLOBAL) || y->sc == SC_STATIC)
		return;
	for (uint32_t i = 0; i < c->npweak;) {
		struct pweak w = c->pweak[i];

		if (w.sym || w.key != y->name) {
			i++;
			continue;
		}
		memmove(&c->pweak[i], &c->pweak[i + 1], (c->npweak - i - 1) * sizeof(*c->pweak));
		c->npweak--;
		if (w.alias)
			weak_alias(c, s, w.alias, w.loc);
		else
			c->syms[s].flags |= SF_WEAK;
	}
}

/* the end of the TU: never declared, or an alias of nothing */
VPRIV void weak_finish(cc *c)
{
	int st = pp_wstate(c->p, "", true, NULL);

	for (uint32_t i = 0; i < c->npweak; i++) {
		const struct pweak *w = &c->pweak[i];

		if (!w->sym && st && !pp_in_system_header(c->p, w->loc))
			report(c, st == 2 ? "error" : "warning", w->loc, NULL, 0, NULL, 0, "weak identifier '%.*s' never declared%s",
			       IDF(w->key), st == 2 ? " [-Werror]" : "");
	}
	for (uint32_t i = 0; c->o->emit && i < c->npweak; i++) {	/* (clang: its code generator) */
		const struct pweak *w = &c->pweak[i];
		const sym *t = &c->syms[w->key];

		if (w->sym && !(t->kind == S_FUNC ? (t->flags & SF_DEFINED) && t->body : (t->flags & (SF_DEFINED | SF_TENTATIVE)))) {
			cc_error(c, w->loc, "alias must point to a defined variable or function");
			note(c, w->loc, "the function or variable specified in an alias must refer to its mangled name");
		}
	}
}

static uint32_t declare(cc *c, const dspec *ds, declr *d, qty t, bool file)
{
	attrs at = d->at;
	uint32_t s, prev;
	int kind;

	merge_attrs(&at, &ds->at);
	if (ds->align > at.aligned)
		at.aligned = ds->align;
	if (d->at.mode && is_integer(c, t)) {
		int k = mode_kind(c, d->at.mode, is_signed(c, t));

		if (k)
			t = QT(k, QQ(t));
	}
	if (at.vecsize) {
		if (!is_arith(c, t) || ty_size(c, t) > at.vecsize || at.vecsize % ty_size(c, t))
			cc_error(c, d->loc, "invalid vector element type");
		else
			t = QT(QI(ty_vector(c, t, at.vecsize)), QQ(t));
	}
	if (ds->sc == SC_TYPEDEF) {
		if (at.aligned)
			t = ty_aligned(c, t, at.aligned);
		if (at.transparent && KIND(c, t) == TY_UNION)
			c->ty[QI(t)].flags |= TF_TRANSPARENT;
		prev = lookup(c, d->name);
		if (prev && c->syms[prev].depth == c->depth && c->syms[prev].kind == S_TYPEDEF) {
			if (!compatible(c, c->syms[prev].ty, t) || QQ(c->syms[prev].ty) != QQ(t))
				cc_error(c, d->loc, "typedef redefinition with different types");
			return prev;
		}
		s = sym_new(c, S_TYPEDEF, d->name, t, d->loc);
		c->syms[s].sc = SC_TYPEDEF;
		sym_bind(c, s);
		if (is_record(c, t) || KIND(c, t) == TY_ENUM) {
			tag *r = &c->tags[T(c, t)->aux];

			if (!r->name && !r->typedef_name && file)
				r->typedef_name = d->name;
		}
		if (file)
			c->toplevel[c->ntoplevel++] = s;
		return s;
	}
	kind = is_func(c, t) ? S_FUNC : S_VAR;
	if (kind == S_VAR && is_void(c, t) && ds->sc != SC_EXTERN)
		cc_error(c, d->loc, "variable has incomplete type 'void'");
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
			prev = file_scope_sym(c, d->name);
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
	if (prev) {
		sym *y = &c->syms[prev];

		if (y->kind != kind) {
			cc_error(c, d->loc, "redefinition of '%.*s' as different kind of symbol", IDF(d->name));
			note(c, y->loc, "previous definition is here");
		} else if (!compatible(c, y->ty, t)) {
			cc_error(c, d->loc, "conflicting types for '%.*s'", IDF(d->name));
			note(c, y->loc, "previous declaration is here");
		} else {
			y->ty = QT(QI(composite(c, y->ty, t)), QQ(y->ty) | QQ(t));
			if (ds->sc == SC_STATIC && kind == S_FUNC)
				y->sc = SC_STATIC;
			if (ds->inline_)
				y->flags |= SF_INLINE | (ds->sc != SC_EXTERN ? SF_INLINE_NOEXTERN : 0);
			apply_sym_attrs(c, y, &at);
			if (!file && c->syms[prev].depth != c->depth) {
				/* block-scope extern: a local binding that names the global */
				s = sym_new(c, kind, d->name, y->ty, d->loc);
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

		y->sc = (uint8_t)(ds->sc == SC_TYPEDEF ? SC_NONE : ds->sc);
		if (file || ds->sc == SC_EXTERN || kind == S_FUNC)
			y->flags |= SF_GLOBAL;
		if (ds->inline_)
			y->flags |= SF_INLINE | (ds->sc != SC_EXTERN ? SF_INLINE_NOEXTERN : 0);
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

/* C89: a call to an undeclared id declares "extern int id();" in this scope */
VPRIV uint32_t implicit_func(cc *c, uint32_t id, uint32_t loc)
{
	dspec ds = { .ty = prim(TY_INT), .sc = SC_EXTERN, .loc = loc };
	declr d = { .name = id, .loc = loc };

	return declare(c, &ds, &d, ty_func(c, prim(TY_INT), NULL, 0, TF_NOPROTO), c->depth == 0);
}

/* the declarations between a K&R definition's identifier list and its body */
static void kr_params(cc *c, const declr *fd)
{
	uint8_t seen[256] = { 0 };

	/* tags declared here belong to the function, not the file (approximation: not seen by the body) */
	scope_open(c);
	while (!is(c, T_LBRACE)) {
		dspec ds;

		if (!decl_specs(c, &ds, true)) {
			if (!is(c, T_IDENT) || kwof(tok(c)))
				fatal(c, tok(c)->loc, "expected ';' after top level declarator");
			unknown_type(c, &ds);
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
			if (!accept(c, T_COMMA))
				break;
		}
		expect_semi(c, "at end of declaration");
	}
	scope_close(c);
	if (!c->o->implicit_int)
		for (uint32_t i = 0; i < fd->npsyms && i < sizeof(seen); i++)
			if (!seen[i]) {
				const sym *y = &c->syms[c->lists[fd->psyms + i]];

				cc_error(c, y->loc, "parameter '%.*s' was not declared, defaults to 'int'; ISO C99 and later do not support implicit int", IDF(y->name));
			}
}

/* a declaration; block-scope ones append N_DECL statements to out */
VPRIV void declaration(cc *c, vec *out, bool file)
{
	dspec ds;
	uint32_t dbeg = tok(c)->loc;

	if (iskw(c, KW__Static_assert)) {
		static_assert_decl(c);
		return;
	}
	if (!decl_specs(c, &ds, true)) {
		int k = peek(c, 1)->kind;

		if (file && ((is(c, T_IDENT) && !kwof(tok(c)) &&
			      (k == T_LPAREN || k == T_SEMI || k == T_COMMA || k == T_EQUAL || k == T_LSQUARE)) ||
			     is(c, T_STAR))) {
			/* C89 implicit int: "f(x) { ... }", "x;" at file scope */
			memset(&ds, 0, sizeof(ds));
			ds.ty = prim(TY_INT);
			ds.loc = tok(c)->loc;
			if (!c->o->implicit_int)
				cc_error(c, ds.loc, "type specifier missing, defaults to 'int'; ISO C99 and later do not support implicit int");
		} else if (is(c, T_IDENT) && !kwof(tok(c))) {
			unknown_type(c, &ds);
		} else {
			char b[64];

			fatal(c, tok(c)->loc, "expected declaration before '%s'", tokspell(tok(c), b, sizeof(b)));
		}
	}
	if (accept(c, T_SEMI))
		return;
	for (;;) {
		declr d;
		uint32_t first, nops, s;
		qty t;

		declarator(c, &d, false, &first, &nops);
		t = apply_declarator(c, ds.ty, first, nops);
		c->ndops = first;
		asm_label(c, &d.at);
		attribute_list(c, &d.at);
		if (ds.auto_type) {
			uint32_t v;

			expect(c, T_EQUAL, "'=' (__auto_type requires an initializer)");
			v = decay(c, assign_expr(c));
			t = QT(QI(QUNQ(N(v)->ty)), QQ(ds.ty));
			{
				dspec d2 = ds;

				d2.ty = t;
				s = declare(c, &d2, &d, t, file);
			}
			c->syms[s].body = v;
			c->syms[s].flags |= SF_DEFINED;
			if (out) {
				uint32_t n = mk2(c, N_DECL, prim(TY_VOID), d.loc, s, v);

				vpush(c, out, n);
			}
			if (accept(c, T_COMMA))
				continue;
			expect_semi(c, "at end of declaration");
			return;
		}
		if (file && d.kr && is_func(c, t) && !is(c, T_SEMI) && !is(c, T_COMMA) && !is(c, T_EQUAL))
			kr_params(c, &d);
		s = declare(c, &ds, &d, t, file);
		if (d.has_func && c->syms[s].kind == S_FUNC && is_func(c, c->syms[s].ty) &&
		    d.npsyms == T(c, c->syms[s].ty)->n)
			c->syms[s].psyms = d.psyms + 1, c->syms[s].plb = d.plb, c->syms[s].ple = d.ple;
		if (d.at.vecsize || ds.at.vecsize || d.at.mode || ds.at.mode)
			t = c->syms[s].ty;	/* declare made it a vector / applied mode: initialize that */
		if (is_func(c, t) && is(c, T_LBRACE)) {
			if (!file)
				fatal(c, tok(c)->loc, "function definition is not allowed here");
			if (lazy_ok(c, s, &d))
				lazy_capture(c, s, &d);
			else
				function_def(c, s, &d);
			return;
		}
		if (accept(c, T_EQUAL)) {
			sym *y = &c->syms[s];
			qty it = t;
			bool st = file || ds.sc == SC_STATIC;
			uint32_t v;

			if (ds.sc == SC_TYPEDEF || is_func(c, t))
				cc_error(c, d.loc, "illegal initializer");
			if (!file && ds.sc == SC_EXTERN)
				cc_error(c, d.loc, "declaration of block scope identifier with linkage cannot have an initializer");
			if (unlikely(T(c, t)->flags & TF_VLA)) {
				/* only = {} (C23, a GNU extension before): zero it at run time (emit gen_vla_decl) */
				if (is(c, T_LBRACE) && peek(c, 1)->kind == T_RBRACE) {
					next(c), next(c);
					v = mk(c, N_ZERO, t, d.loc);
				} else if (is(c, T_LBRACE)) {
					/* clang: at the outermost size, no range */
					cc_error(c, N(T(c, t)->n)->loc, "variable-sized object may not be initialized");
					skip_vla_init(c);
					v = 0;
				} else {
					/* clang: at the name, the initializer underlined */
					pp_span r = rng(c, assign_expr(c));

					report(c, "error", d.loc, &r, 1, NULL, 0, "variable-sized object may not be initialized");
					v = 0;
				}
			} else {
				uint8_t sw = c->cv_what;
				uint32_t sl = c->cv_loc;

				c->cv_what = CX_INIT, c->cv_loc = d.loc;
				v = initializer(c, &it, st);
				c->cv_what = sw, c->cv_loc = sl;
			}
			y = &c->syms[s];
			if (QI(it) != QI(t)) {
				/* array size from the initializer */
				y->ty = QT(QI(it), QQ(y->ty));
			}
			if (y->body && file) {
				cc_error(c, d.loc, "redefinition of '%.*s'", IDF(d.name));
				note(c, y->loc, "previous definition is here");
			}
			y->body = v;
			y->flags |= SF_DEFINED;
			if (out && !st) {
				uint32_t n = mk2(c, N_DECL, prim(TY_VOID), d.loc, s, v);

				vpush(c, out, n);
			}
		} else {
			sym *y = &c->syms[s];

			if (y->kind == S_VAR && ds.sc != SC_EXTERN && ds.sc != SC_TYPEDEF) {
				if (file)
					y->flags |= SF_TENTATIVE;
				else if (!complete(c, y->ty) && ds.sc != SC_STATIC)
					cc_error(c, d.loc, "variable has incomplete type");
				if (out && (y->sc == SC_AUTO || y->sc == SC_REGISTER)) {
					uint32_t n = mk2(c, N_DECL, prim(TY_VOID), d.loc, s, 0);

					vpush(c, out, n);
				}
			}
		}
		if (c->syms[s].kind == S_VAR)	/* the declaration's range, for notes */
			c->syms[s].plb = dbeg, c->syms[s].ple = c->prevloc;
		if (accept(c, T_COMMA))
			continue;
		expect_semi(c, "at end of declaration");
		return;
	}
}
