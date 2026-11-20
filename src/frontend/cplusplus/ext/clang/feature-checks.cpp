/*
 * feature-checks.cpp: [ext:clang_feature_checks] (clang Language
 * Extensions, Feature Checking Macros), function-like macros replaced by 1
 * or 0 (or a version) as clang has the thing named:
 *
 *   __has_feature(x) __has_extension(x)   a language feature, an extension
 *   __has_builtin(x)                       a builtin function or type trait
 *   __has_attribute(x)                     a GNU attribute __attribute__((x))
 *   __has_c_attribute(x)                   a C23 attribute [[x]]
 *   __has_declspec_attribute(x)            a Microsoft __declspec(x)
 *   __has_warning("-Wx")                   a warning group
 *   __is_identifier(x)                     x is no keyword
 *   __is_target_arch(x), __is_target_vendor(x), __is_target_os(x),
 *   __is_target_environment(x)             the target's parts
 *   __building_module(x)                   0: no modules are built
 *   __has_constexpr_builtin(x)             0: no builtin is constant yet
 *   __FLT_EVAL_METHOD__                    0 (clang_flt_eval_method)
 *
 * and [ext:clang_has_include_next] (__has_include_next, evaluated with
 * __has_include in if-section.cpp), clang_file_name_macro (__FILE_NAME__, the
 * presumed file name without its directory).  As [cpp.cond]/7 says of
 * __has_include, #ifdef sees each as a defined macro.
 */
#include <stdio.h>

#include "../../cplusplus-internal.h"

namespace cplusplus
{

void preprocessor::clang_define_dynamic_macros()
{
    static constexpr struct {
        const char *name;
        dynamic_macro dynamic;
    } checks[] = {
        {"__has_attribute", dynamic_macro::has_attribute},
        {"__has_c_attribute", dynamic_macro::has_c_attribute},
        {"__has_declspec_attribute", dynamic_macro::has_declspec_attribute},
        {"__has_builtin", dynamic_macro::has_builtin},
        {"__has_feature", dynamic_macro::has_feature},
        {"__has_extension", dynamic_macro::has_extension},
        {"__has_warning", dynamic_macro::has_warning},
        {"__is_identifier", dynamic_macro::is_identifier},
        {"__has_embed", dynamic_macro::has_embed},
        {"__is_target_arch", dynamic_macro::is_target_arch},
        {"__is_target_vendor", dynamic_macro::is_target_vendor},
        {"__is_target_os", dynamic_macro::is_target_os},
        {"__is_target_environment", dynamic_macro::is_target_environment},
        {"__is_target_variant_os", dynamic_macro::is_target_variant},
        {"__is_target_variant_environment", dynamic_macro::is_target_variant},
        {"__building_module", dynamic_macro::building_module},
    };
    for (const auto &c : checks)
        define_dynamic(c.name, c.dynamic, true);
    if (behavior.has(extension::clang_has_include_next))
        define_dynamic("__has_include_next", dynamic_macro::has_include_next,
                       true);
    if (behavior.has(extension::clang_file_name_macro))
        define_dynamic("__FILE_NAME__", dynamic_macro::file_name, false);
    if (behavior.has(extension::clang_flt_eval_method))
        define_dynamic("__FLT_EVAL_METHOD__", dynamic_macro::flt_eval_method,
                       false);
    define_dynamic("__has_constexpr_builtin",
                   dynamic_macro::has_constexpr_builtin, true);
}

static bool has_constexpr_builtin(const implementation_defined_behavior &b,
                                  const char *name, size_t length);
static bool target_is(const char *o, size_t n, const char *word)
{
    if (strlen(word) != n)
        return false;
    for (size_t i = 0; i < n; i++)
        if ((o[i] | 32) != (word[i] | 32) && o[i] != word[i])
            return false;
    return true;
}

int preprocessor::clang_dynamic_macro(preprocessing_token &name,
                                      const macro_definition &m,
                                      owned_buffer &text)
{
    if (m.dynamic == dynamic_macro::file_name) {
        source_location at = name.location;
        if (name.expansion)
            at = name.expansion;
        source_position p = files.position(at);
        const char *f = p.file_name;
        if (const char *slash = strrchr(f, '/'))
            f = slash + 1;
        string_literal_of(f, text.get());
        return 1;
    }
    if (m.dynamic == dynamic_macro::flt_eval_method) {
        vbuffer_append(text.get(), "0", 1);
        return 1;
    }
    owned_buffer operand, scope;
    if (!feature_operand(name, operand, scope))
        return 2;
    const char *o = operand.data();
    size_t n = operand.size();
    /* clang accepts __name__ for name */
    if (n > 4 && o[0] == '_' && o[1] == '_' && o[n - 1] == '_' &&
        o[n - 2] == '_' && m.dynamic != dynamic_macro::is_identifier)
        o += 2, n -= 4;
    int64_t value = 0;
    switch (m.dynamic) {
    case dynamic_macro::has_attribute:
        if (scope.size()) {
            /* clang: a scoped name is asked as [[scope::name]] */
            owned_buffer full;
            vbuffer_format_append(full.get(), "%.*s::%.*s", (int)scope.size(),
                                  scope.data(), (int)n, o);
            value = clang_cpp_attribute_value(behavior, full.data(),
                                              full.size(), false) != 0;
        } else
            value = has_gnu_attribute(behavior, o, n);
        break;
    case dynamic_macro::has_c_attribute: {
        owned_buffer full;
        if (scope.size())
            vbuffer_format_append(full.get(), "%.*s::", (int)scope.size(),
                                  scope.data());
        vbuffer_append(full.get(), o, n);
        value =
            clang_cpp_attribute_value(behavior, full.data(), full.size(), true);
        break;
    }
    case dynamic_macro::has_declspec_attribute:
        value = clang_declspec_attribute_value(behavior, o, n);
        break;
    case dynamic_macro::has_feature:
    case dynamic_macro::has_extension:
        value = has_feature(behavior, o, n,
                            m.dynamic == dynamic_macro::has_extension);
        break;
    case dynamic_macro::has_builtin: {
        /* clang counts its target checks as builtins */
        static constexpr const char *target_checks[] = {
            "__is_target_arch",       "__is_target_vendor",
            "__is_target_os",         "__is_target_environment",
            "__is_target_variant_os", "__is_target_variant_environment"};
        value = has_builtin(behavior, operand.data(), operand.size());
        for (const char *c : target_checks)
            value |= strlen(c) == operand.size() &&
                     !memcmp(c, operand.data(), operand.size());
        break;
    }
    case dynamic_macro::is_identifier:
        value = lookup_keyword(behavior, operand.data(), operand.size()) < 0;
        break;
    /* the documented target aarch64-unknown-linux-gnu, names compared
       ignoring case as clang's triple parser does */
    case dynamic_macro::is_target_arch:
        value = target_is(o, n, "aarch64") || target_is(o, n, "arm64");
        break;
    case dynamic_macro::is_target_vendor:
        value = target_is(o, n, "unknown");
        break;
    case dynamic_macro::is_target_os:
        value = target_is(o, n, "linux");
        break;
    case dynamic_macro::is_target_environment:
        value = target_is(o, n, "gnu");
        break;
    case dynamic_macro::is_target_variant:
        value = 0; /* a Darwin target's variant: none here */
        break;
    case dynamic_macro::has_constexpr_builtin:
        value = has_constexpr_builtin(behavior, operand.data(), operand.size());
        break;
    case dynamic_macro::has_warning:
        /* "-Wgroup": whether the group exists; the operand is a string */
        if (operand.data()[0] != '"') {
            diagnostics.issue(diagnostic_id::err_expected_string_literal,
                              name.location)
                << 0 << "'__has_warning'";
            return 2;
        }
        if (operand.size() > 4 && operand.data()[1] == '-' &&
            operand.data()[2] == 'W') {
            char group[64];
            snprintf(group, sizeof group, "%.*s", (int)operand.size() - 4,
                     operand.data() + 3);
            value = diagnostic_group_exists(group);
        }
        break;
    default:
        break;
    }
    vbuffer_format_append(text.get(), "%lld", (long long)value);
    return 1;
}

/* ---- the answers, from clang once (feature-checks.inc) ---------------- */

namespace
{

enum class check_kind : uint8_t {
    attribute,
    builtin,
    c_attribute,
    constexpr_builtin,
    cpp_attribute,
    declspec_attribute,
    extension,
    feature
};

struct feature_check {
    check_kind kind;
    const char *operand;
    int64_t value;
    uint64_t variants;
};

constexpr feature_check feature_checks[] = {
#define FEATURE_CHECK(kind, operand, value, variants)                          \
    {check_kind::kind, operand, value, variants},
#include "feature-checks.inc"
#undef FEATURE_CHECK
};

/* the variant bit of predefined-macros.inc's mode */
uint64_t check_variant(const implementation_defined_behavior &b)
{
    static constexpr standard_edition editions[] = {
        standard_edition::cplusplus_98, standard_edition::cplusplus_11,
        standard_edition::cplusplus_14, standard_edition::cplusplus_17,
        standard_edition::cplusplus_20, standard_edition::cplusplus_23,
        standard_edition::cplusplus_26};
    unsigned e = 0;
    for (unsigned i = 0; i < 7; i++)
        if (b.edition == editions[i])
            e = i;
    bool char8_default = b.edition >= standard_edition::cplusplus_20;
    return UINT64_C(1)
           << (((e * 2 + b.gnu_extensions) * 2 + b.microsoft_extensions) * 2 +
               (b.char8_t_keyword != char8_default));
}

int compare_check(check_kind kind, const char *name, size_t n,
                  const feature_check &c)
{
    if (kind < c.kind)
        return -1;
    if (kind > c.kind)
        return 1;
    int r = strncmp(name, c.operand, n);
    if (r)
        return r;
    if (c.operand[n])
        return -1;
    return 0;
}

int64_t answer(const implementation_defined_behavior &b, check_kind kind,
               const char *name, size_t n)
{
    size_t lo = 0, hi = sizeof feature_checks / sizeof feature_checks[0];
    uint64_t bit = check_variant(b);
    /* the rows of one operand are adjacent (one per value) */
    while (lo < hi) {
        size_t mid = (lo + hi) / 2;
        if (compare_check(kind, name, n, feature_checks[mid]) > 0)
            lo = mid + 1;
        else
            hi = mid;
    }
    for (; lo < sizeof feature_checks / sizeof feature_checks[0] &&
           !compare_check(kind, name, n, feature_checks[lo]);
         lo++)
        if (feature_checks[lo].variants & bit)
            return feature_checks[lo].value;
    return 0;
}

} // namespace

bool has_feature(const implementation_defined_behavior &b, const char *name,
                 size_t length, bool extension)
{
    check_kind kind = check_kind::feature;
    if (extension)
        kind = check_kind::extension;
    return answer(b, kind, name, length) != 0;
}

int64_t has_gnu_attribute(const implementation_defined_behavior &b,
                          const char *name, size_t length)
{
    return answer(b, check_kind::attribute, name, length);
}

/* builtins clang has and this front end does not, which libc++ tests
   with __has_builtin and has a fallback for: the answer is 0, so that
   libc++ takes the fallback (bench/cxx-libcxx-builtins.py lists them) */
static const char *const builtins_not_had[] = {
    "__array_extent",
    "__builtin_complex",
    "__builtin_invoke_r",
    "__builtin_clear_padding",
    "__builtin_coro_noop",
    "__builtin_is_implicit_lifetime",
    "__builtin_is_virtual_base_of",
    "__builtin_is_within_lifetime",
    "__builtin_lt_synthesizes_from_spaceship",
    "__builtin_operator_delete",
    "__builtin_operator_new",
    "__is_destructible",
    "__is_nothrow_destructible",
    "__is_trivially_equality_comparable",
    "__is_trivially_relocatable",
};

bool has_builtin(const implementation_defined_behavior &b, const char *name,
                 size_t length)
{
    for (const char *n : builtins_not_had)
        if (strlen(n) == length && !memcmp(n, name, length))
            return false;
    return answer(b, check_kind::builtin, name, length) != 0;
}

static bool has_constexpr_builtin(const implementation_defined_behavior &b,
                                  const char *name, size_t length)
{
    return answer(b, check_kind::constexpr_builtin, name, length) != 0;
}

/* [cpp.cond]/6: "For other attributes recognized by the implementation,
   the value is implementation-defined": clang's; operand is scope::name */
int64_t clang_cpp_attribute_value(const implementation_defined_behavior &b,
                                  const char *operand, size_t length,
                                  bool c_attribute)
{
    check_kind kind = check_kind::cpp_attribute;
    if (c_attribute)
        kind = check_kind::c_attribute;
    return answer(b, kind, operand, length);
}

int64_t clang_declspec_attribute_value(const implementation_defined_behavior &b,
                                       const char *name, size_t length)
{
    return answer(b, check_kind::declspec_attribute, name, length);
}

} // namespace cplusplus
