/* member-pointers.cpp: pointers to members: their representation, conversions, comparisons, calls (lower-internal.h) */
#include "lower-internal.h"

namespace cplusplus::lowering
{

/* ---- pointers to members ([dcl.mptr]) ------------------------------- */

/*
 * [abi:member-function-pointers]: "the basic ABI properties of member
 * function pointer types are those of the following class, where fnptr_t
 * is the appropriate function-pointer type for a member function of this
 * type: struct { fnptr_t ptr; ptrdiff_t adj; };": one C struct, ptr a
 * void *.  The target (AArch64) has the 32-bit ARM representation: "the
 * this-adjustment stored in adj is left-shifted by one, and the low bit of
 * adj indicates whether ptr is a function pointer (including null) or the
 * offset of a v-table entry."
 */
ast_qualified_type c_lowering::member_function_pointer_type()
{
    if (member_function_pair)
        return member_function_pair;
    ast_qualified_type fields[2] = {pointer_to(void_type),
                                    ast_builder_type_ptrdiff(c)};
    const char *names[2] = {"ptr", "adj"};
    size_t lengths[2] = {3, 3};
    uint32_t aligned[2] = {0, 0};
    uint8_t packed[2] = {0, 0};
    int32_t widths[2] = {-1, -1};
    member_function_pair = ast_builder_type_record(c, "", 0, false);
    ast_builder_type_record_complete(c, member_function_pair, fields, names,
                                     lengths, aligned, packed, widths, 2);
    return member_function_pair;
}

bool c_lowering::holds_data_member_pointer(qualified_type t) const
{
    const struct type &r = record(t);
    if (r.kind == type_kind::array)
        return holds_data_member_pointer(r.operand);
    if (r.kind == type_kind::pointer_to_member)
        return record(r.operand).kind != type_kind::function;
    if (r.kind != type_kind::class_type)
        return false;
    const entity &x = analysis.entity_table[r.entity];
    for (uint32_t i = 0; i < x.member_count; i++)
        if (holds_data_member_pointer(
                analysis
                    .entity_table[analysis.class_members[x.first_member + i]]
                    .type))
            return true;
    for (uint32_t i = 0; i < x.virtual_base_count; i++)
        if (holds_data_member_pointer(
                analysis
                    .entity_table
                        [analysis.class_virtual_bases[x.first_virtual_base + i]]
                    .type))
            return true;
    return false;
}

bool c_lowering::member_pointer_value(uint32_t n, entity_index &member,
                                      int64_t &adjustment, bool *variable) const
{
    adjustment = 0;
    for (;;) {
        const semantic_node &x = node(n);
        if (x.kind == semantic_kind::member_pointer) {
            member = x.a;
            return true;
        }
        if (x.kind != semantic_kind::conversion)
            return false;
        switch (static_cast<conversion_kind>(x.op)) {
        case conversion_kind::null_member_pointer:
            member = 0;
            return true;
        case conversion_kind::lvalue_to_rvalue:
            /* a variable's value, for rereadable_member_pointer */
            if (variable && node(x.a).kind == semantic_kind::entity)
                return *variable = true;
            return false;
        case conversion_kind::member_pointer_to_derived:
            adjustment += static_cast<int64_t>(x.value);
            break;
        case conversion_kind::member_pointer_to_base:
            adjustment -= static_cast<int64_t>(x.value);
            break;
        case conversion_kind::member_pointer_qualification:
            break;
        default:
            return false;
        }
        n = x.a;
    }
}

/* {ptr, adj}: an initializer at namespace scope, an object in a
   function */
uint32_t c_lowering::member_function_pointer_of(uint32_t ptr, uint32_t adj,
                                                token_index at)
{
    ast_qualified_type t = member_function_pointer_type();
    uint32_t values[2] = {ptr, adj};
    uint32_t init = make(AST_NODE_INITIALIZER, t, 0,
                         ast_builder_list(c, values, 2), 2, 0, 0);
    if (!in_function)
        return init;
    return compound_literal_of(init, t);
}

/*
 * A member pointer constant: [expr.unary.op]/3.1's, or the null member
 * pointer value, through conversions.  [abi:data-member-pointers]: "A data
 * member pointer is represented as the data member's offset in bytes from
 * the address point of an object of the base type"; "A null data member
 * pointer is represented as an offset of -1."
 * [abi:member-function-pointers]: "A member function pointer for a
 * non-virtual member function is represented with ptr set to a pointer to
 * the function"; in the ARM representation "A virtual member function
 * pointer sets ptr to the v-table entry offset"; "A null member function
 * pointer sets ptr to a null function pointer and must ensure that the low
 * bit of adj is clear".
 */
uint32_t c_lowering::member_pointer(uint32_t n)
{
    const semantic_node &x = node(n);
    entity_index member = 0;
    int64_t adjustment = 0;
    if (!member_pointer_value(n, member, adjustment))
        return fail("a pointer to member", x.at);
    return member_pointer_constant(x.type, member, adjustment, x.at);
}

uint32_t c_lowering::member_pointer_constant(qualified_type t,
                                             entity_index member,
                                             int64_t adjustment, token_index at)
{
    if (!member)
        return null_member_pointer(t, at);
    ast_qualified_type d = ast_builder_type_ptrdiff(c);
    const entity &f = analysis.entity_table[member];
    if (record(record(t).operand).kind != type_kind::function)
        return ast_builder_integer(c, d,
                                   static_cast<int64_t>(f.offset) + adjustment);
    ast_qualified_type vp = pointer_to(void_type);
    uint32_t ptr = 0;
    int64_t adj = 2 * adjustment;
    if (f.virtual_) {
        ptr = cast(ast_builder_integer(
                       c, d,
                       static_cast<int64_t>(f.vtable_slot) *
                           static_cast<int64_t>(analysis.pointer_size())),
                   vp, AST_CAST_INTEGER_TO_POINTER);
        adj |= 1;
    } else {
        uint32_t s = symbol(member, at);
        if (!s)
            return 0;
        ast_builder_symbol_flags_add(c, s, AST_SYMBOL_FLAG_USED);
        ast_qualified_type ft = entity_type(member, at);
        ptr = cast(cast(make(AST_NODE_VARIABLE, ft, s, 0, 0, 0, 0),
                        pointer_to(ft), AST_CAST_FUNCTION_DECAY),
                   vp, AST_CAST_POINTER_TO_POINTER);
    }
    return member_function_pointer_of(ptr, ast_builder_integer(c, d, adj), at);
}

uint32_t c_lowering::null_member_pointer(qualified_type t, token_index at)
{
    if (record(record(t).operand).kind != type_kind::function)
        return ast_builder_integer(c, ast_builder_type_ptrdiff(c), -1);
    uint32_t zero =
        ast_builder_integer(c, ast_builder_type_basic(AST_TYPE_INT), 0);
    return member_function_pointer_of(
        cast(zero, pointer_to(void_type), AST_CAST_NULL_POINTER),
        ast_builder_integer(c, ast_builder_type_ptrdiff(c), 0), at);
}

uint32_t c_lowering::read_first(uint32_t n, reread &r, const char *what)
{
    r = reread();
    r.node = n;
    uint32_t p = expression(n);
    if (!p)
        return 0;
    r.type = AST_QUALIFIED_TYPE_UNQUALIFIED(type_of(p));
    if (side_effect_free(p) || rereadable_member_pointer(n))
        return p;
    if (!collecting)
        return fail(what, node(n).at);
    r.symbol = full_expression_temporary(r.type);
    return comma(r.type, assign(r.type, read_again(r), p), read_again(r));
}

/* a member pointer constant, or a variable's value, through member pointer
   conversions: lowered as new objects (compound literals) each time, with
   no effect */
bool c_lowering::rereadable_member_pointer(uint32_t n) const
{
    entity_index member = 0;
    int64_t adjustment = 0;
    bool variable = false;
    return member_pointer_value(n, member, adjustment, &variable);
}

uint32_t c_lowering::read_again(const reread &r)
{
    if (!r.symbol)
        return expression(r.node);
    return c_variable(c, r.symbol, r.type);
}

/*
 * [conv.mem]/1, /2 and [expr.static.cast]/12: a constant is folded
 * (member_pointer); otherwise [abi:data-member-pointers]: "Base-to-derived
 * and derived-to-base conversions of a non-null data member pointer can be
 * performed by adding or subtracting (respectively) the static offset of
 * the base within the derived class" (p == -1 ? -1 : p + offset), and
 * [abi:member-function-pointers]: "in the 32-bit ARM representation, the
 * addend must be left-shifted by one.  Because the adjustment does not
 * factor into whether a member function pointer is null, this addition can
 * be done unconditionally when performing a conversion."
 */
uint32_t c_lowering::member_pointer_conversion(const semantic_node &n)
{
    if (static_cast<conversion_kind>(n.op) ==
        conversion_kind::null_member_pointer)
        return null_member_pointer(n.type, n.at);
    int64_t offset = static_cast<int64_t>(n.value);
    if (static_cast<conversion_kind>(n.op) ==
        conversion_kind::member_pointer_to_base)
        offset = -offset;
    entity_index member = 0;
    int64_t adjustment = 0;
    if (member_pointer_value(n.a, member, adjustment))
        return member_pointer_constant(n.type, member, adjustment + offset,
                                       n.at);
    ast_qualified_type d = ast_builder_type_ptrdiff(c);
    if (record(record(n.type).operand).kind == type_kind::function) {
        /* {p.ptr, p.adj + 2 * offset}, p read twice */
        if (!offset)
            return expression(n.a);
        reread r;
        uint32_t first = read_first(n.a, r,
                                    "a pointer to member function converted "
                                    "outside a statement");
        if (!first)
            return 0;
        uint32_t held = first;
        if (r.symbol)
            held = read_again(r);
        uint32_t adj =
            make(AST_NODE_BINARY, d, ast_builder_member(c, read_again(r), 1, d),
                 ast_builder_integer(c, d, 2 * offset), 0, LEX_TOKEN_PLUS, 0);
        uint32_t pair = member_function_pointer_of(
            ast_builder_member(c, held, 0, pointer_to(void_type)), adj, n.at);
        if (!r.symbol)
            return pair;
        /* the temporary assigned before the pair reads it */
        return comma(r.type, first, pair);
    }
    if (!offset)
        return expression(n.a);
    reread r;
    uint32_t first = read_first(n.a, r,
                                "a pointer to data member converted "
                                "outside a statement");
    if (!first)
        return 0;
    uint32_t null = ast_builder_integer(c, d, -1);
    uint32_t test =
        make(AST_NODE_BINARY, ast_builder_type_basic(AST_TYPE_INT), first,
             ast_builder_integer(c, d, -1), 0, LEX_TOKEN_EQUAL_EQUAL, 0);
    uint32_t moved =
        make(AST_NODE_BINARY, d, read_again(r),
             ast_builder_integer(c, d, offset), 0, LEX_TOKEN_PLUS, 0);
    return make(AST_NODE_CONDITIONAL, d, test, null, moved, 0, 0);
}

/*
 * [expr.mptr.oper]/2: "The result is an object ... of the type specified
 * by the second operand": *(T *)((char *)object + p), the object's address
 * first ([expr.mptr.oper]/4: "the expression E1 is sequenced before the
 * expression E2").
 */
uint32_t c_lowering::member_pointer_object(const semantic_node &n)
{
    if (record(n.type).kind == type_kind::function)
        return fail("a member function through a pointer to member outside "
                    "a call",
                    n.at);
    uint32_t object = expression(n.a);
    uint32_t offset = expression(n.b);
    if (!object || !offset)
        return 0;
    if (!side_effect_free(object) && !side_effect_free(offset))
        /* C's operands are unsequenced */
        return fail("a pointer to data member applied with effects in both "
                    "operands",
                    n.at);
    ast_qualified_type byte = ast_builder_type_basic(AST_TYPE_CHAR);
    ast_qualified_type bytes = pointer_to(byte);
    ast_qualified_type t = type(n.type, n.at);
    uint32_t at = make(AST_NODE_INDEX, byte,
                       cast(object, bytes, AST_CAST_POINTER_TO_POINTER),
                       to_ptrdiff(offset), 0, 0, AST_NODE_FLAG_LVALUE);
    uint32_t address = cast(make(AST_NODE_ADDRESS, bytes, at, 0, 0, 0, 0),
                            pointer_to(t), AST_CAST_POINTER_TO_POINTER);
    return dereference(address, t);
}

/*
 * [conv.bool]/1: "A zero value, null pointer value, or null member pointer
 * value is converted to false": [abi:data-member-pointers] p != -1;
 * [abi:member-function-pointers]: "A member function pointer is null if
 * ptr is equal to a null function pointer and (only when using the 32-bit
 * ARM representation) the low bit of adj is clear": p.ptr != 0 || (p.adj &
 * 1).
 */
uint32_t c_lowering::member_pointer_test(uint32_t n, token_index at)
{
    ast_qualified_type i = ast_builder_type_basic(AST_TYPE_INT);
    ast_qualified_type d = ast_builder_type_ptrdiff(c);
    if (record(record(node(n).type).operand).kind != type_kind::function) {
        uint32_t p = expression(n);
        if (!p)
            return 0;
        return to_bool(make(AST_NODE_BINARY, i, p,
                            ast_builder_integer(c, d, -1), 0,
                            LEX_TOKEN_EXCLAMATION_EQUAL, 0));
    }
    ast_qualified_type vp = pointer_to(void_type);
    reread r;
    uint32_t first = read_first(n, r,
                                "a pointer to member function tested "
                                "outside a statement");
    if (!first)
        return 0;
    uint32_t ptr = ast_builder_member(c, first, 0, vp);
    uint32_t adj = ast_builder_member(c, read_again(r), 1, d);
    uint32_t odd = make(AST_NODE_BINARY, d, adj, ast_builder_integer(c, d, 1),
                        0, LEX_TOKEN_AMPERSAND, 0);
    return to_bool(make(AST_NODE_LOGICAL_OR, i, ptr, odd, 0, 0, 0));
}

/*
 * [expr.eq]/4: "If two pointers to members are both the null member
 * pointer value, they compare equal. ... Otherwise, they compare equal if
 * and only if they would refer to the same member of the same most derived
 * object or the same subobject if indirection with a hypothetical object
 * of the associated class type were performed".  [abi:data-member-pointers]
 * as integers; [abi:member-function-pointers]: "Two member function
 * pointers are equal if they are both null or if their corresponding
 * values of ptr and adj are equal": a.ptr == b.ptr && (a.adj == b.adj ||
 * (a.ptr == 0 && ((a.adj | b.adj) & 1) == 0)).
 */
uint32_t c_lowering::member_pointer_equality(const semantic_node &n, int op)
{
    ast_qualified_type i = ast_builder_type_basic(AST_TYPE_INT);
    ast_qualified_type d = ast_builder_type_ptrdiff(c);
    if (record(record(node(n.a).type).operand).kind != type_kind::function) {
        uint32_t a = expression(n.a), b = expression(n.b);
        if (!a || !b)
            return 0;
        return to_bool(
            make(AST_NODE_BINARY, i, a, b, 0, static_cast<uint16_t>(op), 0));
    }
    ast_qualified_type vp = pointer_to(void_type);
    const char *what = "pointers to member functions compared outside a "
                       "statement";
    reread ra, rb;
    uint32_t a = read_first(n.a, ra, what);
    if (!a)
        return 0;
    uint32_t b = read_first(n.b, rb, what);
    if (!b)
        return 0;
    auto binary_int = [&](uint32_t x, uint32_t y, int o) {
        return make(AST_NODE_BINARY, i, x, y, 0, static_cast<uint16_t>(o), 0);
    };
    uint32_t same_ptr =
        binary_int(ast_builder_member(c, a, 0, vp),
                   ast_builder_member(c, b, 0, vp), LEX_TOKEN_EQUAL_EQUAL);
    uint32_t same_adj = binary_int(ast_builder_member(c, read_again(ra), 1, d),
                                   ast_builder_member(c, read_again(rb), 1, d),
                                   LEX_TOKEN_EQUAL_EQUAL);
    uint32_t null_ptr =
        make(AST_NODE_UNARY, i, ast_builder_member(c, read_again(ra), 0, vp), 0,
             0, LEX_TOKEN_EXCLAMATION, 0);
    uint32_t both =
        make(AST_NODE_BINARY, d, ast_builder_member(c, read_again(ra), 1, d),
             ast_builder_member(c, read_again(rb), 1, d), 0, LEX_TOKEN_PIPE, 0);
    uint32_t even =
        binary_int(make(AST_NODE_BINARY, d, both, ast_builder_integer(c, d, 1),
                        0, LEX_TOKEN_AMPERSAND, 0),
                   ast_builder_integer(c, d, 0), LEX_TOKEN_EQUAL_EQUAL);
    uint32_t equal = make(
        AST_NODE_LOGICAL_AND, i, same_ptr,
        make(AST_NODE_LOGICAL_OR, i, same_adj,
             make(AST_NODE_LOGICAL_AND, i, null_ptr, even, 0, 0, 0), 0, 0, 0),
        0, 0, 0);
    if (op == LEX_TOKEN_EXCLAMATION_EQUAL)
        equal = make(AST_NODE_UNARY, i, equal, 0, 0, LEX_TOKEN_EXCLAMATION, 0);
    return to_bool(equal);
}

/*
 * [expr.mptr.oper]/6: (E1.*E2)(args), as [abi:member-function-pointers]
 * says: "A call is performed as follows: Add the stored adjustment to the
 * this address.  If the member pointer stores a v-table entry offset,
 * load the v-table from the adjusted this address and call the v-table
 * entry at the stored offset.  Otherwise, call the stored function
 * pointer."  Into items: p = E2's value, o = (char *)E1's address + (p.adj
 * >> 1); the callee returned, (p.adj & 1 ? *(F **)(*(char **)o +
 * (ptrdiff_t)p.ptr) : (F *)p.ptr), and object o.
 */
uint32_t c_lowering::member_function_call(const semantic_node &n,
                                          owned_array<uint32_t> &items,
                                          uint32_t &object)
{
    const semantic_node &access = node(n.a);
    ast_qualified_type vp = pointer_to(void_type);
    ast_qualified_type d = ast_builder_type_ptrdiff(c);
    ast_qualified_type byte = ast_builder_type_basic(AST_TYPE_CHAR);
    ast_qualified_type bytes = pointer_to(byte);
    uint32_t address = held_value(access.a, bytes, items);
    if (!address)
        return 0;
    uint32_t pair = held_value(access.b, member_function_pointer_type(), items);
    if (!pair)
        return 0;
    ast_qualified_type t = member_function_pointer_type();
    auto read = [&](uint32_t s, ast_qualified_type type_) {
        return c_variable(c, s, type_);
    };
    auto adj = [&] { return ast_builder_member(c, read(pair, t), 1, d); };
    /* o = (char *)E1 + (p.adj >> 1) */
    uint32_t moved =
        make(AST_NODE_INDEX, byte, read(address, bytes),
             make(AST_NODE_BINARY, d, adj(), ast_builder_integer(c, d, 1), 0,
                  LEX_TOKEN_GREATER_GREATER, 0),
             0, 0, AST_NODE_FLAG_LVALUE);
    items.append(c_statement(
        c, assign(bytes, read(address, bytes),
                  make(AST_NODE_ADDRESS, bytes, moved, 0, 0, 0, 0))));
    const struct type &pointer = record(node(access.b).type);
    qualified_type class_type_ = qualified_type::make(
        analysis.entity_table[pointer.entity].type.unqualified(), cv_none);
    ast_qualified_type ft =
        member_function_type(class_type_, pointer.operand, n.at);
    ast_qualified_type fp = pointer_to(ft);
    /* *(F **)(*(char **)o + (ptrdiff_t)p.ptr) */
    uint32_t table = dereference(cast(read(address, bytes), pointer_to(bytes),
                                      AST_CAST_POINTER_TO_POINTER),
                                 bytes);
    uint32_t entry = make(AST_NODE_INDEX, byte, table,
                          cast(ast_builder_member(c, read(pair, t), 0, vp), d,
                               AST_CAST_POINTER_TO_INTEGER),
                          0, 0, AST_NODE_FLAG_LVALUE);
    uint32_t virtual_callee =
        dereference(cast(make(AST_NODE_ADDRESS, bytes, entry, 0, 0, 0, 0),
                         pointer_to(fp), AST_CAST_POINTER_TO_POINTER),
                    fp);
    uint32_t direct_callee = cast(ast_builder_member(c, read(pair, t), 0, vp),
                                  fp, AST_CAST_POINTER_TO_POINTER);
    uint32_t odd = make(AST_NODE_BINARY, d, adj(), ast_builder_integer(c, d, 1),
                        0, LEX_TOKEN_AMPERSAND, 0);
    object = cast(read(address, bytes), pointer_to(type(class_type_, n.at)),
                  AST_CAST_POINTER_TO_POINTER);
    return make(AST_NODE_CONDITIONAL, fp, odd, virtual_callee, direct_callee, 0,
                0);
}

} // namespace cplusplus::lowering
