#ifndef CXX_EXPRESSION_H
#define CXX_EXPRESSION_H
/*
 * expression.c: C++ expressions (cxx-internal.h), by precedence climbing.
 *
 * Inside template arguments (c->in_template_args) a > or >> ends the
 * argument instead of being an operator.  A name followed by < starts
 * template arguments only if lookup says it is a template, or (C++20) if
 * lookup finds nothing and the arguments parse and are followed by (.
 */
#include <ctype.h>

#include "../cxx-internal.h"

static void cxx_cast_expression_parse(cxx *c);
static void cxx_unary_expression_parse(cxx *c);

/* the functions of expression/, by file */

/* name.c */
VPRIV bool cxx_lookahead_fn(cxx *c, void (*fn)(cxx *));
VPRIV void cxx_template_arguments_then_call(cxx *c);
VPRIV cxx_entity *cxx_entity_kind_find(cxx_entity *e, int kind);
VPRIV bool cxx_entity_has_template(cxx_entity *e);
VPRIV void cxx_unqualified_id_rest_parse(cxx *c);
VPRIV bool cxx_after_member_access(cxx *c);
VPRIV void cxx_name_type(cxx *c, cxx_entity *e);
VPRIV void cxx_id_expression_parse(cxx *c);

/* cast.c */
VPRIV bool cxx_starts_type(cxx *c);
VPRIV void cxx_type_id_then_close(cxx *c);
VPRIV bool cxx_paren_type_id_follows(cxx *c);
VPRIV void cxx_type_then_init(cxx *c);
VPRIV bool cxx_const_widens(cxx *c, qty from, qty to, int64_t v);
VPRIV void cxx_functional_cast_type(cxx *c, qty cast_to);

/* argument.c */
VPRIV void cxx_call_arguments_parse(cxx *c);
VPRIV void cxx_braced_arguments_parse(cxx *c);
VPRIV void cxx_arguments_parse(cxx *c, int open, int close);
VPRIV void cxx_builtin_call_parse(cxx *c);
VPRIV uint32_t cxx_closure_name(cxx *c, uint32_t serial);

/* lambda.c */
VPRIV bool cxx_try_block_parse(cxx *c, uint32_t *closure_node, cxx_entity **call_out);
VPRIV void cxx_lambda_parse(cxx *c);

/* requires.c */
VPRIV void cxx_requires_expression_parse(cxx *c);

/* primary.c */
VPRIV bool cxx_is_fold_operator(int k);
VPRIV void cxx_paren_expression_parse(cxx *c);
VPRIV void cxx_builtin_invoke_parse(cxx *c);
VPRIV void cxx_primary_expression_parse(cxx *c);

/* postfix.c */
VPRIV void cxx_postfix_expression_parse(cxx *c);
VPRIV void cxx_postfix_expression_parse1(cxx *c);

/* unary.c */
VPRIV void cxx_new_expression_parse(cxx *c);
VPRIV void cxx_unary_expression_parse(cxx *c);
VPRIV bool cxx_cast_operand_follows(cxx *c);
VPRIV void cxx_cast_expression_parse(cxx *c);

/* binary.c */
VPRIV int cxx_binary_precedence(cxx *c, uint32_t *ntok);
VPRIV void cxx_binary_expression_parse(cxx *c, int min);
VPRIV void cxx_conditional_expression_parse(cxx *c);
VPRIV bool cxx_is_assignment_operator(cxx *c, uint32_t *ntok);
VPRIV void cxx_assignment_expression_parse(cxx *c);
VPRIV void cxx_expression_parse(cxx *c);
VPRIV void cxx_constant_expression_parse(cxx *c);

#endif
