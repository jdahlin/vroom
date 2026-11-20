/*
 * assignable-traits.cpp: [ext:clang_type_traits] __is_assignable(T, U),
 * __is_nothrow_assignable(T, U) and __is_trivially_assignable(T, U), the
 * traits libc++'s <__type_traits/is_assignable.h> and its relatives use when
 * __has_builtin says they exist.
 *
 * [meta.unary.prop] is_assignable: "the expression declval<T>() =
 * declval<U>() is well-formed when treated as an unevaluated operand";
 * is_nothrow_assignable: "is_assignable is true and the assignment is known
 * not to throw any exceptions"; is_trivially_assignable: "is_assignable is
 * true and the assignment, as defined by is_assignable, is known to call no
 * operation that is not trivial".  The assignment is analysed as the
 * program's (assignment_of: operator functions by overload resolution, the
 * built-in assignment otherwise) and what makes it ill-formed is no error
 * but "false" ([temp.deduct]/8's way: the diagnostics are taken back).
 * What the analysis has no rule for is unsupported, not a guess.
 */
#include "../../cplusplus-internal.h"
#include "frontend/cplusplus/sema.h"

namespace cplusplus
{

namespace
{
enum class triviality { trivial, not_trivial, unknown };
}

/* [meta.unary.prop] is_trivially_assignable: whether the analysed
   expression calls no function (a conversion function, an operator
   function): the built-in operations and the implicit trivial assignment
   of a class are not calls */
static triviality assignment_triviality(const semantic_analysis &a,
                                        uint32_t node)
{
    owned_array<uint32_t> work;
    work.append(node);
    while (!work.empty()) {
        uint32_t k = work.back();
        work.resize(work.size() - 1);
        /* an operand with no node of its own (declval<T>()) calls nothing */
        if (!k)
            continue;
        const semantic_node s = a.semantics[k];
        switch (s.kind) {
        case semantic_kind::constant:
        case semantic_kind::entity:
        case semantic_kind::zero:
            break;
        case semantic_kind::conversion:
        case semantic_kind::unary:
        case semantic_kind::address:
        case semantic_kind::indirection:
        case semantic_kind::bind_reference:
        case semantic_kind::member:
            work.append(s.a);
            break;
        case semantic_kind::assignment:
            work.append(s.a);
            work.append(s.b);
            break;
        case semantic_kind::call: {
            /* an implicitly-declared assignment operator that analysis
               defines memberwise (implicit_assignments) is trivial when
               every subobject's selected one is, which it does not tell */
            const semantic_node callee = a.semantics[s.a];
            if (callee.kind == semantic_kind::entity &&
                a.entity_table[callee.a].implicit &&
                a.entity_table[callee.a].operator_function)
                return triviality::unknown;
            return triviality::not_trivial;
        }
        default:
            return triviality::unknown;
        }
    }
    return triviality::trivial;
}

/* ( type-id , type-id ): which is 0 __is_assignable, 1
   __is_nothrow_assignable, 2 __is_trivially_assignable */
analysis_result semantic_analysis::clang_assignable_trait(SyntaxRef n,
                                                          int which,
                                                          expression_meaning &m)
{
    qualified_type t, u;
    VTRY(r, type_id(n[2], t));
    VTRY_SET(r, type_id(n[4], u));
    if (depends_on_template_parameter(t) || depends_on_template_parameter(u))
        return not_supported(n, "temp.dep.constexpr");
    bool value = false;
    expression_meaning left = declval_meaning(t);
    expression_meaning right = declval_meaning(u);
    const struct type lk = type_table[left.type.unqualified()];
    /* the operands have no syntax to analyse again (the overload rules
       would): each a node of its own, as an operand analysed once */
    left.node = expression_node(semantic_kind::constant, left, n.first_token());
    right.node =
        expression_node(semantic_kind::constant, right, n.first_token());
    bool void_operand = fundamental_of(left.type) == fundamental_type::void_ ||
                        fundamental_of(right.type) == fundamental_type::void_;
    /* [expr.ass]/1: the left operand is a modifiable lvalue: no function
       and no array is ("nonmodifiable" [basic.lval]/10, [dcl.array]) */
    bool unassignable =
        lk.kind == type_kind::function || lk.kind == type_kind::array;
    if (!void_operand && !unassignable) {
        if (lk.kind == type_kind::class_type) {
            if (!entity_table[lk.entity].size)
                return not_supported(n, "expr.ass");
            /* [over.match.funcs]/5: an operator= without a reference
               qualifier takes an rvalue object */
            left.category = value_category::lvalue;
        }
        expression_meaning result;
        substitution_mark mark = mark_substitution();
        substituting++;
        unevaluated++;
        r = assignment_of(n, n.first_token(), preprocessing_op_or_punc::EQUAL,
                          SyntaxElement{}, SyntaxElement{}, left, right,
                          result);
        unevaluated--;
        substituting--;
        if (r == analysis_result::ill_formed) {
            if (!undo_substitution(mark))
                return r;
        } else if (r == analysis_result::well_formed) {
            value = true;
            if (which == 1) {
                bool throwing = false;
                VTRY_SET(r, potentially_throwing(n, result.node, throwing));
                value = !throwing;
            } else if (which == 2) {
                triviality k = assignment_triviality(*this, result.node);
                if (k == triviality::unknown)
                    return not_supported(n, "meta.unary.prop");
                value = k == triviality::trivial;
            }
        } else {
            return r;
        }
    }
    m = integer_constant(fundamental_type::bool_, value, n.first_token());
    return analysis_result::well_formed;
}

} // namespace cplusplus
