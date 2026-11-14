/*
 * instantiate.c: class and alias template instantiation (PLAN-cxx.md X4),
 * first form.
 *
 * The parser keeps every token and never edits them, so instantiating a
 * template is re-parsing its tokens: a scope binds each parameter to its
 * argument (a type parameter becomes a type entity with that AST type), and
 * the class's head and body (or the alias's type-id) are parsed again from
 * where the template defined them, with no template around.  The result is
 * an ordinary class: its members, its record, its layout.  Function bodies
 * are skipped (types and layouts only, for now).
 *
 * Each instance is kept in a cache keyed by the template and its arguments,
 * the first form of PLAN-cxx.md X5's: an explicit specialization is
 * entered there when it is defined.  What is not handled yet answers
 * "unknown" instead of guessing: a template with partial specializations
 * (matching them is not written), packs, template template parameters,
 * arguments that are not types or integer literals.
 */
#include <stdio.h>
#include "cxx-internal.h"

static bool cxx_instance_spell(cxx *c, const cxx_template *t, const cxx_targ *a, uint32_t n, char *buf, size_t cap, size_t *len);

/* a partial specialization: its parameters, its argument pattern's tokens, its class-key */
typedef struct cxx_partial {
	cxx_scope *params, *home;
	uint32_t less_pos, key_pos;	/* the '<' of the pattern X<...>; the class-key */
	uint32_t req_pos;		/* its requires-clause (0: none); its parameters may be constrained too (template<C T>) */
	uint8_t evaluated, ok;		/* the pattern was parsed (ok) */
	uint8_t completed;		/* ... and completed with the primary's default arguments (X<T*> of template<class, class = void>) */
	const cxx_scope *primary, *primary_d;	/* the primary's parameters (and an earlier declaration's): defaults that use them */
	uint8_t subst;			/* ... and an argument of it is not deducible (decltype((void)expr), typename T::x): matched by
					   substituting the deduced parameters and comparing */
	cxx_targ *pattern;		/* the pattern, placeholders for the parameters */
	uint32_t npattern;
	qty *ph;			/* per parameter: its placeholder type (a type parameter) */
} cxx_partial;

struct cxx_template {
	cxx_partial *partials;
	uint32_t npartials;
	cxx_scope *params;	/* the template scope: its parameters (NULL until defined) */
	cxx_scope *declared;	/* an earlier declaration's template scope: default arguments may be there */
	cxx_scope *home;	/* where the template is declared */
	uint32_t pos, end;	/* class: its class-key token; alias: its type-id [pos, end) */
	uint8_t is_alias, has_partial;
	char *path;		/* its qualified name (the layout dump), NULL: not nameable */
	/* its members defined outside the class: template<...> R X<...>::f(...) { } */
	struct cxx_ooc { uint32_t name, pos, nparams; cxx_scope *params, *home; uint8_t is_const; } *oocs;
	uint32_t noocs;
};

typedef struct cxx_instance {
	cxx_template *t;
	uint32_t nargs;
	cxx_targ *args;
	uint32_t hash;
	cxx_entity *inst;	/* NULL: failed */
	cxx_entity *of;		/* the template */
	cxx_scope *binding;	/* its parameters bound to the arguments */
	uint32_t pos;		/* where its class is re-parsed: the template's, or a partial specialization's class-key */
	uint8_t state;		/* 0 declared (an incomplete record), 1 being completed, 2 complete, 3 failed */
	uint8_t selecting;	/* its partial specialization is being chosen (asked for again meanwhile: not known) */
	uint8_t late;		/* named while its template was only declared (using json = basic_json<>;): bound at completion */
	uint8_t missing;	/* an alias whose type named a member that is not there (typename X::type): said again on a cache hit */
} cxx_instance;

VPRIV cxx_template *cxx_template_of(cxx_entity *e)
{
	if (!e->tmpl)
		e->tmpl = calloc(1, sizeof *e->tmpl);
	return e->tmpl;
}

static void cxx_template_define_path(cxx *c, cxx_entity *e, cxx_template *t)
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
	t->partials = realloc(t->partials, (t->npartials + 1) * sizeof *t->partials);
	memset(&t->partials[t->npartials], 0, sizeof *t->partials);
	t->partials[t->npartials].params = params, t->partials[t->npartials].home = home;
	t->partials[t->npartials].less_pos = less_pos, t->partials[t->npartials].key_pos = key_pos;
	t->partials[t->npartials].req_pos = req_pos;
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

static uint32_t cxx_instance_hash(const cxx_template *t, const cxx_targ *a, uint32_t n)
{
	uint64_t h = (uintptr_t)t * 0x9E3779B97F4A7C15ull;

	for (uint32_t i = 0; i < n; i++)
		h = (h ^ ((uint64_t)a[i].kind << 56 ^ (a[i].kind == 1 ? a[i].t : (uint64_t)a[i].v ^ (a[i].kind == 2 ? (uint64_t)a[i].t << 20 : 0)))) *
		    0x100000001B3ull;
	return (uint32_t)(h ^ (h >> 32));
}

static bool cxx_args_equal(const cxx_targ *a, const cxx_targ *b, uint32_t n)
{
	for (uint32_t i = 0; i < n; i++)
		if (a[i].kind != b[i].kind || (a[i].kind == 1 ? a[i].t != b[i].t : a[i].v != b[i].v) || (a[i].kind == 2 && a[i].t != b[i].t))
			return false;	/* (a value's enumeration type is part of it) */
	return true;
}

/* the table holds pointers: an entry never moves (entities and tag_instance point at it) */
static cxx_instance *cxx_instance_find(cxx *c, const cxx_template *t, const cxx_targ *a, uint32_t n, uint32_t h)
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

static cxx_instance *cxx_instance_add(cxx *c, cxx_template *t, const cxx_targ *a, uint32_t n, uint32_t h)
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

static bool cxx_default_argument(cxx *c, const cxx_tparam *p, cxx_scope *binding, cxx_targ *out);
static void cxx_bind(cxx *c, cxx_scope *binding, const cxx_tparam *p, const cxx_targ *a);

static void cxx_instance_put(cxx *c, cxx_template *t, const cxx_targ *args, uint32_t nargs, cxx_entity *inst)
{
	uint32_t h = cxx_instance_hash(t, args, nargs);
	cxx_instance *x = cxx_instance_find(c, t, args, nargs, h);

	if (!x)
		x = cxx_instance_add(c, t, args, nargs, h);
	x->inst = inst;
	if (inst && inst->ty != CXX_TY_UNKNOWN && cc_type_is_record(c->ast, inst->ty))
		cxx_tag_info_set(c, AST_TYPE(c->ast, inst->ty)->aux, x);	/* (an explicit specialization: matchable) */
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
	cxx_instance_put(c, t, args, nargs, inst);	/* as written */
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
		cxx_instance_put(c, t, full, n, inst);
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

static bool cxx_spell_append(char *buf, size_t cap, size_t *len, const char *s)
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

typedef struct cxx_saved {
	uint32_t pos, template_depth, in_template_args, implicit_typename, class_depth, nlate, name_path_len, explicit_specialization;
	cxx_scope *scope;
	jmp_buf *fail;
	struct cxx_class_build *build;
	uint8_t name_path_valid;
	char name_path[512];
	cxx_entity *target, *target2, *last_type;
	uint8_t last_type_dependent, dependent_seen;
	qty ds_type, type_in, type_out;
} cxx_saved;

static void cxx_state_save(cxx *c, cxx_saved *s)
{
	s->pos = c->pos, s->template_depth = c->template_depth, s->in_template_args = c->in_template_args;
	s->implicit_typename = c->implicit_typename, s->class_depth = c->class_depth, s->nlate = c->nlate;
	s->scope = c->scope, s->fail = c->fail, s->build = c->build, s->explicit_specialization = c->explicit_specialization;
	s->name_path_len = c->name_path_len, s->name_path_valid = c->name_path_valid;
	memcpy(s->name_path, c->name_path, c->name_path_len);
	s->target2 = c->instantiation_template;
	s->target = c->instantiation_target, s->last_type = c->last_type, s->last_type_dependent = c->last_type_dependent;
	s->ds_type = c->ds_type, s->type_in = c->type_in, s->type_out = c->type_out;
	s->dependent_seen = c->dependent_seen;
}

static void cxx_state_restore(cxx *c, const cxx_saved *s)
{
	c->pos = s->pos, c->template_depth = s->template_depth, c->in_template_args = s->in_template_args;
	c->implicit_typename = s->implicit_typename, c->class_depth = s->class_depth, c->nlate = s->nlate;
	c->scope = s->scope, c->fail = s->fail, c->explicit_specialization = s->explicit_specialization;
	while (c->build != s->build) {	/* (classes an error left open) */
		struct cxx_class_build *b = c->build;

		cxx_class_skip(c, CXX_SKIP_UNKNOWN_MEMBER);
		cxx_class_end(c);
		if (c->build == b)
			break;
	}
	c->name_path_len = s->name_path_len, c->name_path_valid = s->name_path_valid;
	memcpy(c->name_path, s->name_path, s->name_path_len);
	c->instantiation_template = s->target2;
	c->instantiation_target = s->target, c->last_type = s->last_type, c->last_type_dependent = s->last_type_dependent;
	c->ds_type = s->ds_type, c->type_in = s->type_in, c->type_out = s->type_out;
	c->dependent_seen = s->dependent_seen;	/* (what the instance's own parse saw is not the enclosing one's) */
}

/* a default template argument: its tokens, parsed with the parameters bound so far */
static bool cxx_default_argument(cxx *c, const cxx_tparam *p, cxx_scope *binding, cxx_targ *out)
{
	cxx_saved s;
	jmp_buf jb;
	volatile bool ok = false;

	cxx_state_save(c, &s);
	c->pos = p->def_b;
	c->scope = binding;
	c->template_depth = 0;
	c->fail = &jb;
	if (!setjmp(jb)) {
		if (p->kind == 1) {
			cxx_type_id_parse(c);
			out->kind = c->type_out != CXX_TY_UNKNOWN, out->t = c->type_out, out->v = 0;
		} else if (p->kind == 3) {
			out->t = 0, out->v = 0;
			cxx_template_name_default_parse(c, out);	/* template<...> class C = std::vector */
		} else {
			/* a value: a constant expression (its enumeration, if any, is part of the argument) */
			out->kind = 0, out->t = 0, out->v = 0;
			c->in_template_args++;
			cxx_conditional_expression_parse(c);
			c->in_template_args--;
			if (c->econst && c->evalue >= 0) {
				out->kind = 2, out->v = c->evalue;
				if (c->etype != CXX_TY_UNKNOWN && AST_TYPE_KIND(c->ast, c->etype) == TY_ENUM)
					out->t = QUNQ(c->etype);
			}
		}
		ok = c->pos == p->def_e && out->kind;
	}
	cxx_state_restore(c, &s);
	return ok;
}

static void cxx_bind(cxx *c, cxx_scope *binding, const cxx_tparam *p, const cxx_targ *a)
{
	cxx_entity *e;

	if (!p->name)
		return;
	if (p->kind == 3) {	/* a template template parameter: the template it names */
		cxx_entity *te = a->kind == 4 ? (cxx_entity *)(intptr_t)a->v : NULL;

		e = cxx_declare(c, binding, p->name, CXX_ENTITY_TEMPLATE);
		if (e && te) {
			e->dependent = 0, e->members = te->members, e->specs = te->specs, e->nspecs = te->nspecs, e->tmpl = te->tmpl;
			e->bound_template = te->bound_template ? te->bound_template : te;	/* (Op<...> and Op are the template it is bound to) */
		}
		return;
	}
	e = cxx_declare(c, binding, p->name, p->kind == 1 ? CXX_ENTITY_TYPE : CXX_ENTITY_VALUE);
	if (!e)
		return;
	e->dependent = 0;
	if (p->kind == 2) {
		e->value = a->v, e->has_value = 1;
		if (a->kind == 2 && a->t && AST_TYPE_KIND(c->ast, a->t) == TY_ENUM)
			e->ty = a->t;	/* (a value of an enumeration: the parameter's type is that) */
	}
	if (p->kind == 1) {
		e->ty = a->t;
		/* a class argument: its members are reachable through the parameter's name (T::value_type) */
		if (a->t != CXX_TY_UNKNOWN && cc_type_is_record(c->ast, a->t)) {
			uint32_t tag = AST_TYPE(c->ast, a->t)->aux;

			if (tag < c->ntag_scope)
				e->members = c->tag_scope[tag];
		}
	}
}

/* a pack parameter bound to its arguments */
static void cxx_bind_pack(cxx *c, cxx_scope *binding, const cxx_tparam *p, const cxx_targ *list, uint32_t n)
{
	cxx_entity *e;

	if (!p->name)
		return;
	e = cxx_declare(c, binding, p->name, p->kind == 1 ? CXX_ENTITY_TYPE : p->kind == 2 ? CXX_ENTITY_VALUE : CXX_ENTITY_TEMPLATE);
	if (!e)
		return;
	e->dependent = 0, e->is_pack = 1, e->pack_index = -1, e->ty = CXX_TY_UNKNOWN;
	e->pack = malloc((n ? n : 1) * sizeof *e->pack);
	memcpy(e->pack, list, n * sizeof *list);
	e->npack = n;
}

/* an expansion's element: the pack entity stands for its i-th argument (i < 0: the whole pack) */
VPRIV void cxx_pack_element(cxx *c, cxx_entity *e, int32_t i)
{
	e->pack_index = i;
	if (i < 0) {
		e->ty = CXX_TY_UNKNOWN, e->has_value = 0, e->members = NULL, e->tmpl = NULL;
		return;
	}
	if (e->pack[i].kind == 1) {
		e->ty = e->pack[i].t;
		e->members = cxx_type_class_scope(c, e->ty);
	} else if (e->pack[i].kind == 2)
		e->value = e->pack[i].v, e->has_value = 1;
	else if (e->pack[i].kind == 4) {
		cxx_entity *te = (cxx_entity *)(intptr_t)e->pack[i].v;

		e->members = te->members, e->specs = te->specs, e->nspecs = te->nspecs, e->tmpl = te->tmpl;
	}
}

/* the spelling of X<args> for the layout dump */
VPRIV bool cxx_template_id_spell(cxx *c, cxx_entity *te, const cxx_targ *a, uint32_t n, char *buf, size_t cap, size_t *len)
{
	return te && te->tmpl && cxx_instance_spell(c, te->tmpl, a, n, buf, cap, len);
}

static bool cxx_instance_spell(cxx *c, const cxx_template *t, const cxx_targ *a, uint32_t n, char *buf, size_t cap, size_t *len)
{
	*len = 0;
	if (!t->path || !cxx_spell_append(buf, cap, len, t->path) || !cxx_spell_append(buf, cap, len, "<"))
		return false;
	for (uint32_t i = 0; i < n; i++) {
		char num[32];

		if (!a[i].kind)
			return false;	/* an argument not known: no spelling */
		if (i && !cxx_spell_append(buf, cap, len, ", "))
			return false;
		if (a[i].kind == 1) {
			if (!cxx_type_spell(c, a[i].t, buf, cap, len))
				return false;
		} else if (a[i].kind == 4) {
			const cxx_entity *te = (const cxx_entity *)(intptr_t)a[i].v;

			if (!te->tmpl || !te->tmpl->path || !cxx_spell_append(buf, cap, len, te->tmpl->path))
				return false;
		} else if (a[i].kind != 2)
			return false;
		else {
			/* an enumeration's value: (E)4 */
			if (a[i].t && a[i].t != CXX_TY_UNKNOWN && AST_TYPE_KIND(c->ast, a[i].t) == TY_ENUM &&
			    !(cxx_spell_append(buf, cap, len, "(") && cxx_type_spell(c, a[i].t, buf, cap, len) && cxx_spell_append(buf, cap, len, ")")))
				return false;
			snprintf(num, sizeof num, "%lld", (long long)a[i].v);
			if (!cxx_spell_append(buf, cap, len, num))
				return false;
		}
	}
	return cxx_spell_append(buf, cap, len, ">");
}


/* ---- partial specializations --------------------------------------------------------------- */

static void cxx_tag_grow(void ***arr, uint32_t *n, uint32_t tag)
{
	if (tag >= *n) {
		uint32_t nn = tag + 1024;

		*arr = realloc(*arr, nn * sizeof **arr);
		memset(*arr + *n, 0, (nn - *n) * sizeof **arr);
		*n = nn;
	}
}

VPRIV void cxx_tag_info_set(cxx *c, uint32_t tag, void *x)
{
	cxx_tag_grow(&c->tag_info, &c->ntag_info, tag);
	c->tag_info[tag] = x;
}

static cxx_instance *cxx_tag_info(cxx *c, qty t)
{
	uint32_t tag;

	if (t == CXX_TY_UNKNOWN || !cc_type_is_record(c->ast, t))
		return NULL;
	tag = AST_TYPE(c->ast, t)->aux;
	return tag < c->ntag_info ? c->tag_info[tag] : NULL;
}

/* a placeholder type: its partial specialization and parameter index (-1: not one) */
static int cxx_placeholder_of(cxx *c, qty t, void **owner)
{
	uint32_t tag;

	if (t == CXX_TY_UNKNOWN || !cc_type_is_record(c->ast, t))
		return -1;
	tag = AST_TYPE(c->ast, t)->aux;
	if (tag >= c->ntag_placeholder || !c->tag_placeholder[tag].index)
		return -1;
	*owner = c->tag_placeholder[tag].owner;
	return (int)c->tag_placeholder[tag].index - 1;
}

/* does t mention a placeholder (an argument list that is still a pattern)? */
static bool cxx_mentions_placeholder(cxx *c, qty t, int depth)
{
	void *o;
	const ctype *x;

	if (t == CXX_TY_UNKNOWN || depth > 32)
		return false;
	if (cxx_placeholder_of(c, t, &o) >= 0)
		return true;
	x = AST_TYPE(c->ast, t);
	switch (x->kind) {
	case TY_ARRAY:
		if (x->ext)
			return true;	/* T[N]: the bound is a placeholder */
		return cxx_mentions_placeholder(c, x->base, depth + 1);
	case TY_PTR:
		return cxx_mentions_placeholder(c, x->base, depth + 1);
	case TY_FUNC:
		if (cxx_mentions_placeholder(c, x->base, depth + 1))
			return true;
		for (uint32_t i = 0; i < x->n; i++)
			if (cxx_mentions_placeholder(c, c->ast->params[x->aux + i], depth + 1))
				return true;
		return false;
	case TY_STRUCT: case TY_UNION: {
		cxx_instance *ix = cxx_tag_info(c, t);

		if (ix)
			for (uint32_t i = 0; i < ix->nargs; i++)
				if ((ix->args[i].kind == 1 && cxx_mentions_placeholder(c, ix->args[i].t, depth + 1)) || ix->args[i].kind == 3 ||
				    ix->args[i].kind == 7 || ix->args[i].kind == 5 || ix->args[i].kind == 0)
					return true;
		return false;
	}
	}
	return false;
}

static bool cxx_args_mention_placeholder(cxx *c, const cxx_targ *a, uint32_t n)
{
	for (uint32_t i = 0; i < n; i++)
		if (a[i].kind == 3 || a[i].kind == 5 || a[i].kind == 7 || (a[i].kind == 1 && cxx_mentions_placeholder(c, a[i].t, 0)))
			return true;
	return false;
}

static bool cxx_match_arg(cxx *c, cxx_partial *p, const cxx_targ *pat, const cxx_targ *arg, cxx_targ *bind, int depth);
static bool cxx_match_list(cxx *c, cxx_partial *p, const cxx_targ *pat, uint32_t np, const cxx_targ *args, uint32_t n, cxx_targ *bind,
			   int depth);

/* deduce: pat (a pattern of p) against the type arg, binding p's parameters in bind */
static bool cxx_match_type(cxx *c, cxx_partial *p, qty pat, qty arg, cxx_targ *bind, int depth)
{
	void *owner;
	int i;
	const ctype *pt, *at;

	if (depth > 32 || pat == CXX_TY_UNKNOWN || arg == CXX_TY_UNKNOWN)
		return false;
	if (pat == arg)
		return true;
	i = cxx_placeholder_of(c, pat, &owner);
	if (i >= 0 && owner == p) {
		/* T (or const T) binds what is left of arg after the pattern's qualifiers */
		int q = QQ(pat);
		qty a;

		if ((QQ(arg) & q) != q)
			return false;
		a = QT(QI(arg), QQ(arg) & ~q);
		if (bind[i].kind)
			return bind[i].kind == 1 && bind[i].t == a;
		bind[i].kind = 1, bind[i].t = a, bind[i].v = 0;
		return true;
	}
	if (QQ(pat) != QQ(arg))
		return false;
	pt = AST_TYPE(c->ast, pat), at = AST_TYPE(c->ast, arg);
	if (pt->kind != at->kind)
		return false;
	switch (pt->kind) {
	case TY_PTR:
		return pt->ext == at->ext && cxx_match_type(c, p, pt->base, at->base, bind, depth + 1);
	case TY_ARRAY:
		if (pt->ext) {
			/* T[N]: N is the argument array's length */
			uint32_t vi = pt->ext - 1u;

			if (at->ext || (at->flags & TF_INCOMPLETE) || !at->n || vi >= p->params->ntparams)
				return false;
			if (bind[vi].kind) {
				if (bind[vi].kind != 2 || bind[vi].v != (int64_t)at->n)
					return false;
			} else
				bind[vi].kind = 2, bind[vi].v = (int64_t)at->n, bind[vi].t = 0;
			return cxx_match_type(c, p, pt->base, at->base, bind, depth + 1);
		}
		return pt->flags == at->flags && pt->n == at->n && pt->aux == at->aux && cxx_match_type(c, p, pt->base, at->base, bind, depth + 1);
	case TY_FUNC:
		if (pt->n != at->n || pt->flags != at->flags || !cxx_match_type(c, p, pt->base, at->base, bind, depth + 1))
			return false;
		for (uint32_t k = 0; k < pt->n; k++)
			if (!cxx_match_type(c, p, c->ast->params[pt->aux + k], c->ast->params[at->aux + k], bind, depth + 1))
				return false;
		return true;
	case TY_STRUCT: case TY_UNION: {
		/* X<pattern...> against X<args...>: the same template, its arguments matched */
		cxx_instance *ip = cxx_tag_info(c, pat), *ia = cxx_tag_info(c, arg);

		if (!ip)
			return QI(pat) == QI(arg);
		if (!ia || ia->t != ip->t)
			return false;
		return cxx_match_list(c, p, ip->args, ip->nargs, ia->args, ia->nargs, bind, depth + 1);
	}
	}
	return QI(pat) == QI(arg);
}

/* a pattern's argument list against an argument list; a trailing Pack... takes the rest */
static bool cxx_match_list(cxx *c, cxx_partial *p, const cxx_targ *pat, uint32_t np, const cxx_targ *args, uint32_t n, cxx_targ *bind,
			   int depth)
{
	bool trailing_pack = np && pat[np - 1].kind == 5;

	if (trailing_pack ? n < np - 1 : np != n)
		return false;
	for (uint32_t i = 0; i < (trailing_pack ? np - 1 : n); i++)
		if (!cxx_match_arg(c, p, &pat[i], &args[i], bind, depth))
			return false;
	if (trailing_pack) {
		cxx_targ *b = &bind[pat[np - 1].v];
		uint32_t start = np - 1, count = n - (np - 1);

		if (b->kind == 6)	/* (bound before: the same elements) */
			return b->v == count && cxx_args_equal(b->list + b->t, args + start, count);
		b->kind = 6, b->list = args, b->t = start, b->v = count;
	}
	return true;
}

static bool cxx_match_arg(cxx *c, cxx_partial *p, const cxx_targ *pat, const cxx_targ *arg, cxx_targ *bind, int depth)
{
	switch (pat->kind) {
	case 0:
		return true;	/* (not deducible: checked by substitution) */
	case 1:
		return arg->kind == 1 && cxx_match_type(c, p, pat->t, arg->t, bind, depth);
	case 2:
		return arg->kind == 2 && pat->v == arg->v && pat->t == arg->t;
	case 4:	/* a template */
		return arg->kind == 4 && pat->v == arg->v;
	case 7:	/* a template template parameter: the template (or, pattern against pattern, another's parameter) */
		if (arg->kind != 4 && arg->kind != 7)
			return false;
		if (bind[pat->v].kind)
			return bind[pat->v].kind == arg->kind && bind[pat->v].v == arg->v;
		bind[pat->v] = *arg;
		return true;
	case 3:	/* a value parameter */
		if (arg->kind != 2 && arg->kind != 3)
			return false;
		if (bind[pat->v].kind)
			return bind[pat->v].kind == arg->kind && bind[pat->v].v == arg->v;
		bind[pat->v] = *arg;
		return true;
	}
	return false;
}

/* parse p's argument pattern once, its parameters bound to placeholders */
static void cxx_partial_evaluate(cxx *c, cxx_partial *p)
{
	cxx_saved s;
	jmp_buf jb;
	cxx_scope *binding;

	if (p->evaluated)
		return;
	p->evaluated = 1;
	p->ok = 0;
	binding = cxx_scope_new(c, CXX_SCOPE_BLOCK, p->home, NULL);
	p->ph = calloc(p->params->ntparams + 1, sizeof *p->ph);
	for (uint32_t i = 0; i < p->params->ntparams; i++) {
		const cxx_tparam *tp = &p->params->tparams[i];
		cxx_entity *e;

		if (!tp->name)
			return;
		if (tp->kind == 3) {
			/* a template template parameter: its name in the pattern is a template placeholder (kind 7) */
			if (tp->pack)
				return;
			e = cxx_declare(c, binding, tp->name, CXX_ENTITY_TEMPLATE);
			if (!e)
				return;
			e->dependent = 0, e->placeholder = (uint16_t)(i + 1);
			continue;
		}
		e = cxx_declare(c, binding, tp->name, tp->kind == 1 ? CXX_ENTITY_TYPE : CXX_ENTITY_VALUE);
		if (!e)
			return;
		e->dependent = 0;
		if (tp->pack) {	/* a pack: its expansion in the pattern takes the remaining arguments */
			e->is_pack = 1, e->pack_index = -1, e->pack_placeholder = (uint16_t)(i + 1), e->npack = 0;
			continue;
		}
		if (tp->kind == 1) {
			uint32_t tag = cc_tag_new(c->ast, TY_STRUCT, tp->name, 0);

			if (tag >= c->ntag_placeholder) {
				uint32_t nn = tag + 1024;

				c->tag_placeholder = realloc(c->tag_placeholder, nn * sizeof *c->tag_placeholder);
				memset(c->tag_placeholder + c->ntag_placeholder, 0, (nn - c->ntag_placeholder) * sizeof *c->tag_placeholder);
				c->ntag_placeholder = nn;
			}
			c->tag_placeholder[tag].owner = p, c->tag_placeholder[tag].index = i + 1;
			p->ph[i] = e->ty = QT(c->ast->tags[tag].ty, 0);
		} else
			e->placeholder = (uint16_t)(i + 1);
	}
	cxx_state_save(c, &s);
	c->instantiating++;
	c->fail = &jb;
	c->pos = p->less_pos;
	c->scope = binding;
	c->template_depth = 0, c->in_template_args = 0, c->explicit_specialization = 0, c->class_depth = 0, c->implicit_typename = 0;
	uint8_t o_unsure = c->eunsure;

	c->eunsure = 0;
	if (!setjmp(jb)) {
		cxx_template_arguments_parse(c);
		p->npattern = c->ntargs;
		p->pattern = malloc((c->ntargs ? c->ntargs : 1) * sizeof *p->pattern);
		memcpy(p->pattern, c->targs, c->ntargs * sizeof *p->pattern);
		p->ok = 1;
		for (uint32_t i = 0; i < p->npattern; i++) {
			/* decltype((void)expr-of-T): void here, whatever expr is: not deducible, substituted when matching */
			if (c->eunsure && ((p->pattern[i].kind == 1 && !cxx_mentions_placeholder(c, p->pattern[i].t, 0)) || p->pattern[i].kind == 2))
				p->pattern[i].kind = 0;
			p->subst |= p->pattern[i].kind == 0;
		}
	}
	c->eunsure = o_unsure;
	c->instantiating--;
	cxx_state_restore(c, &s);
}

/* the deducible part: p's pattern against the arguments; bind: its parameters, all deduced */
static bool cxx_partial_deduces(cxx *c, cxx_partial *p, const cxx_targ *args, uint32_t n, cxx_targ *bind)
{
	memset(bind, 0, p->params->ntparams * sizeof *bind);
	if (!cxx_match_list(c, p, p->pattern, p->npattern, args, n, bind, 0))
		return false;
	for (uint32_t i = 0; i < p->params->ntparams; i++)
		if (!bind[i].kind)
			return false;	/* (a parameter not deduced) */
	return true;
}

/*
 * p's pattern with its deduced parameters substituted, against the arguments: 1 the same, 0 not (or
 * the substitution is ill-formed: X<T, decltype((void)T().f())> for a T with no f), -1 not known.
 */
static int cxx_partial_substitute(cxx *c, cxx_partial *p, const cxx_targ *bind, const cxx_targ *args, uint32_t n)
{
	cxx_scope *binding = cxx_scope_new(c, CXX_SCOPE_BLOCK, p->home, NULL);
	cxx_saved s;
	jmp_buf jb;
	cxx_targ got[32];
	volatile uint32_t ngot = UINT32_MAX;
	uint8_t o_invalid = c->einvalid, o_missing = c->member_missing, o_unsure = c->eunsure;
	bool invalid, unsure;

	if (n > 32 || c->instantiating > 48)
		return -1;
	for (uint32_t i = 0; i < p->params->ntparams; i++)
		if (bind[i].kind == 6)
			cxx_bind_pack(c, binding, &p->params->tparams[i], bind[i].list + bind[i].t, (uint32_t)bind[i].v);
		else
			cxx_bind(c, binding, &p->params->tparams[i], &bind[i]);
	cxx_state_save(c, &s);
	c->instantiating++, c->unevaluated++;
	c->fail = &jb;
	c->pos = p->less_pos;
	c->scope = binding;
	c->template_depth = 0, c->in_template_args = 0, c->explicit_specialization = 0, c->class_depth = 0, c->implicit_typename = 0;
	c->einvalid = 0, c->member_missing = 0, c->eunsure = 0;
	if (!setjmp(jb)) {
		cxx_template_arguments_parse(c);
		if (c->ntargs <= 32) {
			memcpy(got, c->targs, c->ntargs * sizeof *got);
			ngot = c->ntargs;
		}
	}
	c->instantiating--, c->unevaluated--;
	cxx_state_restore(c, &s);
	invalid = c->einvalid || c->member_missing, unsure = c->eunsure;
	if (getenv("VCC_CXX_SUBST")) {
		char sp[256] = "?";
		size_t sl = 0;

		if (n && args[0].kind == 1)
			cxx_type_spell(c, args[0].t, sp, sizeof sp, &sl);
		fprintf(stderr, "substitute: invalid %u missing %u unsure %u parsed %d args %u first kind %u last kind %u; first argument %s\n",
			c->einvalid, c->member_missing, c->eunsure, ngot != UINT32_MAX, (unsigned)ngot, ngot && ngot != UINT32_MAX ? got[0].kind : 9,
			ngot && ngot != UINT32_MAX ? got[ngot - 1].kind : 9, sp);
	}
	c->einvalid = o_invalid, c->member_missing = o_missing, c->eunsure = o_unsure;
	if (invalid)
		return 0;
	if (ngot == UINT32_MAX || unsure)
		return -1;
	if (ngot > n)
		return 0;
	for (uint32_t i = 0; i < ngot; i++)
		if (!got[i].kind)
			return -1;
	if (!cxx_args_equal(got, args, ngot))
		return 0;
	/* the arguments the pattern does not write are the primary's defaults: known ones are in p->pattern;
	   one that uses the earlier parameters (class = enable_if_t<cond<T>>) is computed from the arguments */
	if (ngot < n && p->npattern != n)
		return 0;
	for (uint32_t i = ngot; i < n; i++) {
		cxx_targ d;

		if (p->pattern[i].kind) {
			if (!cxx_args_equal(&p->pattern[i], &args[i], 1))
				return 0;
			continue;
		}
		if (!p->primary || i >= p->primary->ntparams)
			return -1;
		{
			const cxx_tparam *tp = &p->primary->tparams[i];
			cxx_scope *b = cxx_scope_new(c, CXX_SCOPE_BLOCK, p->home, NULL);

			if (!tp->def_b && p->primary_d && p->primary_d != p->primary && i < p->primary_d->ntparams)
				tp = &p->primary_d->tparams[i];
			for (uint32_t j = 0; j < i; j++)
				if (!p->primary->tparams[j].pack)
					cxx_bind(c, b, &p->primary->tparams[j], &args[j]);
			memset(&d, 0, sizeof d);
			o_missing = c->member_missing, c->member_missing = 0;
			if (!tp->def_b || !cxx_default_argument(c, tp, b, &d)) {
				bool missing = c->member_missing;

				c->member_missing = o_missing;
				return missing ? 0 : -1;
			}
			c->member_missing = o_missing;
			if (!cxx_args_equal(&d, &args[i], 1))
				return 0;
		}
	}
	return 1;
}

/*
 * A pattern shorter than the primary's parameter list takes the primary's default arguments for the
 * rest (ps: the primary's parameters, d: an earlier declaration's, which may hold the defaults).  Only
 * defaults that do not use the earlier parameters (class = void) are known here; false otherwise.
 */
static bool cxx_partial_complete(cxx *c, cxx_partial *p, const cxx_scope *ps, const cxx_scope *d, uint32_t n)
{
	cxx_scope *empty;

	if (p->completed || p->npattern >= n || !ps || (p->npattern && p->pattern[p->npattern - 1].kind == 5))
		return true;
	if (ps->ntparams && ps->tparams[ps->ntparams - 1].pack && p->npattern + 1 >= ps->ntparams)
		return true;	/* (the primary ends in a pack: the pattern's arguments are all of them) */
	if (n > ps->ntparams)
		return false;
	empty = cxx_scope_new(c, CXX_SCOPE_BLOCK, p->home, NULL);
	p->pattern = realloc(p->pattern, n * sizeof *p->pattern);
	p->primary = ps, p->primary_d = d;
	for (uint32_t i = p->npattern; i < n; i++) {
		const cxx_tparam *tp = &ps->tparams[i];

		if (!tp->def_b && d && d != ps && i < d->ntparams)
			tp = &d->tparams[i];
		memset(&p->pattern[i], 0, sizeof p->pattern[i]);
		if (!tp->def_b)
			return false;
		/* a default that uses the earlier parameters: computed from the arguments when matching */
		if (!cxx_default_argument(c, tp, empty, &p->pattern[i])) {
			memset(&p->pattern[i], 0, sizeof p->pattern[i]);
			p->subst = 1;
		}
	}
	p->npattern = n, p->completed = 1;
	return true;
}

/* does p match the arguments?  1 yes (bind: its parameters), 0 no, -1 not known */
static int cxx_partial_matches(cxx *c, cxx_partial *p, const cxx_targ *args, uint32_t n, cxx_targ *bind)
{
	if (!cxx_partial_deduces(c, p, args, n, bind))
		return 0;
	return p->subst ? cxx_partial_substitute(c, p, bind, args, n) : 1;
}

/* a is at least as specialized as b: b's pattern matches a's (a's placeholders as types) */
static bool cxx_partial_at_least_as_specialized(cxx *c, cxx_partial *a, cxx_partial *b)
{
	cxx_targ bind[64];

	if (b->params->ntparams > 64)
		return false;
	return cxx_partial_deduces(c, b, a->pattern, a->npattern, bind);
}

static int cxx_constraint_parse(cxx *c, cxx_partial *p, const cxx_targ *bind, uint32_t pos, const cxx_targ *first);

static bool cxx_partial_constrained(const cxx_partial *p)
{
	if (p->req_pos)
		return true;
	for (uint32_t i = 0; i < p->params->ntparams; i++)
		if (p->params->tparams[i].constraint_b)
			return true;
	return false;
}

/* p's constraints with its parameters bound: 1 satisfied (or none), 0 not, -1 not known */
static int cxx_partial_constraints(cxx *c, cxx_partial *p, const cxx_targ *bind)
{
	for (uint32_t i = 0; i < p->params->ntparams; i++) {
		const cxx_tparam *tp = &p->params->tparams[i];

		if (tp->constraint_b && bind[i].kind == 1) {
			int v = cxx_constraint_parse(c, p, bind, tp->constraint_b, &bind[i]);

			if (v <= 0)
				return v;
		}
	}
	return p->req_pos ? cxx_constraint_parse(c, p, bind, p->req_pos, NULL) : 1;
}

/*
 * The partial specialization the arguments select: 0 none (the primary template), 1 one (*pick,
 * its parameters in bind), -1 not known (a pattern that cannot be evaluated, an ambiguity).
 */
static int cxx_partial_select(cxx *c, cxx_template *t, const cxx_targ *args, uint32_t n, cxx_partial **pick, cxx_targ *bind)
{
	cxx_partial *cand[16];
	uint32_t ncand = 0;
	cxx_targ tmp[64];

	for (uint32_t i = 0; i < t->npartials; i++) {
		cxx_partial *p = &t->partials[i];

		cxx_partial_evaluate(c, p);
		if (!p->ok || p->params->ntparams > 64 || !cxx_partial_complete(c, p, t->params ? t->params : t->declared, t->declared, n)) {
			if (getenv("VCC_CXX_WHY")) fprintf(stderr, "partial unknown (%s) %s\n", !p->ok ? "pattern" : "defaults", t->path ? t->path : "?");
			return -1;	/* a pattern vcc cannot evaluate might match: do not guess */
		}
		int m = cxx_partial_matches(c, p, args, n, tmp);

		if (m < 0) {
			if (getenv("VCC_CXX_WHY")) fprintf(stderr, "partial unknown (substitution) %s\n", t->path ? t->path : "?");
			return -1;	/* (a pattern vcc cannot decide might match) */
		}
		if (m) {
			/* its constraints with the deduced parameters: template<C T> struct X<T>, requires ... */
			int ok = cxx_partial_constraints(c, p, tmp);

			if (ok < 0) {
				if (getenv("VCC_CXX_WHY")) fprintf(stderr, "partial unknown (constraint) %s\n", t->path ? t->path : "?");
				return -1;
			}
			if (!ok)
				continue;
			if (ncand == 16)
				return -1;
			cand[ncand++] = p;
		}
	}
	if (!ncand)
		return 0;
	/* the most specialized: at least as specialized as every other, and not the reverse; of two with the
	   same pattern the constrained one (two constrained ones: their constraints' subsumption, not done) */
	for (uint32_t i = 0; i < ncand; i++) {
		bool best = true;

		for (uint32_t k = 0; k < ncand && best; k++) {
			bool ik, ki;

			if (k == i)
				continue;
			ik = cxx_partial_at_least_as_specialized(c, cand[i], cand[k]);
			ki = cxx_partial_at_least_as_specialized(c, cand[k], cand[i]);
			if (ik && ki)
				best = cxx_partial_constrained(cand[i]) && !cxx_partial_constrained(cand[k]);
			else
				best = ik && !ki;
		}
		if (best) {
			*pick = cand[i];
			cxx_partial_matches(c, cand[i], args, n, bind);
			return 1;
		}
	}
	return -1;	/* ambiguous */
}

static bool cxx_instance_parse(cxx *c, cxx_instance *x);

static cxx_entity *cxx_instantiate_now(cxx *c, cxx_entity *te, const cxx_targ *args, uint32_t nargs, bool complete);

/* X<args>: its instance (in a tentative parse too: is X<A>::type a type?  The instance is made and cached
   as in any other parse; its own parses are not tentative) */
VPRIV cxx_entity *cxx_instantiate(cxx *c, cxx_entity *te, const cxx_targ *args, uint32_t nargs, bool complete)
{
	int tentative = c->tentative;
	cxx_targ acopy[32];
	cxx_entity *r;

	if (nargs > 32)
		return NULL;
	/* a template template parameter bound to a template: that template's instance (named as it, not as the
	   parameter: AllocatorType<T> is std::allocator<T>, whose constructors are called allocator) */
	if (te && te->bound_template)
		te = te->bound_template;
	memcpy(acopy, args, nargs * sizeof *args);	/* (often c->targs, which a pattern's parse reuses) */
	c->tentative = 0;
	r = cxx_instantiate_now(c, te, acopy, nargs, complete);
	c->tentative = tentative;
	return r;
}

static cxx_entity *cxx_instantiate_now(cxx *c, cxx_entity *te, const cxx_targ *args, uint32_t nargs, bool complete)
{
	cxx_template *t = te ? te->tmpl : NULL;
	cxx_targ full[32];
	uint32_t n = 0, h;
	cxx_scope *binding;
	cxx_instance *x;
	cxx_entity *inst;

	if (!t || c->tentative || c->instantiating > 48 || nargs > 32)
		return NULL;
	for (uint32_t i = 0; i < nargs; i++)
		if (!args[i].kind)
			return NULL;
	/* an explicit specialization (or an instance made before) */
	h = cxx_instance_hash(t, args, nargs);
	x = cxx_instance_find(c, t, args, nargs, h);
	if (x) {
		if (x->missing)
			c->member_missing = 1;
		if (complete && x->inst)
			cxx_instance_complete(c, x->inst);
		return x->inst;
	}
	cxx_scope *ps = t->params ? t->params : t->declared;	/* (declared only: its partial specializations may still match) */

	bool late = false;	/* declared, not defined yet: the instance is named now, bound when it is completed */

	if (!ps)
		return NULL;
	/* the arguments, defaults completed, bound in a scope of their own */
	binding = cxx_scope_new(c, CXX_SCOPE_BLOCK, t->home, NULL);
	for (uint32_t i = 0; i < ps->ntparams; i++) {
		const cxx_tparam *p = &ps->tparams[i];
		const cxx_tparam *d = t->declared && t->declared != t->params && i < t->declared->ntparams ? &t->declared->tparams[i] : NULL;

		if (n >= 32)
			return NULL;
		if (p->pack) {
			/* a pack: the remaining arguments (the last parameter; a pack elsewhere is deduced only) */
			uint32_t k = n;

			if (i + 1 != ps->ntparams)
				return NULL;
			for (uint32_t j = i; j < nargs; j++) {
				/* (a pattern's pack expansion, tuple<Ts...>: it stands for the pack's elements) */
				if (k >= 32 || (args[j].kind != 5 && (args[j].kind == 1) != (p->kind == 1)))
					return NULL;
				full[k++] = args[j];
			}
			cxx_bind_pack(c, binding, p, full + n, k - n);
			n = k;
			break;
		}
		if (i < nargs)
			full[n] = args[i];
		else if (p->def_b || (d && d->def_b && d->kind == p->kind)) {
			memset(&full[n], 0, sizeof full[n]);
			if (!cxx_default_argument(c, p->def_b ? p : d, binding, &full[n])) {	/* (or an earlier declaration's default) */
				/* in a pattern (X<T> with T a placeholder and a default that uses it): any argument there */
				if (!cxx_args_mention_placeholder(c, full, n))
					return NULL;
				memset(&full[n], 0, sizeof full[n]);
				n++;
				continue;
			}
		} else
			return NULL;
		/* an injected class name given where a template is wanted: the instance's template */
		if (p->kind == 3 && full[n].kind == 1) {
			cxx_instance *ix = cxx_tag_info(c, full[n].t);

			if (ix && ix->of)
				full[n].kind = 4, full[n].v = (int64_t)(intptr_t)ix->of, full[n].t = 0;
		}
		if ((p->kind == 1) != (full[n].kind == 1) || (p->kind == 3) != (full[n].kind == 4 || full[n].kind == 7))
			return NULL;
		cxx_bind(c, binding, p, &full[n]);
		if (d && d->name != p->name)
			cxx_bind(c, binding, d, &full[n]);	/* (the declaration's name for it, in its defaults) */
		n++;
	}
	if (nargs > ps->ntparams && !(ps->ntparams && ps->tparams[ps->ntparams - 1].pack))
		return NULL;
	/* the complete argument list is the key (X<int> and X<int, allocator<int>> are one instance) */
	h = cxx_instance_hash(t, full, n);
	x = cxx_instance_find(c, t, full, n, h);
	if (x) {
		if (x->missing)
			c->member_missing = 1;
		if (complete && x->inst)
			cxx_instance_complete(c, x->inst);
		return x->inst;
	}
	if (getenv("VCC_CXX_TRACE_INSTANCE")) {
		/* where an instance whose spelling contains the given text is first named */
		char sp[512];
		size_t sl;

		if (cxx_instance_spell(c, t, full, n, sp, sizeof sp, &sl) && strstr(sp, getenv("VCC_CXX_TRACE_INSTANCE"))) {
			fprintf(stderr, "instance %s named at:", sp);
			for (uint32_t i = c->pos > 12 ? c->pos - 12 : 0; i < c->pos + 6 && i < c->ntok; i++)
				fprintf(stderr, " %.*s", (int)c->tok[i].len, c->tok[i].s);
			fputc('\n', stderr);
		}
	}
	bool pattern = cxx_args_mention_placeholder(c, full, n);	/* X<T*> in a partial specialization's pattern */
	cxx_partial *pick = NULL;
	cxx_targ bind[64];

	/* A class template's partial specialization is chosen when the instance must be complete, as its body
	   is: naming X<args> decides nothing (std::_And<A, X<args>> never looks at X<args> when A is false;
	   choosing it here made __is_ostreamable ask for itself without end).  An alias is a type now. */
	if (!pattern && t->npartials && !t->is_alias)
		late = true;
	else if (!pattern && t->npartials) {
		int r = cxx_partial_select(c, t, full, n, &pick, bind);

		if (r < 0) {
			c->partial_unknown++;
			return NULL;
		}
		if (r == 0 && !t->params)
			late = true;	/* the primary template: declared, not defined yet */
		if (r > 0) {
			/* the partial specialization's parameters, deduced, bound for its re-parse */
			c->partial_matches++;
			binding = cxx_scope_new(c, CXX_SCOPE_BLOCK, pick->home, NULL);
			for (uint32_t i = 0; i < pick->params->ntparams; i++)
				if (bind[i].kind == 6)	/* a deduced pack: its slice of the arguments */
					cxx_bind_pack(c, binding, &pick->params->tparams[i], bind[i].list + bind[i].t, (uint32_t)bind[i].v);
				else
					cxx_bind(c, binding, &pick->params->tparams[i], &bind[i]);
		}
	}
	if (!t->params && !pick)
		late = true;
	inst = calloc(1, sizeof *inst);
	inst->name = te->name, inst->kind = CXX_ENTITY_TYPE, inst->ty = CXX_TY_UNKNOWN;
	x = cxx_instance_add(c, t, full, n, h);
	x->inst = inst, x->of = te, x->binding = binding;
	x->late = late && !pattern;
	x->pos = pick ? pick->key_pos : 0;
	if (pattern)
		x->state = 3;	/* a pattern's record: matched by its arguments, never completed */
	inst->instance = x;
	if (t->is_alias) {
		/* an alias is a type: computed now */
		uint8_t mm = c->member_missing;

		c->member_missing = 0;
		if (!cxx_instance_parse(c, x))
			x->inst = NULL;
		x->missing = c->member_missing;
		c->member_missing |= mm;
		return x->inst;
	}
	/* a class: an incomplete record now; its body when it must be complete (C++ is lazy too: naming
	   X<args> does not instantiate it; a member of that type, a base, X<args>::name do) */
	{
		uint32_t tag = cc_tag_new(c->ast, TY_STRUCT, te->name, 0);
		char spelled[512];
		size_t sl;

		inst->ty = QT(c->ast->tags[tag].ty, 0);
		cxx_tag_instance_set(c, tag, x);
		cxx_tag_info_set(c, tag, x);
		if (cxx_instance_spell(c, t, full, n, spelled, sizeof spelled, &sl))
			cxx_tag_spelling_set(c, tag, spelled, sl);
	}
	if (complete)
		cxx_instance_complete(c, inst);
	return x->inst;
}

/* parse the instance's tokens: the class body, or the alias's type-id; false if that failed */
static bool cxx_instance_parse(cxx *c, cxx_instance *x)
{
	cxx_template *t = x->t;
	cxx_entity *inst = x->inst;
	cxx_saved s;
	jmp_buf jb;
	char spelled[512];
	size_t sl;
	volatile bool ok = false;

	if (c->instantiating > 48)
		return false;
	if (c->trace)
		dprintf(2, "cxx-trace: %*s%s\n", (int)c->instantiating * 2, "",
			cxx_instance_spell(c, t, x->args, x->nargs, spelled, sizeof spelled, &sl) ? spelled : t->path ? t->path : "?");
	cxx_state_save(c, &s);
	c->instantiating++;
	c->fail = &jb;
	c->pos = x->pos ? x->pos : t->pos;
	c->scope = x->binding;
	c->template_depth = 0, c->in_template_args = 0, c->explicit_specialization = 0, c->class_depth = 0;
	if (!setjmp(jb)) {
		if (t->is_alias) {
			c->implicit_typename = 1;
			cxx_type_id_parse(c);
			if (c->pos != t->end)
				cxx_fail(c, "an alias template's type ended early");
			inst->ty = c->type_out;
			if (c->last_type && c->type_out != CXX_TY_UNKNOWN)
				inst->members = c->last_type->members, inst->specs = c->last_type->specs, inst->nspecs = c->last_type->nspecs;
			ok = inst->ty != CXX_TY_UNKNOWN;
		} else {
			c->implicit_typename = 0;
			if (cxx_instance_spell(c, t, x->args, x->nargs, spelled, sizeof spelled, &sl) && sl < sizeof c->name_path) {
				memcpy(c->name_path, spelled, sl);
				c->name_path_len = (uint32_t)sl, c->name_path_valid = 1;
			} else
				c->name_path_valid = 0;
			c->instantiation_target = inst;
			c->instantiation_template = x->of;
			cxx_class_specifier_for_instance(c);
			ok = true;
		}
		c->instances_made++;
	} else {
		c->instances_failed++;
		if (getenv("VCC_CXX_WHY")) {
			/* the instance and the parse error that stopped it */
			char sp[400] = "?";
			size_t sl2 = 0;

			cxx_instance_spell(c, t, x->args, x->nargs, sp, sizeof sp, &sl2);
			fprintf(stderr, "instance failed: %s: %s; at:", sp, c->error);
			for (uint32_t i = c->pos > 8 ? c->pos - 8 : 0; i < c->pos + 4 && i < c->ntok; i++)
				fprintf(stderr, " %.*s", (int)c->tok[i].len, c->tok[i].s);
			fputc('\n', stderr);
		}
	}
	c->instantiating--;
	cxx_state_restore(c, &s);
	return ok;
}

static void cxx_instance_complete_body(cxx *c, cxx_entity *inst);

/* an instance's class, completed: its body parsed (once).  What its members' declarations leave in the
   validity flags is not the verdict on the expression that needed the class. */
VPRIV void cxx_instance_complete(cxx *c, cxx_entity *inst)
{
	uint8_t invalid = c->einvalid, unsure = c->eunsure, missing = c->member_missing, adl = c->einvalid_unsure;

	if (!inst || !inst->instance || inst->instance->state)
		return;
	if (getenv("VCC_CXX_TRACE_COMPLETE")) {
		/* where an instance whose spelling contains the given text is completed, and the instantiations it is in */
		char sp[512];
		size_t sl;
		cxx_instance *x = inst->instance;

		if (cxx_instance_spell(c, x->t, x->args, x->nargs, sp, sizeof sp, &sl) && strstr(sp, getenv("VCC_CXX_TRACE_COMPLETE"))) {
			fprintf(stderr, "instance %s completed (instantiating %u, unevaluated %u) at:", sp, c->instantiating, c->unevaluated);
			for (uint32_t i = c->pos > 30 ? c->pos - 30 : 0; i < c->pos + 6 && i < c->ntok; i++)
				fprintf(stderr, " %.*s", (int)c->tok[i].len, c->tok[i].s);
			fputc('\n', stderr);
			if (getenv("VCC_CXX_TRACE_ABORT") && !strncmp(sp, getenv("VCC_CXX_TRACE_COMPLETE"), strlen(getenv("VCC_CXX_TRACE_COMPLETE"))))
				abort();	/* (a debugger shows who asked) */
		}
	}
	cxx_instance_complete_body(c, inst);
	c->einvalid = invalid, c->eunsure = unsure, c->member_missing = missing, c->einvalid_unsure = adl;
}

static void cxx_instance_complete_body(cxx *c, cxx_entity *inst)
{
	cxx_instance *x = inst ? inst->instance : NULL;

	int tentative = c->tentative;

	if (!x || x->state)
		return;
	/* (asked for in a tentative parse too: is J::type a type?  The instance's own parse is not tentative) */
	c->tentative = 0;
	if (x->late) {
		/* the template is defined now (else the instance stays incomplete): its own parameter names, and
		   the partial specializations declared since */
		cxx_template *t = x->t;
		cxx_partial *pick = NULL;
		cxx_targ bind[64];
		cxx_scope *binding;
		int r;

		if (x->selecting) {
			c->tentative = tentative;
			return;
		}
		x->selecting = 1;
		r = t->npartials ? cxx_partial_select(c, t, x->args, x->nargs, &pick, bind) : 0;
		x->selecting = 0;
		if (r < 0)
			c->partial_unknown++;
		if (r < 0 || (r == 0 && !t->params)) {
			c->tentative = tentative;
			return;
		}
		if (r > 0)
			c->partial_matches++;
		x->late = 0;
		if (r > 0) {
			binding = cxx_scope_new(c, CXX_SCOPE_BLOCK, pick->home, NULL);
			for (uint32_t i = 0; i < pick->params->ntparams; i++)
				if (bind[i].kind == 6)
					cxx_bind_pack(c, binding, &pick->params->tparams[i], bind[i].list + bind[i].t, (uint32_t)bind[i].v);
				else
					cxx_bind(c, binding, &pick->params->tparams[i], &bind[i]);
			x->pos = pick->key_pos;
		} else {
			binding = cxx_scope_new(c, CXX_SCOPE_BLOCK, t->home, NULL);
			for (uint32_t i = 0; i < t->params->ntparams; i++) {
				const cxx_tparam *tp = &t->params->tparams[i];

				if (tp->pack) {
					cxx_bind_pack(c, binding, tp, x->args + i, i <= x->nargs ? x->nargs - i : 0);
					break;
				}
				if (i < x->nargs)
					cxx_bind(c, binding, tp, &x->args[i]);
			}
		}
		x->binding = binding;
	}
	x->state = 1;
	x->state = cxx_instance_parse(c, x) ? 2 : 3;
	c->tentative = tentative;
}

/* a type that must be complete (a member by value, a base, X::name): an instance's body is parsed now */
VPRIV void cxx_type_require_complete(cxx *c, qty t)
{
	uint32_t tag;

	if (t == CXX_TY_UNKNOWN || !cc_type_is_record(c->ast, t))
		return;
	tag = AST_TYPE(c->ast, t)->aux;
	if (tag < c->ntag_instance && c->tag_instance[tag])
		cxx_instance_complete(c, ((cxx_instance *)c->tag_instance[tag])->inst);
}

/* the class scope of a record type (an instance: completed first), NULL if none */
VPRIV cxx_scope *cxx_type_class_scope(cxx *c, qty t)
{
	uint32_t tag;

	if (t == CXX_TY_UNKNOWN || !cc_type_is_record(c->ast, t))
		return NULL;
	cxx_type_require_complete(c, t);
	tag = AST_TYPE(c->ast, t)->aux;
	return tag < c->ntag_scope ? c->tag_scope[tag] : NULL;
}

VPRIV void cxx_tag_instance_set(cxx *c, uint32_t tag, void *x)
{
	if (tag >= c->ntag_instance) {
		uint32_t nn = tag + 1024;

		c->tag_instance = realloc(c->tag_instance, nn * sizeof *c->tag_instance);
		memset(c->tag_instance + c->ntag_instance, 0, (nn - c->ntag_instance) * sizeof *c->tag_instance);
		c->ntag_instance = nn;
	}
	c->tag_instance[tag] = x;
}

/* what an enclosing parse still uses while a body is instantiated inside it */
typedef struct cxx_outer {
	cxx_saved s;
	cxx_targ *targs;
	uint32_t ntargs, ndp, ndd;
	cxx_param *dp;
	qty this_type, etype;
	bool elval;
	cxx_entity *ecallee, *last_fn;
	uint32_t unevaluated;
	uint8_t this_const, ret_capture, ret_lval;
	qty ret_type;
} cxx_outer;

static void cxx_outer_save(cxx *c, cxx_outer *o)
{
	cxx_state_save(c, &o->s);
	o->ntargs = c->ntargs, o->ndp = c->ndecl_params, o->ndd = c->decl_ndefaults;
	o->targs = malloc((o->ntargs ? o->ntargs : 1) * sizeof *o->targs);
	memcpy(o->targs, c->targs, o->ntargs * sizeof *o->targs);
	o->dp = malloc((o->ndp ? o->ndp : 1) * sizeof *o->dp);
	memcpy(o->dp, c->decl_params, o->ndp * sizeof *o->dp);
	o->this_type = c->this_type, o->etype = c->etype, o->elval = c->elval, o->ecallee = c->ecallee, o->last_fn = c->last_fn;
	o->this_const = c->this_const, o->ret_capture = c->ret_capture, o->ret_lval = c->ret_lval, o->ret_type = c->ret_type;
	c->ret_capture = 0;
	o->unevaluated = c->unevaluated;
}

static void cxx_outer_restore(cxx *c, cxx_outer *o)
{
	cxx_state_restore(c, &o->s);
	memcpy(c->targs, o->targs, o->ntargs * sizeof *o->targs), c->ntargs = o->ntargs;
	memcpy(c->decl_params, o->dp, o->ndp * sizeof *o->dp), c->ndecl_params = o->ndp, c->decl_ndefaults = o->ndd;
	free(o->targs), free(o->dp);
	c->this_type = o->this_type, c->etype = o->etype, c->elval = o->elval, c->ecallee = o->ecallee, c->last_fn = o->last_fn;
	c->this_const = o->this_const, c->ret_capture = o->ret_capture, c->ret_lval = o->ret_lval, c->ret_type = o->ret_type;
	c->unevaluated = o->unevaluated;
}

/* ---- function templates: deduction from a call's argument types -------------------------- */

typedef struct cxx_fdone { cxx_targ *args; qty ret, sig; uint8_t body; int8_t viable; uint32_t n; int64_t value; } cxx_fdone;

/* a variable template's partial or explicit specialization: its pattern and its initializer */
typedef struct cxx_vspec { cxx_partial p; uint32_t init_pos; } cxx_vspec;

typedef struct cxx_ftemplate {
	cxx_partial p;		/* its parameters as placeholders (the partial specializations' machinery) */
	cxx_fdone *done;	/* the argument lists instantiated: their return types (the body parsed once) */
	uint32_t ndone, cdone;
	uint32_t pos;		/* its decl-specifiers */
	cxx_scope *ctor_class;	/* a constructor template: its class (no return type to parse) */
	uint32_t req_pos;	/* its template header's requires-clause (0: none) */
	qty *pt;		/* its parameters' types, placeholders for the template's (one may be unknown: not deduced from) */
	uint32_t np;
	uint8_t has_pt, variadic;
	uint32_t pack_tp, pack_param;	/* a trailing type pack Ts... and the parameter Ts&&... ts that expands it (index + 1; 0: none) */
	uint8_t defined;	/* this declaration has a body (another of the same function may be a declaration only) */
	cxx_vspec **vspecs;	/* a variable template: its specializations */
	uint32_t nvspecs;
	qty sig;		/* its function type, placeholders for the parameters (CXX_TY_UNKNOWN: not known) */
	uint8_t evaluated, packs;
} cxx_ftemplate;

VPRIV cxx_ftemplate *cxx_ftemplate_new(cxx *c, cxx_scope *params, cxx_scope *home, uint32_t pos, cxx_scope *ctor_class)
{
	cxx_ftemplate *f = calloc(1, sizeof *f);

	(void)c;
	f->p.params = params, f->p.home = home, f->pos = pos, f->sig = CXX_TY_UNKNOWN, f->ctor_class = ctor_class;
	return f;
}

VPRIV void cxx_ftemplate_requires(cxx_ftemplate *f, uint32_t pos)
{
	if (f)
		f->req_pos = pos;
}

/* parse the signature once, the parameters bound to placeholders */
static void cxx_ftemplate_evaluate(cxx *c, cxx_ftemplate *f)
{
	cxx_partial *p = &f->p;
	cxx_saved s;
	jmp_buf jb;
	cxx_scope *binding;

	if (f->evaluated)
		return;
	f->evaluated = 1;
	if (p->params->ntparams > 64)
		return;
	binding = cxx_scope_new(c, CXX_SCOPE_BLOCK, p->home, NULL);
	p->ph = calloc(p->params->ntparams + 1, sizeof *p->ph);
	for (uint32_t i = 0; i < p->params->ntparams; i++) {
		const cxx_tparam *tp = &p->params->tparams[i];
		cxx_entity *e;

		if (tp->pack && !tp->name) {
			f->packs = 1;	/* (unnamed packs: not deduced yet) */
			return;
		}
		if (tp->kind == 3) {
			/* a template template parameter: not deduced; its name in a parameter's type is a template
			   placeholder, so that the type has a shape (X<T, Layout>&) */
			f->packs = 1;
			if (tp->name && !tp->pack) {
				e = cxx_declare(c, binding, tp->name, CXX_ENTITY_TEMPLATE);
				if (e)
					e->dependent = 0, e->placeholder = (uint16_t)(i + 1);
			}
			continue;
		}
		if (tp->pack && (tp->kind != 1 || i + 1 != p->params->ntparams))
			f->packs = 1;	/* (value packs, packs not last: not deduced; their placeholders still give the parameters'
					   shapes, which rule the candidate out for arguments of another shape) */
		if (tp->pack && !f->packs)
			f->pack_tp = i + 1;	/* Ts...: a placeholder as a type's; the parameter Ts&&... ts gives its pattern */
		if (!tp->name)
			continue;	/* (unnamed: a default argument, a constraint in its type) */
		e = cxx_declare(c, binding, tp->name, tp->kind == 1 ? CXX_ENTITY_TYPE : CXX_ENTITY_VALUE);
		if (!e)
			return;
		e->dependent = 0;
		if (tp->pack)	/* tuple<Ts...> in a parameter's type: a pattern whose arguments end in the pack */
			e->is_pack = 1, e->pack_index = -1, e->pack_placeholder = (uint16_t)(i + 1), e->npack = 0;
		if (tp->kind == 1) {
			uint32_t tag = cc_tag_new(c->ast, TY_STRUCT, tp->name, 0);

			if (tag >= c->ntag_placeholder) {
				uint32_t nn = tag + 1024;

				c->tag_placeholder = realloc(c->tag_placeholder, nn * sizeof *c->tag_placeholder);
				memset(c->tag_placeholder + c->ntag_placeholder, 0, (nn - c->ntag_placeholder) * sizeof *c->tag_placeholder);
				c->ntag_placeholder = nn;
			}
			c->tag_placeholder[tag].owner = p, c->tag_placeholder[tag].index = i + 1;
			p->ph[i] = e->ty = QT(c->ast->tags[tag].ty, 0);
		} else
			e->placeholder = (uint16_t)(i + 1);
	}
	cxx_state_save(c, &s);
	c->instantiating++;
	c->fail = &jb;
	c->pos = f->pos;
	c->scope = binding;
	c->template_depth = 0, c->in_template_args = 0, c->explicit_specialization = 0, c->class_depth = 0, c->implicit_typename = 0;
	if (!setjmp(jb)) {
		f->sig = cxx_function_signature_parse(c, true, NULL, f->ctor_class);
		if (c->sig_is_function) {
			/* each parameter's type on its own: one that vcc cannot form does not stop the others */
			f->np = c->ndecl_params, f->variadic = c->decl_variadic, f->has_pt = 1;
			f->pt = malloc((f->np ? f->np : 1) * sizeof *f->pt);
			for (uint32_t i = 0; i < f->np; i++)
				f->pt[i] = c->decl_params[i].ty;
			/* the pack's parameter: the last one, its type a pattern over the pack's placeholder */
			if (f->pack_tp && c->decl_pack_param != UINT32_MAX && c->decl_pack_param + 1 == f->np &&
			    f->pt[f->np - 1] != CXX_TY_UNKNOWN)
				f->pack_param = f->np;
		}
	}
	if (f->pack_tp && !f->pack_param)
		f->packs = 1;	/* (the pack is not expanded by a plain last parameter: tuple<Ts...>, only explicit) */
	c->instantiating--;
	cxx_state_restore(c, &s);
	if (f->sig != CXX_TY_UNKNOWN && AST_TYPE_KIND(c->ast, f->sig) != TY_FUNC)
		f->sig = CXX_TY_UNKNOWN;
}

/*
 * The return type with the deduced arguments: the signature re-parsed with them bound (member typedefs,
 * partial specializations: as for a real instance).  want_body: the function is the one called, in
 * evaluated code: its body is instantiated too (once).  An auto return type needs the body anyway.
 */
static qty cxx_ftemplate_return(cxx *c, cxx_ftemplate *f, const cxx_targ *bind, bool want_body)
{
	cxx_partial *p = &f->p;
	uint32_t np = p->params->ntparams;
	cxx_saved s;
	jmp_buf jb;
	cxx_scope *binding;
	volatile qty r = CXX_TY_UNKNOWN, sig = CXX_TY_UNKNOWN;
	volatile bool body = false;
	volatile int8_t viable = 1;
	cxx_targ *targs;
	uint32_t ntargs = c->ntargs, ndp = c->ndecl_params, ndd = c->decl_ndefaults;
	cxx_param *dp;
	qty this_type = c->this_type, etype = c->etype;
	uint8_t this_const = c->this_const, ret_capture = c->ret_capture, ret_lval = c->ret_lval;
	qty ret_type = c->ret_type;
	bool elval = c->elval;
	cxx_entity *ecallee = c->ecallee;
	uint32_t unevaluated = c->unevaluated, at = UINT32_MAX;

	for (uint32_t i = 0; i < f->ndone; i++)
		if (cxx_args_equal(f->done[i].args, bind, np)) {
			if (!want_body || f->done[i].body) {
				c->stats.bodies_cached++;
				c->bound_sig = f->done[i].sig;
				c->sig_requires = f->done[i].viable;
				return f->done[i].ret;
			}
			at = i;	/* its signature was parsed; now its body */
			break;
		}
	c->bound_sig = CXX_TY_UNKNOWN;
	if (c->body_depth >= 64)
		return at != UINT32_MAX ? f->done[at].ret : CXX_TY_UNKNOWN;
	if (at == UINT32_MAX) {
		if (f->ndone == f->cdone)
			f->done = realloc(f->done, (f->cdone = f->cdone ? f->cdone * 2 : 4) * sizeof *f->done);
		f->done[f->ndone].args = malloc((np ? np : 1) * sizeof *bind);
		memcpy(f->done[f->ndone].args, bind, np * sizeof *bind);
		f->done[f->ndone].ret = CXX_TY_UNKNOWN, f->done[f->ndone].sig = CXX_TY_UNKNOWN, f->done[f->ndone].body = 0;
		f->done[f->ndone].viable = 1, f->done[f->ndone].n = np;
		at = f->ndone++;
	}
	if (want_body)
		f->done[at].body = 1;	/* (before the body: a recursive call finds it) */
	/* what the enclosing parse still uses */
	targs = malloc((ntargs ? ntargs : 1) * sizeof *targs);
	memcpy(targs, c->targs, ntargs * sizeof *targs);
	dp = malloc((ndp ? ndp : 1) * sizeof *dp);
	memcpy(dp, c->decl_params, ndp * sizeof *dp);
	binding = cxx_scope_new(c, CXX_SCOPE_BLOCK, p->home, NULL);
	for (uint32_t i = 0; i < p->params->ntparams; i++)
		cxx_bind(c, binding, &p->params->tparams[i], &bind[i]);
	cxx_state_save(c, &s);
	c->instantiating++;
	c->fail = &jb;
	c->pos = f->pos;
	c->scope = binding;
	c->template_depth = 0, c->in_template_args = 0, c->explicit_specialization = 0, c->class_depth = 0, c->implicit_typename = 0;
	/* a member template: this is its class */
	c->this_type = p->home->kind == CXX_SCOPE_CLASS && p->home->owner && p->home->owner->ty ? p->home->owner->ty : CXX_TY_UNKNOWN;
	c->body_depth++;
	if (c->body_depth > c->stats.body_depth_max)
		c->stats.body_depth_max = c->body_depth;
	c->sig_auto = 0;
	if (want_body)
		c->unevaluated = 0;	/* (the body is evaluated code, whatever the call was in) */
	if (!setjmp(jb)) {
		bool b = false;
		qty t = cxx_function_signature_parse(c, false, want_body ? &b : NULL, f->ctor_class);

		body = b;
		viable = c->sig_requires;
		if (t != CXX_TY_UNKNOWN && AST_TYPE_KIND(c->ast, t) == TY_FUNC)
			r = AST_TYPE(c->ast, t)->base, sig = c->sig_fake ? CXX_TY_UNKNOWN : t;	/* (its parameters: only if they are the real ones) */
		c->stats.bodies += body;
	} else
		c->stats.bodies_failed++;
	c->body_depth--;
	c->instantiating--;
	cxx_state_restore(c, &s);
	memcpy(c->targs, targs, ntargs * sizeof *targs), c->ntargs = ntargs;
	memcpy(c->decl_params, dp, ndp * sizeof *dp), c->ndecl_params = ndp, c->decl_ndefaults = ndd;
	free(targs), free(dp);
	c->this_type = this_type, c->etype = etype, c->elval = elval, c->ecallee = ecallee;
	c->this_const = this_const, c->ret_capture = ret_capture, c->ret_lval = ret_lval, c->ret_type = ret_type;
	c->unevaluated = unevaluated;
	/* auto with no trailing type: only the body tells (an unevaluated call instantiates it too) */
	if (!want_body && c->sig_auto && r == CXX_TY_UNKNOWN && viable == 1)
		return cxx_ftemplate_return(c, f, bind, true);
	for (uint32_t i = 0; i < f->ndone; i++)
		if (cxx_args_equal(f->done[i].args, bind, np)) {
			if (r != CXX_TY_UNKNOWN || f->done[i].ret == CXX_TY_UNKNOWN)
				f->done[i].ret = r, f->done[i].sig = sig;
			if (!want_body || viable != 1)
				f->done[i].viable = viable;	/* (not viable, however it was found out) */
			r = f->done[i].ret, sig = f->done[i].sig, viable = f->done[i].viable;
			break;
		}
	c->bound_sig = sig;
	c->sig_requires = viable;
	return r;
}

/*
 * A template whose last parameter is a pack, every argument given explicitly: its return type, the
 * signature parsed with the parameters bound (not cached, no body).  c->sig_requires: 0 when the
 * substitution fails.
 */
static qty cxx_ftemplate_return_pack(cxx *c, cxx_ftemplate *f, const cxx_targ *xargs, uint32_t nx, bool want_body, qty *ptypes,
				     uint32_t pn)
{
	cxx_partial *p = &f->p;
	uint32_t np = p->params->ntparams, at = UINT32_MAX;
	cxx_scope *binding;
	cxx_targ acopy[32];
	cxx_outer o;
	jmp_buf jb;
	volatile qty r = CXX_TY_UNKNOWN;
	volatile int8_t viable = -1;
	volatile bool body = false;

	if (c->body_depth >= 64 || nx > 32) {
		c->sig_requires = -1;
		return CXX_TY_UNKNOWN;
	}
	memcpy(acopy, xargs, nx * sizeof *xargs);
	/* done before with these arguments (the pack's elements are part of the key) */
	for (uint32_t i = 0; i < f->ndone; i++)
		if (f->done[i].n == nx + 1000 && cxx_args_equal(f->done[i].args, acopy, nx)) {
			if (!want_body || f->done[i].body) {
				c->bound_sig = f->done[i].sig, c->sig_requires = f->done[i].viable;
				return f->done[i].ret;
			}
			at = i;
			break;
		}
	if (at == UINT32_MAX) {
		if (f->ndone == f->cdone)
			f->done = realloc(f->done, (f->cdone = f->cdone ? f->cdone * 2 : 4) * sizeof *f->done);
		at = f->ndone++;
		memset(&f->done[at], 0, sizeof f->done[at]);
		f->done[at].args = malloc((nx ? nx : 1) * sizeof *acopy);
		memcpy(f->done[at].args, acopy, nx * sizeof *acopy);
		f->done[at].n = nx + 1000;	/* (apart from the fixed-length entries) */
		f->done[at].ret = CXX_TY_UNKNOWN, f->done[at].sig = CXX_TY_UNKNOWN, f->done[at].viable = 1;
	}
	if (want_body)
		f->done[at].body = 1;
	binding = cxx_scope_new(c, CXX_SCOPE_BLOCK, p->home, NULL);
	for (uint32_t i = 0; i + 1 < np; i++)
		cxx_bind(c, binding, &p->params->tparams[i], &acopy[i]);
	cxx_bind_pack(c, binding, &p->params->tparams[np - 1], acopy + (np - 1), nx - (np - 1));
	cxx_outer_save(c, &o);
	c->instantiating++, c->body_depth++;
	c->fail = &jb;
	c->pos = f->pos;
	c->scope = binding;
	c->template_depth = 0, c->in_template_args = 0, c->explicit_specialization = 0, c->class_depth = 0, c->implicit_typename = 0;
	c->this_type = p->home->kind == CXX_SCOPE_CLASS && p->home->owner && p->home->owner->ty ? p->home->owner->ty : CXX_TY_UNKNOWN;
	if (want_body) {
		c->unevaluated = 0;
		if (c->body_depth > c->stats.body_depth_max)
			c->stats.body_depth_max = c->body_depth;
	}
	c->sig_pack_types = ptypes, c->sig_pack_n = pn;	/* (the signature with the pack expanded; in a body, the pack of values) */
	if (!setjmp(jb)) {
		bool b = false;
		qty t = cxx_function_signature_parse(c, false, want_body ? &b : NULL, f->ctor_class);

		body = b;
		viable = c->sig_requires;
		if (t != CXX_TY_UNKNOWN && AST_TYPE_KIND(c->ast, t) == TY_FUNC)
			r = AST_TYPE(c->ast, t)->base;
		c->bound_sig = c->sig_fake ? CXX_TY_UNKNOWN : t;
		c->stats.bodies += body;
	} else {
		c->bound_sig = CXX_TY_UNKNOWN;
		if (want_body)
			c->stats.bodies_failed++;
	}
	c->instantiating--, c->body_depth--;
	{
		qty sig = c->bound_sig;

		cxx_outer_restore(c, &o);
		c->bound_sig = sig;
	}
	/* (found again: the body may have added entries) */
	for (uint32_t i = 0; i < f->ndone; i++)
		if (f->done[i].n == nx + 1000 && cxx_args_equal(f->done[i].args, acopy, nx)) {
			if (r != CXX_TY_UNKNOWN || f->done[i].ret == CXX_TY_UNKNOWN)
				f->done[i].ret = r, f->done[i].sig = c->bound_sig;
			if (!want_body || viable != 1)
				f->done[i].viable = viable;
			r = f->done[i].ret, viable = f->done[i].viable, c->bound_sig = f->done[i].sig;
			break;
		}
	c->sig_requires = viable;
	return r;
}

/* P against the base classes of the class A (cv as A's): true if one matches (bind then holds it) */
static bool cxx_match_bases(cxx *c, cxx_partial *p, qty P, qty A, cxx_targ *bind, int depth)
{
	cxx_scope *cls = depth < 8 ? cxx_type_class_scope(c, QUNQ(A)) : NULL;
	cxx_targ save[64];

	if (!cls || p->params->ntparams > 64)
		return false;
	memcpy(save, bind, p->params->ntparams * sizeof *bind);
	for (uint32_t i = 0; i < cls->nused; i++) {
		cxx_scope *b = cls->used[i];
		qty bt;

		if (!b || b->kind != CXX_SCOPE_CLASS || !b->owner || b->owner->ty == CXX_TY_UNKNOWN || !cc_type_is_record(c->ast, b->owner->ty))
			continue;
		bt = QT(QI(b->owner->ty), QQ(A));
		if (cxx_match_type(c, p, P, bt, bind, 0) || cxx_match_bases(c, p, P, bt, bind, depth + 1))
			return true;
		memcpy(bind, save, p->params->ntparams * sizeof *bind);	/* (a partial match binds nothing) */
	}
	return false;
}

/*
 * After deduction: the parameters left take their default arguments, and each value parameter's type
 * is formed with the earlier parameters bound (template<class T, enable_if_t<cond<T>, int> = 0>).
 * 1 all bound, 0 a substitution failure (typename X::type with no such member: not viable), -1 not
 * known.
 */
static int cxx_ftemplate_complete(cxx *c, cxx_ftemplate *f, cxx_targ *bind)
{
	cxx_partial *p = &f->p;

	for (uint32_t i = 0; i < p->params->ntparams; i++) {
		const cxx_tparam *tp = &p->params->tparams[i];
		bool check = tp->kind == 2 && tp->type_b && !tp->name;	/* (an unnamed value: there for its type) */
		cxx_scope *binding;
		cxx_outer o;
		jmp_buf jb;
		volatile bool failed = false;

		if (!bind[i].kind && !tp->def_b)
			return -1;
		if (bind[i].kind && !check)
			continue;
		binding = cxx_scope_new(c, CXX_SCOPE_BLOCK, p->home, NULL);
		for (uint32_t j = 0; j < i; j++)
			cxx_bind(c, binding, &p->params->tparams[j], &bind[j]);
		if (check) {
			cxx_outer_save(c, &o);
			c->instantiating++;
			c->fail = &jb;
			c->pos = tp->type_b;
			c->scope = binding;
			c->template_depth = 0, c->in_template_args = 0, c->explicit_specialization = 0, c->class_depth = 0, c->implicit_typename = 0;
			c->member_missing = 0;
			volatile qty got = CXX_TY_UNKNOWN;

			if (!setjmp(jb)) {
				cxx_type_specifiers_parse(c);
				got = c->ds_type;
			} else
				failed = true;
			c->instantiating--;
			cxx_outer_restore(c, &o);
			if (c->member_missing)
				return 0;
			if (failed || got == CXX_TY_UNKNOWN)
				return -1;	/* (enable_if_t<cond, int> with a cond vcc cannot evaluate: not known, not fine) */
		}
		if (!bind[i].kind) {
			c->member_missing = 0;
			if (!cxx_default_argument(c, tp, binding, &bind[i]))
				return c->member_missing ? 0 : -1;
			if (c->member_missing)
				return 0;
		}
	}
	return 1;
}

/* a constraint parsed with the parameters bound: fn at pos gives 1, 0 or -1 */
static int cxx_constraint_parse(cxx *c, cxx_partial *p, const cxx_targ *bind, uint32_t pos, const cxx_targ *first)
{
	cxx_scope *binding = cxx_scope_new(c, CXX_SCOPE_BLOCK, p->home, NULL);
	cxx_outer o;
	jmp_buf jb;
	volatile int v = -1;

	if (c->body_depth >= 64)
		return -1;
	for (uint32_t j = 0; j < p->params->ntparams; j++)
		if (bind[j].kind == 6)
			cxx_bind_pack(c, binding, &p->params->tparams[j], bind[j].list + bind[j].t, (uint32_t)bind[j].v);
		else if (bind[j].kind)
			cxx_bind(c, binding, &p->params->tparams[j], &bind[j]);
	cxx_outer_save(c, &o);
	c->instantiating++, c->body_depth++;
	c->fail = &jb;
	c->pos = pos;
	c->scope = binding;
	c->template_depth = 0, c->in_template_args = 0, c->explicit_specialization = 0, c->class_depth = 0, c->implicit_typename = 0;
	if (!setjmp(jb)) {
		if (first) {
			/* template<Concept<args> T>: Concept<T, args> */
			cxx_entity *ce = cxx_constraint_name_parse(c);
			cxx_targ a[32];

			if (ce && c->ntargs < 31) {
				a[0] = *first;
				memcpy(a + 1, c->targs, c->ntargs * sizeof *a);
				v = cxx_concept_value(c, ce, a, c->ntargs + 1);
			}
		} else
			v = cxx_requires_clause_value(c);
	}
	c->instantiating--, c->body_depth--;
	cxx_outer_restore(c, &o);
	return v;
}

/* the template's constraints with its parameters bound: constrained parameters, the header's requires-clause */
static int cxx_ftemplate_constraints(cxx *c, cxx_ftemplate *f, const cxx_targ *bind)
{
	cxx_partial *p = &f->p;

	for (uint32_t i = 0; i < p->params->ntparams; i++) {
		const cxx_tparam *tp = &p->params->tparams[i];

		if (tp->constraint_b && bind[i].kind == 1) {
			int v = cxx_constraint_parse(c, p, bind, tp->constraint_b, &bind[i]);

			if (v <= 0)
				return v;
		}
	}
	return f->req_pos ? cxx_constraint_parse(c, p, bind, f->req_pos, NULL) : 1;
}

/* is the class a an instance of the template t, or derived from one? */
static bool cxx_instance_of(cxx *c, qty a, const cxx_template *t, int depth)
{
	cxx_instance *ia = cxx_tag_info(c, a);
	cxx_scope *cls;

	if (ia && ia->t == t)
		return true;
	cls = depth < 8 ? cxx_type_class_scope(c, QUNQ(a)) : NULL;
	if (!cls || cls->has_dependent_base)
		return cls != NULL;	/* (bases not known: it may be) */
	for (uint32_t i = 0; i < cls->nused; i++) {
		cxx_scope *b = cls->used[i];

		if (b && b->kind == CXX_SCOPE_CLASS && b->owner && b->owner->ty != CXX_TY_UNKNOWN && cc_type_is_record(c->ast, b->owner->ty) &&
		    cxx_instance_of(c, b->owner->ty, t, depth + 1))
			return true;
	}
	return false;
}

/* can no argument of type A match the pattern P, whatever its parameters are?  X<...> against what is not
   an X (or derived from one), a pointer or array pattern against what is not one */
static bool cxx_shape_mismatch(cxx *c, qty P, qty A)
{
	const ctype *pt, *at;
	void *owner;

	if (P == CXX_TY_UNKNOWN || A == CXX_TY_UNKNOWN || !cxx_mentions_placeholder(c, P, 0))
		return false;
	pt = AST_TYPE(c->ast, P);
	if (pt->kind == TY_PTR && pt->ext)
		P = pt->base, pt = AST_TYPE(c->ast, P);
	if (cxx_placeholder_of(c, P, &owner) >= 0)
		return false;	/* a bare T: anything */
	at = AST_TYPE(c->ast, A);
	if (pt->kind == TY_STRUCT || pt->kind == TY_UNION) {
		cxx_instance *ip = cxx_tag_info(c, P);

		if (!ip)
			return false;
		if (at->kind != TY_STRUCT && at->kind != TY_UNION)
			return true;
		return !cxx_instance_of(c, A, ip->t, 0);
	}
	if (pt->kind == TY_PTR)
		return !((at->kind == TY_PTR && !at->ext) || at->kind == TY_ARRAY || at->kind == TY_FUNC);
	if (pt->kind == TY_ARRAY)
		return at->kind != TY_ARRAY;
	return false;
}

/* one argument (type A, an lvalue or not) against a parameter's pattern P: 1 deduced or nothing to deduce,
   0 no match, -1 not known */
static int cxx_deduce_argument(cxx *c, cxx_partial *p, qty P, qty A, bool lvalue, cxx_targ *bind)
{
	if (P == CXX_TY_UNKNOWN || !cxx_mentions_placeholder(c, P, 0))
		return 1;	/* (non-deduced: its conversion is not checked here) */
	if (A == CXX_TY_UNKNOWN || AST_TYPE_KIND(c->ast, A) == TY_VOID) {
		c->why = CXX_WHY_ARG;
		return -1;	/* (void: a type vcc got wrong; no argument has it) */
	}
	if (AST_TYPE_KIND(c->ast, P) == TY_PTR && AST_TYPE(c->ast, P)->ext) {
		int ext = AST_TYPE(c->ast, P)->ext;
		void *owner;

		P = AST_TYPE(c->ast, P)->base;
		/* T&& with an lvalue: a forwarding reference deduces T as A& */
		if (ext == 2 && !QQ(P) && lvalue && cxx_placeholder_of(c, P, &owner) >= 0 && owner == p)
			A = cxx_type_reference(c, A, false);
		else
			A = QT(QI(A), QQ(A) | QQ(P));	/* (const T& binds what is not const too) */
	} else {
		/* by value: arrays and functions decay, top-level cv goes */
		const ctype *at = AST_TYPE(c->ast, A);

		if (at->kind == TY_ARRAY)
			A = cc_type_pointer(c->ast, at->base);
		else if (at->kind == TY_FUNC)
			A = cc_type_pointer(c->ast, A);
		A = QUNQ(A);
		P = QUNQ(P);	/* (a by-value parameter's own const is not part of the function's type) */
	}
	if (!cxx_match_type(c, p, P, A, bind, 0)) {
		/* X<T> against a class derived from X<int>: its bases, nearest first */
		if (AST_TYPE_KIND(c->ast, QUNQ(A)) == TY_STRUCT && cxx_match_bases(c, p, P, A, bind, 0))
			return 1;
		/* (deduction does not convert: a pattern that matches neither the argument nor a base of it fails) */
		c->why = CXX_WHY_CLASS_MATCH;
		return 0;
	}
	return 1;
}

/* pat with p's placeholder (the pack's) replaced by t: T, const T, T&, T&&, T* */
static qty cxx_placeholder_subst(cxx *c, cxx_partial *p, qty pat, qty t, int depth)
{
	const ctype *x;
	void *owner;

	if (pat == CXX_TY_UNKNOWN || depth > 16)
		return CXX_TY_UNKNOWN;
	if (cxx_placeholder_of(c, pat, &owner) >= 0)
		return owner == p ? (AST_TYPE_KIND(c->ast, t) == TY_PTR && AST_TYPE(c->ast, t)->ext ? t : cc_type_qualify(c->ast, t, QQ(pat))) :
				    CXX_TY_UNKNOWN;
	x = AST_TYPE(c->ast, pat);
	if (x->kind == TY_PTR) {
		qty b = cxx_placeholder_subst(c, p, x->base, t, depth + 1);

		if (b == CXX_TY_UNKNOWN)
			return b;
		if (x->ext)
			return cxx_type_reference(c, b, x->ext == 2);	/* (collapses) */
		return cc_type_qualify(c->ast, cc_type_pointer(c->ast, b), QQ(pat));
	}
	return cxx_mentions_placeholder(c, pat, 0) ? CXX_TY_UNKNOWN : pat;
}

/*
 * Deduce e's template arguments from a call: 0 not viable (deduction failed), 1 viable, *ret its
 * return type (CXX_TY_UNKNOWN if it cannot be formed), -1 not known (vcc cannot deduce it).
 */
static int cxx_ftemplate_deduce1(cxx *c, cxx_entity *e, const cxx_targ *xargs, uint32_t nxargs, const qty *args, const uint8_t *lval,
				 uint32_t nargs, qty *ret, bool body);

/* (a candidate that does not deduce, or whose default arguments or constraints fail, is not an error
   in the expression that calls: what its evaluation left in the validity flags goes with it) */
VPRIV int cxx_ftemplate_deduce(cxx *c, cxx_entity *e, const cxx_targ *xargs, uint32_t nxargs, const qty *args, const uint8_t *lval,
			      uint32_t nargs, qty *ret, bool body)
{
	uint8_t invalid = c->einvalid, unsure = c->eunsure, missing = c->member_missing, adl = c->einvalid_unsure;
	int r = cxx_ftemplate_deduce1(c, e, xargs, nxargs, args, lval, nargs, ret, body);

	c->einvalid = invalid, c->eunsure = unsure, c->member_missing = missing, c->einvalid_unsure = adl;
	return r;
}

static int cxx_ftemplate_deduce1(cxx *c, cxx_entity *e, const cxx_targ *xargs, uint32_t nxargs, const qty *args, const uint8_t *lval,
				 uint32_t nargs, qty *ret, bool body)
{
	cxx_ftemplate *f = e->ftmpl;
	cxx_partial *p;
	const ctype *ft;
	cxx_targ bind[64];

	*ret = CXX_TY_UNKNOWN;
	if (!f) {
		c->why = CXX_WHY_NO_FTMPL;
		return -1;
	}
	cxx_ftemplate_evaluate(c, f);
	p = &f->p;
	/* (make<T>(args...) with a deducible pack goes below: T explicit, the pack from the arguments) */
	if (f->packs && xargs && p->params->ntparams && p->params->ntparams <= 32) {
		/* f<A, Ps...>(args) with the last parameter a pack and every argument explicit (libc++'s
		   __and_helper<_Pred...>(0)): the signature with them bound */
		uint32_t np = p->params->ntparams;
		bool simple = p->params->tparams[np - 1].pack && p->params->tparams[np - 1].kind != 3 && nxargs + 1 >= np;

		for (uint32_t i = 0; i + 1 < np && simple; i++)
			simple = !p->params->tparams[i].pack && p->params->tparams[i].kind != 3 && xargs[i].kind &&
				 (xargs[i].kind == 1) == (p->params->tparams[i].kind == 1);
		for (uint32_t i = np - 1; i < nxargs && simple; i++)
			simple = xargs[i].kind == 1 || xargs[i].kind == 2;
		if (simple) {
			*ret = cxx_ftemplate_return_pack(c, f, xargs, nxargs, false, NULL, UINT32_MAX);
			return c->sig_requires == 1 ? 1 : c->sig_requires;
		}
	}
	if (f->packs || p->params->ntparams > 64) {
		/* not deduced, but a parameter whose shape the argument cannot have rules it out: swap(tuple<Ts...>&,
		   tuple<Ts...>&) for two ints */
		if (f->has_pt && f->packs && !f->variadic)
			for (uint32_t i = 0; i < nargs && i < f->np; i++)
				if (cxx_shape_mismatch(c, f->pt[i], args[i]))
					return 0;
		c->why = CXX_WHY_PACKS;
		return -1;
	}
	memset(bind, 0, p->params->ntparams * sizeof *bind);
	/* every parameter given explicitly: the placeholder signature is not needed (its parameter types may
	   not parse with placeholders: typename remove_reference<T>::type&) */
	if (xargs && nxargs == p->params->ntparams) {
		for (uint32_t i = 0; i < nxargs; i++) {
			if (!xargs[i].kind || xargs[i].kind > 2 || (xargs[i].kind == 1) != (p->params->tparams[i].kind == 1))
				return xargs[i].kind ? 0 : -1;
			bind[i] = xargs[i];
		}
		{
			int k = cxx_ftemplate_constraints(c, f, bind);

			if (k <= 0)
				return k;
		}
		*ret = cxx_ftemplate_return(c, f, bind, body);
		if (c->sig_requires != 1)
			return c->sig_requires;
		return 1;	/* (its arity is not checked: the signature with the arguments bound would tell) */
	}
	if (!f->has_pt) {
		c->why = CXX_WHY_SIG;
		return -1;
	}
	(void)ft;
	if (!f->pack_tp) {	/* (a pack takes any number of arguments) */
		if (nargs > f->np && !f->variadic)
			return 0;
		if (nargs + e->ndefaults < f->np)
			return 0;
	}
	/* explicit arguments: the first parameters (all of them: nothing left to deduce) */
	if (xargs) {
		if (nxargs > p->params->ntparams)
			return 0;
		for (uint32_t i = 0; i < nxargs; i++) {
			if (!xargs[i].kind || xargs[i].kind > 2 || (xargs[i].kind == 1) != (p->params->tparams[i].kind == 1))
				return xargs[i].kind ? 0 : -1;
			bind[i] = xargs[i];
		}
	}
	if (f->pack_tp) {
		/* f(A a, Ts&&... ts): the fixed parameters, then each further argument against the pack's pattern */
		uint32_t ntp = p->params->ntparams, fixed = f->np - 1, pk = f->pack_tp - 1, ne = 0;
		cxx_targ flat[32];
		qty etypes[16];

		if (nargs < fixed)
			return 0;
		if (nargs - fixed > 16 || ntp > 16 || f->req_pos || (xargs && nxargs > pk)) {	/* (explicit elements of the pack: not mixed) */
			c->why = CXX_WHY_PACKS;
			return -1;
		}
		for (uint32_t i = 0; i + 1 < ntp; i++)
			if (!p->params->tparams[i].name || p->params->tparams[i].def_b || p->params->tparams[i].constraint_b) {
				c->why = CXX_WHY_PACKS;
				return -1;	/* (constraints and defaults beside a pack: not evaluated here) */
			}
		for (uint32_t i = 0; i < fixed; i++) {
			int r = cxx_deduce_argument(c, p, f->pt[i], args[i], lval[i], bind);

			if (r <= 0)
				return r;
		}
		for (uint32_t i = fixed; i < nargs; i++) {
			int r;

			bind[pk].kind = 0;
			r = cxx_deduce_argument(c, p, f->pt[fixed], args[i], lval[i], bind);
			if (r <= 0)
				return r;
			if (bind[pk].kind != 1) {
				c->why = CXX_WHY_PACKS;
				return -1;
			}
			/* the element's parameter type: the pattern with this element (Ts&& with T = A&: A&) */
			etypes[ne] = cxx_placeholder_subst(c, p, f->pt[fixed], bind[pk].t, 0);
			if (etypes[ne] == CXX_TY_UNKNOWN) {
				c->why = CXX_WHY_PACKS;
				return -1;
			}
			flat[ntp - 1 + ne++] = bind[pk];
		}
		bind[pk].kind = 0;
		for (uint32_t i = 0; i + 1 < ntp; i++) {
			if (!bind[i].kind) {
				c->why = CXX_WHY_UNBOUND;
				return -1;
			}
			flat[i] = bind[i];
		}
		*ret = cxx_ftemplate_return_pack(c, f, flat, ntp - 1 + ne, body, etypes, ne);
		return c->sig_requires == 1 ? 1 : c->sig_requires;
	}
	for (uint32_t i = 0; i < nargs && i < f->np; i++) {
		int r = cxx_deduce_argument(c, p, f->pt[i], args[i], lval[i], bind);

		if (r <= 0 && getenv("VCC_CXX_SIG"))
			fprintf(stderr, "deduce: argument %u: %d\n", i, r);
		if (r <= 0)
			return r;
	}
	{
		int k = cxx_ftemplate_complete(c, f, bind);

		if (k <= 0 && getenv("VCC_CXX_SIG"))
			fprintf(stderr, "deduce: complete: %d\n", k);
		if (k > 0) {
			k = cxx_ftemplate_constraints(c, f, bind);
			if (k <= 0 && getenv("VCC_CXX_SIG"))
				fprintf(stderr, "deduce: constraints: %d\n", k);
		}
		if (k <= 0) {
			c->why = CXX_WHY_UNBOUND;
			return k;
		}
	}
	*ret = cxx_ftemplate_return(c, f, bind, body);
	if (c->sig_requires != 1 && getenv("VCC_CXX_SIG"))
		fprintf(stderr, "deduce: signature: %d\n", c->sig_requires);
	if (c->sig_requires != 1) {
		c->why = CXX_WHY_UNBOUND;
		return c->sig_requires;	/* its trailing requires-clause: false, or not known */
	}
	return 1;
}

VPRIV void cxx_template_member_definition(cxx *c, cxx_entity *te, uint32_t name, cxx_scope *params, cxx_scope *home, uint32_t pos,
					    uint32_t nparams, bool is_const)
{
	cxx_template *t = cxx_template_of(te);

	(void)c;
	t->oocs = realloc(t->oocs, (t->noocs + 1) * sizeof *t->oocs);
	t->oocs[t->noocs++] = (struct cxx_ooc){ .name = name, .pos = pos, .nparams = nparams, .params = params, .home = home,
						.is_const = is_const };
}

/*
 * A call uses e, a member function of a class template's instance declared without a body: its
 * definition outside the class (matched by name, parameter count and const), parsed as an ordinary
 * out-of-class member definition with the definition's template parameters bound to the instance's
 * arguments.
 */
static void cxx_member_definition_use(cxx *c, cxx_entity *e)
{
	cxx_scope *cs = e->member_of;
	cxx_instance *x = cs && cs->kind == CXX_SCOPE_CLASS && cs->owner ? cs->owner->instance : NULL;
	cxx_template *t = x ? x->t : NULL;
	const struct cxx_ooc *d = NULL;
	uint32_t nparams, matches = 0;
	cxx_scope *binding;
	cxx_outer o;
	jmp_buf jb;

	if (!t || !t->noocs || e->ty == CXX_TY_UNKNOWN || AST_TYPE_KIND(c->ast, e->ty) != TY_FUNC)
		return;
	if (x->pos)
		return;	/* (an instance of a partial specialization: its members' definitions are not the primary's) */
	nparams = AST_TYPE(c->ast, e->ty)->n;
	for (uint32_t i = 0; i < t->noocs; i++)
		if (t->oocs[i].name == e->name && t->oocs[i].nparams == nparams && t->oocs[i].is_const == e->fn_const)
			d = &t->oocs[i], matches++;
	if (!matches)
		return;	/* (declared only: defined in another TU, or = default) */
	e->late_done = 1;
	if (matches > 1) {
		c->stats.ooc_ambiguous++;	/* (overloads with the same parameter count: their types would tell) */
		return;
	}
	/* the definition's own template parameters are the class template's, in order */
	binding = cxx_scope_new(c, CXX_SCOPE_BLOCK, d->home, NULL);
	for (uint32_t i = 0; i < d->params->ntparams; i++) {
		const cxx_tparam *tp = &d->params->tparams[i];

		if (tp->pack) {
			if (i + 1 != d->params->ntparams || i > x->nargs)
				return;
			cxx_bind_pack(c, binding, tp, x->args + i, x->nargs - i);
			break;
		}
		if (i >= x->nargs)
			return;
		cxx_bind(c, binding, tp, &x->args[i]);
	}
	cxx_outer_save(c, &o);
	c->instantiating++, c->body_depth++;
	if (c->body_depth > c->stats.body_depth_max)
		c->stats.body_depth_max = c->body_depth;
	c->unevaluated = 0;
	c->fail = &jb;
	c->pos = d->pos;
	c->scope = binding;
	c->template_depth = 0, c->in_template_args = 0, c->explicit_specialization = 0, c->class_depth = 0, c->implicit_typename = 0;
	c->this_type = cs->owner->ty ? cs->owner->ty : CXX_TY_UNKNOWN, c->this_const = e->fn_const;
	if (!setjmp(jb)) {
		cxx_declaration_parse(c, CXX_CONTEXT_NAMESPACE);
		c->stats.ooc_bodies++;
	} else
		c->stats.ooc_failed++;
	c->instantiating--, c->body_depth--;
	cxx_outer_restore(c, &o);
}

/* a call uses e: an instance's member function, its body parsed now (once) */
VPRIV void cxx_member_body_use(cxx *c, cxx_entity *e)
{
	cxx_outer o;
	jmp_buf jb;

	if (e && !e->late && !e->late_done && !c->tentative && c->body_depth < 64 && !c->unevaluated && e->member_of) {
		cxx_member_definition_use(c, e);
		return;
	}
	if (!e || !e->late || e->late_done || c->tentative || c->body_depth >= 64 || c->unevaluated)
		return;
	e->late_done = 1;
	if (getenv("VCC_CXX_BODIES") && e->late->scope && e->late->scope->owner)
		fprintf(stderr, "body %.*s::%.*s\n", (int)vintern_ents[e->late->scope->owner->name].len, vintern_ents[e->late->scope->owner->name].s,
			(int)vintern_ents[e->name].len, vintern_ents[e->name].s);
	cxx_outer_save(c, &o);
	c->instantiating++;
	c->body_depth++;
	if (c->body_depth > c->stats.body_depth_max)
		c->stats.body_depth_max = c->body_depth;
	c->fail = &jb;
	c->in_template_args = 0, c->explicit_specialization = 0, c->implicit_typename = 0;
	if (!setjmp(jb)) {
		cxx_late_parse_one(c, e->late);
		c->stats.member_bodies++;
	} else
		c->stats.member_bodies_failed++;
	c->body_depth--;
	c->instantiating--;
	cxx_outer_restore(c, &o);
}

VPRIV bool cxx_type_has_placeholder(cxx *c, qty t)
{
	return cxx_mentions_placeholder(c, t, 0);
}

/* an entity for a computed type (a builtin transform's result): a type name, a class's members reachable */
VPRIV cxx_entity *cxx_type_entity(cxx *c, qty t)
{
	cxx_entity *e = calloc(1, sizeof *e);

	e->kind = CXX_ENTITY_TYPE, e->ty = t, e->pack_index = -1;
	if (t != CXX_TY_UNKNOWN && cc_type_is_record(c->ast, t)) {
		uint32_t tag = AST_TYPE(c->ast, t)->aux;

		if (tag < c->ntag_scope)
			e->members = c->tag_scope[tag];
	}
	return e;
}

/* ---- auto: one type parameter deduced from an initializer --------------------------------- */

/* auto's placeholder: the declarator is parsed over it (const auto &x: a reference to const AUTO) */
VPRIV qty cxx_auto_type(cxx *c)
{
	if (!c->auto_ty) {
		uint32_t tag = cc_tag_new(c->ast, TY_STRUCT, vintern_cstr("auto"), 0);

		c->auto_owner = calloc(1, sizeof *c->auto_owner);
		if (tag >= c->ntag_placeholder) {
			uint32_t nn = tag + 1024;

			c->tag_placeholder = realloc(c->tag_placeholder, nn * sizeof *c->tag_placeholder);
			memset(c->tag_placeholder + c->ntag_placeholder, 0, (nn - c->ntag_placeholder) * sizeof *c->tag_placeholder);
			c->ntag_placeholder = nn;
		}
		c->tag_placeholder[tag].owner = c->auto_owner, c->tag_placeholder[tag].index = 1;
		c->auto_ty = QT(c->ast->tags[tag].ty, 0);
	}
	return c->auto_ty;
}

/* pattern with auto replaced by t */
static qty cxx_auto_subst(cxx *c, qty pat, qty t, int depth)
{
	const ctype *x;
	void *owner;

	if (pat == CXX_TY_UNKNOWN || depth > 16)
		return CXX_TY_UNKNOWN;
	if (cxx_placeholder_of(c, pat, &owner) >= 0)
		return owner == c->auto_owner ? (AST_TYPE_KIND(c->ast, t) == TY_PTR && AST_TYPE(c->ast, t)->ext ? t :
						 cc_type_qualify(c->ast, t, QQ(pat))) : CXX_TY_UNKNOWN;
	x = AST_TYPE(c->ast, pat);
	if (x->kind == TY_PTR) {
		qty b = cxx_auto_subst(c, x->base, t, depth + 1);

		if (b == CXX_TY_UNKNOWN)
			return b;
		if (x->ext)	/* auto&& with T = A&: A& */
			return AST_TYPE_KIND(c->ast, b) == TY_PTR && AST_TYPE(c->ast, b)->ext ? b : cxx_type_reference(c, b, x->ext == 2);
		return cc_type_qualify(c->ast, cc_type_pointer(c->ast, b), QQ(pat));
	}
	return cxx_mentions_placeholder(c, pat, 0) ? CXX_TY_UNKNOWN : pat;	/* (auto x[] = ..., a function of auto: not yet) */
}

/*
 * The type of a variable declared with pattern (auto, const auto&, auto*, auto&&) from an initializer
 * of type a: template argument deduction with one parameter.  CXX_TY_UNKNOWN if it does not deduce.
 */
VPRIV qty cxx_auto_deduce(cxx *c, qty pattern, qty a, bool lvalue)
{
	qty P = pattern;
	cxx_targ bind[1];
	void *owner;

	if (pattern == CXX_TY_UNKNOWN || a == CXX_TY_UNKNOWN || AST_TYPE_KIND(c->ast, a) == TY_VOID)
		return CXX_TY_UNKNOWN;
	memset(bind, 0, sizeof bind);
	if (AST_TYPE_KIND(c->ast, P) == TY_PTR && AST_TYPE(c->ast, P)->ext) {
		int ext = AST_TYPE(c->ast, P)->ext;

		P = AST_TYPE(c->ast, P)->base;
		if (ext == 2 && !QQ(P) && lvalue && cxx_placeholder_of(c, P, &owner) >= 0)
			a = cxx_type_reference(c, a, false);	/* auto&& bound to an lvalue */
		else
			a = QT(QI(a), QQ(a) | QQ(P));	/* (const auto& binds what is not const too) */
	} else {
		const ctype *at = AST_TYPE(c->ast, a);

		if (at->kind == TY_ARRAY)
			a = cc_type_pointer(c->ast, at->base);
		else if (at->kind == TY_FUNC)
			a = cc_type_pointer(c->ast, a);
		a = QT(QI(a), QQ(P));	/* by value: the initializer's cv goes, the declaration's stays */
	}
	if (!cxx_match_type(c, c->auto_owner, P, a, bind, 0) || bind[0].kind != 1)
		return CXX_TY_UNKNOWN;
	return cxx_auto_subst(c, pattern, bind[0].t, 0);
}

/* ---- concepts ------------------------------------------------------------------------------ */

/*
 * Concept<args>: its constraint-expression with the arguments bound, as a constant: 1 satisfied,
 * 0 not, -1 not known (an argument or an atomic constraint vcc cannot evaluate).
 */
VPRIV int cxx_concept_value(cxx *c, cxx_entity *e, const cxx_targ *args, uint32_t n)
{
	cxx_ftemplate *f = e ? e->ftmpl : NULL;
	cxx_scope *ps, *binding;
	cxx_outer o;
	jmp_buf jb;
	volatile int v = -1;
	uint32_t at;

	cxx_targ acopy[32];

	if (!f || n > 32 || c->body_depth >= 64 || c->tentative)
		return -1;
	memcpy(acopy, args, n * sizeof *args);	/* (often c->targs, which the parse below reuses) */
	args = acopy;
	ps = f->p.params;
	for (uint32_t i = 0; i < n; i++)
		if (!args[i].kind || args[i].kind == 3 || args[i].kind == 5 || (args[i].kind == 1 && cxx_mentions_placeholder(c, args[i].t, 0)))
			return -1;
	for (uint32_t i = 0; i < f->ndone; i++)
		if (f->done[i].n == n && cxx_args_equal(f->done[i].args, args, n))
			return f->done[i].viable;
	binding = cxx_scope_new(c, CXX_SCOPE_BLOCK, f->p.home, NULL);
	for (uint32_t i = 0; i < ps->ntparams; i++) {
		const cxx_tparam *tp = &ps->tparams[i];

		if (tp->pack) {
			if (i + 1 != ps->ntparams || i > n)
				return -1;
			cxx_bind_pack(c, binding, tp, args + i, n - i);
			break;
		}
		if (i >= n || (args[i].kind == 1) != (tp->kind == 1))
			return -1;	/* (default arguments of a concept: not yet) */
		cxx_bind(c, binding, tp, &args[i]);
	}
	if (f->ndone == f->cdone)
		f->done = realloc(f->done, (f->cdone = f->cdone ? f->cdone * 2 : 4) * sizeof *f->done);
	at = f->ndone++;
	memset(&f->done[at], 0, sizeof f->done[at]);
	f->done[at].args = malloc((n ? n : 1) * sizeof *args);
	memcpy(f->done[at].args, args, n * sizeof *args);
	f->done[at].n = n, f->done[at].viable = -1;	/* (a concept that names itself: not known) */
	cxx_outer_save(c, &o);
	c->instantiating++, c->body_depth++, c->unevaluated++;
	c->fail = &jb;
	c->pos = f->pos;
	c->scope = binding;
	c->template_depth = 0, c->in_template_args = 0, c->explicit_specialization = 0, c->class_depth = 0, c->implicit_typename = 0;
	if (!setjmp(jb)) {
		cxx_conditional_expression_parse(c);
		if (c->econst)
			v = c->evalue != 0;
	}
	c->instantiating--, c->body_depth--;
	cxx_outer_restore(c, &o);
	f->done[at].viable = (int8_t)v;
	return v;
}

/* ---- variable templates -------------------------------------------------------------------- */

VPRIV void cxx_vtemplate_special(cxx *c, cxx_ftemplate *f, cxx_scope *params, cxx_scope *home, uint32_t less_pos, uint32_t init_pos)
{
	cxx_vspec *sp = calloc(1, sizeof *sp);

	/* (an explicit specialization: no parameters) */
	sp->p.params = params ? params : cxx_scope_new(c, CXX_SCOPE_TEMPLATE, home, NULL);
	sp->p.home = home, sp->p.less_pos = less_pos, sp->init_pos = init_pos;
	f->vspecs = realloc(f->vspecs, (f->nvspecs + 1) * sizeof *f->vspecs);
	f->vspecs[f->nvspecs++] = sp;
}

/*
 * name<args> for a variable template: the initializer of the specialization the arguments select (the
 * most specialized partial or explicit one that matches, else the primary), as a constant.  false: not
 * known.
 */
VPRIV bool cxx_vtemplate_value(cxx *c, cxx_entity *e, const cxx_targ *args, uint32_t n, int64_t *out)
{
	cxx_ftemplate *f = e ? e->ftmpl : NULL;
	cxx_scope *ps, *binding;
	cxx_vspec *pick = NULL;
	cxx_targ bind[64], tmp[64];
	cxx_outer o;
	jmp_buf jb;
	volatile bool known = false;
	volatile int64_t v = 0;
	uint32_t at, pos;

	cxx_targ acopy[32];

	if (!f || n > 32 || c->body_depth >= 64 || c->tentative)
		return false;
	memcpy(acopy, args, n * sizeof *args);	/* (often c->targs, which the parses below reuse) */
	args = acopy;
	ps = f->p.params;
	for (uint32_t i = 0; i < n; i++)
		if (!args[i].kind || args[i].kind == 3 || args[i].kind == 5 || (args[i].kind == 1 && cxx_mentions_placeholder(c, args[i].t, 0)))
			return false;
	for (uint32_t i = 0; i < f->ndone; i++)
		if (f->done[i].n == n && cxx_args_equal(f->done[i].args, args, n)) {
			*out = f->done[i].value;
			return f->done[i].viable == 1;
		}
	/* the complete argument list: default arguments for the rest (template<class T, class = void>) */
	if (n < ps->ntparams && !(ps->ntparams && ps->tparams[ps->ntparams - 1].pack)) {
		static __thread cxx_targ full[32];
		cxx_scope *b = cxx_scope_new(c, CXX_SCOPE_BLOCK, f->p.home, NULL);

		if (ps->ntparams > 32)
			return false;
		memcpy(full, args, n * sizeof *args);
		for (uint32_t i = 0; i < ps->ntparams; i++) {
			if (i >= n && (!ps->tparams[i].def_b || !cxx_default_argument(c, &ps->tparams[i], b, &full[i])))
				return false;
			cxx_bind(c, b, &ps->tparams[i], &full[i]);
		}
		memcpy(acopy, full, ps->ntparams * sizeof *full);
		n = ps->ntparams;
		for (uint32_t i = 0; i < f->ndone; i++)
			if (f->done[i].n == n && cxx_args_equal(f->done[i].args, args, n)) {
				*out = f->done[i].value;
				return f->done[i].viable == 1;
			}
	}
	/* the specialization: every pattern must be one vcc can match */
	for (uint32_t i = 0; i < f->nvspecs; i++) {
		cxx_vspec *sp = f->vspecs[i];

		cxx_partial_evaluate(c, &sp->p);
		if (!sp->p.ok || sp->p.params->ntparams > 64 || !cxx_partial_complete(c, &sp->p, ps, NULL, n))
			return false;
		{
			int m = cxx_partial_matches(c, &sp->p, args, n, tmp);

			if (m < 0)
				return false;
			if (!m)
				continue;
		}
		if (pick && !(cxx_partial_at_least_as_specialized(c, &sp->p, &pick->p) && !cxx_partial_at_least_as_specialized(c, &pick->p, &sp->p))) {
			if (!(cxx_partial_at_least_as_specialized(c, &pick->p, &sp->p) && !cxx_partial_at_least_as_specialized(c, &sp->p, &pick->p)))
				return false;	/* (neither is more specialized: not decided here) */
			continue;
		}
		pick = sp;
		memcpy(bind, tmp, sp->p.params->ntparams * sizeof *bind);
	}
	if (pick) {
		binding = cxx_scope_new(c, CXX_SCOPE_BLOCK, pick->p.home, NULL);
		for (uint32_t i = 0; i < pick->p.params->ntparams; i++)
			if (bind[i].kind == 6)
				cxx_bind_pack(c, binding, &pick->p.params->tparams[i], bind[i].list + bind[i].t, (uint32_t)bind[i].v);
			else
				cxx_bind(c, binding, &pick->p.params->tparams[i], &bind[i]);
		pos = pick->init_pos;
	} else {
		binding = cxx_scope_new(c, CXX_SCOPE_BLOCK, f->p.home, NULL);
		for (uint32_t i = 0; i < ps->ntparams; i++) {
			const cxx_tparam *tp = &ps->tparams[i];

			if (tp->pack) {
				if (i + 1 != ps->ntparams || i > n)
					return false;
				cxx_bind_pack(c, binding, tp, args + i, n - i);
				break;
			}
			if (i >= n || (args[i].kind == 1) != (tp->kind == 1))
				return false;	/* (default arguments: not yet) */
			cxx_bind(c, binding, tp, &args[i]);
		}
		pos = f->pos;
	}
	if (f->ndone == f->cdone)
		f->done = realloc(f->done, (f->cdone = f->cdone ? f->cdone * 2 : 4) * sizeof *f->done);
	at = f->ndone++;
	memset(&f->done[at], 0, sizeof f->done[at]);
	f->done[at].args = malloc((n ? n : 1) * sizeof *args);
	memcpy(f->done[at].args, args, n * sizeof *args);
	f->done[at].n = n, f->done[at].viable = -1;
	cxx_outer_save(c, &o);
	c->instantiating++, c->body_depth++, c->unevaluated++;
	c->fail = &jb;
	c->pos = pos;
	c->scope = binding;
	c->template_depth = 0, c->in_template_args = 0, c->explicit_specialization = 0, c->class_depth = 0, c->implicit_typename = 0;
	if (!setjmp(jb)) {
		cxx_conditional_expression_parse(c);
		if (c->econst)
			known = true, v = c->evalue;
	}
	c->instantiating--, c->body_depth--;
	cxx_outer_restore(c, &o);
	f->done[at].viable = known ? 1 : -1, f->done[at].value = v;
	*out = v;
	return known;
}

/*
 * How generic a function template is for a call of nargs arguments: the parameters that are a bare
 * template parameter (T, const T&, T&&), which match anything.  Between two templates that fit equally,
 * the one with fewer is the more specialized (f(T*) over f(T&&)): a stand-in for partial ordering.
 * -1: not known.
 */
VPRIV int cxx_ftemplate_bare_params(cxx *c, cxx_entity *e, uint32_t nargs)
{
	cxx_ftemplate *f = e->ftmpl;
	int n = 0;

	if (!f || !f->has_pt)
		return -1;
	for (uint32_t i = 0; i < nargs && i < f->np; i++) {
		qty P = f->pt[i];
		void *owner;

		if (P == CXX_TY_UNKNOWN)
			return -1;
		if (AST_TYPE_KIND(c->ast, P) == TY_PTR && AST_TYPE(c->ast, P)->ext)
			P = AST_TYPE(c->ast, P)->base;
		/* (a bare T counts 2, const T 1: f(const T&) is more specialized than f(T&)) */
		if (cxx_placeholder_of(c, P, &owner) >= 0)
			n += (QQ(P) & Q_CONST) ? 1 : 2;
	}
	return n;
}

VPRIV void cxx_ftemplate_defined(cxx_ftemplate *f)
{
	if (f)
		f->defined = 1;
}

VPRIV bool cxx_ftemplate_has_body(const cxx_entity *e)
{
	return e->ftmpl && e->ftmpl->defined;
}

VPRIV uint32_t cxx_ftemplate_pos(const cxx_entity *e)
{
	return e->ftmpl ? e->ftmpl->pos : 0;
}
