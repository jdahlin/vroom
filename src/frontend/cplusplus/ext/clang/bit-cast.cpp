/*
 * bit-cast.cpp: [ext:clang_bit_cast] (clang Language Extensions,
 * __builtin_bit_cast): the primitive libc++'s std::bit_cast is.  [bit.cast]/1:
 * "Returns: An object of type To.  Implicitly creates objects nested within
 * the result.  Each bit of the value representation of the result is equal
 * to the corresponding bit in the object representation of from."
 *
 *     primary-expression:
 *         clang-bit-cast-expression
 *
 *     clang-bit-cast-expression:
 *         __builtin_bit_cast ( type-id , assignment-expression )
 */
#include "../../cplusplus-internal.h"
#include "frontend/cplusplus/sema.h"

namespace cplusplus
{

node_index parser::clang_bit_cast_expression()
{
    size_t mark = begin_node();
    token_index first = position;
    add_token(); /* __builtin_bit_cast */
    if (!accept(preprocessing_op_or_punc::LEFT_PARENTHESIS))
        return expected("'(' after __builtin_bit_cast", "ext.clang");
    flag_set greater(greater_is_operator, true);
    type_id();
    if (failed())
        return 0;
    if (!accept(preprocessing_op_or_punc::COMMA))
        return expected("',' after the type-id", "ext.clang");
    assignment_expression();
    if (failed())
        return 0;
    if (!accept(preprocessing_op_or_punc::RIGHT_PARENTHESIS))
        return expected("')' to end __builtin_bit_cast", "ext.clang");
    return finish_node(syntax_kind::clang_bit_cast_expression, mark, first);
}

bool bit_cast_kept(const implementation_defined_behavior &behavior,
                   fundamental_type from, fundamental_type to, uint64_t kept,
                   uint64_t &result)
{
    /* the object representation of from's value */
    uint64_t bits = kept;
    if (from == fundamental_type::float_) {
        bits = float_storage_bits(kept);
    } else if (from != fundamental_type::double_ &&
               (!fundamental_type_is_integral(from) ||
                fundamental_type_width(behavior, from) > 64)) {
        return false;
    }
    if (to == fundamental_type::float_) {
        uint32_t b32 = static_cast<uint32_t>(bits);
        uint32_t fraction = b32 & UINT32_C(0x007fffff);
        if ((b32 & UINT32_C(0x7f800000)) == UINT32_C(0x7f800000) && fraction) {
            result = (uint64_t)(b32 & UINT32_C(0x80000000)) << 32 |
                     UINT64_C(0x7ff0000000000000) | (uint64_t)fraction << 29;
            return true;
        }
        float f;
        memcpy(&f, &b32, sizeof f);
        result = floating_kept(f);
        return true;
    }
    if (to == fundamental_type::double_) {
        result = bits;
        return true;
    }
    if (!fundamental_type_is_integral(to) ||
        fundamental_type_width(behavior, to) > 64)
        return false;
    /* [bit.cast]/2: a value representation that is no value of bool is
       undefined: not a constant */
    if (to == fundamental_type::bool_ && bits > 1)
        return false;
    unsigned width = fundamental_type_width(behavior, to);
    if (width < 64)
        bits &= (uint64_t(1) << width) - 1;
    result = integer_wrap(behavior, to, bits);
    return true;
}

/* [ext:clang_bit_cast] a prvalue of the type-id's type.  [bit.cast]/1:
   "sizeof(To) == sizeof(From) is true"; analysis handles arithmetic,
   pointer and trivially copyable class types (a class's value is not
   folded).  [bit.cast]/3: a constant expression unless a type is a pointer
   type (among others); folded here for arithmetic types up to 64 bits */
analysis_result semantic_analysis::clang_bit_cast(SyntaxRef n,
                                                  expression_meaning &m)
{
    qualified_type to;
    VTRY(r, type_id(n[2], to));
    VTRY_SET(r, expression(n[4], m));
    if (depends_on_template_parameter(to) ||
        depends_on_template_parameter(m.type))
        return not_supported_because(
            n, "temp.dep.constexpr",
            "__builtin_bit_cast with a dependent type or operand");
    to = qualified_type::make(to.unqualified(), cv_none);
    bool pointer = is_pointer(to) || is_pointer(m.type);
    /* arithmetic types (__bf16 among them, which no arithmetic conversion
       takes), pointers, and [bit.cast]/1's trivially copyable classes */
    const qualified_type both[] = {to, m.type};
    for (qualified_type t : both) {
        const struct type &x = type_table[t.unqualified()];
        if (is_arithmetic(t) || is_pointer(t) ||
            (x.kind == type_kind::fundamental &&
             x.fundamental == fundamental_type::bfloat16))
            continue;
        if (x.kind != type_kind::class_type ||
            entity_table[x.entity].kind != entity_kind::class_type)
            return not_supported_because(n, "bit.cast",
                                         "__builtin_bit_cast of this type");
        require_complete(x.entity);
        bool known = true;
        bool copyable = class_trivially_copyable(x.entity, known);
        if (!known)
            return not_supported_because(
                n, "bit.cast",
                "__builtin_bit_cast of a class whose triviality is not known");
        if (!copyable)
            return ill_formed(n,
                              "__builtin_bit_cast requires trivially copyable "
                              "types",
                              "bit.cast");
    }
    if (size_of_type(to) != size_of_type(m.type))
        return ill_formed(n,
                          "__builtin_bit_cast requires sizeof(To) == "
                          "sizeof(From)",
                          "bit.cast");
    VTRY_SET(r, operand_value(n, m));
    uint64_t value = 0;
    if (pointer)
        m.evaluation = constness::not_constant;
    else if (m.evaluation == constness::constant &&
             bit_cast_kept(tokens.files.behavior, fundamental_of(m.type),
                           fundamental_of(to), m.value, value))
        m.value = value;
    else if (m.evaluation == constness::constant)
        m.evaluation = constness::unknown;
    m.type = to;
    m.zero_literal = false;
    converted(m, conversion_kind::bit_cast);
    return analysis_result::well_formed;
}

} // namespace cplusplus
