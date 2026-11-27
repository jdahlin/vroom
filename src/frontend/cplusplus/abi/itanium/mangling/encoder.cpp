/*
 * mangling/encoder.cpp: [abi:mangling-compression], [abi:mangle.number] and [abi:mangle.seq-id]:
 * the substitutions, numbers and sequence ids (doc/PLAN-mangling.md).
 */
#include "mangling-internal.h"

namespace cplusplus
{

/* whether t is ::std::NAME<char> (NAME of length n: char_traits,
   allocator), outside an inline namespace */
bool itanium_mangler::is_std_class_of_char(qualified_type t, const char *name,
                                           size_t n) const
{
    const struct type &r = record(t);
    if (t.cv() || r.kind != type_kind::class_type)
        return false;
    entity_index c = r.entity;
    const entity &x = analysis.entity_table[c];
    if (!x.instantiation || !x.name || !is_std(x.enclosing) ||
        in_inline_namespace(c))
        return false;
    size_t length = 0;
    const char *identifier = spelling(x.name, &length);
    if (length != n || memcmp(identifier, name, n) ||
        analysis.specialization_argument_count(c) != 1 ||
        analysis.specialization_pack(c, 0))
        return false;
    qualified_type a = analysis.specialization_argument(c, 0);
    const struct type &ar = record(a);
    return !a.cv() && ar.kind == type_kind::fundamental &&
           ar.fundamental == fundamental_type::char_;
}

/* [abi:mangling-compression]'s abbreviation of class c, a specialization
   of ::std (no inline namespace) as a whole: Ss, Si, So or Sd; nullptr for
   another */
const char *itanium_mangler::std_substitution(entity_index c) const
{
    const entity &x = analysis.entity_table[c];
    if (x.kind != entity_kind::class_type || !x.instantiation || !x.name ||
        !is_std(x.enclosing) || in_inline_namespace(c))
        return nullptr;
    size_t length = 0;
    const char *identifier = spelling(x.name, &length);
    static const struct {
        const char *name;
        uint32_t arguments;
        const char *code;
    } whole[] = {
        {"basic_string", 3, "Ss"},
        {"basic_istream", 2, "Si"},
        {"basic_ostream", 2, "So"},
        {"basic_iostream", 2, "Sd"},
    };
    for (const auto &w : whole) {
        if (length != strlen(w.name) || memcmp(identifier, w.name, length) ||
            analysis.specialization_argument_count(c) != w.arguments)
            continue;
        for (uint32_t i = 0; i < w.arguments; i++)
            if (analysis.specialization_pack(c, i))
                return nullptr;
        qualified_type first = analysis.specialization_argument(c, 0);
        const struct type &fr = record(first);
        if (first.cv() || fr.kind != type_kind::fundamental ||
            fr.fundamental != fundamental_type::char_ ||
            !is_std_class_of_char(analysis.specialization_argument(c, 1),
                                  "char_traits", 11) ||
            (w.arguments == 3 &&
             !is_std_class_of_char(analysis.specialization_argument(c, 2),
                                   "allocator", 9)))
            return nullptr;
        return w.code;
    }
    return nullptr;
}

/*
 * [abi:mangle.number]:
 *
 *     <number> ::= [n] <non-negative decimal integer>
 *
 * In decimal, without leading zeros (0 is "0"); the n of a negative
 * number is never needed here.
 */
void itanium_mangler::number(uint64_t n)
{
    vbuffer_format_append(out, "%llu", static_cast<unsigned long long>(n));
}

void itanium_mangler::signed_number(int64_t n)
{
    uint64_t magnitude = static_cast<uint64_t>(n);
    if (n < 0) {
        text("n");
        magnitude = 0 - magnitude;
    }
    number(magnitude);
}

/*
 * [abi:mangling-compression]:
 *
 *     <substitution> ::= S <seq-id> _
 *                    ::= S_
 *
 * The first candidate is S_, the (n+2)-th S <n in base 36> _.
 * A member function type is a distinct candidate for each owning class,
 * even when its <function-type> text matches another function type.
 */
bool itanium_mangler::substitution(qualified_type t, entity_index member_class)
{
    for (size_t i = 0; i < candidates.size(); i++)
        if (!candidates[i].scope &&
            candidates[i].member_class == member_class &&
            ((!candidates[i].expansion &&
              same_candidate(candidates[i].type, t)) ||
             (candidates[i].expansion && !t.cv() &&
              record(t).kind == type_kind::pack_expansion &&
              same_candidate(candidates[i].type, record(t).operand)))) {
            substitution_at(i);
            return true;
        }
    return false;
}

/* [impl.clang] whether a and b are one candidate: the same type, or made
   the same way of template parameters at the same positions (clang keys a
   template parameter by its position: a function template's T_ in a
   <local-name>'s encoding and the T_ of the function encoded after it are
   one candidate) */
bool itanium_mangler::same_candidate(qualified_type a, qualified_type b) const
{
    for (;;) {
        if (a == b)
            return true;
        const struct type &x = record(a);
        const struct type &y = record(b);
        if (a.cv() != b.cv() || x.kind != y.kind)
            return false;
        switch (x.kind) {
        case type_kind::template_parameter:
            /* (the stand-in for a substituted pack is at no position) */
            if (a.unqualified() == analysis.substituted_pack_type ||
                b.unqualified() == analysis.substituted_pack_type)
                return false;
            return analysis.entity_table[x.entity].value ==
                   analysis.entity_table[y.entity].value;
        case type_kind::pointer:
        case type_kind::lvalue_reference:
        case type_kind::rvalue_reference:
        case type_kind::pack_expansion:
            a = x.operand;
            b = y.operand;
            continue;
        default:
            return false;
        }
    }
}

/* a namespace seen as a prefix before */
bool itanium_mangler::substitution(entity_index scope)
{
    for (size_t i = 0; i < candidates.size(); i++)
        if (candidates[i].scope == scope) {
            substitution_at(i);
            return true;
        }
    return false;
}

void itanium_mangler::substitution_at(size_t i)
{
    text("S");
    if (i)
        seq_id(i - 1);
    text("_");
}

/*
 * [abi:mangle.seq-id]:
 *
 *     <seq-id> ::= <0-9A-Z>+
 *
 * Base 36 with upper case letters, no leading zero.
 */
void itanium_mangler::seq_id(size_t n)
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

} // namespace cplusplus
