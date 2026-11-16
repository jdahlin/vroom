/*
 * cpp.predefined.cpp: [cpp.predefined] Predefined macro names, and the
 * operators of conditional inclusion that act as macro names ([cpp.cond]/7:
 * "The #ifdef and #ifndef directives, and the defined conditional
 * inclusion operator, shall treat __has_include and __has_cpp_attribute as
 * if they were the names of defined macros").
 *
 * The macros whose replacement never changes ([cpp.predefined]/3: "The
 * values of the predefined macros (except for __FILE__ and __LINE__) remain
 * constant throughout the translation unit") are clang's for the documented
 * target in the edition and mode in effect (cpp/cpp.predefined.inc, taken
 * from clang once), defined by a file of #define lines as clang's
 * "<built-in>".  [cpp.predefined]/4: "Any other predefined macro names shall
 * begin with a leading underscore followed by an uppercase letter or a
 * second underscore": clang's include linux and unix in GNU modes.
 */
#include <stdio.h>
#include <sys/stat.h>
#include <time.h>

#include "../cplusplus-internal.h"

namespace cplusplus
{

namespace
{

struct predefined_macro {
    uint64_t variants;
    const char *name, *replacement;
};

constexpr predefined_macro predefined_macros[] = {
#define PREDEFINED_MACRO(variants, name, replacement)                          \
    {variants, name, replacement},
#include "cpp.predefined.inc"
#undef PREDEFINED_MACRO
};

/* the bit of cpp.predefined.inc's mode */
unsigned predefined_variant(const implementation_defined_behavior &b)
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
    return ((e * 2 + b.gnu_extensions) * 2 + b.microsoft_extensions) * 2 +
           (b.char8_t_keyword != char8_default);
}

} // namespace

void preprocessor::write_predefined_macros(vbuffer *out) const
{
    uint64_t bit = UINT64_C(1) << predefined_variant(behavior);
    for (const predefined_macro &m : predefined_macros)
        if (m.variants & bit)
            vbuffer_format_append(out, "#define %s %s\n", m.name,
                                  m.replacement);
}

int32_t preprocessor::define_dynamic(const char *name, dynamic_macro dynamic,
                                     bool function_like)
{
    macro_definition m;
    m.name = identifier(name, strlen(name));
    m.dynamic = dynamic;
    m.function_like = function_like;
    m.predefined = true;
    return add_definition(m);
}

void preprocessor::define_dynamic_macros()
{
    static constexpr struct {
        const char *name;
        dynamic_macro dynamic;
        bool function_like;
    } dynamic[] = {
        {"__FILE__", dynamic_macro::file, false},
        {"__LINE__", dynamic_macro::line, false},
        {"__DATE__", dynamic_macro::date, false},
        {"__TIME__", dynamic_macro::time, false},
        {"__COUNTER__", dynamic_macro::counter, false},
        {"__INCLUDE_LEVEL__", dynamic_macro::include_level, false},
        {"__BASE_FILE__", dynamic_macro::base_file, false},
        {"__FILE_NAME__", dynamic_macro::file_name, false},
        {"__TIMESTAMP__", dynamic_macro::timestamp, false},
        {"_Pragma", dynamic_macro::pragma_operator, false},
        {"__has_include", dynamic_macro::has_include, true},
        {"__has_include_next", dynamic_macro::has_include_next, true},
        {"__has_cpp_attribute", dynamic_macro::has_cpp_attribute, true},
        {"__has_attribute", dynamic_macro::has_attribute, true},
        {"__has_c_attribute", dynamic_macro::has_c_attribute, true},
        {"__has_declspec_attribute", dynamic_macro::has_declspec_attribute,
         true},
        {"__has_builtin", dynamic_macro::has_builtin, true},
        {"__has_feature", dynamic_macro::has_feature, true},
        {"__has_extension", dynamic_macro::has_extension, true},
        {"__has_warning", dynamic_macro::has_warning, true},
        {"__is_identifier", dynamic_macro::is_identifier, true},
        {"__has_embed", dynamic_macro::has_embed, true},
        {"__is_target_arch", dynamic_macro::is_target_arch, true},
        {"__is_target_vendor", dynamic_macro::is_target_vendor, true},
        {"__is_target_os", dynamic_macro::is_target_os, true},
        {"__is_target_environment", dynamic_macro::is_target_environment, true},
        {"__building_module", dynamic_macro::building_module, true},
    };
    for (const auto &d : dynamic)
        define_dynamic(d.name, d.dynamic, d.function_like);
}

/* a string literal of the characters of s, " and \ escaped */
static void string_literal_of(const char *s, vbuffer *out)
{
    vbuffer_append(out, "\"", 1);
    for (; *s; s++) {
        if (*s == '"' || *s == '\\')
            vbuffer_append(out, "\\", 1);
        vbuffer_append(out, s, 1);
    }
    vbuffer_append(out, "\"", 1);
}

/* the identifier operand of __has_feature(x) and its kin: false (and the
   diagnostic) if there is none */
bool preprocessor::feature_operand(const preprocessing_token &name,
                                   owned_buffer &operand, owned_buffer &scope)
{
    preprocessing_token t;
    operand.clear();
    scope.clear();
    if (!read_unexpanded(t) ||
        !is_operator(t, preprocessing_op_or_punc::LEFT_PARENTHESIS)) {
        owned_buffer s;
        files.spelling(name, s.get());
        quoted(diagnostics.issue(diagnostic_id::err_pp_expected_after,
                                 name.location),
               name)
            << "'('";
        return false;
    }
    unsigned depth = 1;
    owned_array<preprocessing_token> inside;
    for (;;) {
        if (!read_expanded(t) ||
            t.kind == preprocessing_token_kind::end_of_file) {
            diagnostics.issue(diagnostic_id::err_pp_expected_after, t.location)
                << "'('"
                << "')'";
            return false;
        }
        if (is_operator(t, preprocessing_op_or_punc::LEFT_PARENTHESIS))
            depth++;
        else if (is_operator(t, preprocessing_op_or_punc::RIGHT_PARENTHESIS) &&
                 --depth == 0)
            break;
        inside.append(t);
    }
    /* identifier, or scope :: identifier (an attribute-token, [dcl.attr]) */
    if (inside.size() == 3 && is_identifier(inside[0]) &&
        is_operator(inside[1], preprocessing_op_or_punc::COLON_COLON) &&
        is_identifier(inside[2])) {
        files.spelling(inside[0], scope.get());
        files.spelling(inside[2], operand.get());
        return true;
    }
    if (inside.size() == 1 &&
        (is_identifier(inside[0]) ||
         inside[0].kind == preprocessing_token_kind::string_literal)) {
        files.spelling(inside[0], operand.get());
        return true;
    }
    diagnostics.issue(diagnostic_id::err_feature_check_malformed,
                      inside.empty() ? t.location : inside[0].location);
    return false;
}

/* the replacement of a dynamic macro, a single preprocessing token */
bool preprocessor::replace_dynamic(preprocessing_token &name,
                                   const macro_definition &m)
{
    owned_buffer text;
    switch (m.dynamic) {
    case dynamic_macro::line: {
        /* clang: the line of the end of the outermost invocation, which is
           the last token read from the source file */
        source_position p = files.position(last_file_location);
        vbuffer_format_append(text.get(), "%u", p.line);
        break;
    }
    case dynamic_macro::file:
    case dynamic_macro::file_name: {
        source_position p =
            files.position(name.expansion ? name.expansion : name.location);
        const char *f = p.file_name;
        if (m.dynamic == dynamic_macro::file_name && strrchr(f, '/'))
            f = strrchr(f, '/') + 1;
        string_literal_of(f, text.get());
        break;
    }
    case dynamic_macro::base_file:
        string_literal_of(main_file ? main_file->name.c_str() : "", text.get());
        break;
    case dynamic_macro::date:
    case dynamic_macro::time:
        diagnostics.issue(diagnostic_id::warn_pp_date_time, name.location);
        vbuffer_string_append(
            text.get(), m.dynamic == dynamic_macro::date ? date : time_of_day);
        break;
    case dynamic_macro::timestamp: {
        struct stat st;
        const char *path = inclusions.back().file->name.c_str();
        char stamp[64];
        if (stat(path, &st) == 0) {
            struct tm t;
            localtime_r(&st.st_mtime, &t);
            strftime(stamp, sizeof stamp, "\"%a %b %e %H:%M:%S %Y\"", &t);
        } else
            snprintf(stamp, sizeof stamp, "\"??? ??? ?? ??:??:?? ????\"");
        vbuffer_string_append(text.get(), stamp);
        break;
    }
    case dynamic_macro::counter:
        vbuffer_format_append(text.get(), "%u", counter++);
        break;
    case dynamic_macro::include_level:
        vbuffer_format_append(text.get(), "%u",
                              (unsigned)(inclusions.size() - 1));
        break;
    case dynamic_macro::pragma_operator:
        return pragma_operator(name);
    case dynamic_macro::has_include:
    case dynamic_macro::has_include_next:
    case dynamic_macro::has_embed:
        /* [cpp.cond]/7: "The identifiers __has_include and
           __has_cpp_attribute shall not appear in any context not mentioned
           in this subclause"; #if's evaluation reads them itself */
        if (in_condition)
            return false;
        quoted(diagnostics.issue(diagnostic_id::err_pp_directive_required,
                                 name.location),
               name);
        return true;
    default: {
        owned_buffer operand, scope;
        int64_t value = 0;
        if (!feature_operand(name, operand, scope))
            return true;
        const char *o = operand.data();
        size_t n = operand.size();
        /* clang accepts __name__ for name */
        if (n > 4 && o[0] == '_' && o[1] == '_' && o[n - 1] == '_' &&
            o[n - 2] == '_' && m.dynamic != dynamic_macro::is_identifier)
            o += 2, n -= 4;
        switch (m.dynamic) {
        case dynamic_macro::has_cpp_attribute:
            value =
                cpp_attribute_value(behavior, scope.data(), scope.size(), o, n);
            break;
        case dynamic_macro::has_attribute:
            value = scope.size() ? 0 : has_gnu_attribute(behavior, o, n);
            break;
        case dynamic_macro::has_feature:
        case dynamic_macro::has_extension:
            value = cplusplus::has_feature(
                behavior, o, n, m.dynamic == dynamic_macro::has_extension);
            break;
        case dynamic_macro::has_builtin:
            value = has_builtin(behavior, operand.data(), operand.size());
            break;
        case dynamic_macro::is_identifier: {
            value =
                lookup_keyword(behavior, operand.data(), operand.size()) < 0;
            break;
        }
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
        case dynamic_macro::has_warning: {
            /* "-Wgroup": whether the group exists */
            value = 0;
            if (operand.size() > 4 && operand.data()[1] == '-' &&
                operand.data()[2] == 'W') {
                char group[64];
                snprintf(group, sizeof group, "%.*s", (int)operand.size() - 4,
                         operand.data() + 3);
                value = diagnostic_group_exists(group);
            }
            break;
        }
        default:
            value = 0;
            break;
        }
        vbuffer_format_append(text.get(), "%lld", (long long)value);
        if (m.dynamic == dynamic_macro::has_cpp_attribute && value)
            vbuffer_append(text.get(), "L", 1);
        break;
    }
    }
    preprocessing_token t;
    spelled(text.data(), text.size(), &t);
    owned_array<preprocessing_token> result;
    result.append(t);
    finish_replacement(name, result);
    push_tokens(result.data(), result.size(), -1, false);
    return true;
}

/* clang's -dM: "#define NAME replacement" for each macro defined, in the
   order defined */
void preprocessor::write_definitions(vbuffer *out)
{
    for (int32_t index : definition_order) {
        const macro_definition &m = macros[index];
        if (identifiers[m.name].macro != index ||
            m.dynamic != dynamic_macro::none)
            continue;
        size_t n;
        const char *name = identifier_spelling(m.name, &n);
        vbuffer_format_append(out, "#define %.*s", (int)n, name);
        if (m.function_like) {
            vbuffer_append(out, "(", 1);
            for (uint16_t i = 0; i < m.parameter_count; i++) {
                if (i)
                    vbuffer_append(out, ",", 1);
                if (m.variadic && !m.named_variadic &&
                    i + 1 == m.parameter_count) {
                    vbuffer_append(out, "...", 3);
                    break;
                }
                const char *p =
                    identifier_spelling(parameters[m.first_parameter + i], &n);
                vbuffer_append(out, p, n);
                if (m.named_variadic && i + 1 == m.parameter_count)
                    vbuffer_append(out, "...", 3);
            }
            vbuffer_append(out, ")", 1);
        }
        vbuffer_append(out, " ", 1);
        for (uint32_t i = 0; i < m.length; i++) {
            const preprocessing_token &t = elements[m.first_element + i].token;
            if (i && t.after_white_space)
                vbuffer_append(out, " ", 1);
            files.spelling(t, out);
        }
        vbuffer_append(out, "\n", 1);
    }
}

} // namespace cplusplus
