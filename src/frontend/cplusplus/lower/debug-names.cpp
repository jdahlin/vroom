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
        name = x.details().linkage_name;
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

} // namespace cplusplus::lowering
