/*
 * semantic-tree.cpp: the semantic tree of a translation unit
 * (doc/PLAN-cxx-sema.md 3.2).
 *
 * Analysis builds it as it checks: each expression's node is made where
 * its meaning is, and each conversion analysis applies to an operand wraps
 * the operand's node in a conversion node, so the tree says every implicit
 * step the standard describes ([conv]/1: "Standard conversions are
 * implicit conversions with built-in meaning").  Nodes of a declaration
 * that turned out unsupported stay in the table, unused.
 */
#include "../cplusplus-internal.h"
#include "frontend/cplusplus/sema.h"

namespace cplusplus
{

semantic_tree::semantic_tree()
{
    /* node 0 and list index 0 are none */
    nodes.append(semantic_node());
    lists.append(0);
}

const char *semantic_kind_name(semantic_kind kind)
{
    static const char *const names[] = {
        "none",
        "constant",
        "string_literal",
        "entity",
        "conversion",
        "vector_shuffle",
        "complex_value",
        "complex_part",
        "compound_literal",
        "unary",
        "address",
        "indirection",
        "increment",
        "binary",
        "logical_and",
        "logical_or",
        "assignment",
        "conditional",
        "comma",
        "call",
        "subscript",
        "member",
        "bound_member_function",
        "member_pointer",
        "member_pointer_access",
        "zero",
        "initializer_list",
        "default_member_initializer",
        "bind_reference",
        "construct",
        "destroy",
        "new_object",
        "allocated",
        "array_count",
        "array_size",
        "array_index",
        "delete_object",
        "throw_exception",
        "exception_object",
        "va_arg",
        "label_address",
        "gnu_asm_operand",
        "source_location",
        "predefined_name",
        "default_argument",
        "vtable_pointer_store",
        "variable_size",
        "block",
        "expression_statement",
        "declaration_statement",
        "declarations",
        "if_statement",
        "while_statement",
        "do_statement",
        "for_statement",
        "array_loop",
        "switch_statement",
        "case_label",
        "statement_expression",
        "default_label",
        "identifier_label",
        "goto_statement",
        "indirect_goto_statement",
        "break_statement",
        "continue_statement",
        "return_statement",
        "gnu_asm_statement",
        "try_statement",
        "handler",
        "null_statement",
        "condition_declaration",
        "function_body",
    };
    size_t k = static_cast<size_t>(kind);
    static_assert(sizeof names / sizeof names[0] ==
                      static_cast<size_t>(semantic_kind::function_body) + 1,
                  "one name for each semantic_kind");
    if (k >= sizeof names / sizeof names[0])
        return "?";
    return names[k];
}

uint32_t semantic_tree::add(const semantic_node &n)
{
    uint32_t index = static_cast<uint32_t>(nodes.size());
    nodes.append(n);
    return index;
}

uint32_t semantic_tree::list(const uint32_t *items, size_t count)
{
    uint32_t first = static_cast<uint32_t>(lists.size());
    for (size_t i = 0; i < count; i++)
        lists.append(items[i]);
    return first;
}

void semantic_analysis::require_destructor_definition(qualified_type type)
{
    while (type_table[type.unqualified()].kind == type_kind::array)
        type = type_table[type.unqualified()].operand;
    const auto &object = type_table[type.unqualified()];
    if (object.kind != type_kind::class_type)
        return;
    entity_index destructor = entity_table[object.entity].details().destructor;
    if (destructor) {
        /* (a postponed destructor body ill-formed for these arguments is
           this declaration's error, instantiate_pending) */
        if (!instantiation_depth &&
            !entity_table[destructor].details().member_needed_by)
            entity_table[destructor].edit().member_needed_by =
                current_declaration;
        mark_needed(destructor);
    }
}

void semantic_analysis::mark_needed(entity_index function)
{
    if (needs_deferred_by) {
        /* ([basic.def.odr]: a recursive function's use of itself in a
           body nothing is known to use makes it no more used) */
        if (function == needs_deferred_by)
            return;
        deferred_need d;
        d.by = needs_deferred_by;
        d.needed = function;
        deferred_needs.append(d);
        return;
    }
    entity_table[function].member_definition_needed = true;
}

void semantic_analysis::default_argument_used(uint32_t node)
{
    for (const default_argument_need_range &r : default_argument_need_ranges)
        if (r.node == node)
            for (uint32_t i = 0; i < r.count; i++) {
                entity_index f = default_argument_needed[r.first + i];
                mark_needed(f);
                /* [temp.inst]/12: the default argument was analysed
                   unevaluated; its use in a call needs a function
                   template specialization's definition.  Constructors'
                   only: the list also has functions named in operands not
                   evaluated (std::declval, whose definition is ill-formed) */
                if (entity_table[f].special == special_member::constructor &&
                    need_specialization_definition(current_declaration, f) !=
                        analysis_result::well_formed)
                    return;
            }
}

/* [class.virtual]/17 with [basic.def.odr]/5: a class's virtual functions
   are needed by its virtual table, which an object of the class (or of a
   class derived from it, whose constructor uses the base's) wants: some
   constructor of the class is needed and defined here, or its key function
   is [impl.clang] (clang emits the table, or uses its entries, where a
   constructor is defined; a constructor only declared here leaves that to
   the translation unit that defines it) */
bool semantic_analysis::virtual_table_needed(entity_index c) const
{
    for (entity_index k = entity_table[c].details().first_constructor; k;
         k = entity_table[k].details().next_constructor) {
        const entity &x = entity_table[k];
        if (x.member_definition_needed && (x.defined || x.implicit))
            return true;
    }
    const dynamic_class *d = dynamic_of(c);
    return d && d->key_function && entity_table[d->key_function].defined;
}

/* the needs of a defaulted special member now needed (or virtual: its
   vtable needs it, [temp.inst]/11), passed on; whether any was */
bool semantic_analysis::pass_deferred_needs()
{
    bool passed = false;
    for (const deferred_need &d : deferred_needs) {
        const entity &by = entity_table[d.by];
        if ((by.member_definition_needed ||
             (by.virtual_ && virtual_table_needed(by.enclosing))) &&
            !entity_table[d.needed].member_definition_needed) {
            entity_table[d.needed].member_definition_needed = true;
            passed = true;
        }
    }
    return passed;
}

uint32_t semantic_analysis::expression_node(semantic_kind kind,
                                            const expression_meaning &m,
                                            token_index at, uint32_t a,
                                            uint32_t b, uint8_t op)
{
    if (!unevaluated) {
        entity_index function = 0;
        if ((kind == semantic_kind::entity ||
             kind == semantic_kind::member_pointer) &&
            a && entity_table[entity_index_in(a)].kind == entity_kind::function)
            function = entity_index_in(a);
        else if (kind == semantic_kind::construct ||
                 kind == semantic_kind::destroy)
            function = entity_index_in(b);
        /* ([basic.def.odr]/4: an overload set named before overload
           resolution is no use of its first function; the call's node
           of the one chosen is) */
        else if (kind == semantic_kind::bound_member_function && b &&
                 !entity_table[entity_index_in(b)].details().next_overload)
            function = entity_index_in(b);
        else if ((kind == semantic_kind::entity) && a &&
                 entity_table[entity_index_in(a)].kind == entity_kind::variable)
            function = entity_index_in(a);
        if (function) {
            if (!instantiation_depth &&
                !entity_table[function].details().member_needed_by)
                entity_table[function].edit().member_needed_by =
                    current_declaration;
            mark_needed(function);
        }
        require_complete(m.type);
        /* [class.dtor]/15 and [expr.new]/10: the object a non-array
           new-expression makes is not destroyed by it: no use of its
           destructor */
        if (m.category == value_category::prvalue &&
            !(kind == semantic_kind::construct && a && a == new_object_made))
            require_destructor_definition(m.type);
    } else if (default_argument_unevaluated &&
               unevaluated == default_argument_unevaluated) {
        /* (a default argument's: needed by the call that uses it) */
        entity_index function = 0;
        if ((kind == semantic_kind::entity ||
             kind == semantic_kind::member_pointer) &&
            a && entity_table[entity_index_in(a)].kind == entity_kind::function)
            function = entity_index_in(a);
        else if (kind == semantic_kind::construct ||
                 kind == semantic_kind::destroy ||
                 kind == semantic_kind::bound_member_function)
            function = entity_index_in(b);
        if (function)
            default_argument_needed.append(function);
        if (m.category == value_category::prvalue) {
            qualified_type object = m.type;
            while (type_table[object.unqualified()].kind == type_kind::array)
                object = type_table[object.unqualified()].operand;
            const struct type &record = type_table[object.unqualified()];
            if (record.kind == type_kind::class_type &&
                entity_table[record.entity].details().destructor)
                default_argument_needed.append(
                    entity_table[record.entity].details().destructor);
        }
    }
    semantic_node n;
    n.kind = kind;
    n.op = op;
    n.category = m.category;
    n.evaluation = m.evaluation;
    n.type = m.type;
    n.at = at;
    n.a = a;
    n.b = b;
    n.value = m.value;
    return semantics.add(n);
}

void semantic_analysis::converted(expression_meaning &m, conversion_kind kind)
{
    /* the value of another type is no bit-field's */
    if (kind != conversion_kind::lvalue_to_rvalue)
        m.bit_field_width = 0;
    if (!m.node)
        return;
    m.node = expression_node(semantic_kind::conversion, m, semantics[m.node].at,
                             m.node, 0, static_cast<uint8_t>(kind));
}

uint32_t semantic_analysis::statement_node(semantic_kind kind, token_index at,
                                           uint32_t a, uint32_t b, uint32_t c,
                                           uint32_t d)
{
    if (kind == semantic_kind::declaration_statement)
        require_destructor_definition(entity_table[entity_index_in(a)].type);
    semantic_node n;
    n.kind = kind;
    n.at = at;
    n.a = a;
    n.b = b;
    n.c = c;
    n.d = d;
    return semantics.add(n);
}

token_index semantic_analysis::first_token(SyntaxElement c) const
{
    if (c.is_token())
        return c.token();
    return c.first_token();
}

} // namespace cplusplus
