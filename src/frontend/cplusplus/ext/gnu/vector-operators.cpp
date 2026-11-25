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
 * scalar the element type holds (vector_scalar_fits: clang's "implicit
 * conversion would cause truncation");
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

/*
 * Whether the scalar converts to the vector's element type without
 * changing its value, as clang's splat of a scalar operand checks it
 * ("implicit conversion would cause truncation" otherwise): fits is the
 * answer, false when analysis cannot tell.  Of the element's type, yes.  A
 * constant: an integer element holds it in its width, signed or unsigned
 * (clang: unsigned + -1 is allowed); a floating-point element has its value
 * exactly (constant_representable: double 1.1 to float is not, 0.5 is).
 * Not a constant: a floating-point scalar of a type no greater than the
 * element's ([conv.fpprom]'s order: _Float16, float, double); an integer
 * whose width is at most the element's significand (short to float, int to
 * double; int to float is not).  long double, __float128, __bf16 and a
 * non-constant integer to an integer element of another type: not told.
 */
bool semantic_analysis::vector_scalar_fits(const expression_meaning &scalar,
                                           qualified_type element,
                                           bool &fits) const
{
    fits = true;
    if (scalar.type.unqualified() == element.unqualified())
        return true;
    const implementation_defined_behavior &b = tokens.files.behavior;
    fundamental_type e = fundamental_of(element),
                     s = fundamental_of(scalar.type);
    if (is_integral(element) && is_integral(scalar.type)) {
        if (scalar.evaluation != constness::constant)
            return false;
        unsigned bits = static_cast<unsigned>(size_of_type(element)) * 8;
        if (bits >= 127)
            return true;
        __int128 v = integer_value(b, s, scalar.value);
        __int128 high = (static_cast<__int128>(1) << bits) - 1,
                 low = -(static_cast<__int128>(1) << (bits - 1));
        fits = v >= low && v <= high;
        return true;
    }
    if (!fundamental_type_is_floating(e))
        return false;
    if (scalar.evaluation == constness::constant)
        return constant_representable(scalar, e, fits);
    if (scalar.evaluation != constness::not_constant)
        return false;
    static const fundamental_type order[] = {fundamental_type::float16,
                                             fundamental_type::float_,
                                             fundamental_type::double_};
    static const unsigned precision[] = {11, 24, 53};
    int element_rank = -1, scalar_rank = -1;
    for (int i = 0; i < 3; i++) {
        if (order[i] == e)
            element_rank = i;
        if (order[i] == s)
            scalar_rank = i;
    }
    if (element_rank < 0)
        return false;
    if (fundamental_type_is_floating(s)) {
        if (scalar_rank < 0)
            return false;
        fits = scalar_rank <= element_rank;
        return true;
    }
    if (!fundamental_type_is_integral(s))
        return false;
    fits = fundamental_type_width(b, s) <= precision[element_rank];
    return true;
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
        return not_supported_because(
            n, "ext.gnu",
            "a vector operand of an operator that does not work lane by lane");
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
        return not_supported_because(
            n, "ext.gnu", "a binary operator on vectors of different types");
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
            return not_supported_because(
                n, "ext.gnu",
                "a vector operator with a non-arithmetic scalar operand");
        bool fits = true;
        if (!shift && !vector_scalar_fits(*scalar, element, fits))
            return not_supported_because(
                n, "ext.gnu",
                "a vector operator with a scalar whose fit in the element type "
                "is unknown");
        if (!fits)
            return ill_formed(n,
                              "a scalar cannot be converted to a vector "
                              "element type, as the implicit conversion "
                              "would cause truncation",
                              "ext.gnu");
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
