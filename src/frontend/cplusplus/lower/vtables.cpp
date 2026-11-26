/* vtables.cpp: virtual tables and VTTs: their layout and definitions (lower-internal.h) */
#include "lower-internal.h"

namespace cplusplus::lowering
{

/* ---- definitions ---------------------------------------------------- */

/* [abi:vtable-components]: the array of class k's virtual table: the
   offset to top, the RTTI pointer (null with -fno-rtti), then a
   function pointer per slot */
ast_qualified_type c_lowering::vtable_type(entity_index k)
{
    table_spec t;
    t.k = k;
    t.layout = k;
    return ast_builder_type_array(c, pointer_to(void_type), table_length(t),
                                  true);
}

/*
 * [abi:vague-vtable]: "If the class has a key function ..., the virtual
 * table is emitted in the object file containing the definition of the
 * key function"; else it is emitted where it is used, each copy weak so
 * that they link as one (as an inline function's, symbol: COMDAT is the
 * C code generator's to come).  A class without linkage has a local one.
 */
uint32_t c_lowering::vtable(entity_index k, token_index at)
{
    if (vtables[k])
        return vtables[k];
    const dynamic_class *d = analysis.dynamic_of(k);
    if (!d)
        return fail("the virtual table of a class that is not dynamic", at);
    owned_buffer name;
    if (!itanium_vtable_name(analysis, tokens.pp, k, name.get()))
        return fail("a virtual table whose symbol is not mangled", at);
    const entity &x = analysis.entity_table[k];
    /* [abi:vague-vtable]: a template specialization's tables, and those of
       a key function declared inline after the class, are emitted where
       used */
    entity_index key = d->key_function;
    if (key && (x.instantiation || analysis.entity_table[key].inline_))
        key = 0;
    bool here = !key || analysis.entity_table[key].defined;
    int storage = AST_STORAGE_NONE;
    uint32_t flags = AST_SYMBOL_FLAG_GLOBAL;
    if (!key)
        flags |= AST_SYMBOL_FLAG_LINKONCE | AST_SYMBOL_FLAG_WEAK;
    if (class_internal(k)) {
        storage = AST_STORAGE_STATIC;
        flags = AST_SYMBOL_FLAG_GLOBAL;
    }
    if (!here)
        storage = AST_STORAGE_EXTERN;
    ast_qualified_type array = vtable_type(k);
    uint32_t s = ast_builder_symbol(c, AST_SYMBOL_VARIABLE, name.data(),
                                    name.size(), array, storage, flags, 0);
    vtables[k] = s;
    ast_builder_toplevel(c, s);
    if (here) {
        table_spec t;
        t.k = k;
        t.layout = k;
        if (!table_definition(t, s, at))
            return 0;
    }
    if (x.virtual_base_count && !vtt_define(k, s, storage, flags, at))
        return 0;
    return s;
}

uint32_t c_lowering::address_point(entity_index k, uint32_t group,
                                   token_index at)
{
    table_spec t;
    t.k = k;
    t.layout = k;
    uint32_t s = vtable(k, at);
    if (!s)
        return 0;
    shape sh;
    table_shape(t, sh);
    return element_address(s, vtable_type(k), sh.points[group]);
}

/* ---- virtual bases ([class.mi]/4) ---------------------------------- */

uint64_t c_lowering::virtual_base_offset(entity_index layout,
                                         entity_index v) const
{
    return analysis.entity_table[analysis.virtual_base_of(layout, v)].offset;
}

int64_t c_lowering::group_location(const table_spec &t, uint32_t group) const
{
    const vtable_group &g =
        analysis.vtable_groups[analysis.dynamic_of(t.k)->first_group + group];
    if (!g.in_virtual_base)
        return static_cast<int64_t>(g.offset);
    return static_cast<int64_t>(
               virtual_base_offset(t.layout, g.in_virtual_base) +
               g.offset_in_virtual) -
           static_cast<int64_t>(t.base_offset);
}

/* [abi:vtable-ctor-entries]: "Some of the base class subobjects may not
   need construction virtual tables": [impl.clang] those of non-virtual
   bases without virtual bases */
bool c_lowering::group_included(const table_spec &t, uint32_t group) const
{
    if (!t.construction || !group)
        return true;
    const vtable_group &g =
        analysis.vtable_groups[analysis.dynamic_of(t.k)->first_group + group];
    return g.in_virtual_base ||
           analysis.entity_table[g.subobject_class].virtual_base_count;
}

/*
 * [abi:vtable-components]: a table's vbase offsets, one per virtual base
 * of the class whose primary table it is, "added to the derived class
 * object address (i.e. the address of its virtual table pointer) to get
 * the address of a virtual base class subobject", nearest the address
 * point first; then, in a virtual base's own table, its vcall offsets
 * ([abi:vtable-construction] category 3: one per virtual function of
 * the base's primary table and of its non-virtual bases' tables, the
 * first of a signature only), each the offset from the virtual base to
 * the subobject of the final overrider.  No class overrides a function
 * of a virtual base yet (dynamic_class_record), so that subobject is in
 * the virtual base.
 */
void c_lowering::group_offsets(const table_spec &t, uint32_t group,
                               owned_array<int64_t> &out) const
{
    const vtable_group &g =
        analysis.vtable_groups[analysis.dynamic_of(t.k)->first_group + group];
    int64_t at = group_location(t, group);
    const dynamic_class *sd = analysis.dynamic_of(g.subobject_class);
    for (uint32_t i = 0; i < sd->vbase_entry_count; i++) {
        entity_index v = analysis.vbase_entries[sd->first_vbase_entry + i];
        out.append(static_cast<int64_t>(virtual_base_offset(t.layout, v)) -
                   static_cast<int64_t>(t.base_offset) - at);
    }
    /* the primary table of a virtual base: in the layout's table, or the
       construction table of a virtual base */
    entity_index virtual_class = 0;
    if (g.virtual_primary)
        virtual_class = g.in_virtual_base;
    else if (!group && t.is_virtual)
        virtual_class = t.k;
    if (!virtual_class)
        return;
    const dynamic_class *vd = analysis.dynamic_of(virtual_class);
    const dynamic_class *kd = analysis.dynamic_of(t.k);
    owned_array<vcall_entry> entries;
    vcall_functions(virtual_class, entries);
    for (const vcall_entry &e : entries) {
        const vtable_group &vg =
            analysis.vtable_groups[vd->first_group + e.group];
        int64_t own = static_cast<int64_t>(vg.offset) +
                      analysis.vtable_adjustments[vg.first_slot + e.slot];
        /* the final overrider in k's table for the virtual base's
           subobject, if k is not the virtual base itself */
        entity_index f = e.f;
        for (uint32_t h = 0; h < kd->group_count && t.k != virtual_class; h++) {
            const vtable_group &kg =
                analysis.vtable_groups[kd->first_group + h];
            if (kg.in_virtual_base == virtual_class &&
                kg.offset_in_virtual == vg.offset &&
                kg.subobject_class == vg.subobject_class)
                f = analysis.vtable_slots[kg.first_slot + e.slot];
        }
        int64_t location = 0;
        if (f == e.f) {
            out.append(own);
            continue;
        }
        if (!subobject_location(t, analysis.entity_table[f].enclosing,
                                location))
            vcall_unknown = true;
        out.append(location - at);
    }
}

void c_lowering::vcall_functions(entity_index v,
                                 owned_array<vcall_entry> &out) const
{
    const dynamic_class *vd = analysis.dynamic_of(v);
    for (uint32_t h = 0; h < vd->group_count; h++) {
        const vtable_group &vg = analysis.vtable_groups[vd->first_group + h];
        if (vg.in_virtual_base)
            continue;
        for (uint32_t k = 0; k < vg.slot_count; k++) {
            entity_index f = analysis.vtable_slots[vg.first_slot + k];
            bool repeated = false;
            for (const vcall_entry &e : out)
                repeated =
                    repeated || e.f == f || analysis.overrides_function(f, e.f);
            if (repeated)
                continue;
            vcall_entry e;
            e.f = f;
            e.group = h;
            e.slot = k;
            out.append(e);
        }
    }
}

entity_index c_lowering::virtual_slot_own(const vtable_group &g,
                                          uint32_t i) const
{
    const dynamic_class *vd = analysis.dynamic_of(g.in_virtual_base);
    for (uint32_t h = 0; vd && h < vd->group_count; h++) {
        const vtable_group &vg = analysis.vtable_groups[vd->first_group + h];
        if (!vg.in_virtual_base && vg.offset == g.offset_in_virtual &&
            vg.subobject_class == g.subobject_class)
            return analysis.vtable_slots[vg.first_slot + i];
    }
    return 0;
}

int64_t c_lowering::vcall_offset_offset(const vtable_group &g, uint32_t i) const
{
    if (!g.in_virtual_base || g.offset_in_virtual)
        return 0;
    entity_index own = virtual_slot_own(g, i);
    if (!own || own == analysis.vtable_slots[g.first_slot + i])
        return 0;
    owned_array<vcall_entry> entries;
    vcall_functions(g.in_virtual_base, entries);
    const dynamic_class *sd = analysis.dynamic_of(g.subobject_class);
    for (uint32_t j = 0; j < entries.size(); j++)
        if (entries[j].f == own ||
            analysis.overrides_function(own, entries[j].f))
            return -static_cast<int64_t>(analysis.pointer_size()) *
                   (3 + static_cast<int64_t>(sd->vbase_entry_count) + j);
    return 0;
}

/* f is t.k, one of its virtual bases, a non-virtual base of it or of
   one of its virtual bases (one such subobject) */
bool c_lowering::subobject_location(const table_spec &t, entity_index f,
                                    int64_t &out) const
{
    auto nonvirtual = [&](entity_index from, int64_t at) {
        while (from != f) {
            entity_index step = analysis.nonvirtual_step(from, f);
            at += static_cast<int64_t>(analysis.entity_table[step].offset);
            from = record(analysis.entity_table[step].type).entity;
        }
        return at;
    };
    if (f == t.k || analysis.nonvirtual_subobjects(t.k, f) == 1) {
        out = nonvirtual(t.k, 0);
        return true;
    }
    if (analysis.nonvirtual_subobjects(t.k, f))
        return false;
    uint32_t found = 0;
    const entity &x = analysis.entity_table[t.k];
    for (uint32_t i = 0; i < x.virtual_base_count; i++) {
        entity_index w =
            record(
                analysis
                    .entity_table
                        [analysis.class_virtual_bases[x.first_virtual_base + i]]
                    .type)
                .entity;
        if (w != f && analysis.nonvirtual_subobjects(w, f) != 1)
            continue;
        found++;
        out = nonvirtual(
            w, static_cast<int64_t>(virtual_base_offset(t.layout, w)) -
                   static_cast<int64_t>(t.base_offset));
    }
    return found == 1;
}

/* the table's groups in order: each included group's vbase and vcall
   offsets and its address point (UINT32_MAX for a group left out), and
   the table's length in pointers */
void c_lowering::table_shape(const table_spec &t, shape &out) const
{
    const dynamic_class *d = analysis.dynamic_of(t.k);
    out.length = 0;
    out.points.clear();
    out.offsets.clear();
    out.first_offset.clear();
    for (uint32_t g = 0; g < d->group_count; g++) {
        out.first_offset.append(static_cast<uint32_t>(out.offsets.size()));
        if (!group_included(t, g)) {
            out.points.append(UINT32_MAX);
            continue;
        }
        size_t before = out.offsets.size();
        group_offsets(t, g, out.offsets);
        out.length += static_cast<uint32_t>(out.offsets.size() - before) + 2;
        out.points.append(out.length);
        out.length += analysis.vtable_groups[d->first_group + g].slot_count;
    }
    out.first_offset.append(static_cast<uint32_t>(out.offsets.size()));
}

uint32_t c_lowering::table_length(const table_spec &t) const
{
    shape sh;
    table_shape(t, sh);
    return sh.length;
}

/* an array variable s of type array defined by its (index, index, value)
   runs */
void c_lowering::define_array(uint32_t s, ast_qualified_type array,
                              const owned_array<uint32_t> &runs)
{
    uint32_t count = static_cast<uint32_t>(runs.size());
    ast_builder_symbol_body(c, s,
                            make(AST_NODE_INITIALIZER, array, 0,
                                 ast_builder_list(c, runs.data(), count),
                                 count / 3, 1, 0));
    ast_builder_symbol_flags_add(c, s, AST_SYMBOL_FLAG_DEFINED);
}

uint32_t c_lowering::element_address(uint32_t s, ast_qualified_type array,
                                     uint32_t index)
{
    ast_builder_symbol_flags_add(c, s, AST_SYMBOL_FLAG_USED);
    ast_qualified_type vp = pointer_to(void_type);
    ast_qualified_type vpp = pointer_to(vp);
    uint32_t table = c_variable(c, s, array);
    address_taken(table);
    uint32_t element =
        make(AST_NODE_INDEX, vp, cast(table, vpp, AST_CAST_ARRAY_DECAY),
             to_ptrdiff(ast_builder_integer(
                 c, ast_builder_type_basic(AST_TYPE_INT), index)),
             0, 0, AST_NODE_FLAG_LVALUE);
    return address_of(element, vpp);
}

/*
 * [abi:vtable-ctor-vtt]: "The elements of the VTT array for a class D are
 * in this order: Primary virtual pointer ... Secondary VTTs: for each
 * direct non-virtual proper base class B of D that requires a VTT, in
 * declaration order, a sub-VTT for B-in-D, structured like the main VTT
 * for B ... Secondary virtual pointers: for each base class X which (a)
 * has virtual bases or is reachable along a virtual path from D, and (b)
 * is not a non-virtual primary base ... in inheritance graph preorder ...
 * Virtual VTTs: For each proper virtual base classes in inheritance graph
 * preorder, construct a sub-VTT".  The tables a sub-VTT's entries point
 * into are construction virtual tables, as clang's VTTBuilder makes them.
 */
void c_lowering::vtt_of(entity_index k, vtt_layout &out) const
{
    out.k = k;
    out.tables.clear();
    out.entries.clear();
    out.subs.clear();
    vtt_tables(out, k, 0, false);
}

void c_lowering::vtt_tables(vtt_layout &out, entity_index b, uint64_t at,
                            bool is_virtual) const
{
    const entity &x = analysis.entity_table[b];
    if (!x.virtual_base_count)
        return;
    if (b != out.k) {
        sub_vtt sub;
        sub.of = b;
        sub.offset = at;
        sub.index = static_cast<uint32_t>(out.entries.size());
        out.subs.append(sub);
    }
    uint32_t table = static_cast<uint32_t>(out.tables.size());
    table_spec spec;
    spec.k = b;
    spec.layout = out.k;
    spec.base_offset = at;
    spec.construction = b != out.k;
    spec.is_virtual = is_virtual;
    out.tables.append(spec);
    vtt_pointer(out, at, table);
    for (uint32_t i = 0; i < x.base_count; i++) {
        const entity &base =
            analysis.entity_table[analysis.class_bases[x.first_base + i]];
        if (!base.virtual_base)
            vtt_tables(out, record(base.type).entity, at + base.offset, false);
    }
    owned_array<entity_index> seen;
    vtt_secondary_pointers(out, b, at, false, table, seen);
    if (b == out.k) {
        seen.clear();
        vtt_virtual(out, b, seen);
    }
}

/* the entry for the subobject of class b at offset at: the address point
   of the group of the table whose subobject that is */
void c_lowering::vtt_pointer(vtt_layout &out, uint64_t at, uint32_t table) const
{
    const table_spec &t = out.tables[table];
    const dynamic_class *d = analysis.dynamic_of(t.k);
    for (uint32_t g = 0; g < d->group_count; g++)
        if (group_included(t, g) &&
            static_cast<int64_t>(t.base_offset) + group_location(t, g) ==
                static_cast<int64_t>(at)) {
            vtt_entry e;
            e.table = table;
            e.group = g;
            out.entries.append(e);
            return;
        }
}

void c_lowering::vtt_secondary_pointers(vtt_layout &out, entity_index b,
                                        uint64_t at, bool morally_virtual,
                                        uint32_t table,
                                        owned_array<entity_index> &seen) const
{
    const entity &x = analysis.entity_table[b];
    if (!x.virtual_base_count && !morally_virtual)
        return;
    const dynamic_class *d = analysis.dynamic_of(b);
    for (uint32_t i = 0; i < x.base_count; i++) {
        entity_index specifier = analysis.class_bases[x.first_base + i];
        const entity &base = analysis.entity_table[specifier];
        entity_index of = record(base.type).entity;
        if (!analysis.entity_table[of].dynamic)
            continue;
        bool base_morally_virtual = morally_virtual, nonvirtual_primary = false;
        uint64_t base_at = 0;
        if (base.virtual_base) {
            if (!first_time(seen, of))
                continue;
            base_at = virtual_base_offset(out.k, of);
            base_morally_virtual = true;
        } else {
            base_at = at + base.offset;
            nonvirtual_primary = d && d->primary_base == specifier;
        }
        if (!nonvirtual_primary &&
            (analysis.entity_table[of].virtual_base_count ||
             base_morally_virtual))
            vtt_pointer(out, base_at, table);
        vtt_secondary_pointers(out, of, base_at, base_morally_virtual, table,
                               seen);
    }
}

void c_lowering::vtt_virtual(vtt_layout &out, entity_index b,
                             owned_array<entity_index> &seen) const
{
    const entity &x = analysis.entity_table[b];
    for (uint32_t i = 0; i < x.base_count; i++) {
        const entity &base =
            analysis.entity_table[analysis.class_bases[x.first_base + i]];
        entity_index of = record(base.type).entity;
        if (base.virtual_base) {
            if (!first_time(seen, of))
                continue;
            vtt_tables(out, of, virtual_base_offset(out.k, of), true);
        }
        if (analysis.entity_table[of].virtual_base_count)
            vtt_virtual(out, of, seen);
    }
}

uint32_t c_lowering::sub_vtt_index(entity_index k, entity_index b,
                                   uint64_t offset) const
{
    vtt_layout layout;
    vtt_of(k, layout);
    for (const sub_vtt &sub : layout.subs)
        if (sub.of == b && sub.offset == offset)
            return sub.index;
    return UINT32_MAX;
}

/*
 * The definition of a virtual table: per included group, its vcall and
 * vbase offsets (furthest from the address point first), the offset to
 * top, the RTTI pointer (null with -fno-rtti), then a function
 * pointer per slot ([abi:vtable-components]).  A construction virtual
 * table's offsets to top are from k's subobject ([abi:vtable-ctor-entries]:
 * "The offset-to-top fields refer to B").
 */
bool c_lowering::table_definition(const table_spec &t, uint32_t s,
                                  token_index at)
{
    const dynamic_class *d = analysis.dynamic_of(t.k);
    shape sh;
    table_shape(t, sh);
    owned_array<uint32_t> runs;
    ast_qualified_type vp = pointer_to(void_type);
    auto entry = [&](uint32_t index, uint32_t value) {
        runs.append(index);
        runs.append(index);
        runs.append(value);
    };
    auto integer = [&](uint32_t index, int64_t value) {
        if (value)
            entry(index,
                  cast(ast_builder_integer(
                           c, ast_builder_type_ptrdiff(c),
                           static_cast<int64_t>(static_cast<uint64_t>(value))),
                       vp, AST_CAST_INTEGER_TO_POINTER));
    };
    for (uint32_t g = 0; g < d->group_count; g++) {
        uint32_t point = sh.points[g];
        if (point == UINT32_MAX)
            continue;
        const vtable_group &group = analysis.vtable_groups[d->first_group + g];
        /* the offsets, nearest the address point first; then the offset
           to top, "the displacement to the top of the object from the
           location within the object of the virtual table pointer that
           addresses this virtual table" */
        for (uint32_t i = sh.first_offset[g], k = 3; i < sh.first_offset[g + 1];
             i++, k++)
            integer(point - k, sh.offsets[i]);
        integer(point - 2, -group_location(t, g));
        /* [abi:vtable-components]: "typeinfo pointer": the type_info
           object of the class whose table this is ([impl.clang]: the same
           in each of its groups) */
        if (rtti) {
            uint32_t ti = typeinfo_address(
                analysis.entity_table[t.k].type.unqualified(), at);
            if (!ti)
                return false;
            entry(point - 1, ti);
        }
        for (uint32_t i = 0; i < group.slot_count; i++) {
            uint32_t slot = group.first_slot + i;
            entity_index f = analysis.vtable_slots[slot];
            int64_t adjustment = analysis.vtable_adjustments[slot];
            /* [abi:mangling-special-thunks]: an overrider outside the
               virtual base whose table this is: its virtual thunk */
            int64_t vcall = vcall_offset_offset(group, i);
            /* [abi:vtable-components]: a virtual destructor's pair of
               entries, the complete object destructor (the variant its
               calls use: D2 for an inline one, as clang) and the deleting
               one; [abi:pure-virtual]: a pure virtual function's slot */
            bool destructor =
                analysis.entity_table[f].special == special_member::destructor;
            bool deleting =
                destructor && i && analysis.vtable_slots[slot - 1] == f;
            ast_qualified_type ft = entity_type(f, at);
            uint32_t target = 0;
            if (destructor && analysis.entity_table[f].deleted) {
                /* [abi:pure-virtual]: a deleted virtual function's slot
                   points at __cxa_deleted_virtual, as for a destructor
                   defined as deleted ([class.dtor]/6) */
                ft = ast_builder_type_function(c, void_type, nullptr, 0, 0);
                if (!deleted_virtual) {
                    deleted_virtual = ast_builder_symbol(
                        c, AST_SYMBOL_FUNCTION, "__cxa_deleted_virtual", 21, ft,
                        AST_STORAGE_NONE, AST_SYMBOL_FLAG_GLOBAL, 0);
                    ast_builder_toplevel(c, deleted_virtual);
                }
                target = deleted_virtual;
            } else if (analysis.entity_table[f].pure && !destructor) {
                ft = ast_builder_type_function(c, void_type, nullptr, 0, 0);
                if (!pure_virtual) {
                    pure_virtual = ast_builder_symbol(
                        c, AST_SYMBOL_FUNCTION, "__cxa_pure_virtual", 18, ft,
                        AST_STORAGE_NONE, AST_SYMBOL_FLAG_GLOBAL, 0);
                    ast_builder_toplevel(c, pure_virtual);
                }
                target = pure_virtual;
            } else if (deleting && !deleting_destructor(f, at, false)) {
                return false;
            } else if (vcall || adjustment) {
                int variant = 2;
                if (destructor)
                    variant = !deleting;
                target = thunk(f, adjustment, at, vcall, variant);
            } else if (deleting) {
                target = deleting_symbols[f];
            } else if (destructor) {
                target = special_callee(f, at);
            } else {
                target = symbol(f, at);
            }
            if (!target)
                return false;
            ast_builder_symbol_flags_add(c, target,
                                         AST_SYMBOL_FLAG_USED |
                                             AST_SYMBOL_FLAG_ADDRESS_TAKEN);
            uint32_t function =
                cast(make(AST_NODE_VARIABLE, ft, target, 0, 0, 0, 0),
                     pointer_to(ft), AST_CAST_FUNCTION_DECAY);
            entry(point + i, cast(function, vp, AST_CAST_POINTER_TO_POINTER));
        }
    }
    if (vcall_unknown)
        return fail("a vcall offset whose final overrider's subobject is not "
                    "found once",
                    at);
    define_array(s, ast_builder_type_array(c, vp, sh.length, true), runs);
    return true;
}

/*
 * [abi:vtable-ctor-layout]: "The construction virtual tables for a
 * complete object are emitted in the same object file as the virtual
 * table", and the VTT with them, with the virtual table's linkage
 * ([impl.clang]: _ZTT <type>, _ZTC <type> <offset> _ <base type>).
 */
uint32_t c_lowering::vtt(entity_index k, token_index at)
{
    if (!vtable(k, at))
        return 0;
    if (!vtts[k])
        return fail("a VTT not made with its virtual table", at);
    ast_builder_symbol_flags_add(c, vtts[k], AST_SYMBOL_FLAG_USED);
    return vtts[k];
}

uint32_t c_lowering::vtt_define(entity_index k, uint32_t table, int storage,
                                uint32_t flags, token_index at)
{
    owned_buffer name;
    if (!itanium_vtt_name(analysis, tokens.pp, k, name.get()))
        return fail("a VTT whose symbol is not mangled", at);
    vtt_layout layout;
    vtt_of(k, layout);
    ast_qualified_type vp = pointer_to(void_type);
    ast_qualified_type array =
        ast_builder_type_array(c, vp, layout.entries.size(), true);
    uint32_t s = ast_builder_symbol(c, AST_SYMBOL_VARIABLE, name.data(),
                                    name.size(), array, storage, flags, 0);
    vtts[k] = s;
    ast_builder_toplevel(c, s);
    if (storage == AST_STORAGE_EXTERN)
        return s;
    /* [impl.clang]: emitted with the virtual table, used or not */
    ast_builder_symbol_keep(c, s);
    /* the tables, then the entries */
    owned_array<uint32_t> symbols_of;
    owned_array<ast_qualified_type> types_of;
    for (const table_spec &t : layout.tables) {
        if (!t.construction) {
            symbols_of.append(table);
            types_of.append(vtable_type(k));
            continue;
        }
        name.clear();
        if (!itanium_construction_vtable_name(analysis, tokens.pp, k,
                                              t.base_offset, t.k, name.get()))
            return fail("a construction virtual table whose symbol is not "
                        "mangled yet",
                        at);
        ast_qualified_type ta =
            ast_builder_type_array(c, vp, table_length(t), true);
        uint32_t ts = ast_builder_symbol(c, AST_SYMBOL_VARIABLE, name.data(),
                                         name.size(), ta, storage, flags, 0);
        ast_builder_toplevel(c, ts);
        if (!table_definition(t, ts, at))
            return 0;
        symbols_of.append(ts);
        types_of.append(ta);
    }
    owned_array<uint32_t> runs;
    shape sh;
    for (uint32_t i = 0; i < layout.entries.size(); i++) {
        const vtt_entry &e = layout.entries[i];
        table_shape(layout.tables[e.table], sh);
        uint32_t point = sh.points[e.group];
        runs.append(i);
        runs.append(i);
        runs.append(
            cast(element_address(symbols_of[e.table], types_of[e.table], point),
                 vp, AST_CAST_POINTER_TO_POINTER));
    }
    define_array(s, array, runs);
    return s;
}

} // namespace cplusplus::lowering
