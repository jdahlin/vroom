/* exceptions.cpp: throw-expressions, try-blocks and handlers, the destruction of objects when an exception unwinds (lower-internal.h; doc/PLAN-vir-eh.md E8) */
#include "lower-internal.h"

namespace cplusplus::lowering
{

/*
 * [except.throw]/3, [abi:eh-2.4.3]: ({ T *p = (T *)__cxa_allocate_exception(sizeof (T)); the initialization
 * of *p; __cxa_throw(p, &_ZTI<T>, the destructor or null); }).  /4: without an operand,
 * __cxa_rethrow().
 */
uint32_t c_lowering::throw_expression(const semantic_node &n)
{
    if (!in_function || !ast_builder_exceptions_enabled(c))
        return fail("a throw-expression without exceptions", n.at);
    ast_qualified_type vp = pointer_to(v);
    if (!n.a)
        return runtime_call(cxa_rethrow, "__cxa_rethrow", v, nullptr, nullptr,
                            0);
    qualified_type object = n.computation;
    ast_qualified_type t = type(object, n.at), pointer = pointer_to(t);
    ast_qualified_type size_type = ast_builder_type_basic(AST_TYPE_LONG);
    uint32_t info = typeinfo_address(object.unqualified(), n.at);
    if (!info)
        return 0;
    owned_array<uint32_t> items;
    uint32_t bytes = ast_builder_integer(
        c, size_type, static_cast<int64_t>(analysis.size_of_type(object)));
    uint32_t memory =
        runtime_call(cxa_allocate_exception, "__cxa_allocate_exception", vp,
                     &size_type, &bytes, 1);
    if (!memory)
        return 0;
    uint32_t p = c_local(c, pointer);
    items.append(c_declare(c, p));
    items.append(c_statement(
        c, assign(pointer, c_variable(c, p, pointer),
                  cast(memory, pointer, AST_CAST_POINTER_TO_POINTER))));
    {
        allocation bound(*this, p, pointer, count_symbol);
        uint32_t done = initialize_into(p, object, t, n.a, true);
        if (!done)
            return 0;
        items.append(c_statement(c, done));
    }
    ast_qualified_type dt = ast_builder_type_function(c, v, &vp, 1, false);
    qualified_type element;
    entity_index destructor = element_destructor(object, element);
    uint32_t function = cast(ast_builder_integer(c, size_type, 0),
                             pointer_to(dt), AST_CAST_INTEGER_TO_POINTER);
    if (destructor) {
        uint32_t d = special_callee(destructor, n.at);
        if (!d)
            return 0;
        ast_builder_symbol_flags_add(c, d, AST_SYMBOL_FLAG_ADDRESS_TAKEN);
        ast_qualified_type f = entity_type(destructor, n.at);
        function = cast(make(AST_NODE_VARIABLE, f, d, 0, 0, 0, 0),
                        pointer_to(f), AST_CAST_FUNCTION_DECAY);
    }
    ast_qualified_type parameters[3] = {vp, vp, pointer_to(dt)};
    uint32_t arguments[3] = {
        cast(c_variable(c, p, pointer), vp, AST_CAST_POINTER_TO_POINTER), info,
        function};
    uint32_t thrown =
        runtime_call(cxa_throw, "__cxa_throw", v, parameters, arguments, 3);
    if (!thrown)
        return 0;
    items.append(c_statement(c, thrown));
    return statement_expression(v, block(items));
}

/* [except.handle]/10: the exception object a handler's variable is initialized from: *(T *)p, p what
   __cxa_begin_catch returned; a pointer is that value itself ([abi:eh-2.5.1]), so *(T *)&p */
uint32_t c_lowering::exception_object(const semantic_node &n)
{
    if (!exception_symbol)
        return fail("an exception object outside a handler", n.at);
    ast_qualified_type vp = pointer_to(v), t = type(n.type, n.at);
    uint32_t held = c_variable(c, exception_symbol, vp);
    uint32_t pointer = held;
    if (record(n.type).kind == type_kind::pointer)
        pointer = address_of(held, pointer_to(vp));
    return dereference(
        cast(pointer, pointer_to(t), AST_CAST_POINTER_TO_POINTER), t);
}

/*
 * [except.pre], [except.handle]: the try-block's statement and handlers as ast_builder_try builds them.  A
 * handler runs ex = __cxa_begin_catch(exception) first; a cleanup variable calls __cxa_end_catch on every
 * way out of it, also when an exception unwinds out of it ([except.handle]/7: "the exception is destroyed
 * when the handler exits").  The handler's variable is made as any block variable is, from the exception
 * object; "catch (...)" has none.
 */
uint32_t c_lowering::try_statement(const semantic_node &n)
{
    if (!in_function || !ast_builder_exceptions_enabled(c))
        return fail("a try-block without exceptions", n.at);
    uint32_t body = statement(n.a);
    if (!body)
        return 0;
    ast_qualified_type vp = pointer_to(v);
    ast_qualified_type byte = ast_builder_type_basic(AST_TYPE_CHAR);
    uint32_t exception = c_local(c, vp);
    owned_array<ast_builder_catch> handlers;
    const uint32_t *items = analysis.semantics.items(n.first);
    for (uint32_t i = 0; i < n.count; i++) {
        const semantic_node &h = node(items[i]);
        ast_builder_catch k = {};
        if (h.computation != qualified_type()) {
            k.type = typeinfo(h.computation.unqualified(), h.at);
            if (!k.type)
                return 0;
        }
        owned_array<uint32_t> out;
        uint32_t argument = c_variable(c, exception, vp);
        uint32_t begun = runtime_call(cxa_begin_catch, "__cxa_begin_catch", vp,
                                      &vp, &argument, 1);
        if (!begun)
            return 0;
        uint32_t held = c_local(c, vp);
        out.append(c_declare(c, held, begun));
        uint32_t end = c_local(c, byte);
        ast_builder_symbol_cleanup(c, end, ast_builder_end_catch_function(c));
        out.append(c_declare(c, end));
        uint32_t outer = exception_symbol;
        exception_symbol = held;
        size_t mark = live.size();
        if (h.a)
            declaration_items(node(h.a), out);
        uint32_t inner = statement(h.c);
        destroy(mark, out, h.at);
        live.resize(mark);
        exception_symbol = outer;
        if (!inner)
            return 0;
        out.append(inner);
        k.body = block(out);
        handlers.append(k);
    }
    return ast_builder_try(c, body, exception, handlers.data(),
                           static_cast<uint32_t>(handlers.size()));
}

/* the function that destroys what a guard points to: void f(void **g) { if (*g) ~T(*g); } */
uint32_t c_lowering::unwind_function(entity_index destructor,
                                     qualified_type object, token_index at)
{
    for (size_t i = 0; i < unwind_destructors.size(); i++)
        if (unwind_destructors[i] == destructor)
            return unwind_functions[i];
    ast_qualified_type vp = pointer_to(v), vpp = pointer_to(vp);
    ast_qualified_type t = type(object, at), pointer = pointer_to(t);
    ast_qualified_type ft = ast_builder_type_function(c, v, &vpp, 1, false);
    owned_buffer name;
    vbuffer_string_append(name.get(), ".Lcxx_unwind");
    if (!unwind_functions.empty())
        vbuffer_format_append(name.get(), ".%u",
                              static_cast<unsigned>(unwind_functions.size()));
    uint32_t f = ast_builder_symbol(c, AST_SYMBOL_FUNCTION, name.data(),
                                    name.size(), ft, AST_STORAGE_STATIC, 0, 0);
    ast_builder_toplevel(c, f);
    uint32_t g = c_local(c, vpp);
    ast_builder_symbol_flags_add(c, g, AST_SYMBOL_FLAG_PARAMETER);
    uint32_t held = dereference(c_variable(c, g, vpp), vp);
    uint32_t call = special_call(
        destructor, cast(held, pointer, AST_CAST_POINTER_TO_POINTER), nullptr,
        0, at);
    if (!call)
        return 0;
    uint32_t guarded = if_statement(
        truth(dereference(c_variable(c, g, vpp), vp)), c_statement(c, call), 0);
    uint32_t body = block(&guarded, 1);
    struct node &b = ast_builder_ast(c)->nodes[body];
    b.a = ast_builder_list(c, &g, 1);
    b.aux = 1;
    ast_builder_symbol_body(c, f, body);
    ast_builder_symbol_flags_add(c, f, AST_SYMBOL_FLAG_DEFINED);
    unwind_destructors.append(destructor);
    unwind_functions.append(f);
    return f;
}

/* [except.ctor]/1: "As control passes from the point where an exception is thrown to a handler, destructors
   are invoked for all automatic objects constructed since the try block was entered": the guard of the
   object in symbol s, declared (with the address as its value) in out after the object's construction */
uint32_t c_lowering::guard_object(uint32_t s, qualified_type object,
                                  entity_index destructor, token_index at,
                                  owned_array<uint32_t> &out)
{
    if (!ast_builder_exceptions_enabled(c) || !in_function)
        return 0;
    uint32_t f = unwind_function(destructor, object, at);
    if (!f)
        return 0;
    ast_qualified_type vp = pointer_to(v), t = type(object, at);
    uint32_t address = cast(address_of(c_variable(c, s, t), pointer_to(t)), vp,
                            AST_CAST_POINTER_TO_POINTER);
    uint32_t guard = c_local(c, vp);
    ast_builder_symbol_cleanup(c, guard, f);
    out.append(c_declare(c, guard, address));
    return guard;
}

} // namespace cplusplus::lowering
