/*
 * layout.cpp: [abi:class-types] the layout of a class: the offset of each
 * base class subobject and non-static data member, and the class's size,
 * data size and alignment (doc/PLAN-cxx-sema.md, S4).
 *
 * The class rules ([class.mem], [class.derived], [class.bit]) declare the
 * bases and members and say what each is; no offset is known until the
 * closing }, where the class is complete and itanium_class_layout runs the
 * ABI's procedure over them in order.  The procedure has four parts, and
 * the functions below are named after them:
 *
 *     I    Initialization: a dynamic class's primary base, or its virtual
 *          table pointer at offset 0
 *     II   Allocation of members other than virtual bases: the primary
 *          base, the other non-virtual bases in declaration order, then
 *          the non-static data members and unnamed bit-fields
 *     III  Virtual base allocation
 *     IV   Finalization: the size rounded up to the alignment
 *
 * A class reaches this file only with rules analysis has: no virtual
 * functions and no virtual bases yet, so I allocates nothing and III has
 * no base to place.  Both are where S4's [class.virtual] and [class.mi]
 * continue: a virtual function declared after the data members moves
 * every one of them, which is why the layout is not made member by member.
 *
 * The ABI's prose is cited by section and said in our own words
 * (bench/cxx-spec-refs.py); -fcxx-dump=layout-claims writes what this
 * file concluded and bench/cxx-sema.py compares it with clang's
 * -fdump-record-layouts.
 */
#include "../../cplusplus-internal.h"
#include "frontend/cplusplus/sema.h"

namespace cplusplus
{

namespace
{

uint64_t align_up(uint64_t n, uint64_t alignment)
{
    if (!alignment)
        return n;
    return (n + alignment - 1) / alignment * alignment;
}

/* the class a base class subobject or a data member is an object of,
   through any array bounds; 0 for a member that is no class object */
entity_index class_of_component(const semantic_analysis &a, const entity &m)
{
    qualified_type t = m.type;
    while (a.type_table[t.unqualified()].kind == type_kind::array)
        t = a.type_table[t.unqualified()].operand;
    if (a.type_table[t.unqualified()].kind != type_kind::class_type)
        return 0;
    return a.type_table[t.unqualified()].entity;
}

/*
 * [abi:definitions]: an empty class has no non-static data members (an
 * unnamed bit-field of width zero is none), and only empty base classes.
 * A base class that is not empty is among the class's members
 * (entity::base_subobject).
 */
bool empty_class(const semantic_analysis &a, entity_index c)
{
    const entity &x = a.entity_table[c];
    for (uint32_t i = 0; i < x.member_count; i++) {
        const entity &m = a.entity_table[a.class_members[x.first_member + i]];
        if (m.bit_field && !m.bit_width)
            continue;
        if (m.base_subobject && empty_class(a, class_of_component(a, m)))
            continue;
        return false;
    }
    return true;
}

/*
 * [abi:pod]: a class that is a POD for the purpose of layout, whose tail
 * padding a derived class does not reuse.  C++03's POD class is an
 * aggregate (no user-declared constructors, no private or protected
 * non-static data members, no base classes, no virtual functions) without
 * a user-declared copy assignment operator or destructor whose non-static
 * data members are PODs.
 * [class.bit]/2: "Unnamed bit-fields are not members".
 */
bool pod_for_layout(const semantic_analysis &a, entity_index c)
{
    const entity &x = a.entity_table[c];
    if (x.first_constructor || x.destructor || x.base_count ||
        x.user_copy_assignment)
        return false;
    for (uint32_t i = 0; i < x.member_count; i++) {
        const entity &m = a.entity_table[a.class_members[x.first_member + i]];
        if (m.bit_field && !m.name)
            continue;
        if (m.access)
            return false;
        entity_index of = class_of_component(a, m);
        if (of && !pod_for_layout(a, of))
            return false;
    }
    return true;
}

/* whether class c has a subobject of class type `of` at offset 0: itself,
   or one of its own at offset 0 ([abi:class-types] II's component type
   conflicts) */
bool at_offset_zero(const semantic_analysis &a, entity_index c, entity_index of)
{
    if (c == of)
        return true;
    const entity &x = a.entity_table[c];
    for (uint32_t i = 0; i < x.base_count; i++) {
        const entity &b = a.entity_table[a.class_bases[x.first_base + i]];
        if (!b.offset && at_offset_zero(a, class_of_component(a, b), of))
            return true;
    }
    for (uint32_t i = 0; i < x.member_count; i++) {
        const entity &m = a.entity_table[a.class_members[x.first_member + i]];
        entity_index inner = class_of_component(a, m);
        if (!m.offset && inner && at_offset_zero(a, inner, of))
            return true;
    }
    return false;
}

/* whether a subobject at offset 0 of class c2 (c2 itself, or a base or
   member it has there) has the type of one at offset 0 of class c1: the
   two cannot both be placed at one address ([abi:class-types] II) */
bool share_offset_zero(const semantic_analysis &a, entity_index c1,
                       entity_index c2)
{
    if (at_offset_zero(a, c1, c2))
        return true;
    const entity &x = a.entity_table[c2];
    for (uint32_t i = 0; i < x.base_count; i++) {
        const entity &b = a.entity_table[a.class_bases[x.first_base + i]];
        if (!b.offset && share_offset_zero(a, c1, class_of_component(a, b)))
            return true;
    }
    for (uint32_t i = 0; i < x.member_count; i++) {
        const entity &m = a.entity_table[a.class_members[x.first_member + i]];
        entity_index inner = class_of_component(a, m);
        if (!m.offset && inner && share_offset_zero(a, c1, inner))
            return true;
    }
    return false;
}

/* [abi:general]: the quantities of class C while its components are
   allocated */
class class_layout
{
  public:
    class_layout(semantic_analysis &analysis, entity_index c)
        : analysis(analysis), table(analysis.entity_table), c(c)
    {
    }

    const char *initialization();
    const char *allocation_of_members();
    const char *virtual_base_allocation();
    const char *finalization();

  private:
    semantic_analysis &analysis;
    entities &table;
    entity_index c;
    /* sizeof(C) and align(C) so far, in bytes */
    uint64_t size = 0;
    uint32_t alignment = 1;
    /* dsize(C) so far, in bits: the bits its last byte has left after a
       bit-field are the next bit-field's */
    uint64_t data_bits = 0;

    uint64_t data_size() const
    {
        return align_up(data_bits, 8) / 8;
    }
    void raise_alignment(uint64_t to)
    {
        if (to > alignment)
            alignment = static_cast<uint32_t>(to);
    }
    void raise_size(uint64_t to)
    {
        if (to > size)
            size = to;
    }
    const char *base(uint32_t number);
    void bit_field(entity &m);
    void data_member(entity &m);
};

/*
 * [abi:class-types] I: sizeof(C) and dsize(C) start at zero, align(C) at
 * one.  For a dynamic class this part chooses the primary base class, or
 * allocates the virtual table pointer at offset 0; no class with a virtual
 * function or a virtual base is analysed yet ([class.virtual], [class.mi]
 * are unsupported where they are declared).
 */
const char *class_layout::initialization()
{
    size = 0;
    alignment = 1;
    data_bits = 0;
    return nullptr;
}

/*
 * [abi:class-types] II, a base class: an empty one at offset 0, any other
 * at dsize(C) rounded up to its alignment, taking its data size (which is
 * its size for a POD for the purpose of layout, [abi:pod]).  A base that
 * would share its address with a subobject of its own type moves to a
 * later offset in the ABI; that rule is not written, and the class has no
 * layout.
 */
const char *class_layout::base(uint32_t number)
{
    const entity &x = table[c];
    entity &b = table[analysis.class_bases[x.first_base + number]];
    entity_index of = class_of_component(analysis, b);
    const entity &d = table[of];
    raise_alignment(d.alignment);
    if (empty_class(analysis, of)) {
        /* [class.derived]/4 (Note): "A base class subobject may be of
           zero size" */
        for (uint32_t k = 0; k < number; k++)
            if (share_offset_zero(
                    analysis,
                    class_of_component(
                        analysis,
                        table[analysis.class_bases[x.first_base + k]]),
                    of))
                return "class.derived";
        b.offset = 0;
        raise_size(d.size);
        return nullptr;
    }
    b.offset = align_up(data_size(), d.alignment);
    /* at offset 0 with an empty base placed there before */
    for (uint32_t k = 0; !b.offset && k < number; k++)
        if (share_offset_zero(
                analysis,
                class_of_component(
                    analysis, table[analysis.class_bases[x.first_base + k]]),
                of))
            return "class.derived";
    data_bits = (b.offset + d.data_size) * 8;
    raise_size(data_size());
    return nullptr;
}

/*
 * [abi:class-types] II, a bit-field no wider than its type: allocated as
 * the C ABI does.  [impl.clang] that is clang's for aarch64 Linux
 * (ItaniumRecordLayoutBuilder::LayoutBitField with
 * useBitFieldTypeAlignment and useZeroLengthBitfieldAlignment): a
 * bit-field starts at the next bit unless it would cross a boundary of its
 * type's alignment, then at that boundary (a zero-width one always moves
 * to it); every bit-field, named or not, raises the class's alignment to
 * its type's.  One wider than its type ([class.bit]/1's padding bits) is
 * unsupported where it is declared.
 */
void class_layout::bit_field(entity &m)
{
    uint64_t unit = analysis.size_of_type(m.type) * 8;
    uint64_t type_alignment = analysis.alignment_of_type(m.type);
    uint64_t bit_alignment = type_alignment * 8;
    uint64_t at = data_bits;
    if (!m.bit_width || (at % bit_alignment) + m.bit_width > unit)
        at = align_up(at, bit_alignment);
    m.offset = at / 8;
    m.bit_offset = at;
    data_bits = at + m.bit_width;
    raise_size(data_size());
    raise_alignment(type_alignment);
}

/*
 * [abi:class-types] II, any other data member: at dsize(C) rounded up to
 * the member's alignment, taking its size.  [class.mem]/19 (Note):
 * "Non-static data members of a (non-union) class with the same access
 * control and non-zero size are allocated so that later members have
 * higher addresses within a class object."  [ext:gnu_attributes] aligned
 * raises a member's alignment; packed lowers it to one byte, which
 * aligned raises.
 */
void class_layout::data_member(entity &m)
{
    uint64_t member_size = analysis.size_of_type(m.type);
    uint64_t member_alignment = analysis.alignment_of_type(m.type);
    if (m.gnu_flags & gnu_packed)
        member_alignment = 1;
    if (m.alignment > member_alignment)
        member_alignment = m.alignment;
    if (table[c].union_) {
        /* [class.union]/2: "The size of a union is sufficient to contain
           the largest of its non-static data members.  Each non-static
           data member is allocated as if it were the sole member of a
           non-union class." */
        m.offset = 0;
        if (member_size > data_size())
            data_bits = member_size * 8;
        else
            data_bits = data_size() * 8;
    } else {
        m.offset = align_up(data_size(), member_alignment);
        data_bits = (m.offset + member_size) * 8;
    }
    raise_size(data_size());
    raise_alignment(member_alignment);
}

/*
 * [abi:class-types] II: the components in order: the primary base (none
 * yet), the non-virtual direct bases in declaration order, then the
 * non-static data members and unnamed bit-fields in declaration order.
 * A base that is not empty is also among the class's members
 * (entity::base_subobject), where it is skipped.
 */
const char *class_layout::allocation_of_members()
{
    const entity &x = table[c];
    for (uint32_t i = 0; i < x.base_count; i++) {
        const char *rule = base(i);
        if (rule)
            return rule;
    }
    for (uint32_t i = 0; i < x.member_count; i++) {
        entity &m = table[analysis.class_members[x.first_member + i]];
        if (m.base_subobject)
            continue;
        if (m.bit_field)
            bit_field(m);
        else
            data_member(m);
    }
    return nullptr;
}

/* [abi:class-types] III: the virtual bases, after everything else.  No
   class with a virtual base is analysed yet ([class.mi]). */
const char *class_layout::virtual_base_allocation()
{
    return nullptr;
}

/*
 * [abi:class-types] IV: the size is rounded up to a non-zero multiple of
 * the alignment.  [intro.object]/8: "Unless it is a bit-field, an object
 * with nonzero size shall occupy one or more bytes of storage": an empty
 * class is one byte.  The data size a derived class continues from is the
 * size for a POD for the purpose of layout ([abi:pod]), whose tail
 * padding is not reused.
 */
const char *class_layout::finalization()
{
    entity &x = table[c];
    x.alignment = alignment;
    raise_size(data_size());
    x.size = align_up(size, alignment);
    if (!x.size)
        x.size = alignment;
    x.data_size = data_size();
    /* a data member of class type placed at offset 0 where an empty base
       of its type (or one it has at offset 0) is: the ABI moves the
       member, a rule not written */
    if (x.member_count) {
        const entity &first = table[analysis.class_members[x.first_member]];
        entity_index of = class_of_component(analysis, first);
        for (uint32_t i = 0;
             of && !first.base_subobject && !first.offset && i < x.base_count;
             i++) {
            const entity &b = table[analysis.class_bases[x.first_base + i]];
            entity_index base_class = class_of_component(analysis, b);
            if (empty_class(analysis, base_class) &&
                share_offset_zero(analysis, base_class, of))
                return "class.derived";
        }
    }
    if (pod_for_layout(analysis, c))
        x.data_size = x.size;
    return nullptr;
}

} // namespace

const char *itanium_class_layout(semantic_analysis &analysis, entity_index c)
{
    class_layout layout(analysis, c);
    const char *rule = layout.initialization();
    if (!rule)
        rule = layout.allocation_of_members();
    if (!rule)
        rule = layout.virtual_base_allocation();
    if (!rule)
        rule = layout.finalization();
    if (rule)
        analysis.entity_table[c].size = 0;
    return rule;
}

} // namespace cplusplus
