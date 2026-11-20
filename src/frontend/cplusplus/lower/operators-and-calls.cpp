/* operators-and-calls.cpp: operators, assignments, calls and conditionals (lower-internal.h) */
#include "lower-internal.h"
#include "frontend/cplusplus/builtin-registry.h"

namespace cplusplus::lowering
{

/* the C token of an operator */
int c_operator(preprocessing_op_or_punc op)
{
    switch (preprocessing_op_or_punc_primary(op)) {
    case preprocessing_op_or_punc::PLUS:
        return LEX_TOKEN_PLUS;
    case preprocessing_op_or_punc::MINUS:
        return LEX_TOKEN_MINUS;
    case preprocessing_op_or_punc::STAR:
        return LEX_TOKEN_STAR;
    case preprocessing_op_or_punc::SLASH:
        return LEX_TOKEN_SLASH;
    case preprocessing_op_or_punc::PERCENT:
        return LEX_TOKEN_PERCENT;
    case preprocessing_op_or_punc::CARET:
        return LEX_TOKEN_CARET;
    case preprocessing_op_or_punc::AMPERSAND:
        return LEX_TOKEN_AMPERSAND;
    case preprocessing_op_or_punc::BAR:
        return LEX_TOKEN_PIPE;
    case preprocessing_op_or_punc::LESS_LESS:
        return LEX_TOKEN_LESS_LESS;
    case preprocessing_op_or_punc::GREATER_GREATER:
        return LEX_TOKEN_GREATER_GREATER;
    case preprocessing_op_or_punc::EQUAL_EQUAL:
        return LEX_TOKEN_EQUAL_EQUAL;
    case preprocessing_op_or_punc::EXCLAMATION_EQUAL:
        return LEX_TOKEN_EXCLAMATION_EQUAL;
    case preprocessing_op_or_punc::LESS:
        return LEX_TOKEN_LESS;
    case preprocessing_op_or_punc::GREATER:
        return LEX_TOKEN_GREATER;
    case preprocessing_op_or_punc::LESS_EQUAL:
        return LEX_TOKEN_LESS_EQUAL;
    case preprocessing_op_or_punc::GREATER_EQUAL:
        return LEX_TOKEN_GREATER_EQUAL;
    case preprocessing_op_or_punc::TILDE:
        return LEX_TOKEN_TILDE;
    case preprocessing_op_or_punc::EXCLAMATION:
        return LEX_TOKEN_EXCLAMATION;
    /* a compound assignment's operator is the binary one */
    case preprocessing_op_or_punc::PLUS_EQUAL:
        return LEX_TOKEN_PLUS;
    case preprocessing_op_or_punc::MINUS_EQUAL:
        return LEX_TOKEN_MINUS;
    case preprocessing_op_or_punc::STAR_EQUAL:
        return LEX_TOKEN_STAR;
    case preprocessing_op_or_punc::SLASH_EQUAL:
        return LEX_TOKEN_SLASH;
    case preprocessing_op_or_punc::PERCENT_EQUAL:
        return LEX_TOKEN_PERCENT;
    case preprocessing_op_or_punc::CARET_EQUAL:
        return LEX_TOKEN_CARET;
    case preprocessing_op_or_punc::AMPERSAND_EQUAL:
        return LEX_TOKEN_AMPERSAND;
    case preprocessing_op_or_punc::BAR_EQUAL:
        return LEX_TOKEN_PIPE;
    case preprocessing_op_or_punc::LESS_LESS_EQUAL:
        return LEX_TOKEN_LESS_LESS;
    case preprocessing_op_or_punc::GREATER_GREATER_EQUAL:
        return LEX_TOKEN_GREATER_GREATER;
    default:
        return 0;
    }
}

bool is_comparison(int op)
{
    return op == LEX_TOKEN_EQUAL_EQUAL || op == LEX_TOKEN_EXCLAMATION_EQUAL ||
           op == LEX_TOKEN_LESS || op == LEX_TOKEN_GREATER ||
           op == LEX_TOKEN_LESS_EQUAL || op == LEX_TOKEN_GREATER_EQUAL;
}

/* [ext:gnu_vector_size] v[i]: the lane through a pointer to the first, as
   C's parser does (frontend/c/expression.cpp): the address of a vector
   object, the vector rvalue's bits in a temporary (AST_CAST_VECTOR_ADDRESS) */
uint32_t c_lowering::vector_lane(const semantic_node &n)
{
    ast_qualified_type vector = type(node(n.a).type, n.at);
    ast_qualified_type lane_pointer = pointer_to(type(n.type, n.at));
    uint32_t first = 0;
    if (is_glvalue(node(n.a))) {
        uint32_t object_node = object(n.a);
        if (!object_node)
            return 0;
        first = cast(address_of(object_node, pointer_to(vector)), lane_pointer,
                     AST_CAST_POINTER_TO_POINTER);
    } else {
        uint32_t value = expression(n.a);
        if (!value)
            return 0;
        first = cast(value, lane_pointer, AST_CAST_VECTOR_ADDRESS);
    }
    uint32_t index = expression(n.b);
    if (!first || !index)
        return 0;
    uint8_t flags = 0;
    if (is_glvalue(node(n.a)))
        flags = AST_NODE_FLAG_LVALUE;
    return make(AST_NODE_INDEX, type(n.type, n.at), first, to_ptrdiff(index), 0,
                0, flags);
}

uint32_t c_lowering::binary(const semantic_node &n)
{
    int op = c_operator(static_cast<preprocessing_op_or_punc>(n.op));
    if (!op)
        return fail("a binary operator", n.at);
    /* [conv.ptr]/1: a null pointer value is distinct from every pointer to
       an object or function: the address of a variable or function that is
       not weak compared with one is known, as clang's code generator folds
       it (the variable is then not referenced) */
    if ((op == LEX_TOKEN_EQUAL_EQUAL || op == LEX_TOKEN_EXCLAMATION_EQUAL) &&
        ((nonnull_address(n.a) && null_pointer(n.b)) ||
         (null_pointer(n.a) && nonnull_address(n.b))))
        return to_bool(ast_builder_integer(
            c, ast_builder_type_basic(AST_TYPE_INT),
            static_cast<int64_t>(op == LEX_TOKEN_EXCLAMATION_EQUAL)));
    if (record(node(n.a).type).kind == type_kind::pointer_to_member)
        return member_pointer_equality(n, op);
    uint32_t a = expression(n.a), b = expression(n.b);
    if (!a || !b)
        return 0;
    if (is_comparison(op) && record(n.type).kind == type_kind::vector)
        /* [ext:gnu_vector_size] lane by lane: a signed integer vector */
        return make(AST_NODE_BINARY, type(n.type, n.at), a, b, 0,
                    static_cast<uint16_t>(op), 0);
    if (is_comparison(op))
        return to_bool(make(AST_NODE_BINARY,
                            ast_builder_type_basic(AST_TYPE_INT), a, b, 0,
                            static_cast<uint16_t>(op), 0));
    ast_qualified_type t = AST_QUALIFIED_TYPE_UNQUALIFIED(type(n.type, n.at));
    /* [expr.add]/4: a pointer and an integer; C has the pointer first and
       the integer as a ptrdiff_t */
    bool a_pointer = record(node(n.a).type).kind == type_kind::pointer;
    bool b_pointer = record(node(n.b).type).kind == type_kind::pointer;
    if (a_pointer && !b_pointer)
        b = to_ptrdiff(b);
    else if (b_pointer && !a_pointer) {
        uint32_t integer = a;
        a = b;
        b = to_ptrdiff(integer);
    }
    return make(AST_NODE_BINARY, t, a, b, 0, static_cast<uint16_t>(op), 0);
}

/*
 * A trivial assignment of a class whose tail padding a derived class may
 * reuse ([abi:pod]): the left operand may be a base class subobject, so
 * only the class's data size is copied, as clang copies it:
 *     *(p = &a, *(struct { char d[dsize]; } *)p = *(... *)&b, p)
 * with p a temporary.
 */
uint32_t c_lowering::data_size_assignment(const semantic_node &n, uint32_t a,
                                          uint32_t b)
{
    if (!collecting)
        return fail("an assignment of a class with tail padding outside a "
                    "statement",
                    n.at);
    const entity &x = analysis.entity_table[record(n.type).entity];
    ast_qualified_type t = AST_QUALIFIED_TYPE_UNQUALIFIED(type(n.type, n.at));
    ast_qualified_type pointer = pointer_to(t);
    if (!(ast_builder_ast(c)->nodes[b].flags & AST_NODE_FLAG_LVALUE))
        return fail("an assignment of a class with tail padding from a "
                    "prvalue",
                    n.at);
    ast_qualified_type byte = ast_builder_type_basic(AST_TYPE_CHAR);
    ast_qualified_type view = ast_builder_type_record(c, "", 0, false);
    ast_qualified_type data =
        ast_builder_type_array(c, byte, x.data_size, true);
    const char *field_name = "d";
    size_t field_length = 1;
    uint32_t no_alignment = 0;
    uint8_t not_packed = 0;
    int32_t no_width = -1;
    ast_builder_type_record_complete(c, view, &data, &field_name, &field_length,
                                     &no_alignment, &not_packed, &no_width, 1);
    ast_qualified_type view_pointer = pointer_to(view);
    uint32_t temporary = c_local(c, pointer);
    temporaries.append(c_declare(c, temporary));
    auto p = [&] { return c_variable(c, temporary, pointer); };
    auto as_view = [&](uint32_t address) {
        return dereference(
            cast(address, view_pointer, AST_CAST_POINTER_TO_POINTER), view);
    };
    uint32_t stored = assign(pointer, p(), address_of(a, pointer));
    uint32_t copied =
        assign(view, as_view(p()), as_view(address_of(b, pointer)));
    uint32_t sequence = comma(
        pointer, stored, make(AST_NODE_COMMA, pointer, copied, p(), 0, 0, 0));
    return dereference(sequence, t);
}

uint32_t c_lowering::assignment(const semantic_node &n)
{
    uint32_t a = object(n.a);
    uint32_t b = 0;
    if (node(n.b).kind == semantic_kind::initializer_list)
        /* [dcl.init.list]/3.4: a member's braced default member
           initializer ([class.base.init]/9.1): its object, copied in */
        b = compound_literal(n.b);
    else
        b = expression(n.b);
    if (!a || !b)
        return 0;
    ast_qualified_type t = AST_QUALIFIED_TYPE_UNQUALIFIED(type(n.type, n.at));
    if (preprocessing_op_or_punc_primary(static_cast<preprocessing_op_or_punc>(
            n.op)) == preprocessing_op_or_punc::EQUAL) {
        const struct type &r = record(n.type);
        if (r.kind == type_kind::class_type &&
            analysis.entity_table[r.entity].data_size <
                analysis.entity_table[r.entity].size)
            return data_size_assignment(n, a, b);
        return assign(t, a, b);
    }
    int op = c_operator(static_cast<preprocessing_op_or_punc>(n.op));
    if (!op)
        return fail("an assignment operator", n.at);
    ast_qualified_type computation = 0;
    if (record(node(n.a).type).kind == type_kind::pointer) {
        computation = t;
        b = to_ptrdiff(b);
    } else
        computation = AST_QUALIFIED_TYPE_UNQUALIFIED(type(n.computation, n.at));
    return make(AST_NODE_ASSIGN, t, a, b, computation,
                static_cast<uint16_t>(op), 0);
}

/* [expr.call]: a function called through a pointer; one that returns a
   reference returns its address */
/*
 * [ext:gnu_library_builtins] a call of a builtin of this front end that is
 * not the C front end's builtin node of its name (the registry's lowering):
 * the argument itself (__builtin_launder), the arguments evaluated and no
 * value (__builtin_assume_dereferenceable), a trap (__builtin_verbose_trap:
 * its message strings are no run-time operands), or the byte swap of the
 * argument's width (__builtin_bswapg).
 */
uint32_t c_lowering::special_builtin(builtin_lowering lowering,
                                     ast_qualified_type t,
                                     const owned_array<uint32_t> &arguments)
{
    uint32_t count = static_cast<uint32_t>(arguments.size());
    switch (lowering) {
    case builtin_lowering::identity:
        return arguments[0];
    case builtin_lowering::discard: {
        uint32_t all = 0;
        for (uint32_t i = 0; i < count; i++) {
            uint32_t one = cast(arguments[i], v, AST_CAST_VOID);
            if (all)
                all = comma(v, all, one);
            else
                all = one;
        }
        if (!all)
            all = cast(
                ast_builder_integer(c, ast_builder_type_basic(AST_TYPE_INT), 0),
                v, AST_CAST_VOID);
        return all;
    }
    case builtin_lowering::trap:
        return ast_builder_builtin_call(c, "__builtin_trap", 14, t, nullptr, 0);
    case builtin_lowering::bswap: {
        /* the argument as the unsigned integer of its width, swapped, and
           the result as the argument's type */
        uint64_t bytes = ast_builder_type_size(c, type_of(arguments[0]));
        if (bytes == 1)
            return arguments[0];
        const char *name = "__builtin_bswap16";
        ast_qualified_type u = ast_builder_type_basic(AST_TYPE_UNSIGNED_SHORT);
        if (bytes == 4) {
            name = "__builtin_bswap32";
            u = ast_builder_type_basic(AST_TYPE_UNSIGNED_INT);
        } else if (bytes == 8) {
            name = "__builtin_bswap64";
            u = ast_builder_type_basic(AST_TYPE_UNSIGNED_LONG_LONG);
        }
        uint32_t x = cast(arguments[0], u, AST_CAST_INTEGER);
        uint32_t swapped =
            ast_builder_builtin_call(c, name, strlen(name), u, &x, 1);
        return cast(swapped, t, AST_CAST_INTEGER);
    }
    case builtin_lowering::node:
        break;
    }
    return fail("a builtin of this front end", 0);
}

uint32_t c_lowering::call(const semantic_node &n)
{
    qualified_type function_type = node(n.a).type;
    uint32_t callee = 0;
    uint32_t builtin = 0;
    bool untyped_builtin = false;
    builtin_lowering lowering = builtin_lowering::node;
    owned_array<uint32_t> arguments;
    /* a call through a pointer to member function: its statements
       first ([expr.mptr.oper]/6) */
    owned_array<uint32_t> through;
    uint32_t before_call = 0;
    bool member_pointer_call =
        node(n.a).kind == semantic_kind::member_pointer_access;
    if (member_pointer_call) {
        if (passed_indirectly(record(function_type).operand))
            return fail("a call through a pointer to member function "
                        "returning a class by address",
                        n.at);
        uint32_t object = 0;
        callee = member_function_call(n, through, object);
        if (!callee)
            return 0;
        arguments.append(object);
    } else if (n.c) {
        /* [class.virtual]/16: the final overrider, the function pointer in
           the object's virtual table at the function's slot
           ([abi:vtable-components]); the object's address is read twice,
           so it is lowered twice when that has no effect, else held */
        reread r;
        uint32_t first = read_first(n.b, r,
                                    "a virtual call on an object whose "
                                    "address has side effects outside a "
                                    "statement");
        if (!first)
            return 0;
        /* held: stored before the call, read for the callee and the
           argument */
        if (r.symbol)
            before_call = first;
        uint32_t object = read_again(r);
        uint32_t again = read_again(r);
        callee = virtual_function(node(n.a).a, again, n.at);
        if (!callee)
            return 0;
        arguments.append(object);
    } else {
        /* [ext:gnu_library_builtins] __builtin_is_constant_evaluated()
           evaluated at run time: false ([meta.const.eval]/1) */
        uint32_t named = n.a;
        if (node(named).kind == semantic_kind::conversion &&
            static_cast<conversion_kind>(node(named).op) ==
                conversion_kind::function_to_pointer)
            named = node(named).a;
        if (node(named).kind == semantic_kind::entity &&
            (analysis.entity_table[node(named).a].gnu_flags &
             gnu_is_constant_evaluated))
            return ast_builder_integer(
                c,
                AST_QUALIFIED_TYPE_UNQUALIFIED(
                    type(record(function_type).operand, n.at)),
                0);
        if (node(named).kind == semantic_kind::entity &&
            (analysis.entity_table[node(named).a].gnu_flags &
             gnu_compiler_builtin))
        /* [ext:gnu_library_builtins] no function: the C front end's
               builtin node, made below */
        {
            builtin = analysis.entity_table[node(named).a].name;
            uint16_t row = analysis.entity_table[node(named).a].gnu_builtin_row;
            untyped_builtin = row && gnu_builtin_untyped(row);
            if (row)
                lowering = gnu_builtin_at(row).lower;
        } else {
            /* [expr.call]/8: "The postfix-expression is sequenced before
               each expression in the expression-list": the object
               expression of a static member's call (a comma's left operand,
               [expr.ref]/1) is evaluated before the arguments, which C
               leaves unsequenced with the function designator */
            uint32_t function = n.a;
            if (node(named).kind == semantic_kind::comma) {
                before_call = discard(node(named).a);
                if (!before_call)
                    return 0;
                function = node(named).b;
                function_type = node(function).type;
            }
            callee = expression(function);
            if (!callee)
                return 0;
            if (record(function_type).kind == type_kind::pointer)
                function_type = record(function_type).operand;
            else
                callee = cast(callee, pointer_to(type_of(callee)),
                              AST_CAST_FUNCTION_DECAY);
        }
        if (n.b) {
            /* the implicit object argument: the object's address, the
               function's first parameter */
            uint32_t object = expression(n.b);
            if (!object)
                return 0;
            arguments.append(object);
        }
    }
    const uint32_t *items = analysis.semantics.items(n.first);
    const struct type &ft = record(function_type);
    const qualified_type *parameters =
        analysis.type_table.parameters(function_type.unqualified());
    for (uint32_t i = 0; i < n.count; i++) {
        uint32_t a = 0;
        if (i < ft.parameter_count && passed_indirectly(parameters[i]))
            a = indirect_argument(items[i], parameters[i], n.at);
        else if (node(items[i]).kind == semantic_kind::initializer_list)
            /* a class's object initialized by a braced-init-list
               ([dcl.init.list]/3.4): its value */
            a = compound_literal(items[i]);
        else
            a = expression(items[i]);
        if (!a)
            return 0;
        arguments.append(a);
    }
    qualified_type result = record(function_type).operand;
    /* an atomic builtin's type is by its arguments: the call's */
    if (untyped_builtin)
        result = n.type;
    ast_qualified_type t = AST_QUALIFIED_TYPE_UNQUALIFIED(type(result, n.at));
    uint32_t count = static_cast<uint32_t>(arguments.size());
    uint32_t made = 0;
    if (builtin && lowering != builtin_lowering::node) {
        made = special_builtin(lowering, t, arguments);
        if (!made)
            return 0;
    } else if (builtin) {
        size_t length = 0;
        const char *spelling =
            tokens.pp.identifier_spelling(builtin - 1, &length);
        made = ast_builder_builtin_call(c, spelling, length, t,
                                        arguments.data(), count);
    } else {
        made = make(AST_NODE_CALL, t, callee,
                    ast_builder_list(c, arguments.data(), count), count, 0, 0);
    }
    if (member_pointer_call) {
        through.append(
            ast_builder_node(c, AST_NODE_EXPRESSION, t, made, 0, 0, 0, 0));
        made = statement_expression(t, block(through));
    }
    if (before_call)
        made = comma(t, before_call, made);
    if (!is_reference(result))
        return made;
    return dereference(made, type(n.type, n.at));
}

/* [expr.cond] */
uint32_t c_lowering::conditional(const semantic_node &n)
{
    ast_qualified_type t = type(n.type, n.at);
    if (!is_glvalue(n))
        return make(AST_NODE_CONDITIONAL, AST_QUALIFIED_TYPE_UNQUALIFIED(t),
                    truth(expression(n.a)), expression(n.b), expression(n.c), 0,
                    0);
    /* the object one of the operands is: its address selected */
    ast_qualified_type pointer = pointer_to(t);
    uint32_t b = object(n.b), c_ = object(n.c);
    if (!b || !c_)
        return 0;
    uint32_t selected =
        make(AST_NODE_CONDITIONAL, pointer, truth(expression(n.a)),
             address_of(b, pointer), address_of(c_, pointer), 0, 0);
    return dereference(selected, t);
}

} // namespace cplusplus::lowering
