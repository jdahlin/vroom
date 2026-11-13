/*
 * translation-unit.c: cxx_run, the C++ front end's entry (cxx.h).
 *
 * Reads the TU's tokens, parses them with name lookup, and reports the
 * first error with how far the parser got (PLAN-cxx.md's measure of
 * progress on json: bench/cxx-json.py --stage syntax).
 */
#include <stdio.h>
#include "cxx-internal.h"

static void cxx_free(cxx *c)
{
	free(c->tok);
	free(c->undo);
	free(c->late);
	pp_temporary_hold(c->p, 0);
	cc_translation_unit_free(c->ast);
	free(c->ast);
	/* (scopes and entities are leaked with the TU for now: the AST step gives them an arena) */
}

int cxx_run(pp *p, const cc_opts *co, const cxx_options *o)
{
	cxx c;
	jmp_buf top;
	int status = 0;

	memset(&c, 0, sizeof c);
	c.p = p;
	c.o = o;
	c.ast = malloc(sizeof(cc));
	cc_translation_unit_init(c.ast, p, co);	/* the builtin types, the tables */
	cxx_tokens_read(&c);
	c.global = cxx_scope_new(&c, CXX_SCOPE_NAMESPACE, NULL, NULL);
	c.scope = c.global;
	{
		/* clang's predeclared types (Sema::Initialize / ASTContext's builtin typedefs) */
		static const char *const builtin_types[] = {
			"__builtin_va_list", "__int128_t", "__uint128_t", "__builtin_ms_va_list", "__NSConstantString",
			"_Float32", "_Float64", "_Float128", "_Float32x", "_Float64x", "__ibm128",
		};

		for (size_t i = 0; i < sizeof builtin_types / sizeof *builtin_types; i++)
			cxx_declare(&c, c.global, vintern_cstr(builtin_types[i]), CXX_ENTITY_TYPE);
	}
	c.fail = &top;
	if (!setjmp(top)) {
		cxx_translation_unit_parse(&c);
	} else {
		const char *name = "?";
		uint32_t nl = 1, line = 0, col = 0;
		cxx_token *t = &c.tok[c.error_pos < c.ntok ? c.error_pos : c.ntok];
		char ctx[400];
		size_t n = 0;

		pp_location_describe(p, t->loc, &name, &nl, &line, &col);
		/* the tokens around the error, as written */
		for (uint32_t i = c.error_pos > 8 ? c.error_pos - 8 : 0; i < c.error_pos + 8 && i < c.ntok && n < sizeof ctx - 80; i++)
			n += (size_t)snprintf(ctx + n, sizeof ctx - n, "%s%.*s%s", i == c.error_pos ? ">>>" : "",
					      (int)(c.tok[i].len > 40 ? 40 : c.tok[i].len), c.tok[i].s, i == c.error_pos ? "<<< " : " ");
		dprintf(o->errfd, "%.*s:%u:%u: error: %s\n  %s\n", (int)nl, name, line, col, c.error, ctx);
		status = 1;
	}
	if (o->progress)
		dprintf(o->errfd, "cxx: parsed %u/%u tokens\n", status ? c.error_pos : c.ntok, c.ntok);
	cxx_free(&c);
	return status;
}
