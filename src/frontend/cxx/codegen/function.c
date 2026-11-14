/* C++ code generation: a function's definition: begin and end */
#include "codegen.h"

/*
 * A function definition at namespace scope, before its body: its parameters' symbols, bound to the
 * parameter entities of the body's scope fs.  False: not a function code is generated for yet.
 */
/* VCC_CXX_GEN=3: why a function definition was not even started (cxx_gen_function_begin) */
#define CXX_BEGIN_WHY() do { \
		if (getenv("VCC_CXX_GEN") && atoi(getenv("VCC_CXX_GEN")) >= 3 && f && !c->tentative && !c->template_depth && !c->unevaluated && \
		    !(f->sym && (c->ast->syms[f->sym].flags & SF_DEFINED))) \
			fprintf(stderr, "no begin (codegen.c:%d): %.*s\n", __LINE__, (int)vintern_ents[f->name].len, vintern_ents[f->name].s); \
	} while (0)

VPRIV bool cxx_gen_function_begin(cxx *c, cxx_entity *f, cxx_scope *fs, const cxx_param *ps, uint32_t nps)
{
	cc *a = c->ast;
	const ctype *ft;
	cc_id_list psyms;
	cxx_param own[32];

	/* (the parser's parameter records are its scratch: completing a class below parses declarations of its own) */
	if (nps > 32)
		{ CXX_BEGIN_WHY(); return false; }
	memcpy(own, ps, nps * sizeof *ps);
	ps = own;

	if (!a->o->emit || c->tentative || c->template_depth || c->unevaluated || !f || f->ty == CXX_TY_UNKNOWN ||
	    AST_TYPE_KIND(a, f->ty) != TY_FUNC || (f->sym && (a->syms[f->sym].flags & SF_DEFINED)) || c->gen_stack_depth > 48)
		{ CXX_BEGIN_WHY(); return false; }
	ft = AST_TYPE(a, f->ty);
	if ((ft->flags & TF_VARIADIC) || ft->n != nps || (!cxx_gen_value_ok(c, ft->base) && AST_TYPE_KIND(a, ft->base) != TY_VOID))
		{ CXX_BEGIN_WHY(); return false; }
	for (uint32_t i = 0; i < nps; i++)	/* (the parser's record of a parameter may lack its type: the function type has it) */
		if (!cxx_gen_value_ok(c, ps[i].ty != CXX_TY_UNKNOWN ? ps[i].ty : a->params[ft->aux + i]))
			{ CXX_BEGIN_WHY(); return false; }
	if (!cxx_gen_name(c, f) || !f->sym)
		{ CXX_BEGIN_WHY(); return false; }
	{
		/* Two definitions with one symbol name: char16_t and unsigned short (and wchar_t, char32_t,
		   char8_t) are one type here for now, so overloads on them mangle alike.  The first has the code. */
		const sym *y = &a->syms[f->sym];
		const char *nm = y->attrs && a->attr[y->attrs].asmname ? a->strs[a->attr[y->attrs].asmname - 1].s : NULL;

		if (nm && cxx_gen_name_defined(c, nm))
			{ CXX_BEGIN_WHY(); return false; }
	}
	ft = AST_TYPE(a, f->ty);
	/* (from here the body is being built: what was, if anything, is set aside until cxx_gen_function_end) */
	cxx_gen_push(c);
	c->gen = 0, c->gen_failed = 0, c->gen_this = 0, c->gen_pre = NULL, c->gen_level = c->instantiating;
	c->gen_sret = 0, c->gen_notemp = 0, c->gen_block_temps = 0;
	c->gen_lambda = NULL;	/* (a lambda's operator(): set by its caller after this) */
	cc_scope_open(a);
	cc_id_list_init(&psyms);
	if (cxx_gen_by_address(c, ft->base)) {
		/* the result object's address, first of all */
		uint32_t s = cc_symbol_new(a, S_VAR, 0, cc_type_pointer(a, QUNQ(ft->base)), a->syms[f->sym].loc);

		a->syms[s].flags |= SF_PARAM;
		a->syms[s].sc = SC_AUTO;
		cc_id_list_append(a, &psyms, s);
		c->gen_sret = s;
	}
	if (cxx_gen_has_this(f)) {
		uint32_t s = cc_symbol_new(a, S_VAR, vintern_cstr("this"), cxx_gen_this_type(c, f), a->syms[f->sym].loc);

		a->syms[s].flags |= SF_PARAM;
		a->syms[s].sc = SC_AUTO;
		cc_id_list_append(a, &psyms, s);
		c->gen_this = s;
	}
	for (uint32_t i = 0; i < nps; i++) {
		/* (a class passed by address: the parameter names the caller's temporary, as a reference would) */
		qty pin = ps[i].ty != CXX_TY_UNKNOWN ? ps[i].ty : a->params[AST_TYPE(a, f->ty)->aux + i];
		qty pty = cxx_gen_by_address(c, pin) ? cxx_type_reference(c, QUNQ(pin), false) : QUNQ(pin);
		uint32_t s = cc_symbol_new(a, S_VAR, ps[i].name > 1 ? ps[i].name : 0, pty, a->syms[f->sym].loc);
		cxx_entity *pe = ps[i].name > 1 ? cxx_lookup_in(c, fs, ps[i].name, 0) : NULL;

		a->syms[s].flags |= SF_PARAM;
		a->syms[s].sc = SC_AUTO;
		cc_id_list_append(a, &psyms, s);
		if (c->gen_pack_entity && i >= c->gen_pack_from && i < c->gen_pack_from + c->gen_pack_n)
			c->gen_pack_entity->pack_syms[i - c->gen_pack_from] = s;	/* (a pack's element: found through the pack) */
		else if (pe)
			pe->sym = s;
	}
	c->gen_psyms = cc_id_list_freeze(a, &psyms), c->gen_npsyms = psyms.n;
	c->gen_pre = calloc(1, sizeof(cc_id_list));
	cc_id_list_init(c->gen_pre);
	c->ngen_init_names = 0, c->ngen_init_bases = 0, c->gen_delegates = 0, c->gen_returns = 0, c->gen_why = 0;
	c->gen_ret = ft->base;
	a->fn = f->sym;
	c->gen = 1;
	c->snode = 0, c->sdone = 0, c->sdepth = 0, c->gen_block = NULL;
	if (cxx_gen_is_special(f) && vintern_ents[f->name].s[1] == 'c')
		c->gen_block = c->gen_pre;	/* (a constructor: its initializers' temporaries are declared among them) */
	return true;
}

/* ... after its body (body: the compound statement's node): defined, for emit_tu, if all of it was built */
VPRIV void cxx_gen_function_end(cxx *c, cxx_entity *f, uint32_t body)
{
	cc *a = c->ast;
	bool ok = c->gen && !c->gen_failed && body;
	cc_id_list *pre = c->gen_pre;

	if (!ok && c->gen && getenv("VCC_CXX_GEN") && atoi(getenv("VCC_CXX_GEN")) == 4)	/* VCC_CXX_GEN=4: each function without code, why */
		fprintf(stderr, "nofn %.*s: %s\n", (int)vintern_ents[f->name].len, vintern_ents[f->name].s, cxx_gen_why), cxx_gen_why[0] = 0;

	if (ok && cxx_gen_is_special(f) && c->gen_this) {
		const vident *fv = &vintern_ents[f->name];
		uint32_t bloc = AST_NODE_AT(a, body)->loc;
		cc_id_list all;

		cc_id_list_init(&all);
		if (fv->s[1] == 'c') {
			/* a constructor: the bases and members its initializers did not name, the initializers, its body
			   (members' default initializers: not built yet) */
			ok = cxx_gen_constructor_defaults(c, &all, bloc);
			if (!ok && getenv("VCC_CXX_GEN"))
				fprintf(stderr, "no code (constructor: %s): %.*s\n", cxx_class_has_member_initializer(c, f->member_of->owner->ty) ?
					"members with default initializers" : "a base or member to default-construct",
					(int)vintern_ents[f->member_of->owner->name].len, vintern_ents[f->member_of->owner->name].s);
			for (uint32_t i = 0; ok && pre && i < pre->n; i++)
				cc_id_list_append(a, &all, pre->v[i]);
			if (ok && !c->gen_delegates && cxx_class_polymorphic(c, f->member_of->owner->ty)) {
				/* the object is of this class from here: its vptr (members initialized by calls of virtual
				   functions would want it before them) */
				uint32_t st = cxx_gen_vptr_store(c, f->member_of, bloc);

				if (st)
					cc_id_list_append(a, &all, st);
				else
					ok = false;
			}
			if (ok && all.n) {
				cc_id_list_append(a, &all, body);
				body = cc_statement_new(a, N_BLOCK, bloc, 0, cc_id_list_freeze(a, &all), all.n);
			}
		} else {
			/* a destructor: its body, then its members and bases (a return in the body would skip them) */
			uint32_t before;

			if (cxx_class_polymorphic(c, f->member_of->owner->ty)) {
				uint32_t st = cxx_gen_vptr_store(c, f->member_of, bloc);	/* (the body sees an object of this class) */

				if (st)
					cc_id_list_append(a, &all, st);
				else
					ok = false;
			}
			cc_id_list_append(a, &all, body);
			before = all.n;
			ok = ok && cxx_gen_destructor_rest(c, &all, bloc);
			if (ok && all.n > before)
				ok = !c->gen_returns;
			if (ok && all.n > 1) {
				body = cc_statement_new(a, N_BLOCK, bloc, 0, cc_id_list_freeze(a, &all), all.n);
			}
		}
	}
	c->gen_pre = NULL;
	free(pre);
	uint32_t psyms = c->gen_psyms, npsyms = c->gen_npsyms;

	cxx_gen_pop(c);	/* (the body around this one, if any, goes on) */
	c->stats.functions++;
	if (ok && getenv("VCC_CXX_NOGEN") && f->sym) {
		/* (a debugging aid: the functions named in the file, one mangled name a line, get no code here, so
		   that another compiler's definition is used: bisecting which of vcc's is wrong) */
		static __thread char *list;
		static __thread size_t nlist;
		const vident *v = &vintern_ents[a->syms[f->sym].name];

		if (!list) {
			FILE *lf = fopen(getenv("VCC_CXX_NOGEN"), "r");

			list = calloc(1, 1 << 24);
			if (lf) {
				nlist = fread(list + 1, 1, (1 << 24) - 2, lf);
				fclose(lf);
			}
			list[0] = '\n';
			for (size_t i = 1; i <= nlist; i++)
				if (list[i] == '\r')
					list[i] = '\n';
		}
		for (const char *q = list; (q = strstr(q, "\n")) && q[1]; q++)
			if (!strncmp(q + 1, v->s, v->len) && (q[1 + v->len] == '\n' || !q[1 + v->len])) {
				ok = false;
				break;
			}
		if (!ok)
			return;
	}
	if (!ok)
		return;
	if (f->fn_inline) {
		/* an inline function's definition is weak (a reference to one that got no code stays a plain
		   undefined symbol: the link fails instead of calling nothing) */
		attrs at;

		memset(&at, 0, sizeof at);
		at.weak = 1;
		cc_attribute_apply_symbol(a, &a->syms[f->sym], &at);
		if (f->sym1 && f->sym1 != f->sym)
			cc_attribute_apply_symbol(a, &a->syms[f->sym1], &at);	/* (named before it was known to be inline: the alias below needs its target, which stays) */
		else
			a->syms[f->sym].flags |= SF_LINKONCE;	/* (emitted only if the TU refers to it) */
		if (f->sym0) {
			cc_attribute_apply_symbol(a, &a->syms[f->sym0], &at);
			a->syms[f->sym0].flags |= SF_LINKONCE;
		}
	}
	a->syms[f->sym].body = body;
	a->syms[f->sym].flags |= SF_DEFINED;
	a->syms[f->sym].defloc = a->syms[f->sym].loc;
	AST_NODE_AT(a, body)->a = psyms;
	AST_NODE_AT(a, body)->aux = (uint16_t)npsyms;
	a->toplevel[a->ntoplevel++] = f->sym;	/* (the TU's definitions, in order: what emit_tu walks) */
	if (f->sym1 == f->sym && cxx_gen_is_special(f) && f->sym) {
		/* (an inline one with one symbol for both names: another declaration's complete-object symbol, made
		   apart and referred to, is an alias of this one) */
		char n1[2048];
		cxx_scope *home = f->member_of ? f->member_of : f->home;
		uint32_t other;

		if (cxx_mangle_function(c, f, home, f->ty, false, 1, n1, sizeof n1) && (other = cxx_gen_symbol_named(c, n1, 0)) &&
		    other != f->sym && !(a->syms[other].flags & SF_DEFINED)) {
			const sym *y = &a->syms[f->sym];
			attrs at;

			memset(&at, 0, sizeof at);
			at.alias = a->attr[y->attrs].asmname;
			at.weak = f->fn_inline;
			cc_attribute_apply_symbol(a, &a->syms[other], &at);
			a->syms[other].flags |= SF_DEFINED;
			a->toplevel[a->ntoplevel++] = other;
		}
	}
	if (f->sym1 && f->sym1 != f->sym) {
		/* a constructor or destructor: the complete-object function is the same code (no virtual bases) */
		const sym *y = &a->syms[f->sym];
		attrs at;

		memset(&at, 0, sizeof at);
		at.alias = a->attr[y->attrs].asmname;
		cc_attribute_apply_symbol(a, &a->syms[f->sym1], &at);
		a->syms[f->sym1].flags |= SF_DEFINED;
		a->toplevel[a->ntoplevel++] = f->sym1;
	}
	if (f->fn_virtual && f->member_of) {
		if (f->sym0)
			cxx_gen_deleting_destructor(c, f);
		cxx_vtable_symbol(c, f->member_of);	/* (the key function's TU defines the vtable) */
	}
	c->stats.functions_generated++;
}
