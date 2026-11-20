/* new-delete.cpp: new and delete: allocation, initialization, array cookies, deallocation (lower-internal.h) */
#include "lower-internal.h"

namespace cplusplus::lowering
{

/* ---- new and delete ------------------------------------------------ */

/* the value of the semantic node value held in a variable of type t,
   declared and assigned in items (a pointer converted to t): its symbol,
   0 on failure */
uint32_t c_lowering::held_value(uint32_t value, ast_qualified_type t,
                                owned_array<uint32_t> &items)
{
    uint32_t lowered = expression(value);
    if (!lowered)
        return 0;
    if (record(node(value).type).kind == type_kind::pointer)
        lowered = cast(lowered, t, AST_CAST_POINTER_TO_POINTER);
    uint32_t symbol = c_local(c, t);
    items.append(c_declare(c, symbol));
    uint32_t variable = c_variable(c, symbol, t);
    items.append(c_statement(c, assign(t, variable, lowered)));
    return symbol;
}

/* [expr.new]: ({ T *p = (T *)allocation; initialization of *p; p; }), the
   initialization skipped when p is null and the allocation function may
   return null ([expr.new]/20) */
uint32_t c_lowering::new_object(const semantic_node &n)
{
    if (!in_function)
        return fail("a new-expression outside a function", n.at);
    ast_qualified_type t = type(n.type, n.at);
    owned_array<uint32_t> items;
    if (n.d)
        return new_array(n, t);
    uint32_t symbol = held_value(n.a, t, items);
    if (!symbol)
        return 0;
    auto variable = [&] { return c_variable(c, symbol, t); };
    if (n.b) {
        qualified_type object = record(n.type).operand;
        allocation bound(*this, symbol, t, count_symbol);
        uint32_t done = initialize_into(
            symbol, object,
            type(qualified_type::make(object.unqualified(), cv_none), n.at),
            n.b, true);
        if (!done)
            return 0;
        uint32_t statement = c_statement(c, done);
        if (n.c)
            statement = if_statement(truth(variable()), statement, 0);
        items.append(statement);
    }
    items.append(
        ast_builder_node(c, AST_NODE_EXPRESSION, t, variable(), 0, 0, 0, 0));
    return statement_expression(t, block(items));
}

uint32_t c_lowering::element_loop(uint32_t first, ast_qualified_type object,
                                  ast_qualified_type size, uint32_t count,
                                  bool reverse, uint32_t each)
{
    ast_qualified_type pointer = pointer_to(object);
    uint32_t i = c_local(c, size);
    uint32_t element = c_local(c, pointer);
    auto index = [&] { return c_variable(c, i, size); };
    auto counted = [&] { return c_variable(c, count, size); };
    allocation bound(*this, element, pointer, count_symbol);
    uint32_t done = expression(each);
    if (!done)
        return 0;
    uint32_t at_index =
        make(AST_NODE_INDEX, object, c_variable(c, first, pointer),
             to_ptrdiff(index()), 0, 0, AST_NODE_FLAG_LVALUE);
    uint32_t body_items[2] = {
        c_statement(c, assign(pointer, c_variable(c, element, pointer),
                              address_of(at_index, pointer))),
        c_statement(c, done)};
    uint32_t items[3] = {
        c_declare(c, i), c_declare(c, element),
        counted_loop(i, size, counted(), reverse, block(body_items, 2))};
    return block(items, 3);
}

/* a variable symbol declared in items, assigned value if any */
static uint32_t declared_symbol(cc *c, owned_array<uint32_t> &items,
                                ast_qualified_type t)
{
    uint32_t s = c_local(c, t);
    items.append(c_declare(c, s));
    return s;
}

/* [expr.new]/22 and [abi:array-cookies]: ({ size_t k = count; char *raw =
   allocation; T *p = (T *)raw; if (raw) { *(size_t *)raw = k; p = (T *)(raw
   + cookie); for each element: its initialization; } p; }), the test only
   when the allocation function may return null */
uint32_t c_lowering::new_array(const semantic_node &n, ast_qualified_type t)
{
    if (target == PP_DARWIN && n.value)
        /* Apple's arm64 cookie has the element size too */
        return fail("an array cookie of Apple's arm64 ABI", n.at);
    ast_qualified_type size =
        AST_QUALIFIED_TYPE_UNQUALIFIED(type(node(n.d).type, n.at));
    ast_qualified_type bytes =
        pointer_to(ast_builder_type_basic(AST_TYPE_CHAR));
    qualified_type element = record(n.type).operand;
    ast_qualified_type object = type(element, n.at);
    owned_array<uint32_t> items;
    uint32_t count = held_value(n.d, size, items);
    if (!count)
        return 0;
    uint32_t raw = 0;
    {
        allocation bound(*this, allocated_symbol, allocated_type, count);
        raw = held_value(n.a, bytes, items);
    }
    if (!raw)
        return 0;
    auto variable = [&](uint32_t s, ast_qualified_type st) {
        return c_variable(c, s, st);
    };
    uint32_t p = declared_symbol(c, items, t);
    items.append(c_statement(
        c, assign(t, variable(p, t),
                  cast(variable(raw, bytes), t, AST_CAST_POINTER_TO_POINTER))));
    owned_array<uint32_t> body;
    if (n.value) {
        ast_qualified_type counts = pointer_to(size);
        uint32_t stored = dereference(
            cast(variable(raw, bytes), counts, AST_CAST_POINTER_TO_POINTER),
            size);
        body.append(
            c_statement(c, assign(size, stored, variable(count, size))));
        body.append(c_statement(
            c, assign(t, variable(p, t),
                      offset_pointer(variable(raw, bytes),
                                     static_cast<int64_t>(n.value), t))));
    }
    if (n.b) {
        uint32_t loop = element_loop(p, object, size, count, false, n.b);
        if (!loop)
            return 0;
        body.append(loop);
    }
    if (!body.empty()) {
        uint32_t made = block(body);
        if (n.c)
            made = if_statement(truth(variable(raw, bytes)), made, 0);
        items.append(made);
    }
    items.append(ast_builder_node(c, AST_NODE_EXPRESSION, t, variable(p, t), 0,
                                  0, 0, 0));
    return statement_expression(t, block(items));
}

/* [expr.delete]/6, /7 and [abi:array-cookies]: ({ T *p = operand; if (p) {
   char *raw = (char *)p - cookie; size_t k = *(size_t *)raw; each element,
   last first: its destruction; deallocation (raw, k * size + cookie); } }) */
uint32_t c_lowering::delete_array(const semantic_node &n)
{
    if (target == PP_DARWIN)
        return fail("an array cookie of Apple's arm64 ABI", n.at);
    ast_qualified_type t = type(n.computation, n.at);
    ast_qualified_type size = ast_builder_type_basic(AST_TYPE_UNSIGNED_LONG);
    ast_qualified_type bytes =
        pointer_to(ast_builder_type_basic(AST_TYPE_CHAR));
    qualified_type element = record(n.computation).operand;
    ast_qualified_type object =
        type(qualified_type::make(element.unqualified(), cv_none), n.at);
    owned_array<uint32_t> items;
    uint32_t p = held_value(n.a, t, items);
    if (!p)
        return 0;
    auto variable = [&](uint32_t s, ast_qualified_type st) {
        return c_variable(c, s, st);
    };
    owned_array<uint32_t> body;
    uint32_t raw = declared_symbol(c, body, bytes);
    body.append(c_statement(
        c, assign(bytes, variable(raw, bytes),
                  offset_pointer(variable(p, t), -static_cast<int64_t>(n.d),
                                 bytes))));
    uint32_t count = declared_symbol(c, body, size);
    uint32_t stored = dereference(cast(variable(raw, bytes), pointer_to(size),
                                       AST_CAST_POINTER_TO_POINTER),
                                  size);
    body.append(c_statement(c, assign(size, variable(count, size), stored)));
    if (n.b) {
        uint32_t loop = element_loop(p, object, size, count, true, n.b);
        if (!loop)
            return 0;
        body.append(loop);
    }
    allocation bound(*this, raw, bytes, count);
    uint32_t deallocated = expression(n.c);
    if (!deallocated)
        return 0;
    body.append(c_statement(c, deallocated));
    items.append(if_statement(truth(variable(p, t)), block(body), 0));
    return statement_expression(v, block(items));
}

/* [expr.new]/9: ({ size_t k = count; k > SIZE_MAX / size ? SIZE_MAX : k *
   size; }): a size no allocation function can give when the product does
   not fit, as clang passes */
uint32_t c_lowering::array_size(const semantic_node &n)
{
    if (n.value == 1 && !n.d)
        return expression(n.a);
    ast_qualified_type t = AST_QUALIFIED_TYPE_UNQUALIFIED(type(n.type, n.at));
    owned_array<uint32_t> items;
    uint32_t symbol = held_value(n.a, t, items);
    if (!symbol)
        return 0;
    auto variable = [&] { return c_variable(c, symbol, t); };
    auto constant = [&](uint64_t value) {
        return ast_builder_integer(c, t, static_cast<int64_t>(value));
    };
    /* with an array cookie of d bytes: k > (SIZE_MAX - d) / size */
    uint32_t too_large =
        make(AST_NODE_BINARY, ast_builder_type_basic(AST_TYPE_INT), variable(),
             constant((UINT64_MAX - n.d) / n.value), 0, LEX_TOKEN_GREATER, 0);
    uint32_t product = make(AST_NODE_BINARY, t, variable(), constant(n.value),
                            0, LEX_TOKEN_STAR, 0);
    if (n.d)
        product = make(AST_NODE_BINARY, t, product, constant(n.d), 0,
                       LEX_TOKEN_PLUS, 0);
    uint32_t chosen = make(AST_NODE_CONDITIONAL, t, too_large,
                           constant(UINT64_MAX), product, 0, 0);
    items.append(
        ast_builder_node(c, AST_NODE_EXPRESSION, t, chosen, 0, 0, 0, 0));
    return statement_expression(t, block(items));
}

/* [expr.delete]: ({ T *p = operand; if (p) { p->~T(); deallocation; } }) */
uint32_t c_lowering::delete_object(const semantic_node &n)
{
    if (!in_function)
        return fail("a delete-expression outside a function", n.at);
    if (n.d)
        return delete_array(n);
    ast_qualified_type t = type(n.computation, n.at);
    owned_array<uint32_t> items;
    uint32_t symbol = held_value(n.a, t, items);
    if (!symbol)
        return 0;
    uint32_t body[2];
    uint32_t count = 0;
    uint32_t destroyed = 0, deallocated = 0;
    auto variable = [&] { return c_variable(c, symbol, t); };
    if (n.value == 1) {
        /* [expr.delete]/3: a virtual destructor, the deleting destructor
           in the slot after the complete object one destroys and
           deallocates the most derived object ([abi:vtable-components]) */
        uint32_t callee = virtual_function(node(n.b).b, variable(), n.at, 1);
        if (!callee)
            return 0;
        uint32_t argument = variable();
        uint32_t call = make(AST_NODE_CALL, v, callee,
                             ast_builder_list(c, &argument, 1), 1, 0, 0);
        body[0] = c_statement(c, call);
        items.append(if_statement(truth(variable()), block(body, 1), 0));
        return statement_expression(v, block(items));
    }
    allocation bound(*this, symbol, t, count_symbol);
    if (n.b)
        destroyed = expression(n.b);
    deallocated = expression(n.c);
    if ((n.b && !destroyed) || !deallocated)
        return 0;
    if (destroyed)
        body[count++] = c_statement(c, destroyed);
    body[count++] = c_statement(c, deallocated);
    items.append(if_statement(truth(variable()), block(body, count), 0));
    return statement_expression(v, block(items));
}

} // namespace cplusplus::lowering
