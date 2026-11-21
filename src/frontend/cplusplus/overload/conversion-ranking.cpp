/*
 * conversion-ranking.cpp: [over.ics.rank] Ranking implicit conversion
 * sequences.
 *
 * [over.ics.rank]/1: "This subclause defines a partial ordering of implicit
 * conversion sequences based on the relationships better conversion
 * sequence and better conversion.  If an implicit conversion sequence S1 is
 * defined by these rules to be a better conversion sequence than S2, then it
 * is also the case that S2 is a worse conversion sequence than S1.  If
 * conversion sequence S1 is neither better than nor worse than conversion
 * sequence S2, S1 and S2 are said to be indistinguishable conversion
 * sequences."
 *
 * List-initialization sequences (/3.1) are not formed: an argument that is
 * a braced-init-list is not analysed with more than one candidate.
 */
#include "../cplusplus-internal.h"
#include "frontend/cplusplus/sema.h"

namespace cplusplus
{

namespace
{

using sequence = semantic_analysis::conversion_sequence;

/* [over.ics.rank]/2: "a standard conversion sequence is a better
   conversion sequence than a user-defined conversion sequence or an
   ellipsis conversion sequence, and a user-defined conversion sequence is
   a better conversion sequence than an ellipsis conversion sequence" */
int form_order(sequence::form f)
{
    switch (f) {
    case sequence::form::standard:
        return 0;
    case sequence::form::user_defined:
        return 1;
    default:
        return 2;
    }
}

/* -1 when the condition holds of a and not of b, 1 the other way, 0
   otherwise */
int prefer(bool a, bool b)
{
    if (a && !b)
        return -1;
    if (b && !a)
        return 1;
    return 0;
}

} // namespace

int semantic_analysis::compare_conversions(const conversion_sequence &a,
                                           const conversion_sequence &b) const
{
    /* [over.match.funcs]/4: a static member function's implicit object
       parameter matches any object, and is no better or worse */
    if (a.any_object || b.any_object)
        return 0;
    int fa = form_order(a.kind), fb = form_order(b.kind);
    if (fa != fb)
        return prefer(fa < fb, fb < fa);
    if (a.kind == sequence::form::user_defined) {
        /* /3.3: "User-defined conversion sequence U1 is a better conversion
           sequence than another user-defined conversion sequence U2 if they
           contain the same user-defined conversion function or constructor
           ... and the second standard conversion sequence of U1 is better
           than the second standard conversion sequence of U2."  The second
           of a converting constructor's is the identity (a reference
           binding's, reference_sequence's) */
        if (!a.function || a.function != b.function || a.ambiguous ||
            b.ambiguous)
            return 0;
        return compare_standard_conversions(a, b);
    }
    if (a.kind != sequence::form::standard)
        return 0;
    return compare_standard_conversions(a, b);
}

/*
 * [over.ics.rank]/3.2: "Standard conversion sequence S1 is a better
 * conversion sequence than standard conversion sequence S2 if ..." and /4:
 * "Standard conversion sequences are ordered by their ranks: an Exact Match
 * is a better conversion than a Promotion, which is a better conversion
 * than a Conversion.  Two conversion sequences with the same rank are
 * indistinguishable unless one of the following rules applies".
 */
int semantic_analysis::compare_standard_conversions(
    const conversion_sequence &a, const conversion_sequence &b) const
{
    /* /3.2.1: "S1 is a proper subsequence of S2 (comparing the conversion
       sequences in the canonical form defined by Table 17, excluding any
       Lvalue Transformation; the identity conversion sequence is
       considered to be a subsequence of any non-identity conversion
       sequence)".  [impl.clang] only of two that convert to the same type
       (a reference binding's: the type it refers to), as since CWG2352 one
       may bind directly to a more qualified type; others are told apart
       by /3.2.5 below */
    qualified_type ta = qualified_type::make(a.target.unqualified(), cv_none);
    qualified_type tb = qualified_type::make(b.target.unqualified(), cv_none);
    if (a.identity != b.identity && ta == tb)
        return prefer(a.identity, b.identity);
    /* [conv.fctptr]/1: using a noexcept pointer unchanged is also a
       proper subsequence of converting it to a potentially-throwing one. */
    if (a.identity && !b.identity && function_pointer_convertible(ta, tb))
        return -1;
    if (b.identity && !a.identity && function_pointer_convertible(tb, ta))
        return 1;
    /* /3.2.2: "the rank of S1 is better than the rank of S2" */
    if (a.rank != b.rank)
        return prefer(a.rank < b.rank, b.rank < a.rank);
    if (a.reference && b.reference && !a.implicit_object &&
        !b.implicit_object) {
        /* /3.2.3: "S1 and S2 include reference bindings ([dcl.init.ref])
           and neither refers to an implicit object parameter of a
           non-static member function declared without a ref-qualifier, and
           S1 binds an rvalue reference to an rvalue and S2 binds an lvalue
           reference" */
        int c =
            prefer(a.rvalue_reference && a.binds_rvalue && !b.rvalue_reference,
                   b.rvalue_reference && b.binds_rvalue && !a.rvalue_reference);
        if (c)
            return c;
        /* /3.2.4: "S1 and S2 include reference bindings and S1 binds an
           lvalue reference to an lvalue of function type and S2 binds an
           rvalue reference to an lvalue of function type" */
        c = prefer(a.function_lvalue && b.function_lvalue &&
                       !a.rvalue_reference && b.rvalue_reference,
                   a.function_lvalue && b.function_lvalue &&
                       !b.rvalue_reference && a.rvalue_reference);
        if (c)
            return c;
    }
    /* /3.2.5: "S1 and S2 differ only in their qualification conversion
       ([conv.qual]) and yield similar types T1 and T2, respectively, where
       T1 can be converted to T2 by a qualification conversion".  [impl.clang]
       after CWG2352 (a reference binds directly to a pointer of more
       qualified type), a reference binding's T is the type it refers to,
       its top-level cv-qualifiers dropped */
    if (!a.implicit_object && !b.implicit_object && !(ta == tb) &&
        (is_pointer(ta) || is_member_pointer(ta)) && similar(ta, tb)) {
        int c = prefer(qualification_convertible(ta, tb),
                       qualification_convertible(tb, ta));
        if (c)
            return c;
    }
    /* /3.2.6: "S1 and S2 include reference bindings, and the types to
       which the references refer are the same type except for top-level
       cv-qualifiers, and the type to which the reference initialized by
       S2 refers is more cv-qualified than the type to which the reference
       initialized by S1 refers" */
    if (a.reference && b.reference &&
        a.target.unqualified() == b.target.unqualified() &&
        a.target.cv() != b.target.cv()) {
        uint32_t x = a.target.cv(), y = b.target.cv();
        int c = prefer((x & y) == x, (x & y) == y);
        if (c)
            return c;
    }
    /* /4.1: "A conversion that does not convert a pointer or a pointer to
       member to bool is better than one that does." */
    if (a.pointer_to_bool != b.pointer_to_bool)
        return prefer(!a.pointer_to_bool, !b.pointer_to_bool);
    /* /4.2: "A conversion that promotes an enumeration whose underlying
       type is fixed to its underlying type is better than one that
       promotes to the promoted underlying type, if the two are
       different." */
    if (a.rank == 1 && b.rank == 1 && a.to_underlying != b.to_underlying)
        return prefer(a.to_underlying, b.to_underlying);
    /* /4.3: "If class B is derived directly or indirectly from class A,
       conversion of B* to A* is better than conversion of B* to void*";
       /4.4: "If class B is derived directly or indirectly from class A and
       class C is derived directly or indirectly from B, (4.4.1) conversion
       of C* to B* is better than conversion of C* to A*, (4.4.2) binding of
       an expression of type C to a reference to type B& is better than
       binding an expression of type C to a reference to type A&, ...
       (4.4.5) conversion of C to B is better than conversion of C to A" */
    if (a.base_from && a.base_from == b.base_from) {
        if (a.base_to && b.to_void_pointer)
            return -1;
        if (b.base_to && a.to_void_pointer)
            return 1;
        if (a.base_to && b.base_to && a.base_to != b.base_to) {
            bool ambiguous = false;
            bool a_derived = base_step(a.base_to, b.base_to, ambiguous) != 0;
            bool b_derived = base_step(b.base_to, a.base_to, ambiguous) != 0;
            return prefer(a_derived, b_derived);
        }
    }
    return 0;
}

} // namespace cplusplus
