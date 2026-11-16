/*
 * feature_checks.cpp: [ext.clang] clang_feature_checks (clang Language
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
 *
 * and [ext.clang] clang_has_include_next (__has_include_next, evaluated with
 * __has_include in cpp.cond.cpp), clang_file_name_macro (__FILE_NAME__, the
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
        {"__building_module", dynamic_macro::building_module},
    };
    for (const auto &c : checks)
        define_dynamic(c.name, c.dynamic, true);
    if (behavior.has(extension::clang_has_include_next))
        define_dynamic("__has_include_next", dynamic_macro::has_include_next,
                       true);
    if (behavior.has(extension::clang_file_name_macro))
        define_dynamic("__FILE_NAME__", dynamic_macro::file_name, false);
}

int preprocessor::clang_dynamic_macro(preprocessing_token &name,
                                      const macro_definition &m,
                                      owned_buffer &text)
{
    if (m.dynamic == dynamic_macro::file_name) {
        source_position p =
            files.position(name.expansion ? name.expansion : name.location);
        const char *f = strrchr(p.file_name, '/');
        string_literal_of(f ? f + 1 : p.file_name, text.get());
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
        value = scope.size() ? 0 : has_gnu_attribute(behavior, o, n);
        break;
    case dynamic_macro::has_feature:
    case dynamic_macro::has_extension:
        value = has_feature(behavior, o, n,
                            m.dynamic == dynamic_macro::has_extension);
        break;
    case dynamic_macro::has_builtin:
        value = has_builtin(behavior, operand.data(), operand.size());
        break;
    case dynamic_macro::is_identifier:
        value = lookup_keyword(behavior, operand.data(), operand.size()) < 0;
        break;
    case dynamic_macro::is_target_arch:
        value = (n == 7 && !memcmp(o, "aarch64", 7)) ||
                (n == 5 && !memcmp(o, "arm64", 5));
        break;
    case dynamic_macro::is_target_vendor:
        value = n == 7 && !memcmp(o, "unknown", 7);
        break;
    case dynamic_macro::is_target_os:
        value = n == 5 && !memcmp(o, "linux", 5);
        break;
    case dynamic_macro::is_target_environment:
        value = n == 3 && !memcmp(o, "gnu", 3);
        break;
    case dynamic_macro::has_warning:
        /* "-Wgroup": whether the group exists */
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

/* the data, from clang once: not generated yet, every answer is 0 */
bool has_feature(const implementation_defined_behavior &b, const char *name,
                 size_t length, bool extension)
{
    (void)b, (void)name, (void)length, (void)extension;
    return false;
}

int64_t has_gnu_attribute(const implementation_defined_behavior &b,
                          const char *name, size_t length)
{
    (void)b, (void)name, (void)length;
    return 0;
}

bool has_builtin(const implementation_defined_behavior &b, const char *name,
                 size_t length)
{
    (void)b, (void)name, (void)length;
    return false;
}

} // namespace cplusplus
