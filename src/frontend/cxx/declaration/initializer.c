/* C++ declarations: initializers and braced lists */
#include "declaration.h"

VPRIV void cxx_braced_init_list_parse(cxx *c)
{
	/* code generation: the elements' nodes of a flat list { a, b, c } (c->brace_n; UINT32_MAX: not flat,
	   or an element was not built) */
	uint32_t nodes[CXX_BRACE_MAX], n = 0;
	bool flat = true;

	cxx_expect(c, LEX_TOKEN_LEFT_BRACE);
	while (!cxx_is(c, LEX_TOKEN_RIGHT_BRACE)) {
		if (cxx_is(c, LEX_TOKEN_PERIOD) && cxx_peek(c, 1)->kind == LEX_TOKEN_IDENTIFIER) {	/* .designator = */
			cxx_advance(c);
			cxx_advance(c);
			if (!cxx_accept(c, LEX_TOKEN_EQUAL) && !cxx_is(c, LEX_TOKEN_LEFT_BRACE))
				cxx_fail(c, "expected '=' after a designator");
			flat = false;
		}
		flat &= !cxx_is(c, LEX_TOKEN_LEFT_BRACE);
		cxx_initializer_parse(c);
		if (flat && n < CXX_BRACE_MAX && c->enode)
			nodes[n++] = c->enode;
		else
			flat = false;
		if (cxx_accept(c, LEX_TOKEN_ELLIPSIS))
			flat = false;
		if (!cxx_accept(c, LEX_TOKEN_COMMA))
			break;
	}
	cxx_expect(c, LEX_TOKEN_RIGHT_BRACE);
	c->brace_n = flat ? n : UINT32_MAX;
	if (flat)
		memcpy(c->brace_nodes, nodes, n * sizeof *nodes);
}

/* initializer-clause: assignment-expression or braced-init-list */
VPRIV void cxx_initializer_parse(cxx *c)
{
	if (cxx_is(c, LEX_TOKEN_LEFT_BRACE))
		cxx_braced_init_list_parse(c);
	else
		cxx_assignment_expression_parse(c);
}

VPRIV void cxx_expression_list_parse(cxx *c)
{
	cxx_expect(c, LEX_TOKEN_LEFT_PAREN);
	while (!cxx_is(c, LEX_TOKEN_RIGHT_PAREN)) {
		cxx_initializer_parse(c);
		cxx_accept(c, LEX_TOKEN_ELLIPSIS);
		if (!cxx_accept(c, LEX_TOKEN_COMMA))
			break;
	}
	cxx_expect(c, LEX_TOKEN_RIGHT_PAREN);
}

/* ---- functions --------------------------------------------------------------------------- */
