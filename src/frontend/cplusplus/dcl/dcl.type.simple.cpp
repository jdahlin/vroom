/*
 * dcl.type.simple.cpp: [dcl.type.simple] Simple type specifiers: the types
 * of the fundamental type keywords.  The grammar is in dcl.spec.cpp.
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
 */
#include "../cplusplus-internal.h"
#include "frontend/cplusplus/sema.h"

namespace cplusplus
{

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
                        k.int_ + k.double_ + k.alone + k.int128;
    fundamental_type t = fundamental_type::none;
    if (k.int128) {
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
