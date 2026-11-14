/* C++ expressions: argument lists, builtin calls */
#include "expression.h"

VPRIV void cxx_call_arguments_parse(cxx *c)
{
	cxx_arguments_parse(c, LEX_TOKEN_LEFT_PAREN, LEX_TOKEN_RIGHT_PAREN);
}

VPRIV void cxx_braced_arguments_parse(cxx *c)
{
	cxx_arguments_parse(c, LEX_TOKEN_LEFT_BRACE, LEX_TOKEN_RIGHT_BRACE);
}

VPRIV void cxx_arguments_parse(cxx *c, int open, int close)
{
	qty args[CXX_MAX_ARGS];
	uint8_t lval[CXX_MAX_ARGS];
	uint32_t n = 0;
	uint32_t nodes[CXX_MAX_ARGS] = { 0 }, braces[CXX_MAX_ARGS] = { 0 }, zero = 0;
	bool unknown = false, first_const = false;
	int64_t first_value = 0;

	cxx_expect(c, open);
	while (!cxx_is(c, close)) {
		uint32_t start = c->pos, seen = c->npacks_seen;
		cxx_entity *saved[8];

		uint8_t was_invalid = c->einvalid, was_unsure = c->eunsure, was_missing = c->member_missing;

		memcpy(saved, c->packs_seen, sizeof saved);
		c->npacks_seen = 0, c->packs_overflow = 0;
		/* a braced list as an argument: typed here, no code (an initializer_list's element built from it
		   re-parses it there: cxx_gen_initializer_list) */
		bool brace = cxx_is(c, LEX_TOKEN_LEFT_BRACE);	/* (f({...}) too: its node is no expression's) */

		c->unevaluated += brace;
		cxx_initializer_parse(c);
		c->unevaluated -= brace;
		if (brace)
			c->enode = 0;
		/* f(std::forward<Args>(args)...) in an instance: the argument once per element of its packs */
		if (cxx_is(c, LEX_TOKEN_ELLIPSIS) && c->npacks_seen && !c->packs_overflow && c->instantiating && !c->tentative &&
		    !c->template_depth) {
			cxx_entity *packs[8];
			uint32_t np = c->npacks_seen, len = c->packs_seen[0]->npack, end = c->pos;
			bool ok = true;

			memcpy(packs, c->packs_seen, sizeof packs);
			for (uint32_t k = 0; k < np; k++)
				ok &= packs[k]->npack == len && !packs[k]->pack_placeholder;
			if (ok) {
				/* (what the pattern said with its packs unexpanded says nothing of the elements) */
				c->einvalid = was_invalid, c->eunsure = was_unsure, c->member_missing = was_missing;
				for (uint32_t i = 0; i < len; i++) {
					for (uint32_t k = 0; k < np; k++)
						cxx_pack_element(c, packs[k], (int32_t)i);
					c->pos = start;
					cxx_initializer_parse(c);
					if (c->pos != end)
						unknown = true;
					if (c->etype == CXX_TY_UNKNOWN && !c->tentative)
						c->eunsure = 1;
					if (n < CXX_MAX_ARGS)
						args[n] = c->etype, lval[n] = c->elval, nodes[n] = c->enode;	/* (each element's node too) */
					n++;
				}
				for (uint32_t k = 0; k < np; k++)
					cxx_pack_element(c, packs[k], -1);
				c->pos = end + 1;
				c->npacks_seen = seen;
				memcpy(c->packs_seen, saved, sizeof saved);
				if (!cxx_accept(c, LEX_TOKEN_COMMA))
					break;
				continue;
			}
		}
		/* not expanded here: its packs belong to an enclosing expansion, with the ones seen before */
		{
			cxx_entity *mine[8];
			uint32_t nmine = c->npacks_seen;

			memcpy(mine, c->packs_seen, sizeof mine);
			c->npacks_seen = seen;
			memcpy(c->packs_seen, saved, sizeof saved);
			for (uint32_t k = 0; k < nmine && k < 8; k++) {
				uint32_t j = 0;

				while (j < c->npacks_seen && c->packs_seen[j] != mine[k])
					j++;
				if (j == c->npacks_seen && j < 8)
					c->packs_seen[c->npacks_seen++] = mine[k];
			}
		}
		if (c->etype == CXX_TY_UNKNOWN && !c->tentative)
			c->eunsure = 1;	/* (the call may get a type whatever this argument is) */
		if (n < CXX_MAX_ARGS)
			args[n] = c->etype, lval[n] = c->elval, nodes[n] = c->enode, braces[n] = brace ? start : 0,
			zero |= (uint32_t)(c->econst && !c->evalue && !brace && c->etype != CXX_TY_UNKNOWN &&
					   AST_TYPE_KIND(c->ast, c->etype) >= TY_BOOL && AST_TYPE_KIND(c->ast, c->etype) <= TY_UINT128) << n;
		if (!n)
			first_const = c->econst, first_value = c->evalue;
		n++;
		if (cxx_accept(c, LEX_TOKEN_ELLIPSIS))
			unknown = true;
		if (!cxx_accept(c, LEX_TOKEN_COMMA))
			break;
	}
	cxx_expect(c, close);
	if (n > CXX_MAX_ARGS)
		unknown = true;
	c->carg_const = n == 1 && !unknown && first_const, c->carg_value = first_value;
	memcpy(c->carg_nodes, nodes, sizeof nodes);
	memcpy(c->carg_brace, braces, sizeof braces);
	c->carg_zero = zero;
	if (n > c->ccargs)
		c->cargs = realloc(c->cargs, (c->ccargs = n + 16) * sizeof *c->cargs),
		c->cargs_lval = realloc(c->cargs_lval, c->ccargs);
	memcpy(c->cargs, args, (n < CXX_MAX_ARGS ? n : 16) * sizeof *args);
	if (n)
		memcpy(c->cargs_lval, lval, n < CXX_MAX_ARGS ? n : 16);
	c->ncargs = unknown ? UINT32_MAX : n;
}

/* a builtin trait: __is_same(T, U), __builtin_xxx(args); arguments are types or expressions */
VPRIV void cxx_builtin_call_parse(cxx *c)
{
	qty types[2];
	uint32_t n = 0;
	uint32_t bn[4], nb = 0;	/* (code generation: the expression arguments' nodes) */
	qty first = CXX_TY_UNKNOWN;

	cxx_advance(c);
	cxx_expect(c, LEX_TOKEN_LEFT_PAREN);
	c->bconst = 0;
	while (!cxx_is(c, LEX_TOKEN_RIGHT_PAREN)) {
		/* Args...: a bound pack of types, each a type argument (__is_constructible(T, Args...)) */
		if (cxx_is(c, LEX_TOKEN_IDENTIFIER) && !cxx_kw(c) && cxx_peek(c, 1)->kind == LEX_TOKEN_ELLIPSIS && !c->tentative &&
		    !c->template_depth) {
			cxx_entity *pk = cxx_lookup(c, cxx_peek(c, 0)->id);

			if (pk && pk->kind == CXX_ENTITY_TYPE && pk->is_pack && !pk->pack_placeholder && pk->pack_index < 0) {
				for (uint32_t i = 0; i < pk->npack; i++)
					if (n < 2 && pk->pack[i].kind == 1 && pk->pack[i].t != CXX_TY_UNKNOWN)
						types[n++] = pk->pack[i].t;
					else
						n = 3;
				c->npacks_seen = 0;	/* (expanded here) */
				cxx_advance(c);
				cxx_advance(c);
				if (!cxx_accept(c, LEX_TOKEN_COMMA))
					break;
				continue;
			}
		}
		if (cxx_try_type_id(c)) {
			uint32_t start = c->pos, seen = c->npacks_seen;

			c->npacks_seen = 0;
			cxx_type_id_parse(c);
			if (cxx_is(c, LEX_TOKEN_ELLIPSIS) && c->npacks_seen == 1 && !c->packs_seen[0]->pack_placeholder && c->instantiating &&
			    !c->tentative && !c->template_depth) {
				/* __is_constructible(T, Args...): the pack's elements, each a type argument */
				cxx_entity *pk = c->packs_seen[0];
				uint32_t end = c->pos;

				for (uint32_t i = 0; i < pk->npack; i++) {
					cxx_pack_element(c, pk, (int32_t)i);
					c->pos = start;
					cxx_type_id_parse(c);
					if (n < 2 && c->type_out != CXX_TY_UNKNOWN && c->pos == end)
						types[n++] = c->type_out;
					else
						n = 3;
				}
				cxx_pack_element(c, pk, -1);
				c->pos = end + 1;
				c->npacks_seen = seen;
				if (!cxx_accept(c, LEX_TOKEN_COMMA))
					break;
				continue;
			}
			c->npacks_seen = seen;
			if (n < 2 && c->type_out != CXX_TY_UNKNOWN)
				types[n++] = c->type_out;
			else
				n = 3;
		} else {
			cxx_initializer_parse(c);
			if (!nb && n == 0)
				first = c->etype, c->bconst = c->econst, c->bvalue = c->evalue;
			if (nb < 4 && nb != UINT32_MAX)
				bn[nb++] = c->enode;
			else
				nb = UINT32_MAX;
			n = 3;
		}
		if (cxx_accept(c, LEX_TOKEN_ELLIPSIS))
			n = 3, nb = UINT32_MAX;
		if (!cxx_accept(c, LEX_TOKEN_COMMA))
			break;
	}
	cxx_expect(c, LEX_TOKEN_RIGHT_PAREN);
	c->nbnodes = n == 3 || n == 0 ? nb : UINT32_MAX;
	if (c->nbnodes != UINT32_MAX)
		memcpy(c->bnodes, bn, sizeof bn);
	c->bfirst = first;
	/* the type arguments, for the traits that are evaluated (3: something else) */
	c->nbtypes = n;
	if (n <= 2)
		memcpy(c->btypes, types, n * sizeof *types);
}

/* ---- lambdas and requires-expressions ---------------------------------------------------- */

/* a closure type's name: $_N and a number of the TU's own (templates instantiated with it in two TUs differ) */
VPRIV uint32_t cxx_closure_name(cxx *c, uint32_t serial)
{
	char nm[64] = { 0 };
	uint32_t h = c->ntok * 2654435761u;

	for (uint32_t i = 0; i < c->ntok; i += 1 + c->ntok / 512)
		h = (h ^ c->tok[i].len ^ (c->tok[i].len ? (unsigned char)c->tok[i].s[0] : 0)) * 16777619u;
	snprintf(nm, 48, "$_%u_%08x", serial, h);
	return vintern_cstr(nm);
}
