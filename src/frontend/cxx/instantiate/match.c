/* C++ templates: placeholders, matching types against patterns */
#include "instantiate.h"

/* a placeholder type: its partial specialization and parameter index (-1: not one) */
VPRIV int cxx_placeholder_of(cxx *c, qty t, void **owner)
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
VPRIV bool cxx_mentions_placeholder(cxx *c, qty t, int depth)
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

VPRIV bool cxx_args_mention_placeholder(cxx *c, const cxx_targ *a, uint32_t n)
{
	for (uint32_t i = 0; i < n; i++)
		if (a[i].kind == 3 || a[i].kind == 5 || a[i].kind == 7 || (a[i].kind == 1 && cxx_mentions_placeholder(c, a[i].t, 0)))
			return true;
	return false;
}

VPRIV bool cxx_match_arg(cxx *c, cxx_partial *p, const cxx_targ *pat, const cxx_targ *arg, cxx_targ *bind, int depth);
VPRIV bool cxx_match_list(cxx *c, cxx_partial *p, const cxx_targ *pat, uint32_t np, const cxx_targ *args, uint32_t n, cxx_targ *bind,
			   int depth);

/* deduce: pat (a pattern of p) against the type arg, binding p's parameters in bind */
VPRIV bool cxx_match_type(cxx *c, cxx_partial *p, qty pat, qty arg, cxx_targ *bind, int depth)
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

		if ((QQ(arg) & q) != q && q && AST_TYPE_KIND(c->ast, arg) == TY_ARRAY) {
			/* const T against const char[N]: an array's qualifiers are its elements' -- T is char[N] */
			const ctype *ar = AST_TYPE(c->ast, arg);
			qty el = ar->base;

			if (el == CXX_TY_UNKNOWN || (QQ(el) & q) != q || AST_TYPE_KIND(c->ast, el) == TY_ARRAY)
				return false;
			a = ar->flags & TF_INCOMPLETE ? CXX_TY_UNKNOWN : cc_type_array(c->ast, QT(QI(el), QQ(el) & ~q), ar->n, 0);
			if (a == CXX_TY_UNKNOWN)
				return false;
		} else if ((QQ(arg) & q) != q)
			return false;
		else
			a = QT(QI(arg), QQ(arg) & ~q);
		if (q && AST_TYPE_KIND(c->ast, a) == TY_ARRAY && !(AST_TYPE(c->ast, a)->flags & TF_INCOMPLETE)) {
			/* (an array's elements carry its qualifiers: const T is const char[N] with T char[N]) */
			const ctype *ar = AST_TYPE(c->ast, a);

			if (ar->base != CXX_TY_UNKNOWN && (QQ(ar->base) & q))
				a = cc_type_array(c->ast, QT(QI(ar->base), QQ(ar->base) & ~q), ar->n, 0);
		}
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

			if (vi >= p->params->ntparams)
				return false;
			if (at->ext) {
				/* (partial ordering: the other pattern's own bound placeholder, U[M] against T[N]: N is M) */
				if (bind[vi].kind && !(bind[vi].kind == 3 && bind[vi].v == (int64_t)(at->ext - 1u)))
					return false;
				bind[vi].kind = 3, bind[vi].v = (int64_t)(at->ext - 1u), bind[vi].t = 0;
				return cxx_match_type(c, p, pt->base, at->base, bind, depth + 1);
			}
			if ((at->flags & TF_INCOMPLETE) || !at->n)
				return false;
			if (bind[vi].kind) {
				if (bind[vi].kind != 2 || bind[vi].v != (int64_t)at->n)
					return false;
			} else
				bind[vi].kind = 2, bind[vi].v = (int64_t)at->n, bind[vi].t = 0;
			return cxx_match_type(c, p, pt->base, at->base, bind, depth + 1);
		}
		return pt->flags == at->flags && pt->n == at->n && pt->aux == at->aux && cxx_match_type(c, p, pt->base, at->base, bind, depth + 1);
	case TY_FUNC: {
		/* R(A...): a trailing parameter that is a pack's placeholder takes the remaining parameters */
		int pk = -1;
		void *pown;

		if (pt->n && !(pt->flags & TF_VARIADIC)) {
			int li = cxx_placeholder_of(c, c->ast->params[pt->aux + pt->n - 1], &pown);

			if (li >= 0 && pown == p && (uint32_t)li < p->params->ntparams && p->params->tparams[li].pack &&
			    QQ(c->ast->params[pt->aux + pt->n - 1]) == 0)
				pk = li;
		}
		if (pk >= 0) {
			uint32_t fixed = pt->n - 1, rest;
			cxx_targ *list;

			if (at->n < fixed || pt->flags != at->flags || !cxx_match_type(c, p, pt->base, at->base, bind, depth + 1))
				return false;
			for (uint32_t k = 0; k < fixed; k++)
				if (!cxx_match_type(c, p, c->ast->params[pt->aux + k], c->ast->params[at->aux + k], bind, depth + 1))
					return false;
			rest = at->n - fixed;
			list = calloc(rest ? rest : 1, sizeof *list);	/* (the binding points into it: kept) */
			for (uint32_t k = 0; k < rest; k++)
				list[k].kind = 1, list[k].t = c->ast->params[at->aux + fixed + k];
			if (bind[pk].kind == 6) {
				bool same = bind[pk].v == rest && cxx_args_equal(bind[pk].list + bind[pk].t, list, rest);

				free(list);
				return same;
			}
			bind[pk].kind = 6, bind[pk].list = list, bind[pk].t = 0, bind[pk].v = rest;
			return true;
		}
	}
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
VPRIV bool cxx_match_list(cxx *c, cxx_partial *p, const cxx_targ *pat, uint32_t np, const cxx_targ *args, uint32_t n, cxx_targ *bind,
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

VPRIV bool cxx_match_arg(cxx *c, cxx_partial *p, const cxx_targ *pat, const cxx_targ *arg, cxx_targ *bind, int depth)
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
