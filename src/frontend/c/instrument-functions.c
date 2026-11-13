/* vcc front end: -finstrument-functions and -pg calls inserted into a body */
#include "cc-internal.h"

/* a call of void name(void *this_fn, void *call_site) (declared if need be) */
static uint32_t cc_instrument_call_new(cc *c, const char *name, uint32_t fn, uint32_t loc)
{
	uint32_t id = vintern_find(name, strlen(name), vintern_hash(name, strlen(name))), f, v, n;
	qty vp = cc_type_pointer(c, cc_type_basic(TY_VOID));
	cc_id_list args;

	f = id ? cc_symbol_file_scope_find(c, id) : 0;
	if (!f || c->syms[f].kind != S_FUNC) {
		qty pt[2] = { vp, vp };

		if (!id)
			id = vintern(name, strlen(name), vintern_hash(name, strlen(name)));
		f = cc_symbol_new(c, S_FUNC, id, cc_type_function(c, cc_type_basic(TY_VOID), pt, 2, 0), loc);
		c->syms[f].sc = SC_EXTERN;
		c->syms[f].flags |= SF_GLOBAL;
	}
	c->syms[f].flags |= SF_USED;
	cc_id_list_init(&args);
	v = cc_node_new(c, N_VAR, c->syms[fn].ty, loc);
	AST_NODE(v)->a = fn;
	c->syms[fn].flags |= SF_USED | SF_ADDR_TAKEN;
	cc_id_list_append(c, &args, cc_cast_insert(c, cc_node_rvalue(c, v), vp, CK_P2P));
	{
		cc_id_list ra;

		cc_id_list_init(&ra);
		cc_id_list_append(c, &ra, cc_node_integer_new(c, 0, cc_type_basic(TY_UINT), loc));
		cc_id_list_append(c, &args, cc_builtin_node_new(c, BI_RETURN_ADDRESS, vp, loc, &ra));
	}
	v = cc_node_new(c, N_VAR, c->syms[f].ty, loc);
	AST_NODE(v)->a = f;
	n = cc_node_new(c, N_CALL, cc_type_basic(TY_VOID), loc);
	AST_NODE(n)->a = cc_node_rvalue(c, v);
	AST_NODE(n)->b = cc_id_list_freeze(c, &args);
	AST_NODE(n)->c = args.n;
	return n;
}

/* -finstrument-functions: every return of fn (in statements st) calls the exit hook first */
static void cc_instrument_returns(cc *c, uint32_t fn, uint32_t st)
{
	node *x;

	if (!st)
		return;
	x = AST_NODE(st);
	switch (x->op) {
	case N_BLOCK:
		for (uint32_t i = 0; i < x->c; i++)
			cc_instrument_returns(c, fn, c->lists[x->b + i]);
		break;
	case N_IF:
		cc_instrument_returns(c, fn, x->b);
		cc_instrument_returns(c, fn, x->c);
		break;
	case N_WHILE: case N_DO: case N_SWITCH:
		cc_instrument_returns(c, fn, x->b);
		break;
	case N_FOR:
		cc_instrument_returns(c, fn, c->lists[x->c + 1]);
		break;
	case N_CASE: case N_DEFAULT: case N_LABEL:
		cc_instrument_returns(c, fn, x->c);
		break;
	case N_RETURN: {
		uint32_t ex = cc_instrument_call_new(c, "__cyg_profile_func_exit", fn, x->loc), e = x->a;

		if (!e || cc_type_is_void(c, AST_NODE(e)->ty)) {
			AST_NODE(st)->a = e ? cc_node_new_operands(c, N_COMMA, cc_type_basic(TY_VOID), x->loc, e, ex) : ex;
		} else {
			uint32_t pre, t;

			/* return (tmp = e, exit(), tmp) */
			t = cc_lowering_once(c, e, &pre);
			if (!pre) {
				uint32_t s = cc_symbol_new(c, S_VAR, 0, QUNQ(AST_NODE(e)->ty), x->loc);

				c->syms[s].sc = SC_AUTO;
				t = cc_node_new(c, N_VAR, QUNQ(AST_NODE(e)->ty), x->loc);
				AST_NODE(t)->a = s;
				AST_NODE(t)->flags = NF_LVALUE;
				pre = cc_node_new_operands(c, N_ASSIGN, QUNQ(AST_NODE(e)->ty), x->loc, t, e);
			}
			AST_NODE(st)->a = cc_node_new_operands(c, N_COMMA, AST_NODE(t)->ty, x->loc, pre, cc_node_new_operands(c, N_COMMA, AST_NODE(t)->ty, x->loc, ex, cc_node_rvalue(c, t)));
		}
		break;
	}
	default:
		break;
	}
}

CC_PRIVATE uint32_t cc_instrument_functions(cc *c, uint32_t fn, uint32_t body)
{
	cc_id_list items;
	uint32_t loc = AST_NODE(body)->loc;

	cc_instrument_returns(c, fn, body);
	cc_id_list_init(&items);
	cc_id_list_append(c, &items, cc_statement_new(c, N_EXPR, loc, cc_instrument_call_new(c, "__cyg_profile_func_enter", fn, loc), 0, 0));
	for (uint32_t i = 0; i < AST_NODE(body)->c; i++)
		cc_id_list_append(c, &items, c->lists[AST_NODE(body)->b + i]);
	cc_id_list_append(c, &items, cc_statement_new(c, N_EXPR, loc, cc_instrument_call_new(c, "__cyg_profile_func_exit", fn, loc), 0, 0));
	return cc_statement_new(c, N_BLOCK, loc, 0, cc_id_list_freeze(c, &items), items.n);
}
