/*
 * mangling/encoder.cpp: the encoder (mangling-internal.h): the bytes of the
 * tables' terminals, numbers ([abi:mangle.number]), <source-name>s, the
 * <discriminator> and [abi:mangling-compression]'s <substitution> and
 * <seq-id> (doc/PLAN-mangling.md).
 */
#include "mangling-internal.h"

namespace cplusplus
{

/* productions.inc's codes, in its order */
static const char *const terminal_codes[] = {
#define ITANIUM_TERMINAL(name, code) code,
#include "productions.inc"
#undef ITANIUM_TERMINAL
};

void itanium_encoder::write(terminal t)
{
    vbuffer_string_append(out, terminal_codes[static_cast<size_t>(t)]);
}

void itanium_encoder::write(itanium_operator o)
{
    vbuffer_string_append(out, operator_code(o));
}

bool itanium_encoder::builtin_type(fundamental_type t)
{
    const char *code = builtin_type_code(t);
    if (!code)
        return false;
    vbuffer_string_append(out, code);
    return true;
}

/*
 * [abi:mangle.number]:
 *
 *     <number> ::= [n] <non-negative decimal integer>
 *
 * In decimal, without leading zeros (0 is "0").
 */
void itanium_encoder::number(uint64_t n)
{
    vbuffer_format_append(out, "%llu", static_cast<unsigned long long>(n));
}

void itanium_encoder::signed_number(int64_t n)
{
    uint64_t magnitude = static_cast<uint64_t>(n);
    if (n < 0)
        magnitude = 0 - magnitude;
    value_number(n < 0, magnitude);
}

void itanium_encoder::value_number(bool negative, uint64_t magnitude)
{
    if (negative)
        write(terminal::negative);
    number(magnitude);
}

/*
 * [abi:mangle.name]:
 *
 *     <source-name> ::= <positive length number> <identifier>
 *     <identifier> ::= <unqualified source code identifier>
 *
 * The length is of the identifier's bytes (UTF-8 for one with a universal
 * character name, as clang and GCC write it).
 */
void itanium_encoder::source_name(const char *bytes, size_t length)
{
    number(length);
    vbuffer_append(out, bytes, length);
}

void itanium_encoder::source_name(const char *identifier)
{
    source_name(identifier, strlen(identifier));
}

void itanium_encoder::ordinal_underscore(uint64_t n)
{
    if (n)
        number(n - 1);
    write(terminal::underscore);
}

/*
 * [abi:mangling-scope]:
 *
 *     <discriminator> ::= _ <non-negative number>
 *
 * The section's second form, for a number of 10 or more, puts it between
 * "__" and "_".
 */
void itanium_encoder::discriminator(uint64_t n)
{
    if (n < 10) {
        write(terminal::underscore);
        number(n);
        return;
    }
    write(terminal::long_discriminator);
    number(n);
    write(terminal::underscore);
}

void itanium_encoder::unmangled(const char *bytes, size_t length)
{
    vbuffer_append(out, bytes, length);
}

/*
 * [abi:mangling-compression]:
 *
 *     <substitution> ::= S <seq-id> _
 *                    ::= S_
 *
 * The first candidate is S_, the (n+2)-th S <n in base 36> _.
 */
void itanium_encoder::substitution(size_t i)
{
    write(terminal::substitution);
    if (i)
        seq_id(i - 1);
    write(terminal::underscore);
}

/*
 * [abi:mangle.seq-id]:
 *
 *     <seq-id> ::= <0-9A-Z>+
 *
 * Base 36 with upper case letters, no leading zero.
 */
void itanium_encoder::seq_id(size_t n)
{
    char digits[16];
    size_t count = 0;
    do {
        unsigned d = static_cast<unsigned>(n % 36);
        char c = static_cast<char>('0' + d);
        if (d >= 10)
            c = static_cast<char>('A' + d - 10);
        digits[count++] = c;
        n /= 36;
    } while (n);
    while (count)
        vbuffer_append(out, &digits[--count], 1);
}

void itanium_encoder::take_candidates(owned_array<mangling_candidate> &into)
{
    into.clear();
    for (const mangling_candidate &c : candidates)
        into.append(c);
    candidates.clear();
}

void itanium_encoder::give_back_candidates(
    const owned_array<mangling_candidate> &from)
{
    candidates.clear();
    for (const mangling_candidate &c : from)
        candidates.append(c);
}

itanium_encoder::mark itanium_encoder::position() const
{
    return {out->n, candidates.size()};
}

void itanium_encoder::rewind(mark m)
{
    out->n = m.bytes;
    candidates.resize(m.candidates);
}

bool itanium_encoder::last_was(terminal t) const
{
    size_t length = strlen(terminal_codes[static_cast<size_t>(t)]);
    return out->n >= length &&
           !memcmp(out->p + out->n - length,
                   terminal_codes[static_cast<size_t>(t)], length);
}

void itanium_encoder::take_back(terminal t)
{
    out->n -= strlen(terminal_codes[static_cast<size_t>(t)]);
}

} // namespace cplusplus
