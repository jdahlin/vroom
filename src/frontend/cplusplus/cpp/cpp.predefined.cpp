/*
 * cpp.predefined.cpp: [cpp.predefined] Predefined macro names, and the
 * operators of conditional inclusion that act as macro names ([cpp.cond]/7:
 * "The #ifdef and #ifndef directives, and the defined conditional
 * inclusion operator, shall treat __has_include and __has_cpp_attribute as
 * if they were the names of defined macros").
 *
 * The macros whose replacement never changes ([cpp.predefined]/3: "The
 * values of the predefined macros (except for __FILE__ and __LINE__) remain
 * constant throughout the translation unit") are [impl.clang] clang's for the documented
 * target in the edition and mode in effect (cpp/cpp.predefined.inc, taken
 * from clang once), defined by a file of #define lines as clang's
 * "<built-in>".  [cpp.predefined]/4: "Any other predefined macro names shall
 * begin with a leading underscore followed by an uppercase letter or a
 * second underscore": [ext.gnu] clang's include linux and unix in GNU modes.
 */
#include <stdio.h>

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

/* [cpp.predefined]/1's macros whose replacement depends on where they are
   used, and the operators of [cpp.cond] and [cpp.pragma.op] that act as
   macro names; then the extensions' */
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
        {"_Pragma", dynamic_macro::pragma_operator, false},
        {"__has_include", dynamic_macro::has_include, true},
        {"__has_cpp_attribute", dynamic_macro::has_cpp_attribute, true},
    };
    for (const auto &d : dynamic)
        define_dynamic(d.name, d.dynamic, d.function_like);
    if (behavior.has(extension::gnu_common_predefined_macros))
        gnu_define_dynamic_macros(); /* [ext.gnu] */
    if (behavior.has(extension::clang_feature_checks))
        clang_define_dynamic_macros(); /* [ext.clang] */
}

/* a string literal of the characters of s, " and \ escaped */
void string_literal_of(const char *s, vbuffer *out)
{
    vbuffer_append(out, "\"", 1);
    for (; *s; s++) {
        if (*s == '"' || *s == '\\')
            vbuffer_append(out, "\\", 1);
        vbuffer_append(out, s, 1);
    }
    vbuffer_append(out, "\"", 1);
}

/*
 * [cpp.cond]:
 *
 *     has-attribute-expression:
 *         '__has_cpp_attribute (' pp-tokens ')'
 *
 * [cpp.cond]/5: "interpreting the pp-tokens, after macro expansion, as an
 * attribute-token": identifier or attribute-namespace :: identifier
 * ([dcl.attr.grammar]).  [ext.clang] The operand of clang's feature
 * checks too, which also take a string literal.  False (and the
 * diagnostic) if it has neither form.
 */
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
        /* [cpp.predefined]/1: "The presumed line number (within the current
           source file) of the current source line"; [impl.clang] the line
           of the end of the outermost invocation, which is the last token
           read from the source file */
        source_position p = files.position(last_file_location);
        vbuffer_format_append(text.get(), "%u", p.line);
        break;
    }
    case dynamic_macro::file: {
        /* [cpp.predefined]/1: "The presumed name of the current source file
           (a character string literal)" */
        source_position p =
            files.position(name.expansion ? name.expansion : name.location);
        string_literal_of(p.file_name, text.get());
        break;
    }
    case dynamic_macro::date:
    case dynamic_macro::time:
        /* [cpp.predefined]/1: "The date of translation of the source file",
           "The time of translation" */
        diagnostics.issue(diagnostic_id::warn_pp_date_time, name.location);
        vbuffer_string_append(
            text.get(), m.dynamic == dynamic_macro::date ? date : time_of_day);
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
        name.not_replaceable = true; /* [impl.clang] left as it is */
        return false;
    case dynamic_macro::has_cpp_attribute: {
        /* [cpp.cond]/5: "replaced by a non-zero pp-number matching the form
           of an integer-literal if the implementation supports an attribute
           with the name specified ..., and by 0 otherwise" */
        owned_buffer operand, scope;
        if (!feature_operand(name, operand, scope))
            return true;
        int64_t value =
            cpp_attribute_value(behavior, scope.data(), scope.size(),
                                operand.data(), operand.size());
        vbuffer_format_append(text.get(), value ? "%lldL" : "%lld",
                              (long long)value);
        break;
    }
    default:
        /* the extensions' */
        if (behavior.has(extension::gnu_common_predefined_macros) &&
            gnu_dynamic_macro(m, text)) /* [ext.gnu] */
            break;
        if (behavior.has(extension::clang_feature_checks)) { /* [ext.clang] */
            int done = clang_dynamic_macro(name, m, text);
            if (done == 2)
                return true;
            if (done == 1)
                break;
        }
        return false;
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
