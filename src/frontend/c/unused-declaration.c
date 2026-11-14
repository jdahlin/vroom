/* vcc front end: warnings for variables, statics and typedefs that were never used */
#include "cc-internal.h"

/* errors other than clang's DefaultError warnings (-Wint-conversion...): clang's
   hasUncompilableErrorOccurred */
static uint32_t cc_report_hard_error_count(cc *c)
{
	uint32_t ce = (uint32_t)c->errors, pe = (uint32_t)pp_error_count(c->p), e = ce > pe ? ce : pe;

	return e > c->nconv ? e - c->nconv : 0;
}

/* -Wunused-variable: a block-scope variable never named (clang's
   ActOnPopScope, in declaration order) */
CC_PRIVATE void cc_unused_variables_report(cc *c, uint32_t m)
{
	static const char *const g[] = { "unused", "all", "most", NULL };

	for (uint32_t i = m; i < c->nsstack; i++) {
		sym *y = &c->syms[c->sstack[i]];
		const attrs *a = y->attrs ? &c->attr[y->attrs] : NULL;
		diag_span r;

		if (y->kind != AST_SYMBOL_VARIABLE || !y->name || (y->flags & (AST_SYMBOL_FLAG_PARAMETER | AST_SYMBOL_FLAG_ATTRIBUTE_USED)) ||
		    y->sc == AST_STORAGE_EXTERN || (a && (a->unused || a->cleanup || a->used)))
			continue;
		if (y->flags & AST_SYMBOL_FLAG_REFERENCED) {	/* -Wunused-but-set-variable: only assigned */
			if (!y->nref) {
				diag_info d = { "warning", y->loc, NULL, 0, NULL, 0, cc_identifier_string(y->name), (uint32_t)cc_identifier_length(y->name) };
				static const char *const gb[] = { "unused-but-set", "unused", "all", "most", NULL };

				diag_warning_report(c->p, "unused-but-set-variable", false, gb, &d, "variable '%.*s' set but not used", CC_IDENTIFIER(y->name));
			}
			continue;
		}
		r = (diag_span){ y->loc, pp_token_end_find(c->p, y->loc) };
		{
			diag_info d = { "warning", y->loc, &r, 1, NULL, 0, cc_identifier_string(y->name), (uint32_t)cc_identifier_length(y->name) };

			diag_warning_report(c->p, "unused-variable", false, g, &d, "unused variable '%.*s'", CC_IDENTIFIER(y->name));
		}
	}
}

/* -Wunused-variable, -Wunused-const-variable: a static file-scope variable
   of the main file never named, at its definition (clang's end of TU) */
CC_PRIVATE void cc_unused_statics_report(cc *c)
{
	static const char *const g[] = { "unused", "all", "most", NULL };
	static const char *const gc[] = { "unused-variable", "unused", "all", "most", NULL };

	if (cc_report_hard_error_count(c))
		return;
	for (uint32_t i = 1; i < c->nsym; i++)	/* alias("f") uses f */
		if (c->syms[i].attrs && c->attr[c->syms[i].attrs].alias) {
			uint32_t t = cc_symbol_global_find(c, c->strs[c->attr[c->syms[i].attrs].alias - 1].s);

			if (t)
				c->syms[t].flags |= AST_SYMBOL_FLAG_USED;
		}
	for (uint32_t i = 1; i < c->nsym; i++) {
		const sym *y = &c->syms[i];
		uint32_t at;
		bool ref = false, used = false, skip = false, fn = y->kind == AST_SYMBOL_FUNCTION;

		if ((y->kind != AST_SYMBOL_VARIABLE && !fn) || y->depth || y->sc != AST_STORAGE_STATIC || !y->name || cc_symbol_lookup(c, y->name) != i ||
		    (y->flags & AST_SYMBOL_FLAG_OVERLOAD))
			continue;
		for (uint32_t s = i; s; s = c->syms[s].next) {
			const sym *z = &c->syms[s];
			const attrs *a = z->attrs ? &c->attr[z->attrs] : NULL;

			ref |= (z->flags & AST_SYMBOL_FLAG_REFERENCED) != 0;
			used |= (z->flags & (AST_SYMBOL_FLAG_ATTRIBUTE_USED | AST_SYMBOL_FLAG_USED | AST_SYMBOL_FLAG_ADDRESS_TAKEN)) != 0;
			skip |= a && (a->unused || a->used || a->alias || a->constructor || a->destructor);
		}
		at = y->defloc ? y->defloc : y->loc;
		if (used || skip || !pp_location_is_in_main_file(c->p, at)	/* clang: also a non-inline header function (vcc: its references from unparsed lazy bodies are unknown) */)
			continue;
		{
			ast_qualified_type t = y->ty;
			bool k = false;
			diag_span r = { at, pp_token_end_find(c->p, at) };
			diag_info d = { "warning", at, &r, 1, NULL, 0, cc_identifier_string(y->name), (uint32_t)cc_identifier_length(y->name) };

			if (!fn) {
				while (AST_TYPE_KIND(c, t) == AST_TYPE_ARRAY)
					t = AST_QUALIFIED_TYPE(AST_QUALIFIED_TYPE_INDEX(AST_TYPE(c, t)->base), AST_QUALIFIED_TYPE_QUALIFIERS(AST_TYPE(c, t)->base) | AST_QUALIFIED_TYPE_QUALIFIERS(t));
				k = (AST_QUALIFIED_TYPE_QUALIFIERS(t) & AST_QUALIFIER_CONST) != 0;
			}
			if (ref) {	/* named only where it is not evaluated (sizeof, typeof) */
				static const char *const gu[] = { "unused-function", "unused", "all", "most", NULL };

				diag_warning_report(c->p, "unneeded-internal-declaration", false, gu, &d, "%s '%.*s' is not needed and will not be emitted",
					 fn ? "function" : "variable", CC_IDENTIFIER(y->name));
			} else if (fn)
				diag_warning_report(c->p, "unused-function", false, g, &d, "unused function '%.*s'", CC_IDENTIFIER(y->name));
			else
				diag_warning_report(c->p, k ? "unused-const-variable" : "unused-variable", false, k ? gc : g, &d,
					 "unused variable '%.*s'", CC_IDENTIFIER(y->name));
		}
	}
}

/* -Wunused-local-typedef: a block-scope typedef never named (clang: at the end of the TU) */
CC_PRIVATE void cc_unused_typedefs_report(cc *c)
{
	static const char *const g[] = { "unused-local-typedefs", "unused", "all", "most", NULL };

	if (c->errors || pp_error_count(c->p))
		return;
	for (uint32_t i = 1; i < c->nsym; i++) {
		const sym *y = &c->syms[i];
		const attrs *a = y->attrs ? &c->attr[y->attrs] : NULL;

		if (y->kind != AST_SYMBOL_TYPEDEF || !y->depth || !y->name || (y->flags & AST_SYMBOL_FLAG_REFERENCED) || (a && (a->unused || a->used)))
			continue;
		{
			diag_info d = { "warning", y->loc, NULL, 0, NULL, 0, cc_identifier_string(y->name), (uint32_t)cc_identifier_length(y->name) };

			diag_warning_report(c->p, "unused-local-typedef", false, g, &d, "unused typedef '%.*s'", CC_IDENTIFIER(y->name));
		}
	}
}
