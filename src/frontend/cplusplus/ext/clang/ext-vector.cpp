/*
 * ext-vector.cpp: [ext:clang_ext_vector_type] (clang Language Extensions,
 * Vectors and Extended Vectors): ext_vector_type(N) on an
 * alias-declaration makes its type a vector of N elements of the type,
 * which libc++'s __algorithm/simd_utils.h declares as
 *
 *     template <class T, size_t N>
 *     using __simd_vector __attribute__((__ext_vector_type__(N))) = T;
 *
 * The type has clang's layout on AArch64 (ext_vector_layout) and mangling
 * (Dv N _ element).  Its values (element access, operators, conversions)
 * are not analysed yet: an expression of the type is unsupported.
 */
#include "../../cplusplus-internal.h"
#include "frontend/cplusplus/sema.h"

namespace cplusplus
{

/* ext_vector_type(N)'s N: an integral constant at least 1, or in a
   template's own declaration a non-type template-parameter
   ([temp.dep.constexpr]/2), as for an array's bound */
analysis_result semantic_analysis::clang_ext_vector_count(SyntaxRef at,
                                                          SyntaxElement count,
                                                          gnu_attributes &g)
{
    if (entity_index named = value_parameter_named(count)) {
        if (!is_integral(entity_table[named].type))
            return not_supported(at, "temp.dep.constexpr");
        g.ext_vector_parameter = named;
        return analysis_result::well_formed;
    }
    expression_meaning m;
    analysis_result r = expression(count, m);
    if (r == analysis_result::well_formed)
        r = operand_value(at, m);
    bool constant = false;
    if (r == analysis_result::well_formed && is_integral(m.type))
        r = integral_constant_expression(at, m, constant);
    if (r != analysis_result::well_formed)
        return r;
    if (!constant)
        return not_supported(at, "ext.clang");
    __int128 n =
        integer_value(tokens.files.behavior, fundamental_of(m.type), m.value);
    if (n < 1 || n > 4096)
        return ill_formed(at, "a vector's element count shall be positive",
                          "ext.clang");
    g.ext_vector = static_cast<uint64_t>(n);
    return analysis_result::well_formed;
}

/* the vector of the type's elements g asks for: the element an
   arithmetic type other than bool (clang: "invalid vector element type"),
   or a template's type parameter */
analysis_result
semantic_analysis::clang_ext_vector_type(SyntaxRef at, const gnu_attributes &g,
                                         qualified_type &type)
{
    const struct type &element = type_table[type.unqualified()];
    if (element.kind != type_kind::template_parameter &&
        (element.kind != type_kind::fundamental ||
         element.fundamental == fundamental_type::bool_ ||
         !is_arithmetic(type)))
        return not_supported(at, "ext.clang");
    type = qualified_type::make(
        type_table.ext_vector_of(
            qualified_type::make(type.unqualified(), cv_none), g.ext_vector,
            g.ext_vector_parameter),
        cv_none);
    return analysis_result::well_formed;
}

/* [impl.clang] the vector's size, or alignment, on AArch64: the elements'
   bytes rounded up to a power of two, aligned to it up to 16 */
uint64_t semantic_analysis::ext_vector_layout(const struct type &record,
                                              bool alignment) const
{
    if (!record.bound)
        return 0;
    uint64_t size =
        power_of_two_at_least(record.bound * size_of_type(record.operand));
    if (alignment && size > 16)
        return 16;
    return size;
}

} // namespace cplusplus
