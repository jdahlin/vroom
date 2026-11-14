/* vcc front end: declarators */
#include "cc-internal.h"

enum { CC_DECLARATOR_QUALIFIER_ATOMIC = 8 };	/* dop.q: _Atomic on a pointer (beside Q_*) */

/* ---------------- declarators ---------------- */

/* GNU vector_size modifies the scalar leaf, retaining declarator layers. */
CC_PRIVATE ast_qualified_type cc_declarator_vector_attribute_apply(cc *c, ast_qualified_type t, uint32_t size, uint32_t loc)
{
	int kind = AST_TYPE_KIND(c, t);
	if (kind == AST_TYPE_POINTER || kind == AST_TYPE_ARRAY || kind == AST_TYPE_FUNCTION) {
		ctype outer = *AST_TYPE(c, t);
		outer.base = cc_declarator_vector_attribute_apply(c, outer.base, size, loc);
		if (kind == AST_TYPE_ARRAY) {
			outer.align = cc_type_alignment(c, outer.base);
			if (!(outer.flags & (AST_TYPE_FLAG_VLA | AST_TYPE_FLAG_INCOMPLETE)))
				outer.size = cc_type_size(c, outer.base) * outer.n;
		}
		return AST_QUALIFIED_TYPE(cc_type_intern(c, &outer), AST_QUALIFIED_TYPE_QUALIFIERS(t));
	}
	if (cc_type_is_bitint(c, t)) {
		cc_report_error(c, loc, "vector integer elements wider than 128 bits are not supported");
		return t;
	}
	if (kind == AST_TYPE_BOOL || !cc_type_is_arithmetic(c, t) || cc_type_is_complex(c, t) || !cc_type_size(c, t) ||
	    cc_type_size(c, t) > size || size % cc_type_size(c, t)) {
		cc_report_error(c, loc, "invalid vector element type '%s'", tyname(c, t));
		return t;
	}
	return AST_QUALIFIED_TYPE(AST_QUALIFIED_TYPE_INDEX(cc_type_vector(c, t, size)), AST_QUALIFIED_TYPE_QUALIFIERS(t));
}

/* false when the list did not end in its ')' (the declaration is invalid, clang) */
static bool cc_declarator_parameter_list_parse(cc *c, cc_declarator_operation *op, const attrs *leading)
{
	cc_id_list ptys, psyms;
	int fl = 0;
	uint32_t open = c->prevloc, voidat = 0;	/* the '('; a 'void' that is not alone (clang: after the list) */

	cc_id_list_init(&ptys);
	cc_id_list_init(&psyms);
	cc_scope_open(c);
	op->kr = false;
	if (!c->in_proto)
		c->nptag = 0;
	if (cc_token_is(c, LEX_TOKEN_RIGHT_PAREN)) {
		if (cc_keyword_map != cc_keyword_map_c23)
			fl |= AST_TYPE_FLAG_NO_PROTOTYPE;
	} else if (cc_token_is_keyword(c, KW_void) && cc_token_peek(c, 1)->kind == LEX_TOKEN_RIGHT_PAREN) {
		next(c);
	} else if (cc_token_is(c, LEX_TOKEN_IDENTIFIER) && !cc_token_keyword(tok(c)) && !cc_token_is_typedef_name(c, tok(c)) &&
		   (cc_token_peek(c, 1)->kind == LEX_TOKEN_COMMA || cc_token_peek(c, 1)->kind == LEX_TOKEN_RIGHT_PAREN)) {
		/* K&R identifier list: int for now, the declarations before the body set the types */
		fl |= AST_TYPE_FLAG_NO_PROTOTYPE;
		op->kr = true;
		for (;;) {
			uint32_t s;

			if (!cc_token_is(c, LEX_TOKEN_IDENTIFIER) || cc_token_keyword(tok(c)))
				cc_report_fatal(c, tok(c)->loc, "expected identifier");
			s = cc_symbol_new(c, AST_SYMBOL_VARIABLE, tok(c)->id, cc_type_basic(AST_TYPE_INT), tok(c)->loc);
			c->syms[s].flags |= AST_SYMBOL_FLAG_PARAMETER;
			c->syms[s].sc = AST_STORAGE_AUTO;
			cc_id_list_append(c, &psyms, s);
			next(c);
			if (!cc_token_accept(c, LEX_TOKEN_COMMA))
				break;
		}
	} else {
		if (!c->in_proto)
			c->nptag = 0;
		c->in_proto++;
		for (;;) {
			cc_declaration_specifiers ds;
			cc_declarator d;
			uint32_t first, nops, s;
			ast_qualified_type t;

			if (cc_token_accept(c, LEX_TOKEN_ELLIPSIS)) {
				fl |= AST_TYPE_FLAG_VARIADIC;
				break;
			}
			if (!cc_declaration_specifiers_parse(c, &ds, true)) {
				/* clang: an implicit-int parameter is only an error outside system headers */
				if (cc_token_is(c, LEX_TOKEN_IDENTIFIER) && !cc_token_keyword(tok(c)) && pp_location_is_in_system_header(c->p, tok(c)->loc)) {
					memset(&ds, 0, sizeof(ds));
					ds.ty = cc_type_basic(AST_TYPE_INT);
					ds.loc = tok(c)->loc;
				} else if (cc_token_is(c, LEX_TOKEN_IDENTIFIER)) {
					cc_declaration_specifiers_unknown_type_report(c, &ds, true);
				} else {
					cc_report_fatal(c, tok(c)->loc, "expected parameter declarator");
				}
			}
			if (leading && !ptys.n) {
				cc_attribute_merge(&ds.at, leading);
				if (leading->mode) {
					int kind = cc_attribute_mode_kind(c, leading->mode, cc_type_is_signed(c, ds.ty));
					if (kind) ds.ty = cc_type_basic(kind);
				}
			}
			if (ds.constexpr_)
				cc_report_error(c, ds.loc, "function parameter cannot be constexpr");
			cc_declarator_parse(c, &d, true, &first, &nops);
			if (!d.name && !d.loc)
				d.loc = tok(c)->loc;	/* an unnamed parameter: where its name would be (clang) */
			cc_attribute_list_parse(c, &d.at);
			t = cc_declarator_apply(c, ds.ty, first, nops);
			c->ndops = first;
			{
				/* int __attribute__((vector_size(16))) p: a vector parameter, as a declaration */
				uint32_t vs = d.at.vecsize ? d.at.vecsize : ds.at.vecsize;

				if (vs) t = cc_declarator_vector_attribute_apply(c, t, vs, d.loc);
			}
			{
				/* ms_abi int (*f)(int): on the function type through the pointer, as declare() */
				uint32_t fa = d.at.fnabi ? d.at.fnabi : ds.at.fnabi;

				if (unlikely(fa)) {
					ast_qualified_type ft = ty_fnabi(c, t, fa == (CC_WIN64(c) ? CC_FNABI_MS : CC_FNABI_SYSV) ? CC_FNABI_DEFAULT : (int)fa);

					if (ft) t = ft;
				}
			}
			/* adjustments (C11 6.7.6.3p7-8) */
			uint32_t vsize = 0;

			int pk = AST_TYPE_KIND(c, t);

			if (pk == AST_TYPE_ARRAY) {
				const cc_declarator_operation *ad = nops ? &c->dops[first + nops - 1] : NULL;
				int q = ad && ad->kind == 1 ? ad->q : 0;

				/* int a[n++]: the size is still evaluated on entry (cc_function_definition_parse) */
				if ((AST_TYPE(c, t)->flags & AST_TYPE_FLAG_VLA) && AST_TYPE(c, t)->n && cc_expression_has_side_effects(c, AST_TYPE(c, t)->n))
					vsize = AST_TYPE(c, t)->n;
				t = AST_QUALIFIED_TYPE(AST_QUALIFIED_TYPE_INDEX(cc_type_pointer(c, AST_TYPE(c, t)->base)), q);
			} else if (pk == AST_TYPE_FUNCTION) {
				t = cc_type_pointer(c, t);
			} else if (unlikely(pk == AST_TYPE_VOID)) {
				if (!d.name && !ptys.n && !cc_token_is(c, LEX_TOKEN_COMMA) && !AST_QUALIFIED_TYPE_QUALIFIERS(t))
					break;	/* "(void void)" after its error, "(void;": still no parameters */
				/* "(int a, void)", "(void x)": clang's errors (where the name would be);
				   the parameter is invalid */
				if (d.name)
					cc_report_error(c, d.loc, "argument may not have 'void' type");
				else if (!voidat)
					voidat = tok(c)->loc;
				ds.invalid = true;
			}
			cc_id_list_append(c, &ptys, t);
			if (d.name) {
				uint32_t o = cc_symbol_lookup(c, d.name);

				if (o && c->syms[o].depth == c->depth && (c->syms[o].flags & AST_SYMBOL_FLAG_PARAMETER)) {
					cc_report_error(c, d.loc, "redefinition of parameter '%.*s'", CC_IDENTIFIER(d.name));
					cc_report_note(c, c->syms[o].loc, "previous declaration is here");
					d.name = 0;	/* clang: the parameter loses its name */
				}
			}
			s = cc_symbol_new(c, AST_SYMBOL_VARIABLE, d.name, t, d.loc ? d.loc : ds.loc);
			c->syms[s].tdef = cc_typedef_sugar_declared(c, ds.tdef, ds.ty, t);
			c->syms[s].flags |= AST_SYMBOL_FLAG_PARAMETER | ((d.at.nonnull | ds.at.nonnull) ? AST_SYMBOL_FLAG_NONNULL : 0) | (ds.invalid ? AST_SYMBOL_FLAG_INVALID : 0);
			if (unlikely(ds.invalid))
				cc_report_delayed_bug(c, d.loc, "an invalid parameter without an error");
			c->syms[s].body = vsize;	/* parameters: a decayed VLA's size expression with side effects */
			c->syms[s].sc = ds.sc == AST_STORAGE_REGISTER ? AST_STORAGE_REGISTER : AST_STORAGE_AUTO;
			cc_symbol_bind(c, s);
			cc_id_list_append(c, &psyms, s);
			if (!cc_token_accept(c, LEX_TOKEN_COMMA))
				break;
		}
		c->in_proto--;
	}
	if (unlikely(voidat))	/* the list parsed: "(int, void)", "(void, int)" */
		cc_report_error(c, voidat, "'void' must be the first and only parameter if specified");
	cc_scope_close_prototype(c);
	bool ok = cc_token_is(c, LEX_TOKEN_RIGHT_PAREN);

	cc_token_expect_match(c, LEX_TOKEN_RIGHT_PAREN, open);
	op->kind = 2;
	op->fl = (uint16_t)fl;
	op->a = cc_id_list_freeze(c, &ptys);
	op->b = ptys.n;
	op->np = psyms.n;
	/* the parameter syms follow the types */
	cc_id_list_freeze(c, &psyms);
	return ok;
}

static void cc_declarator_array_suffix_parse(cc *c, cc_declarator_operation *op, uint32_t name)
{
	int q = 0;

	op->kind = 1;
	op->loc = c->prevloc;
	op->fl = 0;
	for (;;) {
		int kw = cc_token_keyword(tok(c));

		if (kw == KW_static) {
			next(c);
		} else if (kw == KW_const || kw == KW___const || kw == KW___const__) {
			q |= AST_QUALIFIER_CONST, next(c);
		} else if (kw == KW_volatile || kw == KW___volatile__ || kw == KW___volatile) {
			q |= AST_QUALIFIER_VOLATILE, next(c);
		} else if (kw == KW_restrict || kw == KW___restrict || kw == KW___restrict__) {
			q |= AST_QUALIFIER_RESTRICT, next(c);
		} else if (cc_token_is_nullability(c, tok(c))) {
			next(c);
		} else {
			break;
		}
	}
	op->q = (uint8_t)q;
	op->np = tok(c)->loc;	/* where the size is written (an array of VLAs: its size node) */
	if (cc_token_accept(c, LEX_TOKEN_RIGHT_SQUARE)) {
		op->fl = AST_TYPE_FLAG_INCOMPLETE;
		return;
	}
	if (cc_token_is(c, LEX_TOKEN_STAR) && cc_token_peek(c, 1)->kind == LEX_TOKEN_RIGHT_SQUARE) {
		next(c), next(c);
		op->fl = AST_TYPE_FLAG_VLA;
		op->a = 0;
		return;
	}
	{
		uint32_t n = cc_node_rvalue(c, cc_assignment_parse(c));
		i128 v;
		cc_value bound;

		cc_token_expect(c, LEX_TOKEN_RIGHT_SQUARE, "']'");
		if (!cc_type_is_integer(c, AST_NODE(n)->ty)) {
			diag_span r = cc_node_range(c, n);

			report(c, "error", AST_NODE(n)->loc, &r, 1, NULL, 0, "size of array has non-integer type '%s'", tyname(c, AST_NODE(n)->ty));
			v = 1;
		} else if (!cc_constant_eval(c, n, &bound) || bound.kind != CC_VALUE_INTEGER ||
			   (!bound.ice && cc_expression_has_side_effects(c, n))) {
			/* Knowing the value of (callback(), 1) does not make its
			   evaluation optional. Keep effectful bounds in the VLA type.
			   An ICE may contain unevaluated calls in a short-circuit arm. */
			op->fl = AST_TYPE_FLAG_VLA;
			op->a = n;
			return;
		} else {
			v = bound.i;
		}
		if (v < 0) {
			diag_span r = cc_node_range(c, n);

			if (name)
				report(c, "error", r.b, &r, 1, NULL, 0, "'%.*s' declared as an array with a negative size", CC_IDENTIFIER(name));
			else
				report(c, "error", r.b, &r, 1, NULL, 0, "array size is negative");
			v = 1;
		}
		if (unlikely(v == 0))
			CC_REPORT_EXTENSION(c, "zero-length-array", cc_warning_group_gnu, cc_node_range(c, n).b, "zero size arrays are an extension");
		op->a = (uint32_t)(uint64_t)v;
		op->b = (uint32_t)((uint64_t)v >> 32);
	}
}

static bool cc_declarator_nested_parse(cc *c, bool abstract_ok)
{
	const ptok *t = cc_token_peek(c, 1);
	int kw = cc_token_keyword(t);

	if (t->kind == LEX_TOKEN_STAR || t->kind == LEX_TOKEN_CARET)
		return true;
	if (t->kind == LEX_TOKEN_LEFT_PAREN)
		return true;	/* "((": only a nested declarator (an abstract one or a named one) */
	if (t->kind == LEX_TOKEN_LEFT_SQUARE)
		return abstract_ok;
	if (kw == KW___attribute__ || kw == KW___attribute)
		return true;
	if (t->kind == LEX_TOKEN_IDENTIFIER && !kw && !cc_token_is_typedef_name(c, t))
		return true;
	return false;
}

/*
 * Parse a declarator into the op stack; ops [*first, *first + *nops) in the
 * order they apply to the base type (pointers, suffixes right to left, then
 * the nested declarator's ops).
 */
/* Arena-backed growth keeps flat declarators off the C stack. Recovery resets
 * ndops; temporary buffers are reclaimed with the translation unit arena. */
static cc_declarator_operation *cc_declarator_operations_grow(cc *c, cc_declarator_operation *old, uint32_t used, uint32_t *cap, uint32_t need)
{
	uint32_t nc = *cap;
	cc_declarator_operation *p;

	if (need > CC_DECLARATOR_OPERATIONS_MAX)
		cc_report_fatal(c, tok(c)->loc, "declarator too complex");
	while (nc < need)
		nc *= 2;
	p = cc_heap_allocate(c, nc * sizeof(*p));
	memcpy(p, old, used * sizeof(*p));
	*cap = nc;
	return p;
}

CC_PRIVATE void cc_declarator_parse(cc *c, cc_declarator *d, bool abstract_ok, uint32_t *first, uint32_t *nops)
{
	cc_declarator_operation ptrbuf[16], sufbuf[8], *ptrs = ptrbuf, *sufs = sufbuf;
	uint32_t pcap = 16, scap = 8;
	int np = 0, ns = 0;
	uint32_t ifirst = 0, inops = 0;
	bool inner = false;
	cc_declarator id;

	if (vstack_guard_low())	/* int (((((... nests declarators */
		cc_report_fatal(c, tok(c)->loc, "declarator nested too deeply");

	memset(d, 0, sizeof(*d));
	d->loc = tok(c)->loc;
	for (;;) {
		if (cc_token_is(c, LEX_TOKEN_STAR) || cc_token_is(c, LEX_TOKEN_CARET)) {	/* ^: a block pointer (Darwin SDK prototypes) */
			int q = 0, blk = cc_token_is(c, LEX_TOKEN_CARET);

			next(c);

			for (;;) {
				int kw = cc_token_keyword(tok(c));

				if (kw == KW_const || kw == KW___const || kw == KW___const__)
					q |= AST_QUALIFIER_CONST;
				else if (kw == KW_volatile || kw == KW___volatile__ || kw == KW___volatile)
					q |= AST_QUALIFIER_VOLATILE;
				else if (kw == KW_restrict || kw == KW___restrict || kw == KW___restrict__)
					q |= AST_QUALIFIER_RESTRICT;
				else if (kw == KW__Atomic)
					q |= CC_DECLARATOR_QUALIFIER_ATOMIC;
				else if (cc_token_is_nullability(c, tok(c)))
					;
				else if (kw == KW___attribute__ || kw == KW___attribute ||
					 (cc_token_is(c, LEX_TOKEN_LEFT_SQUARE) && cc_token_peek(c, 1)->kind == LEX_TOKEN_LEFT_SQUARE)) {
					cc_attribute_list_parse(c, &d->at);
					continue;
				} else
					break;
				next(c);
			}
			if ((uint32_t)np == pcap)
				ptrs = cc_declarator_operations_grow(c, ptrs, np, &pcap, pcap + 1);
			ptrs[np++] = (cc_declarator_operation){ .kind = 0, .q = (uint8_t)q, .fl = (uint16_t)(blk ? AST_TYPE_FLAG_BLOCK : 0) };
			continue;
		}
		if (cc_token_is_keyword(c, KW___attribute__) || cc_token_is_keyword(c, KW___attribute)) {
			cc_attribute_list_parse(c, &d->at);
			continue;
		}
		break;
	}
	if (cc_token_is(c, LEX_TOKEN_LEFT_PAREN) && cc_declarator_nested_parse(c, abstract_ok)) {
		uint32_t plb0 = tok(c)->loc;

		next(c);
		cc_attribute_list_parse(c, &d->at);
		if (abstract_ok && cc_token_is_declaration_start(c, tok(c))) {
			/* (attributes type ...) starts an abstract function parameter
			   list, while (attributes *name) still groups a declarator. */
			attrs leading = d->at;
			memset(&d->at, 0, sizeof d->at);
			if (!cc_declarator_parameter_list_parse(c, &sufs[ns], &leading)) d->invalid = true;
			d->psyms = sufs[ns].a + sufs[ns].b;
			d->npsyms = sufs[ns].np;
			d->kr = sufs[ns].kr, d->has_func = true;
			d->plb = plb0, d->ple = c->prevloc;
			ns++;
		} else {
			cc_declarator_parse(c, &id, abstract_ok, &ifirst, &inops);
			cc_token_expect_match(c, LEX_TOKEN_RIGHT_PAREN, plb0);
			d->name = id.name;
			d->loc = id.loc;
			cc_attribute_merge(&d->at, &id.at);
			if (id.has_func) {
				d->psyms = id.psyms, d->npsyms = id.npsyms, d->kr = id.kr, d->has_func = true;
				d->plb = id.plb, d->ple = id.ple;
			}
			inner = true;
		}
	} else if (cc_token_is(c, LEX_TOKEN_IDENTIFIER) && !cc_token_keyword(tok(c))) {
		d->name = tok(c)->id;
		d->loc = tok(c)->loc;
		if (unlikely(c->field_uses))
			cc_field_uses_name_keep(c);
		if (!abstract_ok)	/* an error before the declaration ends declares it, invalid (cc_recovery_restore) */
			c->decl_name = d->name, c->decl_loc = d->loc, c->decl_depth = c->depth;
		next(c);
	} else if (!abstract_ok) {
		/* clang: a '(' that is not a grouping one starts the parameters of a nameless
		   declarator, and the name was expected after it */
		cc_report_fatal(c, cc_token_is(c, LEX_TOKEN_LEFT_PAREN) ? cc_token_peek(c, 1)->loc : tok(c)->loc,
		      c->in_struct ? "expected member name or ';' after declaration specifiers" : "expected identifier or '('");
	} else {
		d->loc = tok(c)->loc;	/* abstract: where the name would be (clang) */
	}
	cc_attribute_list_parse(c, &d->at);
	for (;;) {
		if (cc_token_accept(c, LEX_TOKEN_LEFT_SQUARE)) {
			if ((uint32_t)ns == scap)
				sufs = cc_declarator_operations_grow(c, sufs, ns, &scap, scap + 1);
			cc_declarator_array_suffix_parse(c, &sufs[ns++], d->name);
		} else if (cc_token_is(c, LEX_TOKEN_LEFT_PAREN)) {
			uint32_t plb;

			next(c);
			plb = tok(c)->loc;
			if ((uint32_t)ns == scap)
				sufs = cc_declarator_operations_grow(c, sufs, ns, &scap, scap + 1);
			if (unlikely(!cc_declarator_parameter_list_parse(c, &sufs[ns], NULL)))
				d->invalid = true;
			/* Grouping a name adds no operators: (f)(int x) still defines x.
			 * A pointer/array/function inside the group does take precedence. */
			if (!inops && !d->has_func && ns == 0) {
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
		cc_attribute_list_parse(c, &d->at);
	}
	/* assemble: pointers, suffixes reversed, inner ops */
	{
		uint32_t at = c->ndops;
		uint32_t n = (uint32_t)np + (uint32_t)ns + inops;

		if (at + n > c->capdops)
			c->dops = cc_declarator_operations_grow(c, c->dops, c->ndops, &c->capdops, at + n);
		/* the inner ops sit at [ifirst, ifirst + inops) which is below at */
		if (inner) {
			at = ifirst;
			memmove(&c->dops[at + np + ns], &c->dops[ifirst], inops * sizeof(cc_declarator_operation));
			for (int i = 0; i < np; i++)
				c->dops[at + (uint32_t)i] = ptrs[i];
			for (int i = 0; i < ns; i++)
				c->dops[at + (uint32_t)np + (uint32_t)i] = sufs[ns - 1 - i];
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

CC_PRIVATE ast_qualified_type cc_declarator_apply(cc *c, ast_qualified_type t, uint32_t first, uint32_t nops)
{
	for (uint32_t i = 0; i < nops; i++) {
		const cc_declarator_operation *o = &c->dops[first + i];

		switch (o->kind) {
		case 0:
			if (o->fl & AST_TYPE_FLAG_BLOCK) {
				ctype b = { .kind = AST_TYPE_POINTER, .flags = AST_TYPE_FLAG_BLOCK, .size = CC_PTRSIZE(c), .align = CC_PTRSIZE(c), .base = t };

				t = AST_QUALIFIED_TYPE(cc_type_intern(c, &b), o->q & AST_QUALIFIER_MASK);
			} else {
				t = AST_QUALIFIED_TYPE(AST_QUALIFIED_TYPE_INDEX(cc_type_pointer(c, t)), o->q & AST_QUALIFIER_MASK);
			}
			if (o->q & CC_DECLARATOR_QUALIFIER_ATOMIC)	/* AST_TYPE *_Atomic */
				t = cc_type_atomic(c, t, 0);
			break;
		case 1:
			if (!cc_type_complete(c, t)) {
				cc_report_error(c, o->loc, "array has incomplete element type '%s'", tyname(c, t));
				cc_report_forward_note(c, t);
			}
			if (AST_TYPE_KIND(c, t) == AST_TYPE_SVE_BUILTIN)
				cc_report_error(c, o->np, "array has sizeless element type '%s'", tyname(c, t));
			if (cc_type_is_function(c, t))
				cc_report_error(c, tok(c)->loc, "array of functions is not allowed");
			if (o->fl & AST_TYPE_FLAG_VLA)
				t = cc_type_vla(c, t, o->a);
			else if ((AST_TYPE(c, t)->flags & AST_TYPE_FLAG_VLA) && !(o->fl & AST_TYPE_FLAG_INCOMPLETE))
				/* int a[6][n]: an array of VLAs is one too (its size is known only at run time) */
				t = cc_type_vla(c, t, cc_node_integer_new(c, (i128)((uint64_t)o->a | (uint64_t)o->b << 32), c->t_size, o->np));
			else
				t = cc_type_array(c, t, (uint64_t)o->a | (uint64_t)o->b << 32, o->fl);
			break;
		case 2:
			if (cc_type_is_array(c, t) || cc_type_is_function(c, t))
				cc_report_error(c, tok(c)->loc, "function cannot return %s type", cc_type_is_array(c, t) ? "array" : "function");
			t = cc_type_function(c, AST_QUALIFIED_TYPE_UNQUALIFIED(t), &c->lists[o->a], o->b, o->fl);
			break;
		}
	}
	return t;
}

CC_PRIVATE ast_qualified_type cc_type_name_parse(cc *c)
{
	cc_declaration_specifiers ds;
	cc_declarator d;
	uint32_t first, nops;
	ast_qualified_type t;

	c->in_tyname++;
	if (!cc_declaration_specifiers_parse(c, &ds, false))
		cc_report_fatal(c, tok(c)->loc, "expected a type");
	c->in_tyname--;
	cc_declarator_parse(c, &d, true, &first, &nops);
	cc_attribute_list_parse(c, &d.at);
	t = cc_declarator_apply(c, ds.ty, first, nops);
	c->ndops = first;
	if (d.name)	/* clang: the abstract declarator ends before it */
		cc_report_error(c, d.loc, "expected ')'");
	{
		uint32_t vs = d.at.vecsize ? d.at.vecsize : ds.at.vecsize;

		/* (__attribute__((vector_size(AST_NODE))) AST_TYPE) */
		if (vs) t = cc_declarator_vector_attribute_apply(c, t, vs, d.loc);
	}
	c->tn_tdef = ds.tdef && AST_QUALIFIED_TYPE_UNQUALIFIED(t) == AST_QUALIFIED_TYPE_UNQUALIFIED(ds.ty) ? ds.tdef : 0;
	return t;
}
