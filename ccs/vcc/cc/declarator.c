/* vcc front end: declarators */
#include "ccimpl.h"

enum { DQ_ATOMIC = 8 };	/* dop.q: _Atomic on a pointer (beside Q_*) */

/* ---------------- declarators ---------------- */

/* false when the list did not end in its ')' (the declaration is invalid, clang) */
static bool param_list(cc *c, dop *op)
{
	vec ptys, psyms;
	int fl = 0;
	uint32_t open = c->prevloc, voidat = 0;	/* the '('; a 'void' that is not alone (clang: after the list) */

	vinit(&ptys);
	vinit(&psyms);
	scope_open(c);
	op->kr = false;
	if (!c->in_proto)
		c->nptag = 0;
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
		if (!c->in_proto)
			c->nptag = 0;
		c->in_proto++;
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
					unknown_type(c, &ds, true);
				} else {
					fatal(c, tok(c)->loc, "expected parameter declarator");
				}
			}
			declarator(c, &d, true, &first, &nops);
			if (!d.name)
				d.loc = tok(c)->loc;	/* an unnamed parameter: where its name would be (clang) */
			attribute_list(c, &d.at);
			t = apply_declarator(c, ds.ty, first, nops);
			c->ndops = first;
			{
				/* int __attribute__((vector_size(16))) p: a vector parameter, as a declaration */
				uint32_t vs = d.at.vecsize ? d.at.vecsize : ds.at.vecsize;

				if (vs) {
					if (!is_arith(c, t) || ty_size(c, t) > vs || vs % ty_size(c, t))
						cc_error(c, d.loc, "invalid vector element type '%s'", tyname(c, t));
					else
						t = QT(QI(ty_vector(c, t, vs)), QQ(t));
				}
			}
			/* adjustments (C11 6.7.6.3p7-8) */
			uint32_t vsize = 0;

			int pk = KIND(c, t);

			if (pk == TY_ARRAY) {
				const dop *ad = nops ? &c->dops[first + nops - 1] : NULL;
				int q = ad && ad->kind == 1 ? ad->q : 0;

				/* int a[n++]: the size is still evaluated on entry (function_def) */
				if ((T(c, t)->flags & TF_VLA) && T(c, t)->n && has_side_effects(c, T(c, t)->n))
					vsize = T(c, t)->n;
				t = QT(QI(ty_ptr(c, T(c, t)->base)), q);
			} else if (pk == TY_FUNC) {
				t = ty_ptr(c, t);
			} else if (unlikely(pk == TY_VOID)) {
				if (!d.name && !ptys.n && !is(c, T_COMMA) && !QQ(t))
					break;	/* "(void void)" after its error, "(void;": still no parameters */
				/* "(int a, void)", "(void x)": clang's errors (where the name would be);
				   the parameter is invalid */
				if (d.name)
					cc_error(c, d.loc, "argument may not have 'void' type");
				else if (!voidat)
					voidat = tok(c)->loc;
				ds.invalid = true;
			}
			vpush(c, &ptys, t);
			if (d.name) {
				uint32_t o = lookup(c, d.name);

				if (o && c->syms[o].depth == c->depth && (c->syms[o].flags & SF_PARAM)) {
					cc_error(c, d.loc, "redefinition of parameter '%.*s'", IDF(d.name));
					note(c, c->syms[o].loc, "previous declaration is here");
					d.name = 0;	/* clang: the parameter loses its name */
				}
			}
			s = sym_new(c, S_VAR, d.name, t, d.loc ? d.loc : ds.loc);
			c->syms[s].tdef = ds.tdef && QUNQ(t) == QUNQ(ds.ty) ? ds.tdef : 0;
			c->syms[s].flags |= SF_PARAM | ((d.at.nonnull | ds.at.nonnull) ? SF_NONNULL : 0) | (ds.invalid ? SF_INVALID : 0);
			if (unlikely(ds.invalid))
				delayed_bug(c, d.loc, "an invalid parameter without an error");
			c->syms[s].body = vsize;	/* parameters: a decayed VLA's size expression with side effects */
			c->syms[s].sc = ds.sc == SC_REGISTER ? SC_REGISTER : SC_AUTO;
			sym_bind(c, s);
			vpush(c, &psyms, s);
			if (!accept(c, T_COMMA))
				break;
		}
		c->in_proto--;
	}
	if (unlikely(voidat))	/* the list parsed: "(int, void)", "(void, int)" */
		cc_error(c, voidat, "'void' must be the first and only parameter if specified");
	scope_close_proto(c);
	bool ok = is(c, T_RPAREN);

	expect_match(c, T_RPAREN, open);
	op->kind = 2;
	op->fl = (uint16_t)fl;
	op->a = vfreeze(c, &ptys);
	op->b = ptys.n;
	op->np = psyms.n;
	/* the parameter syms follow the types */
	vfreeze(c, &psyms);
	return ok;
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
	op->np = tok(c)->loc;	/* where the size is written (an array of VLAs: its size node) */
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
			pp_span r = rng(c, n);

			report(c, "error", N(n)->loc, &r, 1, NULL, 0, "size of array has non-integer type '%s'", tyname(c, N(n)->ty));
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
		if (unlikely(v == 0))
			EXT_WARN(c, "zero-length-array", w_gnu, rng(c, n).b, "zero size arrays are an extension");
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
	dop ptrs[1024], sufs[64];	/* (clang: no limit; DOPS_MAX bounds the whole) */
	int np = 0, ns = 0;
	uint32_t ifirst = 0, inops = 0;
	bool inner = false;
	declr id;

	if (vstack_low())	/* int (((((... nests declarators */
		fatal(c, tok(c)->loc, "declarator nested too deeply");

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
				else if (kw == KW__Atomic)
					q |= DQ_ATOMIC;
				else if (is_nullability(c, tok(c)))
					;
				else if (kw == KW___attribute__ || kw == KW___attribute) {
					attribute_list(c, &d->at);
					continue;
				} else
					break;
				next(c);
			}
			if (np >= 1024)
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
		uint32_t plb0 = tok(c)->loc;

		next(c);
		attribute_list(c, &d->at);
		declarator(c, &id, abstract_ok, &ifirst, &inops);
		expect_match(c, T_RPAREN, plb0);
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
		if (!abstract_ok)	/* an error before the declaration ends declares it, invalid (rec_restore) */
			c->decl_name = d->name, c->decl_loc = d->loc, c->decl_depth = c->depth;
		next(c);
	} else if (!abstract_ok) {
		/* clang: a '(' that is not a grouping one starts the parameters of a nameless
		   declarator, and the name was expected after it */
		fatal(c, is(c, T_LPAREN) ? peek(c, 1)->loc : tok(c)->loc,
		      c->in_struct ? "expected member name or ';' after declaration specifiers" : "expected identifier or '('");
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
			if (unlikely(!param_list(c, &sufs[ns])))
				d->invalid = true;
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
				ctype b = { .kind = TY_PTR, .flags = TF_BLOCK, .size = CC_PTRSIZE(c), .align = CC_PTRSIZE(c), .base = t };

				t = QT(ty_intern(c, &b), o->q & Q_MASK);
			} else {
				t = QT(QI(ty_ptr(c, t)), o->q & Q_MASK);
			}
			if (o->q & DQ_ATOMIC)	/* T *_Atomic */
				t = ty_atomic(c, t, 0);
			break;
		case 1:
			if (is_func(c, t))
				cc_error(c, tok(c)->loc, "array of functions is not allowed");
			if (o->fl & TF_VLA)
				t = ty_vla(c, t, o->a);
			else if ((T(c, t)->flags & TF_VLA) && !(o->fl & TF_INCOMPLETE))
				/* int a[6][n]: an array of VLAs is one too (its size is known only at run time) */
				t = ty_vla(c, t, mk_int(c, (i128)((uint64_t)o->a | (uint64_t)o->b << 32), prim(TY_ULONG), o->np));
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

	c->in_tyname++;
	if (!decl_specs(c, &ds, false))
		fatal(c, tok(c)->loc, "expected a type");
	c->in_tyname--;
	declarator(c, &d, true, &first, &nops);
	attribute_list(c, &d.at);
	t = apply_declarator(c, ds.ty, first, nops);
	c->ndops = first;
	if (d.name)	/* clang: the abstract declarator ends before it */
		cc_error(c, d.loc, "expected ')'");
	{
		uint32_t vs = d.at.vecsize ? d.at.vecsize : ds.at.vecsize;

		/* (__attribute__((vector_size(N))) T) */
		if (vs && is_arith(c, t) && ty_size(c, t) <= vs && !(vs % ty_size(c, t)))
			t = QT(QI(ty_vector(c, t, vs)), QQ(t));
	}
	return t;
}
