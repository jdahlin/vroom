/* vcc front end: struct, union and enum bodies */
#include "ccimpl.h"

_Thread_local fscratch tl_fscr;

static void fscr_grow(fscratch *x, uint32_t need)
{
	uint32_t cap = x->cap ? x->cap : 1024;

	while (cap < need)
		cap *= 2;
	x->fs = realloc(x->fs, cap * sizeof(field));
	x->fa = realloc(x->fa, cap * sizeof(uint32_t));
	x->fp = realloc(x->fp, cap);
	if (!x->fs || !x->fa || !x->fp) {
		fputs("vcc: out of memory\n", stderr);
		abort();
	}
	x->cap = cap;
}

/* clang: a member declaration not ended by ';': a warning before the '}' (on as if it
   were there), else an error and the rest of the member skipped */
static __attribute__((noinline, cold)) void member_semi(cc *c)
{
	uint32_t e = pp_tok_end(c->p, c->prevloc);
	pp_fixit fx = { e, e, ";", semi_app(c) };
	bool end = is(c, T_RBRACE);

	report(c, end ? "warning" : "error", e, NULL, 0, &fx, 1, "expected ';' at end of declaration list");
	if (!end)
		skip_semi(c);
}

__attribute__((noinline)) VPRIV void struct_body(cc *c, uint32_t ti)
{
	fscratch *x = &tl_fscr;
	uint32_t base = x->n, nf = 0;
	field *fs;
	tag *r;
	attrs post = { 0 };

	DR_TAGWRITE(c, ti);
	c->tags[ti].flags |= RF_BEING_DEFINED;
	c->in_struct++;
	expect(c, T_LBRACE, "'{'");
	recov rv;

	if (REC_ARM(c, &rv)) {
		rec_restore(c, &rv, REC_BLOCK);	/* after a parse error: on with the next member */
		nf = x->n - base;
	}
	while (!is(c, T_RBRACE)) {
		dspec ds;

		if (is(c, T_EOF)) {
			cc_error(c, pp_tok_end(c->p, c->prevloc), "expected '}'");
			longjmp(c->bail, 1);
		}
		if (is(c, T_SEMI)) {
			EXT_WARN_DEL(c, ";", tok(c)->flags, "extra-semi", w_ped, tok(c)->loc, "extra ';' inside a %s",
				 c->tags[ti].flags & RF_UNION ? "union" : "struct");
			next(c);
			continue;
		}
		if (iskw(c, KW__Static_assert)) {
			static_assert_decl(c);
			continue;
		}
		if (!decl_specs(c, &ds, false)) {
			int k = peek(c, 1)->kind;

			if (!is(c, T_IDENT))
				fatal(c, tok(c)->loc, "expected member declaration");
			if (!kwof(tok(c)) && (k == T_COMMA || k == T_SEMI || k == T_COLON)) {
				/* clang: "struct { a, b; }": int members */
				cc_error(c, tok(c)->loc, "type name requires a specifier or qualifier");
				memset(&ds, 0, sizeof(ds));
				ds.ty = prim(TY_INT);
				ds.loc = tok(c)->loc;
				ds.invalid = true;
			} else {
				unknown_type(c, &ds, false);
			}
		}
		if (is(c, T_SEMI)) {
			/* anonymous struct/union member; with -fms-anonymous-structs also a
			 * tagged or typedef'd complete record */
			if (is_record(c, ds.ty) && (!c->tags[T(c, ds.ty)->aux].name ||
						    (c->o->ms_anon && complete(c, ds.ty)))) {
				if (nf >= (1u << 20))
					fatal(c, ds.loc, "too many members");
				if (base + nf >= x->cap)
					fscr_grow(x, base + nf + 1);
				x->fs[base + nf] = (field){ .ty = ds.ty, .loc = ds.loc };
				x->fa[base + nf] = ds.at.aligned > ds.align ? ds.at.aligned : ds.align;
				x->fp[base + nf] = ds.at.packed;
				x->n = base + ++nf;
			}
			next(c);
			continue;
		}
		for (;;) {
			declr d;
			uint32_t first = 0, nops = 0;
			qty t = ds.ty;
			field f = { 0 };

			if (!is(c, T_COLON)) {
				declarator(c, &d, false, &first, &nops);
				t = apply_declarator(c, ds.ty, first, nops);
				c->ndops = first;
			} else {
				memset(&d, 0, sizeof(d));
				d.loc = tok(c)->loc;
			}
			f.name = d.name;
			f.loc = d.loc;
			if (accept(c, T_COLON)) {
				i128 w;
				uint32_t wn = 0;
				pp_span wr;

				const_expr(c, &w, &wn);
				wr = wn ? rng(c, wn) : (pp_span){ d.loc, d.loc };
				if (!complete(c, t))
					;	/* clang: only "field has incomplete type", below */
				else if (!is_integer(c, t))
					report(c, "error", d.loc, &wr, 1, NULL, 0, "bit-field '%.*s' has non-integral type '%s'", IDF(d.name),
					       tyname(c, t));
				if (!complete(c, t))
					w = 0;
				else if (w < 0)
					cc_error(c, d.loc, "bit-field '%.*s' has negative width (%lld)", IDF(d.name), (long long)w), w = 0;
				else if ((uint64_t)w > ty_size(c, t) * 8)
					cc_error(c, d.loc, "width of bit-field '%.*s' (%lld bits) exceeds the width of its type (%llu bits)",
					      IDF(d.name), (long long)w, (unsigned long long)ty_size(c, t) * 8), w = 0;
				else if (w == 0 && d.name)
					report(c, "error", d.loc, &wr, 1, NULL, 0, "named bit-field '%.*s' has zero width", IDF(d.name));
				f.bitfield = 1;
				f.width = (uint16_t)w;
			}
			attribute_list(c, &d.at);
			merge_attrs(&d.at, &ds.at);
			if (d.at.mode) {
				int k = mode_kind(c, d.at.mode, is_signed(c, t));

				if (k)
					t = prim(k);
			}
			f.ty = t;
			f.tdef = decl_tdef(c, ds.tdef, ds.ty, t);
			f.counted_by = d.at.counted_by;
			f.depr = unlikely(d.at.depr) ? new_attrs(c, &d.at) : 0;
			if (is_func(c, t))
				cc_error(c, d.loc, "field '%.*s' declared as a function", IDF(d.name));
			else if (KIND(c, t) == TY_SVE) {
				cc_error(c, d.loc, "field has sizeless type '%s'", tyname(c, t));
				f.invalid = 1;
			}
			else if (!ds.invalid && !complete(c, t) && !(is_array(c, t) && (T(c, t)->flags & TF_INCOMPLETE))) {
				cc_error(c, d.loc, "field has incomplete type '%s'", tyname(c, t));
				fwd_note(c, t);
				f.invalid = 1;
			}
			f.invalid |= ds.invalid;
			if (nf >= (1u << 20))
				fatal(c, d.loc, "too many members");
			for (uint32_t i = 0; f.name && i < nf; i++)
				if (x->fs[base + i].name == f.name) {
					cc_error(c, d.loc, "duplicate member '%.*s'", IDF(f.name));
					note(c, x->fs[base + i].loc, "previous declaration is here");
					break;
				}
			if (base + nf >= x->cap)
				fscr_grow(x, base + nf + 1);
			x->fa[base + nf] = d.at.aligned > ds.align ? d.at.aligned : ds.align;
			x->fp[base + nf] = d.at.packed;
			x->fs[base + nf] = f;
			x->n = base + ++nf;
			if (!accept(c, T_COMMA))
				break;
		}
		if (unlikely(!accept(c, T_SEMI)))
			member_semi(c);
	}
	REC_POP(c, &rv);
	next(c);
	c->in_struct--;
	attribute_list(c, &post);
	r = &c->tags[ti];
	r->f = halloc(c, (nf + 1) * sizeof(field));
	memcpy(r->f, &x->fs[base], nf * sizeof(field));
	fs = r->f;
	r->nfield = nf;
	tag_deprecate(c, ti, &post);
	r = &c->tags[ti];
	if (post.ms_struct)
		r->flags |= RF_MS_STRUCT;
	if (post.packed)
		r->flags |= RF_PACKED;
	if (post.aligned > r->maxalign)
		r->maxalign = post.aligned;
	if (post.transparent)
		c->ty[r->ty].flags |= TF_TRANSPARENT;
	for (uint32_t i = 0; i < nf; i++)
		if (is_array(c, fs[i].ty) && (T(c, fs[i].ty)->flags & TF_INCOMPLETE)) {
			if (i + 1 < nf)
				;	/* GNU: flexible array not at the end */
			r->flags |= RF_FLEX;
		}
	lay_record(c, r, &x->fa[base], &x->fp[base]);
	r->flags = (r->flags | RF_COMPLETE) & ~(uint32_t)RF_BEING_DEFINED;
	x->n = base;
}

VPRIV void enum_body(cc *c, uint32_t ti, attrs *after)
{
	i128 v = -1, mn = 0, mx = 0;
	bool any = false;
	vec cs;
	qty fixed = 0;

	DR_TAGWRITE(c, ti);
	vinit(&cs);
	if (accept(c, T_COLON)) {
		fixed = type_name(c);
		c->ty[c->tags[ti].ty].base = QUNQ(fixed);
	}
	expect(c, T_LBRACE, "'{'");
	while (!is(c, T_RBRACE)) {
		uint32_t name, loc, s;
		attrs ea = { 0 };

		if (!is(c, T_IDENT) || kwof(tok(c)))
			fatal(c, tok(c)->loc, "expected identifier");
		name = tok(c)->id;
		loc = tok(c)->loc;
		next(c);
		attribute_list(c, &ea);
		if (accept(c, T_EQUAL))
			const_expr(c, &v, NULL);
		else
			v++;
		if (!any || v < mn)
			mn = v;
		if (!any || v > mx)
			mx = v;
		any = true;
		{
			uint32_t o = lookup(c, name);

			if (o && c->syms[o].depth == c->depth && c->syms[o].kind != S_TAG) {
				if (c->syms[o].kind == S_ENUMCONST)
					cc_error(c, loc, "redefinition of enumerator '%.*s'", IDF(name));
				else
					cc_error(c, loc, "redefinition of '%.*s' as different kind of symbol", IDF(name));
				note(c, c->syms[o].loc, "previous definition is here");
			}
		}
		if (unlikely(v < INT32_MIN || v > INT32_MAX) && c->o->std != 23) {
			char b[48];

			i128s_(b, sizeof b, v);
			EXT_WARN(c, "c23-extensions", w_ped, loc, "enumerator value which exceeds the range of 'int' is a C23 extension "
				 "(%s is too %s)", b, v < 0 ? "small" : "large");
		}
		s = sym_new(c, S_ENUMCONST, name, v >= INT32_MIN && v <= INT32_MAX ? prim(TY_INT) :
						 v >= 0 && v <= UINT32_MAX ? prim(TY_UINT) : v >= INT64_MIN && v <= INT64_MAX ? prim(CC_I64(c, false)) : prim(CC_I64(c, true)), loc);
		if (unlikely(ea.depr))
			apply_sym_attrs(c, &c->syms[s], &ea);
		c->syms[s].val = (int64_t)v;
		sym_bind(c, s);
		proto_binding(c, s);
		vpush(c, &cs, s);
		if (!accept(c, T_COMMA))
			break;
	}
	expect(c, T_RBRACE, "'}'");
	attribute_list(c, after);
	{
		qty best;
		ctype *et = &c->ty[c->tags[ti].ty];

		if (fixed) {
			best = QUNQ(fixed);
		} else if (after->packed) {
			if (mn < 0)
				best = mn >= INT8_MIN && mx <= INT8_MAX ? prim(TY_SCHAR) : mn >= INT16_MIN && mx <= INT16_MAX ? prim(TY_SHORT) :
				       mn >= INT32_MIN && mx <= INT32_MAX ? prim(TY_INT) : prim(CC_I64(c, false));
			else
				best = mx <= UINT8_MAX ? prim(TY_UCHAR) : mx <= UINT16_MAX ? prim(TY_USHORT) :
				       mx <= UINT32_MAX ? prim(TY_UINT) : prim(CC_I64(c, true));
		} else if (mn < 0) {
			best = mn >= INT32_MIN && mx <= INT32_MAX ? prim(TY_INT) : prim(CC_I64(c, false));
		} else {
			best = mx <= UINT32_MAX ? prim(TY_UINT) : prim(CC_I64(c, true));
		}
		et->base = best;
		et->size = ty_size(c, best);
		et->align = ty_align(c, best);
		c->tags[ti].flags |= RF_COMPLETE;
		c->tags[ti].size = et->size;
		c->tags[ti].align = et->align;
		/* clang: if every value fits int the enumerators are int, else all of
		 * them have the enumerated type */
		for (uint32_t i = 0; i < cs.n; i++) {
			sym *y = &c->syms[cs.v[i]];

			if (fixed || mn < INT32_MIN || mx > INT32_MAX)
				y->ty = QT(c->tags[ti].ty, 0);
			else
				y->ty = prim(TY_INT);
		}
		c->tags[ti].enums = vfreeze(c, &cs);
		c->tags[ti].nenum = cs.n;
	}
}
