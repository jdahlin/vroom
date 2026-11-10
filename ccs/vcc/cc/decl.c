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

VPRIV uint32_t new_attrs(cc *c, const attrs *a)
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
		merge_attrs(c, &c->attr[s->attrs], a);
	else
		s->attrs = new_attrs(c, a);
	if (a->aligned > s->align)
		s->align = a->aligned;
	if (a->weak) s->flags |= SF_WEAK;
	if (a->fmt) s->fmt = a->fmt;
	if (unlikely(a->ext)) {
		const attrx *x = &c->attrx[a->ext];

		if (x->depr) s->flags |= SF_DEPR;
		if (x->nonnull) s->flags |= SF_NONNULL;
		if (x->wur) s->flags |= SF_WUR;
	}
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

/* the typedef ds named, if t (an object's type or a function's return type) is that type */
VPRIV uint32_t ds_tdef(cc *c, const dspec *ds, qty t)
{
	if (KIND(c, t) == TY_FUNC)
		t = T(c, t)->base;
	return QUNQ(t) == QUNQ(ds->ty) ? ds->tds : 0;
}

static uint32_t declare(cc *c, const dspec *ds, declr *d, qty t, bool file)
{
	attrs at = d->at;
	uint32_t s, prev;
	int kind;

	merge_attrs(c, &at, &ds->at);
	if (ds->align > at.aligned)
		at.aligned = ds->align;
	if (d->at.mode && is_integer(c, t)) {
		int k = mode_kind(c, d->at.mode, is_signed(c, t));

		if (k)
			t = QT(k, QQ(t));
	}
	if (at.vecsize) {
		if (!is_arith(c, t) || ty_size(c, t) > at.vecsize || at.vecsize % ty_size(c, t))
			cc_error(c, d->loc, "invalid vector element type '%s'", tyname(c, t));
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
		if (at.unused || at.used)
			c->syms[s].flags |= SF_REF;	/* (no -Wunused-local-typedef) */
		if (unlikely(attrs_depr(c, &at)))
			apply_sym_attrs(c, &c->syms[s], &at);
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
	if (prev && unlikely(c->syms[prev].flags & SF_INVALID) && c->syms[prev].kind != kind)
		prev = 0;	/* a placeholder left by parser recovery (rec_restore): replaced */
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

		if (ds->tds)
			y->tdef = ds_tdef(c, ds, t);
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
/* ---------------- library builtins (cc/libbuiltins.inc, from clang) ---------------- */

static const struct libbi { const char *name, *lib, *sig; } libbis[] = {
#include "libbuiltins.inc"
};

/* one type of a clang builtin type string at *p (Builtins.def's encoding); false: not one vcc reads */
static bool lb_type(cc *c, const char **p, qty *out)
{
	const char *s = *p;
	int lng = 0, sgn = 0, uns = 0, cplx = 0;
	qty t;

	for (;; s++) {
		if (*s == 'L') lng++;
		else if (*s == 'S') sgn = 1;
		else if (*s == 'U') uns = 1;
		else if (*s == 'I') ;	/* a required constant */
		else if (*s == 'X') cplx = 1;
		else break;
	}
	switch (*s++) {
	case 'v': t = prim(TY_VOID); break;
	case 'b': t = prim(TY_BOOL); break;
	case 'c': t = prim(uns ? TY_UCHAR : sgn ? TY_SCHAR : TY_CHAR); break;
	case 's': t = prim(uns ? TY_USHORT : TY_SHORT); break;
	case 'i':
		t = prim(lng == 0 ? (uns ? TY_UINT : TY_INT) : lng == 1 ? (uns ? TY_ULONG : TY_LONG)
			 : lng == 2 ? (uns ? TY_ULLONG : TY_LLONG) : (uns ? TY_UINT128 : TY_INT128));
		lng = 0;
		break;
	case 'f': t = prim(TY_FLOAT); break;
	case 'd':
		if (lng > 1)
			return false;	/* __float128 */
		t = prim(lng ? TY_LDOUBLE : TY_DOUBLE);
		lng = 0;
		break;
	case 'z': t = prim(TY_ULONG); break;	/* size_t */
	case 'Y': t = prim(TY_LONG); break;	/* ptrdiff_t */
	case 'w': t = c->t_wchar; break;
	case 'P': t = prim(TY_VOID); break;	/* FILE: only ever as P*, a void * */
	case 'W': t = prim(TY_LONG); break;	/* int64_t */
	case 'Z': t = prim(TY_INT); break;	/* int32_t */
	default: return false;
	}
	if (lng)
		return false;
	if (cplx)
		t = ty_complex(c, t);
	for (;;) {
		if (*s == '*') {
			t = ty_ptr(c, t);
			s++;
			while (*s >= '0' && *s <= '9')
				s++;	/* address space */
		} else if (*s == 'C' || *s == 'D' || *s == 'R') {
			t = QT(QI(t), QQ(t) | (*s == 'C' ? Q_CONST : *s == 'D' ? Q_VOLATILE : Q_RESTRICT));
			s++;
		} else if (*s == '&') {
			return false;
		} else {
			break;
		}
	}
	*p = s;
	*out = t;
	return true;
}

/* a function type from a clang builtin type string (its result in *ret); false: not readable */
static bool lb_functype(cc *c, const char *p, qty *fty, qty *ret)
{
	qty par[16];
	uint32_t np = 0;
	int fl = 0;

	if (!lb_type(c, &p, ret))
		return false;
	while (*p) {
		if (*p == '.') {
			fl |= TF_VARIADIC;
			break;
		}
		if (np == 16 || !lb_type(c, &p, &par[np]))
			return false;
		np++;
	}
	*fty = ty_func(c, *ret, par, np, fl);
	return true;
}

/*
 * __builtin_X for a library function X that vcc has no code for: declared on
 * first use with clang's prototype, its symbol X (clang calls X unless it
 * folds or inlines it).  0: not a library builtin, or a type vcc cannot read.
 */
static const struct libbi *libbi_find(const char *n, size_t nl)
{
	size_t lo = 0, hi = sizeof(libbis) / sizeof(libbis[0]);

	if (nl < 11 || memcmp(n, "__builtin_", 10))
		return NULL;
	while (lo < hi) {
		size_t m = (lo + hi) / 2;
		int r = strncmp(libbis[m].name, n, nl);

		if (r == 0 && libbis[m].name[nl])
			r = 1;
		if (r == 0)
			return &libbis[m];
		if (r < 0)
			lo = m + 1;
		else
			hi = m;
	}
	return NULL;
}

/* __has_builtin: a library builtin vcc declares (the table holds only the readable ones) */
VPRIV bool lib_builtin_known(const char *n)
{
	return libbi_find(n, strlen(n)) != NULL;
}

VPRIV uint32_t lib_builtin(cc *c, uint32_t id, uint32_t loc)
{
	const struct libbi *b = libbi_find(idstr_(id), idlen_(id));

	if (!b)
		return 0;
	{
		qty fty, ret;

		if (!lb_functype(c, b->sig, &fty, &ret))
			return 0;
		{
			dspec ds = { .ty = ret, .sc = SC_EXTERN, .loc = loc };
			declr d = { .name = id, .loc = loc };
			uint32_t s = declare(c, &ds, &d, fty, true);
			size_t ll = strlen(b->lib), pre = c->o->target == PP_DARWIN ? 1 : 0;
			char *keep = halloc(c, ll + pre + 1);
			attrs a;

			keep[0] = '_';
			memcpy(keep + pre, b->lib, ll + 1);
			memset(&a, 0, sizeof(a));
			a.asmname = c->nstr + 1;
			c->strs[c->nstr++] = (cstr){ keep, (uint32_t)(ll + pre + 1), 1, T_STRING };
			apply_sym_attrs(c, &c->syms[s], &a);
			return s;
		}
	}
}

VPRIV uint32_t implicit_func(cc *c, uint32_t id, uint32_t loc)
{
	dspec ds = { .ty = prim(TY_INT), .sc = SC_EXTERN, .loc = loc };
	declr d = { .name = id, .loc = loc };

	return declare(c, &ds, &d, ty_func(c, prim(TY_INT), NULL, 0, TF_NOPROTO), c->depth == 0);
}

/* ---------------- library functions and names with a header (D5.4) ---------------- */

/* clang's library functions with their header (cc/libfuncs.inc, gen-libbuiltins.py) */
static const struct libfn { const char *name, *hdr, *sig; uint8_t gnu; } libfns[] = {
#include "libfuncs.inc"
};
/* other names standard headers declare (cc/libnames.inc, by hand) */
static const struct libname { const char *name, *hdr; } libnames[] = {
#include "libnames.inc"
};

#define BSEARCH(tab, n, nl, out) do {						\
	size_t lo_ = 0, hi_ = sizeof(tab) / sizeof((tab)[0]);			\
	(out) = NULL;								\
	while (lo_ < hi_) {							\
		size_t m_ = (lo_ + hi_) / 2;					\
		int r_ = strncmp((tab)[m_].name, (n), (nl));			\
		if (r_ == 0 && (tab)[m_].name[nl])				\
			r_ = 1;							\
		if (r_ == 0) { (out) = &(tab)[m_]; break; }			\
		if (r_ < 0) lo_ = m_ + 1; else hi_ = m_;			\
	}									\
} while (0)

static const struct libfn *libfn_find(cc *c, uint32_t id)
{
	const struct libfn *f;

	if (c->o->no_builtin)
		return NULL;	/* -ffreestanding / -fno-builtin: clang knows no library functions */
	BSEARCH(libfns, idstr_(id), (size_t)idlen_(id), f);
	return f && f->gnu && c->o->strict_std ? NULL : f;
}

/* the standard header that declares id, or NULL */
VPRIV const char *lib_header(cc *c, uint32_t id)
{
	const struct libfn *f = libfn_find(c, id);
	const struct libname *n;

	if (f)
		return f->hdr;
	BSEARCH(libnames, idstr_(id), (size_t)idlen_(id), n);
	return n ? n->hdr : NULL;
}

/* "#include <hdr>" as a fix-it on d, where the file's includes are (or at its top) */
VPRIV void include_fix(cc *c, vdiag *d, uint32_t loc, const char *hdr)
{
	uint32_t at = pp_include_point(c->p, loc);
	char b[96];

	if (at == UINT32_MAX)
		return;
	snprintf(b, sizeof b, "#include <%s>\n", hdr);
	pp_diag_fix(c->p, d, at, at, b, VDA_MAYBE);
}

/*
 * A call to an undeclared library function (clang's Builtins with a header):
 * declared with the library's prototype, as clang, after clang's diagnostic
 * ("call to undeclared library function 'memcpy' with type ...", in C89 a
 * warning) and the note naming the header.  0: not one (or a prototype vcc
 * cannot read): the caller declares "int id()".
 */
VPRIV uint32_t lib_func(cc *c, uint32_t id, uint32_t loc)
{
	static const char *const none[] = { NULL };
	const struct libfn *f = libfn_find(c, id);
	qty fty, ret;
	int lv;

	if (!f || strchr(f->sig, 'P') || !lb_functype(c, f->sig, &fty, &ret) || !strcmp(f->name, "alloca"))
		return 0;	/* FILE * parameters: vcc has no FILE; alloca is the builtin */
	/* C89, or -Wno-error=implicit-function-declaration: clang's warning; else an error */
	lv = c->o->std == 89 || c->o->implicit_fn ? pp_diag_wlevel(c->p, "implicit-function-declaration", true, none, loc)
		: VDL_ERROR;
	if (lv >= 0) {
		const char *t = tyname(c, fty);
		vdiag *d;

		pp_diag_pc = __builtin_return_address(0);
		if (lv == VDL_ERROR)
			error_limit(c), c->errors++;
		c->ndiag++;
		if (c->o->std == 89)
			d = pp_diag_begin(c->p, lv, loc, "implicitly declaring library function '%.*s' with type '%s'", IDF(id), t);
		else
			d = pp_diag_begin(c->p, lv, loc, "call to undeclared library function '%.*s' with type '%s'; ISO C99 and "
					  "later do not support implicit function declarations", IDF(id), t);
		pp_diag_flag(d, "implicit-function-declaration");
		d->werror = lv == VDL_ERROR && (c->o->std == 89 || c->o->implicit_fn);
		include_fix(c, d, loc, f->hdr);
		pp_diag_emit(c->p, d);
		note(c, loc, "include the header <%s> or explicitly provide a declaration for '%.*s'", f->hdr, IDF(id));
	}
	{
		dspec ds = { .ty = ret, .sc = SC_EXTERN, .loc = loc };
		declr d = { .name = id, .loc = loc };

		return declare(c, &ds, &d, fty, c->depth == 0);
	}
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
/* the ';' ending a declaration; clang's ParseDeclGroup when it is missing: unless a
   declaration starts here, the rest is skipped through the next ';' */
static void decl_semi(cc *c, bool file)
{
	if (likely(accept(c, T_SEMI)))
		return;
	if (expect_semi(c, file ? "after top level declarator" : "at end of declaration") && !is_decl_start(c, tok(c)))
		skip_stmt(c, false);
}

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
			if (!c->o->implicit_int) {
				uint32_t i = 0;

				while (peek(c, i)->kind == T_STAR)	/* clang: at the name in "*p = 1;" */
					i++;
				cc_error(c, peek(c, i)->kind == T_IDENT ? peek(c, i)->loc : ds.loc,
					 "type specifier missing, defaults to 'int'; ISO C99 and later do not support implicit int [-Wimplicit-int]");
			}
		} else if (is(c, T_IDENT) && !kwof(tok(c))) {
			unknown_type(c, &ds, true);
		} else {
			fatal(c, tok(c)->loc, "expected identifier or '('");
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

			if (!is(c, T_EQUAL) && c->o->std == 23 && d.name) {
				/* clang's text for C23 'auto' (vcc cannot tell it from __auto_type here) */
				dspec d2 = ds;

				cc_error(c, d.loc, "declaration of variable '%.*s' with deduced type 'auto' requires an initializer",
					 IDF(d.name));
				d2.auto_type = false, d2.invalid = true, d2.ty = prim(TY_INT);
				declare(c, &d2, &d, d2.ty, file);	/* (invalid: its uses say nothing more) */
				if (accept(c, T_COMMA))
					continue;
				decl_semi(c, file);
				return;
			}
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
			if (decl_comma_typo(c))
				return;
			if (accept(c, T_COMMA))
				continue;
			decl_semi(c, file);
			return;
		}
		if (file && d.kr && is_func(c, t) && !is(c, T_SEMI) && !is(c, T_COMMA) && !is(c, T_EQUAL))
			kr_params(c, &d);
		else if (d.kr)
			cc_error(c, d.plb, "a parameter list without types is only allowed in a function definition");
		s = declare(c, &ds, &d, t, file);
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
		if (unlikely(file && d.has_func) && is_func(c, t) && !is(c, T_SEMI) && !is(c, T_COMMA) &&
		    !is(c, T_EQUAL) && !at_asm(c) && !iskw(c, KW___attribute__) && !iskw(c, KW___attribute) &&
		    !is_decl_start(c, tok(c))) {	/* a declaration next: the ';' is missing (decl_semi) */
			/* clang: "int f(int x) return x; }": the '{' is missing */
			cc_error(c, tok(c)->loc, "expected function body after function declarator");
			skip_stmt(c, true);
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
			if (out && !st) {
				uint32_t n = mk2(c, N_DECL, prim(TY_VOID), d.loc, s, v);

				vpush(c, out, n);
			}
		} else {
			sym *y = &c->syms[s];

			if (y->kind == S_VAR && ds.sc != SC_EXTERN && ds.sc != SC_TYPEDEF) {
				if (file)
					y->flags |= SF_TENTATIVE;
				else if (!complete(c, y->ty) && ds.sc != SC_STATIC) {
					if (!is_void(c, y->ty)) {	/* void: declare said so */
						cc_error(c, d.loc, "variable has incomplete type '%s'", tyname(c, y->ty));
						fwd_note(c, y->ty);
					}
					y->flags |= SF_INVALID;
				}
				if (out && (y->sc == SC_AUTO || y->sc == SC_REGISTER)) {
					uint32_t n = mk2(c, N_DECL, prim(TY_VOID), d.loc, s, 0);

					vpush(c, out, n);
				}
			}
		}
		if (c->syms[s].kind == S_VAR)	/* the declaration's range, for notes */
			c->syms[s].plb = dbeg, c->syms[s].ple = c->prevloc;
		if (decl_comma_typo(c))
			return;
		if (accept(c, T_COMMA))
			continue;
		decl_semi(c, file);
		return;
	}
}
