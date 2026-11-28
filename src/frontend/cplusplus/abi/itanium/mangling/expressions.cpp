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
    if (!expr_primary_external(target))
        return false;
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
    if (!expr_primary_value(of, negative, magnitude))
        return false;
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
            expr_primary_value(ranks[rank * 2 + u], false, value);
            return true;
        }
    }
    return false;
}

/* the template-arguments written in template-id id, one after another
   (one argument, or a template-argument-list's, without the commas) */
void itanium_mangler::template_argument_elements(
    SyntaxElement id, owned_array<SyntaxElement> &out)
{
    for (uint32_t i = 2; i + 1 < id.size(); i++) {
        if (id[i].is(syntax_kind::template_argument_list)) {
            for (auto x : id[i].elements())
                if (!x.is(preprocessing_op_or_punc::COMMA))
                    out.append(x);
        } else if (!id[i].is(preprocessing_op_or_punc::COMMA)) {
            out.append(id[i]);
        }
    }
}

/*
 * The shapes the four sources of expressions (expressions-*.cpp) share.
 * [abi:expressions]:
 *
 *     <expr-primary> ::= L _Z <encoding> E
 *
 * an object or function named by its external name.
 */
bool itanium_mangler::expr_primary_external(entity_index e)
{
    enc.write(terminal::L_Z_external_name);
    if (!encoding(e))
        return false;
    enc.write(terminal::E_end);
    return true;
}

/*
 * [abi:expressions]:
 *
 *     <expr-primary> ::= L <type> <value number> E
 *
 * [abi:mangling.literal]: the value in decimal, n before a negative one's
 * magnitude (a bool's 0 or 1).
 */
bool itanium_mangler::expr_primary_value(qualified_type t, bool negative,
                                         uint64_t magnitude)
{
    enc.write(terminal::L_expr_primary);
    return type(t) && expr_primary_end(negative, magnitude);
}

bool itanium_mangler::expr_primary_value(fundamental_type t, bool negative,
                                         uint64_t magnitude)
{
    enc.write(terminal::L_expr_primary);
    return builtin_type(t) && expr_primary_end(negative, magnitude);
}

/* (the value and E after the type) */
bool itanium_mangler::expr_primary_end(bool negative, uint64_t magnitude)
{
    enc.value_number(negative, magnitude);
    enc.write(terminal::E_end);
    return true;
}

/* [abi:expressions] <expr-primary> ::= L <nullptr type> E, nullptr's
   ([abi:mangling.literal]: its type alone) */
void itanium_mangler::expr_primary_nullptr()
{
    enc.write(terminal::L_expr_primary);
    builtin_type(fundamental_type::std_nullptr_t);
    enc.write(terminal::E_end);
}

/* [abi:mangle.function-param]:

       <function-param> ::= fp <top-level CV-qualifiers> _
                        ::= fp <top-level CV-qualifiers> <parameter-2 non-negative number> _

   the parameter at position (from 1); outer, [impl.clang] a
   requires-expression's in a template-head's requires-clause, written fL0p */
void itanium_mangler::function_param(uint32_t position, uint32_t cv, bool outer)
{
    if (outer) {
        enc.write(terminal::fL_outer_function_param);
        enc.number(0);
        enc.write(terminal::p_parameter_of_level);
    } else {
        enc.write(terminal::fp_function_param);
    }
    if (cv & cv_restrict)
        enc.write(terminal::r_restrict_qualifier);
    if (cv & cv_volatile)
        enc.write(terminal::V_volatile_qualifier);
    if (cv & cv_const)
        enc.write(terminal::K_const_qualifier);
    if (position > 1)
        enc.number(position - 2);
    enc.write(terminal::underscore);
}

/* [abi:expressions] <expression> ::= sZ <template-param>, sizeof... of a
   parameter pack */
void itanium_mangler::sizeof_pack(uint64_t position)
{
    enc.write(terminal::sZ_sizeof_pack);
    template_param(position);
}

} // namespace cplusplus
