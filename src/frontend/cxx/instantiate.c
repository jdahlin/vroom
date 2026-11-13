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

struct cxx_template {
	cxx_scope *params;	/* the template scope: its parameters (NULL until defined) */
	cxx_scope *declared;	/* an earlier declaration's template scope: default arguments may be there */
	cxx_scope *home;	/* where the template is declared */
	uint32_t pos, end;	/* class: its class-key token; alias: its type-id [pos, end) */
	uint8_t is_alias, has_partial;
	char *path;		/* its qualified name (the layout dump), NULL: not nameable */
};

typedef struct cxx_instance {
	cxx_template *t;
	uint32_t nargs;
	cxx_targ *args;
	uint32_t hash;
	cxx_entity *inst;	/* NULL: failed */
	cxx_entity *of;		/* the template */
	cxx_scope *binding;	/* its parameters bound to the arguments */
	uint8_t state;		/* 0 declared (an incomplete record), 1 being completed, 2 complete, 3 failed */
} cxx_instance;

VPRIV cxx_template *cxx_template_of(cxx_entity *e)
{
	if (!e->tmpl)
		e->tmpl = calloc(1, sizeof *e->tmpl);
	return e->tmpl;
}

VPRIV void cxx_template_define(cxx *c, cxx_entity *e, cxx_scope *params, cxx_scope *home, uint32_t pos, uint32_t end, bool alias)
{
	cxx_template *t;

	if (!e || c->tentative || c->instantiating)
		return;
	t = cxx_template_of(e);
	t->params = params, t->home = home, t->pos = pos, t->end = end, t->is_alias = alias;
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

/* a declaration before the definition: its parameters' defaults count (template<class T = void> struct X;) */
VPRIV void cxx_template_declared(cxx *c, cxx_entity *e, cxx_scope *params)
{
	cxx_template *t;

	if (!e || c->tentative || c->instantiating || !params || params->kind != CXX_SCOPE_TEMPLATE)
		return;
	t = cxx_template_of(e);
	if (!t->declared)
		t->declared = params;
}

VPRIV void cxx_template_partial(cxx_entity *e)
{
	if (e)
		cxx_template_of(e)->has_partial = 1;
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
		h = (h ^ ((uint64_t)a[i].kind << 56 ^ (a[i].kind == 1 ? a[i].t : (uint64_t)a[i].v))) * 0x100000001B3ull;
	return (uint32_t)(h ^ (h >> 32));
}

static bool cxx_args_equal(const cxx_targ *a, const cxx_targ *b, uint32_t n)
{
	for (uint32_t i = 0; i < n; i++)
		if (a[i].kind != b[i].kind || (a[i].kind == 1 ? a[i].t != b[i].t : a[i].v != b[i].v))
			return false;
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
	if (x->kind < TY_NKINDS && basic[x->kind] && x->kind <= TY_LDOUBLE)
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
	uint8_t last_type_dependent;
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
			out->kind = c->type_out != CXX_TY_UNKNOWN, out->t = c->type_out;
		} else {
			cxx_token *t = cxx_peek(c, 0);

			out->kind = 0;
			if (t->kw == CXX_KW_true || t->kw == CXX_KW_false)
				out->kind = 2, out->v = t->kw == CXX_KW_true;
			else if (t->kind == LEX_TOKEN_NUMBER && t->len == 1 && t->s[0] >= '0' && t->s[0] <= '9')
				out->kind = 2, out->v = t->s[0] - '0';
			c->pos = p->def_e;
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
	e = cxx_declare(c, binding, p->name, p->kind == 1 ? CXX_ENTITY_TYPE : CXX_ENTITY_VALUE);
	if (!e)
		return;
	e->dependent = 0;
	if (p->kind == 2)
		e->value = a->v, e->has_value = 1;
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
		} else {
			snprintf(num, sizeof num, "%lld", (long long)a[i].v);
			if (!cxx_spell_append(buf, cap, len, num))
				return false;
		}
	}
	return cxx_spell_append(buf, cap, len, ">");
}

static bool cxx_instance_parse(cxx *c, cxx_instance *x);

VPRIV cxx_entity *cxx_instantiate(cxx *c, cxx_entity *te, const cxx_targ *args, uint32_t nargs, bool complete)
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
		if (complete && x->inst)
			cxx_instance_complete(c, x->inst);
		return x->inst;
	}
	if (!t->params || t->has_partial)
		return NULL;	/* not defined; or a partial specialization might match (not handled yet) */
	/* the arguments, defaults completed, bound in a scope of their own */
	binding = cxx_scope_new(c, CXX_SCOPE_BLOCK, t->home, NULL);
	for (uint32_t i = 0; i < t->params->ntparams; i++) {
		const cxx_tparam *p = &t->params->tparams[i];
		const cxx_tparam *d = t->declared && t->declared != t->params && i < t->declared->ntparams ? &t->declared->tparams[i] : NULL;

		if (p->pack || p->kind == 3 || n >= 32)
			return NULL;	/* packs and template template parameters: not yet */
		if (i < nargs)
			full[n] = args[i];
		else if (p->def_b) {
			if (!cxx_default_argument(c, p, binding, &full[n]))
				return NULL;
		} else if (d && d->def_b && d->kind == p->kind) {
			if (!cxx_default_argument(c, d, binding, &full[n]))	/* (an earlier declaration's default) */
				return NULL;
		} else
			return NULL;
		if ((p->kind == 1) != (full[n].kind == 1))
			return NULL;
		cxx_bind(c, binding, p, &full[n]);
		if (d && d->name != p->name)
			cxx_bind(c, binding, d, &full[n]);	/* (the declaration's name for it, in its defaults) */
		n++;
	}
	if (nargs > t->params->ntparams)
		return NULL;
	/* the complete argument list is the key (X<int> and X<int, allocator<int>> are one instance) */
	h = cxx_instance_hash(t, full, n);
	x = cxx_instance_find(c, t, full, n, h);
	if (x) {
		if (complete && x->inst)
			cxx_instance_complete(c, x->inst);
		return x->inst;
	}
	if (0) {
		char sp[512];
		size_t sl;

		dprintf(2, "cxx-trace: %*s%s\n", (int)c->instantiating * 2, "",
			cxx_instance_spell(c, t, full, n, sp, sizeof sp, &sl) ? sp : t->path ? t->path : "?");
	}
	inst = calloc(1, sizeof *inst);
	inst->name = te->name, inst->kind = CXX_ENTITY_TYPE, inst->ty = CXX_TY_UNKNOWN;
	x = cxx_instance_add(c, t, full, n, h);
	x->inst = inst, x->of = te, x->binding = binding;
	inst->instance = x;
	if (t->is_alias) {
		/* an alias is a type: computed now */
		if (!cxx_instance_parse(c, x))
			x->inst = NULL;
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
	c->pos = t->pos;
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
	} else
		c->instances_failed++;
	c->instantiating--;
	cxx_state_restore(c, &s);
	return ok;
}

/* an instance's class, completed: its body parsed (once) */
VPRIV void cxx_instance_complete(cxx *c, cxx_entity *inst)
{
	cxx_instance *x = inst ? inst->instance : NULL;

	if (!x || x->state || c->tentative)
		return;
	x->state = 1;
	x->state = cxx_instance_parse(c, x) ? 2 : 3;
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
