/*
 * literal-operator.cpp: [over.literal] User-defined literals, the literal
 * operators, and the calls [lex.ext] makes of a user-defined-literal.
 *
 * A literal operator's name is operator""X as an identifier number (X its
 * literal suffix identifier), so declaration and lookup are those of any
 * function.  Not analysed: literal operator templates ([over.literal]/5,
 * declarations the parser keeps for P7), a raw literal operator called for
 * a user-defined-integer-literal and a user-defined-floating-point-literal
 * (whose long double value analysis does not compute).
 */
#include "../cplusplus-internal.h"
#include "frontend/cplusplus/sema.h"

namespace cplusplus
{

namespace
{

/* the literal suffix identifier of a spelling that ends in one after its
   last quote, empty if none */
void suffix_after_quote(const owned_buffer &spelling, owned_buffer &suffix)
{
    size_t i = spelling.size();
    while (i > 0 && spelling.data()[i - 1] != '"' &&
           spelling.data()[i - 1] != '\'')
        i--;
    vbuffer_append(suffix.get(), spelling.data() + i, spelling.size() - i);
}

} // namespace

uint32_t semantic_analysis::literal_operator_name(const char *suffix,
                                                  size_t length)
{
    owned_buffer rest;
    vbuffer_string_append(rest.get(), "\"\"");
    vbuffer_append(rest.get(), suffix, length);
    return operator_identifier(rest.data(), rest.size());
}

/*
 * The literal-operator-id of a declarator:
 *
 *     literal-operator-id:
 *         operator string-literal identifier
 *         operator user-defined-string-literal
 */
analysis_result semantic_analysis::literal_operator_id(LiteralOperatorId id,
                                                       declarator_meaning &m)
{
    owned_buffer suffix;
    if (id.size() == 3 && id[2].is_token() && id[2].is_identifier()) {
        /* /1: "The string-literal or user-defined-string-literal in a
           literal-operator-id shall have no encoding-prefix and shall
           contain no characters other than the implicit terminating
           '\0'." */
        owned_buffer quotes;
        tokens.spelling(id[1].token(), quotes.get());
        if (quotes.size() != 2)
            return ill_formed(id,
                              "the string-literal in a literal-operator-id "
                              "shall have no encoding-prefix and contain no "
                              "characters",
                              "over.literal");
        tokens.spelling(id[2].token(), suffix.get());
    } else if (id.size() == 2 && id[1].is_token()) {
        owned_buffer spelling;
        tokens.spelling(id[1].token(), spelling.get());
        suffix_after_quote(spelling, suffix);
    }
    if (!id.size() || !id[0].is_token() || !suffix.size())
        return not_supported(id, "over.literal");
    /* [usrlit.suffix]/1: "Literal suffix identifiers that do not start
       with an underscore are reserved for future standardization": the
       standard library's, and clang's rules for them, are not analysed */
    if (suffix.data()[0] != '_')
        return not_supported(id, "over.literal");
    m.has_id = true;
    m.id = id[0].token();
    m.literal_operator = true;
    tokens.name_token(m.id,
                      literal_operator_name(suffix.data(), suffix.size()));
    return analysis_result::well_formed;
}

analysis_result semantic_analysis::literal_operator_declaration(SyntaxRef n,
                                                                entity_index f)
{
    const entity &x = entity_table[f];
    /* [over.literal]/2: "A declaration whose declarator-id is a
       literal-operator-id shall be a declaration of a namespace-scope
       function or function template" */
    if (x.kind != entity_kind::function || !is_namespace(x.enclosing))
        return ill_formed(n,
                          "a declaration whose declarator-id is a "
                          "literal-operator-id shall be a declaration of a "
                          "namespace-scope function",
                          "over.literal");
    /* /6: "Literal operators and literal operator templates shall not have
       C language linkage." */
    if (x.c_language_linkage || c_language)
        return ill_formed(n,
                          "literal operators shall not have C language "
                          "linkage",
                          "over.literal");
    /* /3: "If a parameter has a default argument ([dcl.fct.default]), the
       program is ill-formed." */
    if (x.default_arguments)
        return ill_formed(n,
                          "a literal operator's parameter shall not have a "
                          "default argument",
                          "over.literal");
    /* /3: "The declaration of a literal operator shall have a
       parameter-declaration-clause equivalent to one of the following:
       const char*, unsigned long long int, long double, char, wchar_t,
       char8_t, char16_t, char32_t, const char*, std::size_t, const
       wchar_t*, std::size_t, const char8_t*, std::size_t, const
       char16_t*, std::size_t, const char32_t*, std::size_t" */
    const struct type &function = type_table[x.type.unqualified()];
    const qualified_type *parameters =
        type_table.parameters(x.type.unqualified());
    auto character = [](fundamental_type t) {
        return t == fundamental_type::char_ ||
               t == fundamental_type::wchar_t_ ||
               t == fundamental_type::char8_t_ ||
               t == fundamental_type::char16_t_ ||
               t == fundamental_type::char32_t_;
    };
    auto const_character_pointer = [&](qualified_type t) -> fundamental_type {
        const struct type &p = type_table[t.unqualified()];
        if (p.kind != type_kind::pointer || p.operand.cv() != cv_const)
            return fundamental_type::none;
        fundamental_type c = fundamental_of(p.operand);
        if (!character(c))
            return fundamental_type::none;
        return c;
    };
    bool fits = false;
    if (function.parameter_count == 1 && !function.variadic) {
        fundamental_type t = fundamental_of(parameters[0]);
        fits =
            t == fundamental_type::unsigned_long_long_int ||
            t == fundamental_type::long_double || character(t) ||
            const_character_pointer(parameters[0]) == fundamental_type::char_;
    } else if (function.parameter_count == 2 && !function.variadic) {
        /* std::size_t is unsigned long on the targets analysis has */
        fits =
            const_character_pointer(parameters[0]) != fundamental_type::none &&
            fundamental_of(parameters[1]) ==
                fundamental_type::unsigned_long_int;
    }
    if (!fits)
        return ill_formed(n,
                          "the declaration of a literal operator shall have "
                          "one of the parameter-declaration-clauses of "
                          "[over.literal]/3",
                          "over.literal");
    return analysis_result::well_formed;
}

/*
 * [lex.ext]/2: "A user-defined-literal is treated as a call to a literal
 * operator or literal operator template ([over.literal]).  To determine the
 * form of this call for a given user-defined-literal L with ud-suffix X,
 * the literal-operator-id whose literal suffix identifier is X is looked up
 * in the context of L using the rules for unqualified name lookup
 * ([basic.lookup.unqual]).  Let S be the set of declarations found by this
 * lookup.  S shall not be empty."
 */
analysis_result semantic_analysis::user_defined_literal(token_index t,
                                                        expression_meaning &m)
{
    const token &k = tokens[t];
    if (operators_held)
        /* a literal operator (template) analysis did not follow */
        return not_supported(current_declaration, "lex.ext");
    owned_buffer spelling, suffix, number;
    tokens.spelling(t, spelling.get());
    bool numeric = k.preprocessing.kind == preprocessing_token_kind::pp_number;
    if (numeric) {
        uint32_t at = k.preprocessing.ud_suffix_offset;
        if (!at || at >= spelling.size() ||
            spelling.size() != k.preprocessing.extent)
            return not_supported(current_declaration, "lex.ext");
        vbuffer_append(number.get(), spelling.data(), at);
        vbuffer_append(suffix.get(), spelling.data() + at,
                       spelling.size() - at);
    } else {
        suffix_after_quote(spelling, suffix);
    }
    if (!suffix.size())
        return not_supported(current_declaration, "lex.ext");
    uint32_t name = literal_operator_name(suffix.data(), suffix.size());
    lookup_result found = lookup_unqualified(name, lookup_filter::any);
    if (found.unknown || found.ambiguous)
        return not_supported(current_declaration, "lex.ext");
    if (!found.entity)
        return ill_formed_at_token(t,
                                   "the set of declarations found by the "
                                   "lookup of a user-defined-literal's "
                                   "literal operator shall not be empty",
                                   "lex.ext");
    if (entity_table[found.entity].kind != entity_kind::function ||
        !is_namespace(entity_table[found.entity].enclosing))
        return not_supported(current_declaration, "lex.ext");
    owned_array<entity_index> set;
    overload_set(found.entity, set);
    owned_array<expression_meaning> arguments;
    entity_index chosen = 0;
    auto with_parameter = [&](fundamental_type p) {
        for (entity_index f : set) {
            const struct type &ft =
                type_table[entity_table[f].type.unqualified()];
            if (ft.parameter_count == 1 &&
                fundamental_of(type_table.parameters(
                    entity_table[f].type.unqualified())[0]) == p)
                return f;
        }
        return entity_index(0);
    };
    if (numeric) {
        /* [lex.ext]/3: "If L is a user-defined-integer-literal, let n be the
           literal without its ud-suffix.  If S contains a literal operator
           with parameter type unsigned long long, the literal L is treated
           as a call of the form operator "" X(nULL)" */
        const char *s = number.data();
        size_t length = number.size();
        unsigned base = 10;
        size_t i = 0;
        if (length > 1 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
            base = 16;
            i = 2;
        } else if (length > 1 && s[0] == '0' && (s[1] == 'b' || s[1] == 'B')) {
            base = 2;
            i = 2;
        } else if (length > 1 && s[0] == '0') {
            base = 8;
            i = 1;
        }
        unsigned __int128 value = 0;
        for (; i < length; i++) {
            char c = s[i];
            unsigned digit = base;
            if (c == '\'')
                continue;
            if (c >= '0' && c <= '9')
                digit = static_cast<unsigned>(c - '0');
            else if (c >= 'a' && c <= 'f')
                digit = static_cast<unsigned>(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F')
                digit = static_cast<unsigned>(c - 'A' + 10);
            if (digit >= base)
                /* a user-defined-floating-point-literal ([lex.ext]/4) */
                return not_supported(current_declaration, "lex.ext");
            value = value * base + digit;
            if (value > UINT64_MAX)
                return not_supported(current_declaration, "lex.ext");
        }
        chosen = with_parameter(fundamental_type::unsigned_long_long_int);
        if (!chosen)
            /* a raw literal operator or a numeric literal operator
               template: "operator "" X("n")" */
            return not_supported(current_declaration, "lex.ext");
        arguments.append(
            integer_constant(fundamental_type::unsigned_long_long_int,
                             static_cast<uint64_t>(value), t));
    } else if (k.preprocessing.kind ==
               preprocessing_token_kind::user_defined_character_literal) {
        /* /6: "S shall contain a literal operator ([over.literal]) whose
           only parameter has the type of ch and the literal L is treated as
           a call of the form operator "" X(ch)" */
        if (!k.value_known || !fundamental_type_is_integral(k.type))
            return not_supported(current_declaration, "lex.ext");
        chosen = with_parameter(k.type);
        if (!chosen)
            return ill_formed_at_token(t,
                                       "S shall contain a literal operator "
                                       "whose only parameter has the type of "
                                       "the character literal",
                                       "lex.ext");
        arguments.append(
            integer_constant(k.type,
                             integer_wrap(tokens.files.behavior, k.type,
                                          tokens.files.value_of(k).value),
                             t));
    } else {
        /* /5: "let str be the literal without its ud-suffix and let len be
           the number of code units in str (i.e., its length excluding the
           terminating null character). ... Otherwise, the literal L is
           treated as a call of the form operator "" X(str, len)" */
        expression_meaning str;
        if (!k.value_known || !string_literal_meaning(t, str))
            return not_supported(current_declaration, "lex.ext");
        str.node = expression_node(semantic_kind::string_literal, str, t);
        arguments.append(str);
        arguments.append(
            integer_constant(fundamental_type::unsigned_long_int,
                             tokens.files.value_of(k).string_length, t));
        owned_array<overload_candidate> candidates;
        for (entity_index f : set) {
            overload_candidate c;
            c.function = f;
            c.type = entity_table[f].type;
            candidates.append(c);
        }
        size_t index = 0;
        analysis_result r = best_viable_function(
            current_declaration, candidates, nullptr, arguments, index);
        if (r != analysis_result::well_formed)
            return r;
        chosen = set[index];
    }
    /* the call's node is the literal's own: its first token */
    analysis_result r =
        call_function(current_declaration, t, chosen, arguments, m);
    if (r == analysis_result::well_formed)
        used(t, chosen);
    return r;
}

} // namespace cplusplus
