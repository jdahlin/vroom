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
    bool three_way = name == operator_name(preprocessing_op_or_punc::SPACESHIP);
    if ((name != operator_name(preprocessing_op_or_punc::EQUAL_EQUAL) &&
         name != operator_name(preprocessing_op_or_punc::EXCLAMATION_EQUAL) &&
         !three_way) ||
        f.kind != type_kind::function || f.variadic || f.parameter_count != 2)
        return false;
    if (three_way && !comparison_category(f.operand) &&
        !(f.operand == placeholder_type(false)))
        return false;
    if (!three_way && fundamental_of(f.operand) != fundamental_type::bool_)
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
    expression_meaning y, expression_meaning &all, bool &any, bool &deleted,
    owned_array<uint32_t> *statements, qualified_type category, bool &constant)
{
    const struct type &t = type_table[type.unqualified()];
    if (t.kind == type_kind::array) {
        if (!t.bound_known || t.variable_bound || t.bound > 64)
            return not_supported_because(
                current_declaration, "class.compare.default",
                "a defaulted comparison of an array member of unknown or "
                "variable bound or more than 64 elements");
        for (uint64_t i = 0; i < t.bound; i++) {
            expression_meaning ex = x, ey = y;
            VTRY(r, array_element(current_declaration, at, ex, i));
            VTRY(r2, array_element(current_declaration, at, ey, i));
            VTRY(r3,
                 compare_subobjects(at, t.operand, ex, ey, all, any, deleted,
                                    statements, category, constant));
            if (deleted)
                return analysis_result::well_formed;
        }
        return analysis_result::well_formed;
    }
    if (statements)
        return three_way_step(at, x, y, deleted, *statements, category,
                              constant);
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
 * [dcl.fct.def.default]/3: "An explicitly-defaulted function that is not
 * defined as deleted may be declared constexpr or consteval only if it is
 * constexpr-compatible ([class.compare.default])", before C++23's
 * P2448R2; the function is implicitly constexpr when it is
 * ([class.compare.default]/3).
 */
analysis_result semantic_analysis::constexpr_compatible(SyntaxRef n,
                                                        bool compatible)
{
    entity_table[last_declared].constexpr_ = compatible;
    if (compatible || !declared_constexpr ||
        tokens.files.behavior.has(
            language_feature::constexpr_without_constant_result))
        return analysis_result::well_formed;
    entity_table[last_declared].constexpr_ = true;
    return ill_formed(n,
                      "an explicitly-defaulted function that is not "
                      "constexpr-compatible shall not be declared constexpr",
                      "dcl.fct.def.default");
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
        return not_supported_because(
            current_declaration, "class.compare.secondary",
            "a defaulted != without exactly two parameters");
    expression_meaning sides[2], equal;
    size_t issued = diagnostics.messages.size();
    for (int k = 0; k < 2; k++) {
        analysis_result r = analysis_result::well_formed;
        if (of_class && k == 0)
            r = this_object(at, sides[k]);
        else
            r = named_entity(at, parameters[static_cast<size_t>(k)], sides[k],
                             false);
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
    VTRY(c2, constexpr_compatible(current_declaration,
                                  literal && potential_constant(equal.node) !=
                                                 constness::not_constant));
    owned_array<uint32_t> statements;
    statements.append(
        statement_node(semantic_kind::return_statement, at, equal.node));
    block = block_statement(at, statements);
    return analysis_result::well_formed;
}

/*
 * [class.spaceship]/2: "Let R be the declared return type.  The operator
 * function is defined as deleted if ... overload resolution ... x_i <=>
 * y_i ... cannot be converted to R.  Otherwise the return value is the
 * first x_i <=> y_i, converted to R, that is not equal to 0"; the
 * statements `R c = x_i <=> y_i; if (c != 0) return c;` for one
 * subobject.
 */
analysis_result semantic_analysis::three_way_step(
    token_index at, expression_meaning x, expression_meaning y, bool &deleted,
    owned_array<uint32_t> &statements, qualified_type category, bool &constant)
{
    expression_meaning compared;
    size_t issued = diagnostics.messages.size();
    analysis_result r =
        binary_of(current_declaration, at, preprocessing_op_or_punc::SPACESHIP,
                  SyntaxElement(), x, SyntaxElement(), y, compared);
    if (r == analysis_result::well_formed &&
        compared.type.unqualified() != category.unqualified())
        r = implicit_conversion(current_declaration, compared, category, false);
    if (r == analysis_result::ill_formed) {
        /* no usable <=> or a result not convertible to R: deleted
           ([class.spaceship]/2), but a subobject with only < and == is
           compared by the synthesized three-way comparison ([class.spaceship]/3,
           [expr.spaceship]): not analysed */
        diagnostics.discard_since(issued);
        return not_supported_because(
            current_declaration, "class.spaceship",
            "a defaulted <=> where a subobject's <=> is unusable or does not "
            "convert to the result type");
    }
    VTRY(r1, r);
    if (potential_constant(compared.node) == constness::not_constant)
        constant = false;
    /* the variable c of type R, initialized by the comparison */
    entity_index v = entity_table.make(entity_kind::variable, 0, current_scope);
    entity_table[v].defined = true;
    entity_table[v].automatic = true;
    entity_table[v].type = category;
    initialized_value initial;
    VTRY(r2, initialize_with(current_declaration, compared, category, false,
                             false, initial));
    entity_table[v].definition = initial.node;
    uint32_t declaration = statement_node(semantic_kind::declaration_statement,
                                          at, v, initial.node);
    expression_meaning variable;
    VTRY(r2b, named_entity(at, v, variable, false));
    expression_meaning zero = integer_constant(fundamental_type::int_, 0, at);
    zero.zero_literal = true;
    expression_meaning unequal, returned = variable;
    VTRY(r3,
         binary_of(current_declaration, at,
                   preprocessing_op_or_punc::EXCLAMATION_EQUAL, SyntaxElement(),
                   variable, SyntaxElement(), zero, unequal));
    initialized_value result;
    VTRY(r4, initialize_with(current_declaration, returned, category, false,
                             false, result));
    statements.append(declaration);
    statements.append(statement_node(
        semantic_kind::if_statement, at, unequal.node,
        statement_node(semantic_kind::return_statement, at, result.node)));
    return analysis_result::well_formed;
}

/*
 * [class.spaceship]/2: "Otherwise, the return value is static_cast<R>(0
 * <=> 0)", the category's `equal` (`equivalent` for the weak and partial
 * orderings); the body is the statements the subobjects made, then that
 * return.  Implicitly constexpr as ==.
 */
analysis_result semantic_analysis::defaulted_three_way_end(
    token_index at, const owned_array<uint32_t> &parameters, bool of_class,
    qualified_type category, owned_array<uint32_t> &statements, bool constant,
    bool equality_compatible, uint32_t &block)
{
    entity_index c = type_table[category.unqualified()].entity;
    const char *name = "equivalent";
    if (comparison_category(category) == 1)
        name = "equal";
    bool ambiguous = false;
    entity_index e = class_member_lookup(
        c, tokens.pp.identifier(name, static_cast<uint32_t>(strlen(name))) + 1,
        ambiguous);
    if (!e || ambiguous)
        return not_supported_because(
            current_declaration, "class.spaceship",
            "a comparison category type without the expected result member");
    expression_meaning value;
    VTRY(r, named_entity(at, e, value, false));
    initialized_value result;
    VTRY(r2, initialize_with(current_declaration, value, category, false, false,
                             result));
    statements.append(
        statement_node(semantic_kind::return_statement, at, result.node));
    bool literal = constant;
    for (size_t k = 0; k < parameters.size(); k++)
        if (!(of_class && k == 0) &&
            literal_type(entity_table[parameters[k]].type) !=
                constness::constant)
            literal = false;
    VTRY(c3, constexpr_compatible(current_declaration,
                                  literal && literal_type(category) ==
                                                 constness::constant));
    /* [class.compare.default]/5: the == declared with it is declared
       constexpr as it is, and an explicitly-defaulted function that is not
       constexpr-compatible shall not be ([dcl.fct.def.default]/3) */
    if (declared_constexpr && !(literal && equality_compatible) &&
        !tokens.files.behavior.has(
            language_feature::constexpr_without_constant_result))
        return ill_formed(current_declaration,
                          "the implicitly declared == of a defaulted <=> is "
                          "defaulted, and constexpr only if it is "
                          "constexpr-compatible",
                          "class.compare.default");
    block = block_statement(at, statements);
    return analysis_result::well_formed;
}

bool semantic_analysis::calls_non_constexpr(uint32_t node) const
{
    const semantic_node &x = semantics[node];
    switch (x.kind) {
    case semantic_kind::logical_and:
        return calls_non_constexpr(x.a) || calls_non_constexpr(x.b);
    case semantic_kind::conversion:
        return calls_non_constexpr(x.a);
    case semantic_kind::call: {
        uint32_t callee = x.a;
        if (semantics[callee].kind == semantic_kind::conversion)
            callee = semantics[callee].a;
        if (semantics[callee].kind != semantic_kind::entity)
            return false;
        const entity &f = entity_table[semantics[callee].a];
        return f.kind == entity_kind::function && !f.constexpr_ &&
               !f.instantiation;
    }
    default:
        return false;
    }
}

/*
 * [cmp.categories]: 1 when t is std::strong_ordering, 2 for
 * std::weak_ordering and std::partial_ordering, else 0.
 */
int semantic_analysis::comparison_category(qualified_type t)
{
    const struct type &k = type_table[t.unqualified()];
    if (k.kind != type_kind::class_type)
        return 0;
    entity_index c = k.entity;
    entity_index scope = entity_table[c].enclosing;
    uint32_t std_name = tokens.pp.identifier("std", 3) + 1;
    /* in namespace std, perhaps in an inline namespace of it */
    while (scope && is_namespace(scope) && entity_table[scope].name &&
           entity_table[scope].name != std_name)
        scope = entity_table[scope].enclosing;
    if (!scope || !is_namespace(scope))
        return 0;
    uint32_t n = entity_table[c].name;
    if (n == tokens.pp.identifier("strong_ordering", 15) + 1)
        return 1;
    if (n == tokens.pp.identifier("weak_ordering", 13) + 1 ||
        n == tokens.pp.identifier("partial_ordering", 16) + 1)
        return 2;
    return 0;
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
    entity_index compared = defaulted_comparison;
    scoped_value<entity_index> clear_compared(defaulted_comparison, 0);
    const entity x = entity_table[compared];
    entity_table[last_declared].inline_ = true;
    declared_constexpr = entity_table[last_declared].constexpr_;
    /* (provisionally: a body analysis does not complete is a function not
       known to be non-constexpr) */
    entity_table[last_declared].constexpr_ = true;
    /* a member's first parameter entity is its object */
    bool of_class = entity_table[last_declared].kind == entity_kind::function &&
                    entity_table[last_declared].enclosing == compared;
    if ((!x.base_count && !x.member_count) || parameters.size() != 2u)
        return not_supported_because(
            current_declaration, "class.compare.default",
            "a defaulted comparison of a class with no members or without "
            "exactly two parameters");
    bool three_way = entity_table[last_declared].name ==
                     operator_name(preprocessing_op_or_punc::SPACESHIP);
    if (entity_table[last_declared].name ==
        operator_name(preprocessing_op_or_punc::EXCLAMATION_EQUAL))
        return defaulted_inequality(at, parameters, of_class, block);
    /* [class.eq]/3: the expanded list of subobjects: the direct base
       classes in order, then the members ([class.compare.default]/6) */
    if (x.virtual_base_count)
        return not_supported_because(
            current_declaration, "class.compare.default",
            "a defaulted comparison of a class with virtual bases");
    expression_meaning all;
    bool any = false, deleted = false;
    qualified_type category;
    owned_array<uint32_t> statements;
    bool constant = true;
    /* [class.compare.default]/5: the == a defaulted <=> declares compares
       the same subobjects by == */
    expression_meaning equal_all;
    bool equal_any = false, equal_deleted = false, equal_known = true;
    bool equality_constant = true;
    if (three_way) {
        category =
            type_table[entity_table[last_declared].type.unqualified()].operand;
        if (category == placeholder_type(false)) {
            const char *cname = "strong_ordering";
            for (uint32_t i = 0; i < x.member_count; i++) {
                entity_index mem = class_members[x.first_member + i];
                if (!entity_table[mem].name && entity_table[mem].bit_field)
                    continue;
                if (is_floating(entity_table[mem].type))
                    cname = "partial_ordering";
            }
            entity_index std_ns =
                entity_table.find(0, tokens.pp.identifier("std", 3) + 1);
            entity_index cat_e = 0;
            if (std_ns && is_namespace(std_ns))
                cat_e =
                    lookup_in_namespace(
                        std_ns, tokens.pp.identifier(cname, strlen(cname)) + 1,
                        lookup_filter::types)
                        .entity;
            if (!cat_e || entity_table[cat_e].kind != entity_kind::class_type)
                return not_supported_because(
                    current_declaration, "class.spaceship",
                    "deduced defaulted operator<=> without <compare>");
            category = qualified_type::make(
                entity_table[cat_e].type.unqualified(), cv_none);
            entity_table[last_declared].type =
                replace_placeholder(entity_table[last_declared].type, category);
            function_state.deduced = true;
            function_state.returns = category;
        }
    }
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
                r = named_entity(at, parameters[static_cast<size_t>(k)],
                                 sides[k], false);
            if (r == analysis_result::well_formed && base)
                r = to_base(at, base, sides[k]);
            else if (r == analysis_result::well_formed)
                r = member_access(at, member, sides[k]);
            VTRY(r2, r);
        }
        owned_array<uint32_t> *collected = nullptr;
        if (three_way)
            collected = &statements;
        analysis_result r =
            compare_subobjects(at, type, sides[0], sides[1], all, any, deleted,
                               collected, category, constant);
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
        if (three_way && equal_known && !equal_deleted) {
            size_t before = diagnostics.messages.size();
            bool unused = true;
            analysis_result q = compare_subobjects(
                at, type, sides[0], sides[1], equal_all, equal_any,
                equal_deleted, nullptr, category, unused);
            diagnostics.discard_since(before);
            if (q != analysis_result::well_formed)
                equal_known = false;
        }
    }
    if (three_way) {
        if (equal_known && !equal_deleted && equal_any &&
            calls_non_constexpr(equal_all.node))
            equality_constant = false;
        return defaulted_three_way_end(at, parameters, of_class, category,
                                       statements, constant, equality_constant,
                                       block);
    }
    if (!any)
        return not_supported_because(
            current_declaration, "class.compare.default",
            "a defaulted comparison with no subobject to compare");
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
    VTRY(c1, constexpr_compatible(current_declaration,
                                  literal && potential_constant(all.node) !=
                                                 constness::not_constant));
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
         m.implemented != preprocessing_op_or_punc::EXCLAMATION_EQUAL &&
         m.implemented != preprocessing_op_or_punc::SPACESHIP) ||
        function_cv != cv_const)
        return false;
    for (const parameter_meaning &parameter : function_parameters)
        if (parameter.default_argument)
            return false;
    const struct type &f = type_table[type.unqualified()];
    if (f.kind != type_kind::function || f.variadic || f.parameter_count != 1 ||
        f.operand.cv())
        return false;
    bool three_way = m.implemented == preprocessing_op_or_punc::SPACESHIP;
    if (three_way && !comparison_category(f.operand) &&
        !(f.operand == placeholder_type(false)))
        return false;
    if (!three_way && fundamental_of(f.operand) != fundamental_type::bool_)
        return false;
    const struct type &first =
        type_table[type_table.parameters(type.unqualified())[0].unqualified()];
    return first.kind == type_kind::lvalue_reference &&
           first.operand == qualified_type::make(
                                entity_table[c].type.unqualified(), cv_const);
}

} // namespace cplusplus
