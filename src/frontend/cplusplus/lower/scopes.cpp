/* scopes.cpp: scopes: the destruction of objects, element loops, constructed flags, temporaries constructed (lower-internal.h) */
#include "lower-internal.h"

namespace cplusplus::lowering
{

/* [stmt.dcl]/2: "Upon each transfer of control (including sequential
   execution of statements) within a function from a point P to a point Q,
   each variable with automatic storage duration that is active at P and
   not at Q is destroyed by the evaluation of its destructor (if any) ...
   Variables with automatic storage duration that are destroyed in this way
   are destroyed in the reverse order of their construction." */
void c_lowering::destroy(size_t mark, owned_array<uint32_t> &out,
                         token_index at)
{
    for (size_t i = live.size(); i-- > mark;) {
        const scope_record &r = live[i];
        if (r.kind == scope_record::subobjects) {
            uint32_t destructions = statement(r.symbol);
            if (destructions)
                out.append(destructions);
            continue;
        }
        if (r.kind != scope_record::object)
            continue;
        uint32_t destroyed = destruction(r.symbol, r.object_type, at);
        if (destroyed)
            out.append(if_constructed(destroyed, r.flag));
    }
}

/* [class.dtor]/13: the destruction of the object in symbol s, of a class
   or array of classes type; "the elements of an array are destroyed in
   reverse order of their construction" */
uint32_t c_lowering::destruction(uint32_t s, qualified_type object_type,
                                 token_index at)
{
    qualified_type element;
    entity_index destructor = element_destructor(object_type, element);
    ast_qualified_type t = type(object_type, at);
    if (uint64_t count = element_count(object_type, element))
        return each_element(s, t, type(element, at), count, true, destructor,
                            at);
    uint32_t call =
        special_call(destructor, address_of(c_variable(c, s, t), pointer_to(t)),
                     nullptr, 0, at);
    if (!call)
        return 0;
    return c_statement(c, call);
}

uint32_t c_lowering::each_element(uint32_t s, ast_qualified_type array,
                                  ast_qualified_type element, uint64_t bound,
                                  bool reverse, entity_index e, token_index at,
                                  const semantic_node *construct)
{
    ast_qualified_type index_type = ast_builder_type_basic(AST_TYPE_LONG);
    uint32_t i = c_local(c, index_type);
    /* the elements of an array of arrays too, as one sequence
       ([dcl.array]/6: contiguous) */
    ast_qualified_type pointer = pointer_to(element);
    uint32_t first =
        cast(address_of(c_variable(c, s, array), pointer_to(array)), pointer,
             AST_CAST_POINTER_TO_POINTER);
    uint32_t object =
        make(AST_NODE_INDEX, element, first, c_variable(c, i, index_type), 0, 0,
             AST_NODE_FLAG_LVALUE);
    owned_array<uint32_t> arguments;
    uint32_t argument_count = 0;
    if (construct) {
        const uint32_t *given = analysis.semantics.items(construct->first);
        argument_count = construct->count;
        for (uint32_t k = 0; k < argument_count; k++) {
            uint32_t a = expression(given[k]);
            if (!a)
                return 0;
            arguments.append(a);
        }
    }
    uint32_t call = special_call(e, address_of(object, pointer),
                                 arguments.data(), argument_count, at);
    if (!call)
        return 0;
    uint32_t items[2] = {
        c_declare(c, i),
        counted_loop(
            i, index_type,
            ast_builder_integer(c, index_type, static_cast<int64_t>(bound)),
            reverse, c_statement(c, call))};
    return block(items, 2);
}

/* for (i = 0; i < count; i++) body, or reverse for (i = count; i-- > 0;)
   body; count a constant or a variable */
uint32_t c_lowering::counted_loop(uint32_t i, ast_qualified_type t,
                                  uint32_t count, bool reverse, uint32_t body)
{
    ast_qualified_type truth = ast_builder_type_basic(AST_TYPE_INT);
    auto index = [&] { return c_variable(c, i, t); };
    uint32_t zero = ast_builder_integer(c, t, 0);
    uint32_t init, cond, step = 0;
    if (reverse) {
        init = assign(t, index(), count);
        cond = make(AST_NODE_BINARY, truth,
                    make(AST_NODE_POST_DECREMENT, t, index(), 0, 0, 0, 0), zero,
                    0, LEX_TOKEN_GREATER, 0);
    } else {
        init = assign(t, index(), zero);
        cond =
            make(AST_NODE_BINARY, truth, index(), count, 0, LEX_TOKEN_LESS, 0);
        step = make(AST_NODE_POST_INCREMENT, t, index(), 0, 0, 0, 0);
    }
    uint32_t step_body[2] = {step, body};
    return ast_builder_node(c, AST_NODE_FOR, v, c_statement(c, init), cond,
                            ast_builder_list(c, step_body, 2), 0, 0);
}

bool c_lowering::leaving_objects() const
{
    for (const scope_record &r : live)
        if (r.kind == scope_record::object ||
            r.kind == scope_record::subobjects)
            return true;
    return false;
}

size_t c_lowering::leaving(bool is_break) const
{
    for (size_t i = live.size(); i-- > 0;)
        if (live[i].kind == scope_record::loop ||
            (is_break && live[i].kind == scope_record::switch_))
            return i + 1;
    return 0;
}

uint32_t c_lowering::constructed_flag()
{
    if (!conditional_depth)
        return 0;
    ast_qualified_type b = ast_builder_type_basic(AST_TYPE_BOOL);
    uint32_t flag = c_local(c, b);
    temporaries.append(c_declare(c, flag, ast_builder_integer(c, b, 0)));
    return flag;
}

uint32_t c_lowering::mark_constructed(uint32_t made, uint32_t flag)
{
    if (!flag || !made)
        return made;
    ast_qualified_type b = ast_builder_type_basic(AST_TYPE_BOOL);
    uint32_t set =
        assign(b, c_variable(c, flag, b), ast_builder_integer(c, b, 1));
    return comma(b, made, set);
}

uint32_t c_lowering::if_constructed(uint32_t statement, uint32_t flag)
{
    if (!flag)
        return statement;
    ast_qualified_type b = ast_builder_type_basic(AST_TYPE_BOOL);
    return if_statement(c_variable(c, flag, b), statement, 0);
}

/* [class.ctor]: a constructor call whose object is a temporary
   ([class.temporary]/1: "Temporary objects are created ... when a prvalue
   is converted to an xvalue", and a prvalue initializing an object is that
   object's initialization, [dcl.init]/17.6.1): GNU C's ({ T t; C(&t, ...);
   t; }) */
uint32_t c_lowering::construct(const semantic_node &n)
{
    if (n.a) {
        /* [class.base.init]: a base or member of *this, constructed in
           place */
        uint32_t object = expression(n.a);
        if (!object)
            return 0;
        uint32_t vtt = 0;
        if (entity_index b = base_subobject_of(n.a))
            vtt = vtt_argument(b, n.at);
        if (failure.what)
            return 0;
        return construct_at(n, object, vtt, n.at);
    }
    if (!in_function)
        return fail("dynamic initialization of a variable", n.at);
    ast_qualified_type t = type(n.type, n.at);
    uint32_t temporary =
        ast_builder_symbol(c, AST_SYMBOL_VARIABLE, "", 0, t, AST_STORAGE_AUTO,
                           AST_SYMBOL_FLAG_ADDRESS_TAKEN, 1);
    uint32_t object = c_variable(c, temporary, t);
    uint32_t call = construct_at(n, object, 0, n.at);
    if (!call)
        return 0;
    uint32_t statements[3] = {c_declare(c, temporary), c_statement(c, call),
                              ast_builder_node(c, AST_NODE_EXPRESSION, t,
                                               c_variable(c, temporary, t), 0,
                                               0, 0, 0)};
    return statement_expression(t, block(statements, 3));
}

} // namespace cplusplus::lowering
