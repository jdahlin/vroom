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
 * operator function shall have a declared return type bool."  The
 * friend operator== of a class without base classes is analysed.
 */
bool semantic_analysis::defaulted_equality_declared(entity_index c,
                                                    uint32_t name,
                                                    qualified_type type)
{
    const struct type &f = type_table[type.unqualified()];
    if ((name != operator_name(preprocessing_op_or_punc::EQUAL_EQUAL) &&
         name != operator_name(preprocessing_op_or_punc::EXCLAMATION_EQUAL)) ||
        f.kind != type_kind::function || f.variadic || f.parameter_count != 2 ||
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
 * [class.eq]/3: "comparing corresponding elements x_i and y_i in the
 * expanded lists of subobjects": an array member's elements are the
 * subobjects ([class.compare.default]/6), compared in index order; every
 * other subobject by ==.  The comparisons are joined by && into all
 * ([expr.log.and]); deleted set when one is ill-formed ([class.eq]/2).
 */
analysis_result semantic_analysis::compare_subobjects(
    token_index at, qualified_type type, expression_meaning x,
    expression_meaning y, expression_meaning &all, bool &any, bool &deleted)
{
    const struct type &t = type_table[type.unqualified()];
    if (t.kind == type_kind::array) {
        if (!t.bound_known || t.variable_bound || t.bound > 64)
            return not_supported(current_declaration, "class.compare.default");
        for (uint64_t i = 0; i < t.bound; i++) {
            expression_meaning ex = x, ey = y;
            VTRY(r, array_element(current_declaration, at, ex, i));
            VTRY(r2, array_element(current_declaration, at, ey, i));
            VTRY(r3,
                 compare_subobjects(at, t.operand, ex, ey, all, any, deleted));
            if (deleted)
                return analysis_result::well_formed;
        }
        return analysis_result::well_formed;
    }
    expression_meaning equal;
    analysis_result compared = binary_of(
        current_declaration, at, preprocessing_op_or_punc::EQUAL_EQUAL,
        SyntaxElement(), x, SyntaxElement(), y, equal);
    if (compared == analysis_result::ill_formed) {
        deleted = true;
        return analysis_result::well_formed;
    }
    VTRY(r, compared);
    /* a bool prvalue each ([class.eq]/2) */
    if (any) {
        if (all.evaluation == constness::not_constant)
            equal.evaluation = constness::not_constant;
        equal.node = expression_node(semantic_kind::logical_and, equal, at,
                                     all.node, equal.node);
    }
    all = equal;
    any = true;
    return analysis_result::well_formed;
}

/*
 * [class.compare.secondary]/1: "A secondary comparison operator @ for a
 * class C ... a defaulted != operator function is defined as deleted if
 * the == is not usable, otherwise x != y is !(x == y) ([class.compare.
 * secondary]/2)": the body returns !(x == y) on the whole objects, as a
 * bool prvalue; constexpr when that is potentially constant and the
 * parameters are literal ([class.compare.default]/3).
 */
analysis_result
semantic_analysis::defaulted_inequality(token_index at,
                                        const owned_array<uint32_t> &parameters,
                                        bool of_class, uint32_t &block)
{
    if (parameters.size() != 2u)
        return not_supported(current_declaration, "class.compare.secondary");
    expression_meaning sides[2], equal;
    size_t issued = diagnostics.messages.size();
    for (int k = 0; k < 2; k++) {
        analysis_result r = analysis_result::well_formed;
        if (of_class && k == 0)
            r = this_object(at, sides[k]);
        else
            r = named_entity(at, parameters[k], sides[k], false);
        VTRY(r2, r);
    }
    analysis_result compared = binary_of(
        current_declaration, at, preprocessing_op_or_punc::EQUAL_EQUAL,
        SyntaxElement(), sides[0], SyntaxElement(), sides[1], equal);
    if (compared == analysis_result::ill_formed) {
        diagnostics.discard_since(issued);
        entity_table[last_declared].deleted = true;
        owned_array<uint32_t> none;
        block = block_statement(at, none);
        return analysis_result::well_formed;
    }
    VTRY(r3, compared);
    /* [expr.unary.op]/9: the contextually converted operand negated */
    VTRY(r4, contextual_bool(current_declaration, equal));
    equal.value = !equal.value;
    equal.node = expression_node(
        semantic_kind::unary, equal, at, equal.node, 0,
        static_cast<uint8_t>(preprocessing_op_or_punc::EXCLAMATION));
    bool literal = true;
    for (size_t k = 0; k < parameters.size(); k++)
        if (!(of_class && k == 0) &&
            literal_type(entity_table[parameters[k]].type) !=
                constness::constant)
            literal = false;
    if (literal && potential_constant(equal.node) != constness::not_constant)
        entity_table[last_declared].constexpr_ = true;
    owned_array<uint32_t> statements;
    statements.append(
        statement_node(semantic_kind::return_statement, at, equal.node));
    block = block_statement(at, statements);
    return analysis_result::well_formed;
}

/*
 * [class.eq]/3: "The return value V of a defaulted == operator function
 * with parameters x and y is determined by comparing corresponding
 * elements x_i and y_i in the expanded lists of subobjects for x and y
 * (in increasing index order) until the first index i where x_i == y_i
 * yields a result value which, when contextually converted to bool,
 * yields false.  If no such index exists, V is true.  Otherwise, V is
 * false."  The body: return x.m0 == y.m0 && x.m1 == y.m1 ...; for the
 * class's bases and non-static data members.
 */
analysis_result semantic_analysis::defaulted_equality(
    token_index at, const owned_array<uint32_t> &parameters, uint32_t &block)
{
    const entity &x = entity_table[defaulted_comparison];
    entity_table[last_declared].inline_ = true;
    /* a member's first parameter entity is its object */
    bool of_class =
        entity_table[last_declared].kind == entity_kind::function &&
        entity_table[last_declared].enclosing == defaulted_comparison;
    if (!x.member_count || parameters.size() != 2u)
        return not_supported(current_declaration, "class.compare.default");
    if (entity_table[last_declared].name ==
        operator_name(preprocessing_op_or_punc::EXCLAMATION_EQUAL))
        return defaulted_inequality(at, parameters, of_class, block);
    /* [class.eq]/3: the expanded list of subobjects: the direct base
       classes in order, then the members ([class.compare.default]/6) */
    if (x.virtual_base_count)
        return not_supported(current_declaration, "class.compare.default");
    expression_meaning all;
    bool any = false, deleted = false;
    for (uint32_t i = 0; i < x.base_count + x.member_count; i++) {
        entity_index member = 0, base = 0;
        qualified_type type;
        if (i < x.base_count) {
            base = type_table[entity_table[class_bases[x.first_base + i]]
                                  .type.unqualified()]
                       .entity;
            type = entity_table[base].type;
        } else {
            member = class_members[x.first_member + i - x.base_count];
            /* an unnamed bit-field is no subobject compared */
            if (!entity_table[member].name && entity_table[member].bit_field)
                continue;
            type = entity_table[member].type;
        }
        expression_meaning sides[2];
        size_t issued = diagnostics.messages.size();
        for (int k = 0; k < 2; k++) {
            analysis_result r = analysis_result::well_formed;
            if (of_class && k == 0)
                r = this_object(at, sides[k]);
            else
                r = named_entity(at, parameters[k], sides[k], false);
            if (r == analysis_result::well_formed && base)
                r = to_base(at, base, sides[k]);
            else if (r == analysis_result::well_formed)
                r = member_access(at, member, sides[k]);
            VTRY(r2, r);
        }
        analysis_result r =
            compare_subobjects(at, type, sides[0], sides[1], all, any, deleted);
        if (deleted) {
            /* [class.eq]/2: "A defaulted == operator function for class C
               is defined as deleted if ... overload resolution as applied
               to find the corresponding == operator ... results in an
               ambiguity or a function that is deleted or inaccessible":
               not an error where it is declared */
            diagnostics.discard_since(issued);
            entity_table[last_declared].deleted = true;
            owned_array<uint32_t> none;
            block = block_statement(at, none);
            return analysis_result::well_formed;
        }
        VTRY(r3, r);
    }
    if (!any)
        return not_supported(current_declaration, "class.compare.default");
    /* [class.compare.default]/3: "A defaulted comparison operator function
       is constexpr-compatible if it satisfies the requirements for a
       constexpr function": its parameters are of literal types and each
       comparison it makes may be a constant expression; it is then
       implicitly constexpr ([dcl.fct.def.default]/3) */
    bool literal = true;
    for (size_t k = 0; k < parameters.size(); k++)
        if (!(of_class && k == 0) &&
            literal_type(entity_table[parameters[k]].type) !=
                constness::constant)
            literal = false;
    if (literal && potential_constant(all.node) != constness::not_constant)
        entity_table[last_declared].constexpr_ = true;
    owned_array<uint32_t> statements;
    statements.append(
        statement_node(semantic_kind::return_statement, at, all.node));
    block = block_statement(at, statements);
    return analysis_result::well_formed;
}

} // namespace cplusplus

namespace cplusplus
{

/*
 * [class.compare.default]/1: a member function of C with one parameter of
 * type const C& and the cv-qualifier const, declared in the class with
 * = default: the declarator of member_declaration n of class c, whether it
 * is such an operator==, the only defaulted comparison analysed as a
 * member.  [class.eq]/1: its declared return type is bool.
 */
bool semantic_analysis::defaulted_member_equality(entity_index c,
                                                  SyntaxElement d,
                                                  const specified &s,
                                                  uint8_t access)
{
    if (access || !s.has_type || s.typedef_ || s.static_ || s.extern_ ||
        s.virtual_ || s.friend_ || s.explicit_ || s.declares_class || s.aligned)
        return false;
    qualified_type type = s.type;
    declarator_meaning m;
    function_cv = 0;
    in_member_declarator = true;
    function_parameters.clear();
    analysis_result r = declarator(current_declaration, d, type, m);
    in_member_declarator = false;
    if (r != analysis_result::well_formed || !m.has_id || m.qualifier ||
        !m.operator_function || m.aligned ||
        (m.implemented != preprocessing_op_or_punc::EQUAL_EQUAL &&
         m.implemented != preprocessing_op_or_punc::EXCLAMATION_EQUAL) ||
        function_cv != cv_const)
        return false;
    for (const parameter_meaning &parameter : function_parameters)
        if (parameter.default_argument)
            return false;
    const struct type &f = type_table[type.unqualified()];
    if (f.kind != type_kind::function || f.variadic || f.parameter_count != 1 ||
        fundamental_of(f.operand) != fundamental_type::bool_ || f.operand.cv())
        return false;
    const struct type &first =
        type_table[type_table.parameters(type.unqualified())[0].unqualified()];
    return first.kind == type_kind::lvalue_reference &&
           first.operand == qualified_type::make(
                                entity_table[c].type.unqualified(), cv_const);
}

} // namespace cplusplus
