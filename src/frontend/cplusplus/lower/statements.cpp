/* statements.cpp: statements: blocks, declarations, selection, iteration, jumps (lower-internal.h) */
#include "lower-internal.h"

namespace cplusplus::lowering
{

/* ---- statements ----------------------------------------------------- */

uint32_t c_lowering::block(const uint32_t *items, uint32_t count)
{
    return ast_builder_node(c, AST_NODE_BLOCK, void_type, 0,
                            ast_builder_list(c, items, count), count, 0, 0);
}

/* a block's statements; a declaration of several variables is one
   statement per variable, in the block's scope */
void c_lowering::block_items(uint32_t first, uint32_t count,
                             owned_array<uint32_t> &out)
{
    const uint32_t *items = analysis.semantics.items(first);
    for (uint32_t i = 0; i < count; i++) {
        uint32_t item = items[i];
        /* a labeled declaration-statement declares in the block
           ([stmt.label]/1, [stmt.dcl]): the label, then the declaration */
        while (node(item).kind == semantic_kind::identifier_label &&
               node(node(item).a).kind ==
                   semantic_kind::declaration_statement) {
            out.append(ast_builder_label_define(
                c, label(static_cast<uint32_t>(node(item).value)),
                c_statement(c, 0)));
            item = node(item).a;
        }
        const semantic_node &s = node(item);
        if (s.kind == semantic_kind::declarations) {
            block_items(s.first, s.count, out);
            continue;
        }
        if (s.kind == semantic_kind::declaration_statement) {
            declaration_items(s, out);
            continue;
        }
        out.append(statement(item));
    }
}

/* a variable declared in a block, and its constructor's call; one with a
   destructor is live until the block ends ([stmt.dcl]/2) */
void c_lowering::declaration_items(const semantic_node &n,
                                   owned_array<uint32_t> &out)
{
    /* the initializer is a full-expression ([intro.execution]/5.3): the
       temporaries of its arguments before the declaration, destroyed
       after it */
    collection k = collect();
    owned_array<uint32_t> items, after;
    initialized_declaration(n, items);
    collected(k, out, after);
    for (uint32_t i : items)
        out.append(i);
    for (uint32_t a : after)
        out.append(a);
}

void c_lowering::initialized_declaration(const semantic_node &n,
                                         owned_array<uint32_t> &out)
{
    entity_index outer_declaring = declaring;
    declaring = n.a;
    initialized_variable(n, out);
    declaring = outer_declaring;
}

void c_lowering::initialized_variable(const semantic_node &n,
                                      owned_array<uint32_t> &out)
{
    entity_index e = n.a;
    const entity &x = analysis.entity_table[e];
    qualified_type object;
    entity_index destructor = element_destructor(x.type, object);
    if (n.b && x.automatic && node(n.b).kind == semantic_kind::bind_reference) {
        /* [class.temporary]/6: "The temporary object to which the
           reference is bound ... persists for the lifetime of the
           reference" (through the base subobjects it names) */
        const semantic_node *root = &node(node(n.b).a);
        while (root->kind == semantic_kind::member &&
               !is_glvalue(node(root->a)))
            root = &node(root->a);
        extended_temporary = root;
    }
    if (x.c11_thread_local && destructor)
        /* (its destruction at thread exit, __cxa_thread_atexit, is not
           made) */
        return (void)fail("a _Thread_local variable destroyed at run time",
                          n.at);
    if (!x.automatic && (destructor || (n.b && !constant_initializer(n.b)))) {
        /* [stmt.dcl]/4: initialized the first time control passes
           through, under a guard */
        guarded_initialization(n, destructor, out);
        return;
    }
    if (e == named_return && named_return) {
        /* [class.copy.elision]/1.1: the variable is the result object */
        if (n.b) {
            ast_qualified_type t = type(x.type, n.at);
            uint32_t at = c_local(c, pointer_to(t));
            uint32_t made = initialize_into(at, x.type, t, n.b, true);
            if (!made)
                return;
            out.append(
                c_declare(c, at, address_of(result_node(t), pointer_to(t))));
            out.append(c_statement(c, made));
        }
    } else if (!n.b ||
               (node(n.b).kind != semantic_kind::construct &&
                !(node(n.b).kind == semantic_kind::initializer_list &&
                  piecewise_initializer(n.b)) &&
                !passed_indirectly(x.type)) ||
               !x.automatic) {
        /* a block's object, whose destructor is called below */
        out.append(declaration(n));
    } else {
        uint32_t s = symbol(e, n.at);
        uint32_t made = initialization(e, n.b, n.at);
        if (!made)
            return;
        out.append(c_declare(c, s));
        out.append(c_statement(c, made));
    }
    if (destructor && e != named_return) {
        scope_record r;
        r.kind = scope_record::object;
        r.symbol = symbol(e, n.at);
        r.object_type = x.type;
        r.owner = e;
        if (x.automatic && element_count(x.type, object) == 0)
            r.guard = guard_object(r.symbol, x.type, destructor, n.at, out);
        live.append(r);
    }
}

/* a block: its statements, then the destructor calls of the objects it
   declared ([stmt.dcl]/2); prefix (if any) first */
uint32_t c_lowering::scoped_block(const semantic_node &n, uint32_t prefix)
{
    size_t mark = live.size();
    owned_array<uint32_t> items;
    if (prefix)
        items.append(prefix);
    block_items(n.first, n.count, items);
    destroy(mark, items, n.at);
    live.resize(mark);
    return block(items);
}

/* the substatement of an iteration or switch statement, which a break (and
   for a loop, a continue) leaves */
uint32_t c_lowering::loop_body(uint32_t n, bool is_switch)
{
    size_t mark = live.size();
    scope_record r;
    r.kind = scope_record::loop;
    if (is_switch)
        r.kind = scope_record::switch_;
    live.append(r);
    uint32_t s = statement(n);
    live.resize(mark);
    return s;
}

/*
 * [stmt.while]/2, [stmt.for]/1: while (condition) statement, with the step
 * after the statement (a for statement's expression, 0 if none):
 *     for (;;) { condition's declaration; if (condition) { statement }
 *                else { destroy the declaration's objects; break; }
 *                continue_label: step; destroy them; }
 * so that the condition's variable lives through the statement and the
 * step and is destroyed before the next test; without one, C's own loop.
 * A condition's temporaries are those of its full-expression.
 */
uint32_t c_lowering::loop(uint32_t cond_node, uint32_t step_node,
                          uint32_t body_node, token_index at)
{
    bool declares = cond_node && node(cond_node).kind ==
                                     semantic_kind::condition_declaration;
    if (!declares) {
        uint32_t cond = 0, step = 0;
        if (cond_node) {
            cond = condition(cond_node, no_statements);
            if (!cond)
                return 0;
        }
        if (step_node) {
            step = full_value(step_node, true);
            if (!step)
                return 0;
        }
        uint32_t body = loop_body(body_node, false);
        if (!step_node && cond)
            return ast_builder_node(c, AST_NODE_WHILE, void_type, cond, body, 0,
                                    0, 0);
        uint32_t step_body[2] = {step, body};
        return ast_builder_node(c, AST_NODE_FOR, void_type, 0, cond,
                                ast_builder_list(c, step_body, 2), 0, 0);
    }
    size_t mark = live.size();
    scope_record r;
    r.kind = scope_record::loop;
    live.append(r);
    owned_array<uint32_t> items;
    uint32_t cond = condition(cond_node, items);
    if (!cond)
        return 0;
    if (step_node) {
        live[mark].continue_label = ast_builder_label(c, "", 0);
        live[mark].continue_mark = live.size();
    }
    uint32_t body = statement(body_node);
    owned_array<uint32_t> leave;
    destroy(mark + 1, leave, at);
    leave.append(ast_builder_node(c, AST_NODE_BREAK, void_type, 0, 0, 0, 0, 0));
    items.append(if_statement(cond, body, block(leave)));
    if (step_node) {
        uint32_t step = full_value(step_node, true);
        if (!step)
            return 0;
        items.append(ast_builder_label_define(c, live[mark].continue_label,
                                              c_statement(c, step)));
    }
    destroy(mark + 1, items, at);
    live.resize(mark);
    uint32_t step_body[2] = {0, block(items)};
    return ast_builder_node(c, AST_NODE_FOR, void_type, 0, 0,
                            ast_builder_list(c, step_body, 2), 0, 0);
}

/* whether the glvalue n designates an object or function of static
   storage duration ([basic.stc.static]) at an address known before the
   program runs: a variable's, a function, a string literal, or a part of
   one at a constant place */
bool c_lowering::static_lvalue(uint32_t n) const
{
    const semantic_node &x = node(n);
    switch (x.kind) {
    case semantic_kind::string_literal:
        return true;
    case semantic_kind::entity: {
        const entity &e = analysis.entity_table[x.a];
        if (is_reference(e.type))
            return false;
        return e.kind == entity_kind::function ||
               (e.kind == entity_kind::variable && !e.automatic &&
                !e.nonstatic_member);
    }
    case semantic_kind::member:
        return static_lvalue(x.a);
    case semantic_kind::subscript:
        return address_constant(x.a) && constant_initializer(x.b) &&
               node(x.b).kind == semantic_kind::constant;
    case semantic_kind::indirection:
        return address_constant(x.a);
    default:
        return false;
    }
}

/* [expr.const]/11: "a prvalue core constant expression of pointer type ...
   contains the address of an object with static storage duration, ... the
   address of a non-immediate function, or a null pointer value": an
   address the linker resolves */
bool c_lowering::address_constant(uint32_t n) const
{
    const semantic_node &x = node(n);
    switch (x.kind) {
    case semantic_kind::label_address:
        /* the linker resolves it ([ext:gnu_labels_as_values]) */
        return true;
    case semantic_kind::address:
        return static_lvalue(x.a);
    case semantic_kind::conversion:
        switch (static_cast<conversion_kind>(x.op)) {
        case conversion_kind::array_to_pointer:
        case conversion_kind::function_to_pointer:
            return static_lvalue(x.a);
        case conversion_kind::pointer:
        case conversion_kind::qualification_cast:
        case conversion_kind::pointer_cast:
            return address_constant(x.a);
        case conversion_kind::derived_to_base:
            /* a virtual base's offset is read from the virtual table */
            return !analysis.entity_table[x.b].virtual_base &&
                   address_constant(x.a);
        default:
            return false;
        }
    case semantic_kind::binary: {
        /* E1 + E2, E1 - E2 of a pointer and a constant integer */
        if (record(x.type).kind != type_kind::pointer)
            return false;
        const semantic_node &a = node(x.a), &b = node(x.b);
        if (record(a.type).kind == type_kind::pointer)
            return address_constant(x.a) && b.kind == semantic_kind::constant;
        return address_constant(x.b) && a.kind == semantic_kind::constant;
    }
    default:
        return false;
    }
}

/* the address of a variable of static storage duration (or a member of
   one) or of a function, none of them weak: never a null pointer, and
   evaluating it has no effect */
bool c_lowering::nonnull_address(uint32_t n) const
{
    const semantic_node &x = node(n);
    uint32_t object = 0;
    if (x.kind == semantic_kind::address)
        object = x.a;
    else if (x.kind == semantic_kind::conversion) {
        switch (static_cast<conversion_kind>(x.op)) {
        case conversion_kind::array_to_pointer:
        case conversion_kind::function_to_pointer:
            object = x.a;
            break;
        case conversion_kind::pointer:
        case conversion_kind::qualification_cast:
            return nonnull_address(x.a);
        default:
            return false;
        }
    } else {
        return false;
    }
    while (node(object).kind == semantic_kind::member)
        object = node(object).a;
    const semantic_node &o = node(object);
    if (o.kind != semantic_kind::entity)
        return false;
    const entity &e = analysis.entity_table[o.a];
    if (is_reference(e.type) || (e.gnu_flags & gnu_weak))
        return false;
    return e.kind == entity_kind::function ||
           (e.kind == entity_kind::variable && !e.automatic &&
            !e.nonstatic_member);
}

/* a null pointer constant converted to a pointer ([conv.ptr]/1) */
bool c_lowering::null_pointer(uint32_t n) const
{
    const semantic_node &x = node(n);
    return x.kind == semantic_kind::conversion &&
           static_cast<conversion_kind>(x.op) == conversion_kind::null_pointer;
}

/* whether evaluating n reads or does something only the running program
   has: a call, a constructor, an assignment, a variable's value.  An
   initializer without any that analysis could not fold (clang folds an
   out-of-range shift, say) is not lowered as dynamic initialization */
bool c_lowering::needs_run_time(uint32_t n) const
{
    if (!n)
        return false;
    const semantic_node &x = node(n);
    switch (x.kind) {
    case semantic_kind::call:
    case semantic_kind::construct:
    case semantic_kind::assignment:
    case semantic_kind::increment:
        return true;
    case semantic_kind::bind_reference:
        /* [class.temporary]/6: a temporary of static storage, made and
           its destructor registered when the program starts */
        if (!is_glvalue(node(x.a)))
            return true;
        break;
    case semantic_kind::conversion:
        if (static_cast<conversion_kind>(x.op) ==
                conversion_kind::lvalue_to_rvalue &&
            !constant_initializer(n))
            return true;
        break;
    default:
        break;
    }
    if (x.kind == semantic_kind::entity || x.kind == semantic_kind::member ||
        x.kind == semantic_kind::member_pointer)
        return false;
    if (needs_run_time(x.a) || needs_run_time(x.b) || needs_run_time(x.c) ||
        needs_run_time(x.d))
        return true;
    const uint32_t *items = analysis.semantics.items(x.first);
    for (uint32_t i = 0; i < x.count; i++)
        if (needs_run_time(items[i]))
            return true;
    return false;
}

/* whether an initializer is a constant C emits as data */
/* [expr.const]/11: arithmetic of a 128-bit integer type on constants
   whose value the analysis evaluated but does not keep in its 64 bits
   (libc++'s __pow10_128: __uint128_t(10000000000000000000) * 10): the
   initializer as written, which C folds as a constant expression too */
bool c_lowering::wide_constant_arithmetic(uint32_t n) const
{
    const semantic_node &x = node(n);
    if (x.kind == semantic_kind::constant)
        return true;
    const struct type &t = record(x.type);
    if (t.kind != type_kind::fundamental ||
        (t.fundamental != fundamental_type::int128 &&
         t.fundamental != fundamental_type::unsigned_int128) ||
        x.evaluation == constness::not_constant)
        return false;
    if (x.kind == semantic_kind::conversion)
        return static_cast<conversion_kind>(x.op) ==
                   conversion_kind::integral &&
               (node(x.a).kind == semantic_kind::constant ||
                wide_constant_arithmetic(x.a));
    if (x.kind == semantic_kind::unary)
        return wide_constant_arithmetic(x.a);
    if (x.kind == semantic_kind::binary)
        return wide_constant_arithmetic(x.a) && wide_constant_arithmetic(x.b);
    return false;
}

bool c_lowering::constant_initializer(uint32_t n) const
{
    const semantic_node &x = node(n);
    if (wide_constant_arithmetic(n))
        return true;
    switch (x.kind) {
    case semantic_kind::constant:
    case semantic_kind::zero:
    case semantic_kind::string_literal:
        return true;
    case semantic_kind::initializer_list: {
        const uint32_t *items = analysis.semantics.items(x.first);
        for (uint32_t i = 0; i < x.count; i++)
            if (!constant_initializer(items[i]))
                return false;
        return true;
    }
    case semantic_kind::member_pointer:
        return true;
    case semantic_kind::conversion: {
        if (address_constant(n))
            return true;
        entity_index member = 0;
        int64_t adjustment = 0;
        if (member_pointer_value(n, member, adjustment))
            return true;
        /* a read of a variable is code, unless read() folds it */
        return x.evaluation == constness::constant &&
               (static_cast<conversion_kind>(x.op) !=
                    conversion_kind::lvalue_to_rvalue ||
                folded_read(node(x.a)));
    }
    case semantic_kind::bind_reference:
        /* a reference bound to an object of static storage duration */
        return static_lvalue(x.a);
    default:
        return x.evaluation == constness::constant || address_constant(n);
    }
}

/* a variable declared in a block: an automatic one is a C declaration, a
   static one's symbol holds its initializer */
uint32_t c_lowering::declaration(const semantic_node &n)
{
    entity_index e = n.a;
    const entity &x = analysis.entity_table[e];
    uint32_t s = symbol(e, n.at);
    if (!s)
        return 0;
    uint32_t init = 0;
    if (!x.automatic && !n.b && holds_data_member_pointer(x.type)) {
        if (record(x.type).kind != type_kind::pointer_to_member)
            return fail("a pointer to data member zero-initialized as part "
                        "of an object",
                        n.at);
        init = null_member_pointer(x.type, n.at);
    }
    if (n.b) {
        if (!x.automatic && !constant_initializer(n.b))
            return fail("a static local with dynamic initialization", n.at);
        init = initializer(n.b);
        if (!init)
            return 0;
    }
    if (!x.automatic) {
        ast_builder_symbol_flags_add(c, s, AST_SYMBOL_FLAG_DEFINED);
        /* [impl.clang]: a static local is emitted with its function,
           referred to or not; an inline function's only where the
           function is (not yet: its function's COMDAT group) */
        if (!analysis.entity_table[enclosing_function(e)].inline_)
            ast_builder_symbol_keep(c, s);
        ast_builder_symbol_body(c, s, init);
        return c_statement(c, 0);
    }
    return c_declare(c, s, init);
}

/* [stmt.pre]/4: a condition that declares is that declaration (its
   initializer a full-expression, its variable live in the statement),
   then the variable's value as the condition */
uint32_t c_lowering::condition(uint32_t n, owned_array<uint32_t> &out)
{
    const semantic_node &x = node(n);
    if (x.kind == semantic_kind::condition_declaration) {
        declaration_items(node(x.a), out);
        n = x.b;
    }
    uint32_t value = full_value(n, false);
    if (!value)
        return 0;
    return truth(value);
}

/* [stmt.if]/3, [stmt.switch]/7, [stmt.for]/1: an init-statement before
   the statement, its variables in the statement's scope */
void c_lowering::init_statement(uint32_t n, owned_array<uint32_t> &out)
{
    if (!n)
        return;
    const semantic_node &first = node(n);
    if (first.kind == semantic_kind::declarations)
        block_items(first.first, first.count, out);
    else if (first.kind == semantic_kind::declaration_statement)
        declaration_items(first, out);
    else if (first.kind != semantic_kind::null_statement)
        out.append(statement(n));
}

/* the automatic variables in scope at each label of the function, for a
   goto's [stmt.dcl]/2: a block's declarations before the label and those
   of the statements around it (an init-statement's, a condition's) */
void c_lowering::label_scopes(uint32_t s, owned_array<uint32_t> &path)
{
    if (!s || s >= analysis.semantics.size())
        return;
    const semantic_node &n = node(s);
    if (n.kind == semantic_kind::condition_declaration) {
        label_scopes(n.a, path);
        return;
    }
    if (n.kind < semantic_kind::block || n.kind > semantic_kind::null_statement)
        return;
    switch (n.kind) {
    case semantic_kind::declarations: {
        const uint32_t *items = analysis.semantics.items(n.first);
        for (uint32_t i = 0; i < n.count; i++)
            label_scopes(items[i], path);
        return;
    }
    case semantic_kind::declaration_statement:
        path.append(n.a);
        return;
    case semantic_kind::identifier_label:
        label_paths.append(static_cast<uint32_t>(n.value));
        label_paths.append(static_cast<uint32_t>(path.size()));
        for (uint32_t e : path)
            label_paths.append(e);
        label_scopes(n.a, path);
        return;
    default:
        break;
    }
    size_t mark = path.size();
    if (n.kind == semantic_kind::block) {
        const uint32_t *items = analysis.semantics.items(n.first);
        for (uint32_t i = 0; i < n.count; i++)
            label_scopes(items[i], path);
    } else {
        /* an if or switch's init-statement is d, before its condition a */
        label_scopes(n.d, path);
        label_scopes(n.a, path);
        label_scopes(n.b, path);
        label_scopes(n.c, path);
    }
    path.resize(mark);
}

bool c_lowering::in_label_scope(uint32_t identifier, entity_index e) const
{
    for (size_t i = 0; i + 1 < label_paths.size();) {
        uint32_t count = label_paths[i + 1];
        if (label_paths[i] == identifier) {
            for (uint32_t k = 0; k < count; k++)
                if (label_paths[i + 2 + k] == e)
                    return true;
            return false;
        }
        i += 2 + count;
    }
    return false;
}

uint32_t c_lowering::label(uint32_t identifier)
{
    for (const label_record &l : labels)
        if (l.identifier == identifier)
            return l.label;
    size_t length = 0;
    const char *s = tokens.pp.identifier_spelling(identifier - 1, &length);
    label_record made;
    made.identifier = identifier;
    made.label = ast_builder_label(c, s, length);
    labels.append(made);
    return made.label;
}

/*
 * [ext:gnu_asm_statements] asm (...) as C's AST_NODE_ASM: the template,
 * the constraints and the clobbers as the strings they spell, an output
 * as the lvalue it designates, an input read (a variable usable in a
 * constant expression as its value, as `read` gives it).  The temporaries
 * of the operands are the statement's own, destroyed after it.  What the
 * C front end's asm record has no room for is refused: asm goto, and an
 * operand that is not a scalar (a class or union object, which C's code
 * generator cannot put in a register).
 */
uint32_t c_lowering::asm_statement(const semantic_node &n)
{
    if (n.value & 4)
        return fail("an asm goto statement (labels as asm operands)", n.at);
    const uint32_t *items = analysis.semantics.items(n.first);
    const uint32_t *clobbers = analysis.semantics.items(n.b);
    /* the strings, in one buffer: the template, then each operand's name
       and constraint, then the clobbers */
    owned_buffer pool;
    owned_array<size_t> ends; /* where each string ends, from 0 */
    ends.append(0);
    auto written = [&](token_index t, bool characters) {
        if (characters)
            gnu_asm_string_value(tokens, t, pool);
        else if (t)
            tokens.spelling(t, pool.get());
        ends.append(pool.size());
    };
    written(n.at, true);
    for (uint32_t i = 0; i < n.count; i++) {
        const semantic_node &o = node(items[i]);
        written(static_cast<token_index>(o.b), false);
        written(o.at, true);
        const struct type &t = record(o.type);
        if (t.kind != type_kind::fundamental && t.kind != type_kind::pointer &&
            t.kind != type_kind::enumeration)
            return fail("an asm operand that is not of a scalar type", o.at);
    }
    for (uint32_t i = 0; i < n.c; i++)
        written(clobbers[i], true);
    auto text = [&](size_t index) {
        ast_builder_asm_text t;
        t.text = pool.data() + ends[index];
        t.length = ends[index + 1] - ends[index];
        return t;
    };
    collection k = collect();
    owned_array<ast_builder_asm_operand> operands;
    for (uint32_t i = 0; i < n.count; i++) {
        const semantic_node &o = node(items[i]);
        ast_builder_asm_operand made;
        made.name = text(1 + 2 * i);
        if (!o.b)
            made.name.text = nullptr;
        made.constraint = text(2 + 2 * i);
        /* an output is the object itself, an input the value read (an
           lvalue stays one for the code generator to give in memory) */
        if (o.value)
            made.expr = object(o.a);
        else
            made.expr = read(o.a);
        if (!made.expr)
            return 0;
        operands.append(made);
    }
    owned_array<ast_builder_asm_text> clobber_text;
    for (uint32_t i = 0; i < n.c; i++)
        clobber_text.append(text(1 + 2 * n.count + i));
    uint32_t made = ast_builder_asm(c, text(0), n.value & 1, n.value & 2,
                                    n.value & 8, operands.data(), n.a, n.count,
                                    clobber_text.data(), n.c);
    owned_array<uint32_t> before, after;
    collected(k, before, after);
    if (before.empty() && after.empty())
        return made;
    before.append(made);
    for (uint32_t a : after)
        before.append(a);
    return block(before);
}

/* { long i; for (i = 0; i < count; i++) body } (or last first), body's
   array_index the variable i */
uint32_t c_lowering::array_loop(const semantic_node &n)
{
    ast_qualified_type index_type = ast_builder_type_basic(AST_TYPE_LONG);
    uint32_t i = c_local(c, index_type);
    uint32_t enclosing = array_index_symbol;
    array_index_symbol = i;
    uint32_t body = statement(n.a);
    array_index_symbol = enclosing;
    if (!body)
        return 0;
    uint32_t items[2] = {
        c_declare(c, i),
        counted_loop(
            i, index_type,
            ast_builder_integer(c, index_type, static_cast<int64_t>(n.value)),
            n.b, body)};
    return block(items, 2);
}

uint32_t c_lowering::statement(uint32_t index)
{
    if (failure.what || !index)
        return 0;
    const semantic_node &n = node(index);
    switch (n.kind) {
    case semantic_kind::block:
    case semantic_kind::declarations:
        return scoped_block(n, 0);
    case semantic_kind::expression_statement: {
        if ((n.d == semantic_analysis::complete_object_only &&
             structor_variant == 2) ||
            (n.d != semantic_analysis::complete_object_only &&
             structor_variant == 3))
            /* [class.base.init]/7: a virtual base is initialized by the
               most derived class's constructor alone (and destroyed by its
               destructor, which runs the rest as the base object one,
               variant 3) */
            return c_statement(c, 0);
        uint32_t e = full_value(n.a, true);
        if (!e)
            return 0;
        return c_statement(c, e);
    }
    case semantic_kind::null_statement:
        return c_statement(c, 0);
    case semantic_kind::array_loop:
        return array_loop(n);
    case semantic_kind::try_statement:
        return try_statement(n);
    case semantic_kind::gnu_asm_statement:
        return asm_statement(n);
    case semantic_kind::declaration_statement: {
        /* a substatement that is a declaration: [stmt.pre]/2 its own block
           scope */
        size_t mark = live.size();
        owned_array<uint32_t> items;
        declaration_items(n, items);
        destroy(mark, items, n.at);
        live.resize(mark);
        return block(items);
    }
    case semantic_kind::if_statement:
    case semantic_kind::switch_statement: {
        /* [stmt.if]/3, [stmt.switch]/7: an init-statement is the statement
           before the if or switch, in a block around both; a condition
           that declares likewise ([stmt.select]: its scope is the
           statement), its variables destroyed after it */
        size_t mark = live.size();
        owned_array<uint32_t> items;
        init_statement(n.d, items);
        uint32_t cond = condition(n.a, items);
        if (!cond)
            return 0;
        uint32_t made = 0;
        if (n.kind == semantic_kind::if_statement)
            made = if_statement(cond, statement(n.b), statement(n.c));
        else
            made = ast_builder_node(c, AST_NODE_SWITCH, void_type, cond,
                                    loop_body(n.b, true), 0, 0, 0);
        if (items.empty() && live.size() == mark)
            return made;
        items.append(made);
        destroy(mark, items, n.at);
        live.resize(mark);
        return block(items);
    }
    case semantic_kind::while_statement:
        return loop(n.a, 0, n.b, n.at);
    case semantic_kind::do_statement: {
        uint32_t body = loop_body(n.a, false);
        uint32_t cond = full_value(n.b, false);
        if (!cond)
            return 0;
        return ast_builder_node(c, AST_NODE_DO, void_type, truth(cond), body, 0,
                                0, 0);
    }
    case semantic_kind::for_statement: {
        /* [stmt.for]/1: { init-statement while (condition) { statement
           expression ; } } */
        size_t mark = live.size();
        owned_array<uint32_t> items;
        init_statement(n.a, items);
        uint32_t made = loop(n.b, n.c, n.d, n.at);
        if (items.empty() && live.size() == mark)
            return made;
        items.append(made);
        destroy(mark, items, n.at);
        live.resize(mark);
        return block(items);
    }
    case semantic_kind::case_label: {
        int64_t lo = static_cast<int64_t>(n.value);
        int64_t hi = lo;
        if (n.b)
            hi = static_cast<int64_t>(node(n.b).value);
        return ast_builder_case_range(c, lo, hi, statement(n.a));
    }
    case semantic_kind::default_label:
        return ast_builder_node(c, AST_NODE_DEFAULT, void_type, 0, 0,
                                statement(n.a), 0, 0);
    case semantic_kind::identifier_label: {
        uint32_t l = label(static_cast<uint32_t>(n.value));
        return ast_builder_label_define(c, l, statement(n.a));
    }
    case semantic_kind::goto_statement: {
        /* [stmt.dcl]/2: the objects live here and not at the label are
           destroyed, last constructed first; those the label has in scope
           are the first ones live (a jump into an object's scope past its
           declaration is ill-formed, [stmt.dcl]/3) */
        uint32_t identifier = static_cast<uint32_t>(n.value);
        uint32_t jump = ast_builder_node(c, AST_NODE_GOTO, void_type,
                                         label(identifier), 0, 0, 0, 0);
        if (!leaving_objects())
            return jump;
        if (!label_paths_made) {
            owned_array<uint32_t> path;
            label_scopes(function_root, path);
            label_paths_made = true;
        }
        size_t from = live.size();
        for (size_t i = 0; i < live.size(); i++)
            if (live[i].kind == scope_record::object &&
                !in_label_scope(identifier, live[i].owner)) {
                from = i;
                break;
            }
        if (from == live.size())
            return jump;
        owned_array<uint32_t> items;
        destroy(from, items, n.at);
        items.append(jump);
        return block(items);
    }
    case semantic_kind::indirect_goto_statement: {
        /* the target is unknown here: no object's destructor is run on the
           way out, so a computed goto in an object's scope is refused */
        if (leaving_objects())
            return fail("a computed goto in the scope of an object with a "
                        "destructor",
                        n.at);
        uint32_t address = expression(n.a);
        if (!address)
            return 0;
        return ast_builder_goto_indirect(c, address);
    }
    case semantic_kind::break_statement:
    case semantic_kind::continue_statement: {
        bool is_break = n.kind == semantic_kind::break_statement;
        int op = AST_NODE_CONTINUE;
        if (is_break)
            op = AST_NODE_BREAK;
        uint32_t jump = ast_builder_node(c, op, void_type, 0, 0, 0, 0, 0);
        size_t mark = leaving(is_break);
        if (!is_break && mark && live[mark - 1].continue_label) {
            /* to the step, the condition's variable still live */
            jump = ast_builder_node(c, AST_NODE_GOTO, void_type,
                                    live[mark - 1].continue_label, 0, 0, 0, 0);
            mark = live[mark - 1].continue_mark;
        }
        if (mark >= live.size())
            return jump;
        owned_array<uint32_t> items;
        destroy(mark, items, n.at);
        items.append(jump);
        return block(items);
    }
    case semantic_kind::return_statement: {
        uint32_t value = 0;
        owned_array<uint32_t> items, after;
        if (n.a && named_return && returned_variable(n.a) == named_return) {
            /* the variable is the result object: its bytes are the
               result, and it is not destroyed */
            value = result_node(
                type(analysis.entity_table[named_return].type, n.at));
        } else if (n.a && returned_in_place.unqualified()) {
            /* [stmt.return]/3, [abi:calls]: built in the result object */
            collection k = collect();
            ast_qualified_type t = type(returned_in_place, n.at);
            uint32_t at = c_local(c, pointer_to(t));
            uint32_t made =
                initialize_into(at, returned_in_place, t, n.a, true);
            if (!made)
                return 0;
            uint32_t stmts[3] = {
                c_declare(c, at, address_of(result_node(t), pointer_to(t))),
                c_statement(c, made),
                ast_builder_node(c, AST_NODE_EXPRESSION, t, result_node(t), 0,
                                 0, 0, 0)};
            value = statement_expression(t, block(stmts, 3));
            collected(k, items, after);
            if (!value)
                return 0;
        } else if (n.a && node(n.a).kind == semantic_kind::initializer_list) {
            /* a class's result initialized by a braced-init-list
               ([stmt.return]/2, [dcl.init.list]/3.4): its value; the
               temporaries of its full-expression ([class.temporary]/4)
               made before it and destroyed after */
            collection k = collect();
            if (piecewise_initializer(n.a)) {
                ast_qualified_type t = type(node(n.a).type, n.at);
                uint32_t temp = c_local(c, t);
                uint32_t made = initialize_into(temp, node(n.a).type, t, n.a);
                if (!made)
                    return 0;
                uint32_t stmts[3] = {c_declare(c, temp), c_statement(c, made),
                                     ast_builder_node(c, AST_NODE_EXPRESSION, t,
                                                      c_variable(c, temp, t), 0,
                                                      0, 0, 0)};
                value = statement_expression(t, block(stmts, 3));
            } else {
                value = compound_literal(n.a);
            }
            collected(k, items, after);
            if (!value)
                return 0;
        } else if (n.a) {
            value = full_expression(n.a, false, items, after);
            if (!value)
                return 0;
        }
        if (!leaving_objects() && items.empty())
            return ast_builder_node(c, AST_NODE_RETURN, void_type, value, 0, 0,
                                    0, 0);
        /* [stmt.return]/3: "The copy-initialization of the result of the
           call is sequenced before the destruction of temporaries at the
           end of the full-expression established by the operand of the
           return statement, which, in turn, is sequenced before the
           destruction of local variables ([stmt.jump]) of the block
           enclosing the return statement." */
        uint32_t result = 0;
        if (value &&
            AST_QUALIFIED_TYPE_UNQUALIFIED(type_of(value)) != void_type) {
            ast_qualified_type t = type_of(value);
            uint32_t held = c_local(c, t);
            items.append(c_declare(c, held, value));
            result = c_variable(c, held, t);
        } else if (value) {
            items.append(c_statement(c, value));
        }
        for (uint32_t a : after)
            items.append(a);
        destroy(0, items, n.at);
        items.append(ast_builder_node(c, AST_NODE_RETURN, void_type, result, 0,
                                      0, 0, 0));
        return block(items);
    }
    default:
        return fail("a statement of this kind", n.at);
    }
}

} // namespace cplusplus::lowering
