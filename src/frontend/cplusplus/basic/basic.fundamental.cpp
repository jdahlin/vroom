/*
 * basic.fundamental.cpp: [basic.fundamental] Fundamental types, and the
 * implementation-defined choices ([intro.defs]) that fix their properties.
 */
#include "../cplusplus-internal.h"

namespace cplusplus
{

/*
 * The choices clang makes for -triple aarch64-unknown-linux-gnu: LP64
 * ([basic.fundamental]/1 leaves the widths to the implementation), unsigned
 * plain char and wchar_t (AAPCS64), '$' in identifiers, no Microsoft
 * extensions.
 */
implementation_defined_behavior
implementation_defined_behavior::for_edition(standard_edition edition,
                                             bool gnu_extensions)
{
    implementation_defined_behavior behavior;
    behavior.edition = edition;
    behavior.gnu_extensions = gnu_extensions;
    /* [diff.cpp14.lex]/1: "Removal of trigraph support as a required
       feature."  clang keeps trigraphs in the strict modes before C++17. */
    behavior.trigraphs =
        !gnu_extensions && behavior.has(language_feature::trigraphs);
    behavior.char8_t_keyword = behavior.has(language_feature::char8_t_type);
    return behavior;
}

const char *fundamental_type_name(fundamental_type type)
{
    static constexpr const char *names[] = {
        "",
        "bool",
        "char",
        "char8_t",
        "char16_t",
        "char32_t",
        "wchar_t",
        "int",
        "unsigned int",
        "long",
        "unsigned long",
        "long long",
        "unsigned long long",
        "float",
        "double",
        "long double",
        "decltype(nullptr)",
        "_Float16",
        "_BitInt",
        "unsigned _BitInt",
    };
    return names[static_cast<int>(type)];
}

/* [basic.fundamental]/4: the width is the number of value bits plus the sign
   bit; [basic.fundamental]/1 leaves the standard integer types' widths to the
   implementation within the minimums of Table 12 */
unsigned fundamental_type_width(const implementation_defined_behavior &behavior,
                                fundamental_type type)
{
    switch (type) {
    case fundamental_type::bool_:
        return 1;
    case fundamental_type::char_:
    case fundamental_type::char8_t_:
        return 8;
    case fundamental_type::char16_t_:
        return 16;
    case fundamental_type::char32_t_:
        return 32;
    case fundamental_type::wchar_t_:
        return behavior.wchar_t_width;
    case fundamental_type::int_:
    case fundamental_type::unsigned_int:
        return behavior.int_width;
    case fundamental_type::long_int:
    case fundamental_type::unsigned_long_int:
        return behavior.long_width;
    case fundamental_type::long_long_int:
    case fundamental_type::unsigned_long_long_int:
        return behavior.long_long_width;
    default:
        return 0;
    }
}

bool fundamental_type_is_signed(const implementation_defined_behavior &behavior,
                                fundamental_type type)
{
    switch (type) {
    case fundamental_type::char_:
        /* [basic.fundamental]/7: "It is implementation-defined whether a char
           object can hold negative values." */
        return behavior.plain_char_is_signed;
    case fundamental_type::wchar_t_:
        return behavior.wchar_t_is_signed;
    case fundamental_type::int_:
    case fundamental_type::long_int:
    case fundamental_type::long_long_int:
        return true;
    default:
        return false;
    }
}

} // namespace cplusplus
