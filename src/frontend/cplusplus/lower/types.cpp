/* types.cpp: types: C types of C++ types, a class as a C struct, parameters (lower-internal.h) */
#include "lower-internal.h"

namespace cplusplus::lowering
{

/* ---- types ---------------------------------------------------------- */

ast_qualified_type c_lowering::fundamental(fundamental_type f, token_index at)
{
    switch (f) {
    case fundamental_type::void_:
        return v;
    case fundamental_type::bool_:
        return ast_builder_type_basic(AST_TYPE_BOOL);
    case fundamental_type::char_:
        return ast_builder_type_basic(AST_TYPE_CHAR);
    case fundamental_type::signed_char:
        return ast_builder_type_basic(AST_TYPE_SIGNED_CHAR);
    case fundamental_type::unsigned_char:
    case fundamental_type::char8_t_:
        return ast_builder_type_basic(AST_TYPE_UNSIGNED_CHAR);
    case fundamental_type::short_int:
        return ast_builder_type_basic(AST_TYPE_SHORT);
    case fundamental_type::unsigned_short_int:
    case fundamental_type::char16_t_:
        return ast_builder_type_basic(AST_TYPE_UNSIGNED_SHORT);
    case fundamental_type::int_:
        return ast_builder_type_basic(AST_TYPE_INT);
    case fundamental_type::unsigned_int:
    case fundamental_type::char32_t_:
        return ast_builder_type_basic(AST_TYPE_UNSIGNED_INT);
    case fundamental_type::wchar_t_:
        /* [impl.clang]: the target's wchar_t, as C's (unsigned on Linux
           arm64) */
        if (target == PP_LINUX_AARCH64)
            return ast_builder_type_basic(AST_TYPE_UNSIGNED_INT);
        return ast_builder_type_basic(AST_TYPE_INT);
    case fundamental_type::long_int:
        return ast_builder_type_basic(AST_TYPE_LONG);
    case fundamental_type::unsigned_long_int:
        return ast_builder_type_basic(AST_TYPE_UNSIGNED_LONG);
    case fundamental_type::long_long_int:
        return ast_builder_type_basic(AST_TYPE_LONG_LONG);
    case fundamental_type::unsigned_long_long_int:
        return ast_builder_type_basic(AST_TYPE_UNSIGNED_LONG_LONG);
    case fundamental_type::int128:
        return ast_builder_type_basic(AST_TYPE_INT128);
    case fundamental_type::unsigned_int128:
        return ast_builder_type_basic(AST_TYPE_UINT128);
    case fundamental_type::float_:
        return ast_builder_type_basic(AST_TYPE_FLOAT);
    case fundamental_type::double_:
        return ast_builder_type_basic(AST_TYPE_DOUBLE);
    case fundamental_type::long_double:
        return ast_builder_type_basic(AST_TYPE_LONG_DOUBLE);
    case fundamental_type::float16:
        return ast_builder_type_basic(AST_TYPE_FLOAT16);
    case fundamental_type::bfloat16:
        return ast_builder_type_basic(AST_TYPE_BFLOAT16);
    case fundamental_type::float128:
        /* as C: a type of its own on x86-64, long double elsewhere */
        if (target == PP_LINUX_X86_64)
            return ast_builder_type_basic(AST_TYPE_FLOAT128);
        return ast_builder_type_basic(AST_TYPE_LONG_DOUBLE);
    case fundamental_type::std_nullptr_t:
        /* [basic.fundamental]/14: "A value of type std::nullptr_t is a
           null pointer constant"; it has a pointer's size */
        return pointer_to(v);
    case fundamental_type::builtin_va_list:
        return ast_builder_type_va_list(c);
    default:
        fail("a bit-precise integer type", at);
        return ast_builder_type_basic(AST_TYPE_INT);
    }
}

bool c_lowering::is_reference(qualified_type t) const
{
    type_kind k = record(t).kind;
    return k == type_kind::lvalue_reference || k == type_kind::rvalue_reference;
}

/* whether t is made from a class with no linkage (an unnamed class no
   typedef names, [basic.link]/4) */
bool c_lowering::without_linkage(qualified_type t) const
{
    for (;;) {
        const struct type &r = record(t);
        switch (r.kind) {
        case type_kind::class_type: {
            const entity &x = analysis.entity_table[r.entity];
            return !x.name && !x.linkage_name &&
                   !visible_local_class(analysis.entity_table, r.entity);
        }
        case type_kind::pointer:
        case type_kind::lvalue_reference:
        case type_kind::rvalue_reference:
        case type_kind::array:
            t = r.operand;
            continue;
        case type_kind::function: {
            const qualified_type *p =
                analysis.type_table.parameters(t.unqualified());
            for (uint32_t i = 0; i < r.parameter_count; i++)
                if (without_linkage(p[i]))
                    return true;
            t = r.operand;
            continue;
        }
        default:
            return false;
        }
    }
}

/* the C type of a C++ type; a reference is a pointer to what it refers to */
ast_qualified_type c_lowering::type(qualified_type t, token_index at)
{
    const struct type &r = record(t);
    ast_qualified_type made = 0;
    switch (r.kind) {
    case type_kind::fundamental:
        made = fundamental(r.fundamental, at);
        break;
    case type_kind::pointer:
    case type_kind::lvalue_reference:
    case type_kind::rvalue_reference:
    case type_kind::block_pointer: /* the block's address, as C's ^ */
        made = pointer_to(pointee_type(r.operand, at));
        break;
    case type_kind::array:
        if (r.variable_bound) {
            /* [ext:clang_vla] the bound is evaluated where the array is
               declared: its first mention */
            type_index index = t.unqualified();
            if (!variable_arrays[index]) {
                uint32_t size = full_value(r.variable_bound, false);
                if (!size)
                    return ast_builder_type_basic(AST_TYPE_INT);
                variable_arrays[index] =
                    ast_builder_type_vla(c, type(r.operand, at), size);
            }
            made = variable_arrays[index];
            break;
        }
        made = ast_builder_type_array(c, type(r.operand, at), r.bound,
                                      r.bound_known);
        break;
    case type_kind::atomic:
        made = ast_builder_type_atomic(c, type(r.operand, at));
        break;
    case type_kind::complex:
        made = ast_builder_type_complex(c, type(r.operand, at));
        break;
    case type_kind::vector:
        made = ast_builder_type_vector(c, type(r.operand, at), r.bound);
        break;
    case type_kind::ext_vector:
        made = ast_builder_type_ext_vector(c, type(r.operand, at), r.bound);
        break;
    case type_kind::function: {
        owned_array<ast_qualified_type> parameters;
        const qualified_type *p =
            analysis.type_table.parameters(t.unqualified());
        for (uint32_t i = 0; i < r.parameter_count; i++)
            parameters.append(parameter_type(p[i], at));
        made =
            ast_builder_type_function(c, type(r.operand, at), parameters.data(),
                                      r.parameter_count, r.variadic);
        break;
    }
    case type_kind::class_type:
        made = class_type(r.entity, at);
        break;
    case type_kind::enumeration: {
        /* [dcl.enum]/8: an enumeration is its underlying type's values,
           and has its representation ([basic.fundamental]/10) */
        fundamental_type u = analysis.entity_table[r.entity].underlying;
        if (u == fundamental_type::none) {
            fail("an incomplete enumeration type", at);
            return ast_builder_type_basic(AST_TYPE_INT);
        }
        made = fundamental(u, at);
        break;
    }
    case type_kind::pointer_to_member:
        /* [abi:data-member-pointers]: "The basic ABI properties of data
           member pointer types are those of ptrdiff_t." */
        if (record(r.operand).kind == type_kind::function)
            made = member_function_pointer_type();
        else
            made = ast_builder_type_ptrdiff(c);
        break;
    case type_kind::placeholder:
        /* [dcl.spec.auto]/11: a name of an entity whose type has an
           undeduced placeholder is in no expression, so the analysis
           leaves none for code */
        fail("a type with an undeduced placeholder", at);
        return ast_builder_type_basic(AST_TYPE_INT);
    default:
        fail("a pointer to member type", at);
        return ast_builder_type_basic(AST_TYPE_INT);
    }
    /* the qualifiers are C's bits: const 1, volatile 2 */
    if (t.cv())
        made = ast_builder_type_qualified(c, made, static_cast<int>(t.cv()));
    return made;
}

/* the C struct of a class, without fields yet (made once) */
ast_qualified_type c_lowering::class_tag(entity_index e)
{
    if (records[e])
        return records[e];
    const entity &x = analysis.entity_table[e];
    owned_buffer name;
    spelling(e, name);
    if (!x.name && x.linkage_name) {
        size_t length = 0;
        const char *s =
            tokens.pp.identifier_spelling(x.linkage_name - 1, &length);
        vbuffer_append(name.get(), s, length);
    }
    records[e] = ast_builder_type_record(c, name.data(), name.size(), x.union_);
    return records[e];
}

/*
 * A class as a C struct of its non-static data members (and base class
 * subobjects that are not empty), in order, at the offsets of the Itanium
 * layout ([abi:class-types]), which are C's for a POD ([abi:layout]).
 * Where they are not, the struct says them anyway:
 *   - a member the ABI places after where C would (an empty subobject
 *     moved by a conflict, II) has a char array of padding before it;
 *   - a base whose tail padding the class reuses (a member placed before
 *     the base's sizeof ends: [abi:pod]) is an aligned char array of the
 *     base's data size, which a member access reads as the base's type;
 *   - the struct ends with padding to the class's size.
 * The offsets C gives are checked against the analysis's.
 */
ast_qualified_type c_lowering::class_type(entity_index e, token_index at)
{
    if (records[e] && !deferred_class[e])
        return records[e];
    const entity &x = analysis.entity_table[e];
    if (x.kind != entity_kind::class_type) {
        fail("a class whose definition was not analysed", at);
        return ast_builder_type_basic(AST_TYPE_INT);
    }
    ast_qualified_type made = class_tag(e);
    deferred_class[e] = 0;
    if (!x.size)
        /* incomplete: a pointer to it is all a program can have */
        return made;
    classes_in_progress++;
    owned_array<ast_qualified_type> fields;
    owned_array<const char *> names;
    owned_array<size_t> lengths;
    owned_array<uint32_t> aligned;
    owned_array<uint8_t> packed;
    owned_array<int32_t> widths;
    owned_array<uint64_t> offsets;
    /* the generated names (__base, __pad), NUL-separated, for the fields
       whose names[] is null */
    owned_buffer generated;
    ast_qualified_type byte = ast_builder_type_basic(AST_TYPE_CHAR);
    uint64_t end = 0;
    auto add = [&](ast_qualified_type t, const char *n, size_t length,
                   uint32_t align, bool pack, int32_t width) {
        fields.append(t);
        names.append(n);
        lengths.append(length);
        aligned.append(align);
        packed.append(pack);
        widths.append(width);
    };
    const dynamic_class *dynamic = analysis.dynamic_of(e);
    if (dynamic && !dynamic->primary_base) {
        /* [abi:class-types] I: the virtual table pointer at offset 0 */
        add(pointer_to(ast_builder_type_pointer(c, v)), "__vptr", 6, 0, false,
            -1);
        offsets.append(0);
        end = analysis.pointer_size();
    }
    auto padding = [&](uint64_t bytes) {
        vbuffer_format_append(generated.get(), "__pad%u",
                              static_cast<uint32_t>(fields.size()));
        vbuffer_append(generated.get(), "", 1);
        add(ast_builder_type_array(c, byte, bytes, true), nullptr, 0, 0, false,
            -1);
        offsets.append(UINT64_MAX);
    };
    for (uint32_t i = 0; i < x.member_count; i++) {
        entity_index member = analysis.class_members[x.first_member + i];
        const entity &m = analysis.entity_table[member];
        ast_qualified_type t = type(m.type, at);
        uint64_t size = analysis.size_of_type(m.type);
        uint64_t align = analysis.alignment_of_type(m.type);
        if (is_reference(m.type)) {
            /* a pointer in the C struct */
            size = analysis.pointer_size();
            align = size;
        }
        if (m.gnu_flags & gnu_packed)
            align = 1;
        if (m.alignment > align)
            align = m.alignment;
        if (!x.union_ && !m.bit_field) {
            uint64_t natural = end;
            if (align)
                natural = (end + align - 1) / align * align;
            if (m.offset > natural && m.offset > end)
                padding(m.offset - end);
            /* the next member placed inside this base's tail padding */
            uint64_t next = x.size;
            if (i + 1 < x.member_count)
                next = analysis
                           .entity_table[analysis.class_members[x.first_member +
                                                                i + 1]]
                           .offset;
            const entity &of = analysis.entity_table[record(m.type).entity];
            if (m.base_subobject && of.virtual_base_count) {
                /* a base with virtual bases: its non-virtual part
                   ([abi:class-types] nvsize), its virtual bases the
                   class's own */
                size = of.nv_size;
                align = of.nv_alignment;
                t = ast_builder_type_array(c, byte, size, true);
                tail_reused[member] = 1;
            } else if (m.base_subobject && next < m.offset + size) {
                size = of.data_size;
                t = ast_builder_type_array(c, byte, size, true);
                tail_reused[member] = 1;
            }
        }
        c_fields[member] = static_cast<uint32_t>(fields.size());
        offsets.append(m.offset);
        int32_t width = -1;
        if (m.bit_field)
            width = m.bit_width;
        uint32_t field_align = m.alignment;
        if (tail_reused[member])
            /* the data-size char array: its base's alignment */
            field_align = static_cast<uint32_t>(align);
        if (m.base_subobject || (!m.name && !m.bit_field)) {
            /* a base class subobject or an anonymous union's object: a
               field no C name can refer to */
            vbuffer_format_append(generated.get(), "__base%u", i);
            vbuffer_append(generated.get(), "", 1);
            add(t, nullptr, 0, field_align, (m.gnu_flags & gnu_packed) != 0,
                width);
        } else if (!m.name) {
            /* an unnamed bit-field */
            add(t, "", 0, field_align, (m.gnu_flags & gnu_packed) != 0, width);
        } else {
            size_t length = 0;
            const char *spelled =
                tokens.pp.identifier_spelling(m.name - 1, &length);
            add(t, spelled, length, field_align,
                (m.gnu_flags & gnu_packed) != 0, width);
        }
        if (m.bit_field)
            end = (m.bit_offset + m.bit_width + 7) / 8;
        else if (m.offset + size > end)
            end = m.offset + size;
    }
    /* [abi:class-types] III: the virtual bases of a complete object, each
       its non-virtual part (an empty one has no bytes); no C name refers
       to them, the lowering reaches them through their offsets.  They
       are placed in inheritance graph order, not in the order of the
       list (clang's vbases()): by offset here */
    owned_array<uint32_t> placed;
    for (uint32_t i = 0; i < x.virtual_base_count; i++) {
        uint32_t at_index = static_cast<uint32_t>(placed.size());
        uint64_t offset =
            analysis
                .entity_table
                    [analysis.class_virtual_bases[x.first_virtual_base + i]]
                .offset;
        while (at_index &&
               analysis.entity_table
                       [analysis.class_virtual_bases[x.first_virtual_base +
                                                     placed[at_index - 1]]]
                           .offset > offset)
            at_index--;
        placed.append(0);
        for (uint32_t j = static_cast<uint32_t>(placed.size()) - 1;
             j > at_index; j--)
            placed[j] = placed[j - 1];
        placed[at_index] = i;
    }
    for (uint32_t i : placed) {
        const entity &v =
            analysis.entity_table
                [analysis.class_virtual_bases[x.first_virtual_base + i]];
        const entity &of = analysis.entity_table[record(v.type).entity];
        if (analysis.empty_class(record(v.type).entity))
            continue;
        if (v.offset > end)
            padding(v.offset - end);
        vbuffer_format_append(generated.get(), "__vbase%u", i);
        vbuffer_append(generated.get(), "", 1);
        add(ast_builder_type_array(c, byte, of.nv_size, true), nullptr, 0,
            of.nv_alignment, false, -1);
        offsets.append(v.offset);
        end = v.offset + of.nv_size;
    }
    if (!x.member_count && !end) {
        /* [intro.object]/8: an empty class is one byte, a C struct without
           members none: a char of padding */
        add(byte, "__empty", 7, 0, false, -1);
        offsets.append(UINT64_MAX);
        end = 1;
    }
    if (!x.union_ && end < x.size) {
        /* the C struct's size, rounded to its alignment, may fall short of
           the class's: padding at the end where it would */
        uint64_t rounded = end;
        if (x.alignment)
            rounded = (end + x.alignment - 1) / x.alignment * x.alignment;
        if (rounded < x.size)
            padding(x.size - end);
    }
    for (size_t i = 0, at_name = 0; i < names.size(); i++) {
        if (names[i])
            continue;
        names[i] = generated.data() + at_name;
        lengths[i] = strlen(names[i]);
        at_name += lengths[i] + 1;
    }
    /* [dcl.align]: the class's alignment-specifiers, as C's aligned on
       its first field (at offset 0), which raises the struct's alignment
       and rounds its size */
    if (x.requested_alignment && !aligned.empty() &&
        aligned[0] < x.requested_alignment)
        aligned[0] = x.requested_alignment;
    c_fields[e] = static_cast<uint32_t>(fields.size());
    ast_builder_type_record_complete(
        c, made, fields.data(), names.data(), lengths.data(), aligned.data(),
        packed.data(), widths.data(), static_cast<uint32_t>(fields.size()));
    for (uint32_t i = 0; i < x.member_count; i++) {
        entity_index member = analysis.class_members[x.first_member + i];
        const entity &m = analysis.entity_table[member];
        uint32_t f = c_fields[member];
        if (ast_builder_type_record_offset(c, made, f) != m.offset ||
            (m.bit_field &&
             ast_builder_type_record_bit_offset(c, made, f) != m.bit_offset))
            fail("a class whose layout is not C's", at);
    }
    if (ast_builder_type_size(c, made) != x.size)
        fail("a class whose size is not C's", at);
    if (!analysis.trivial_class(e))
        /* [abi:calls]: "If the return type is a class type that is
           non-trivial for the purposes of calls, the caller passes an
           address as an implicit parameter" (x8 on AArch64), whatever its
           size */
        ast_builder_type_record_in_memory(c, made);
    classes_in_progress--;
    /* the classes only a pointer named, now that no struct is half made */
    while (!classes_in_progress && deferred_classes.size()) {
        entity_index waiting = deferred_classes[deferred_classes.size() - 1];
        token_index named = deferred_at[deferred_at.size() - 1];
        deferred_classes.resize(deferred_classes.size() - 1);
        deferred_at.resize(deferred_at.size() - 1);
        if (deferred_class[waiting])
            class_type(waiting, named);
    }
    return made;
}

/* the type a pointer or reference points to: a class not made yet while
   another class's struct is being made is declared only, so that a class
   reached through the pointer that has the one being made as a member
   sees it whole; it is made when the outermost class is, or where a
   value of it is needed */
ast_qualified_type c_lowering::pointee_type(qualified_type t, token_index at)
{
    const struct type &r = record(t);
    if (!classes_in_progress || r.kind != type_kind::class_type ||
        records[r.entity] ||
        analysis.entity_table[r.entity].kind != entity_kind::class_type ||
        !analysis.entity_table[r.entity].size)
        return type(t, at);
    ast_qualified_type made = class_tag(r.entity);
    deferred_class[r.entity] = 1;
    deferred_classes.append(r.entity);
    deferred_at.append(at);
    if (t.cv())
        made = ast_builder_type_qualified(c, made, static_cast<int>(t.cv()));
    return made;
}

/* the C type of a function or variable: a non-static member function has
   the object's address first ([class.this]) */
/* [abi:calls]: a class that is not trivially copyable or has a
   non-trivial destructor is passed and returned by address, which C's
   calling convention does not do */
bool c_lowering::passed_indirectly(qualified_type t) const
{
    const struct type &r = record(t);
    return r.kind == type_kind::class_type &&
           analysis.entity_table[r.entity].kind == entity_kind::class_type &&
           !analysis.trivial_class(r.entity);
}

/* [abi:calls]: "If the parameter type is non-trivial for the purposes of
   calls, the caller must allocate space for a temporary and pass that
   temporary by reference" */
ast_qualified_type c_lowering::parameter_type(qualified_type t, token_index at)
{
    if (passed_indirectly(t))
        return pointer_to(type(t, at));
    return type(t, at);
}

bool c_lowering::indirect_parameter(entity_index e) const
{
    const entity &x = analysis.entity_table[e];
    return x.kind == entity_kind::variable && x.enclosing &&
           analysis.entity_table[x.enclosing].kind ==
               entity_kind::block_scope &&
           analysis.entity_table[x.enclosing].parameter_scope &&
           passed_indirectly(x.type);
}

ast_qualified_type c_lowering::entity_type(entity_index e, token_index at)
{
    const entity &x = analysis.entity_table[e];
    if (indirect_parameter(e))
        return parameter_type(x.type, at);
    if (x.kind != entity_kind::function || !x.nonstatic_member)
        return type(x.type, at);
    qualified_type object = qualified_type::make(
        analysis.entity_table[x.enclosing].type.unqualified(),
        object_cv(x.member_cv));
    return member_function_type(object, x.type, at);
}

ast_qualified_type c_lowering::member_function_type(qualified_type object,
                                                    qualified_type function,
                                                    token_index at)
{
    const struct type &f = record(function);
    owned_array<ast_qualified_type> parameters;
    parameters.append(pointer_to(type(object, at)));
    const qualified_type *p =
        analysis.type_table.parameters(function.unqualified());
    for (uint32_t i = 0; i < f.parameter_count; i++)
        parameters.append(parameter_type(p[i], at));
    return ast_builder_type_function(c, type(f.operand, at), parameters.data(),
                                     f.parameter_count + 1, f.variadic);
}

} // namespace cplusplus::lowering
