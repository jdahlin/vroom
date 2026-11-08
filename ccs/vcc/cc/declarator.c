/* vcc front end: declarators */
#include "ccimpl.h"

/* ---------------- declarators ---------------- */

static void param_list(cc *c, dop *op)
{
	vec ptys, psyms;
	int fl = 0;

	vinit(&ptys);
	vinit(&psyms);
	scope_open(c);
	op->kr = false;
	if (is(c, T_RPAREN)) {
		fl |= TF_NOPROTO;
	} else if (iskw(c, KW_void) && peek(c, 1)->kind == T_RPAREN) {
		next(c);
	} else if (is(c, T_IDENT) && !kwof(tok(c)) && !is_typedef_name(c, tok(c)) &&
		   (peek(c, 1)->kind == T_COMMA || peek(c, 1)->kind == T_RPAREN)) {
		/* K&R identifier list: int for now, the declarations before the body set the types */
		fl |= TF_NOPROTO;
		op->kr = true;
		for (;;) {
			uint32_t s;

			if (!is(c, T_IDENT) || kwof(tok(c)))
				fatal(c, tok(c)->loc, "expected identifier");
			s = sym_new(c, S_VAR, tok(c)->id, prim(TY_INT), tok(c)->loc);
			c->syms[s].flags |= SF_PARAM;
			c->syms[s].sc = SC_AUTO;
			vpush(c, &psyms, s);
			next(c);
			if (!accept(c, T_COMMA))
				break;
		}
	} else {
		for (;;) {
			dspec ds;
			declr d;
			uint32_t first, nops, s;
			qty t;

			if (accept(c, T_ELLIPSIS)) {
				fl |= TF_VARIADIC;
				break;
			}
			if (!decl_specs(c, &ds, true)) {
				/* clang: an implicit-int parameter is only an error outside system headers */
				if (is(c, T_IDENT) && !kwof(tok(c)) && pp_in_system_header(c->p, tok(c)->loc)) {
					memset(&ds, 0, sizeof(ds));
					ds.ty = prim(TY_INT);
					ds.loc = tok(c)->loc;
				} else if (is(c, T_IDENT)) {
					unknown_type(c, &ds);
				} else {
					fatal(c, tok(c)->loc, "expected parameter declarator");
				}
			}
			declarator(c, &d, true, &first, &nops);
			attribute_list(c, &d.at);
			t = apply_declarator(c, ds.ty, first, nops);
			c->ndops = first;
			/* adjustments (C11 6.7.6.3p7-8) */
			uint32_t vsize = 0;

			if (is_array(c, t)) {
				const dop *ad = nops ? &c->dops[first + nops - 1] : NULL;
				int q = ad && ad->kind == 1 ? ad->q : 0;

				/* int a[n++]: the size is still evaluated on entry (function_def) */
				if ((T(c, t)->flags & TF_VLA) && T(c, t)->n && has_side_effects(c, T(c, t)->n))
					vsize = T(c, t)->n;
				t = QT(QI(ty_ptr(c, T(c, t)->base)), q);
			} else if (is_func(c, t)) {
				t = ty_ptr(c, t);
			}
			vpush(c, &ptys, t);
			s = sym_new(c, S_VAR, d.name, t, d.loc ? d.loc : ds.loc);
			c->syms[s].flags |= SF_PARAM;
			c->syms[s].body = vsize;	/* parameters: a decayed VLA's size expression with side effects */
			c->syms[s].sc = ds.sc == SC_REGISTER ? SC_REGISTER : SC_AUTO;
			sym_bind(c, s);
			vpush(c, &psyms, s);
			if (!accept(c, T_COMMA))
				break;
		}
	}
	scope_close(c);
	expect(c, T_RPAREN, "')'");
	op->kind = 2;
	op->fl = (uint16_t)fl;
	op->a = vfreeze(c, &ptys);
	op->b = ptys.n;
	op->np = psyms.n;
	/* the parameter syms follow the types */
	vfreeze(c, &psyms);
}

static void array_suffix(cc *c, dop *op, uint32_t name)
{
	int q = 0;

	op->kind = 1;
	op->fl = 0;
	for (;;) {
		int kw = kwof(tok(c));

		if (kw == KW_static) {
			next(c);
		} else if (kw == KW_const || kw == KW___const || kw == KW___const__) {
			q |= Q_CONST, next(c);
		} else if (kw == KW_volatile || kw == KW___volatile__ || kw == KW___volatile) {
			q |= Q_VOLATILE, next(c);
		} else if (kw == KW_restrict || kw == KW___restrict || kw == KW___restrict__) {
			q |= Q_RESTRICT, next(c);
		} else if (is_nullability(c, tok(c))) {
			next(c);
		} else {
			break;
		}
	}
	op->q = (uint8_t)q;
	if (accept(c, T_RSQUARE)) {
		op->fl = TF_INCOMPLETE;
		return;
	}
	if (is(c, T_STAR) && peek(c, 1)->kind == T_RSQUARE) {
		next(c), next(c);
		op->fl = TF_VLA;
		op->a = 0;
		return;
	}
	{
		uint32_t n = rvalue(c, assign_expr(c));
		i128 v;

		expect(c, T_RSQUARE, "']'");
		if (!is_integer(c, N(n)->ty)) {
			cc_error(c, N(n)->loc, "size of array has non-integer type");
			v = 1;
		} else if (!const_int(c, n, &v)) {
			op->fl = TF_VLA;
			op->a = n;
			return;
		}
		if (v < 0) {
			pp_span r = rng(c, n);

			if (name)
				report(c, "error", r.b, &r, 1, NULL, 0, "'%.*s' declared as an array with a negative size", IDF(name));
			else
				report(c, "error", r.b, &r, 1, NULL, 0, "array size is negative");
			v = 1;
		}
		op->a = (uint32_t)(uint64_t)v;
		op->b = (uint32_t)((uint64_t)v >> 32);
	}
}

static bool nested_declarator(cc *c, bool abstract_ok)
{
	const ptok *t = peek(c, 1);
	int kw = kwof(t);

	if (t->kind == T_STAR || t->kind == T_CARET)
		return true;
	if (t->kind == T_LPAREN)
		return true;	/* "((": only a nested declarator (an abstract one or a named one) */
	if (t->kind == T_LSQUARE)
		return abstract_ok;
	if (kw == KW___attribute__ || kw == KW___attribute)
		return true;
	if (t->kind == T_IDENT && !kw && !is_typedef_name(c, t))
		return true;
	return false;
}

/*
 * Parse a declarator into the op stack; ops [*first, *first + *nops) in the
 * order they apply to the base type (pointers, suffixes right to left, then
 * the nested declarator's ops).
 */
VPRIV void declarator(cc *c, declr *d, bool abstract_ok, uint32_t *first, uint32_t *nops)
{
	dop ptrs[64], sufs[64];
	int np = 0, ns = 0;
	uint32_t ifirst = 0, inops = 0;
	bool inner = false;
	declr id;

	memset(d, 0, sizeof(*d));
	d->loc = tok(c)->loc;
	for (;;) {
		if (is(c, T_STAR) || is(c, T_CARET)) {	/* ^: a block pointer (Darwin SDK prototypes) */
			int q = 0, blk = is(c, T_CARET);

			next(c);

			for (;;) {
				int kw = kwof(tok(c));

				if (kw == KW_const || kw == KW___const || kw == KW___const__)
					q |= Q_CONST;
				else if (kw == KW_volatile || kw == KW___volatile__ || kw == KW___volatile)
					q |= Q_VOLATILE;
				else if (kw == KW_restrict || kw == KW___restrict || kw == KW___restrict__)
					q |= Q_RESTRICT;
				else if (kw == KW__Atomic || is_nullability(c, tok(c)))
					;
				else if (kw == KW___attribute__ || kw == KW___attribute) {
					attribute_list(c, &d->at);
					continue;
				} else
					break;
				next(c);
			}
			if (np >= 64)
				fatal(c, tok(c)->loc, "declarator too complex");
			ptrs[np++] = (dop){ .kind = 0, .q = (uint8_t)q, .fl = (uint16_t)(blk ? TF_BLOCK : 0) };
			continue;
		}
		if (iskw(c, KW___attribute__) || iskw(c, KW___attribute)) {
			attribute_list(c, &d->at);
			continue;
		}
		break;
	}
	if (is(c, T_LPAREN) && nested_declarator(c, abstract_ok)) {
		next(c);
		attribute_list(c, &d->at);
		declarator(c, &id, abstract_ok, &ifirst, &inops);
		expect(c, T_RPAREN, "')'");
		d->name = id.name;
		d->loc = id.loc;
		merge_attrs(&d->at, &id.at);
		if (id.has_func) {
			d->psyms = id.psyms, d->npsyms = id.npsyms, d->kr = id.kr, d->has_func = true;
			d->plb = id.plb, d->ple = id.ple;
		}
		inner = true;
	} else if (is(c, T_IDENT) && !kwof(tok(c))) {
		d->name = tok(c)->id;
		d->loc = tok(c)->loc;
		next(c);
	} else if (!abstract_ok) {
		char b[64];

		fatal(c, tok(c)->loc, "expected identifier or '(' before '%s'", tokspell(tok(c), b, sizeof(b)));
	}
	attribute_list(c, &d->at);
	for (;;) {
		if (accept(c, T_LSQUARE)) {
			if (ns >= 64)
				fatal(c, tok(c)->loc, "declarator too complex");
			array_suffix(c, &sufs[ns++], d->name);
		} else if (is(c, T_LPAREN)) {
			uint32_t plb;

			next(c);
			plb = tok(c)->loc;
			if (ns >= 64)
				fatal(c, tok(c)->loc, "declarator too complex");
			param_list(c, &sufs[ns]);
			/* the innermost function declarator next to the name gives the parameters */
			if (!inner && !d->has_func && ns == 0) {
				d->psyms = sufs[ns].a + sufs[ns].b;
				d->npsyms = sufs[ns].np;
				d->kr = sufs[ns].kr;
				d->has_func = true;
				d->plb = plb, d->ple = c->prevloc;
			}
			ns++;
		} else {
			break;
		}
		attribute_list(c, &d->at);
	}
	/* assemble: pointers, suffixes reversed, inner ops */
	{
		uint32_t at = c->ndops;
		uint32_t n = (uint32_t)np + (uint32_t)ns + inops;

		if (at + n > DOPS_MAX)
			fatal(c, tok(c)->loc, "declarator too complex");
		/* the inner ops sit at [ifirst, ifirst + inops) which is below at */
		if (inner) {
			dop tmp[inops ? inops : 1];

			memcpy(tmp, &c->dops[ifirst], inops * sizeof(dop));
			at = ifirst;
			for (int i = 0; i < np; i++)
				c->dops[at + (uint32_t)i] = ptrs[i];
			for (int i = 0; i < ns; i++)
				c->dops[at + (uint32_t)np + (uint32_t)i] = sufs[ns - 1 - i];
			memcpy(&c->dops[at + (uint32_t)np + (uint32_t)ns], tmp, inops * sizeof(dop));
		} else {
			for (int i = 0; i < np; i++)
				c->dops[at + (uint32_t)i] = ptrs[i];
			for (int i = 0; i < ns; i++)
				c->dops[at + (uint32_t)np + (uint32_t)i] = sufs[ns - 1 - i];
		}
		*first = at;
		*nops = n;
		c->ndops = at + n;
	}
}

VPRIV qty apply_declarator(cc *c, qty t, uint32_t first, uint32_t nops)
{
	for (uint32_t i = 0; i < nops; i++) {
		const dop *o = &c->dops[first + i];

		switch (o->kind) {
		case 0:
			if (o->fl & TF_BLOCK) {
				ctype b = { .kind = TY_PTR, .flags = TF_BLOCK, .size = 8, .align = 8, .base = t };

				t = QT(ty_intern(c, &b), o->q);
			} else {
				t = QT(QI(ty_ptr(c, t)), o->q);
			}
			break;
		case 1:
			if (is_func(c, t))
				cc_error(c, tok(c)->loc, "array of functions is not allowed");
			if (o->fl & TF_VLA)
				t = ty_vla(c, t, o->a);
			else
				t = ty_array(c, t, (uint64_t)o->a | (uint64_t)o->b << 32, o->fl);
			break;
		case 2:
			if (is_array(c, t) || is_func(c, t))
				cc_error(c, tok(c)->loc, "function cannot return %s type", is_array(c, t) ? "array" : "function");
			t = ty_func(c, QUNQ(t), &c->lists[o->a], o->b, o->fl);
			break;
		}
	}
	return t;
}

VPRIV qty type_name(cc *c)
{
	dspec ds;
	declr d;
	uint32_t first, nops;
	qty t;

	if (!decl_specs(c, &ds, false))
		fatal(c, tok(c)->loc, "expected a type");
	declarator(c, &d, true, &first, &nops);
	attribute_list(c, &d.at);
	t = apply_declarator(c, ds.ty, first, nops);
	c->ndops = first;
	if (d.name)
		cc_error(c, d.loc, "unexpected identifier in type name");
	{
		uint32_t vs = d.at.vecsize ? d.at.vecsize : ds.at.vecsize;

		/* (__attribute__((vector_size(N))) T) */
		if (vs && is_arith(c, t) && ty_size(c, t) <= vs && !(vs % ty_size(c, t)))
			t = QT(QI(ty_vector(c, t, vs)), QQ(t));
	}
	return t;
}
