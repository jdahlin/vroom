/* operators-and-calls.cpp: operators, assignments, calls and conditionals (lower-internal.h) */
#include "lower-internal.h"
#include "frontend/cplusplus/builtin-registry.h"
#include <math.h>

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
    if (!(ast_builder_ast(c)->nodes[b].flags & AST_NODE_FLAG_LVALUE)) {
        /* [basic.lval]/7: materialize the source before copying its data. */
        uint32_t held = full_expression_temporary(t);
        uint32_t variable = c_variable(c, held, t);
        uint32_t stored = assign(t, variable, b);
        b = dereference(comma(pointer, stored, address_of(variable, pointer)),
                        t);
    }
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
                                     const semantic_node &n,
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
    case builtin_lowering::clear_padding: {
        /* [ext:clang_builtin_functions] zero every byte of the pointee object
           that no base or non-static data member occupies */
        if (!n.count)
            return cast(
                ast_builder_integer(c, ast_builder_type_basic(AST_TYPE_INT), 0),
                v, AST_CAST_VOID);
        uint32_t arg_node = analysis.semantics.items(n.first)[0];
        qualified_type ptr_type = node(arg_node).type;
        qualified_type pointee = record(ptr_type).operand;
        uint64_t total = ast_builder_type_size(c, type(pointee, n.at));
        if (!total || record(pointee).kind != type_kind::class_type)
            return cast(arguments[0], v, AST_CAST_VOID);
        owned_array<uint8_t> covered;
        covered.resize(static_cast<size_t>(total));
        for (size_t i = 0; i < covered.size(); i++)
            covered[i] = 0;
        auto mark_type = [&](auto &&self, qualified_type qt,
                             uint64_t base_off) -> void {
            const struct type &rt = record(qt);
            if (rt.kind == type_kind::array && rt.bound_known) {
                uint64_t elem_size =
                    ast_builder_type_size(c, type(rt.operand, n.at));
                for (uint64_t i = 0; i < rt.bound; i++)
                    self(self, rt.operand, base_off + i * elem_size);
                return;
            }
            if (rt.kind == type_kind::class_type) {
                entity_index ce = rt.entity;
                const entity &cx = analysis.entity_table[ce];
                for (uint32_t i = 0; i < cx.base_count; i++) {
                    const entity &b =
                        analysis.entity_table
                            [analysis.class_bases[cx.first_base + i]];
                    if (!b.virtual_base)
                        self(self, b.type, base_off + b.offset);
                }
                for (uint32_t i = 0; i < cx.member_count; i++) {
                    entity_index me =
                        analysis.class_members[cx.first_member + i];
                    const entity &mx = analysis.entity_table[me];
                    if (mx.bit_field) {
                        uint64_t start_byte = (base_off * 8 + mx.offset) / 8;
                        uint64_t end_byte =
                            (base_off * 8 + mx.offset + mx.bit_width + 7) / 8;
                        for (uint64_t b = start_byte; b < end_byte && b < total;
                             b++)
                            covered[static_cast<size_t>(b)] = 1;
                    } else {
                        self(self, mx.type, base_off + mx.offset);
                    }
                }
                return;
            }
            uint64_t sz = ast_builder_type_size(c, type(qt, n.at));
            for (uint64_t b = 0; b < sz && base_off + b < total; b++)
                covered[static_cast<size_t>(base_off + b)] = 1;
        };
        mark_type(mark_type, pointee, 0);
        ast_qualified_type u8 = ast_builder_type_basic(AST_TYPE_UNSIGNED_CHAR);
        ast_qualified_type u8p = pointer_to(u8);
        ast_qualified_type sz_t = ast_builder_type_basic(AST_TYPE_LONG);
        uint32_t base_ptr =
            cast(arguments[0], u8p, AST_CAST_POINTER_TO_POINTER);
        uint32_t tmp = c_local(c, u8p);
        owned_array<uint32_t> stmts;
        stmts.append(c_declare(c, tmp));
        stmts.append(
            c_statement(c, assign(u8p, c_variable(c, tmp, u8p), base_ptr)));
        uint32_t zero = ast_builder_integer(c, u8, 0);
        for (uint64_t off = 0; off < total; off++) {
            if (covered[static_cast<size_t>(off)])
                continue;
            uint32_t ptr = c_variable(c, tmp, u8p);
            if (off)
                ptr = make(AST_NODE_BINARY, u8p, ptr,
                           ast_builder_integer(c, sz_t, off), 0, LEX_TOKEN_PLUS,
                           0);
            stmts.append(
                c_statement(c, assign(u8, dereference(ptr, u8), zero)));
        }
        return statement_expression(v, block(stmts));
    }
    case builtin_lowering::refuse:
        return fail("a C23 bit function (__builtin_stdc_*, "
                    "__builtin_bitreverseg) or __builtin_align_* outside a "
                    "constant expression",
                    n.at);
    case builtin_lowering::vtable_pointer: {
        /* the virtual table pointer is the first word of the object
           ([abi:vtable]): loaded through a pointer to a const void * */
        ast_qualified_type word = t;
        uint32_t slot =
            cast(arguments[0], pointer_to(word), AST_CAST_POINTER_TO_POINTER);
        return dereference(slot, word);
    }
    case builtin_lowering::align: {
        /* [ext:clang_builtin_functions] an integer operand and a constant
           alignment a: in unsigned long long, (x + (a - 1)) & ~(a - 1),
           x & ~(a - 1), or (x & (a - 1)) == 0, converted back */
        uint32_t callee = n.a;
        if (node(callee).kind == semantic_kind::conversion)
            callee = node(callee).a;
        const gnu_builtin &row = gnu_builtin_at(
            analysis.entity_table[node(callee).a].gnu_builtin_row);
        const uint32_t *items = analysis.semantics.items(n.first);
        const struct type &operand = record(node(items[0]).type);
        uint32_t alignment = items[1];
        while (node(alignment).kind == semantic_kind::conversion)
            alignment = node(alignment).a;
        uint64_t av = node(alignment).value;
        if (operand.kind == type_kind::pointer ||
            node(alignment).kind != semantic_kind::constant || !av ||
            (av & (av - 1)) || av > (1ull << 63))
            return fail("__builtin_align_* of a pointer or of an alignment "
                        "that is no constant",
                        n.at);
        int64_t low = static_cast<int64_t>(av - 1);
        ast_qualified_type wide =
            ast_builder_type_basic(AST_TYPE_UNSIGNED_LONG_LONG);
        ast_qualified_type narrow = ast_builder_type_basic(AST_TYPE_INT);
        uint32_t x = cast(arguments[0], wide, AST_CAST_INTEGER);
        auto binary = [&](uint32_t l, uint32_t r, int op,
                          ast_qualified_type type_) {
            return make(AST_NODE_BINARY, type_, l, r, 0,
                        static_cast<uint16_t>(op), 0);
        };
        if (row.which == 2) {
            uint32_t rest = binary(x, ast_builder_integer(c, wide, low),
                                   LEX_TOKEN_AMPERSAND, wide);
            return to_bool(binary(rest, ast_builder_integer(c, wide, 0),
                                  LEX_TOKEN_EQUAL_EQUAL, narrow));
        }
        if (row.which == 0)
            x = binary(x, ast_builder_integer(c, wide, low), LEX_TOKEN_PLUS,
                       wide);
        x = binary(x, ast_builder_integer(c, wide, ~low), LEX_TOKEN_AMPERSAND,
                   wide);
        return cast(x, t, AST_CAST_INTEGER);
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
             gnu_coroutine_builtin)) {
            const uint32_t *items = analysis.semantics.items(n.first);
            uint32_t seq = 0;
            uint32_t first_arg = 0;
            for (uint32_t i = 0; i < n.count; i++) {
                uint32_t a = expression(items[i]);
                if (!a)
                    return 0;
                if (i == 0)
                    first_arg = a;
                if (seq)
                    seq = make(AST_NODE_COMMA, type_of(a), seq, a, 0, 0, 0);
                else
                    seq = a;
            }
            ast_qualified_type rtype =
                AST_QUALIFIED_TYPE_UNQUALIFIED(type(n.type, n.at));
            const struct type &rt = analysis.type_table[n.type.unqualified()];
            if (rt.kind == type_kind::fundamental &&
                rt.fundamental == fundamental_type::void_) {
                if (seq)
                    return cast(seq, rtype, AST_CAST_VOID);
                return cast(ast_builder_integer(
                                c, ast_builder_type_basic(AST_TYPE_INT), 0),
                            rtype, AST_CAST_VOID);
            }
            uint32_t result = 0;
            if (n.count == 0 && rt.kind == type_kind::pointer) {
                if (!noop_coro_symbol) {
                    static const char nm[] = "__NoopCoro_ResumeDestroy";
                    noop_coro_symbol = local_function(nm, sizeof nm - 1);
                    ast_builder_symbol_body(
                        c, noop_coro_symbol,
                        make(AST_NODE_BLOCK, 0, 0, 0, 0, 0, 0));
                    ast_builder_symbol_flags_add(c, noop_coro_symbol,
                                                 AST_SYMBOL_FLAG_DEFINED);
                }
                ast_qualified_type fn_t =
                    ast_builder_type_function(c, v, nullptr, 0, 0);
                uint32_t fn_ref = c_variable(c, noop_coro_symbol, fn_t);
                result = cast(address_of(fn_ref, pointer_to(fn_t)), rtype,
                              AST_CAST_POINTER_TO_POINTER);
            } else if (n.count == 3 && first_arg) {
                result = first_arg;
            } else {
                int ck = AST_CAST_INTEGER;
                if (rt.kind == type_kind::pointer)
                    ck = AST_CAST_INTEGER_TO_POINTER;
                result = cast(ast_builder_integer(
                                  c, ast_builder_type_basic(AST_TYPE_INT), 0),
                              rtype, ck);
            }
            if (seq && result != first_arg)
                return make(AST_NODE_COMMA, rtype, seq, result, 0, 0, 0);
            return result;
        }
        /* [impl.clang] std::move, std::forward, std::as_const of a single
           argument: fold to the argument itself (no function call emitted) */
        if (node(named).kind == semantic_kind::entity && n.count == 1 &&
            n.category != value_category::prvalue) {
            const entity &fn = analysis.entity_table[node(named).a];
            entity_index enc = fn.enclosing;
            if (enc &&
                analysis.entity_table[enc].kind ==
                    entity_kind::namespace_entity &&
                analysis.entity_table[enc].enclosing == 0 && fn.name) {
                owned_buffer ns_name, fn_name;
                spelling(enc, ns_name);
                spelling(node(named).a, fn_name);
                if (ns_name.size() == 3 && !memcmp(ns_name.data(), "std", 3) &&
                    ((fn_name.size() == 4 &&
                      !memcmp(fn_name.data(), "move", 4)) ||
                     (fn_name.size() == 7 &&
                      !memcmp(fn_name.data(), "forward", 7)) ||
                     (fn_name.size() == 8 &&
                      !memcmp(fn_name.data(), "as_const", 8)))) {
                    const uint32_t *items = analysis.semantics.items(n.first);
                    uint32_t arg = expression(items[0]);
                    if (!arg)
                        return 0;
                    ast_qualified_type rtype = type(n.type, n.at);
                    ast_qualified_type want = pointer_to(rtype);
                    if (type_of(arg) != want)
                        arg = cast(arg, want, AST_CAST_POINTER_TO_POINTER);
                    return dereference(arg, rtype);
                }
            }
        }
        /* [impl.clang] constant-fold __builtin_sqrt / __builtin_ceill on
           floating-point literals (no libm call emitted) */
        if (node(named).kind == semantic_kind::entity && n.count == 1) {
            owned_buffer fn_name;
            spelling(node(named).a, fn_name);
            const uint32_t *items = analysis.semantics.items(n.first);
            const semantic_node &a0 = node(items[0]);
            if (a0.kind == semantic_kind::constant &&
                record(a0.type).kind == type_kind::fundamental &&
                (record(a0.type).fundamental == fundamental_type::double_ ||
                 record(a0.type).fundamental ==
                     fundamental_type::long_double)) {
                bool is_sqrt =
                    (fn_name.size() == 14 &&
                     !memcmp(fn_name.data(), "__builtin_sqrt", 14)) ||
                    (fn_name.size() == 4 && !memcmp(fn_name.data(), "sqrt", 4));
                bool is_ceill =
                    (fn_name.size() == 15 &&
                     !memcmp(fn_name.data(), "__builtin_ceill", 15)) ||
                    (fn_name.size() == 5 &&
                     !memcmp(fn_name.data(), "ceill", 5));
                if (is_sqrt || is_ceill) {
                    owned_buffer lit;
                    analysis.token_spelling(a0.at, lit.get());
                    if (lit.size()) {
                        vbuffer_append(lit.get(), "", 1);
                        long double v = strtold(lit.data(), nullptr);
                        if (is_sqrt && v >= 0.0L)
                            v = sqrtl(v);
                        else if (is_ceill)
                            v = ceill(v);
                        else
                            is_sqrt = false;
                        if (is_sqrt || is_ceill) {
                            owned_buffer out;
                            vbuffer_format_append(out.get(), "%.21Lg", v);
                            return ast_builder_floating_literal(
                                c, type(n.type, n.at), out.data(), out.size());
                        }
                    }
                }
            }
        }
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
        made = special_builtin(lowering, n, t, arguments);
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

/* [ext:gnu_conditional_omitted] a ? : c: the first operand, evaluated once
   (n.d), is the result when it is nonzero.  C's conditional has the same
   form (no second operand); a glvalue's address is held while its value is
   tested */
uint32_t c_lowering::conditional_omitted(const semantic_node &n,
                                         ast_qualified_type t)
{
    if (!is_glvalue(n)) {
        uint32_t value = expression(n.d), otherwise = expression(n.c);
        if (!value || !otherwise)
            return 0;
        return make(AST_NODE_CONDITIONAL, AST_QUALIFIED_TYPE_UNQUALIFIED(t),
                    value, 0, otherwise, 0, 0);
    }
    if (!in_function)
        return fail("a conditional with an omitted operand and an lvalue "
                    "result outside a function",
                    n.at);
    ast_qualified_type pointer = pointer_to(t);
    uint32_t first = object(n.d), second = object(n.c);
    if (!first || !second)
        return 0;
    uint32_t symbol = c_local(c, pointer);
    auto held = [&] { return c_variable(c, symbol, pointer); };
    uint32_t items[3];
    items[0] = c_declare(c, symbol);
    items[1] =
        c_statement(c, assign(pointer, held(), address_of(first, pointer)));
    /* the first operand where it is nonzero, else the other's address */
    uint32_t selected =
        make(AST_NODE_CONDITIONAL, pointer, dereference(held(), t), held(),
             address_of(second, pointer), 0, 0);
    items[2] =
        ast_builder_node(c, AST_NODE_EXPRESSION, pointer, selected, 0, 0, 0, 0);
    return dereference(statement_expression(pointer, block(items, 3)), t);
}

/* [expr.cond] */
uint32_t c_lowering::conditional(const semantic_node &n)
{
    ast_qualified_type t = type(n.type, n.at);
    if (!n.b)
        return conditional_omitted(n, t);
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
