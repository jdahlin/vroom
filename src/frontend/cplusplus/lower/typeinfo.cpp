/* typeinfo.cpp: the std::type_info objects of [abi:rtti] (lower-internal.h) */
#include "lower-internal.h"

namespace cplusplus::lowering
{

/* [abi:rtti]: the flags of a __vmi_class_type_info and of one of its bases
   ([impl.clang]: clang's ComputeVMIClassTypeInfoFlags) */
namespace
{
constexpr uint32_t vmi_non_diamond_repeat = 1, vmi_diamond_shaped = 2;
constexpr uint32_t base_virtual = 1, base_public = 2;
/* __pointer_type_info's flags */
constexpr uint32_t pointer_const = 1, pointer_volatile = 2;

struct seen_bases {
    owned_array<entity_index> direct, virtual_;
    static bool has(const owned_array<entity_index> &a, entity_index e)
    {
        for (entity_index s : a)
            if (s == e)
                return true;
        return false;
    }
};

uint32_t vmi_flags(const semantic_analysis &analysis, entity_index base,
                   seen_bases &seen)
{
    const entity &b = analysis.entity_table[base];
    entity_index of = analysis.type_table[b.type.unqualified()].entity;
    uint32_t flags = 0;
    if (b.virtual_base) {
        if (seen_bases::has(seen.virtual_, of))
            flags |= vmi_diamond_shaped;
        else {
            seen.virtual_.append(of);
            if (seen_bases::has(seen.direct, of))
                flags |= vmi_non_diamond_repeat;
        }
    } else {
        if (seen_bases::has(seen.direct, of))
            flags |= vmi_non_diamond_repeat;
        else {
            seen.direct.append(of);
            if (seen_bases::has(seen.virtual_, of))
                flags |= vmi_non_diamond_repeat;
        }
    }
    const entity &x = analysis.entity_table[of];
    for (uint32_t i = 0; i < x.base_count; i++)
        flags |=
            vmi_flags(analysis, analysis.class_bases[x.first_base + i], seen);
    return flags;
}
} // namespace

/* a class without linkage or with internal linkage, or an instantiation
   of a template with such an argument: its symbols are local */
bool c_lowering::class_internal(entity_index k) const
{
    const entity &x = analysis.entity_table[k];
    return x.linkage_of_name == linkage::internal ||
           x.linkage_of_name == linkage::none ||
           (x.instantiation &&
            analysis.type_without_external_linkage(x.type, true, true));
}

bool c_lowering::typeinfo_internal(type_index t) const
{
    const struct type &r = analysis.type_table[t];
    switch (r.kind) {
    case type_kind::class_type:
        return class_internal(r.entity);
    case type_kind::enumeration: {
        const entity &x = analysis.entity_table[r.entity];
        return x.linkage_of_name == linkage::internal ||
               x.linkage_of_name == linkage::none;
    }
    case type_kind::pointer:
    case type_kind::array:
        return typeinfo_internal(r.operand.unqualified());
    case type_kind::pointer_to_member:
        return typeinfo_internal(r.operand.unqualified()) ||
               (r.entity && class_internal(r.entity));
    default:
        return false;
    }
}

/* the run-time library's virtual table of the type_info class that
   describes a kind of type: its address point, the symbol + 16 */
uint32_t c_lowering::typeinfo_class_vtable(const char *name)
{
    for (size_t i = 0; i < runtime_vtable_names.size(); i++)
        if (!strcmp(runtime_vtable_names[i], name))
            return runtime_vtables[i];
    ast_qualified_type vp = pointer_to(v);
    uint32_t s =
        ast_builder_symbol(c, AST_SYMBOL_VARIABLE, name, strlen(name),
                           ast_builder_type_array(c, vp, 3, true),
                           AST_STORAGE_EXTERN, AST_SYMBOL_FLAG_GLOBAL, 0);
    ast_builder_toplevel(c, s);
    runtime_vtable_names.append(name);
    runtime_vtables.append(s);
    return s;
}

/* a pointer to the first character of the string symbol s, as void * */
uint32_t c_lowering::string_symbol_pointer(uint32_t s, ast_qualified_type array,
                                           ast_qualified_type element)
{
    ast_builder_symbol_flags_add(c, s, AST_SYMBOL_FLAG_USED);
    uint32_t object = c_variable(c, s, array);
    address_taken(object);
    return cast(cast(object, pointer_to(element), AST_CAST_ARRAY_DECAY),
                pointer_to(v), AST_CAST_POINTER_TO_POINTER);
}

/*
 * [abi:rtti]: "the type_info for a type T is a variable named _ZTI T":
 * the run-time library's class of it, the name, then the class's own
 * members ([abi:rtti-layout]).  A fundamental type's and a pointer to one's
 * is the library's (declared); a class's is emitted with its virtual table
 * (the key function's file) or, without a key function, in each file that
 * uses it, weak ([impl.clang]: _ZTS <type>, the name string, with the same
 * linkage).  Every entry is a pointer's size: the two int fields of a
 * __vmi_class_type_info and a __pointer_type_info's flags go in one
 * (little-endian targets).
 */
uint32_t c_lowering::typeinfo(type_index t, token_index at)
{
    for (size_t i = 0; i < typeinfo_types.size(); i++)
        if (typeinfo_types[i] == t)
            return typeinfo_symbols[i];
    const struct type r = analysis.type_table[t];
    ast_qualified_type vp = pointer_to(v);
    owned_buffer name;
    if (!itanium_typeinfo_name(analysis, tokens.pp, t, 0, name.get()))
        return fail("a std::type_info object whose symbol is not mangled", at);
    bool runtime = typeinfo_in_runtime(t);
    int storage = AST_STORAGE_NONE;
    uint32_t flags = AST_SYMBOL_FLAG_GLOBAL;
    bool define = true;
    const char *library = "_ZTVN10__cxxabiv117__class_type_infoE";
    uint32_t length = 2;
    entity_index k = 0;
    const dynamic_class *d = nullptr;
    switch (r.kind) {
    case type_kind::fundamental:
        library = "_ZTVN10__cxxabiv123__fundamental_type_infoE";
        break;
    case type_kind::class_type: {
        k = r.entity;
        if (!analysis.entity_table[k].size)
            return fail("the std::type_info object of an incomplete class", at);
        d = analysis.dynamic_of(k);
        const entity &x = analysis.entity_table[k];
        if (x.base_count) {
            /* [impl.clang]: one public non-virtual base, dynamic as
               the class is unless it is empty */
            bool single = x.base_count == 1;
            if (single) {
                const entity &b =
                    analysis.entity_table[analysis.class_bases[x.first_base]];
                entity_index be = record(b.type).entity;
                single =
                    !b.virtual_base && b.access == 0 &&
                    (analysis.empty_class(be) ||
                     (analysis.dynamic_of(be) != nullptr) == (d != nullptr));
            }
            if (single) {
                library = "_ZTVN10__cxxabiv120__si_class_type_infoE";
                length = 3;
            } else {
                library = "_ZTVN10__cxxabiv121__vmi_class_type_infoE";
                length = 3 + 2 * x.base_count;
            }
        }
        break;
    }
    case type_kind::enumeration:
        library = "_ZTVN10__cxxabiv116__enum_type_infoE";
        break;
    case type_kind::function:
        library = "_ZTVN10__cxxabiv120__function_type_infoE";
        break;
    case type_kind::array:
        library = "_ZTVN10__cxxabiv117__array_type_infoE";
        break;
    case type_kind::pointer:
        library = "_ZTVN10__cxxabiv119__pointer_type_infoE";
        length = 4;
        break;
    case type_kind::pointer_to_member:
        library = "_ZTVN10__cxxabiv129__pointer_to_member_type_infoE";
        length = 5;
        break;
    default:
        return fail("the std::type_info object of this kind of type", at);
    }
    if (runtime) {
        storage = AST_STORAGE_EXTERN;
        define = false;
    } else if (!rtti) {
        return fail("a std::type_info object with -fno-rtti", at);
    } else if (typeinfo_internal(t)) {
        storage = AST_STORAGE_STATIC;
    } else if (d && d->key_function) {
        if (!analysis.entity_table[d->key_function].defined) {
            storage = AST_STORAGE_EXTERN;
            define = false;
        }
    } else {
        flags |= AST_SYMBOL_FLAG_LINKONCE | AST_SYMBOL_FLAG_WEAK;
    }
    ast_qualified_type array = ast_builder_type_array(c, vp, length, true);
    uint32_t s = ast_builder_symbol(c, AST_SYMBOL_VARIABLE, name.data(),
                                    name.size(), array, storage, flags, 0);
    typeinfo_types.append(t);
    typeinfo_symbols.append(s);
    typeinfo_arrays.append(array);
    ast_builder_toplevel(c, s);
    if (!define)
        return s;

    /* the name string, _ZTS <type> */
    ast_qualified_type element = ast_builder_type_qualified(
        c, ast_builder_type_basic(AST_TYPE_CHAR), AST_QUALIFIER_CONST);
    owned_buffer text;
    if (!itanium_typeinfo_name(analysis, tokens.pp, t, 2, text.get()))
        return fail("a std::type_info object whose symbol is not mangled", at);
    name.clear();
    if (!itanium_typeinfo_name(analysis, tokens.pp, t, 1, name.get()))
        return fail("a std::type_info object whose symbol is not mangled", at);
    ast_qualified_type string_type =
        ast_builder_type_array(c, element, text.size() + 1, true);
    uint32_t ns =
        ast_builder_symbol(c, AST_SYMBOL_VARIABLE, name.data(), name.size(),
                           string_type, storage, flags, 0);
    ast_builder_toplevel(c, ns);
    owned_array<uint8_t> bytes;
    bytes.resize(text.size() + 1);
    for (size_t i = 0; i < text.size(); i++)
        bytes[i] = static_cast<uint8_t>(text.data()[i]);
    bytes[text.size()] = 0;
    ast_builder_symbol_body(
        c, ns,
        ast_builder_string(c, element, bytes.data(),
                           static_cast<uint32_t>(bytes.size()), 1,
                           LEX_TOKEN_STRING));
    ast_builder_symbol_flags_add(c, ns, AST_SYMBOL_FLAG_DEFINED);

    owned_array<uint32_t> runs;
    auto entry = [&](uint32_t index, uint32_t value) {
        runs.append(index);
        runs.append(index);
        runs.append(value);
    };
    auto integer = [&](uint32_t index, uint64_t value) {
        entry(index, cast(ast_builder_integer(c, ast_builder_type_ptrdiff(c),
                                              static_cast<int64_t>(value)),
                          vp, AST_CAST_INTEGER_TO_POINTER));
    };
    /* the address point of the library's virtual table: after the offset
       to top and the type_info pointer ... [abi:rtti]'s "+ 16" */
    uint32_t library_vtable = typeinfo_class_vtable(library);
    entry(0, cast(element_address(library_vtable,
                                  ast_builder_type_array(c, vp, 3, true), 2),
                  vp, AST_CAST_POINTER_TO_POINTER));
    entry(1, string_symbol_pointer(ns, string_type, element));
    auto pointee = [&](uint32_t index, type_index p) {
        uint32_t address = typeinfo_address(p, at);
        if (!address)
            return false;
        entry(index, address);
        return true;
    };
    switch (r.kind) {
    case type_kind::pointer:
    case type_kind::pointer_to_member: {
        qualified_type to = r.operand;
        const struct type &pr = analysis.type_table[to.unqualified()];
        if (pr.kind == type_kind::function && pr.non_throwing)
            return fail("the std::type_info object of a pointer to a "
                        "noexcept function",
                        at);
        uint32_t qualifiers = 0;
        if (to.cv() & cv_const)
            qualifiers |= pointer_const;
        if (to.cv() & cv_volatile)
            qualifiers |= pointer_volatile;
        integer(2, qualifiers);
        if (!pointee(3, to.unqualified()))
            return 0;
        if (r.kind == type_kind::pointer_to_member &&
            !pointee(4, analysis.entity_table[r.entity].type.unqualified()))
            return 0;
        break;
    }
    case type_kind::class_type: {
        const entity &x = analysis.entity_table[k];
        if (length == 3 && x.base_count) {
            const entity &b =
                analysis.entity_table[analysis.class_bases[x.first_base]];
            if (!pointee(2, b.type.unqualified()))
                return 0;
        } else if (x.base_count) {
            seen_bases seen;
            uint32_t vmi = 0;
            for (uint32_t i = 0; i < x.base_count; i++)
                vmi |= vmi_flags(analysis,
                                 analysis.class_bases[x.first_base + i], seen);
            integer(2, vmi | (static_cast<uint64_t>(x.base_count) << 32));
            for (uint32_t i = 0; i < x.base_count; i++) {
                entity_index be = analysis.class_bases[x.first_base + i];
                const entity &b = analysis.entity_table[be];
                if (!pointee(3 + 2 * i, b.type.unqualified()))
                    return 0;
                /* [abi:rtti-layout]: "__offset_flags: the low-order byte
                   are flags, the rest is the offset" ... a virtual base's
                   is its vbase offset's place in the virtual table
                   ([abi:vtable-components]), a negative number of bytes */
                int64_t offset = static_cast<int64_t>(b.offset);
                uint64_t bits = 0;
                if (b.access == 0)
                    bits |= base_public;
                if (b.virtual_base) {
                    bits |= base_virtual;
                    offset = 0;
                    entity_index of = record(b.type).entity;
                    for (uint32_t j = 0; j < d->vbase_entry_count; j++)
                        if (analysis.vbase_entries[d->first_vbase_entry + j] ==
                            of)
                            offset = -8 * static_cast<int64_t>(3 + j);
                }
                bits |= static_cast<uint64_t>(offset) << 8;
                integer(4 + 2 * i, bits);
            }
        }
        break;
    }
    default:
        break;
    }
    define_array(s, array, runs);
    return s;
}

/* the address of type t's type_info object, a void * */
uint32_t c_lowering::typeinfo_address(type_index t, token_index at)
{
    uint32_t s = typeinfo(t, at);
    if (!s)
        return 0;
    size_t i = 0;
    while (typeinfo_symbols[i] != s)
        i++;
    return cast(element_address(s, typeinfo_arrays[i], 0), pointer_to(v),
                AST_CAST_POINTER_TO_POINTER);
}

} // namespace cplusplus::lowering
