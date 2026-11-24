/*
 * types.cpp: the type table of a translation unit (doc/PLAN-cxx-sema.md).
 *
 * [basic.types]: a type is a fundamental type ([basic.fundamental]) or is
 * constructed from other types ([basic.compound]/1).  Each distinct type
 * has one record, found again through a hash of what it is made of, so
 * "the same type" ([basic.types]/11 and everywhere the standard compares
 * types) is an equal index.  cv-qualification is not in the record
 * (qualified_type).
 */
#include "../cplusplus-internal.h"
#include "common/hash.h"
#include "frontend/cplusplus/sema.h"

namespace cplusplus
{

types::types()
{
    records.append(type());
    slots.resize(64);
    for (type_index &slot : slots)
        slot = 0;
}

uint32_t types::hash(const type &t, const qualified_type *parameters) const
{
    /* the record's members as words: its padding is not part of the type */
    uint32_t key[10] = {static_cast<uint32_t>(t.kind),
                        static_cast<uint32_t>(t.fundamental),
                        static_cast<uint32_t>(t.non_throwing) << 2 |
                            static_cast<uint32_t>(t.variadic) << 1 |
                            static_cast<uint32_t>(t.bound_known),
                        t.operand.bits,
                        t.entity,
                        static_cast<uint32_t>(t.bound),
                        static_cast<uint32_t>(t.bound >> 32),
                        t.parameter_count,
                        static_cast<uint32_t>(t.function_cv) << 8 |
                            t.function_ref,
                        t.variable_bound};
    uint32_t h = vhash_string(reinterpret_cast<const char *>(key), sizeof key);
    if (t.parameter_count)
        h ^= vhash_string(reinterpret_cast<const char *>(parameters),
                          t.parameter_count * sizeof *parameters);
    return h;
}

bool types::same(const type &a, const qualified_type *a_parameters,
                 type_index b) const
{
    const type &other = records[b];
    if (a.kind != other.kind || a.fundamental != other.fundamental ||
        a.variadic != other.variadic || a.non_throwing != other.non_throwing ||
        a.function_cv != other.function_cv ||
        a.function_ref != other.function_ref ||
        a.bound_known != other.bound_known || !(a.operand == other.operand) ||
        a.entity != other.entity || a.bound != other.bound ||
        a.variable_bound != other.variable_bound ||
        a.parameter_count != other.parameter_count)
        return false;
    const qualified_type *b_parameters = parameters(b);
    for (uint32_t i = 0; i < a.parameter_count; i++)
        if (!(a_parameters[i] == b_parameters[i]))
            return false;
    return true;
}

void types::grow()
{
    size_t size = slots.size() * 2;
    slots.resize(size);
    for (type_index &slot : slots)
        slot = 0;
    for (type_index t = 1; t < records.size(); t++) {
        size_t i = hash(records[t], parameters(t)) & (size - 1);
        while (slots[i])
            i = (i + 1) & (size - 1);
        slots[i] = t;
    }
}

type_index types::intern(const type &t, const qualified_type *parameters)
{
    size_t mask = slots.size() - 1;
    size_t i = hash(t, parameters) & mask;
    while (slots[i]) {
        if (same(t, parameters, slots[i]))
            return slots[i];
        i = (i + 1) & mask;
    }
    type added = t;
    added.first_parameter = static_cast<uint32_t>(parameter_lists.size());
    for (uint32_t k = 0; k < t.parameter_count; k++)
        parameter_lists.append(parameters[k]);
    type_index index = static_cast<type_index>(records.size());
    records.append(added);
    slots[i] = index;
    /* at most half full */
    if (records.size() * 2 > slots.size())
        grow();
    return index;
}

type_index types::fundamental(fundamental_type which)
{
    type t;
    t.kind = type_kind::fundamental;
    t.fundamental = which;
    return intern(t, nullptr);
}

/* [dcl.ptr]/1: "pointer to T" */
type_index types::pointer_to(qualified_type pointee)
{
    type t;
    t.kind = type_kind::pointer;
    t.operand = pointee;
    return intern(t, nullptr);
}

/* [dcl.ref]/1: "lvalue reference to T" */
type_index types::lvalue_reference_to(qualified_type referred)
{
    type t;
    t.kind = type_kind::lvalue_reference;
    t.operand = referred;
    return intern(t, nullptr);
}

/* [dcl.ref]/1: "rvalue reference to T" */
type_index types::rvalue_reference_to(qualified_type referred)
{
    type t;
    t.kind = type_kind::rvalue_reference;
    t.operand = referred;
    return intern(t, nullptr);
}

/* [dcl.array]/1: "array of N T" */
type_index types::array_of(qualified_type element, uint64_t bound)
{
    type t;
    t.kind = type_kind::array;
    t.operand = element;
    t.bound_known = true;
    t.bound = bound;
    return intern(t, nullptr);
}

/* [temp.deduct.type]/8's T[i]: an array whose bound is a non-type
   template-parameter (its entity) */
type_index types::array_of_parameter_bound(qualified_type element,
                                           uint32_t parameter,
                                           uint64_t expression)
{
    type t;
    t.kind = type_kind::array;
    t.operand = element;
    t.bound_known = true;
    t.entity = parameter;
    t.bound = expression;
    return intern(t, nullptr);
}

/* [ext:clang_vla] a variable length array: the bound is a node, whose
   value no table knows */
type_index types::array_of_variable_bound(qualified_type element, uint32_t node)
{
    type t;
    t.kind = type_kind::array;
    t.operand = element;
    t.bound_known = true;
    t.bound = ~uint64_t(0);
    t.variable_bound = node;
    return intern(t, nullptr);
}

/* [dcl.array]/1: "array of unknown bound of T" */
type_index types::array_of_unknown_bound_of(qualified_type element)
{
    type t;
    t.kind = type_kind::array;
    t.operand = element;
    return intern(t, nullptr);
}

/* [dcl.fct]/1: "function of parameter-type-list returning T" */
type_index types::function(qualified_type result,
                           const qualified_type *parameters,
                           size_t parameter_count, bool variadic,
                           bool non_throwing, uint32_t function_cv,
                           uint32_t function_ref)
{
    type t;
    t.kind = type_kind::function;
    t.operand = result;
    t.variadic = variadic;
    t.non_throwing = non_throwing;
    t.function_cv = static_cast<uint8_t>(function_cv);
    t.function_ref = static_cast<uint8_t>(function_ref);
    t.parameter_count = static_cast<uint32_t>(parameter_count);
    return intern(t, parameters);
}

type_index types::function_qualified(type_index f, uint32_t function_cv,
                                     uint32_t function_ref)
{
    type t = records[f];
    t.function_cv = static_cast<uint8_t>(function_cv);
    t.function_ref = static_cast<uint8_t>(function_ref);
    return intern(t, parameters(f));
}

/* a class ([class.name]/1: "A class definition introduces a new type") or
   an enumeration ([dcl.enum]/1: "a distinct type"): one type per entity */
type_index types::of_entity(type_kind kind, uint32_t entity)
{
    type t;
    t.kind = kind;
    t.entity = entity;
    return intern(t, nullptr);
}

/* [dcl.mptr]/1: "pointer to member of class nested-name-specifier of type T" */
type_index types::pointer_to_member(uint32_t class_entity,
                                    qualified_type member)
{
    type t;
    t.kind = type_kind::pointer_to_member;
    t.entity = class_entity;
    t.operand = member;
    return intern(t, nullptr);
}

/* [dcl.mptr]/1 in a template's own declaration: a pointer to member of a
   class that is a dependent type ([temp.dep.type]/9), a template parameter
   or a member of one: R (C::*)(); its class is the type class_type */
type_index types::dependent_pointer_to_member(type_index class_type,
                                              qualified_type member)
{
    type t;
    t.kind = type_kind::pointer_to_member;
    t.bound = class_type;
    t.operand = member;
    return intern(t, nullptr);
}

/* [temp.variadic]/5: "A pack expansion consists of a pattern and an
   ellipsis" */
type_index types::pack_expansion_of(qualified_type pattern)
{
    type t;
    t.kind = type_kind::pack_expansion;
    t.operand = pattern;
    return intern(t, nullptr);
}

/* [ext:clang_bit_int] _BitInt(width), or _BitInt(N) of the parameter N */
type_index types::bit_int(uint64_t width, uint32_t parameter)
{
    type t;
    t.kind = type_kind::bit_int;
    t.bound = width;
    t.entity = parameter;
    return intern(t, nullptr);
}

/* [ext:clang_ext_vector_type] element
   __attribute__((ext_vector_type(count))), or of the parameter N */
type_index types::ext_vector_of(qualified_type element, uint64_t count,
                                uint32_t parameter)
{
    type t;
    t.kind = type_kind::ext_vector;
    t.operand = element;
    t.bound = count;
    t.entity = parameter;
    return intern(t, nullptr);
}

/* [ext:gnu_vector_size] a vector of count elements */
type_index types::vector_of(qualified_type element, uint64_t count)
{
    type t;
    t.kind = type_kind::vector;
    t.operand = element;
    t.bound = count;
    return intern(t, nullptr);
}

/* [ext:clang_blocks] T (^)(...): a block pointer to a function type */
type_index types::block_pointer_to(qualified_type function)
{
    type t;
    t.kind = type_kind::block_pointer;
    t.operand = function;
    return intern(t, nullptr);
}

/* [ext:clang_atomic] _Atomic(T) */
type_index types::atomic_of(qualified_type value)
{
    type t;
    t.kind = type_kind::atomic;
    t.operand = value;
    return intern(t, nullptr);
}

/* [ext:gnu_complex] _Complex T */
type_index types::complex_of(qualified_type value)
{
    type t;
    t.kind = type_kind::complex;
    t.operand = value;
    return intern(t, nullptr);
}

} // namespace cplusplus
