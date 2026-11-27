/*
 * mangling/expressions.cpp: [abi:expressions] and [abi:mangling.literal]: what the four sources of
 * expressions share, and the refusals (doc/PLAN-mangling.md).
 */
#include "mangling-internal.h"

namespace cplusplus
{

/*
 * [abi:expressions]:
 *
 *     <expression> ::= cl <expression>+ E
 *                  ::= dt <expression> <unresolved-name>
 *                  ::= pt <expression> <unresolved-name>
 *                  ::= <function-param>
 *                  ::= <expr-primary>
 *
 * Binary arithmetic uses <binary operator-name> followed by both operands.
 * The literal form here is a decimal int.
 *
 * For a named callee or member, the supported <unresolved-name> is a
 * <simple-id> consisting of a <source-name>.
 *
 * [abi:mangle.function-param]:
 *
 *     <function-param> ::= fp <top-level CV-qualifiers> _
 *                      ::= fp <top-level CV-qualifiers> <parameter-2 non-negative number> _
 *
 * The function parameter forms below omit the CV-qualifiers.
 */
/* [abi:expressions]' <expr-primary> of a variable the template's scope
   declares, written unqualified: L_Z <encoding> E; false for another name */
bool itanium_mangler::expr_primary_of_variable(entity_index t,
                                               SyntaxElement name)
{
    if (!name.is_token() || !name.is_identifier() || !t)
        return false;
    uint32_t id = analysis.token_identifier(name.token());
    entity_index target = 0;
    for (entity_index s = analysis.entity_table[t].enclosing;;
         s = analysis.entity_table[s].enclosing) {
        target = analysis.entity_table.find(s, id);
        if (target || !s)
            break;
    }
    if (!target || analysis.entity_table[target].kind != entity_kind::variable)
        return false;
    enc.write(terminal::external_name);
    if (!encoding(target))
        return false;
    enc.write(terminal::end);
    return true;
}

/* the construct the mangler has no rule for, the first one only (the
   innermost that failed): what, in mangling_part */
bool itanium_mangler::refuse(const char *what)
{
    if (!refused) {
        refused = what;
        refused_in = mangling_part;
    }
    return false;
}

/* a dependent expression not mangled: its tokens as written (up to 60
   characters) and its kind */
void itanium_mangler::refuse_expression(SyntaxElement x)
{
    if (refused)
        return;
    refused_text.clear();
    vbuffer_string_append(refused_text.get(), "the expression '");
    /* (a token element's own token: it has no first_token) */
    token_index first = 0, end = 0;
    if (x.is_node()) {
        first = x.first_token();
        end = x.end_token();
    } else {
        first = x.token();
        end = first + 1;
    }
    for (token_index t = first; t < end; t++) {
        if (refused_text.size() > 70) {
            vbuffer_string_append(refused_text.get(), " ...");
            break;
        }
        if (t != first)
            vbuffer_string_append(refused_text.get(), " ");
        owned_buffer spelled;
        analysis.token_spelling(t, spelled.get());
        vbuffer_append(refused_text.get(), spelled.data(), spelled.size());
    }
    vbuffer_string_append(refused_text.get(), "'");
    if (x.is_node()) {
        vbuffer_string_append(refused_text.get(), " (");
        vbuffer_string_append(refused_text.get(), syntax_kind_name(x.kind()));
        vbuffer_string_append(refused_text.get(), ")");
    }
    vbuffer_append(refused_text.get(), "", 1);
    refuse(refused_text.data());
}

/* [abi:mangling.literal] an enumerator named in a dependent expression: its
   value, L <type> <value> E, the type its enumeration (as clang) */
bool itanium_mangler::expr_primary_of_enumerator(entity_index e)
{
    qualified_type of = analysis.entity_table[e].type;
    uint64_t magnitude = 0;
    bool negative = analysis.enumerator_value(e, magnitude);
    enc.write(terminal::expr_primary);
    if (!type(of))
        return false;
    if (negative)
        enc.write(terminal::negative);
    enc.number(magnitude);
    enc.write(terminal::end);
    return true;
}

/* [abi:mangling.literal] an integer-literal: L <type> <value> E, the type
   the first of [lex.icon]'s table that holds it (long is as wide as a
   pointer), the value in decimal */
bool itanium_mangler::expr_primary_of_integer_literal(token_index token)
{
    owned_buffer spelling;
    analysis.token_spelling(token, spelling.get());
    const char *p = spelling.data();
    const char *end = p + spelling.size();
    unsigned base = 10;
    if (end - p > 1 && p[0] == '0') {
        base = 8;
        if (p[1] == 'x' || p[1] == 'X') {
            base = 16;
            p += 2;
        } else if (p[1] == 'b' || p[1] == 'B') {
            base = 2;
            p += 2;
        }
    }
    uint64_t value = 0;
    bool digits = false;
    for (; p < end; p++) {
        unsigned digit = base;
        if (*p >= '0' && *p <= '9')
            digit = static_cast<unsigned>(*p - '0');
        else if (*p >= 'a' && *p <= 'f')
            digit = static_cast<unsigned>(*p - 'a') + 10;
        else if (*p >= 'A' && *p <= 'F')
            digit = static_cast<unsigned>(*p - 'A') + 10;
        else if (*p == '\'')
            continue;
        if (digit >= base)
            break;
        if (value > (UINT64_MAX - digit) / base)
            return false;
        value = value * base + digit;
        digits = true;
    }
    bool is_unsigned = false;
    unsigned longs = 0;
    for (; p < end; p++) {
        if (*p == 'u' || *p == 'U')
            is_unsigned = true;
        else if (*p == 'l' || *p == 'L')
            longs++;
        else
            return false;
    }
    if (!digits || longs > 2)
        return false;
    /* int, unsigned, long, unsigned long, long long, unsigned long long */
    static const fundamental_type ranks[] = {
        fundamental_type::int_,
        fundamental_type::unsigned_int,
        fundamental_type::long_int,
        fundamental_type::unsigned_long_int,
        fundamental_type::long_long_int,
        fundamental_type::unsigned_long_long_int,
    };
    unsigned long_bits = static_cast<unsigned>(analysis.pointer_size()) * 8;
    const unsigned widths[] = {32, long_bits, 64};
    for (unsigned rank = longs; rank < 3; rank++) {
        for (unsigned u = 0; u < 2; u++) {
            /* a decimal literal without u is never unsigned */
            if (u != is_unsigned && (is_unsigned || base == 10))
                continue;
            unsigned bits = widths[rank] - 1 + u;
            if (bits < 64 && value >> bits)
                continue;
            enc.write(terminal::expr_primary);
            builtin_type(ranks[rank * 2 + u]);
            enc.number(value);
            enc.write(terminal::end);
            return true;
        }
    }
    return false;
}

} // namespace cplusplus
