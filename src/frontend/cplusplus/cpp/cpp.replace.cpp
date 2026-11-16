/*
 * cpp.replace.cpp: [cpp.replace] Macro replacement: definitions, the
 * identifiers that name them ([cpp.replace]/8: "There is one name space for
 * macro names"), and the invocations of function-like macros.  Also
 * [cpp.scope] (#undef), too small for a file of its own.
 *
 *     control-line:
 *         '# define' identifier replacement-list new-line
 *         '# define' identifier lparen identifier-list_opt ')' replacement-list new-line
 *         '# define' identifier lparen '... )' replacement-list new-line
 *         '# define' identifier lparen identifier-list ', ... )' replacement-list new-line
 *         '# undef' identifier new-line
 *
 *     lparen:
 *         a '(' character not immediately preceded by white-space
 *
 * [impl.clang] Which diagnostics a malformed definition gets is clang's
 * choice (its PPDirectives.cpp).  [ext.gnu] gnu_named_variadic_macro:
 * #define f(args...).
 */
#include <stdio.h>

#include "../cplusplus-internal.h"

namespace cplusplus
{

/* ---- identifiers ------------------------------------------------------ */

uint32_t hash_spelling(const char *s, size_t n)
{
    uint32_t h = 2166136261u; /* FNV-1a */
    for (size_t i = 0; i < n; i++)
        h = (h ^ (unsigned char)s[i]) * 16777619u;
    return h;
}

uint32_t preprocessor::identifier(const char *s, size_t n)
{
    uint32_t h = hash_spelling(s, n);
    if (identifier_slots.size() < 2 * identifiers.size() + 16) {
        /* grow the table to keep it at most half full */
        size_t size = 1024;
        if (identifier_slots.size())
            size = identifier_slots.size() * 2;
        identifier_slots.resize(size);
        memset(identifier_slots.data(), 0, size * sizeof(uint32_t));
        for (uint32_t i = 0; i < identifiers.size(); i++) {
            size_t k = identifiers[i].hash & (size - 1);
            while (identifier_slots[k])
                k = (k + 1) & (size - 1);
            identifier_slots[k] = i + 1;
        }
    }
    size_t mask = identifier_slots.size() - 1;
    for (size_t k = h & mask;; k = (k + 1) & mask) {
        uint32_t slot = identifier_slots[k];
        if (!slot) {
            identifier_entry e{};
            e.spelling_offset = (uint32_t)identifier_spellings.size();
            e.length = (uint32_t)n;
            e.hash = h;
            e.macro = -1;
            memcpy(identifier_spellings.extend(n + 1), s, n);
            identifier_spellings[identifier_spellings.size() - 1] = 0;
            identifiers.append(e);
            identifier_slots[k] = (uint32_t)identifiers.size();
            return (uint32_t)identifiers.size() - 1;
        }
        const identifier_entry &e = identifiers[slot - 1];
        if (e.hash == h && e.length == n &&
            !memcmp(identifier_spellings.data() + e.spelling_offset, s, n))
            return slot - 1;
    }
}

namespace
{

void append_utf8(vbuffer *out, uint32_t c)
{
    unsigned char b[4];
    size_t n;
    if (c < 0x80)
        b[0] = (unsigned char)c, n = 1;
    else if (c < 0x800)
        b[0] = (unsigned char)(0xC0 | c >> 6),
        b[1] = (unsigned char)(0x80 | (c & 0x3F)), n = 2;
    else if (c < 0x10000)
        b[0] = (unsigned char)(0xE0 | c >> 12),
        b[1] = (unsigned char)(0x80 | (c >> 6 & 0x3F)),
        b[2] = (unsigned char)(0x80 | (c & 0x3F)), n = 3;
    else
        b[0] = (unsigned char)(0xF0 | c >> 18),
        b[1] = (unsigned char)(0x80 | (c >> 12 & 0x3F)),
        b[2] = (unsigned char)(0x80 | (c >> 6 & 0x3F)),
        b[3] = (unsigned char)(0x80 | (c & 0x3F)), n = 4;
    vbuffer_append(out, b, n);
}

/* the value of a hexadecimal digit */
uint32_t hexadecimal_value(char c)
{
    if (is_digit((unsigned char)c))
        return (uint32_t)(c - '0');
    return (uint32_t)((c | 32) - 'a' + 10);
}

/* the code point of the universal-character-name at s[i] ('\' 'u' or 'U'
   or 'N'), and its length; 0 if there is none */
size_t universal_character_name(const char *s, size_t n, size_t i, uint32_t *c)
{
    if (i + 1 >= n || s[i] != '\\')
        return 0;
    char kind = s[i + 1];
    size_t j = i + 2;
    uint32_t v = 0;
    if ((kind == 'u' || kind == 'N') && j < n && s[j] == '{') {
        size_t close = j + 1;
        while (close < n && s[close] != '}')
            close++;
        if (close >= n)
            return 0;
        if (kind == 'N')
            v = code_point_of_unicode_name(s + j + 1, close - j - 1);
        else
            for (size_t k = j + 1; k < close; k++) {
                if (!is_hexadecimal_digit((unsigned char)s[k]))
                    return 0;
                v = v * 16 + hexadecimal_value(s[k]);
            }
        *c = v;
        if (!v)
            return 0;
        return close + 1 - i;
    }
    size_t digits = 0;
    if (kind == 'u')
        digits = 4;
    else if (kind == 'U')
        digits = 8;
    if (!digits || j + digits > n)
        return 0;
    for (size_t k = j; k < j + digits; k++) {
        if (!is_hexadecimal_digit((unsigned char)s[k]))
            return 0;
        v = v * 16 + hexadecimal_value(s[k]);
    }
    *c = v;
    return 2 + digits;
}

} // namespace

/* [lex.charset]/2: a universal-character-name designates the character
   whose code point it gives, so ü and the character itself spell one
   identifier: the table keys on its UTF-8 */
uint32_t preprocessor::identifier(const preprocessing_token &t)
{
    spelling_buffer.clear();
    files.spelling(t, spelling_buffer.get());
    const char *s = spelling_buffer.data();
    size_t n = spelling_buffer.size();
    if (!memchr(s, '\\', n))
        return identifier(s, n);
    owned_buffer canonical;
    for (size_t i = 0; i < n;) {
        uint32_t c;
        size_t length = universal_character_name(s, n, i, &c);
        if (length) {
            append_utf8(canonical.get(), c);
            i += length;
        } else
            vbuffer_append(canonical.get(), s + i++, 1);
    }
    return identifier(canonical.data(), canonical.size());
}

const char *preprocessor::identifier_spelling(uint32_t id, size_t *n) const
{
    *n = identifiers[id].length;
    return identifier_spellings.data() + identifiers[id].spelling_offset;
}

/* [impl.clang] an identifier as clang prints one in a message: 'name' */
diagnostic_message &preprocessor::quoted(diagnostic_message &m,
                                         const preprocessing_token &t)
{
    owned_buffer s;
    vbuffer_append(s.get(), "'", 1);
    files.spelling(t, s.get());
    vbuffer_append(s.get(), "'", 1);
    return m.text(s.data(), s.size());
}

/* ---- [cpp.replace]/8: the macro name ----------------------------------- */

/*
 * The identifier after #define or #undef, or nullptr-like false if it
 * cannot be a macro name: [cpp.replace]'s footnote 2 "An alternative token
 * is not an identifier ... Therefore it is not possible to define a macro
 * whose name is the same as that of an alternative token";
 * [cpp.predefined]/4 makes defining "defined" undefined behavior, an error
 * in clang ([impl.clang]).
 */
bool preprocessor::check_macro_name(const preprocessing_token &name,
                                    bool definition)
{
    if (is_end(name)) {
        diagnostics.issue(diagnostic_id::err_pp_missing_macro_name,
                          name.location);
        return false;
    }
    if (!is_identifier(name)) {
        diagnostics.issue(diagnostic_id::err_pp_macro_not_identifier,
                          name.location);
        return false;
    }
    owned_buffer s;
    files.spelling(name, s.get());
    int k = lookup_keyword(behavior, s.data(), s.size());
    preprocessing_op_or_punc op;
    if (k >= 0 && keyword_is_alternative_representation(k, &op) &&
        !behavior.microsoft_compatibility) {
        char aka[8];
        snprintf(aka, sizeof aka, "'%s'",
                 preprocessing_op_or_punc_spelling(op));
        diagnostic_id id = diagnostic_id::err_pp_operator_used_as_macro_name;
        if (behavior.has(extension::microsoft_operator_name_macros))
            id = diagnostic_id::ext_pp_operator_used_as_macro_name;
        quoted(diagnostics.issue(id, name.location), name) << aka;
        if (!behavior.has(extension::microsoft_operator_name_macros))
            return false; /* [ext.microsoft] otherwise */
    }
    if (s.size() == 7 && !memcmp(s.data(), "defined", 7)) {
        diagnostics.issue(diagnostic_id::err_defined_macro_name, name.location);
        return false;
    }
    (void)definition;
    return true;
}

/* [cpp.predefined]/4: "If any of the pre-defined macro names in this
   subclause, or the identifier defined, is the subject of a #define or a
   #undef preprocessing directive, the behavior is undefined": [impl.clang] clang warns
   for its dynamic macros, and for __STDC*, __cplusplus and __cpp* from its
   predefined macros */
bool preprocessor::is_language_defined(const macro_definition &m) const
{
    if (m.dynamic != dynamic_macro::none)
        return true;
    if (!m.predefined)
        return false;
    size_t n;
    const char *s = identifier_spelling(m.name, &n);
    return (n >= 6 && !memcmp(s, "__STDC", 6)) ||
           (n == 11 && !memcmp(s, "__cplusplus", 11)) ||
           (n >= 5 && !memcmp(s, "__cpp", 5));
}

/*
 * [cpp.replace]/1: "Two replacement lists are identical if and only if the
 * preprocessing tokens in both have the same number, ordering, spelling,
 * and white-space separation, where all white-space separations are
 * considered identical."  /2: a function-like redefinition also needs "the
 * same number and spelling of parameters".
 */
bool preprocessor::identical_definitions(const macro_definition &a,
                                         const macro_definition &b) const
{
    if (a.function_like != b.function_like || a.variadic != b.variadic ||
        a.parameter_count != b.parameter_count || a.length != b.length)
        return false;
    for (uint16_t i = 0; i < a.parameter_count; i++)
        if (parameters[a.first_parameter + i] !=
            parameters[b.first_parameter + i])
            return false;
    owned_buffer x, y;
    for (uint32_t i = 0; i < a.length; i++) {
        const replacement_element &p = elements[a.first_element + i];
        const replacement_element &q = elements[b.first_element + i];
        if (i && (p.token.after_white_space || p.token.at_start_of_line) !=
                     (q.token.after_white_space || q.token.at_start_of_line))
            return false;
        x.clear();
        y.clear();
        files.spelling(p.token, x.get());
        files.spelling(q.token, y.get());
        if (x.size() != y.size() || memcmp(x.data(), y.data(), x.size()))
            return false;
    }
    return true;
}

/* the definition replacing a name's current one, or its first */
int32_t preprocessor::add_definition(const macro_definition &m)
{
    macros.append(m);
    int32_t index = (int32_t)macros.size() - 1;
    identifiers[m.name].macro = index;
    definition_order.append(index);
    return index;
}

/*
 * [cpp.replace]/10 and /12: "A preprocessing directive of the form # define
 * identifier replacement-list new-line defines an object-like macro"; with
 * an lparen and an identifier-list, a function-like macro.
 */
void preprocessor::directive_define(const preprocessing_token &directive)
{
    (void)directive;
    preprocessing_token name = directive_token();
    if (!check_macro_name(name, true)) {
        skip_rest_of_directive();
        return;
    }
    macro_definition m;
    m.name = identifier(name);
    m.location = name.location;
    m.predefined = inclusions.back().file->index == predefines_file;
    owned_array<uint32_t> names;

    preprocessing_token t = directive_token();
    if (is_end(t)) {
    } else if (is_operator(t, preprocessing_op_or_punc::LEFT_PARENTHESIS) &&
               !t.after_white_space) {
        m.function_like = true;
        if (!parameter_list(m, names)) {
            skip_rest_of_directive();
            return;
        }
        t = directive_token();
    } else if (!t.after_white_space) {
        /* [cpp.replace]/4: "There shall be white-space between the
           identifier and the replacement list in the definition of an
           object-like macro." */
        if (behavior.has(language_feature::variadic_macros))
            diagnostics.issue(
                diagnostic_id::ext_c99_whitespace_required_after_macro_name,
                t.location);
        else if (t.kind == preprocessing_token_kind::other_character ||
                 is_operator(t, preprocessing_op_or_punc::AT))
            diagnostics.issue(
                diagnostic_id::ext_missing_whitespace_after_macro_name,
                t.location);
        else
            diagnostics.issue(
                diagnostic_id::warn_missing_whitespace_after_macro_name,
                t.location);
    }

    /* [cpp.replace]/8: "Any white-space characters preceding or following
       the replacement list of preprocessing tokens are not considered part
       of the replacement list" */
    m.first_element = (uint32_t)elements.size();
    uint32_t va_args = identifier("__VA_ARGS__", 11),
             va_opt = identifier("__VA_OPT__", 10);
    int va_opt_depth = -1; /* parentheses inside __VA_OPT__ ( ... ) */
    size_t va_opt_start = 0;
    bool first = true;
    for (; !is_end(t); t = directive_token(), first = false) {
        replacement_element e{t, -1};
        if (first)
            e.token.after_white_space = false;
        e.token.at_start_of_line = false;
        if (is_identifier(t)) {
            uint32_t id = identifier(t);
            for (uint16_t i = 0; i < names.size(); i++)
                if (names[i] == id)
                    e.parameter = (int16_t)i;
            /* [cpp.replace]/6: "The identifiers __VA_ARGS__ and __VA_OPT__
               shall occur only in the replacement-list of a function-like
               macro that uses the ellipsis notation in the parameters" */
            if (id == va_args && (!m.variadic || m.named_variadic))
                diagnostics.issue(diagnostic_id::ext_pp_bad_vaargs_use,
                                  t.location);
            if (id == va_opt && m.variadic && !m.named_variadic) {
                if (va_opt_depth >= 0) {
                    diagnostics.issue(diagnostic_id::err_pp_vaopt_nested_use,
                                      t.location);
                    skip_rest_of_directive();
                    return;
                }
                elements.append(e);
                t = directive_token();
                if (!is_operator(t,
                                 preprocessing_op_or_punc::LEFT_PARENTHESIS)) {
                    diagnostics.issue(
                        diagnostic_id::err_pp_missing_lparen_in_vaopt_use,
                        t.location);
                    elements.resize(m.first_element);
                    skip_rest_of_directive();
                    return;
                }
                e = {t, -1};
                e.token.at_start_of_line = false;
                elements.append(e);
                va_opt_depth = 0;
                va_opt_start = elements.size();
                continue;
            } else if (id == va_opt)
                diagnostics.issue(diagnostic_id::ext_pp_bad_vaopt_use,
                                  t.location);
        }
        if (va_opt_depth >= 0) {
            /* [cpp.subst]/4: "its closing ) is determined by skipping
               intervening pairs of matching left and right parentheses" */
            if (is_operator(t, preprocessing_op_or_punc::HASH_HASH) &&
                elements.size() == va_opt_start) {
                diagnostics.issue(diagnostic_id::err_vaopt_paste_at_start,
                                  t.location);
                elements.resize(m.first_element);
                skip_rest_of_directive();
                return;
            }
            if (is_operator(t, preprocessing_op_or_punc::LEFT_PARENTHESIS))
                va_opt_depth++;
            else if (is_operator(t,
                                 preprocessing_op_or_punc::RIGHT_PARENTHESIS) &&
                     va_opt_depth-- == 0) {
                if (elements.size() > va_opt_start &&
                    is_operator(elements[elements.size() - 1].token,
                                preprocessing_op_or_punc::HASH_HASH)) {
                    diagnostics.issue(diagnostic_id::err_vaopt_paste_at_end,
                                      t.location);
                    elements.resize(m.first_element);
                    skip_rest_of_directive();
                    return;
                }
                va_opt_depth = -1;
            }
        }
        /* [cpp.stringize]/1: "Each # preprocessing token in the replacement
           list for a function-like macro shall be followed by a parameter as
           the next preprocessing token in the replacement list." */
        if (m.function_like && is_operator(t, preprocessing_op_or_punc::HASH)) {
            elements.append(e);
            t = directive_token();
            int16_t parameter = -1;
            bool is_va_opt = false;
            if (is_identifier(t)) {
                uint32_t id = identifier(t);
                for (uint16_t i = 0; i < names.size(); i++)
                    if (names[i] == id)
                        parameter = (int16_t)i;
                is_va_opt = id == va_opt && m.variadic && !m.named_variadic;
            }
            if (parameter < 0 && !is_va_opt) {
                diagnostics.issue(diagnostic_id::err_pp_stringize_not_parameter,
                                  t.location)
                    << 0;
                elements.resize(m.first_element);
                skip_rest_of_directive();
                return;
            }
            if (is_va_opt) {
                /* the next iteration reads the __VA_OPT__ itself */
                elements.append({t, -1});
                elements[elements.size() - 1].token.at_start_of_line = false;
                t = directive_token();
                if (!is_operator(t,
                                 preprocessing_op_or_punc::LEFT_PARENTHESIS)) {
                    diagnostics.issue(
                        diagnostic_id::err_pp_missing_lparen_in_vaopt_use,
                        t.location);
                    elements.resize(m.first_element);
                    skip_rest_of_directive();
                    return;
                }
                elements.append({t, -1});
                elements[elements.size() - 1].token.at_start_of_line = false;
                va_opt_depth = 0;
                va_opt_start = elements.size();
                continue;
            }
            e = {t, parameter};
            e.token.at_start_of_line = false;
        }
        elements.append(e);
    }
    if (va_opt_depth >= 0) {
        diagnostics.issue(diagnostic_id::err_pp_expected_after, t.location)
            << "'__VA_OPT__'"
            << "')'";
        elements.resize(m.first_element);
        return;
    }
    m.length = (uint32_t)(elements.size() - m.first_element);
    m.first_parameter = (uint32_t)parameters.size();
    m.parameter_count = (uint16_t)names.size();
    for (uint32_t id : names)
        parameters.append(id);

    /* [cpp.concat]/1: "A ## preprocessing token shall not occur at the
       beginning or at the end of a replacement list for either form of
       macro definition." */
    if (m.length) {
        const replacement_element &a = elements[m.first_element];
        const replacement_element &z = elements[m.first_element + m.length - 1];
        if (is_operator(a.token, preprocessing_op_or_punc::HASH_HASH)) {
            diagnostics.issue(diagnostic_id::err_paste_at_start,
                              a.token.location);
            elements.resize(m.first_element);
            return;
        }
        if (is_operator(z.token, preprocessing_op_or_punc::HASH_HASH)) {
            diagnostics.issue(diagnostic_id::err_paste_at_end,
                              z.token.location);
            elements.resize(m.first_element);
            return;
        }
    }

    /* [cpp.replace]/2: "An identifier currently defined as an object-like
       macro (see below) may be redefined by another #define preprocessing
       directive provided that the second definition is an object-like macro
       definition and the two replacement lists are identical, otherwise the
       program is ill-formed": [impl.clang] clang warns */
    int32_t previous = identifiers[m.name].macro;
    if (previous >= 0 && !system_header_at(name.location)) {
        const macro_definition &old = macros[previous];
        if (is_language_defined(old))
            diagnostics.issue(diagnostic_id::ext_pp_redef_builtin_macro,
                              name.location);
        /* [impl.clang] the other predefined macros may be redefined
           without a warning */
        else if (!old.predefined && !identical_definitions(m, old)) {
            quoted(diagnostics.issue(diagnostic_id::ext_pp_macro_redef,
                                     name.location),
                   name);
            diagnostics.issue(diagnostic_id::note_previous_definition,
                              old.location);
        }
    }
    if (previous >= 0)
        warn_unused_macro(previous);
    int32_t index = add_definition(m);
    /* clang's -Wunused-macros: a macro of the main file, as the severity is
       where it is defined */
    if (inclusions.size() == 1 && !m.predefined &&
        inclusions.back().file == main_file) {
        diagnostic_severity s =
            diagnostics.severity_now(diagnostic_id::pp_macro_not_used);
        if (s != diagnostic_severity::ignored)
            unused_candidates.append({index, s});
    }
}

/* the warning of a macro defined and never used, when its definition
   ends (#undef, a redefinition, the end of the main file) */
void preprocessor::warn_unused_macro(int32_t macro)
{
    for (size_t i = 0; i < unused_candidates.size(); i++)
        if (unused_candidates[i].macro == macro) {
            if (!macros[macro].used)
                diagnostics.issue_as(diagnostic_id::pp_macro_not_used,
                                     macros[macro].location,
                                     unused_candidates[i].severity);
            unused_candidates[i] = unused_candidates.back();
            unused_candidates.resize(unused_candidates.size() - 1);
            return;
        }
}

void preprocessor::warn_unused_macros()
{
    while (!unused_candidates.empty())
        warn_unused_macro(unused_candidates[0].macro);
}

/*
 *     identifier-list:
 *         identifier
 *         identifier-list ',' identifier
 *
 * after the lparen; [cpp.replace]/7: "A parameter identifier in a
 * function-like macro shall be uniquely declared within its scope."
 */
bool preprocessor::parameter_list(macro_definition &m,
                                  owned_array<uint32_t> &names)
{
    for (;;) {
        preprocessing_token t = directive_token();
        if (is_operator(t, preprocessing_op_or_punc::RIGHT_PARENTHESIS)) {
            if (names.empty())
                return true;
            diagnostics.issue(diagnostic_id::err_pp_expected_ident_in_arg_list,
                              t.location);
            return false;
        }
        if (is_operator(t, preprocessing_op_or_punc::ELLIPSIS)) {
            /* [cpp.replace]/12: '# define' identifier lparen '...' ')';
               C++11's (N1653), [ext.clang] clang_later_edition_features in
               C++98 */
            if (behavior.has(language_feature::variadic_macros))
                diagnostics.issue(
                    diagnostic_id::warn_cxx98_compat_variadic_macro,
                    t.location);
            else
                diagnostics.issue(diagnostic_id::ext_variadic_macro,
                                  t.location);
            t = directive_token();
            if (!is_operator(t, preprocessing_op_or_punc::RIGHT_PARENTHESIS)) {
                diagnostics.issue(
                    diagnostic_id::err_pp_missing_rparen_in_macro_def,
                    t.location);
                return false;
            }
            /* [cpp.subst]/2: "An identifier __VA_ARGS__ that occurs in the
               replacement list shall be treated as if it were a parameter" */
            names.append(identifier("__VA_ARGS__", 11));
            m.variadic = true;
            return true;
        }
        if (is_end(t)) {
            diagnostics.issue(diagnostic_id::err_pp_missing_rparen_in_macro_def,
                              t.location);
            return false;
        }
        if (!is_identifier(t)) {
            diagnostics.issue(diagnostic_id::err_pp_invalid_tok_in_arg_list,
                              t.location);
            return false;
        }
        uint32_t id = identifier(t);
        for (uint32_t n : names)
            if (n == id) {
                quoted(diagnostics.issue(
                           diagnostic_id::err_pp_duplicate_name_in_arg_list,
                           t.location),
                       t);
                return false;
            }
        names.append(id);
        t = directive_token();
        if (is_operator(t, preprocessing_op_or_punc::RIGHT_PARENTHESIS))
            return true;
        if (is_operator(t, preprocessing_op_or_punc::COMMA))
            continue;
        if (is_operator(t, preprocessing_op_or_punc::ELLIPSIS) &&
            behavior.has(extension::gnu_named_variadic_macro)) {
            /* [ext.gnu] gnu_named_variadic_macro: the variable arguments
               named by the last parameter */
            diagnostics.issue(diagnostic_id::ext_named_variadic_macro,
                              t.location);
            t = directive_token();
            if (!is_operator(t, preprocessing_op_or_punc::RIGHT_PARENTHESIS)) {
                diagnostics.issue(
                    diagnostic_id::err_pp_missing_rparen_in_macro_def,
                    t.location);
                return false;
            }
            m.variadic = m.named_variadic = true;
            return true;
        }
        if (is_end(t))
            diagnostics.issue(diagnostic_id::err_pp_missing_rparen_in_macro_def,
                              t.location);
        else
            diagnostics.issue(diagnostic_id::err_pp_expected_comma_in_arg_list,
                              t.location);
        return false;
    }
}

/*
 * [cpp.scope]/2: "A preprocessing directive of the form # undef identifier
 * new-line causes the specified identifier no longer to be defined as a
 * macro name.  It is ignored if the specified identifier is not currently
 * defined as a macro name."
 */
void preprocessor::directive_undef(const preprocessing_token &directive)
{
    (void)directive;
    preprocessing_token name = directive_token();
    if (!check_macro_name(name, false)) {
        skip_rest_of_directive();
        return;
    }
    uint32_t id = identifier(name);
    int32_t m = identifiers[id].macro;
    if (m >= 0) {
        if (is_language_defined(macros[m]) && !system_header_at(name.location))
            diagnostics.issue(diagnostic_id::ext_pp_undef_builtin_macro,
                              name.location);
        warn_unused_macro(m);
        identifiers[id].macro = -1;
    }
    expect_end_of_directive("undef");
}

/* ---- [cpp.replace]/12-15: invocations of function-like macros ---------- */

/*
 * [cpp.replace]/12: "Each subsequent instance of the function-like macro
 * name followed by a ( as the next preprocessing token introduces the
 * sequence of preprocessing tokens that is replaced by the replacement list
 * ...  The replaced sequence of preprocessing tokens is terminated by the
 * matching ) preprocessing token, skipping intervening matched pairs of left
 * and right parenthesis preprocessing tokens.  Within the sequence of
 * preprocessing tokens making up an invocation of a function-like macro,
 * new-line is considered a normal white-space character."
 *
 * [cpp.replace]/13: "The individual arguments within the list are separated
 * by comma preprocessing tokens, but comma preprocessing tokens between
 * matching inner parentheses do not separate arguments."  /15: the variable
 * arguments are one, commas included.
 */
bool preprocessor::collect_arguments(const preprocessing_token &name,
                                     const macro_definition &m,
                                     owned_array<argument> &arguments,
                                     owned_array<preprocessing_token> &tokens,
                                     preprocessing_token *right_parenthesis)
{
    unsigned depth = 0;
    uint32_t begin = (uint32_t)tokens.size();
    variadic_elided = false;
    collecting_arguments++;
    for (;;) {
        preprocessing_token t;
        bool more = read_unexpanded(t);
        if (!more || t.kind == preprocessing_token_kind::end_of_file) {
            collecting_arguments--;
            diagnostics.issue(diagnostic_id::err_unterm_macro_invoc,
                              name.location);
            if (more)
                unread_file_token(t);
            return false;
        }
        if (is_operator(t, preprocessing_op_or_punc::LEFT_PARENTHESIS))
            depth++;
        else if (is_operator(t, preprocessing_op_or_punc::RIGHT_PARENTHESIS)) {
            if (depth-- == 0) {
                arguments.append({begin, (uint32_t)tokens.size(), -1, -1});
                *right_parenthesis = t;
                break;
            }
        } else if (is_operator(t, preprocessing_op_or_punc::COMMA) &&
                   depth == 0 &&
                   !(m.variadic && arguments.size() + 1 >= m.parameter_count)) {
            arguments.append({begin, (uint32_t)tokens.size(), -1, -1});
            begin = (uint32_t)tokens.size();
            continue;
        }
        tokens.append(t);
    }
    collecting_arguments--;

    /* f() has one empty argument when f has one parameter, none when it has
       none */
    if (m.parameter_count == 0 && arguments.size() == 1 &&
        arguments[0].begin == arguments[0].end)
        arguments.clear();
    if (m.variadic && m.parameter_count == 1 && arguments.size() == 1 &&
        arguments[0].begin == arguments[0].end)
        variadic_elided = true;
    if (!behavior.has(language_feature::variadic_macros))
        for (const argument &a : arguments)
            if (a.begin == a.end && m.parameter_count)
                diagnostics.issue(diagnostic_id::ext_empty_fnmacro_arg,
                                  right_parenthesis->location);

    /* [cpp.replace]/5: "the number of arguments (including those arguments
       consisting of no preprocessing tokens) in an invocation of a
       function-like macro shall equal the number of parameters in the macro
       definition.  Otherwise, there shall be at least as many arguments in
       the invocation as there are parameters in the macro definition
       (excluding the ...)." */
    size_t n = arguments.size();
    if (n > m.parameter_count) {
        const argument &extra = arguments[m.parameter_count];
        source_location at = right_parenthesis->location;
        if (extra.begin < extra.end)
            at = tokens[extra.begin].location;
        diagnostics.issue(diagnostic_id::err_too_many_args_in_macro_invoc, at);
        quoted(diagnostics.issue(diagnostic_id::note_macro_here, m.location),
               name);
        return false;
    }
    if (n < m.parameter_count) {
        if (m.variadic && n + 1 == m.parameter_count) {
            /* the variable arguments left out: C++20's [cpp.replace]/5;
               [ext.clang] clang_later_edition_features before C++20 */
            if (!behavior.has(
                    language_feature::variadic_arguments_may_be_omitted))
                diagnostics.issue(diagnostic_id::ext_cxx_missing_varargs_arg,
                                  right_parenthesis->location);
            else
                diagnostics.issue(
                    diagnostic_id::warn_cxx17_compat_missing_varargs_arg,
                    right_parenthesis->location);
            uint32_t at = (uint32_t)tokens.size();
            arguments.append({at, at, -1, -1});
            variadic_elided = true;
        } else {
            diagnostics.issue(diagnostic_id::err_too_few_args_in_macro_invoc,
                              right_parenthesis->location);
            quoted(
                diagnostics.issue(diagnostic_id::note_macro_here, m.location),
                name);
            return false;
        }
    }
    return true;
}

} // namespace cplusplus
