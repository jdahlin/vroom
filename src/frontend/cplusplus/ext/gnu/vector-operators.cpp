/*
 * vector-operators.cpp: [ext:gnu_vector_size] the binary operators on
 * vectors (GCC manual 6.37, Using Vector Instructions through Built-in
 * Functions): "The types defined in this manner can be used with a subset
 * of normal C operations.  Currently, GCC allows using the following
 * operators on these types: +, -, *, /, unary minus, ^, |, &, ~, %."
 * and "The operations behave like C++ valarrays.  Addition is defined as
 * the addition of the corresponding elements of the operands. ... For
 * convenience, it is allowed to use a binary vector operation where one
 * operand is a scalar.  In that case the compiler transforms the scalar
 * operand into a vector where each element is the scalar from the
 * operation."  The comparisons give "a vector of the same width and number
 * of elements that has a signed integral element type", -1 where true
 * and 0 where false (clang's, as GCC's); the shifts take the elements of
 * the left operand by the right operand's, or by a scalar.
 *
 * What is analysed: two operands of one vector type, or a vector and a
 * scalar the element type holds (the scalar's type, or an integer constant
 * that fits: clang's "implicit conversion would cause truncation");
 * different vector types (clang's lax conversions) and && || <=> are not.
 * The result is as C's front end builds it (frontend/c/expression.cpp):
 * the scalar converted to the element type, the node left to code
 * generation, which does the lanes.
 */
#include "../../cplusplus-internal.h"
#include "frontend/cplusplus/sema.h"

namespace cplusplus
{

/* the signed integer vector of a comparison: elements of the element's
   size, as many ([impl.clang]: long for 8 bytes where long is 64 bits) */
type_index types::vector_mask_of(const struct type &vector, uint64_t element,
                                 bool long_is_64)
{
    fundamental_type t = fundamental_type::int_;
    switch (element) {
    case 1:
        t = fundamental_type::signed_char;
        break;
    case 2:
        t = fundamental_type::short_int;
        break;
    case 8:
        t = fundamental_type::long_long_int;
        if (long_is_64)
            t = fundamental_type::long_int;
        break;
    case 16:
        t = fundamental_type::int128;
        break;
    default:
        break;
    }
    return vector_of(qualified_type::make(fundamental(t), cv_none),
                     vector.bound);
}

/* whether the scalar converts to the vector's element without changing its
   value: its type is the element's, or it is an integer constant of the
   element's range */
bool semantic_analysis::vector_scalar_fits(const expression_meaning &scalar,
                                           qualified_type element) const
{
    if (scalar.type.unqualified() == element.unqualified())
        return true;
    if (!is_integral(element) || !is_integral(scalar.type) ||
        scalar.evaluation != constness::constant)
        return false;
    const implementation_defined_behavior &b = tokens.files.behavior;
    unsigned bits = static_cast<unsigned>(size_of_type(element)) * 8;
    if (bits >= 127)
        return true;
    __int128 v = integer_value(b, fundamental_of(scalar.type), scalar.value);
    __int128 high = (static_cast<__int128>(1) << bits) - 1, low = 0;
    if (fundamental_type_is_signed(b, fundamental_of(element))) {
        high = (static_cast<__int128>(1) << (bits - 1)) - 1;
        low = -high - 1;
    }
    return v >= low && v <= high;
}

analysis_result semantic_analysis::vector_binary(SyntaxRef n, token_index at,
                                                 preprocessing_op_or_punc op,
                                                 expression_meaning &left,
                                                 expression_meaning &right,
                                                 expression_meaning &m)
{
    using op_t = preprocessing_op_or_punc;
    bool compare = op == op_t::LESS || op == op_t::GREATER ||
                   op == op_t::LESS_EQUAL || op == op_t::GREATER_EQUAL ||
                   op == op_t::EQUAL_EQUAL || op == op_t::EXCLAMATION_EQUAL;
    bool shift = op == op_t::LESS_LESS || op == op_t::GREATER_GREATER;
    bool integer_only = shift || op == op_t::PERCENT || op == op_t::AMPERSAND ||
                        op == op_t::BAR || op == op_t::CARET;
    bool lanewise = compare || integer_only || op == op_t::PLUS ||
                    op == op_t::MINUS || op == op_t::STAR || op == op_t::SLASH;
    if (!lanewise)
        return not_supported(n, "ext.gnu");
    bool left_vector =
             type_table[left.type.unqualified()].kind == type_kind::vector,
         right_vector =
             type_table[right.type.unqualified()].kind == type_kind::vector;
    qualified_type vector = left.type;
    if (!left_vector)
        vector = right.type;
    vector = qualified_type::make(vector.unqualified(), cv_none);
    if (left_vector && right_vector &&
        left.type.unqualified() != right.type.unqualified())
        /* clang's lax vector conversions */
        return not_supported(n, "ext.gnu");
    const struct type record = type_table[vector.unqualified()];
    qualified_type element = record.operand;
    if (integer_only && !is_integral(element))
        return ill_formed(n,
                          "the operands of this operator shall be vectors of "
                          "integer elements",
                          "ext.gnu");
    expression_meaning *scalar = &right;
    if (right_vector)
        scalar = &left;
    if (!(left_vector && right_vector)) {
        if (!is_arithmetic(scalar->type))
            return not_supported(n, "ext.gnu");
        if (!shift && !vector_scalar_fits(*scalar, element)) {
            /* a floating constant of another type: converted when it is
               in the range of the element (clang: "as implicit
               conversion would cause truncation" otherwise) */
            fundamental_type e = fundamental_of(element);
            if (scalar->evaluation != constness::constant ||
                !fundamental_type_is_floating(e) ||
                fundamental_of(scalar->type) != fundamental_type::double_ ||
                e != fundamental_type::float_)
                return not_supported(n, "ext.gnu");
            double v = floating_value(scalar->value);
            if (v > 3.4028234663852886e38 || v < -3.4028234663852886e38)
                return ill_formed(n,
                                  "a scalar cannot be converted to a vector "
                                  "of a narrower element type, as the "
                                  "implicit conversion would cause "
                                  "truncation",
                                  "ext.gnu");
        }
        VTRY(r, implicit_conversion(n, *scalar, element, false));
    }
    m = expression_meaning();
    m.type = vector;
    if (compare)
        m.type = qualified_type::make(
            type_table.vector_mask_of(record, size_of_type(element),
                                      tokens.files.behavior.long_width == 64),
            cv_none);
    m.category = value_category::prvalue;
    m.evaluation = constness::not_constant;
    m.beyond_integral_constant = true;
    return binary_built(analysis_result::well_formed, at, op, left, right, m);
}

} // namespace cplusplus
