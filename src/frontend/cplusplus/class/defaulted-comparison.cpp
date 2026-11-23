/*
 * defaulted-comparison.cpp: [class.compare.default] a defaulted comparison
 * operator function, declared as a friend of its class (class/friend.cpp
 * calls defaulted_equality_declared) or as a member.
 */
#include "../cplusplus-internal.h"
#include "frontend/cplusplus/sema.h"

namespace cplusplus
{

/*
 * [class.compare.default]/1: "A defaulted comparison operator function
 * ([over.binary]) for some class C shall be a non-template function that
 * is ... a friend of C having either two parameters of type const C& or
 * two parameters of type C."  [class.eq]/1: "A defaulted equality
 * operator function shall have a declared return type bool."  Only the
 * friend operator== of a class without base classes is analysed.
 */
bool semantic_analysis::defaulted_equality_declared(entity_index c,
                                                    uint32_t name,
                                                    qualified_type type)
{
    const struct type &f = type_table[type.unqualified()];
    if (name != operator_name(preprocessing_op_or_punc::EQUAL_EQUAL) ||
        f.kind != type_kind::function || f.variadic || f.parameter_count != 2 ||
        entity_table[c].base_count ||
        fundamental_of(f.operand) != fundamental_type::bool_)
        return false;
    const qualified_type *parameters =
        type_table.parameters(type.unqualified());
    qualified_type object =
        qualified_type::make(entity_table[c].type.unqualified(), cv_none);
    qualified_type reference = qualified_type::make(
        type_table.lvalue_reference_to(
            qualified_type::make(object.unqualified(), cv_const)),
        cv_none);
    return parameters[0] == parameters[1] &&
           (parameters[0] == object || parameters[0] == reference);
}

/*
 * [class.eq]/3: "The return value V of a defaulted == operator function
 * with parameters x and y is determined by comparing corresponding
 * elements x_i and y_i in the expanded lists of subobjects for x and y
 * (in increasing index order) until the first index i where x_i == y_i
 * yields a result value which, when contextually converted to bool,
 * yields false.  If no such index exists, V is true.  Otherwise, V is
 * false."  The body: return x.m0 == y.m0 && x.m1 == y.m1 ...; for the
 * class's non-static data members, at least one, none an array.
 */
analysis_result semantic_analysis::defaulted_equality(
    token_index at, const owned_array<uint32_t> &parameters, uint32_t &block)
{
    const entity &x = entity_table[defaulted_comparison];
    if (!x.member_count || parameters.size() != 2)
        return not_supported(current_declaration, "class.compare.default");
    expression_meaning all;
    for (uint32_t i = 0; i < x.member_count; i++) {
        entity_index member = class_members[x.first_member + i];
        if (type_table[entity_table[member].type.unqualified()].kind ==
            type_kind::array)
            return not_supported(current_declaration, "class.compare.default");
        expression_meaning sides[2], equal;
        for (int k = 0; k < 2; k++) {
            analysis_result r =
                named_entity(at, parameters[k], sides[k], false);
            if (r == analysis_result::well_formed)
                VTRY_SET(r, member_access(at, member, sides[k]));
        }
        VTRY(r,
             binary_of(current_declaration, at,
                       preprocessing_op_or_punc::EQUAL_EQUAL, SyntaxElement(),
                       sides[0], SyntaxElement(), sides[1], equal));
        /* a bool prvalue each ([class.eq]/2), joined by && ([expr.log.and]) */
        if (i) {
            if (all.evaluation == constness::not_constant)
                equal.evaluation = constness::not_constant;
            equal.node = expression_node(semantic_kind::logical_and, equal, at,
                                         all.node, equal.node);
        }
        all = equal;
    }
    owned_array<uint32_t> statements;
    statements.append(
        statement_node(semantic_kind::return_statement, at, all.node));
    block = block_statement(at, statements);
    return analysis_result::well_formed;
}

} // namespace cplusplus
