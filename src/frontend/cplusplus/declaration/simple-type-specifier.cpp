/*
 * simple-type-specifier.cpp: [dcl.type.simple] Simple type specifiers: the
 * types of the fundamental type keywords.  The grammar is in
 * decl-specifier.cpp.
 *
 * [dcl.type.simple]/3: "When multiple simple-type-specifiers are allowed,
 * they can be freely intermixed with other decl-specifiers in any order":
 * the keywords are counted (decl_specifier_seq) and the row of Table 14 is
 * found from the counts.  [ext:gnu_int128] adds a row of its own:
 *
 *     simple-type-specifier:
 *         __int128
 *
 * __int128 alone or with signed, and with unsigned, as GCC's types.
 * [ext:gnu_float128] adds a row:
 *
 *     simple-type-specifier:
 *         __float128
 *
 * the type alone.  [ext:clang_float16] adds two:
 *
 *     simple-type-specifier:
 *         _Float16
 *         __bf16
 *
 * each alone (16-bit floating-point types; no arithmetic is analysed for
 * __bf16, and only the types of a vector for either).  [ext:gnu_complex] adds _Complex (or __complex__,
 * __complex), a modifier that goes with float, double, long double and
 * __float128 in any order:
 *
 *     simple-type-specifier:
 *         _Complex
 *
 * alone it is _Complex double.
 */
#include "../cplusplus-internal.h"
#include "frontend/cplusplus/sema.h"

namespace cplusplus
{

/*
 * [ext:gnu_complex] (GCC manual 6.12): "_Complex float", "double _Complex"
 * and "long double _Complex"; _Complex alone is _Complex double, as clang
 * and GCC read it.  A complex integer type (_Complex int) is not analysed.
 */
analysis_result semantic_analysis::complex_type(SyntaxRef seq,
                                                const simple_type_keywords &k,
                                                unsigned keywords,
                                                type_index &type)
{
    if (k.complex > 1)
        return ill_formed(seq, "_Complex appears at most once", "ext.gnu");
    fundamental_type t = fundamental_type::double_;
    if (k.double_ && k.long_ == 1 && keywords == 2)
        t = fundamental_type::long_double;
    else if (k.double_ && keywords == 1)
        t = fundamental_type::double_;
    else if (k.alone && keywords == 1 &&
             k.alone_type == fundamental_type::float_)
        t = fundamental_type::float_;
    else if (k.float128 == 1 && keywords == 1)
        t = fundamental_type::float128;
    else if (keywords != 0)
        return not_supported(seq, "ext.gnu");
    type = type_table.complex_of(
        qualified_type::make(type_table.fundamental(t), cv_none));
    return analysis_result::well_formed;
}

/*
 * [dcl.type]/2: "As a general rule, at most one defining-type-specifier is
 * allowed in the complete decl-specifier-seq of a declaration ... The only
 * exceptions to this rule are the following:
 *   - signed or unsigned can be combined with char, long, short, or int.
 *   - short or long can be combined with int.
 *   - long can be combined with double.
 *   - long can be combined with long."
 */
analysis_result semantic_analysis::simple_type(SyntaxRef seq,
                                               const simple_type_keywords &k,
                                               type_index &type)
{
    const char *rule = "at most one defining-type-specifier is allowed in the "
                       "complete decl-specifier-seq of a declaration, but "
                       "signed or unsigned can be combined with char, long, "
                       "short, or int, short or long with int, long with "
                       "double, and long with long";
    unsigned keywords = k.signed_ + k.unsigned_ + k.short_ + k.long_ + k.char_ +
                        k.int_ + k.double_ + k.alone + k.int128 + k.float128 +
                        k.float16 + k.bfloat16;
    if (k.float128 && tokens.files.behavior.long_double_fraction_bits != 63)
        /* clang: "__float128 is not supported on this target" (x86-64's) */
        return ill_formed(seq, "__float128 is not supported on this target",
                          "ext.gnu");
    if (k.complex)
        return complex_type(seq, k, keywords, type);
    fundamental_type t = fundamental_type::none;
    if (k.float16 || k.bfloat16) {
        /* [ext:clang_float16] each alone */
        if (k.float16 + k.bfloat16 != 1 || keywords != 1)
            return ill_formed(seq,
                              "_Float16 and __bf16 are not combined with "
                              "other type specifiers",
                              "ext.clang");
        fundamental_type half = fundamental_type::bfloat16;
        if (k.float16)
            half = fundamental_type::float16;
        type = type_table.fundamental(half);
        return analysis_result::well_formed;
    }
    if (k.float128) {
        /* [ext:gnu_float128] (GCC manual 6.12): the type alone */
        if (k.float128 > 1 || keywords != k.float128)
            return ill_formed(seq,
                              "__float128 is not combined with other "
                              "type specifiers",
                              "ext.gnu");
        t = fundamental_type::float128;
    } else if (k.int128) {
        /* [ext:gnu_int128] (GCC manual 6.9): "__int128 for signed 128-bit
           integers, and unsigned __int128 for unsigned 128-bit integers" */
        if (k.int128 > 1 || k.signed_ + k.unsigned_ > 1 ||
            keywords != k.int128 + k.signed_ + k.unsigned_)
            return ill_formed(seq,
                              "__int128 combined only with signed or "
                              "unsigned",
                              "ext.gnu");
        t = fundamental_type::int128;
        if (k.unsigned_)
            t = fundamental_type::unsigned_int128;
    } else if (k.alone) {
        /* bool, float, void, wchar_t, char8_t, char16_t, char32_t: a row of
           their own */
        if (keywords != 1)
            return ill_formed(seq, rule, "dcl.type");
        t = k.alone_type;
    } else if (k.signed_ + k.unsigned_ > 1 ||
               k.short_ + k.char_ + k.double_ > 1 || k.int_ > 1 ||
               (k.int_ && (k.char_ || k.double_)) || k.long_ > 2 ||
               (k.short_ && k.long_) || (k.char_ && k.long_) ||
               (k.double_ && k.long_ > 1)) {
        return ill_formed(seq, rule, "dcl.type");
    } else if (k.double_) {
        if (k.signed_ || k.unsigned_)
            return ill_formed(seq, rule, "dcl.type");
        t = fundamental_type::double_;
        if (k.long_)
            t = fundamental_type::long_double;
    } else if (k.char_) {
        /* char, unsigned char and signed char are three distinct types
           ([basic.fundamental]/7) */
        t = fundamental_type::char_;
        if (k.signed_)
            t = fundamental_type::signed_char;
        if (k.unsigned_)
            t = fundamental_type::unsigned_char;
    } else if (k.short_) {
        t = fundamental_type::short_int;
        if (k.unsigned_)
            t = fundamental_type::unsigned_short_int;
    } else if (k.long_ == 2) {
        t = fundamental_type::long_long_int;
        if (k.unsigned_)
            t = fundamental_type::unsigned_long_long_int;
    } else if (k.long_ == 1) {
        t = fundamental_type::long_int;
        if (k.unsigned_)
            t = fundamental_type::unsigned_long_int;
    } else {
        /* int, signed, unsigned, with or without int */
        t = fundamental_type::int_;
        if (k.unsigned_)
            t = fundamental_type::unsigned_int;
    }
    type = type_table.fundamental(t);
    return analysis_result::well_formed;
}

} // namespace cplusplus
