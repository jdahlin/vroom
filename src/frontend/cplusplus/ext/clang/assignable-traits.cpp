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
        return not_supported_because(
            n, "temp.dep.constexpr",
            "an assignability trait of a dependent type");
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
            require_complete(lk.entity);
            if (!entity_table[lk.entity].size)
                return not_supported_because(n, "expr.ass",
                                             "an assignability trait whose "
                                             "left operand class has no size");
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
                    return not_supported_because(
                        n, "meta.unary.prop",
                        "a trivial-assignment trait whose assignment's "
                        "triviality is unknown");
                value = k == triviality::trivial;
            }
        } else {
            return r;
        }
    }
    m = integer_constant(fundamental_type::bool_, value, n.first_token());
    return analysis_result::well_formed;
}

/* ( type-id , type-id ): [meta.unary.prop] reference_constructs_from_temporary
   (which 0) and reference_converts_from_temporary (which 2): "T is a
   reference type, and the initialization T t(declval<U>()) (T t =
   declval<U>()) is well-formed and binds t to a temporary object whose
   lifetime is not extended"; clang takes U as a prvalue when it is no
   reference.  The binding is analysed as the program's: a temporary is a
   bind_reference whose operand is a prvalue ([dcl.init.ref]/5.4). */
analysis_result
semantic_analysis::clang_reference_temporary_trait(SyntaxRef n, int which,
                                                   expression_meaning &m)
{
    qualified_type t, u;
    VTRY(r, type_id(n[2], t));
    VTRY_SET(r, type_id(n[4], u));
    if (depends_on_template_parameter(t) || depends_on_template_parameter(u))
        return not_supported_because(
            n, "temp.dep.constexpr",
            "a reference-temporary trait of a dependent type");
    bool value = false;
    const struct type tk = type_table[t.unqualified()];
    const struct type uk = type_table[u.unqualified()];
    bool reference = tk.kind == type_kind::lvalue_reference ||
                     tk.kind == type_kind::rvalue_reference;
    bool u_reference = uk.kind == type_kind::lvalue_reference ||
                       uk.kind == type_kind::rvalue_reference;
    if (reference && fundamental_of(u) != fundamental_type::void_ &&
        uk.kind != type_kind::function && uk.kind != type_kind::array) {
        expression_meaning from = declval_meaning(u);
        if (!u_reference) {
            from.category = value_category::prvalue;
            /* [expr.type]/2: a non-class prvalue has no cv-qualifiers */
            if (type_table[from.type.unqualified()].kind !=
                type_kind::class_type)
                from.type =
                    qualified_type::make(from.type.unqualified(), cv_none);
        }
        if (from.type.unqualified() &&
            type_table[from.type.unqualified()].kind == type_kind::class_type) {
            require_complete(type_table[from.type.unqualified()].entity);
            entity_index uc = type_table[from.type.unqualified()].entity;
            if (!entity_table[uc].size)
                return not_supported_because(n, "dcl.init.ref",
                                             "a reference-temporary trait from "
                                             "a class that has no size");
            /* before C++17 a class prvalue is copied or moved: not
               followed for a class that cannot be */
            if (!u_reference)
                for (entity_index k = entity_table[uc].first_constructor; k;
                     k = entity_table[k].next_constructor)
                    if (entity_table[k].deleted &&
                        copy_or_move_constructor(uc, k))
                        return not_supported_because(
                            n, "dcl.init.ref",
                            "a reference-temporary trait of a class with a "
                            "deleted copy or move constructor");
            /* [over.match.ref]/1: a direct-initialization also considers
               the explicit conversion functions, which the binding does
               not */
            for (entity_index f = own_conversion_functions(uc); f;
                 f = entity_table[f].next_overload)
                if (which != 2 && entity_table[f].explicit_)
                    return not_supported_because(
                        n, "over.match.ref",
                        "a reference-temporary trait through an explicit "
                        "conversion function");
        }
        from.node =
            expression_node(semantic_kind::constant, from, n.first_token());
        initialized_value made;
        bool ok = false;
        VTRY_SET(r, try_initialization(n, from, t, which != 2, made, ok));
        if (ok) {
            if (!made.node ||
                semantics[made.node].kind != semantic_kind::bind_reference)
                return not_supported_because(
                    n, "dcl.init.ref",
                    "a reference-temporary trait whose initialization is not a "
                    "reference binding");
            uint32_t target = semantics[made.node].a;
            /* a derived-to-base conversion of a temporary binds to it */
            while (target &&
                   (semantics[target].kind == semantic_kind::conversion ||
                    semantics[target].kind == semantic_kind::member) &&
                   semantics[target].category != value_category::prvalue &&
                   type_table[semantics[target].type.unqualified()].kind ==
                       type_kind::class_type)
                target = semantics[target].a;
            value =
                target && semantics[target].category == value_category::prvalue;
        }
    }
    m = integer_constant(fundamental_type::bool_, value, n.first_token());
    return analysis_result::well_formed;
}

} // namespace cplusplus
