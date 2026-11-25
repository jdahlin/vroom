/*
 * expression.cpp: [expr.pre] Expressions: the analysis of one expression,
 * by the nonterminal the parser gave it.
 *
 * [expr.pre]/1: "An expression is a sequence of operators and operands
 * that specifies a computation."  Each nonterminal's rules are in the file
 * of its subclause (primary-expression.cpp, unary-expression.cpp,
 * binary-expression.cpp ...); here they are dispatched, and every expression
 * with a meaning is recorded for the type and constant claims
 * (-fcxx-dump=type-claims).
 */
#include "../cplusplus-internal.h"
#include "frontend/cplusplus/sema.h"

namespace cplusplus
{

/* [ext:gnu_float128]: a name of an object, or a cast (which analysis
   allows only to the type itself or void), is a value only passed on */
bool semantic_analysis::passes_binary128(SyntaxElement e,
                                         const expression_meaning &m) const
{
    SyntaxElement operand = e;
    while (operand.is(syntax_kind::primary_expression) && operand.size() == 3 &&
           operand[0].is(preprocessing_op_or_punc::LEFT_PARENTHESIS))
        operand = operand[1];
    if (operand.is(syntax_kind::cast_expression))
        return true;
    if (!m.node || m.category != value_category::lvalue)
        return false;
    const semantic_node &n = semantics[m.node];
    return (n.kind == semantic_kind::entity && n.a) ||
           (n.kind == semantic_kind::member && n.b);
}

/* [ext:gnu_float128], [ext:gnu_complex] */
bool semantic_analysis::binary128_or_complex(qualified_type t) const
{
    const struct type record = type_table[t.unqualified()];
    return record.kind == type_kind::complex ||
           (record.kind == type_kind::fundamental &&
            record.fundamental == fundamental_type::float128);
}

/* [ext:gnu_float128], [ext:gnu_complex]: the conversions of a binary128 or
   complex value are not analysed: only an identity is */
bool semantic_analysis::binary128_conversion(qualified_type from,
                                             qualified_type to) const
{
    if (from.unqualified() == to.unqualified())
        return false;
    return binary128_or_complex(from) || binary128_or_complex(to);
}

/* [ext:gnu_float128] the operand of a call: a binary128 object's name is
   a value that is only passed on */
analysis_result semantic_analysis::passed_expression(SyntaxElement e,
                                                     expression_meaning &m)
{
    binary128_value_passes = true;
    atomic_object_operand = true;
    analysis_result r = expression(e, m);
    binary128_value_passes = false;
    atomic_object_operand = false;
    return r;
}

analysis_result semantic_analysis::passed_target_expression(
    SyntaxElement e, qualified_type target, expression_meaning &m)
{
    binary128_value_passes = true;
    atomic_object_operand = true;
    analysis_result r = target_expression(e, target, m);
    binary128_value_passes = false;
    atomic_object_operand = false;
    return r;
}

analysis_result semantic_analysis::expression(SyntaxElement e,
                                              expression_meaning &m)
{
    m = expression_meaning();
    bool passes = binary128_value_passes;
    binary128_value_passes = false;
    bool atomic_object = atomic_object_operand;
    atomic_object_operand = false;
    if (e.is(syntax_kind::primary_expression,
             syntax_kind::gnu_extension_expression)) {
        binary128_value_passes = passes;
        atomic_object_operand = atomic_object;
    }
    VTRY(r, expression_kind(e, m));
    atomic_object_operand = false;
    /* [ext:clang_atomic], [ext:clang_bit_int] the operations on an atomic
       object or a _BitInt are not analysed yet */
    type_kind k = type_table[m.type.unqualified()].kind;
    bool binary128 = k == type_kind::fundamental &&
                     type_table[m.type.unqualified()].fundamental ==
                         fundamental_type::float128;
    if (binary128 && passes && passes_binary128(e, m))
        binary128 = false;
    if (binary128) {
        /* [ext:gnu_float128] binary128 arithmetic is not analysed yet */
        if (e.is_token())
            return not_supported_because(current_declaration, "ext.gnu",
                                         "a __float128 value in this use");
        return not_supported_because(e.index(), "ext.gnu",
                                     "a __float128 value in this use");
    }
    if ((k == type_kind::atomic && (m.category == value_category::prvalue ||
                                    !atomic_value_type(m.type))) ||
        k == type_kind::bit_int || k == type_kind::ext_vector) {
        if (e.is_token())
            return not_supported_because(
                current_declaration, "ext.clang",
                "a value of atomic prvalue, _BitInt or ext_vector type");
        return not_supported_because(
            e.index(), "ext.clang",
            "a value of atomic prvalue, _BitInt or ext_vector type");
    }
    /* [expr.type]/2: "If a prvalue initially has the type "cv T", where T
       is a cv-unqualified non-class, non-array type, the type of the
       expression is adjusted to T prior to any further analysis." */
    const struct type record = type_table[m.type.unqualified()];
    if (m.category == value_category::prvalue &&
        record.kind != type_kind::class_type && record.kind != type_kind::array)
        m.type = qualified_type::make(m.type.unqualified(), cv_none);
    if (m.node)
        semantics[m.node].type = m.type;
    analysed_expression x;
    x.at = e;
    x.declaration = current_declaration;
    x.meaning = m;
    x.place = claim_place;
    x.placeable = current_scope == claim_scope;
    /* in a lambda's body a claim is an unevaluated operand, where clang
       gives a name of an entity the lambda captures by copy the entity's
       type, not the capture's ([expr.prim.id.unqual]/2) */
    if (current_lambda())
        x.placeable = false;
    /* a name in its own declaration's initializer: [basic.scope.pdecl]/1
       "The point of declaration for a name is immediately after its
       complete declarator ([dcl.decl]) and before its initializer (if
       any)", but the initializer can complete the type (an array's bound),
       so after the declaration the name may have another type */
    if (e.is_identifier()) {
        const struct type &named = type_table[m.type.unqualified()];
        if (named.kind == type_kind::array && !named.bound_known)
            x.placeable = false;
    }
    expressions.append(x);
    /* [ext:clang_atomic] an atomic object (the expression's type, as
       clang's) is read where it is not the operand of &, sizeof or an
       assignment's left operand (C23 6.3.2.1/2: an lvalue of atomic type
       converts to the value of its type's unqualified, non-atomic
       version), with one atomic load; the lvalue an assignment or a prefix
       ++ or -- results in ([expr.ass]/1) is read only where its value is
       used (operand_value), not where it is discarded */
    if (k == type_kind::atomic && !atomic_object && m.node &&
        semantics[m.node].kind != semantic_kind::assignment &&
        semantics[m.node].kind != semantic_kind::increment) {
        SyntaxRef at = current_declaration;
        if (!e.is_token())
            at = e.index();
        VTRY_SET(r, operand_value(at, m));
    }
    return analysis_result::well_formed;
}

/*
 * [expr.pre]/1: "[expr] defines the syntax, order of evaluation, and
 * meaning of expressions": one function per production of the grammar,
 * [expr.prim] (literals, this, id-expressions, parentheses) to
 * [expr.comma], chosen by the node's kind.
 */
analysis_result semantic_analysis::expression_kind(SyntaxElement e,
                                                   expression_meaning &m)
{
    if (e.is_token()) {
        const token &t = tokens[e.token()];
        /* true, false and nullptr are keywords that are literals */
        if (t.literal == literal_kind::user_defined_literal)
            return user_defined_literal(e.token(), m);
        if (t.literal != literal_kind::none) {
            /* the value of a literal analysis does not evaluate (a long
               double's) is the token's */
            analysis_result r = literal(e.token(), m);
            semantic_kind kind = semantic_kind::constant;
            if (t.literal == literal_kind::string_literal)
                kind = semantic_kind::string_literal;
            if (r == analysis_result::well_formed)
                m.node = expression_node(kind, m, e.token());
            return r;
        }
        if (t.kind == token_kind::identifier)
            return id_expression(e.token(), m);
        if (t.kind == token_kind::keyword &&
            syntax_parser.keyword_of(t) == standard_keyword::this_)
            return this_pointer(e.token(), m);
        if (t.kind == token_kind::keyword &&
            syntax_parser.extension_keyword_of(t) ==
                extension_keyword::predefined_name)
            return predefined_name(e.token(), m);
        return not_supported_because(
            current_declaration, "expr.prim",
            "a primary expression starting with an unhandled token");
    }
    switch (e.kind()) {
    case syntax_kind::gnu_extension_expression:
        /* [ext:gnu_extension_keyword]: __extension__ changes no meaning */
        return expression(e[1], m);
    case syntax_kind::primary_expression:
        return parenthesized(e, m);
    case syntax_kind::qualified_id:
        return qualified_id(e, m);
    case syntax_kind::unary_expression:
        return unary(e, m);
    case syntax_kind::postfix_expression:
        return postfix(e, m);
    case syntax_kind::cast_expression:
        return cast(e, m);
    case syntax_kind::pm_expression:
    case syntax_kind::compare_expression:
    case syntax_kind::multiplicative_expression:
    case syntax_kind::additive_expression:
    case syntax_kind::shift_expression:
    case syntax_kind::relational_expression:
    case syntax_kind::equality_expression:
    case syntax_kind::and_expression:
    case syntax_kind::exclusive_or_expression:
    case syntax_kind::inclusive_or_expression:
    case syntax_kind::logical_and_expression:
    case syntax_kind::logical_or_expression:
        return binary(e, m);
    case syntax_kind::conditional_expression:
        return conditional(e, m);
    case syntax_kind::assignment_expression:
        return assignment(e, m);
    case syntax_kind::expression:
        return comma(e, m);
    case syntax_kind::new_expression:
        return new_expression(e, m);
    case syntax_kind::delete_expression:
        return delete_expression(e, m);
    case syntax_kind::throw_expression:
        return throw_expression(e, m);
    case syntax_kind::fold_expression:
        return fold(e, m);
    case syntax_kind::lambda_expression:
        return lambda_expression(e, m);
    case syntax_kind::noexcept_expression:
        return noexcept_operator(e, m);
    case syntax_kind::simple_template_id:
        return variable_template_id(e, m);
    case syntax_kind::requires_expression:
        return requires_expression(e, m);
    case syntax_kind::clang_type_trait_expression:
        return clang_type_trait(e, m);
    case syntax_kind::gnu_offsetof_expression:
        return gnu_offsetof(e, m);
    case syntax_kind::gnu_label_address:
        return gnu_label_address(e, m);
    case syntax_kind::gnu_va_arg_expression:
        return gnu_va_arg(e, m);
    case syntax_kind::gnu_compound_literal:
        return gnu_compound_literal(e, m);
    case syntax_kind::clang_bit_cast_expression:
        return clang_bit_cast(e, m);
    case syntax_kind::clang_convertvector_expression:
        return clang_convertvector(e, m);
    case syntax_kind::clang_source_location_expression:
        return clang_source_location(e, m);
    case syntax_kind::operator_function_id: {
        declarator_meaning function_name;
        VTRY(r, operator_function_id(e.operator_function_id(), function_name));
        return id_expression(function_name.id, m);
    }
    default:
        return not_supported_because(
            e, syntax_kind_subclause(e.kind()),
            "an expression of an unhandled syntactic kind");
    }
}

} // namespace cplusplus
