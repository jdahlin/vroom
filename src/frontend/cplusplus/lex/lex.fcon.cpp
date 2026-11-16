/*
 * lex.fcon.cpp: [lex.fcon] Floating-point literals: the type of a pp-number
 * that lex.ppnumber.cpp read as a floating-point-literal.  Its value
 * ([lex.fcon]/2-3) is a later step.
 *
 *     floating-point-literal:
 *         decimal-floating-point-literal
 *         hexadecimal-floating-point-literal
 */
#include "../cplusplus-internal.h"

namespace cplusplus
{

/*
 * [lex.fcon]/1: "The type of a floating-point-literal is determined by its
 * floating-point-suffix as specified in Table 10":
 *
 *     none      double
 *     f or F    float
 *     l or L    long double
 *
 * and clang's f16 for _Float16, an extended floating-point type
 * ([basic.fundamental]/12) on this target.
 */
void lexer::convert_floating_point_literal(const pp_number_analysis &a,
                                           token &out)
{
    out.literal = literal_kind::floating_point_literal;
    out.type = a.float_suffix     ? fundamental_type::float_
               : a.long_suffix    ? fundamental_type::long_double
               : a.float16_suffix ? fundamental_type::float16
                                  : fundamental_type::double_;
}

} // namespace cplusplus
