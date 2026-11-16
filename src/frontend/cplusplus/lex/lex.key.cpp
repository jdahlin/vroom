/*
 * lex.key.cpp: [lex.key] Keywords.
 *
 *     keyword:
 *         any identifier listed in Table 5
 *         import-keyword
 *         module-keyword
 *         export-keyword
 *
 * [lex.key]/1: "The identifiers shown in Table 5 are reserved for use as
 * keywords (that is, they are unconditionally treated as keywords in phase
 * 7) except in an attribute-token."  [lex.key]/2: the alternative
 * representations of Table 6 "are reserved and shall not be used otherwise".
 *
 * Table 5 differs by edition (char8_t, concept, consteval, ... are C++20's),
 * and clang adds its extensions' keywords (__attribute__, __is_same, ...).
 * lex.key.inc is clang's table, with the conditions under which each
 * spelling is a keyword; keyword_status() is clang's reading of them
 * (IdentifierTable.cpp, getKeywordStatus).
 */
#include "../cplusplus-internal.h"

namespace cplusplus
{

namespace
{

enum keyword_flag : uint32_t {
    KEYWORD_KEYALL = 1u << 0,
    KEYWORD_KEYC99 = 1u << 1,
    KEYWORD_KEYC23 = 1u << 2,
    KEYWORD_KEYCXX = 1u << 3,
    KEYWORD_KEYCXX11 = 1u << 4,
    KEYWORD_KEYCXX20 = 1u << 5,
    KEYWORD_KEYGNU = 1u << 6,
    KEYWORD_KEYMS = 1u << 7,
    KEYWORD_KEYMSCOMPAT = 1u << 8,
    KEYWORD_KEYNOCXX = 1u << 9,
    KEYWORD_KEYNOMS18 = 1u << 10,
    KEYWORD_KEYCOROUTINES = 1u << 11,
    KEYWORD_KEYMODULES = 1u << 12,
    KEYWORD_BOOLSUPPORT = 1u << 13,
    KEYWORD_WCHARSUPPORT = 1u << 14,
    KEYWORD_CHAR8SUPPORT = 1u << 15,
    KEYWORD_HALFSUPPORT = 1u << 16,
    /* [lex.key]/2, Table 6: an alternative representation of an operator */
    KEYWORD_OPERATOR = 1u << 17,
    /* OpenCL, HLSL, CUDA, AltiVec, Objective-C, ...: never C++ here */
    KEYWORD_OTHER = 1u << 18,
};

struct keyword_description {
    const char *spelling;
    const char *clang_name;
    uint32_t flags;
};

constexpr keyword_description keywords[] = {
#define KEYWORD(spelling, clang_name, flags) {spelling, clang_name, flags},
#include "lex.key.inc"
#undef KEYWORD
};

constexpr int keyword_count = sizeof keywords / sizeof *keywords;

/* clang's KeywordStatus, in its order: the strongest condition wins */
enum class keyword_status { unknown, disabled, future, extension, enabled };

keyword_status flag_status(const implementation_defined_behavior &b,
                           uint32_t flag)
{
    switch (flag) {
    case KEYWORD_KEYCXX:
    case KEYWORD_OPERATOR:
    case KEYWORD_BOOLSUPPORT:
    case KEYWORD_WCHARSUPPORT:
        return keyword_status::enabled;
    case KEYWORD_KEYCXX11:
        if (b.has(language_feature::cplusplus_11_keywords))
            return keyword_status::enabled;
        return keyword_status::future;
    case KEYWORD_KEYCXX20:
        if (b.has(language_feature::cplusplus_20_keywords))
            return keyword_status::enabled;
        return keyword_status::future;
    case KEYWORD_KEYCOROUTINES:
        if (b.has(language_feature::cplusplus_20_keywords))
            return keyword_status::enabled;
        return keyword_status::unknown;
    case KEYWORD_KEYGNU:
        if (b.gnu_extensions)
            return keyword_status::extension;
        return keyword_status::unknown;
    case KEYWORD_KEYMS:
        if (b.microsoft_extensions)
            return keyword_status::extension;
        return keyword_status::unknown;
    case KEYWORD_KEYMSCOMPAT:
        if (b.microsoft_compatibility)
            return keyword_status::enabled;
        return keyword_status::unknown;
    case KEYWORD_CHAR8SUPPORT:
        if (b.char8_t_keyword)
            return keyword_status::enabled;
        if (b.has(language_feature::char8_t_type))
            return keyword_status::unknown;
        return keyword_status::future;
    default:
        /* KEYC99, KEYC23, KEYNOCXX (C only), KEYMODULES (clang makes import
           and module identifiers and handles them in phase 4), HALFSUPPORT
           (OpenCL), KEYNOMS18 (only disables) and the other languages' */
        return keyword_status::unknown;
    }
}

keyword_status keyword_status_of(const implementation_defined_behavior &b,
                                 uint32_t flags)
{
    if (flags == KEYWORD_KEYALL)
        return keyword_status::enabled;
    keyword_status status = keyword_status::unknown;
    for (uint32_t f = flags; f; f &= f - 1) {
        keyword_status s = flag_status(b, f & -f);
        if (s > status)
            status = s;
    }
    if (flags & KEYWORD_KEYALL)
        status = keyword_status::enabled;
    if (status == keyword_status::unknown)
        return keyword_status::disabled;
    return status;
}

} // namespace

VPRIV int lookup_keyword(const implementation_defined_behavior &behavior,
                         const char *spelling, size_t length)
{
    int lo = 0, hi = keyword_count;
    while (lo < hi) {
        int mid = (lo + hi) / 2;
        const char *k = keywords[mid].spelling;
        size_t n = strlen(k);
        size_t common = n;
        if (length < common)
            common = length;
        int c = memcmp(k, spelling, common);
        if (!c && n < length)
            c = -1;
        else if (!c && n > length)
            c = 1;
        if (!c) {
            /* [ext.clang] clang_no_operator_names: -fno-operator-names
               makes and, or, ... identifiers */
            if ((keywords[mid].flags & KEYWORD_OPERATOR) &&
                !behavior.operator_names)
                return -1;
            keyword_status s = keyword_status_of(behavior, keywords[mid].flags);
            if (s == keyword_status::enabled || s == keyword_status::extension)
                return mid;
            return -1;
        }
        if (c < 0)
            lo = mid + 1;
        else
            hi = mid;
    }
    return -1;
}

VPRIV const char *keyword_clang_name(int keyword)
{
    return keywords[keyword].clang_name;
}

VPRIV int keyword_table_size()
{
    return keyword_count;
}

VPRIV const char *keyword_spelling(int keyword)
{
    return keywords[keyword].spelling;
}

VPRIV bool keyword_is_alternative_representation(int keyword,
                                                 preprocessing_op_or_punc *op)
{
    if (!(keywords[keyword].flags & KEYWORD_OPERATOR))
        return false;
    for (int i = 0; i < static_cast<int>(preprocessing_op_or_punc::count); i++)
        if (!strcmp(preprocessing_op_or_punc_clang_name(
                        static_cast<preprocessing_op_or_punc>(i)),
                    keywords[keyword].clang_name)) {
            *op = preprocessing_op_or_punc_primary(
                static_cast<preprocessing_op_or_punc>(i));
            return true;
        }
    return false;
}

} // namespace cplusplus
