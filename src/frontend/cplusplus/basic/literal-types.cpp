/*
 * literal-types.cpp: [basic.types]/10's literal types, which
 * [dcl.constexpr]/3 asks of a constexpr function's parameters, its return
 * type and the variables its body defines (until C++23).
 *
 * The test answers constant for a literal type, not_constant for one that
 * is not, and unknown where analysis does not know enough of a class: an
 * implicitly declared default constructor or a defaulted destructor
 * (whether either is constexpr), an incomplete class.  Only not_constant
 * makes a program ill-formed.
 */
#include "../cplusplus-internal.h"
#include "frontend/cplusplus/sema.h"

namespace cplusplus
{

namespace
{

/* the worse of two answers for a type made of both: not literal, then
   unknown */
constness both(constness a, constness b)
{
    if (a == constness::not_constant || b == constness::not_constant)
        return constness::not_constant;
    if (a == constness::unknown || b == constness::unknown)
        return constness::unknown;
    return constness::constant;
}

} // namespace

/*
 * [basic.types]/10: "A type is a literal type if it is: cv void;
 * or a scalar type; or a reference type; or an array of literal type; or a
 * possibly cv-qualified class type that has all of the following
 * properties: it has a constexpr destructor, it is either a closure type,
 * an aggregate type, or has at least one constexpr constructor or
 * constructor template (possibly inherited from a base class) that is not
 * a copy or move constructor, if it is a union, at least one of its
 * non-static data members is of non-volatile literal type, and if it is
 * not a union, all of its non-static data members and base classes are of
 * non-volatile literal types."
 */
constness semantic_analysis::literal_type(qualified_type t) const
{
    const struct type &record = type_table[t.unqualified()];
    switch (record.kind) {
    case type_kind::array:
        return literal_type(record.operand);
    case type_kind::class_type:
        return literal_class(record.entity);
    default:
        /* void, the scalar types ([basic.types]/9: arithmetic,
           enumeration, pointer, pointer-to-member, std::nullptr_t) and
           the references */
        return constness::constant;
    }
}

constness semantic_analysis::literal_class(entity_index c) const
{
    const entity &x = entity_table[c];
    if (!x.defined)
        return constness::unknown;
    /* "it has a constexpr destructor": before C++20 a trivial one
       ([dcl.constexpr]/3, C++17's [basic.types]/10.5.1); one with a body
       is constexpr only if declared so ([dcl.constexpr]/1) */
    constness result = constness::constant;
    if (x.destructor && !entity_table[x.destructor].implicit) {
        const entity &d = entity_table[x.destructor];
        bool provided = d.definition && semantics[d.definition].kind ==
                                            semantic_kind::function_body;
        if (provided && !d.constexpr_)
            return constness::not_constant;
        if (!provided ||
            !tokens.files.behavior.has(language_feature::constexpr_destructors))
            result = constness::unknown;
    }
    /* "an aggregate type, or has at least one constexpr constructor ...
       that is not a copy or move constructor" */
    if (!is_aggregate(c)) {
        /* the implicitly declared ones: constexpr when they would satisfy
           a constexpr constructor's requirements ([class.default.ctor]/4,
           [class.copy.ctor]/12), which analysis does not check */
        constness constructor = constness::unknown;
        for (entity_index k = x.first_constructor; k;
             k = entity_table[k].next_constructor)
            if (!entity_table[k].implicit)
                constructor = constness::not_constant;
        for (entity_index k = x.first_constructor; k;
             k = entity_table[k].next_constructor)
            if (entity_table[k].constexpr_ && !copy_or_move_constructor(c, k))
                constructor = constness::constant;
        /* an implicit constructor that is no copy or move one (the
           defaulted default constructor beside user-provided ones) is
           constexpr when it would satisfy [dcl.constexpr]'s rules, which
           analysis does not check: not decided */
        for (entity_index k = x.first_constructor; k;
             k = entity_table[k].next_constructor)
            if (entity_table[k].implicit && !copy_or_move_constructor(c, k) &&
                constructor == constness::not_constant)
                constructor = constness::unknown;
        result = both(result, constructor);
    }
    /* the subobjects: each non-volatile and literal, or for a union one
       of them */
    /* (a union without members is literal for clang, which the rule's
       "at least one" does not say) */
    constness members = constness::constant;
    if (x.union_ && x.member_count)
        members = constness::not_constant;
    for (uint32_t i = 0; i < x.member_count; i++) {
        qualified_type m = entity_table[class_members[x.first_member + i]].type;
        constness literal = constness::not_constant;
        if (!(m.cv() & cv_volatile))
            literal = literal_type(m);
        if (!x.union_)
            members = both(members, literal);
        else if (literal == constness::constant)
            members = constness::constant;
        else if (literal == constness::unknown &&
                 members == constness::not_constant)
            members = constness::unknown;
    }
    for (uint32_t i = 0; i < x.base_count; i++)
        members = both(
            members,
            literal_type(entity_table[class_bases[x.first_base + i]].type));
    return both(result, members);
}

} // namespace cplusplus
