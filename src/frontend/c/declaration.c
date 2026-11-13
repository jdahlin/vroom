/* vcc front end: declarations, cc_declaration_bind(), K&R parameters and the declaration list */
#include "cc-internal.h"

/* the braced initializer of a VLA, already diagnosed: skipped unchecked */
static void cc_declaration_vla_initializer_skip(cc *c)
{
	int depth = 0;

	do {
		if (cc_token_is(c, LEX_TOKEN_LEFT_BRACE))
			depth++;
		else if (cc_token_is(c, LEX_TOKEN_RIGHT_BRACE))
			depth--;
		else if (cc_token_is(c, LEX_TOKEN_EOF))
			cc_report_fatal(c, tok(c)->loc, "expected '}'");
		next(c);
	} while (depth > 0);
}

/* ---------------- declarations ---------------- */

CC_PRIVATE uint32_t cc_attribute_new(cc *c, const attrs *a)
{
	uint32_t i = c->nattr++;

	c->attr[i] = *a;
	return i;
}

/* clang's -Wdeprecated-declarations for a use at loc of name, a: the record */
CC_PRIVATE __attribute__((noinline, cold)) void cc_deprecated_use_report(cc *c, uint32_t loc, uint32_t name, const attrs *a, uint32_t dloc)
{
	if (unlikely(a->unavail)) {	/* clang: an error, also inside an unavailable function */
		if (a->deprmsg)
			cc_report_error(c, loc, "'%.*s' is unavailable: %s", CC_IDENTIFIER(name), c->strs[a->deprmsg - 1].s);
		else
			cc_report_error(c, loc, "'%.*s' is unavailable", CC_IDENTIFIER(name));
		cc_report_note(c, dloc, "'%.*s' has been explicitly marked unavailable here", CC_IDENTIFIER(name));	/* (clang: the declaration) */
		return;
	}
	if (c->fn && (c->syms[c->fn].flags & AST_SYMBOL_FLAG_DEPRECATED) && !c->attr[c->syms[c->fn].attrs].unavail)
		return;	/* clang: not inside a deprecated function */
	if (a->deprmsg)
		cc_report_warning(c, "deprecated-declarations", true, NULL, loc, NULL, 0, NULL, 0, "'%.*s' is deprecated: %s",
			 CC_IDENTIFIER(name), c->strs[a->deprmsg - 1].s);
	else
		cc_report_warning(c, "deprecated-declarations", true, NULL, loc, NULL, 0, NULL, 0, "'%.*s' is deprecated", CC_IDENTIFIER(name));
	cc_report_note(c, a->depr, "'%.*s' has been explicitly marked deprecated here", CC_IDENTIFIER(name));
}

/* a deprecated tag: its record */
CC_PRIVATE void cc_tag_deprecate(cc *c, uint32_t ti, const attrs *a)
{
	if (a->depr && !c->tags[ti].depr)
		c->tags[ti].depr = cc_attribute_new(c, a);
}

CC_PRIVATE void cc_attribute_apply_symbol(cc *c, sym *s, const attrs *a)
{
	if (cc_attribute_is_empty(a))
		return;		/* (no record: sym_attrs() in emit.c reads attrs 0 as none) */
	if (a->interrupt) {
		const ctype *ft = AST_TYPE(c, s->ty);
		if (c->o->target != PP_LINUX_X86_64)
			cc_report_error(c, s->loc, "interrupt calling convention is supported only for x86-64 Linux");
		else if (s->kind != AST_SYMBOL_FUNCTION || ft->kind != AST_TYPE_FUNCTION)
			cc_report_error(c, s->loc, "'interrupt' attribute only applies to functions");
		else if (AST_TYPE_KIND(c, ft->base) != AST_TYPE_VOID)
			cc_report_error(c, s->loc, "interrupt function must return void");
		else if ((ft->flags & AST_TYPE_FLAG_NO_PROTOTYPE) || ft->n < 1 || ft->n > 2 || !cc_type_is_pointer(c, c->params[ft->aux]))
			cc_report_error(c, s->loc, "interrupt function requires a pointer parameter optionally followed by an unsigned long");
		else if (ft->n == 2 && AST_TYPE_KIND(c, c->params[ft->aux + 1]) != AST_TYPE_UNSIGNED_LONG && AST_TYPE_KIND(c, c->params[ft->aux + 1]) != AST_TYPE_UNSIGNED_LONG_LONG)
			cc_report_error(c, s->loc, "interrupt function second parameter must have unsigned long type");
	}
	if (a->tlsmodel && (s->kind != AST_SYMBOL_VARIABLE || !(s->flags & AST_SYMBOL_FLAG_THREAD)))
		cc_report_error(c, a->tlsloc, "'tls_model' attribute only applies to thread-local variables");
	if (s->attrs)
		cc_attribute_merge(&c->attr[s->attrs], a);
	else
		s->attrs = cc_attribute_new(c, a);
	if (a->aligned > s->align)
		s->align = a->aligned;
	if (a->weak) s->flags |= AST_SYMBOL_FLAG_WEAK;
	if (a->depr) s->flags |= AST_SYMBOL_FLAG_DEPRECATED;
	if (a->wur) s->flags |= AST_SYMBOL_FLAG_WARN_UNUSED_RESULT;
	if (a->noreturn) s->flags |= AST_SYMBOL_FLAG_NORETURN;
	if (a->always_inline) s->flags |= AST_SYMBOL_FLAG_ALWAYS_INLINE;
	if (a->noinline) s->flags |= AST_SYMBOL_FLAG_NOINLINE;
	if (a->noinstr) s->flags |= AST_SYMBOL_FLAG_NO_INSTRUMENT;
	if (a->gnu_inline) s->flags |= AST_SYMBOL_FLAG_GNU_INLINE;
	if (a->used) s->flags |= AST_SYMBOL_FLAG_ATTRIBUTE_USED;
	if (a->overloadable) s->flags |= AST_SYMBOL_FLAG_OVERLOAD;
}

/* the file-scope entity name refers to, if any */
CC_PRIVATE uint32_t cc_symbol_file_scope_find(cc *c, uint32_t name)
{
	for (uint32_t s = cc_symbol_lookup(c, name); s; s = c->syms[s].shadow)
		if (c->syms[s].depth == 0)
			return s;
	return 0;
}

/* the file-scope sym named s (the emitter: alias targets), or 0 */
uint32_t cc_symbol_global_find(cc *c, const char *s)
{
	size_t n = strlen(s);
	uint32_t id = vintern_find(s, n, vintern_hash(s, n));

	return id ? cc_symbol_file_scope_find(c, id) : 0;
}

CC_PRIVATE uint32_t cc_declaration_bind(cc *c, const cc_declaration_specifiers *ds, cc_declarator *d, ast_qualified_type t, bool file)
{
	attrs at = d->at;
	uint32_t s, prev;
	int kind;
	bool inherit_type = true;

	cc_attribute_merge(&at, &ds->at);
	if (!at.visibility && c->nvis && file)
		at.visibility = c->visstk[c->nvis - 1];	/* #pragma GCC visibility push */
	if (ds->align > at.aligned)
		at.aligned = ds->align;
	if (d->at.mode && cc_type_is_arithmetic(c, t)) {
		int k = cc_attribute_mode_kind(c, d->at.mode, cc_type_is_signed(c, t));

		if (k)
			t = AST_QUALIFIED_TYPE(k, AST_QUALIFIED_TYPE_QUALIFIERS(t));
	}
	if (at.vecsize) t = cc_declarator_vector_attribute_apply(c, t, at.vecsize, d->loc);
	if (at.extvec && ds->sc == AST_STORAGE_TYPEDEF && cc_type_is_bitint(c, t))
		cc_report_error(c, d->loc, "vector integer elements wider than 128 bits are not supported");
	else if (at.extvec && ds->sc == AST_STORAGE_TYPEDEF && cc_type_is_arithmetic(c, t) && !cc_type_is_complex(c, t)) {
		/* clang: n elements, the size rounded up to a power of 2 (float3: 16) */
		uint64_t sz = AST_TYPE_KIND(c, t) == AST_TYPE_BOOL ? (at.extvec + 7) / 8 : (uint64_t)at.extvec * cc_type_size(c, t), p = 1;

		while (p < sz)
			p <<= 1;
		{
			ctype v = *AST_TYPE(c, cc_type_vector(c, t, (uint32_t)p));

			v.ext = (uint16_t)at.extvec;
			if (AST_TYPE_KIND(c, t) == AST_TYPE_BOOL) {
				v.n = at.extvec;
				if (c->o->target == PP_LINUX_X86_64) v.align = (uint32_t)p;
			}
			t = AST_QUALIFIED_TYPE(cc_type_intern(c, &v), AST_QUALIFIED_TYPE_QUALIFIERS(t));
		}
	}
	if (ds->sc == AST_STORAGE_TYPEDEF) {
		if (at.aligned)
			t = cc_type_aligned(c, t, at.aligned);
		if (at.transparent && AST_TYPE_KIND(c, t) == AST_TYPE_UNION) {
			CC_REPLAY_TAG_WRITE_LOG(c, AST_TYPE(c, t)->aux);
			c->ty[AST_QUALIFIED_TYPE_INDEX(t)].flags |= AST_TYPE_FLAG_TRANSPARENT;
		}
		prev = cc_symbol_lookup(c, d->name);
		if (prev && c->syms[prev].depth == c->depth && c->syms[prev].kind == AST_SYMBOL_TYPEDEF) {
			if (!cc_type_is_compatible(c, c->syms[prev].ty, t) || AST_QUALIFIED_TYPE_QUALIFIERS(c->syms[prev].ty) != AST_QUALIFIED_TYPE_QUALIFIERS(t))
				cc_report_error(c, d->loc, "typedef redefinition with different types");
			return prev;
		}
		s = cc_symbol_new(c, AST_SYMBOL_TYPEDEF, d->name, t, d->loc);
		c->syms[s].sc = AST_STORAGE_TYPEDEF;
		c->syms[s].tdef = ds->tdef && AST_QUALIFIED_TYPE_UNQUALIFIED(t) == AST_QUALIFIED_TYPE_UNQUALIFIED(ds->ty) ? ds->tdef : 0;
		if (unlikely(at.depr)) {	/* only what a typedef's use needs */
			attrs da = { 0 };

			da.depr = at.depr, da.deprmsg = at.deprmsg;
			cc_attribute_apply_symbol(c, &c->syms[s], &da);
		}
		cc_symbol_bind(c, s);
		if (cc_type_is_record(c, t) || AST_TYPE_KIND(c, t) == AST_TYPE_ENUM) {
			tag *r = &c->tags[AST_TYPE(c, t)->aux];

			if (!r->name && !r->typedef_name && file) {
				CC_REPLAY_TAG_WRITE_LOG(c, AST_TYPE(c, t)->aux);
				r->typedef_name = d->name;
			}
		}
		if (file)
			c->toplevel[c->ntoplevel++] = s;
		return s;
	}
	kind = cc_type_is_function(c, t) ? AST_SYMBOL_FUNCTION : AST_SYMBOL_VARIABLE;
	if (kind == AST_SYMBOL_VARIABLE && cc_type_is_void(c, t) && ds->sc != AST_STORAGE_EXTERN) {
		cc_report_error(c, d->loc, "variable has incomplete type 'void'");
		d->invalid = true;
	}
	/* redeclarations: the same entity in the same scope, or a block-scope extern */
	prev = 0;
	{
		uint32_t p = cc_symbol_lookup(c, d->name);
		bool same = p && c->syms[p].depth == c->depth;
		bool linkage = file || ds->sc == AST_STORAGE_EXTERN || kind == AST_SYMBOL_FUNCTION;

		if (same && linkage) {
			prev = p;
		} else if (same && c->syms[p].kind != AST_SYMBOL_TYPEDEF) {
			cc_report_error(c, d->loc, "redefinition of '%.*s'", CC_IDENTIFIER(d->name));
			cc_report_note(c, c->syms[p].loc, "previous definition is here");
		} else if (!file && linkage) {
			/* An extern declaration inherits the nearest visible declaration
			   with linkage, including one in an enclosing block. */
			inherit_type = p && (c->syms[p].flags & AST_SYMBOL_FLAG_GLOBAL);
			prev = inherit_type ? p : cc_symbol_file_scope_find(c, d->name);
		}
	}
	if (prev && kind == AST_SYMBOL_FUNCTION && c->syms[prev].kind == AST_SYMBOL_FUNCTION &&
	    (at.overloadable || (c->syms[prev].flags & AST_SYMBOL_FLAG_OVERLOAD))) {
		/* overloadable: redeclare the member with this type, or add one to the set */
		uint32_t head = prev, q;

		for (q = head; q; q = c->syms[q].ovl)
			if (cc_type_is_compatible(c, c->syms[q].ty, t))
				break;
		if (q) {
			prev = q;
		} else if (at.overloadable && (c->syms[head].flags & AST_SYMBOL_FLAG_OVERLOAD)) {
			sym *y;

			s = cc_symbol_new(c, kind, d->name, t, d->loc);
			y = &c->syms[s];
			y->sc = (uint8_t)ds->sc;
			y->depth = c->syms[head].depth;
			y->flags |= AST_SYMBOL_FLAG_GLOBAL;
			if (ds->inline_)
				y->flags |= AST_SYMBOL_FLAG_INLINE | (ds->sc != AST_STORAGE_EXTERN ? AST_SYMBOL_FLAG_INLINE_WITHOUT_EXTERN : 0);
			if (ds->noreturn)
				y->flags |= AST_SYMBOL_FLAG_NORETURN;
			cc_attribute_apply_symbol(c, y, &at);
			for (q = head; c->syms[q].ovl; q = c->syms[q].ovl)
				;
			c->syms[q].ovl = s;
			if (file)
				c->toplevel[c->ntoplevel++] = s;
			cc_mangle_overload_name_set(c, s);
			return s;
		} else {
			cc_report_error(c, d->loc, at.overloadable ? "redeclaration of '%.*s' must not have the 'overloadable' attribute"
			      : "redeclaration of '%.*s' must have the 'overloadable' attribute", CC_IDENTIFIER(d->name));
			return head;
		}
	}
	if (prev && unlikely(c->syms[prev].flags & AST_SYMBOL_FLAG_INVALID) && c->syms[prev].kind != kind)
		prev = 0;	/* a placeholder left by parser recovery (cc_recovery_restore): replaced */
	if (prev) {
		sym *y = &c->syms[prev];

		if (y->kind != kind) {
			cc_report_error(c, d->loc, "redefinition of '%.*s' as different kind of symbol", CC_IDENTIFIER(d->name));
			cc_report_note(c, y->loc, "previous definition is here");
		} else if (!cc_type_is_compatible(c, y->ty, t)) {
			cc_report_error(c, d->loc, "conflicting types for '%.*s'", CC_IDENTIFIER(d->name));
			if (kind == AST_SYMBOL_FUNCTION && (y->flags & AST_SYMBOL_FLAG_DEFINED) && y->defloc)	/* (clang: a defined function's definition) */
				cc_report_note(c, y->defloc, "previous definition is here");
			else
				cc_report_note(c, y->loc, "previous declaration is here");
		} else {
			ast_qualified_type merged = inherit_type ? AST_QUALIFIED_TYPE(AST_QUALIFIED_TYPE_INDEX(cc_type_composite(c, y->ty, t)), AST_QUALIFIED_TYPE_QUALIFIERS(y->ty) | AST_QUALIFIED_TYPE_QUALIFIERS(t)) : t;
			bool inner = !file && y->depth != c->depth;
			/* A composite bound is visible in the new declaration's scope;
			   completing an inner extern must not complete its outer binding. */
			if (!inner)
				y->ty = merged;
			if (ds->sc == AST_STORAGE_STATIC && kind == AST_SYMBOL_FUNCTION)
				y->sc = AST_STORAGE_STATIC;
			if (ds->inline_)
				y->flags |= AST_SYMBOL_FLAG_INLINE | (ds->sc != AST_STORAGE_EXTERN ? AST_SYMBOL_FLAG_INLINE_WITHOUT_EXTERN : 0);
			if (kind == AST_SYMBOL_FUNCTION && file && (!ds->inline_ || ds->sc == AST_STORAGE_EXTERN))
				y->flags |= AST_SYMBOL_FLAG_EXTERNAL_DECLARATION;
			cc_attribute_apply_symbol(c, y, &at);
			if (inner) {
				/* block-scope extern: a local type, linked to the same entity */
				s = cc_symbol_new(c, kind, d->name, merged, d->loc);
				c->syms[s].sc = AST_STORAGE_EXTERN;
				c->syms[s].next = prev;
				c->syms[s].flags = y->flags;
				cc_symbol_bind(c, s);
				return s;
			}
			return prev;
		}
	}
	s = cc_symbol_new(c, kind, d->name, t, d->loc);
	{
		sym *y = &c->syms[s];

		y->tdef = cc_typedef_sugar_declared(c, ds->tdef, ds->ty, t);
		y->sc = (uint8_t)(ds->sc == AST_STORAGE_TYPEDEF ? AST_STORAGE_NONE : ds->sc);
		if (file || ds->sc == AST_STORAGE_EXTERN || kind == AST_SYMBOL_FUNCTION)
			y->flags |= AST_SYMBOL_FLAG_GLOBAL;
		if (ds->inline_)
			y->flags |= AST_SYMBOL_FLAG_INLINE | (ds->sc != AST_STORAGE_EXTERN ? AST_SYMBOL_FLAG_INLINE_WITHOUT_EXTERN : 0);
		if (kind == AST_SYMBOL_FUNCTION && file && (!ds->inline_ || ds->sc == AST_STORAGE_EXTERN))
			y->flags |= AST_SYMBOL_FLAG_EXTERNAL_DECLARATION;
		if (ds->noreturn)
			y->flags |= AST_SYMBOL_FLAG_NORETURN;
		if (ds->thread)
			y->flags |= AST_SYMBOL_FLAG_THREAD;
		cc_attribute_apply_symbol(c, y, &at);
		if (!file && kind == AST_SYMBOL_VARIABLE && ds->sc != AST_STORAGE_EXTERN && ds->sc != AST_STORAGE_STATIC && !y->sc)
			y->sc = AST_STORAGE_AUTO;
		if (!file && prev == 0 && (ds->sc == AST_STORAGE_EXTERN || kind == AST_SYMBOL_FUNCTION)) {
			/* first declaration of a global from block scope: also the file-scope entity */
		}
	}
	cc_symbol_bind(c, s);
	if (file)
		c->toplevel[c->ntoplevel++] = s;
	if (kind == AST_SYMBOL_FUNCTION && at.overloadable)
		cc_mangle_overload_name_set(c, s);
	if (unlikely(c->npweak))
		cc_pragma_weak_declared(c, s);
	return s;
}

static void cc_declaration_kr_parameters_parse(cc *c, const cc_declarator *fd)
{
	uint8_t seen[256] = { 0 };

	/* tags declared here belong to the function, not the file (approximation: not seen by the body) */
	cc_scope_open(c);
	c->in_proto++;
	while (!cc_token_is(c, LEX_TOKEN_LEFT_BRACE)) {
		cc_declaration_specifiers ds;

		if (!cc_declaration_specifiers_parse(c, &ds, true)) {
			if (!cc_token_is(c, LEX_TOKEN_IDENTIFIER) || cc_token_keyword(tok(c)))
				cc_report_fatal(c, tok(c)->loc, "expected ';' after top level declarator");
			cc_declaration_specifiers_unknown_type_report(c, &ds, true);
		}
		for (;;) {
			cc_declarator d;
			uint32_t first, nops, i;
			ast_qualified_type t;

			cc_declarator_parse(c, &d, false, &first, &nops);
			cc_attribute_list_parse(c, &d.at);
			t = cc_declarator_apply(c, ds.ty, first, nops);
			c->ndops = first;
			if (cc_type_is_array(c, t))
				t = cc_type_pointer(c, AST_TYPE(c, t)->base);
			else if (cc_type_is_function(c, t))
				t = cc_type_pointer(c, t);
			for (i = 0; i < fd->npsyms; i++)
				if (c->syms[c->lists[fd->psyms + i]].name == d.name)
					break;
			if (i == fd->npsyms) {
				cc_report_error(c, d.loc, "parameter named '%.*s' is missing", CC_IDENTIFIER(d.name));
			} else {
				sym *y = &c->syms[c->lists[fd->psyms + i]];

				if (i < sizeof(seen) && seen[i]++)
					cc_report_error(c, d.loc, "redefinition of parameter '%.*s'", CC_IDENTIFIER(d.name));
				y->ty = t;
				y->loc = d.loc;
				if (ds.sc == AST_STORAGE_REGISTER)
					y->sc = AST_STORAGE_REGISTER;
			}
			if (!cc_token_accept(c, LEX_TOKEN_COMMA))
				break;
		}
		cc_token_expect_semicolon(c, "at end of declaration");
	}
	c->in_proto--;
	cc_scope_close(c);
	if (!c->o->implicit_int)
		for (uint32_t i = 0; i < fd->npsyms && i < sizeof(seen); i++)
			if (!seen[i]) {
				const sym *y = &c->syms[c->lists[fd->psyms + i]];

				cc_report_error(c, y->loc, "parameter '%.*s' was not declared, defaults to 'int'; ISO C99 and later do not support implicit int", CC_IDENTIFIER(y->name));
			}
}

/* a declaration; block-scope ones append AST_NODE_DECLARATION statements to out */
/* the ';' ending a declaration; clang's ParseDeclGroup when it is missing: unless a
   declaration starts here, the rest is skipped through the next ';' */
static void cc_declaration_semicolon_expect(cc *c, bool file)
{
	if (likely(cc_token_accept(c, LEX_TOKEN_SEMICOLON)))
		return;
	if (cc_token_expect_semicolon(c, file ? "after top level declarator" : "at end of declaration") && !cc_token_is_declaration_start(c, tok(c)))
		cc_recovery_skip_statement(c, false);
}

CC_PRIVATE void cc_declaration_parse(cc *c, cc_id_list *out, bool file)
{
	cc_declaration_specifiers ds;
	uint32_t dbeg = tok(c)->loc;
	bool attr_decl = cc_token_is(c, LEX_TOKEN_LEFT_SQUARE) && cc_token_peek(c, 1)->kind == LEX_TOKEN_LEFT_SQUARE;

	if (cc_token_is_keyword(c, KW__Static_assert)) {
		cc_static_assert_parse(c);
		return;
	}
	if (!cc_declaration_specifiers_parse(c, &ds, true)) {
		if (attr_decl && cc_token_accept(c, LEX_TOKEN_SEMICOLON))
			return;
		int k = cc_token_peek(c, 1)->kind;

		if (file && ((cc_token_is(c, LEX_TOKEN_IDENTIFIER) && !cc_token_keyword(tok(c)) &&
			      (k == LEX_TOKEN_LEFT_PAREN || k == LEX_TOKEN_SEMICOLON || k == LEX_TOKEN_COMMA || k == LEX_TOKEN_EQUAL || k == LEX_TOKEN_LEFT_SQUARE)) ||
			     cc_token_is(c, LEX_TOKEN_STAR))) {
			/* C89 implicit int: "f(x) { ... }", "x;" at file scope */
			memset(&ds, 0, sizeof(ds));
			ds.ty = cc_type_basic(AST_TYPE_INT);
			ds.loc = tok(c)->loc;
			if (!c->o->implicit_int) {
				uint32_t i = 0;

				while (cc_token_peek(c, i)->kind == LEX_TOKEN_STAR)	/* clang: at the name in "*p = 1;" */
					i++;
				cc_report_error(c, cc_token_peek(c, i)->kind == LEX_TOKEN_IDENTIFIER ? cc_token_peek(c, i)->loc : ds.loc,
					 "type specifier missing, defaults to 'int'; ISO C99 and later do not support implicit int [-Wimplicit-int]");
			}
		} else if (cc_token_is(c, LEX_TOKEN_IDENTIFIER) && !cc_token_keyword(tok(c))) {
			cc_declaration_specifiers_unknown_type_report(c, &ds, true);
		} else {
			cc_report_fatal(c, tok(c)->loc, "expected identifier or '('");
		}
	}
	if (cc_token_accept(c, LEX_TOKEN_SEMICOLON)) {
		if (ds.constexpr_)
			cc_report_error(c, ds.loc, "constexpr can only be used in variable declarations");
		return;
	}
	for (;;) {
		cc_declarator d;
		uint32_t first, nops, s;
		ast_qualified_type t;

		cc_declarator_parse(c, &d, false, &first, &nops);
		t = cc_declarator_apply(c, ds.ty, first, nops);
		if (ds.constexpr_ && !cc_type_is_function(c, t))
			t = cc_type_qualify(c, t, AST_QUALIFIER_CONST);
		if (file) {
			ast_qualified_type vm = t;

			while (cc_type_is_array(c, vm) || AST_TYPE_KIND(c, vm) == AST_TYPE_POINTER) {
				if (AST_TYPE(c, vm)->flags & AST_TYPE_FLAG_VLA) {
					cc_report_error(c, d.loc, "variably modified type declaration not allowed at file scope");
					ds.invalid = true;
					break;
				}
				vm = AST_TYPE(c, vm)->base;
			}
		}
		c->ndops = first;
		cc_asm_label_parse(c, &d.at);
		if (unlikely(file && (ds.sc == AST_STORAGE_AUTO || (ds.sc == AST_STORAGE_REGISTER && !d.at.asmname))) && d.name) {
			/* (GNU: a global register variable, "register AST_TYPE x asm("reg")", is fine) */
			/* (clang: the declaration is invalid; a function's at the declaration's start) */
			if (cc_type_is_function(c, t))
				cc_report_error(c, dbeg, "illegal storage class on function");
			else
				cc_report_error(c, d.loc, "illegal storage class on file-scoped variable");
			ds.invalid = true;
		}
		cc_attribute_list_parse(c, &d.at);
		if (ds.auto_type) {
			uint32_t v;

			if (!cc_token_is(c, LEX_TOKEN_EQUAL) && c->o->std == 23 && d.name) {
				/* clang's text for C23 'auto' (vcc cannot tell it from __auto_type here) */
				cc_declaration_specifiers d2 = ds;

				cc_report_error(c, d.loc, "declaration of variable '%.*s' with deduced type 'auto' requires an initializer",
					 CC_IDENTIFIER(d.name));
				d2.auto_type = false, d2.invalid = true, d2.ty = cc_type_basic(AST_TYPE_INT);
				cc_declaration_bind(c, &d2, &d, d2.ty, file);	/* (invalid: its uses say nothing more) */
				if (cc_token_accept(c, LEX_TOKEN_COMMA))
					continue;
				cc_declaration_semicolon_expect(c, file);
				return;
			}
			cc_token_expect(c, LEX_TOKEN_EQUAL, "'=' (__auto_type requires an initializer)");
			v = cc_conversion_decay(c, cc_assignment_parse(c));
			t = AST_QUALIFIED_TYPE(AST_QUALIFIED_TYPE_INDEX(AST_QUALIFIED_TYPE_UNQUALIFIED(AST_NODE(v)->ty)), AST_QUALIFIED_TYPE_QUALIFIERS(ds.ty) | (ds.constexpr_ ? AST_QUALIFIER_CONST : 0));
			{
				cc_declaration_specifiers d2 = ds;

				d2.ty = t;
				s = cc_declaration_bind(c, &d2, &d, t, file);
			}
			if (ds.constexpr_) {
				c->syms[s].flags |= AST_SYMBOL_FLAG_CONSTEXPR;
				if (!cc_constexpr_type_is_valid(c, t))
					cc_report_error(c, d.loc, "constexpr variable cannot have type '%s'", tyname(c, t));
				cc_constexpr_conversion_check(c, v, t);
				if (!cc_constexpr_initializer_is_ok(c, v))
					cc_report_error(c, d.loc, "constexpr variable '%.*s' must be initialized by a constant expression", CC_IDENTIFIER(d.name));
			}
			c->syms[s].body = v;
			c->syms[s].flags |= AST_SYMBOL_FLAG_DEFINED;
			if (out) {
				uint32_t n = cc_node_new_operands(c, AST_NODE_DECLARATION, cc_type_basic(AST_TYPE_VOID), d.loc, s, v);

				AST_NODE(n)->aux = 1; /* inferred type: its bounds are evaluated by the initializer */
				cc_id_list_append(c, out, n);
			}
			if (cc_declaration_comma_typo(c))
				return;
			if (cc_token_accept(c, LEX_TOKEN_COMMA))
				continue;
			cc_declaration_semicolon_expect(c, file);
			return;
		}
		if (file && d.kr && cc_type_is_function(c, t) && !cc_token_is(c, LEX_TOKEN_SEMICOLON) && !cc_token_is(c, LEX_TOKEN_COMMA) && !cc_token_is(c, LEX_TOKEN_EQUAL))
			cc_declaration_kr_parameters_parse(c, &d);
		else if (d.kr)
			cc_report_error(c, d.plb, "a parameter list without types is only allowed in a function definition");
		if (AST_TYPE_KIND(c, t) == AST_TYPE_SVE_BUILTIN && ds.sc != AST_STORAGE_TYPEDEF &&
		    (file || ds.sc == AST_STORAGE_STATIC || ds.sc == AST_STORAGE_EXTERN || ds.thread)) {
			cc_report_error(c, d.loc, "non-local variable with sizeless type '%s'", tyname(c, t));
			ds.invalid = true;
		}
		s = cc_declaration_bind(c, &ds, &d, t, file);
		if (ds.constexpr_) {
			if (c->syms[s].kind != AST_SYMBOL_VARIABLE)
				cc_report_error(c, d.loc, "constexpr can only be used in variable declarations");
			else {
				c->syms[s].flags |= AST_SYMBOL_FLAG_CONSTEXPR;
				if (file)
					c->syms[s].sc = AST_STORAGE_STATIC;	/* C23: file-scope constexpr has internal linkage */
				if (!cc_constexpr_type_is_valid(c, t))
					cc_report_error(c, d.loc, "constexpr variable cannot have type '%s'", tyname(c, t));
			}
			if (ds.sc == AST_STORAGE_EXTERN || ds.thread)
				cc_report_error(c, d.loc, "constexpr cannot be combined with extern or thread-local storage");
		}
		if (unlikely(ds.invalid | d.invalid)) {
			c->syms[s].flags |= AST_SYMBOL_FLAG_INVALID;	/* its type was an error: uses say nothing more */
			cc_report_delayed_bug(c, d.loc, "an invalid declaration without an error");
		}
		if (d.has_func && c->syms[s].kind == AST_SYMBOL_FUNCTION && cc_type_is_function(c, c->syms[s].ty) &&
		    d.npsyms == AST_TYPE(c, c->syms[s].ty)->n)
		{
			c->syms[s].psyms = d.psyms + 1, c->syms[s].plb = d.plb, c->syms[s].ple = d.ple;
			for (uint32_t i = 0; unlikely(c->nnparam) && i < d.npsyms; i++)	/* a nonnull parameter: checked at calls */
				if (c->syms[c->lists[d.psyms + i]].flags & AST_SYMBOL_FLAG_NONNULL)
					c->syms[s].flags |= AST_SYMBOL_FLAG_NONNULL;
		}
		if (d.at.vecsize || ds.at.vecsize || d.at.mode || ds.at.mode)
			t = c->syms[s].ty;	/* declare made it a vector / applied mode: initialize that */
		if (cc_type_is_function(c, t) && cc_token_is(c, LEX_TOKEN_LEFT_BRACE)) {
			if (!file)
				cc_report_fatal(c, tok(c)->loc, "function definition is not allowed here");
			if (cc_lazy_body_eligible(c, s, &d))
				cc_lazy_body_capture(c, s, &d);
			else
				cc_function_definition_parse(c, s, &d);
			return;
		}
		if (unlikely(file && d.has_func) && cc_type_is_function(c, t) && !cc_token_is(c, LEX_TOKEN_SEMICOLON) && !cc_token_is(c, LEX_TOKEN_COMMA) &&
		    !cc_token_is(c, LEX_TOKEN_EQUAL) && !cc_token_at_asm(c) && !cc_token_is_keyword(c, KW___attribute__) && !cc_token_is_keyword(c, KW___attribute) &&
		    !cc_token_is_declaration_start(c, tok(c))) {	/* a declaration next: the ';' is missing (cc_declaration_semicolon_expect) */
			/* clang: "int f(int x) return x; }": the '{' is missing */
			cc_report_error(c, tok(c)->loc, "expected function body after function declarator");
			cc_recovery_skip_statement(c, true);
			return;
		}
		if (cc_token_accept(c, LEX_TOKEN_EQUAL)) {
			sym *y = &c->syms[s];
			/* int a[]: the size of an earlier declaration (the composite type), if it had one */
			ast_qualified_type it = cc_type_is_array(c, t) && (AST_TYPE(c, t)->flags & AST_TYPE_FLAG_INCOMPLETE) && cc_type_is_array(c, y->ty) &&
				 !(AST_TYPE(c, y->ty)->flags & (AST_TYPE_FLAG_INCOMPLETE | AST_TYPE_FLAG_VLA)) ? AST_QUALIFIED_TYPE(AST_QUALIFIED_TYPE_INDEX(y->ty), AST_QUALIFIED_TYPE_QUALIFIERS(t)) : t;
			bool st = file || ds.sc == AST_STORAGE_STATIC;
			uint32_t v;
			int nerr = c->errors;
			uint32_t nconv0 = c->nconv;

			if (ds.sc == AST_STORAGE_TYPEDEF || cc_type_is_function(c, t))
				cc_report_error(c, d.loc, "illegal initializer");
			if (!file && ds.sc == AST_STORAGE_EXTERN)
				cc_report_error(c, d.loc, "declaration of block scope identifier with linkage cannot have an initializer");
			if (unlikely(AST_TYPE(c, t)->flags & AST_TYPE_FLAG_VLA)) {
				/* only = {} (C23, a GNU extension before): zero it at run time (emit gen_vla_decl) */
				if (cc_token_is(c, LEX_TOKEN_LEFT_BRACE) && cc_token_peek(c, 1)->kind == LEX_TOKEN_RIGHT_BRACE) {
					next(c), next(c);
					v = cc_node_new(c, AST_NODE_ZERO, t, d.loc);
				} else if (cc_token_is(c, LEX_TOKEN_LEFT_BRACE)) {
					/* clang: at the outermost size, no range */
					cc_report_error(c, AST_NODE(AST_TYPE(c, t)->n)->loc, "variable-sized object may not be initialized");
					cc_declaration_vla_initializer_skip(c);
					v = 0;
				} else {
					/* clang: at the name, the initializer underlined */
					diag_span r = cc_node_range(c, cc_assignment_parse(c));

					report(c, "error", d.loc, &r, 1, NULL, 0, "variable-sized object may not be initialized");
					v = 0;
				}
			} else if (!file && ds.sc != AST_STORAGE_EXTERN && ds.sc != AST_STORAGE_TYPEDEF && !cc_type_is_function(c, t) && !cc_type_is_array(c, t) &&
				   !cc_type_complete(c, t)) {
				/* clang: the variable is invalid, its initializer not checked */
				cc_report_error(c, d.loc, "variable has incomplete type '%s'", tyname(c, t));
				c->syms[s].flags |= AST_SYMBOL_FLAG_INVALID;
				if (cc_type_is_record(c, t) || AST_TYPE_KIND(c, t) == AST_TYPE_ENUM)
					cc_report_note(c, c->tags[AST_TYPE(c, t)->aux].loc, "forward declaration of '%s'", tyname(c, t));
				if (cc_token_is(c, LEX_TOKEN_LEFT_BRACE))
					cc_declaration_vla_initializer_skip(c);
				else
					(void)cc_assignment_parse(c);
				v = 0;
			} else {
				uint8_t sw = c->cv_what;
				uint32_t sl = c->cv_loc;

				c->cv_what = CC_CONVERSION_CONTEXT_INITIALIZER, c->cv_loc = d.loc;
				c->syms[s].defloc = d.loc;
				uint16_t nr0 = c->syms[s].nref;

				uint32_t saved_constexpr = c->constexpr_init;

				c->constexpr_init = ds.constexpr_ ? s : 0;
				c->defer_rt = st || ds.constexpr_, c->ndefer = 0;
				v = cc_initializer_parse(c, &it, st);
				c->constexpr_init = saved_constexpr;
				if (c->syms[s].nref != nr0 && !st)
					c->uv_any |= 2;	/* (a self-reference: -Wuninitialized) */
				c->defer_rt = 0;
				c->cv_what = sw, c->cv_loc = sl;
			}
			y = &c->syms[s];
			if (AST_QUALIFIED_TYPE_INDEX(it) != AST_QUALIFIED_TYPE_INDEX(t)) {
				/* array size from the initializer */
				y->ty = AST_QUALIFIED_TYPE(AST_QUALIFIED_TYPE_INDEX(it), AST_QUALIFIED_TYPE_QUALIFIERS(y->ty));
			}
			if (unlikely(cc_type_is_record(c, it) && !cc_type_complete(c, it))) {
				y->flags |= AST_SYMBOL_FLAG_INVALID;	/* initializer said so: its uses are silent */
				cc_report_delayed_bug(c, d.loc, "an incomplete initializer without an error");
			}
			if (y->body && file) {
				cc_report_error(c, d.loc, "redefinition of '%.*s'", CC_IDENTIFIER(d.name));
				cc_report_note(c, y->loc, "previous definition is here");
			}
			y->body = v;
			y->flags |= AST_SYMBOL_FLAG_DEFINED;
			if (ds.constexpr_ && c->errors == nerr && !cc_constexpr_initializer_is_ok(c, v))
				cc_report_error(c, d.loc, "constexpr variable '%.*s' must be initialized by a constant expression", CC_IDENTIFIER(d.name));
			{
				uint32_t bad = 0;

				/* not after an error in the initializer (clang: no follow-on error), but after
				   conversion errors, which are clang's warnings defaulting to errors */
				bool ok = true;

				if (st && !ds.constexpr_ && v && c->errors - c->nconv == nerr - nconv0) {
					c->sie = c->errors != nerr;
					ok = cc_initializer_static_is_ok(c, y->ty, v, &bad);
					c->sie = false;
				}
				if (!ok) {
					diag_span r = cc_node_range(c, bad);

					report(c, "error", cc_expression_location(c, bad), &r, 1, NULL, 0, "initializer element is not a compile-time constant");
					/* the runtime-behaviour warnings only for an initializer that runs (not a constant) */
					for (int k = 0; k < c->ndefer; k++)
						cc_report_warning(c, "division-by-zero", true, NULL, c->defer[k].loc, &c->defer[k].r, 1, NULL, 0,
							 "%s by zero is undefined", c->defer[k].rem ? "remainder" : "division");
				}
				c->ndefer = 0;
			}
			y = &c->syms[s];
			if (out && !st) {
				uint32_t n = cc_node_new_operands(c, AST_NODE_DECLARATION, cc_type_basic(AST_TYPE_VOID), d.loc, s, v);

				cc_id_list_append(c, out, n);
			}
		} else {
			sym *y = &c->syms[s];

			if (ds.constexpr_ && y->kind == AST_SYMBOL_VARIABLE)
				cc_report_error(c, d.loc, "constexpr variable '%.*s' must be initialized by a constant expression", CC_IDENTIFIER(d.name));
			if (y->kind == AST_SYMBOL_VARIABLE && ds.sc != AST_STORAGE_EXTERN && ds.sc != AST_STORAGE_TYPEDEF) {
				if (file)
					y->flags |= AST_SYMBOL_FLAG_TENTATIVE;
				else if (!cc_type_complete(c, y->ty) && ds.sc != AST_STORAGE_STATIC) {
					if (cc_type_is_array(c, y->ty))
						cc_report_error(c, d.loc, "definition of variable with array type needs an explicit size or an initializer");
					else if (!cc_type_is_void(c, y->ty)) {	/* void: declare said so */
						cc_report_error(c, d.loc, "variable has incomplete type '%s'", tyname(c, y->ty));
						cc_report_forward_note(c, y->ty);
					}
					y->flags |= AST_SYMBOL_FLAG_INVALID;
				}
				if (out && (y->sc == AST_STORAGE_AUTO || y->sc == AST_STORAGE_REGISTER)) {
					uint32_t n = cc_node_new_operands(c, AST_NODE_DECLARATION, cc_type_basic(AST_TYPE_VOID), d.loc, s, 0);

					cc_id_list_append(c, out, n);
					if (cc_uninitialized_is_tracked(c, y->ty)) {
						c->uv_any |= 1;
						y->flags |= AST_SYMBOL_FLAG_UNINITIALIZED_PENDING;
						if (c->nuv < 64)
							c->uvc[c->nuv] = c->cnd, c->uv[c->nuv++] = s;
					}
				}
			}
		}
		if (out && ds.sc == AST_STORAGE_TYPEDEF) {
			for (ast_qualified_type vm = t; cc_type_is_array(c, vm) || cc_type_is_pointer(c, vm); vm = AST_TYPE(c, vm)->base)
				if (AST_TYPE(c, vm)->flags & AST_TYPE_FLAG_VLA) {
					cc_id_list_append(c, out, cc_node_new_operands(c, AST_NODE_DECLARATION, cc_type_basic(AST_TYPE_VOID), d.loc, s, 0));
					break;
				}
		}
		if (c->syms[s].kind == AST_SYMBOL_VARIABLE)	/* the declaration's range, for notes */
			c->syms[s].plb = dbeg, c->syms[s].ple = c->prevloc;
		if (cc_declaration_comma_typo(c))
			return;
		if (cc_token_accept(c, LEX_TOKEN_COMMA))
			continue;
		cc_declaration_semicolon_expect(c, file);
		return;
	}
}
