/*
 * builtin-invoke.cpp: [ext:clang_builtin_functions] __builtin_invoke (f,
 * args...) (clang's Builtins.td; libc++'s std::invoke is written with it
 * when __has_builtin says it exists): [func.require]/1's INVOKE, "Define
 * INVOKE(f, t1, t2, ..., tN) as follows:
 *   (1.1) (t1.*f)(t2, ..., tN) when f is a pointer to a member function of
 *         a class T and is_base_of_v<T, remove_reference_t<decltype(t1)>>
 *         is true;
 *   (1.4) t1.*f when N == 1 and f is a pointer to data member of a class T
 *         and is_base_of_v<T, remove_reference_t<decltype(t1)>> is true;
 *   (1.6) ((*t1).*f)(t2, ..., tN) when f is a pointer to a member function
 *         of a class T and t1 does not satisfy the previous two items;
 *   (1.7) (*t1).*f when N == 1 and f is a pointer to data member of a
 *         class T and t1 does not satisfy the previous two items;
 *   (1.8) f(t1, t2, ..., tN) in all other cases."
 * The call is analysed as the expression it stands for, so the result type,
 * value category and noexcept-ness are that expression's, and an INVOKE of
 * the arguments that is ill-formed is an error where clang's is (a
 * substitution failure in a template argument).  An object that is a
 * std::reference_wrapper ((1.2), (1.5)) is not supported.
 */
#include "../../cplusplus-internal.h"
#include "frontend/cplusplus/sema.h"

namespace cplusplus
{

/* the expression a __builtin_invoke call stands for, of the arguments'
   meanings ops (the callable first) */
analysis_result semantic_analysis::builtin_invoke(
    SyntaxRef n, entity_index called, const owned_array<SyntaxElement> &written,
    const owned_array<expression_meaning> &ops, expression_meaning &m)
{
    if (ops.empty())
        return ill_formed(n,
                          "__builtin_invoke takes the callable and its "
                          "arguments",
                          "ext.clang");
    expression_meaning f = ops[0];
    /* the arguments after the callable (and the object of a member
       pointer) */
    size_t skip = 1;
    owned_array<SyntaxElement> rest_syntax;
    owned_array<expression_meaning> rest;
    qualified_type t = f.type;
    if (is_member_pointer(t)) {
        if (ops.size() < 2)
            return ill_formed(n,
                              "__builtin_invoke of a pointer to member takes "
                              "an object",
                              "ext.clang");
        skip = 2;
        expression_meaning object = ops[1];
        const struct type pointer = type_table[t.unqualified()];
        bool function = type_table[pointer.operand.unqualified()].kind ==
                        type_kind::function;
        if (!function && ops.size() != 2)
            return ill_formed(n,
                              "__builtin_invoke of a pointer to data member "
                              "takes the object alone",
                              "ext.clang");
        /* the object is of the member's class or a class derived from it
           (1.1, 1.4): .*; a pointer to one (1.6, 1.7): ->*; anything else
           that is a class is no object of the class */
        bool arrow = true;
        const struct type &own = type_table[object.type.unqualified()];
        if (own.kind == type_kind::class_type) {
            if (own.entity != pointer.entity &&
                !base_subobjects(own.entity, pointer.entity))
                return not_supported_builtin(
                    n, "ext.clang", called,
                    "with an object that is not of the member's class: a "
                    "std::reference_wrapper is not supported");
            arrow = false;
        }
        expression_meaning access;
        VTRY(r, member_pointer_operator(n, n.first_token(), arrow, object, f,
                                        access));
        if (!function) {
            m = access;
            return analysis_result::well_formed;
        }
        for (size_t i = skip; i < ops.size(); i++) {
            rest.append(ops[i]);
            if (i < written.size())
                rest_syntax.append(written[i]);
        }
        return call_with(n, access, access.type, 0, 0, rest_syntax, rest, m);
    }
    if (type_table[t.unqualified()].kind == type_kind::class_type) {
        owned_array<SyntaxElement> operand_syntax;
        for (SyntaxElement a : written)
            operand_syntax.append(a);
        owned_array<expression_meaning> operands;
        for (const expression_meaning &e : ops)
            operands.append(e);
        bool handled = false;
        VTRY(r, overloaded_operator(
                    n, n[1].token(), preprocessing_op_or_punc::LEFT_PARENTHESIS,
                    false, operand_syntax, operands, handled, m));
        if (handled)
            return analysis_result::well_formed;
        return ill_formed(n,
                          "the first argument of __builtin_invoke is not "
                          "callable",
                          "ext.clang");
    }
    qualified_type function_type = f.type;
    if (type_table[f.type.unqualified()].kind != type_kind::function ||
        f.category != value_category::lvalue) {
        VTRY(r, operand_value(n, f));
        const struct type pointer = type_table[f.type.unqualified()];
        if (pointer.kind != type_kind::pointer ||
            type_table[pointer.operand.unqualified()].kind !=
                type_kind::function) {
            if (fundamental_of(f.type) == fundamental_type::none &&
                !is_pointer(f.type))
                return not_supported_builtin(n, "ext.clang", called,
                                             "of a callable of this type");
            return ill_formed(n,
                              "the first argument of __builtin_invoke is not "
                              "callable",
                              "ext.clang");
        }
        function_type = pointer.operand;
    }
    for (size_t i = 1; i < ops.size(); i++) {
        rest.append(ops[i]);
        if (i < written.size())
            rest_syntax.append(written[i]);
    }
    return call_with(n, f, function_type, 0, 0, rest_syntax, rest, m);
}

} // namespace cplusplus
