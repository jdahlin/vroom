/*
 * names.c: scopes and what names denote (cxx-internal.h).
 *
 * Lookup follows the language as far as parsing needs it: a namespace's
 * inline namespaces and using-directives are searched with it (libc++'s
 * std::__1), a class's base classes after its own members, and a class
 * template with a dependent base answers "dependent" for a name it does not
 * declare, which the parser treats as a value unless typename or template
 * say otherwise.  Declarations made during a tentative parse are not
 * recorded: a tentative parse only decides, the real parse follows.
 */
#include "cxx-internal.h"

static cxx_entity cxx_dependent_entity = { .kind = CXX_ENTITY_DEPENDENT, .dependent = 1 };

VPRIV cxx_entity *cxx_entity_dependent(void)
{
	return &cxx_dependent_entity;
}

VPRIV cxx_scope *cxx_scope_new(cxx *c, int kind, cxx_scope *parent, cxx_entity *owner)
{
	cxx_scope *s = calloc(1, sizeof *s);

	(void)c;
	s->kind = (uint8_t)kind, s->parent = parent, s->owner = owner;
	return s;
}

VPRIV void cxx_scope_push(cxx *c, cxx_scope *s)
{
	s->parent = s->parent ? s->parent : c->scope;
	c->scope = s;
}

VPRIV void cxx_scope_pop(cxx *c)
{
	c->scope = c->scope->parent;
}

static uint32_t cxx_slot(uint32_t name, uint32_t n)
{
	return (name * 2654435761u) & (n - 1);
}

static cxx_entity **cxx_scope_find_slot(cxx_scope *s, uint32_t name)
{
	uint32_t h = cxx_slot(name, s->nslots);

	while (s->slots[h] && s->slots[h]->name != name)
		h = (h + 1) & (s->nslots - 1);
	return &s->slots[h];
}

VPRIV cxx_entity *cxx_declare(cxx *c, cxx_scope *s, uint32_t name, int kind)
{
	cxx_entity *e, **slot;

	if (c->tentative || !name)
		return NULL;
	if (s->nnames * 2 >= s->nslots) {
		cxx_entity **old = s->slots;
		uint32_t n = s->nslots;

		s->nslots = n ? n * 2 : 16;
		s->slots = calloc(s->nslots, sizeof *s->slots);
		for (uint32_t i = 0; i < n; i++)
			if (old[i])
				*cxx_scope_find_slot(s, old[i]->name) = old[i];
		free(old);
	}
	slot = cxx_scope_find_slot(s, name);
	/* a redeclaration of a namespace or class keeps the first entity (its scope) */
	for (e = *slot; e; e = e->next)
		if (e->kind == kind && (kind == CXX_ENTITY_NAMESPACE || kind == CXX_ENTITY_TYPE || kind == CXX_ENTITY_TEMPLATE))
			return e;
	e = calloc(1, sizeof *e);
	e->name = name, e->kind = (uint8_t)kind;
	e->ty = CXX_TY_UNKNOWN;
	if (!*slot)
		s->nnames++;
	/* types and templates first: a name that is both (struct stat and stat()) is a value
	   unless elaborated; the parser asks for the kind it can use */
	e->next = *slot;
	*slot = e;
	return e;
}

VPRIV void cxx_scope_use(cxx *c, cxx_scope *s, cxx_scope *used)
{
	if (c->tentative || !used || used == s)
		return;
	for (uint32_t i = 0; i < s->nused; i++)
		if (s->used[i] == used)
			return;
	if (s->nused == s->cused)
		s->used = realloc(s->used, (s->cused = s->cused ? s->cused * 2 : 4) * sizeof *s->used);
	s->used[s->nused++] = used;
}

/* a name declared in s or what s uses (inline namespaces, using-directives, bases); qualified:
   a class with a dependent base answers 'dependent' for a name it does not declare (unqualified
   lookup skips dependent bases and goes on outward: two-phase lookup) */
static cxx_entity *cxx_lookup_scope(cxx *c, cxx_scope *s, uint32_t name, int depth, bool qualified)
{
	if (!s || depth > 16)
		return NULL;
	if (s->nslots) {
		cxx_entity *e = *cxx_scope_find_slot(s, name);

		if (e)
			return e;
	}
	for (uint32_t i = 0; i < s->nused; i++) {
		cxx_entity *e = cxx_lookup_scope(c, s->used[i], name, depth + 1, qualified);

		if (e)
			return e;
	}
	if (qualified && s->kind == CXX_SCOPE_CLASS && s->has_dependent_base)
		return &cxx_dependent_entity;
	return NULL;
}

VPRIV cxx_entity *cxx_lookup_in(cxx *c, cxx_scope *s, uint32_t name, int depth)
{
	return cxx_lookup_scope(c, s, name, depth, true);
}

/* a name declared in s itself (a redeclaration, a reopened namespace) */
VPRIV cxx_entity *cxx_lookup_local(cxx_scope *s, uint32_t name)
{
	return s && s->nslots ? *cxx_scope_find_slot(s, name) : NULL;
}

VPRIV cxx_entity *cxx_lookup(cxx *c, uint32_t name)
{
	for (cxx_scope *s = c->scope; s; s = s->parent) {
		cxx_entity *e = cxx_lookup_scope(c, s, name, 0, false);

		if (e) {
			if (e->dependent || e->kind == CXX_ENTITY_DEPENDENT)
				c->dependent_seen = 1;
			return e;
		}
	}
	return NULL;
}

VPRIV cxx_scope *cxx_enclosing_namespace(cxx *c)
{
	cxx_scope *s = c->scope;

	while (s && s->kind != CXX_SCOPE_NAMESPACE)
		s = s->parent;
	return s;
}

VPRIV cxx_scope *cxx_declaration_scope(cxx *c)
{
	cxx_scope *s = c->scope;

	while (s && s->kind == CXX_SCOPE_TEMPLATE)
		s = s->parent;
	return s;
}

/*
 * clang's builtin type traits libc++ uses: __is_same(T, U) is an expression,
 * __remove_cvref(T) a type, __make_integer_seq<...> a template.  The lists
 * are clang's (TokenKinds.def, BuiltinTemplates.td) as far as json's TUs
 * reach them.
 */
static const char *const cxx_type_transforms[] = {
	"__add_lvalue_reference", "__add_pointer", "__add_rvalue_reference", "__decay", "__make_signed", "__make_unsigned",
	"__remove_all_extents", "__remove_const", "__remove_cv", "__remove_cvref", "__remove_extent", "__remove_pointer",
	"__remove_reference_t", "__remove_restrict", "__remove_volatile", "__underlying_type",
};
static const char *const cxx_builtin_templates[] = {
	"__make_integer_seq", "__type_pack_element", "__builtin_common_type", "__builtin_dedup_pack",
};

#define CXX_BUILTIN_SLOTS 1024
static struct { uint32_t id; uint8_t kind; } cxx_builtin_table[CXX_BUILTIN_SLOTS];

static void cxx_builtin_add(const char *n, int kind)
{
	uint32_t id = vintern_cstr(n), h = (id * 2654435761u) % CXX_BUILTIN_SLOTS;

	while (cxx_builtin_table[h].id && cxx_builtin_table[h].id != id)
		h = (h + 1) % CXX_BUILTIN_SLOTS;
	cxx_builtin_table[h].id = id;
	cxx_builtin_table[h].kind = (uint8_t)kind;
}

static void cxx_builtin_table_init(void)
{
	for (size_t i = 0; i < sizeof cxx_type_transforms / sizeof *cxx_type_transforms; i++)
		cxx_builtin_add(cxx_type_transforms[i], CXX_BUILTIN_TYPE_TRANSFORM);
	for (size_t i = 0; i < sizeof cxx_builtin_templates / sizeof *cxx_builtin_templates; i++)
		cxx_builtin_add(cxx_builtin_templates[i], CXX_BUILTIN_TEMPLATE);
}

VPRIV int cxx_builtin_kind(uint32_t id)
{
	static pthread_once_t once = PTHREAD_ONCE_INIT;
	uint32_t h = (id * 2654435761u) % CXX_BUILTIN_SLOTS;
	const vident *v;

	pthread_once(&once, cxx_builtin_table_init);
	while (cxx_builtin_table[h].id) {
		if (cxx_builtin_table[h].id == id)
			return cxx_builtin_table[h].kind;
		h = (h + 1) % CXX_BUILTIN_SLOTS;
	}
	/* type traits: __is_*, __has_*, __reference_*, __array_*, __builtin_* followed by ( */
	v = &vintern_ents[id];
	if (v->len > 4 && (!memcmp(v->s, "__is_", 5) || !memcmp(v->s, "__has_", 6) || !memcmp(v->s, "__reference_", 12) ||
			   !memcmp(v->s, "__array_", 8) || !memcmp(v->s, "__builtin_", 10) || !memcmp(v->s, "__can_pass", 10)))
		return CXX_BUILTIN_TRAIT;
	return CXX_BUILTIN_NONE;
}
