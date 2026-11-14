/* C++ templates: class template instances, their completion */
#include "instantiate.h"

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

VPRIV cxx_entity *cxx_instantiate_now(cxx *c, cxx_entity *te, const cxx_targ *args, uint32_t nargs, bool complete)
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
		/* an alias is a type: computed now; what made it ill-formed is remembered with it (the next
		   use of the same alias is ill-formed too) */
		uint8_t mm = c->member_missing, iv = c->einvalid;

		c->member_missing = 0, c->einvalid = 0;
		if (!cxx_instance_parse(c, x))
			x->inst = NULL;
		x->missing = c->member_missing || c->einvalid;
		c->member_missing |= mm;
		c->einvalid = iv;
		if (x->missing)
			c->member_missing = 1;
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
VPRIV bool cxx_instance_parse(cxx *c, cxx_instance *x)
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

/* is the scope std, or a namespace inline in it (std::__1) */
static bool cxx_scope_in_std(const cxx_scope *s)
{
	for (int i = 0; s && i < 3; i++, s = s->parent)
		if (s->kind == CXX_SCOPE_NAMESPACE && s->owner && vintern_ents[s->owner->name].len == 3 &&
		    !memcmp(vintern_ents[s->owner->name].s, "std", 3))
			return true;
	return false;
}

/*
 * A standard type trait vcc computes from its definition in the standard instead of from the library's
 * body, which needs constant evaluation vcc does not have (a loop over an array in a constexpr function):
 * std::common_comparison_category<Ts...>::type ([cmp.common]): void if a T is not a comparison category
 * type, else partial_ordering if one is, else weak_ordering if one is, else strong_ordering.
 */
static bool cxx_instance_known_type(cxx *c, cxx_instance *x)
{
	static uint32_t ccc, type;
	cxx_entity *m;
	qty po, wo, so, r;
	int worst = 3;	/* 3 strong, 2 weak, 1 partial, 0 none */

	if (!ccc)
		ccc = vintern_cstr("common_comparison_category"), type = vintern_cstr("type");
	if (!x->of || x->of->name != ccc || !x->inst || !x->inst->members || !x->t || !cxx_scope_in_std(x->t->home))
		return false;
	po = cxx_std_class(c, "partial_ordering"), wo = cxx_std_class(c, "weak_ordering"), so = cxx_std_class(c, "strong_ordering");
	if (po == CXX_TY_UNKNOWN || wo == CXX_TY_UNKNOWN || so == CXX_TY_UNKNOWN)
		return false;
	for (uint32_t i = 0; i < x->nargs; i++) {
		qty t = x->args[i].kind == 1 ? x->args[i].t : CXX_TY_UNKNOWN;
		int k = t == CXX_TY_UNKNOWN ? -1 : QUNQ(t) == so && !QQ(t) ? 3 : QUNQ(t) == wo && !QQ(t) ? 2 : QUNQ(t) == po && !QQ(t) ? 1 : 0;

		if (k < 0)
			return false;
		worst = k < worst ? k : worst;
	}
	r = worst == 3 ? so : worst == 2 ? wo : worst == 1 ? po : cc_type_basic(TY_VOID);
	for (m = cxx_lookup_in(c, x->inst->members, type, 0); m; m = m->next)
		if (m->kind == CXX_ENTITY_TYPE)
			m->ty = r, m->dependent = 0, m->members = cc_type_is_record(c->ast, r) ? cxx_type_class_scope(c, r) : NULL;
	return true;
}

VPRIV void cxx_instance_complete_body(cxx *c, cxx_entity *inst);

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

VPRIV void cxx_instance_complete_body(cxx *c, cxx_entity *inst)
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
			x->no_definition = r == 0;
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
			x->npargs = pick->params->ntparams;
			x->pargs = malloc((x->npargs ? x->npargs : 1) * sizeof *x->pargs);
			memcpy(x->pargs, bind, x->npargs * sizeof *bind);
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
	{
		uint8_t mm = c->member_missing, iv = c->einvalid;

		x->state = cxx_instance_parse(c, x) ? 2 : 3;
		if (x->state == 2 && cxx_instance_known_type(c, x))
			c->member_missing = mm, c->einvalid = iv;	/* (what its body's parse could not tell does not count) */
	}
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

/* is the record type incomplete for good: an instance of a template that has no definition and no partial
   specialization that matches it (sizeof of it: a substitution failure, __has_specialization_v in libc++) */
VPRIV bool cxx_type_definitely_incomplete(cxx *c, qty t)
{
	uint32_t tag;

	if (t == CXX_TY_UNKNOWN || !cc_type_is_record(c->ast, t) || AST_TYPE_KIND(c->ast, t) == TY_ARRAY)
		return false;
	cxx_type_require_complete(c, t);
	tag = AST_TYPE(c->ast, t)->aux;
	if (c->ast->tags[tag].flags & RF_COMPLETE)
		return false;
	return tag < c->ntag_instance && c->tag_instance[tag] && ((cxx_instance *)c->tag_instance[tag])->no_definition;
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

/* an enumeration's declaring scope, by its type */
VPRIV void cxx_tag_home_set(cxx *c, qty t, cxx_scope *home)
{
	uint32_t tag;

	if (t == CXX_TY_UNKNOWN || AST_TYPE_KIND(c->ast, t) != TY_ENUM)
		return;
	tag = AST_TYPE(c->ast, t)->aux;
	cxx_tag_grow((void ***)&c->tag_home, &c->ntag_home, tag);
	c->tag_home[tag] = home;
}

VPRIV cxx_scope *cxx_tag_home(cxx *c, qty t)
{
	uint32_t tag;

	if (t == CXX_TY_UNKNOWN || AST_TYPE_KIND(c->ast, t) != TY_ENUM)
		return NULL;
	tag = AST_TYPE(c->ast, t)->aux;
	return tag < c->ntag_home ? c->tag_home[tag] : NULL;
}

/* ... as it is: not completed for the asking (the name mangler: a type's name needs no body) */
VPRIV cxx_scope *cxx_tag_scope_get(cxx *c, qty t)
{
	uint32_t tag;

	if (t == CXX_TY_UNKNOWN || !cc_type_is_record(c->ast, t))
		return NULL;
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
