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
 * A class reaches this file only with rules analysis has.  A
 * virtual function declared after the data members moves every one of
 * them (I's virtual table pointer), which is why the layout is not made
 * member by member.
 *
 * The ABI's prose is cited by section and said in our own words
 * (bench/cxx-spec-refs.py); -fcxx-dump=layout-claims writes what this
 * file concluded and bench/cxx-sema.py compares it with clang's
 * -fdump-record-layouts.
 */
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
 * [dcl.attr.nouniqueaddr]/2: a member declared [[no_unique_address]]
 * "may have the same address as another non-static data member"
 * ([intro.object]/7: potentially-overlapping); [impl.clang] one of class
 * type only, an array's elements are laid out as any member's.
 */
bool potentially_overlapping_class(const semantic_analysis &a, const entity &m)
{
    return m.no_unique_address &&
           a.type_table[m.type.unqualified()].kind == type_kind::class_type;
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
    /* "no virtual functions": a dynamic class has its virtual table
       pointer */
    if (x.dynamic)
        return false;
    for (uint32_t i = 0; i < x.member_count; i++) {
        const entity &m = a.entity_table[a.class_members[x.first_member + i]];
        if (m.bit_field && !m.bit_width)
            continue;
        if (m.base_subobject && empty_class(a, class_of_component(a, m)))
            continue;
        /* [impl.clang] [dcl.attr.nouniqueaddr]: a potentially-overlapping
           member of an empty class's type takes no bytes of its own */
        if (m.no_unique_address && potentially_overlapping_class(a, m) &&
            empty_class(a, class_of_component(a, m)))
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
 * data members are PODs (no reference).
 * [class.bit]/2: "Unnamed bit-fields are not members".
 */
bool pod_for_layout(const semantic_analysis &a, entity_index c)
{
    const entity &x = a.entity_table[c];
    /* [impl.clang] a destructor or copy assignment operator deleted on its
       first declaration is no user-declared one here, as constructors */
    if ((x.destructor && !a.entity_table[x.destructor].deleted) ||
        x.base_count || x.user_copy_assignments || x.dynamic)
        return false;
    /* [impl.clang] a constructor deleted on its first declaration is no
       user-declared one here (C++11's aggregates allow it) */
    for (entity_index k = x.first_constructor; k;
         k = a.entity_table[k].next_constructor)
        if (!a.entity_table[k].deleted)
            return false;
    for (uint32_t i = 0; i < x.member_count; i++) {
        const entity &m = a.entity_table[a.class_members[x.first_member + i]];
        if (m.bit_field && !m.name)
            continue;
        /* [impl.clang] a default member initializer (no aggregate in
           C++11, so no C++03 POD) */
        if (m.access || m.default_initializer)
            return false;
        /* C++03 clause 9 paragraph 4: a POD-struct has no "non-static data members of
           type ... reference" */
        type_kind kind = a.type_table[m.type.unqualified()].kind;
        if (kind == type_kind::lvalue_reference ||
            kind == type_kind::rvalue_reference)
            return false;
        entity_index of = class_of_component(a, m);
        if (of && !pod_for_layout(a, of))
            return false;
    }
    return true;
}

/* an empty subobject placed in the class being laid out: its class and
   offset ([abi:class-types] II's component type conflicts) */
struct empty_subobject {
    entity_index of;
    uint64_t offset;
};

/* the most elements of an array member whose empty subobjects are
   recorded; a larger array of classes with empty subobjects has no layout
   here */
constexpr uint64_t most_recorded_elements = 4096;

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
    bool zero_size_members_only(const entity &x) const;

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
    /* [abi:class-types] II: the empty subobjects allocated so far */
    owned_array<empty_subobject> empty_subobjects;
    /* set when a component has more empty subobjects than are recorded */
    bool too_many = false;

    /* f(class, offset) for every subobject of class `of` at `at`, itself
       included, while it returns true */
    template <typename F>
    bool each_subobject(entity_index of, uint64_t at, bool complete, F &&f);
    /* the objects of class `of` a member of that class type (or an array
       of them) is, at most most_recorded_elements (too_many past it) */
    uint64_t elements(const entity &m, entity_index of)
    {
        uint64_t element = table[of].size;
        uint64_t count = 1;
        if (element)
            count = analysis.size_of_type(m.type) / element;
        if (count > most_recorded_elements) {
            too_many = true;
            count = most_recorded_elements;
        }
        return count;
    }
    bool conflicts(entity_index of, uint64_t at, bool complete);
    void record(entity_index of, uint64_t at, bool complete);
    bool elements_conflict(entity_index of, uint64_t at, uint64_t element,
                           uint64_t count)
    {
        for (uint64_t k = 0; k < count; k++)
            if (conflicts(of, at + k * element, true))
                return true;
        return false;
    }
    const char *base(entity &b);
    void bit_field(entity &m, bool packed = false);
    const char *data_member(entity &m);
    void potentially_overlapping_member(entity &m, entity_index of,
                                        uint64_t member_alignment);
};

/*
 * Every subobject of a component of class type `of` placed at `at`: the
 * component, its bases and its members of class type (each element of an
 * array of them), at their offsets in it, and when the component is a
 * complete object (a member, not a base) its virtual bases.  Returns false
 * when f does.
 */
template <typename F>
bool class_layout::each_subobject(entity_index of, uint64_t at, bool complete,
                                  F &&f)
{
    if (!f(of, at))
        return false;
    const entity &x = table[of];
    for (uint32_t i = 0; i < x.base_count; i++) {
        const entity &b = table[analysis.class_bases[x.first_base + i]];
        /* a virtual base is placed by the most derived class (III) */
        if (b.virtual_base)
            continue;
        if (!each_subobject(class_of_component(analysis, b), at + b.offset,
                            false, f))
            return false;
    }
    /* a complete object (a member's) has its virtual bases too */
    for (uint32_t i = 0; complete && i < x.virtual_base_count; i++) {
        const entity &b =
            table[analysis.class_virtual_bases[x.first_virtual_base + i]];
        if (!each_subobject(class_of_component(analysis, b), at + b.offset,
                            false, f))
            return false;
    }
    for (uint32_t i = 0; i < x.member_count; i++) {
        const entity &m = table[analysis.class_members[x.first_member + i]];
        entity_index inner = class_of_component(analysis, m);
        if (!inner || m.base_subobject)
            continue;
        uint64_t element = table[inner].size;
        uint64_t count = elements(m, inner);
        for (uint64_t k = 0; k < count; k++)
            if (!each_subobject(inner, at + m.offset + k * element, true, f))
                return false;
    }
    return true;
}

/*
 * [abi:class-types] II: a component "conflicts" when one of its empty
 * subobjects would be at the offset of an empty subobject of the same
 * type already allocated: [intro.object]/9, two distinct objects of the
 * same type have distinct addresses.  A subobject that is not empty
 * occupies bytes no other component of the class shares.
 */
bool class_layout::conflicts(entity_index of, uint64_t at, bool complete)
{
    bool found = false;
    each_subobject(of, at, complete, [&](entity_index d, uint64_t offset) {
        if (!empty_class(analysis, d))
            return true;
        for (size_t i = 0; i < empty_subobjects.size(); i++)
            if (empty_subobjects[i].of == d &&
                empty_subobjects[i].offset == offset) {
                found = true;
                return false;
            }
        return true;
    });
    return found;
}

/* the empty subobjects of a component placed at `at` */
void class_layout::record(entity_index of, uint64_t at, bool complete)
{
    each_subobject(of, at, complete, [&](entity_index d, uint64_t offset) {
        if (empty_class(analysis, d))
            empty_subobjects.append(empty_subobject{d, offset});
        return true;
    });
}

/*
 * [abi:class-types] I: sizeof(C) and dsize(C) start at zero, align(C) at
 * one.  For a dynamic class this part chooses the primary base class (its
 * first dynamic base, which analysis takes only as the first base:
 * dynamic_class_record), allocated first by II at offset 0, or else
 * allocates the virtual table pointer at offset 0, the size and alignment
 * of a pointer.  A primary base may be a nearly empty virtual base.
 */
const char *class_layout::initialization()
{
    size = 0;
    alignment = 1;
    data_bits = 0;
    /* [dcl.align]: the class's own alignment-specifiers, before its
       bases and members (clang's) */
    if (table[c].requested_alignment)
        raise_alignment(table[c].requested_alignment);
    const dynamic_class *d = analysis.dynamic_of(c);
    if (d && !d->primary_base) {
        uint64_t pointer = analysis.pointer_size();
        size = pointer;
        data_bits = pointer * 8;
        raise_alignment(pointer);
    }
    return nullptr;
}

/*
 * [impl.clang] where a derived class's data continues after a non-empty
 * base: its data size, or past an empty base of it moved beyond its data
 * (a conflict), as clang's NonVirtualSize (ItaniumRecordLayoutBuilder::
 * LayoutBase), which the ABI's dsize does not count.  A base with virtual
 * bases takes its nvsize, its virtual bases being the most derived
 * class's ([abi:class-types] III).
 */
static uint64_t nonvirtual_size(semantic_analysis &analysis, entity_index of)
{
    const entities &table = analysis.entity_table;
    const entity &d = table[of];
    uint64_t end = d.data_size;
    if (d.virtual_base_count)
        end = d.nv_size;
    for (uint32_t i = 0; i < d.base_count; i++) {
        const entity &b = table[analysis.class_bases[d.first_base + i]];
        if (b.virtual_base)
            continue;
        entity_index bc = class_of_component(analysis, b);
        uint64_t to = b.offset + table[bc].size;
        if (!empty_class(analysis, bc))
            to = b.offset + nonvirtual_size(analysis, bc);
        if (to > end)
            end = to;
    }
    return end;
}

/*
 * [abi:class-types] II, a base class D: an empty one at offset 0, any
 * other at dsize(C) rounded up to nvalign(D), taking its data size (its
 * size for a POD for the purpose of layout, [abi:pod]).  When D conflicts
 * there (conflicts above), the offset moves by nvalign(D), an empty base's
 * starting again from dsize(C) rounded up; an empty base raises sizeof(C)
 * to cover it and leaves dsize(C) where it was.  A base with virtual
 * bases takes its nvsize and nvalign, its virtual bases being the most
 * derived class's (III).
 */
const char *class_layout::base(entity &b)
{
    entity_index of = class_of_component(analysis, b);
    const entity &d = table[of];
    uint64_t base_alignment = d.alignment;
    if (d.virtual_base_count)
        base_alignment = d.nv_alignment;
    raise_alignment(base_alignment);
    bool empty = empty_class(analysis, of);
    uint64_t at = 0;
    if (!empty)
        at = align_up(data_size(), base_alignment);
    if (conflicts(of, at, false)) {
        if (empty)
            at = align_up(data_size(), base_alignment);
        while (conflicts(of, at, false))
            at += base_alignment;
    }
    b.offset = at;
    record(of, at, false);
    if (empty) {
        raise_size(at + d.size);
    } else {
        data_bits = (at + nonvirtual_size(analysis, of)) * 8;
        raise_size(data_size());
    }
    if (too_many)
        return "class.derived";
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
void class_layout::bit_field(entity &m, bool packed)
{
    uint64_t unit = analysis.size_of_type(m.type) * 8;
    uint64_t type_alignment = analysis.alignment_of_type(m.type);
    /* [ext:gnu_attributes] a packed bit-field (the field's or its class's
       attribute) starts at the next bit whatever its type's alignment and
       aligns the class to one byte only (clang's FieldPacked) */
    uint64_t class_alignment = type_alignment;
    if (packed)
        class_alignment = 1;
    uint64_t bit_alignment = type_alignment * 8;
    if (table[c].union_) {
        /* [class.union]/2: each member "as if it were the sole member of
           a non-union class": at offset 0, its bytes the union's data */
        m.offset = 0;
        m.bit_offset = 0;
        uint64_t bits = align_up(m.bit_width, 8);
        if (bits > data_bits)
            data_bits = bits;
        raise_size(data_size());
        raise_alignment(class_alignment);
        return;
    }
    uint64_t at = data_bits;
    if (!packed && (!m.bit_width || (at % bit_alignment) + m.bit_width > unit))
        at = align_up(at, bit_alignment);
    m.offset = at / 8;
    m.bit_offset = at;
    data_bits = at + m.bit_width;
    raise_size(data_size());
    raise_alignment(class_alignment);
}

/*
 * [impl.clang] [abi:class-types] II, a member of class type `of` declared
 * [[no_unique_address]] ([dcl.attr.nouniqueaddr]): an empty class's takes
 * no bytes, so it starts at offset 0, moving (as an empty base does) to
 * dsize(C) rounded up and then by its alignment while an empty subobject
 * of the same type is there; it leaves dsize(C) where it was and raises
 * sizeof(C) to cover its byte.  Another class's takes its data size, not
 * its size: a later member may use its tail padding (a POD for the
 * purpose of layout, [abi:pod], has none, its data size being its size).
 */
void class_layout::potentially_overlapping_member(entity &m, entity_index of,
                                                  uint64_t member_alignment)
{
    if (empty_class(analysis, of)) {
        m.offset = 0;
        if (conflicts(of, 0, true)) {
            m.offset = align_up(data_size(), member_alignment);
            while (conflicts(of, m.offset, true))
                m.offset += member_alignment;
        }
        record(of, m.offset, true);
        raise_size(m.offset + table[of].size);
    } else {
        m.offset = align_up(data_size(), member_alignment);
        while (conflicts(of, m.offset, true))
            m.offset += member_alignment;
        record(of, m.offset, true);
        data_bits = (m.offset + table[of].data_size) * 8;
        raise_size(data_size());
    }
    raise_alignment(member_alignment);
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
const char *class_layout::data_member(entity &m)
{
    uint64_t member_size = analysis.size_of_type(m.type);
    uint64_t member_alignment = analysis.alignment_of_type(m.type);
    type_kind kind = analysis.type_table[m.type.unqualified()].kind;
    if (kind == type_kind::lvalue_reference ||
        kind == type_kind::rvalue_reference) {
        /* [abi:class-types]: a member of reference type is laid out as a
           pointer (clang's and GCC's) */
        member_size = analysis.pointer_size();
        member_alignment = member_size;
    }
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
        /* [abi:class-types] II: a member of class type (each element of
           an array of them) that conflicts moves by its alignment */
        entity_index of = class_of_component(analysis, m);
        if (potentially_overlapping_class(analysis, m)) {
            potentially_overlapping_member(m, of, member_alignment);
            if (too_many)
                return "class.derived";
            return nullptr;
        }
        uint64_t element = 0, count = 0;
        if (of) {
            element = table[of].size;
            count = elements(m, of);
        }
        m.offset = align_up(data_size(), member_alignment);
        while (elements_conflict(of, m.offset, element, count))
            m.offset += member_alignment;
        for (uint64_t k = 0; k < count; k++)
            record(of, m.offset + k * element, true);
        data_bits = (m.offset + member_size) * 8;
    }
    raise_size(data_size());
    raise_alignment(member_alignment);
    if (too_many)
        return "class.derived";
    return nullptr;
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
    /* [ext:gnu_attributes] a packed class's members have no padding; with
       a base or a virtual table pointer clang packs them too, in rules this
       does not follow yet */
    if ((x.gnu_flags & gnu_packed) &&
        (x.base_count || x.virtual_base_count || x.dynamic))
        return "ext.gnu";
    /* a primary base that is a virtual base is allocated first, at offset
       0 (the others are the first non-virtual base) */
    const dynamic_class *dyn = analysis.dynamic_of(c);
    if (dyn && dyn->primary_is_virtual) {
        const char *rule = base(table[dyn->primary_base]);
        if (rule)
            return rule;
    }
    for (uint32_t i = 0; i < x.base_count; i++) {
        entity &b = table[analysis.class_bases[x.first_base + i]];
        if (b.virtual_base)
            continue;
        const char *rule = base(b);
        if (rule)
            return rule;
    }
    for (uint32_t i = 0; i < x.member_count; i++) {
        entity &m = table[analysis.class_members[x.first_member + i]];
        if (m.base_subobject)
            continue;
        if (m.bit_field) {
            /* a packed bit-field starts at the next bit whatever its
               type's alignment */
            if ((m.gnu_flags | x.gnu_flags) & gnu_packed) {
                /* (a zero-width one aligns to its type: not followed) */
                if (!m.bit_width)
                    return "ext.gnu";
                bit_field(m, true);
                continue;
            }
            bit_field(m);
            continue;
        }
        const char *rule = data_member(m);
        if (rule)
            return rule;
    }
    return nullptr;
}

/*
 * [abi:class-types] III: the non-virtual part's size and alignment are
 * nvsize(C) and nvalign(C); then each virtual base, direct or indirect,
 * in inheritance graph order, is allocated as a non-virtual base is (II),
 * its offset that of the subobject in a complete object.  A virtual base
 * that is the primary base of the class or of its primary base chain (an
 * indirect primary base) was allocated with it by II, and is left out.
 */
const char *class_layout::virtual_base_allocation()
{
    entity &x = table[c];
    x.nv_size = size;
    x.nv_alignment = alignment;
    if (!x.virtual_base_count)
        return nullptr;
    owned_array<entity_index> order;
    analysis.virtual_bases_in_graph_order(c, order);
    const dynamic_class *dyn = analysis.dynamic_of(c);
    for (entity_index v : order) {
        entity &b = table[analysis.virtual_base_of(c, v)];
        if (dyn && dyn->primary_base) {
            /* allocated with the primary base chain: at the offset of the
               primary base and of the virtual base within it */
            const entity &pb = table[dyn->primary_base];
            entity_index pk = class_of_component(analysis, pb);
            if (dyn->primary_is_virtual && pk == v) {
                b.offset = pb.offset;
                continue;
            }
            if (analysis.primary_chain_has(pk, v)) {
                b.offset =
                    pb.offset + table[analysis.virtual_base_of(pk, v)].offset;
                continue;
            }
        }
        const char *rule = base(b);
        if (rule)
            return rule;
    }
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
bool class_layout::zero_size_members_only(const entity &x) const
{
    if (x.dynamic || x.base_count || x.virtual_base_count)
        return false;
    bool any = false;
    for (uint32_t i = 0; i < x.member_count; i++) {
        const entity &m = table[analysis.class_members[x.first_member + i]];
        const struct type &t = analysis.type_table[m.type.unqualified()];
        if (m.bit_field && !m.bit_width)
            continue;
        if (t.kind != type_kind::array || analysis.size_of_type(m.type))
            return false;
        any = true;
    }
    return any;
}

const char *class_layout::finalization()
{
    entity &x = table[c];
    x.alignment = alignment;
    raise_size(data_size());
    x.size = align_up(size, alignment);
    /* (a class whose data members are all arrays of zero elements or of
       unknown bound (flexible), with
       no base class or virtual table pointer, takes no bytes: clang's and
       GCC's size is zero, which analysis takes for an incomplete class's) */
    if (!x.size && zero_size_members_only(x))
        return "class.mem";
    if (!x.size)
        x.size = alignment;
    x.data_size = data_size();
    if (pod_for_layout(analysis, c))
        x.data_size = x.size;
    if (!x.virtual_base_count) {
        x.nv_size = x.data_size;
        x.nv_alignment = x.alignment;
    }
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
