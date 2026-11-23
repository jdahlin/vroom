/*
 * atomic.cpp: [ext:clang_atomic] (clang Language Extensions, C11 _Atomic
 * in C++): C's atomic type specifier, which libc++'s
 * __atomic/support/c11.h declares std::atomic's object with.
 *
 *     simple-type-specifier:
 *         clang-atomic-type-specifier
 *
 *     clang-atomic-type-specifier:
 *         _Atomic ( type-id )
 *
 *     decl-specifier:
 *         _Atomic
 *
 * (the second C's atomic type qualifier, decl-specifier.cpp: the type the
 * other specifiers name made atomic)
 * The type has C11's atomic layout (unary-expression.cpp, atomic_layout).  An
 * atomic object of an integer, enumeration or pointer type of 1, 2, 4 or 8
 * bytes (atomic_value_type) is read where it is not the operand of &,
 * sizeof or an assignment's left operand (expression, operand_value: C's
 * atomic load), assigned and compound-assigned (assignment_of: C's atomic
 * store and read-modify-write), and the __atomic builtins take a pointer
 * to it.  Other atomic types, ++ and --, and the __c11_atomic builtins are
 * not analysed yet.
 */
#include "../../cplusplus-internal.h"
#include "frontend/cplusplus/sema.h"

namespace cplusplus
{

node_index parser::clang_atomic_type_specifier()
{
    if (!at(preprocessing_op_or_punc::LEFT_PARENTHESIS, 1))
        return beyond_this_step();
    size_t mark = begin_node();
    token_index first = position;
    add_token(); /* _Atomic */
    add_token(); /* ( */
    flag_set greater(greater_is_operator, true);
    type_id();
    if (failed())
        return 0;
    if (!accept(preprocessing_op_or_punc::RIGHT_PARENTHESIS))
        return expected("')' to end the atomic type specifier", "ext.clang");
    return finish_node(syntax_kind::clang_atomic_type_specifier, mark, first);
}

/* [ext:clang_atomic] _Atomic(T): clang's rules, as for C (C23 6.7.3.5/3:
   "The type name in an atomic type specifier shall not refer to an array
   type, a function type, an atomic type, or a qualified type") */
analysis_result semantic_analysis::clang_atomic_type(SyntaxRef n,
                                                     qualified_type &type)
{
    qualified_type value;
    VTRY(r, type_id(n[2], value));
    type_kind k = type_table[value.unqualified()].kind;
    if (value.cv() || k == type_kind::array || k == type_kind::function ||
        k == type_kind::atomic || k == type_kind::lvalue_reference ||
        k == type_kind::rvalue_reference)
        return ill_formed(n,
                          "_Atomic cannot be applied to an array, function, "
                          "reference, atomic or qualified type",
                          "ext.clang");
    VTRY(r2, atomic_value_check(n, value));
    type = qualified_type::make(type_table.atomic_of(value), cv_none);
    return analysis_result::well_formed;
}

/* clang: "_Atomic cannot be applied to type 'S' which is not trivially
   copyable"; an atomic _BitInt is not analysed */
analysis_result semantic_analysis::atomic_value_check(SyntaxRef n,
                                                      qualified_type value)
{
    const struct type &record = type_table[value.unqualified()];
    /* (clang's rules for an atomic _BitInt, by its width, are not
       analysed) */
    if (record.kind == type_kind::bit_int)
        return not_supported(n, "ext.clang");
    if (record.kind != type_kind::class_type)
        return analysis_result::well_formed;
    bool known = true;
    bool copyable = class_trivially_copyable(record.entity, known);
    if (!known)
        return not_supported(n, "ext.clang");
    if (!copyable)
        return ill_formed(n,
                          "_Atomic cannot be applied to a type which is not "
                          "trivially copyable",
                          "ext.clang");
    return analysis_result::well_formed;
}

/* [ext:clang_atomic] the value types whose atomic objects analysis reads
   and writes: those the code generator does with one instruction (C's
   front end calls the others "wide", through libatomic) */
type_index semantic_analysis::atomic_value_type(qualified_type t) const
{
    const struct type &atomic = type_table[t.unqualified()];
    if (atomic.kind != type_kind::atomic)
        return 0;
    qualified_type value = atomic.operand;
    if (!is_integral(value) && !is_pointer(value) && !enumeration_of(value))
        return 0;
    uint64_t size = size_of_type(value);
    if (size != 1 && size != 2 && size != 4 && size != 8)
        return 0;
    return value.unqualified();
}

} // namespace cplusplus
