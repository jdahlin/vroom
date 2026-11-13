/* vcc front end: struct, union and enum bodies */
#include "cc-internal.h"

_Thread_local cc_record_scratch cc_record_scratch_thread;

static void cc_record_scratch_grow(cc_record_scratch *x, uint32_t need)
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
static __attribute__((noinline, cold)) void cc_record_member_semicolon_expect(cc *c)
{
	uint32_t e = pp_token_end_find(c->p, c->prevloc);
	diag_fixit fx = { e, e, ";", cc_token_semicolon_approximate(c) };
	bool end = cc_token_is(c, LEX_TOKEN_RIGHT_BRACE);

	report(c, end ? "warning" : "error", e, NULL, 0, &fx, 1, "expected ';' at end of declaration list");
	if (!end)
		cc_recovery_skip_semicolon(c);
}

__attribute__((noinline)) CC_PRIVATE void cc_record_body_parse(cc *c, uint32_t ti)
{
	cc_record_scratch *x = &cc_record_scratch_thread;
	uint32_t base = x->n, nf = 0;
	field *fs;
	tag *r;
	attrs post = { 0 };

	/* A member's declaration specifiers can recursively define another record
	 * before reaching the declarator's own stack guard. Do not recover within
	 * an enclosing record while the C stack is still exhausted. */
	if (vstack_guard_low()) {
		cc_report_error(c, tok(c)->loc, "record nested too deeply");
		longjmp(c->bail, 1);
	}
	CC_REPLAY_TAG_WRITE_LOG(c, ti);
	c->tags[ti].flags |= AST_RECORD_FLAG_BEING_DEFINED;
	c->in_struct++;
	if (unlikely(c->field_uses))
		cc_field_uses_def(c, ti);
	cc_token_expect(c, LEX_TOKEN_LEFT_BRACE, "'{'");
	cc_recovery_point rv;

	if (CC_RECOVERY_ARM(c, &rv)) {
		cc_recovery_restore(c, &rv, CC_RECOVERY_BLOCK);	/* after a parse error: on with the next member */
		nf = x->n - base;
	}
	while (!cc_token_is(c, LEX_TOKEN_RIGHT_BRACE)) {
		cc_declaration_specifiers ds;

		if (cc_token_is(c, LEX_TOKEN_EOF)) {
			cc_report_error(c, pp_token_end_find(c->p, c->prevloc), "expected '}'");
			longjmp(c->bail, 1);
		}
		if (cc_token_is(c, LEX_TOKEN_SEMICOLON)) {
			CC_REPORT_EXTENSION_DELETE(c, ";", tok(c)->flags, "extra-semi", cc_warning_group_pedantic, tok(c)->loc, "extra ';' inside a %s",
				 c->tags[ti].flags & AST_RECORD_FLAG_UNION ? "union" : "struct");
			next(c);
			continue;
		}
		if (cc_token_is_keyword(c, KW__Static_assert)) {
			cc_static_assert_parse(c);
			continue;
		}
		if (!cc_declaration_specifiers_parse(c, &ds, false)) {
			int k = cc_token_peek(c, 1)->kind;

			if (!cc_token_is(c, LEX_TOKEN_IDENTIFIER))
				cc_report_fatal(c, tok(c)->loc, "expected member declaration");
			if (!cc_token_keyword(tok(c)) && (k == LEX_TOKEN_COMMA || k == LEX_TOKEN_SEMICOLON || k == LEX_TOKEN_COLON)) {
				/* clang: "struct { a, b; }": int members */
				cc_report_error(c, tok(c)->loc, "type name requires a specifier or qualifier");
				memset(&ds, 0, sizeof(ds));
				ds.ty = cc_type_basic(AST_TYPE_INT);
				ds.loc = tok(c)->loc;
				ds.invalid = true;
			} else {
				cc_declaration_specifiers_unknown_type_report(c, &ds, false);
			}
		}
		if (cc_token_is(c, LEX_TOKEN_SEMICOLON)) {
			/* anonymous struct/union member; with -fms-anonymous-structs also a
			 * tagged or typedef'd complete record */
			if (cc_type_is_record(c, ds.ty) && (!c->tags[AST_TYPE(c, ds.ty)->aux].name ||
						    (c->o->ms_anon && cc_type_complete(c, ds.ty)))) {
				if (nf >= (1u << 20))
					cc_report_fatal(c, ds.loc, "too many members");
				if (base + nf >= x->cap)
					cc_record_scratch_grow(x, base + nf + 1);
				x->fs[base + nf] = (field){ .ty = ds.ty, .loc = ds.loc };
				x->fa[base + nf] = ds.at.aligned > ds.align ? ds.at.aligned : ds.align;
				x->fp[base + nf] = ds.at.packed;
				x->n = base + ++nf;
			}
			next(c);
			continue;
		}
		for (;;) {
			cc_declarator d;
			uint32_t first = 0, nops = 0;
			ast_qualified_type t = ds.ty;
			field f = { 0 };

			if (!cc_token_is(c, LEX_TOKEN_COLON)) {
				cc_declarator_parse(c, &d, false, &first, &nops);
				t = cc_declarator_apply(c, ds.ty, first, nops);
				c->ndops = first;
			} else {
				memset(&d, 0, sizeof(d));
				d.loc = tok(c)->loc;
			}
			f.name = d.name;
			f.loc = d.loc;
			if (cc_token_accept(c, LEX_TOKEN_COLON)) {
				i128 w;
				uint32_t wn = 0;
				uint64_t bits = AST_TYPE_KIND(c, t) == AST_TYPE_BOOL ? 1 : cc_type_integer_bits(c, t);
				diag_span wr;

				cc_constant_expression_parse(c, &w, &wn);
				wr = wn ? cc_node_range(c, wn) : (diag_span){ d.loc, d.loc };
				if (!cc_type_complete(c, t))
					;	/* clang: only "field has incomplete type", below */
				else if (!cc_type_is_integer(c, t))
					report(c, "error", d.loc, &wr, 1, NULL, 0, "bit-field '%.*s' has non-integral type '%s'", CC_IDENTIFIER(d.name),
					       tyname(c, t));
				if (!cc_type_complete(c, t))
					w = 0;
				else if (w < 0)
					cc_report_error(c, d.loc, "bit-field '%.*s' has negative width (%lld)", CC_IDENTIFIER(d.name), (long long)w), w = 0;
				else if ((uint64_t)w > bits) {
					if (bits == 1)
						cc_report_error(c, d.loc, "width of bit-field '%.*s' (%lld bits) exceeds the width of its type (1 bit)",
							 CC_IDENTIFIER(d.name), (long long)w);
					else
						cc_report_error(c, d.loc, "width of bit-field '%.*s' (%lld bits) exceeds the width of its type (%llu bits)",
							 CC_IDENTIFIER(d.name), (long long)w, (unsigned long long)bits);
					w = 0;
				}
				else if (w == 0 && d.name)
					report(c, "error", d.loc, &wr, 1, NULL, 0, "named bit-field '%.*s' has zero width", CC_IDENTIFIER(d.name));
				f.bitfield = 1;
				f.width = (uint16_t)w;
			}
			cc_attribute_list_parse(c, &d.at);
			cc_attribute_merge(&d.at, &ds.at);
			if (d.at.mode) {
				int k = cc_attribute_mode_kind(c, d.at.mode, cc_type_is_signed(c, t));

				if (k)
					t = cc_type_basic(k);
			}
			if (d.at.vecsize) t = cc_declarator_vector_attribute_apply(c, t, d.at.vecsize, d.loc);
			f.ty = t;
			f.tdef = cc_typedef_sugar_declared(c, ds.tdef, ds.ty, t);
			f.counted_by = d.at.counted_by;
			f.depr = unlikely(d.at.depr) ? cc_attribute_new(c, &d.at) : 0;
			if (cc_type_is_function(c, t))
				cc_report_error(c, d.loc, "field '%.*s' declared as a function", CC_IDENTIFIER(d.name));
			else if (AST_TYPE_KIND(c, t) == AST_TYPE_SVE_BUILTIN) {
				cc_report_error(c, d.loc, "field has sizeless type '%s'", tyname(c, t));
				f.invalid = 1;
			}
			else if (!ds.invalid && !cc_type_complete(c, t) && !(cc_type_is_array(c, t) && (AST_TYPE(c, t)->flags & AST_TYPE_FLAG_INCOMPLETE))) {
				cc_report_error(c, d.loc, "field has incomplete type '%s'", tyname(c, t));
				cc_report_forward_note(c, t);
				f.invalid = 1;
			}
			f.invalid |= ds.invalid;
			if (nf >= (1u << 20))
				cc_report_fatal(c, d.loc, "too many members");
			for (uint32_t i = 0; f.name && i < nf; i++)
				if (x->fs[base + i].name == f.name) {
					cc_report_error(c, d.loc, "duplicate member '%.*s'", CC_IDENTIFIER(f.name));
					cc_report_note(c, x->fs[base + i].loc, "previous declaration is here");
					break;
				}
			if (base + nf >= x->cap)
				cc_record_scratch_grow(x, base + nf + 1);
			x->fa[base + nf] = d.at.aligned > ds.align ? d.at.aligned : ds.align;
			x->fp[base + nf] = d.at.packed;
			x->fs[base + nf] = f;
			if (unlikely(c->field_uses) && f.name)
				cc_field_uses_decl(c, ti, nf, f.name, d.loc);
			x->n = base + ++nf;
			if (!cc_token_accept(c, LEX_TOKEN_COMMA))
				break;
		}
		if (unlikely(!cc_token_accept(c, LEX_TOKEN_SEMICOLON)))
			cc_record_member_semicolon_expect(c);
	}
	CC_RECOVERY_POP(c, &rv);
	next(c);
	c->in_struct--;
	cc_attribute_list_parse(c, &post);
	r = &c->tags[ti];
	r->f = cc_heap_allocate(c, (nf + 1) * sizeof(field));
	memcpy(r->f, &x->fs[base], nf * sizeof(field));
	fs = r->f;
	r->nfield = nf;
	cc_tag_deprecate(c, ti, &post);
	r = &c->tags[ti];
	if (post.ms_struct)
		r->flags |= AST_RECORD_FLAG_MS_STRUCT;
	if (post.packed)
		r->flags |= AST_RECORD_FLAG_PACKED;
	if (post.aligned > r->maxalign)
		r->maxalign = post.aligned;
	if (post.transparent)
		c->ty[r->ty].flags |= AST_TYPE_FLAG_TRANSPARENT;
	for (uint32_t i = 0; i < nf; i++)
		if (cc_type_is_array(c, fs[i].ty) && (AST_TYPE(c, fs[i].ty)->flags & AST_TYPE_FLAG_INCOMPLETE)) {
			if (!(r->flags & AST_RECORD_FLAG_UNION) && i + 1 < nf) {
				cc_report_error(c, fs[i].loc, "flexible array member '%.*s' with type '%s' is not at the end of struct",
					 CC_IDENTIFIER(fs[i].name), tyname(c, fs[i].ty));
				cc_report_note(c, fs[i + 1].loc, "next field declaration is here");
			}
			r->flags |= AST_RECORD_FLAG_FLEXIBLE;
		}
	cc_layout_record(c, r, &x->fa[base], &x->fp[base]);
	r->flags = (r->flags | AST_RECORD_FLAG_COMPLETE) & ~(uint32_t)AST_RECORD_FLAG_BEING_DEFINED;
	x->n = base;
}

CC_PRIVATE void cc_enum_body_parse(cc *c, uint32_t ti, attrs *after, uint32_t loc)
{
	i128 v = -1, mn = 0, mx = 0;
	bool any = false;
	cc_id_list cs;
	ast_qualified_type fixed = 0;

	CC_REPLAY_TAG_WRITE_LOG(c, ti);
	cc_id_list_init(&cs);
	if (cc_token_accept(c, LEX_TOKEN_COLON)) {
		cc_declaration_specifiers ds;
		uint32_t baseloc = tok(c)->loc, colon = c->prevloc;
		tag *r = &c->tags[ti];
		ast_qualified_type old = c->ty[r->ty].base;

		if (!cc_declaration_specifiers_parse(c, &ds, false))
			cc_report_fatal(c, baseloc, "expected an integer underlying type");
		fixed = ds.ty;
		if (AST_TYPE(c, fixed)->flags & AST_TYPE_FLAG_ATOMIC) {
			cc_report_error(c, baseloc, "atomic enumeration underlying type is not supported");
			fixed = AST_QUALIFIED_TYPE(AST_TYPE(c, fixed)->n, AST_QUALIFIED_TYPE_QUALIFIERS(fixed));
		}
		if (!cc_type_is_integer(c, fixed) || AST_TYPE_KIND(c, fixed) == AST_TYPE_ENUM ||
		    (cc_type_kind_is_integer(AST_TYPE_KIND(c, fixed)) && AST_TYPE(c, fixed)->n)) {
			cc_report_error(c, baseloc, "'%s' is an invalid underlying type", tyname(c, fixed));
			fixed = cc_type_basic(AST_TYPE_INT);
		}
		if (AST_QUALIFIED_TYPE_QUALIFIERS(fixed))
			cc_report_warning(c, "ignored-qualifiers", true, NULL, baseloc, NULL, 0, NULL, 0,
				 "qualifiers in enumeration underlying type are ignored");
		fixed = AST_QUALIFIED_TYPE_UNQUALIFIED(fixed);
		if ((r->flags & AST_RECORD_FLAG_ENUM_DECLARED) && !(r->flags & AST_RECORD_FLAG_ENUM_FIXED)) {
			cc_report_error(c, loc, "enumeration previously declared with nonfixed underlying type");
			cc_report_note(c, r->loc, "previous declaration is here");
		} else if ((r->flags & AST_RECORD_FLAG_ENUM_FIXED) && !cc_type_is_compatible(c, fixed, old)) {
			cc_report_error(c, loc, "enumeration redeclared with different underlying type '%s' (was '%s')",
				 tyname(c, fixed), tyname(c, old));
			cc_report_note(c, r->loc, "previous declaration is here");
		}
		r->flags |= AST_RECORD_FLAG_ENUM_FIXED | AST_RECORD_FLAG_COMPLETE;
		c->ty[r->ty].base = fixed;
		c->ty[r->ty].size = r->size = cc_type_size(c, fixed);
		c->ty[r->ty].align = r->align = cc_type_alignment(c, fixed);
		if (c->o->std != 23)
			CC_REPORT_EXTENSION(c, "c23-extensions", cc_warning_group_pedantic, loc, "enumeration types with a fixed underlying type are a C23 extension");
		else {
			static const char *const compat_groups[] = { "pre-c23-compat-pedantic", NULL };
			cc_report_warning(c, "pre-c23-compat", false, compat_groups, loc, NULL, 0, NULL, 0,
				 "enumeration types with a fixed underlying type are incompatible with C standards before C23");
		}
		if (!cc_token_is(c, LEX_TOKEN_LEFT_BRACE)) {
			if (!cc_token_is(c, LEX_TOKEN_SEMICOLON))
				cc_report_error(c, colon, "non-defining declaration of enumeration with a fixed underlying type must be standalone [-Welaborated-enum-base]");
			return;
		}
	} else if (c->tags[ti].flags & AST_RECORD_FLAG_ENUM_FIXED) {
		cc_report_error(c, loc, "enumeration previously declared with fixed underlying type");
		cc_report_note(c, c->tags[ti].loc, "previous declaration is here");
		fixed = c->ty[c->tags[ti].ty].base;
	}
	if (c->tags[ti].flags & AST_RECORD_FLAG_ENUM_DEFINED) {
		cc_report_error(c, loc, "redefinition of enumeration");
		cc_report_note(c, c->tags[ti].loc, "previous definition is here");
	}
	cc_token_expect(c, LEX_TOKEN_LEFT_BRACE, "'{'");
	c->tags[ti].flags |= AST_RECORD_FLAG_BEING_DEFINED;
	while (!cc_token_is(c, LEX_TOKEN_RIGHT_BRACE)) {
		uint32_t name, loc, s;
		attrs ea = { 0 };

		if (!cc_token_is(c, LEX_TOKEN_IDENTIFIER) || cc_token_keyword(tok(c)))
			cc_report_fatal(c, tok(c)->loc, "expected identifier");
		name = tok(c)->id;
		loc = tok(c)->loc;
		next(c);
		cc_attribute_list_parse(c, &ea);
		bool explicit_value = cc_token_accept(c, LEX_TOKEN_EQUAL);
		if (explicit_value)
			cc_constant_expression_parse(c, &v, NULL);
		else
			v++;
		if (fixed) {
			unsigned bits = AST_TYPE_KIND(c, fixed) == AST_TYPE_BOOL ? 1 : (unsigned)cc_type_size(c, fixed) * 8;
			bool sign = cc_type_is_signed(c, fixed);
			i128 lo = sign && bits < 128 ? -((i128)1 << (bits - 1)) : 0;
			u128 hi = bits == 128 ? ~(u128)0 >> sign : ((u128)1 << (bits - sign)) - 1;

			if (bits < 128 && (v < lo || (v >= 0 && (u128)v > hi))) {
				if (explicit_value)
					cc_report_error(c, loc, "enumerator value is not representable in underlying type '%s'", tyname(c, fixed));
				else {
					char b[48];
					cc_format_i128(b, sizeof b, v);
					cc_report_error(c, loc, "enumerator value %s is not representable in underlying type '%s'", b, tyname(c, fixed));
				}
			}
		}
		if (!any || v < mn)
			mn = v;
		if (!any || v > mx)
			mx = v;
		any = true;
		{
			uint32_t o = cc_symbol_lookup(c, name);

			if (o && c->syms[o].depth == c->depth && c->syms[o].kind != CC_SYMBOL_NAMESPACE_TAG) {
				if (c->syms[o].kind == AST_SYMBOL_ENUMERATOR)
					cc_report_error(c, loc, "redefinition of enumerator '%.*s'", CC_IDENTIFIER(name));
				else
					cc_report_error(c, loc, "redefinition of '%.*s' as different kind of symbol", CC_IDENTIFIER(name));
				cc_report_note(c, c->syms[o].loc, "previous definition is here");
			}
		}
		if (!fixed && unlikely(v < INT32_MIN || v > INT32_MAX) && c->o->std != 23) {
			char b[48];

			cc_format_i128(b, sizeof b, v);
			CC_REPORT_EXTENSION(c, "c23-extensions", cc_warning_group_pedantic, loc, "enumerator value which exceeds the range of 'int' is a C23 extension "
				 "(%s is too %s)", b, v < 0 ? "small" : "large");
		}
		s = cc_symbol_new(c, AST_SYMBOL_ENUMERATOR, name, fixed ? AST_QUALIFIED_TYPE(c->tags[ti].ty, 0) : v >= INT32_MIN && v <= INT32_MAX ? cc_type_basic(AST_TYPE_INT) :
						 v >= 0 && v <= UINT32_MAX ? cc_type_basic(AST_TYPE_UNSIGNED_INT) : v >= INT64_MIN && v <= INT64_MAX ? cc_type_basic(CC_I64(c, false)) : cc_type_basic(CC_I64(c, true)), loc);
		if (unlikely(ea.depr))
			cc_attribute_apply_symbol(c, &c->syms[s], &ea);
		c->syms[s].val = (int64_t)v;
		cc_symbol_bind(c, s);
		cc_scope_prototype_bindings_save(c, s);
		cc_id_list_append(c, &cs, s);
		if (!cc_token_accept(c, LEX_TOKEN_COMMA))
			break;
	}
	cc_token_expect(c, LEX_TOKEN_RIGHT_BRACE, "'}'");
	cc_attribute_list_parse(c, after);
	{
		ast_qualified_type best;
		ctype *et = &c->ty[c->tags[ti].ty];

		if (fixed) {
			best = AST_QUALIFIED_TYPE_UNQUALIFIED(fixed);
		} else if (after->packed) {
			if (mn < 0)
				best = mn >= INT8_MIN && mx <= INT8_MAX ? cc_type_basic(AST_TYPE_SIGNED_CHAR) : mn >= INT16_MIN && mx <= INT16_MAX ? cc_type_basic(AST_TYPE_SHORT) :
				       mn >= INT32_MIN && mx <= INT32_MAX ? cc_type_basic(AST_TYPE_INT) : cc_type_basic(CC_I64(c, false));
			else
				best = mx <= UINT8_MAX ? cc_type_basic(AST_TYPE_UNSIGNED_CHAR) : mx <= UINT16_MAX ? cc_type_basic(AST_TYPE_UNSIGNED_SHORT) :
				       mx <= UINT32_MAX ? cc_type_basic(AST_TYPE_UNSIGNED_INT) : cc_type_basic(CC_I64(c, true));
		} else if (mn < 0) {
			best = mn >= INT32_MIN && mx <= INT32_MAX ? cc_type_basic(AST_TYPE_INT) : cc_type_basic(CC_I64(c, false));
		} else {
			best = mx <= UINT32_MAX ? cc_type_basic(AST_TYPE_UNSIGNED_INT) : cc_type_basic(CC_I64(c, true));
		}
		et->base = best;
		et->size = cc_type_size(c, best);
		et->align = cc_type_alignment(c, best);
		c->tags[ti].flags = (c->tags[ti].flags | AST_RECORD_FLAG_COMPLETE | AST_RECORD_FLAG_ENUM_DEFINED) & ~(uint32_t)AST_RECORD_FLAG_BEING_DEFINED;
		c->tags[ti].size = et->size;
		c->tags[ti].align = et->align;
		/* clang: if every value fits int the enumerators are int, else all of
		 * them have the enumerated type */
		for (uint32_t i = 0; i < cs.n; i++) {
			sym *y = &c->syms[cs.v[i]];

			if (fixed || mn < INT32_MIN || mx > INT32_MAX)
				y->ty = AST_QUALIFIED_TYPE(c->tags[ti].ty, 0);
			else
				y->ty = cc_type_basic(AST_TYPE_INT);
		}
		c->tags[ti].enums = cc_id_list_freeze(c, &cs);
		c->tags[ti].nenum = cs.n;
	}
}
