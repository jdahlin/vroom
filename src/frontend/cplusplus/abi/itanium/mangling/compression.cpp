/*
 * mangling/compression.cpp: [abi:mangling-compression], the builder's
 * side: which earlier component a type or a prefix is the same candidate
 * as, and the abbreviations of ::std's classes; the encoder keeps the
 * candidates and writes <substitution> (doc/PLAN-mangling.md).
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
   of ::std (no inline namespace) as a whole: Ss, Si, So or Sd; end for
   another */
terminal itanium_mangler::std_substitution(entity_index c) const
{
    const entity &x = analysis.entity_table[c];
    if (x.kind != entity_kind::class_type || !x.instantiation || !x.name ||
        !is_std(x.enclosing) || in_inline_namespace(c))
        return terminal::E_end;
    size_t length = 0;
    const char *identifier = spelling(x.name, &length);
    static const struct {
        const char *name;
        uint32_t arguments;
        terminal code;
    } whole[] = {
        {"basic_string", 3, terminal::Ss_std_string},
        {"basic_istream", 2, terminal::Si_std_istream},
        {"basic_ostream", 2, terminal::So_std_ostream},
        {"basic_iostream", 2, terminal::Sd_std_iostream},
    };
    for (const auto &w : whole) {
        if (length != strlen(w.name) || memcmp(identifier, w.name, length) ||
            analysis.specialization_argument_count(c) != w.arguments)
            continue;
        for (uint32_t i = 0; i < w.arguments; i++)
            if (analysis.specialization_pack(c, i))
                return terminal::E_end;
        qualified_type first = analysis.specialization_argument(c, 0);
        const struct type &fr = record(first);
        if (first.cv() || fr.kind != type_kind::fundamental ||
            fr.fundamental != fundamental_type::char_ ||
            !is_std_class_of_char(analysis.specialization_argument(c, 1),
                                  "char_traits", 11) ||
            (w.arguments == 3 &&
             !is_std_class_of_char(analysis.specialization_argument(c, 2),
                                   "allocator", 9)))
            return terminal::E_end;
        return w.code;
    }
    return terminal::E_end;
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
    for (size_t i = 0; i < enc.candidate_count(); i++)
        if (!enc.candidate_at(i).scope &&
            enc.candidate_at(i).member_class == member_class &&
            ((!enc.candidate_at(i).expansion &&
              same_candidate(enc.candidate_at(i).type, t)) ||
             (enc.candidate_at(i).expansion && !t.cv() &&
              record(t).kind == type_kind::pack_expansion &&
              same_candidate(enc.candidate_at(i).type, record(t).operand)))) {
            enc.substitution(i);
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
    size_t i = candidate_of(scope);
    if (i == no_candidate)
        return false;
    enc.substitution(i);
    return true;
}

/* the candidate a namespace or template seen as a prefix before is;
   no_candidate for one not seen */
size_t itanium_mangler::candidate_of(entity_index scope) const
{
    for (size_t i = 0; i < enc.candidate_count(); i++)
        if (enc.candidate_at(i).scope == scope)
            return i;
    return no_candidate;
}

} // namespace cplusplus
