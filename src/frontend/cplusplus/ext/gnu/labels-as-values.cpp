/*
 * labels-as-values.cpp: [ext:gnu_labels_as_values] (GCC manual 6.3,
 * Labels as Values): "You can get the address of a label defined in the
 * current function (or a containing function) with the unary operator
 * '&&'.  The value has type void *."  "To use these values, you need to be
 * able to jump to one.  This is done with the computed goto statement":
 *
 *     unary-expression:
 *         gnu-label-address
 *
 *     gnu-label-address:
 *         && identifier
 *
 *     jump-statement:
 *         gnu-computed-goto
 *
 *     gnu-computed-goto:
 *         goto * expression ;
 *
 * && followed by an identifier is otherwise no expression's beginning.
 */
#include "../../cplusplus-internal.h"

namespace cplusplus
{

node_index parser::gnu_label_address()
{
    size_t mark = begin_node();
    token_index first = position;
    add_token(); /* && */
    add_token(); /* identifier */
    return finish_node(syntax_kind::gnu_label_address, mark, first);
}

/* after goto, at the '*' */
node_index parser::gnu_computed_goto(size_t mark, token_index first)
{
    add_token(); /* * */
    expression();
    if (failed())
        return 0;
    if (!accept(preprocessing_op_or_punc::SEMICOLON))
        return expected("';' after the computed goto", "ext.gnu");
    return finish_node(syntax_kind::gnu_computed_goto, mark, first);
}

/* [ext:gnu_labels_as_values] &&label: a prvalue of type void*.  The label
   is looked up when the function is complete (it may be defined later:
   check_indirect_gotos) */
analysis_result semantic_analysis::gnu_label_address(SyntaxRef n,
                                                     expression_meaning &m)
{
    if (!function_state.active || n.size() != 2 || !n[1].is_identifier())
        return not_supported(n, "ext.gnu");
    goto_record g;
    g.name = tokens.identifier(n[1].token());
    g.point.scope = current_scope;
    g.point.at = n[0].token();
    g.node = n;
    function_state.label_addresses.append(g);
    qualified_type pointee = qualified_type::make(
        type_table.fundamental(fundamental_type::void_), cv_none);
    m = expression_meaning();
    m.type = qualified_type::make(type_table.pointer_to(pointee), cv_none);
    m.category = value_category::prvalue;
    m.evaluation = constness::not_constant;
    m.node = expression_node(semantic_kind::label_address, m, n[0].token());
    semantics[m.node].value = g.name;
    return analysis_result::well_formed;
}

/* [ext:gnu_labels_as_values] goto * expression ;: the operand is
   implicitly converted to const void*, as clang does (an integer is an
   error) */
analysis_result semantic_analysis::gnu_computed_goto(SyntaxRef n,
                                                     uint32_t &built)
{
    if (!function_state.active || n.size() != 4)
        return not_supported(n, "ext.gnu");
    expression_meaning m;
    VTRY(r, expression(n[2], m));
    VTRY_SET(r, operand_value(n, m));
    qualified_type pointee = qualified_type::make(
        type_table.fundamental(fundamental_type::void_), cv_const);
    qualified_type target =
        qualified_type::make(type_table.pointer_to(pointee), cv_none);
    VTRY_SET(r, implicit_conversion(n, m, target, false));
    goto_record g;
    g.point.scope = current_scope;
    g.point.at = n[0].token();
    g.node = n;
    function_state.indirect_gotos.append(g);
    built = statement_node(semantic_kind::indirect_goto_statement, n[0].token(),
                           m.node);
    return r;
}

/* [ext:gnu_labels_as_values] at the end of a function: each &&label names a
   label of the function, and a computed goto may reach any of them, so
   none may be in the scope of an initialized variable the goto is not in
   (clang's "cannot jump from this indirect goto statement to one of its
   possible targets": [stmt.dcl]/3).  A computed goto with no label address
   taken has no target (clang's err_indirect_goto_without_addrlabel) */
analysis_result semantic_analysis::check_indirect_gotos()
{
    analysis_result result = analysis_result::well_formed;
    for (const goto_record &a : function_state.label_addresses) {
        bool found = false;
        for (const label_record &l : function_state.labels)
            if (l.name == a.name)
                found = true;
        if (!found)
            result = worse(result, ill_formed(a.node,
                                              "the identifier shall be a label "
                                              "located in the current function",
                                              "stmt.goto"));
    }
    if (function_state.indirect_gotos.empty())
        return result;
    if (function_state.label_addresses.empty())
        return worse(result, ill_formed(function_state.indirect_gotos[0].node,
                                        "indirect goto in a function with no "
                                        "address-of-label expressions",
                                        "ext.gnu"));
    for (const goto_record &g : function_state.indirect_gotos)
        for (const goto_record &a : function_state.label_addresses) {
            const label_record *target = nullptr;
            for (const label_record &l : function_state.labels)
                if (l.name == a.name && !target)
                    target = &l;
            if (!target)
                continue;
            if (!target->analysed) {
                result = worse(result, not_supported(g.node, "stmt.dcl"));
                continue;
            }
            bool exits = false;
            for (const jump_point &v : function_state.destroyed)
                if (in_scope(v, g.point) && !in_scope(v, target->point))
                    exits = true;
            if (exits) {
                result = worse(
                    result,
                    ill_formed(g.node,
                               "a computed goto shall not leave the scope of "
                               "a variable with a non-trivial destructor",
                               "stmt.dcl"));
                break;
            }
            if (jump_bypasses(g.point, target->point)) {
                /* Microsoft's modes make the jump past a trivially
                   initialized variable a warning, which analysis does
                   not tell apart */
                const implementation_defined_behavior &b =
                    tokens.files.behavior;
                if (b.microsoft_extensions || b.microsoft_compatibility) {
                    result = worse(result, not_supported(g.node, "stmt.dcl"));
                    break;
                }
                result = worse(
                    result,
                    ill_formed(g.node,
                               "it is possible to transfer into a block, but "
                               "not in a way that bypasses declarations with "
                               "initialization",
                               "stmt.dcl"));
                break;
            }
        }
    return result;
}

} // namespace cplusplus
