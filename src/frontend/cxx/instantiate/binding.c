/* C++ templates: parser state, default arguments, binding parameters */
#include "instantiate.h"

VPRIV void cxx_state_save(cxx *c, cxx_saved *s)
{
	s->pos = c->pos, s->template_depth = c->template_depth, s->in_template_args = c->in_template_args;
	s->implicit_typename = c->implicit_typename, s->class_depth = c->class_depth, s->nlate = c->nlate;
	s->scope = c->scope, s->fail = c->fail, s->build = c->build, s->explicit_specialization = c->explicit_specialization;
	s->layout_attributes = c->layout_attributes;
	s->name_path_len = c->name_path_len, s->name_path_valid = c->name_path_valid;
	memcpy(s->name_path, c->name_path, c->name_path_len);
	s->target2 = c->instantiation_template;
	s->target = c->instantiation_target, s->last_type = c->last_type, s->last_type_dependent = c->last_type_dependent;
	s->ds_type = c->ds_type, s->type_in = c->type_in, s->type_out = c->type_out;
	s->dependent_seen = c->dependent_seen;
}

VPRIV void cxx_state_restore(cxx *c, const cxx_saved *s)
{
	c->pos = s->pos, c->template_depth = s->template_depth, c->in_template_args = s->in_template_args;
	c->implicit_typename = s->implicit_typename, c->class_depth = s->class_depth, c->nlate = s->nlate;
	c->scope = s->scope, c->fail = s->fail, c->explicit_specialization = s->explicit_specialization;
	c->layout_attributes = s->layout_attributes;
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
VPRIV bool cxx_default_argument(cxx *c, const cxx_tparam *p, cxx_scope *binding, cxx_targ *out)
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
			} else if (c->pos == p->def_b + 1 && (c->tok[p->def_b].kw == CXX_KW_nullptr || c->tok[p->def_b].kw == CXX_KW___nullptr))
				out->kind = 2, out->v = 0;	/* = nullptr (enable_if<...>::type * = nullptr): a null pointer */
		}
		ok = c->pos == p->def_e && out->kind;
	}
	cxx_state_restore(c, &s);
	return ok;
}

/* a value template parameter's type, its tokens parsed in the binding's scope (the earlier parameters
   bound, the template's own class for a member template: template <size_type A>); CXX_TY_UNKNOWN: not known */
VPRIV qty cxx_bind_value_type(cxx *c, cxx_scope *binding, uint32_t type_b)
{
	cxx_outer o;
	jmp_buf jb;
	volatile qty got = CXX_TY_UNKNOWN;

	if (c->body_depth >= 64)
		return CXX_TY_UNKNOWN;
	cxx_outer_save(c, &o);
	c->instantiating++, c->body_depth++;
	c->fail = &jb;
	c->pos = type_b;
	c->scope = binding;
	c->template_depth = 0, c->in_template_args = 0, c->explicit_specialization = 0, c->class_depth = 0, c->implicit_typename = 0;
	if (!setjmp(jb)) {
		cxx_type_specifiers_parse(c);
		got = c->ds_type;
	}
	c->instantiating--, c->body_depth--;
	cxx_outer_restore(c, &o);
	if (got != CXX_TY_UNKNOWN && !(AST_TYPE_KIND(c->ast, got) >= TY_BOOL && AST_TYPE_KIND(c->ast, got) <= TY_UINT128) &&
	    AST_TYPE_KIND(c->ast, got) != TY_ENUM && AST_TYPE_KIND(c->ast, got) != TY_PTR)
		return CXX_TY_UNKNOWN;	/* (a class type, auto: not here) */
	return got != CXX_TY_UNKNOWN ? QUNQ(got) : got;
}

VPRIV void cxx_bind(cxx *c, cxx_scope *binding, const cxx_tparam *p, const cxx_targ *a)
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
		else if (p->type_b) {
			e->ty = cxx_simple_type_at(c, p->type_b);	/* (size_t N, bool B: the integer type spelled) */
			if (e->ty == CXX_TY_UNKNOWN && !c->tentative && !c->template_depth)
				e->ty = cxx_bind_value_type(c, binding, p->type_b);	/* (size_type N: a member typedef) */
		}
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
VPRIV void cxx_bind_pack(cxx *c, cxx_scope *binding, const cxx_tparam *p, const cxx_targ *list, uint32_t n)
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
	if (e->pack_syms)
		e->sym = i < 0 || (uint32_t)i >= e->npack ? 0 : e->pack_syms[i];	/* (a parameter pack: its element's parameter) */
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

VPRIV bool cxx_instance_spell(cxx *c, const cxx_template *t, const cxx_targ *a, uint32_t n, char *buf, size_t cap, size_t *len)
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
