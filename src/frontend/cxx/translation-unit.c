/*
 * translation-unit.c: cxx_run, the C++ front end's entry (cxx.h).
 *
 * Reads the TU's tokens, parses them with name lookup, and reports the
 * first error with how far the parser got (PLAN-cxx.md's measure of
 * progress on json: bench/cxx-json.py --stage syntax).
 */
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
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
	c.name_path_valid = 1;
	c.trace = getenv("VCC_CXX_TRACE") != NULL;
	c.layout_fd = -1;
	c.mangle_fd = getenv("VCC_CXX_MANGLE") ? open(getenv("VCC_CXX_MANGLE"), O_WRONLY | O_CREAT | O_TRUNC, 0644) : -1;
	if (getenv("VCC_CXX_LAYOUT"))	/* static_asserts of the layouts built, for clang to check (bench/cxx-layout.py) */
		c.layout_fd = open(getenv("VCC_CXX_LAYOUT"), O_WRONLY | O_CREAT | O_TRUNC, 0644);
	cxx_tokens_read(&c);
	c.global = cxx_scope_new(&c, CXX_SCOPE_NAMESPACE, NULL, NULL);
	c.scope = c.global;
	{
		/* clang's predeclared types (Sema::Initialize / ASTContext's builtin typedefs) */
		static const char *const builtin_types[] = {
			"__builtin_va_list", "__int128_t", "__uint128_t", "__builtin_ms_va_list", "__NSConstantString",
			"_Float32", "_Float64", "_Float128", "_Float32x", "_Float64x", "__ibm128",
		};

		for (size_t i = 0; i < sizeof builtin_types / sizeof *builtin_types; i++) {
			cxx_entity *e = cxx_declare(&c, c.global, vintern_cstr(builtin_types[i]), CXX_ENTITY_TYPE);

			if (e && i == 0)
				e->ty = c.ast->t_valist;
			else if (e && (i == 1 || i == 2))
				e->ty = cc_type_basic(i == 1 ? TY_INT128 : TY_UINT128);
		}
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
	if (o->progress) {
		const struct cxx_stats *st = &c.stats;

		dprintf(o->errfd, "cxx: parsed %u/%u tokens\n", status ? c.error_pos : c.ntok, c.ntok);
		dprintf(o->errfd, "cxx: skipped also: attribute %u, empty base %u\n", st->classes_skipped[CXX_SKIP_ATTRIBUTE],
			st->classes_skipped[CXX_SKIP_EMPTY_BASE]);
		dprintf(o->errfd, "cxx: declarations %u, typed %u; classes %u, laid out %u, skipped: template %u, bases %u, "
			"virtual %u, member %u, bit-field %u, specialization %u; symbols %u\n",
			st->declarations, st->declarations_typed, st->classes, st->classes_laid_out, st->classes_skipped[CXX_SKIP_TEMPLATE],
			st->classes_skipped[CXX_SKIP_BASES], st->classes_skipped[CXX_SKIP_VIRTUAL], st->classes_skipped[CXX_SKIP_UNKNOWN_MEMBER],
			st->classes_skipped[CXX_SKIP_BITFIELD], st->classes_skipped[CXX_SKIP_SPECIALIZATION], st->symbols);
		dprintf(o->errfd, "cxx: expression statements %u, typed %u\n", st->expressions, st->expressions_typed);
		dprintf(o->errfd, "cxx: function template bodies %u, failed %u, cached %u, depth %u\n", st->bodies, st->bodies_failed,
			st->bodies_cached, st->body_depth_max);
		dprintf(o->errfd, "cxx: calls %u, typed %u; untyped: dependent %u, none viable %u, ambiguous %u, template not recorded %u, packs %u, "
			"signature %u, argument %u, unbound %u, class match %u, return type %u, callee unknown %u\n", st->calls, st->calls_typed,
			st->why[0], st->why[1], st->why[2], st->why[3], st->why[4], st->why[5], st->why[6], st->why[7], st->why[8], st->why[9], st->why[10]);
		dprintf(o->errfd, "cxx: out-of-class member bodies %u, failed %u, ambiguous %u\n", st->ooc_bodies, st->ooc_failed, st->ooc_ambiguous);
		dprintf(o->errfd, "cxx: static_asserts true %u, false %u, not evaluated %u\n", st->asserts_true, st->asserts_false, st->asserts_unknown);
		dprintf(o->errfd, "cxx: instance member bodies %u, failed %u\n", st->member_bodies, st->member_bodies_failed);
		dprintf(o->errfd, "cxx: instances %u made, %u failed; cache %u; partial specializations: %u matched, %u unknown\n",
			c.instances_made, c.instances_failed, c.ninstances, c.partial_matches, c.partial_unknown);
	}
	/* code generation: the functions whose bodies were built (codegen.c), through C's emitter */
	if (!status && co->emit && !pp_error_count(p)) {
		if (o->progress)
			dprintf(o->errfd, "cxx: function definitions %u, with code %u\n", c.stats.functions, c.stats.functions_generated);
		cxx_gen_vtables_emit(&c);
		status = emit_tu(c.ast, co) != 0;
	}
	if (c.layout_fd >= 0)
		close(c.layout_fd);
	if (c.mangle_fd >= 0)
		close(c.mangle_fd);
	cxx_free(&c);
	return status;
}
