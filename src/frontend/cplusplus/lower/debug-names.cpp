/* debug-names.cpp: -g: how a debugger is told a class and an entity's scope: the class's name
   with its template arguments spelled, and the namespaces around it (lower-internal.h;
   doc/PLAN-debuginfo.md D4b) */
#include "lower-internal.h"

namespace cplusplus::lowering
{

/* the namespaces around e, outermost first, "a::b", an unnamed one "?" */
void c_lowering::debug_scope(entity_index e, vbuffer *out) const
{
    entity_index around = analysis.entity_table[e].enclosing;
    while (around &&
           analysis.entity_table[around].kind != entity_kind::namespace_entity)
        around = analysis.entity_table[around].enclosing;
    if (!around)
        return;
    debug_scope(around, out);
    if (out->n)
        vbuffer_string_append(out, "::");
    const entity &x = analysis.entity_table[around];
    if (!x.name) {
        vbuffer_string_append(out, "?");
        return;
    }
    size_t length = 0;
    const char *text = analysis.identifier_text(x.name, &length);
    vbuffer_append(out, text, length);
}

/* a class's or enumeration's name within its namespace: the classes around
   it, its identifier and, a specialization's, its template arguments */
void c_lowering::debug_class_name(entity_index e, vbuffer *out)
{
    const entity x = analysis.entity_table[e];
    if (x.enclosing &&
        analysis.entity_table[x.enclosing].kind == entity_kind::class_type) {
        debug_class_name(x.enclosing, out);
        vbuffer_string_append(out, "::");
    }
    uint32_t name = x.name;
    if (!name)
        name = x.linkage_name;
    if (!name)
        return;
    size_t length = 0;
    const char *text = analysis.identifier_text(name, &length);
    vbuffer_append(out, text, length);
    if (!x.instantiation)
        return;
    vbuffer_string_append(out, "<");
    bool first = true;
    uint32_t count = analysis.specialization_argument_count(e);
    for (uint32_t i = 0; i < count; i++) {
        uint32_t elements = analysis.specialization_argument_elements(e, i);
        bool pack = analysis.specialization_argument_is_pack(e, i);
        for (uint32_t k = 0; k < elements; k++) {
            uint32_t element = 0;
            if (pack)
                element = k + 1;
            if (!first)
                vbuffer_string_append(out, ", ");
            first = false;
            if (analysis.specialization_argument_is_type(e, i, element))
                debug_type_name(analysis.specialization_argument(e, i, element),
                                out);
            else if (analysis.specialization_argument_is_value(e, i, element))
                debug_value_name(
                    analysis.specialization_argument(e, i, element),
                    analysis.specialization_argument_value(e, i, element), out);
            else
                vbuffer_string_append(out, "?");
        }
    }
    vbuffer_string_append(out, ">");
}

/* a non-type template argument of integral type */
void c_lowering::debug_value_name(qualified_type t, uint64_t value,
                                  vbuffer *out) const
{
    char text[24];
    const struct type &r = record(t);
    if (r.kind == type_kind::fundamental &&
        r.fundamental == fundamental_type::bool_) {
        const char *word = "false";
        if (value)
            word = "true";
        vbuffer_string_append(out, word);
        return;
    }
    snprintf(text, sizeof text, "%lld", static_cast<long long>(value));
    vbuffer_string_append(out, text);
}

/* a template argument's type; "?" for the kinds not spelled yet */
void c_lowering::debug_type_name(qualified_type t, vbuffer *out)
{
    /* (a copy: naming an argument may make types, which moves the table) */
    const struct type r = record(t);
    if (t.cv() & cv_const)
        vbuffer_string_append(out, "const ");
    if (t.cv() & cv_volatile)
        vbuffer_string_append(out, "volatile ");
    switch (r.kind) {
    case type_kind::fundamental:
        vbuffer_string_append(out, fundamental_type_name(r.fundamental));
        break;
    case type_kind::class_type:
    case type_kind::enumeration: {
        owned_buffer scope;
        debug_scope(r.entity, scope.get());
        if (scope.size()) {
            vbuffer_append(out, scope.data(), scope.size());
            vbuffer_string_append(out, "::");
        }
        debug_class_name(r.entity, out);
        break;
    }
    case type_kind::pointer:
        debug_type_name(r.operand, out);
        vbuffer_string_append(out, " *");
        break;
    case type_kind::lvalue_reference:
        debug_type_name(r.operand, out);
        vbuffer_string_append(out, " &");
        break;
    case type_kind::rvalue_reference:
        debug_type_name(r.operand, out);
        vbuffer_string_append(out, " &&");
        break;
    default:
        vbuffer_string_append(out, "?");
        break;
    }
}

/* -g: every enumeration's enumerators, chained in declaration order, found
   in one pass over the entities when the first is asked for */
void c_lowering::gather_enumerators()
{
    if (enumerators_gathered)
        return;
    enumerators_gathered = true;
    /* (backwards, so that each chain is in order) */
    for (uint32_t i = static_cast<uint32_t>(analysis.entity_table.size());
         i-- > 1;) {
        const entity &y = analysis.entity_table[entity_index(i)];
        if (y.kind != entity_kind::enumerator ||
            record(y.type).kind != type_kind::enumeration)
            continue;
        entity_index of = record(y.type).entity;
        next_enumerator[entity_index(i)] = first_enumerator[of];
        first_enumerator[of] = entity_index(i);
    }
}

/* the enumeration e as -g describes it (made once) */
ast_qualified_type c_lowering::debug_enum_type(entity_index e, token_index at)
{
    if (debug_enums[e])
        return debug_enums[e];
    const entity x = analysis.entity_table[e];
    ast_qualified_type underlying = fundamental(x.underlying, at);
    gather_enumerators();
    owned_array<uint32_t> enumerators;
    for (entity_index i = first_enumerator[e]; i; i = next_enumerator[i]) {
        const entity &y = analysis.entity_table[i];
        size_t length = 0;
        const char *text = analysis.identifier_text(y.name, &length);
        enumerators.append(ast_builder_debug_enumerator(
            c, text, length, static_cast<int64_t>(y.value)));
    }
    owned_buffer name, scope;
    debug_class_name(e, name.get());
    debug_scope(e, scope.get());
    ast_qualified_type made = ast_builder_type_debug_enum(
        c, name.data(), name.size(), scope.data(), scope.size(), underlying,
        enumerators.data(), static_cast<uint32_t>(enumerators.size()),
        x.scoped);
    debug_enums[e] = made;
    return made;
}

/* the type -g says for t where C's lost an enumeration: of one, or a
   pointer, reference or array of one; 0 where C's says it all */
ast_qualified_type c_lowering::debug_type(qualified_type t, token_index at)
{
    if (!ast_builder_debugging(c))
        return 0;
    const struct type r = record(t);
    ast_qualified_type made = 0;
    switch (r.kind) {
    case type_kind::enumeration:
        if (analysis.entity_table[r.entity].underlying ==
            fundamental_type::none)
            return 0;
        made = debug_enum_type(r.entity, at);
        break;
    case type_kind::pointer:
    case type_kind::lvalue_reference:
    case type_kind::rvalue_reference:
        made = debug_type(r.operand, at);
        if (made)
            made = ast_builder_type_pointer(c, made);
        break;
    case type_kind::array:
        made = debug_type(r.operand, at);
        if (made)
            made = ast_builder_type_array(c, made, r.bound, r.bound_known);
        break;
    default:
        break;
    }
    if (made && static_cast<int>(t.cv()) != 0)
        made = ast_builder_type_qualified(c, made, static_cast<int>(t.cv()));
    return made;
}

/* -g: where the direct virtual bases of the class e are: through its
   virtual table ([abi:vtable-components]: the offset of the i-th virtual
   base is 3 + i entries below the address point, virtual_base_address) */
void c_lowering::debug_virtual_bases(entity_index e, ast_qualified_type made,
                                     token_index at)
{
    if (!ast_builder_debugging(c))
        return;
    const entity x = analysis.entity_table[e];
    owned_array<uint32_t> pairs;
    for (uint32_t i = 0; i < x.base_count; i++) {
        const entity b =
            analysis.entity_table[analysis.class_bases[x.first_base + i]];
        if (!b.virtual_base)
            continue;
        const dynamic_class *d = analysis.dynamic_of(e);
        entity_index base = record(b.type).entity;
        uint32_t index = 0;
        while (d && index < d->vbase_entry_count &&
               analysis.vbase_entries[d->first_vbase_entry + index] != base)
            index++;
        if (!d || index == d->vbase_entry_count)
            continue;
        pairs.append(type(b.type, at));
        pairs.append((3 + index) * 8);
    }
    if (pairs.size())
        ast_builder_type_record_debug_virtual_bases(
            c, made, pairs.data(), static_cast<uint32_t>(pairs.size() / 2));
}

} // namespace cplusplus::lowering
