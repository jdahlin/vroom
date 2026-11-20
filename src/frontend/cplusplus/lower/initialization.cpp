/* initialization.cpp: initialization: of an object from its initializer, temporaries, full-expressions, references, initializer lists (lower-internal.h) */
#include "lower-internal.h"

namespace cplusplus::lowering
{

/* the initializer n of an object: a prvalue there initializes the object
   itself ([dcl.init]/17.6.1) */
uint32_t c_lowering::initializer(uint32_t n)
{
    uint32_t outer = result_object;
    result_object = n;
    uint32_t lowered = expression(n);
    result_object = outer;
    return lowered;
}

uint32_t c_lowering::construct_at(const semantic_node &n, uint32_t object,
                                  uint32_t vtt, token_index at)
{
    owned_array<uint32_t> arguments;
    const uint32_t *items = analysis.semantics.items(n.first);
    for (uint32_t i = 0; i < n.count; i++) {
        uint32_t a = expression(items[i]);
        if (!a)
            return 0;
        arguments.append(a);
    }
    uint32_t call =
        special_call(n.b, address_of(object, pointer_to(type_of(object))),
                     arguments.data(), n.count, at, vtt);
    if (!call || !n.op)
        return call;
    /* [dcl.init]/8.2: the object is zero-initialized, then the
       implicit default constructor runs on it */
    if (holds_data_member_pointer(n.type))
        return fail("a zero-initialized object with a pointer to data member "
                    "in it",
                    n.at);
    ast_qualified_type t = AST_QUALIFIED_TYPE_UNQUALIFIED(type_of(object));
    uint32_t zero = make(AST_NODE_ZERO, t, 0, 0, 0, 0, 0);
    return comma(type_of(call), assign(t, object, zero), call);
}

/* [dcl.init]/17.6.1: the prvalue n initializes the object in symbol s
   (of C type t) itself: a constructor's call on it, a conditional's
   operand chosen at run time initializing it ([expr.cond]/7), else its
   value stored */
uint32_t c_lowering::initialize_into(uint32_t s, qualified_type object_type,
                                     ast_qualified_type t, uint32_t n,
                                     bool through)
{
    const semantic_node &x = node(n);
    uint32_t object = c_variable(c, s, t);
    if (through)
        object = dereference(c_variable(c, s, pointer_to(t)), t);
    t = AST_QUALIFIED_TYPE_UNQUALIFIED(t);
    /* a constructor's call on an object the tree names (new's) */
    if (x.kind == semantic_kind::construct && x.a)
        return expression(n);
    qualified_type element;
    element_destructor(object_type, element);
    if (x.kind == semantic_kind::construct && !x.a) {
        uint64_t count = element_count(object_type, element);
        if (!count)
            return construct_at(x, object, 0, x.at);
        /* [dcl.init]/7.2: each element, first to last */
        uint32_t loop =
            each_element(s, t, type(element, x.at), count, false, x.b, x.at);
        if (!loop)
            return 0;
        return statement_expression(v, block(&loop, 1));
    }
    if (x.kind == semantic_kind::conditional && x.b && !is_glvalue(x)) {
        uint32_t cond = truth(expression(x.a));
        conditional_depth++;
        uint32_t b = initialize_into(s, object_type, t, x.b, through);
        uint32_t c_ = initialize_into(s, object_type, t, x.c, through);
        conditional_depth--;
        if (!cond || !b || !c_)
            return 0;
        return make(AST_NODE_CONDITIONAL, v, cond, cast(b, v, AST_CAST_VOID),
                    cast(c_, v, AST_CAST_VOID), 0, 0);
    }
    uint32_t value = 0;
    if (x.kind == semantic_kind::initializer_list)
        /* a braced-init-list's object, its value stored */
        value = compound_literal(n);
    else if (x.kind == semantic_kind::zero &&
             record(x.type).kind != type_kind::class_type)
        value = scalar_zero(x.type, t, x.at);
    else
        value = initializer(n);
    if (!value)
        return 0;
    return assign(t, object, value);
}

/* [dcl.init]/6: "To zero-initialize an object ... of scalar type T means
   the object is initialized to the value obtained by converting the
   integer literal 0 to T" */
uint32_t c_lowering::scalar_zero(qualified_type object, ast_qualified_type u,
                                 token_index at)
{
    const struct type &r = record(object);
    uint32_t zero =
        ast_builder_integer(c, ast_builder_type_basic(AST_TYPE_INT), 0);
    if (r.kind == type_kind::pointer)
        return cast(zero, u, AST_CAST_NULL_POINTER);
    if (r.kind == type_kind::pointer_to_member)
        return null_member_pointer(object, at);
    if (r.kind == type_kind::fundamental &&
        fundamental_type_is_floating(r.fundamental))
        return cast(zero, u, AST_CAST_INTEGER_TO_FLOAT);
    return cast(zero, u, AST_CAST_INTEGER);
}

/* [basic.lval]/7: a class prvalue where a glvalue is wanted, materialized
   in a temporary of the full-expression ([class.temporary]/2) */
uint32_t c_lowering::temporary_object(uint32_t n)
{
    const semantic_node &x = node(n);
    if (!collecting)
        return fail("a class temporary outside a statement", x.at);
    ast_qualified_type t = type(x.type, x.at);
    uint32_t held = full_expression_temporary(t);
    entity_index destructor =
        analysis.entity_table[record(x.type).entity].destructor;
    uint32_t flag = 0;
    if (destructor)
        flag = constructed_flag();
    uint32_t made = mark_constructed(initialize_into(held, x.type, t, n), flag);
    if (!made)
        return 0;
    if (destructor)
        destroyed_temporary(x, held, x.at, flag);
    ast_qualified_type pointer = pointer_to(t);
    uint32_t variable = c_variable(c, held, t);
    return dereference(comma(pointer, made, address_of(variable, pointer)), t);
}

entity_index c_lowering::element_destructor(qualified_type type,
                                            qualified_type &object) const
{
    object = type;
    while (record(object).kind == type_kind::array)
        object = record(object).operand;
    if (record(object).kind != type_kind::class_type)
        return 0;
    return analysis.entity_table[record(object).entity].destructor;
}

bool c_lowering::has_mutable_subobject(qualified_type type) const
{
    qualified_type object = type;
    while (record(object).kind == type_kind::array)
        object = record(object).operand;
    if (record(object).kind != type_kind::class_type)
        return false;
    const entity &k = analysis.entity_table[record(object).entity];
    for (uint32_t i = 0; i < k.member_count; i++) {
        const entity &m =
            analysis.entity_table[analysis.class_members[k.first_member + i]];
        if (m.mutable_member || has_mutable_subobject(m.type))
            return true;
    }
    return false;
}

uint32_t c_lowering::full_expression_temporary(ast_qualified_type t)
{
    uint32_t temporary =
        ast_builder_symbol(c, AST_SYMBOL_VARIABLE, "", 0, t, AST_STORAGE_AUTO,
                           AST_SYMBOL_FLAG_ADDRESS_TAKEN, 1);
    temporaries.append(c_declare(c, temporary));
    return temporary;
}

void c_lowering::destroyed_temporary(const semantic_node &n, uint32_t temporary,
                                     token_index at, uint32_t flag)
{
    if (&n != extended_temporary) {
        uint32_t destroyed = destruction(temporary, n.type, at);
        if (destroyed)
            destructions.append(if_constructed(destroyed, flag));
        return;
    }
    /* [class.temporary]/6: "The temporary object to which the reference
       is bound ... persists for the lifetime of the reference" */
    extended_temporary = nullptr;
    scope_record r;
    r.kind = scope_record::object;
    r.symbol = temporary;
    r.object_type = n.type;
    r.flag = flag;
    r.owner = declaring;
    live.append(r);
}

/* the automatic variable a return statement's operand copies (a copy
   constructor's call on it, as [class.copy.elision]/1.1 allows to omit),
   0 if it is not one */
entity_index c_lowering::returned_variable(uint32_t operand) const
{
    if (!operand)
        return 0;
    const semantic_node &n = node(operand);
    uint32_t argument = 0;
    if (n.kind == semantic_kind::conversion &&
        static_cast<conversion_kind>(n.op) == conversion_kind::lvalue_to_rvalue)
        /* a class trivially copyable but non-trivial for the purposes of
           calls (a non-trivial destructor): its copy is the bytes, still
           elided as clang does */
        argument = n.a;
    else if (n.kind != semantic_kind::construct || n.a || n.count != 1)
        return 0;
    else
        argument = analysis.semantics.items(n.first)[0];
    if (node(argument).kind == semantic_kind::bind_reference)
        argument = node(argument).a;
    const semantic_node &named = node(argument);
    if (named.kind != semantic_kind::entity)
        return 0;
    const entity &x = analysis.entity_table[named.a];
    if (x.kind != entity_kind::variable || !x.automatic ||
        x.type.unqualified() != n.type.unqualified() || x.type.cv() ||
        analysis.entity_table[x.enclosing].parameter_scope)
        return 0;
    return named.a;
}

/* [class.copy.elision]/1.1 as clang applies it: every return statement
   names the same variable */
void c_lowering::find_named_return(uint32_t statement, entity_index &found,
                                   bool &mixed) const
{
    if (!statement || statement >= analysis.semantics.size())
        return;
    const semantic_node &n = node(statement);
    if (n.kind < semantic_kind::block || n.kind > semantic_kind::null_statement)
        return;
    if (n.kind == semantic_kind::return_statement) {
        entity_index e = returned_variable(n.a);
        if (!e || (found && found != e))
            mixed = true;
        found = e;
        return;
    }
    if (n.kind == semantic_kind::block ||
        n.kind == semantic_kind::declarations) {
        const uint32_t *items = analysis.semantics.items(n.first);
        for (uint32_t i = 0; i < n.count; i++)
            find_named_return(items[i], found, mixed);
        return;
    }
    if (n.kind == semantic_kind::declaration_statement)
        return;
    find_named_return(n.a, found, mixed);
    find_named_return(n.b, found, mixed);
    find_named_return(n.c, found, mixed);
    find_named_return(n.d, found, mixed);
}

/* [expr.call]/7: "When a function is called, each parameter is
   initialized with its corresponding argument"; [abi:calls]: a class
   passed by address is a temporary the caller constructs, whose address
   is the argument, destroyed at the end of the full-expression
   ([class.temporary]/4; clang's, as Itanium's caller-destroyed rule).
   (construct the temporary, &temporary) */
uint32_t c_lowering::indirect_argument(uint32_t argument,
                                       qualified_type parameter, token_index at)
{
    uint32_t held = temporary_object(argument);
    if (!held)
        return 0;
    return address_of(held, pointer_to(type(parameter, at)));
}

uint32_t c_lowering::full_expression(uint32_t e, bool discarded,
                                     owned_array<uint32_t> &before,
                                     owned_array<uint32_t> &after)
{
    collection k = collect();
    uint32_t lowered = 0;
    if (discarded)
        lowered = discard(e);
    else
        lowered = initializer(e);
    collected(k, before, after);
    return lowered;
}

c_lowering::collection c_lowering::collect()
{
    collection k;
    k.outer = collecting;
    k.first = temporaries.size();
    k.first_destruction = destructions.size();
    collecting = true;
    return k;
}

void c_lowering::collected(const collection &k, owned_array<uint32_t> &before,
                           owned_array<uint32_t> &after)
{
    collecting = k.outer;
    for (size_t i = k.first; i < temporaries.size(); i++)
        before.append(temporaries[i]);
    /* [class.temporary]/5: destroyed in the reverse order of their
       construction */
    for (size_t i = destructions.size(); i-- > k.first_destruction;)
        after.append(destructions[i]);
    temporaries.resize(k.first);
    destructions.resize(k.first_destruction);
}

/* [intro.execution]/5: a full-expression where C wants an expression (a
   condition, a for statement's step): ({ temporaries; T r = e;
   destructions; r; }), a glvalue's address held instead of its value */
uint32_t c_lowering::full_value(uint32_t e, bool discarded)
{
    owned_array<uint32_t> items, after;
    uint32_t value = full_expression(e, discarded, items, after);
    if (!value || (items.empty() && after.empty()))
        return value;
    ast_qualified_type t = type_of(value);
    if (discarded || AST_QUALIFIED_TYPE_UNQUALIFIED(t) == v) {
        items.append(c_statement(c, value));
        for (uint32_t a : after)
            items.append(a);
        return statement_expression(v, block(items));
    }
    bool lvalue = ast_builder_ast(c)->nodes[value].flags & AST_NODE_FLAG_LVALUE;
    ast_qualified_type held_type = AST_QUALIFIED_TYPE_UNQUALIFIED(t);
    if (lvalue) {
        held_type = pointer_to(t);
        value = address_of(value, held_type);
    }
    uint32_t held = c_local(c, held_type);
    items.append(c_declare(c, held, value));
    for (uint32_t a : after)
        items.append(a);
    items.append(ast_builder_node(c, AST_NODE_EXPRESSION, held_type,
                                  c_variable(c, held, held_type), 0, 0, 0, 0));
    uint32_t result = statement_expression(held_type, block(items));
    if (!lvalue)
        return result;
    return dereference(result, t);
}

/* [dcl.init.ref]: the address of the object a reference is bound to; a
   prvalue's is a temporary's ([class.temporary]), the enclosing block's
   object as C's compound literal is */
uint32_t c_lowering::bind_reference(const semantic_node &n)
{
    ast_qualified_type pointer = type(n.type, n.at);
    const semantic_node &bound = node(n.a);
    if (is_glvalue(bound)) {
        uint32_t a = object(n.a);
        if (!a)
            return 0;
        return address_of(a, pointer);
    }
    if (!in_function)
        return fail("a temporary bound to a reference of static storage", n.at);
    entity_index cls = record(bound.type).entity;
    if (record(bound.type).kind == type_kind::class_type &&
        analysis.entity_table[cls].destructor) {
        /* [class.temporary]/4 and /6: a temporary with a destructor,
           destroyed by the full-expression or as the reference dies: the
           object construct() or call() made */
        uint32_t held = object(n.a);
        if (!held)
            return 0;
        return address_of(held, pointer);
    }
    uint32_t literal = compound_literal(n.a);
    if (!literal)
        return 0;
    return make(AST_NODE_ADDRESS, pointer, literal, 0, 0, 0, 0);
}

/* the prvalue n held in a temporary of the enclosing block, as C's
   compound literal is: an initializer list as an operand */
uint32_t c_lowering::compound_literal(uint32_t n)
{
    const semantic_node &held = node(n);
    if (!in_function)
        return fail("a temporary of static storage", held.at);
    uint32_t value = expression(n);
    if (!value)
        return 0;
    return compound_literal_of(value, type(held.type, held.at));
}

/* (t){value}: an object of type t initialized by an initializer node */
uint32_t c_lowering::compound_literal_of(uint32_t value, ast_qualified_type t)
{
    uint32_t temporary =
        ast_builder_symbol(c, AST_SYMBOL_VARIABLE, "", 0, t, AST_STORAGE_AUTO,
                           AST_SYMBOL_FLAG_ADDRESS_TAKEN, 1);
    ast_builder_symbol_body(c, temporary, value);
    return make(AST_NODE_COMPOUND_LITERAL, t, value, temporary, 0, 0,
                AST_NODE_FLAG_LVALUE);
}

/* [dcl.init.aggr]: an array's elements in order, the rest zero */
uint32_t c_lowering::initializer_list(const semantic_node &n)
{
    if (record(n.type).kind == type_kind::class_type) {
        /* a struct's initializer: a value per field, in order; none (0)
           for an unnamed bit-field, which is no element ([class.bit]/2) */
        owned_array<uint32_t> values;
        const uint32_t *items = analysis.semantics.items(n.first);
        entity_index ce = record(n.type).entity;
        const entity &x = analysis.entity_table[ce];
        class_type(ce, n.at);
        for (uint32_t i = 0, item = 0; item < n.count; i++) {
            if (i >= x.member_count)
                return fail("an initializer for no member", n.at);
            entity_index member = analysis.class_members[x.first_member + i];
            const entity &m = analysis.entity_table[member];
            /* the padding fields before it (class_type) */
            while (values.size() < c_fields[member])
                values.append(0);
            if (m.bit_field && !m.name) {
                values.append(0);
                continue;
            }
            if (tail_reused[member])
                return fail("an initializer of a base whose tail padding "
                            "is reused",
                            n.at);
            uint32_t e = expression(items[item++]);
            if (!e)
                return 0;
            values.append(e);
        }
        /* the members C zero-fills: a pointer to data member's null value
           is not zero bits ([abi:data-member-pointers]) */
        if (holds_data_member_pointer(n.type)) {
            for (uint32_t i = 0; i < x.member_count; i++) {
                entity_index member =
                    analysis.class_members[x.first_member + i];
                if (c_fields[member] >= values.size() &&
                    holds_data_member_pointer(
                        analysis.entity_table[member].type))
                    return fail("a pointer to data member zero-initialized "
                                "as part of an object",
                                n.at);
            }
        }
        uint32_t value_count = static_cast<uint32_t>(values.size());
        /* a union's: the value of field a (C's aux 2), the first */
        uint16_t aux = 0;
        if (analysis.entity_table[record(n.type).entity].union_)
            aux = 2;
        return make(AST_NODE_INITIALIZER, type(n.type, n.at), 0,
                    ast_builder_list(c, values.data(), value_count),
                    value_count, aux, 0);
    }
    owned_array<uint32_t> runs;
    const uint32_t *items = analysis.semantics.items(n.first);
    if (holds_data_member_pointer(n.type) &&
        (!record(n.type).bound_known || n.count < record(n.type).bound))
        return fail("a pointer to data member zero-initialized as part of "
                    "an object",
                    n.at);
    for (uint32_t i = 0; i < n.count; i++) {
        uint32_t e = expression(items[i]);
        if (!e)
            return 0;
        runs.append(i);
        runs.append(i);
        runs.append(e);
    }
    return make(AST_NODE_INITIALIZER, type(n.type, n.at), 0,
                ast_builder_list(c, runs.data(), runs.size()), n.count, 1, 0);
}

} // namespace cplusplus::lowering
