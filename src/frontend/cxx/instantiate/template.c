/* C++ templates: templates, the instance cache, spelling */
#include "instantiate.h"

VPRIV cxx_template *cxx_template_of(cxx_entity *e)
{
	if (!e->tmpl)
		e->tmpl = calloc(1, sizeof *e->tmpl);
	return e->tmpl;
}

VPRIV void cxx_template_define_path(cxx *c, cxx_entity *e, cxx_template *t)
{
	free(t->path);
	t->path = NULL;
	if (c->name_path_valid && e->name > 1) {
		const vident *v = &vintern_ents[e->name];
		size_t n = c->name_path_len;

		t->path = malloc(n + 2 + v->len + 1);
		memcpy(t->path, c->name_path, n);
		if (n)
			t->path[n++] = ':', t->path[n++] = ':';
		memcpy(t->path + n, v->s, v->len);
		t->path[n + v->len] = 0;
	}
}

VPRIV void cxx_template_define(cxx *c, cxx_entity *e, cxx_scope *params, cxx_scope *home, uint32_t pos, uint32_t end, bool alias)
{
	cxx_template *t;

	if (!e || c->tentative)
		return;
	/* (inside an instance too: a member template of X<int>, its home X<int>'s scope, where the instance's
	   own parameters are bound; it has no name of its own in the layout dump) */
	t = cxx_template_of(e);
	t->params = params, t->home = home, t->pos = pos, t->end = end, t->is_alias = alias;
	if (!c->instantiating)
		cxx_template_define_path(c, e, t);
}

/* a declaration before the definition: its parameters' defaults count (template<class T = void> struct X;) */
VPRIV void cxx_template_declared(cxx *c, cxx_entity *e, cxx_scope *params)
{
	cxx_template *t;

	if (!e || c->tentative || c->instantiating || !params || params->kind != CXX_SCOPE_TEMPLATE)
		return;
	t = cxx_template_of(e);
	if (!t->declared)
		t->declared = params;
	if (!t->home)
		t->home = params->parent;	/* where its defaults' names are looked up, until it is defined */
	if (!t->path && !t->params)	/* its name, for spelling instances of its partial specializations */
		cxx_template_define_path(c, e, t);
}

VPRIV void cxx_template_partial(cxx *c, cxx_entity *e, cxx_scope *params, cxx_scope *home, uint32_t less_pos, uint32_t key_pos, uint32_t req_pos)
{
	cxx_template *t;

	if (!e || c->tentative || c->instantiating)
		return;
	t = cxx_template_of(e);
	t->has_partial = 1;
	cxx_partial *np = calloc(1, sizeof *np);

	t->partials = realloc(t->partials, (t->npartials + 1) * sizeof *t->partials);
	np->params = params, np->home = home, np->less_pos = less_pos, np->key_pos = key_pos, np->req_pos = req_pos;
	t->partials[t->npartials] = np;
	t->npartials++;
}

VPRIV void cxx_tparam_add(cxx *c, int kind, uint32_t name, bool pack, uint32_t def_b, uint32_t def_e)
{
	cxx_scope *s = c->scope;

	if (s->kind != CXX_SCOPE_TEMPLATE || c->tentative)
		return;
	s->tparams = realloc(s->tparams, (s->ntparams + 1) * sizeof *s->tparams);
	s->tparams[s->ntparams++] = (cxx_tparam){ .kind = (uint8_t)kind, .pack = pack, .name = name, .def_b = def_b, .def_e = def_e };
}

/* ---- the cache --------------------------------------------------------------------------- */

VPRIV uint32_t cxx_instance_hash(const cxx_template *t, const cxx_targ *a, uint32_t n)
{
	uint64_t h = (uintptr_t)t * 0x9E3779B97F4A7C15ull;

	for (uint32_t i = 0; i < n; i++)
		h = (h ^ ((uint64_t)a[i].kind << 56 ^ (a[i].kind == 1 ? a[i].t : (uint64_t)a[i].v ^ (a[i].kind == 2 ? (uint64_t)a[i].t << 20 : 0)))) *
		    0x100000001B3ull;
	return (uint32_t)(h ^ (h >> 32));
}

VPRIV bool cxx_args_equal(const cxx_targ *a, const cxx_targ *b, uint32_t n)
{
	for (uint32_t i = 0; i < n; i++)
		if (a[i].kind != b[i].kind || (a[i].kind == 1 ? a[i].t != b[i].t : a[i].v != b[i].v) || (a[i].kind == 2 && a[i].t != b[i].t))
			return false;	/* (a value's enumeration type is part of it) */
	return true;
}

/* the table holds pointers: an entry never moves (entities and tag_instance point at it) */
VPRIV cxx_instance *cxx_instance_find(cxx *c, const cxx_template *t, const cxx_targ *a, uint32_t n, uint32_t h)
{
	cxx_instance **tab = (cxx_instance **)c->instances;

	if (!c->cinstances)
		return NULL;
	for (uint32_t i = h & (c->cinstances - 1);; i = (i + 1) & (c->cinstances - 1)) {
		cxx_instance *x = tab[i];

		if (!x)
			return NULL;
		if (x->hash == h && x->t == t && x->nargs == n && cxx_args_equal(x->args, a, n))
			return x;
	}
}

VPRIV cxx_instance *cxx_instance_add(cxx *c, cxx_template *t, const cxx_targ *a, uint32_t n, uint32_t h)
{
	cxx_instance **tab = (cxx_instance **)c->instances, *x;

	if ((c->ninstances + 1) * 2 > c->cinstances) {
		cxx_instance **old = tab;
		uint32_t oc = c->cinstances;

		c->cinstances = oc ? oc * 2 : 256;
		tab = calloc(c->cinstances, sizeof *tab);
		for (uint32_t i = 0; i < oc; i++)
			if (old[i])
				for (uint32_t k = old[i]->hash & (c->cinstances - 1);; k = (k + 1) & (c->cinstances - 1))
					if (!tab[k]) {
						tab[k] = old[i];
						break;
					}
		free(old);
		c->instances = (void *)tab;
	}
	x = calloc(1, sizeof *x);
	x->t = t, x->nargs = n, x->hash = h;
	x->args = malloc((n ? n : 1) * sizeof *a);
	memcpy(x->args, a, n * sizeof *a);
	for (uint32_t i = h & (c->cinstances - 1);; i = (i + 1) & (c->cinstances - 1))
		if (!tab[i]) {
			tab[i] = x;
			c->ninstances++;
			return x;
		}
}

VPRIV bool cxx_default_argument(cxx *c, const cxx_tparam *p, cxx_scope *binding, cxx_targ *out);
VPRIV void cxx_bind(cxx *c, cxx_scope *binding, const cxx_tparam *p, const cxx_targ *a);

VPRIV void cxx_instance_put(cxx *c, cxx_template *t, cxx_entity *te, const cxx_targ *args, uint32_t nargs, cxx_entity *inst)
{
	uint32_t h = cxx_instance_hash(t, args, nargs);
	cxx_instance *x = cxx_instance_find(c, t, args, nargs, h);

	if (!x)
		x = cxx_instance_add(c, t, args, nargs, h);
	x->inst = inst;
	if (!x->of)
		x->of = te;	/* (its template, for the name mangler) */
	if (inst && inst->ty != CXX_TY_UNKNOWN && cc_type_is_record(c->ast, inst->ty))
		cxx_tag_info_set(c, AST_TYPE(c->ast, inst->ty)->aux, x);	/* (an explicit specialization: matchable) */
}

/* template<> struct X<args> { ... } after X<args> was named (typedef X<char> t;, a default template argument):
   the class that name made, not yet complete, which the specialization defines ([temp.expl.spec]/7: it is
   not instantiated from the primary); NULL: none */
VPRIV cxx_entity *cxx_instance_named(cxx *c, cxx_entity *te, const cxx_targ *args, uint32_t nargs)
{
	cxx_template *t = te ? cxx_template_of(te) : NULL;
	cxx_instance *x;

	if (!t || nargs > 32)
		return NULL;
	for (uint32_t i = 0; i < nargs; i++)
		if (!args[i].kind)
			return NULL;
	x = cxx_instance_find(c, t, args, nargs, cxx_instance_hash(t, args, nargs));
	if (!x || !x->inst || x->inst->ty == CXX_TY_UNKNOWN || !cc_type_is_record(c->ast, x->inst->ty) ||
	    (c->ast->tags[AST_TYPE(c->ast, x->inst->ty)->aux].flags & RF_COMPLETE))
		return NULL;
	/* (defined by the specialization, not completed from the primary: its members are its own) */
	x->late = 0, x->state = 2;
	x->inst->instance = NULL;
	return x->inst;
}

VPRIV void cxx_instance_register(cxx *c, cxx_entity *te, const cxx_targ *args, uint32_t nargs, cxx_entity *inst)
{
	cxx_template *t;
	cxx_scope *ps;

	if (!te || c->tentative || nargs > 32)
		return;
	for (uint32_t i = 0; i < nargs; i++)
		if (!args[i].kind)
			return;
	t = cxx_template_of(te);
	cxx_instance_put(c, t, te, args, nargs, inst);	/* as written */
	/* and with the defaults completed: instantiate looks up the complete list */
	ps = t->params ? t->params : t->declared;
	if (ps && nargs < ps->ntparams) {
		cxx_targ full[32];
		cxx_scope *binding = cxx_scope_new(c, CXX_SCOPE_BLOCK, t->home ? t->home : c->scope, NULL);
		uint32_t n = 0;

		for (uint32_t i = 0; i < ps->ntparams && n < 32; i++) {
			const cxx_tparam *p = &ps->tparams[i];
			const cxx_tparam *d = t->declared && i < t->declared->ntparams ? &t->declared->tparams[i] : NULL;

			if (i < nargs)
				full[n] = args[i];
			else if (!((p->def_b && cxx_default_argument(c, p, binding, &full[n])) ||
				   (d && d->def_b && cxx_default_argument(c, d, binding, &full[n]))))
				return;
			cxx_bind(c, binding, p, &full[n]);
			if (d && d->name != p->name)
				cxx_bind(c, binding, d, &full[n]);
			n++;
		}
		cxx_instance_put(c, t, te, full, n, inst);
	}
}

/* ---- spelling (the layout dump: clang checks what vcc instantiated) ------------------------ */

VPRIV void cxx_tag_scope_set(cxx *c, uint32_t tag, cxx_scope *s)
{
	if (c->tentative)
		return;
	if (tag >= c->ntag_scope) {
		uint32_t nn = tag + 1024;

		c->tag_scope = realloc(c->tag_scope, nn * sizeof *c->tag_scope);
		memset(c->tag_scope + c->ntag_scope, 0, (nn - c->ntag_scope) * sizeof *c->tag_scope);
		c->ntag_scope = nn;
	}
	c->tag_scope[tag] = s;
}

VPRIV void cxx_tag_spelling_set(cxx *c, uint32_t tag, const char *s, size_t n)
{
	if (tag >= c->ntag_spelling) {
		uint32_t nn = tag + 1024;

		c->tag_spelling = realloc(c->tag_spelling, nn * sizeof *c->tag_spelling);
		memset(c->tag_spelling + c->ntag_spelling, 0, (nn - c->ntag_spelling) * sizeof *c->tag_spelling);
		c->ntag_spelling = nn;
	}
	free(c->tag_spelling[tag]);
	c->tag_spelling[tag] = malloc(n + 1);
	memcpy(c->tag_spelling[tag], s, n);
	c->tag_spelling[tag][n] = 0;
}

VPRIV bool cxx_spell_append(char *buf, size_t cap, size_t *len, const char *s)
{
	size_t n = strlen(s);

	if (*len + n + 1 > cap)
		return false;
	memcpy(buf + *len, s, n + 1);
	*len += n;
	return true;
}

VPRIV bool cxx_type_spell(cxx *c, qty t, char *buf, size_t cap, size_t *len)
{
	static const char *const basic[TY_NKINDS] = {
		[TY_VOID] = "void", [TY_BOOL] = "bool", [TY_CHAR] = "char", [TY_SCHAR] = "signed char", [TY_UCHAR] = "unsigned char",
		[TY_SHORT] = "short", [TY_USHORT] = "unsigned short", [TY_INT] = "int", [TY_UINT] = "unsigned int", [TY_LONG] = "long",
		[TY_ULONG] = "unsigned long", [TY_LLONG] = "long long", [TY_ULLONG] = "unsigned long long", [TY_INT128] = "__int128",
		[TY_UINT128] = "unsigned __int128", [TY_FLOAT] = "float", [TY_DOUBLE] = "double", [TY_LDOUBLE] = "long double",
	};
	const ctype *x;

	if (t == CXX_TY_UNKNOWN)
		return false;
	x = AST_TYPE(c->ast, t);
	if (x->kind == TY_PTR) {
		if (!cxx_type_spell(c, x->base, buf, cap, len))
			return false;
		if (!cxx_spell_append(buf, cap, len, x->ext == 1 ? "&" : x->ext == 2 ? "&&" : "*"))
			return false;
		if ((QQ(t) & Q_CONST) && !cxx_spell_append(buf, cap, len, " const"))
			return false;
		return !(QQ(t) & Q_VOLATILE) || cxx_spell_append(buf, cap, len, " volatile");
	}
	if ((QQ(t) & Q_CONST) && !cxx_spell_append(buf, cap, len, "const "))
		return false;
	if ((QQ(t) & Q_VOLATILE) && !cxx_spell_append(buf, cap, len, "volatile "))
		return false;
	if (x->ext == 1 && (x->kind == TY_UCHAR || x->kind == TY_USHORT || x->kind == TY_UINT || x->kind == TY_INT))
		return cxx_spell_append(buf, cap, len, x->kind == TY_UCHAR ? "char8_t" : x->kind == TY_USHORT ? "char16_t" :
						       x->kind == TY_UINT ? "char32_t" : "wchar_t");
	if (x->kind < TY_NKINDS && basic[x->kind] && x->kind <= TY_LDOUBLE && !x->ext)
		return cxx_spell_append(buf, cap, len, basic[x->kind]);
	if (x->kind == TY_STRUCT || x->kind == TY_UNION || x->kind == TY_ENUM)
		return x->aux < c->ntag_spelling && c->tag_spelling[x->aux] && cxx_spell_append(buf, cap, len, c->tag_spelling[x->aux]);
	return false;	/* (arrays, functions: not needed by the dump yet) */
}

/* ---- instantiation ----------------------------------------------------------------------- */
