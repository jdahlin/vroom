/* virtual-bases.cpp: virtual bases, this adjustments, thunks, deleting destructors, structor variants (lower-internal.h) */
#include "lower-internal.h"

namespace cplusplus::lowering
{

uint32_t c_lowering::virtual_base_address(uint32_t p, uint32_t again,
                                          entity_index k, entity_index v,
                                          ast_qualified_type t)
{
    const dynamic_class *d = analysis.dynamic_of(k);
    uint32_t index = 0;
    while (index < d->vbase_entry_count &&
           analysis.vbase_entries[d->first_vbase_entry + index] != v)
        index++;
    if (index == d->vbase_entry_count)
        return fail("a virtual base without a vbase offset", 0);
    /* the vbase offset: ((ptrdiff_t *)*(char **)p)[-3 - index] */
    return table_adjusted(p, again, -3 - static_cast<int64_t>(index), t);
}

/* &((char *)again)[((ptrdiff_t *)*(char **)p)[index]] as a t: p (and
   again, a second copy of it) moved by an offset in its virtual table */
uint32_t c_lowering::table_adjusted(uint32_t p, uint32_t again, int64_t index,
                                    ast_qualified_type t)
{
    ast_qualified_type byte = ast_builder_type_basic(AST_TYPE_CHAR);
    ast_qualified_type bytes = pointer_to(byte);
    ast_qualified_type ptrdiff = ast_builder_type_ptrdiff(c);
    if (!p || !again)
        return 0;
    uint32_t table = dereference(
        cast(p, pointer_to(bytes), AST_CAST_POINTER_TO_POINTER), bytes);
    uint32_t offset =
        make(AST_NODE_INDEX, ptrdiff,
             cast(table, pointer_to(ptrdiff), AST_CAST_POINTER_TO_POINTER),
             ast_builder_integer(c, ptrdiff, static_cast<uint64_t>(index)), 0,
             0, AST_NODE_FLAG_LVALUE);
    uint32_t at = make(AST_NODE_INDEX, byte,
                       cast(again, bytes, AST_CAST_POINTER_TO_POINTER), offset,
                       0, 0, AST_NODE_FLAG_LVALUE);
    return cast(make(AST_NODE_ADDRESS, bytes, at, 0, 0, 0, 0), t,
                AST_CAST_POINTER_TO_POINTER);
}

bool c_lowering::at_this(uint32_t n) const
{
    for (;;) {
        const semantic_node &x = node(n);
        if (x.kind == semantic_kind::entity)
            return structor_this && x.a == structor_this;
        if (x.kind == semantic_kind::address ||
            x.kind == semantic_kind::indirection ||
            (x.kind == semantic_kind::conversion &&
             x.op == static_cast<uint8_t>(conversion_kind::lvalue_to_rvalue)))
            n = x.a;
        else
            return false;
    }
}

entity_index c_lowering::base_subobject_of(uint32_t n) const
{
    const semantic_node &x = node(n);
    if (x.kind == semantic_kind::member &&
        analysis.entity_table[x.b].base_subobject && at_this(x.a))
        return x.b;
    if (x.kind != semantic_kind::indirection)
        return 0;
    const semantic_node &p = node(x.a);
    if (p.kind == semantic_kind::conversion &&
        p.op == static_cast<uint8_t>(conversion_kind::derived_to_base) &&
        at_this(p.a))
        return p.b;
    return 0;
}

/* [abi:vtable-ctor-vtt]: the base object constructor or destructor of a
   base with virtual bases takes its sub-VTT: in the base object variant
   the VTT this one was given plus the sub-VTT's index, in the complete
   object one an element of the class's own VTT */
uint32_t c_lowering::vtt_argument(entity_index b, token_index at)
{
    const entity &base = analysis.entity_table[b];
    entity_index of = record(base.type).entity;
    if (!structor_variant || !analysis.entity_table[of].virtual_base_count)
        return 0;
    /* [abi:vtable-ctor-vtt]: a virtual base's is one of the virtual VTTs,
       which only the complete object variants pass */
    uint64_t offset = base.offset;
    if (base.virtual_base)
        offset = virtual_base_offset(structor_class, of);
    else if (base.enclosing != structor_class)
        return 0;
    uint32_t index = sub_vtt_index(structor_class, of, offset);
    if (index == UINT32_MAX)
        return fail("a base without a sub-VTT", at);
    ast_qualified_type vpp = pointer_to(ast_builder_type_pointer(c, v));
    if (structor_variant == 2) {
        uint32_t p = c_variable(c, vtt_parameter, vpp);
        uint32_t element =
            make(AST_NODE_INDEX, pointer_to(v), p,
                 to_ptrdiff(ast_builder_integer(
                     c, ast_builder_type_basic(AST_TYPE_INT), index)),
                 0, 0, AST_NODE_FLAG_LVALUE);
        return address_of(element, vpp);
    }
    uint32_t s = vtt(structor_class, at);
    if (!s)
        return 0;
    vtt_layout layout;
    vtt_of(structor_class, layout);
    ast_qualified_type array =
        ast_builder_type_array(c, pointer_to(v), layout.entries.size(), true);
    return element_address(s, array, index);
}

ast_qualified_type c_lowering::structor_type(entity_index e, token_index at)
{
    const entity &x = analysis.entity_table[e];
    const struct type &f = record(x.type);
    owned_array<ast_qualified_type> parameters;
    parameters.append(
        pointer_to(type(analysis.entity_table[x.enclosing].type, at)));
    parameters.append(pointer_to(ast_builder_type_pointer(c, v)));
    const qualified_type *p =
        analysis.type_table.parameters(x.type.unqualified());
    for (uint32_t i = 0; i < f.parameter_count; i++)
        parameters.append(parameter_type(p[i], at));
    return ast_builder_type_function(c, type(f.operand, at), parameters.data(),
                                     f.parameter_count + 2, f.variadic);
}

/*
 * [class.cdtor]/4, [abi:vtable-ctor]: the virtual table pointers of the
 * class n.b stored in the object at n.a.  In a base object constructor or
 * destructor of a class with virtual bases, a pointer the VTT has an
 * entry for (the primary one first, so that the vbase offsets the others
 * are found with are the construction table's) is stored from the VTT it
 * was given, and one in a virtual base is found through its vbase offset;
 * otherwise each is the address point of the class's own table at its
 * subobject's offset.
 */
uint32_t c_lowering::vtable_pointer_stores(const semantic_node &n)
{
    entity_index k = n.b;
    const dynamic_class *d = analysis.dynamic_of(k);
    ast_qualified_type vp = pointer_to(v);
    ast_qualified_type vpp = pointer_to(vp);
    ast_qualified_type vppp = pointer_to(vpp);
    bool from_vtt = structor_variant == 2 && k == structor_class &&
                    analysis.entity_table[k].virtual_base_count;
    vtt_layout layout;
    if (from_vtt)
        vtt_of(k, layout);
    uint32_t stores = 0;
    for (uint32_t g = 0; g < d->group_count; g++) {
        const vtable_group &group = analysis.vtable_groups[d->first_group + g];
        uint32_t point = 0, index = UINT32_MAX;
        for (uint32_t i = 0; from_vtt && i < layout.entries.size(); i++)
            if (!layout.entries[i].table && layout.entries[i].group == g &&
                index == UINT32_MAX)
                index = i;
        if (index != UINT32_MAX) {
            uint32_t p = c_variable(c, vtt_parameter, vpp);
            point = make(AST_NODE_INDEX, vp, p,
                         to_ptrdiff(ast_builder_integer(
                             c, ast_builder_type_basic(AST_TYPE_INT), index)),
                         0, 0, AST_NODE_FLAG_LVALUE);
            point = cast(point, vpp, AST_CAST_POINTER_TO_POINTER);
        } else {
            point = address_point(k, g, n.at);
        }
        uint32_t where = 0;
        if (from_vtt && group.in_virtual_base) {
            where = virtual_base_address(expression(n.a), expression(n.a), k,
                                         group.in_virtual_base, vppp);
            if (where && group.offset_in_virtual)
                where = offset_pointer(
                    where, static_cast<int64_t>(group.offset_in_virtual), vppp);
        } else {
            uint32_t self = expression(n.a);
            if (!self)
                return 0;
            where = cast(self, vppp, AST_CAST_POINTER_TO_POINTER);
            if (group.offset)
                where = offset_pointer(self, static_cast<int64_t>(group.offset),
                                       vppp);
        }
        if (!where || !point)
            return 0;
        uint32_t slot = dereference(where, vpp);
        uint32_t store = assign(vpp, slot, point);
        if (stores)
            store = comma(vpp, stores, store);
        stores = store;
    }
    return stores;
}

/* [conv.ptr]/3, [expr.static.cast]/11: "The null pointer value is
   converted to the null pointer value of the destination type": p ? p +
   offset : 0, p read twice; an object's address (never null, e.g. to an
   empty base the layout moved, [abi:class-types] II) without the check.
   The non-virtual base conversions n's operand is made of add their
   offsets to the one check */
uint32_t c_lowering::null_checked_offset(const semantic_node &n, int64_t offset,
                                         ast_qualified_type t)
{
    uint32_t operand = n.a;
    for (;;) {
        const semantic_node &x = node(operand);
        if (x.kind != semantic_kind::conversion)
            break;
        conversion_kind k = static_cast<conversion_kind>(x.op);
        int64_t by = static_cast<int64_t>(analysis.entity_table[x.b].offset);
        if (k == conversion_kind::base_to_derived)
            by = -by;
        else if (k != conversion_kind::derived_to_base ||
                 analysis.entity_table[x.b].virtual_base)
            break;
        offset += by;
        operand = x.a;
    }
    if (node(operand).kind == semantic_kind::address) {
        uint32_t p = expression(operand);
        if (!p)
            return 0;
        return offset_pointer(p, offset, t);
    }
    /* (e.g. a pointer to the virtual base it is in, read from the virtual
       table, is held in a temporary) */
    reread r;
    uint32_t p =
        read_first(operand, r, "a pointer to a base at a nonzero offset");
    if (!p)
        return 0;
    uint32_t again = read_again(r);
    uint32_t null =
        cast(ast_builder_integer(c, ast_builder_type_basic(AST_TYPE_INT), 0), t,
             AST_CAST_NULL_POINTER);
    return make(AST_NODE_CONDITIONAL, AST_QUALIFIED_TYPE_UNQUALIFIED(t),
                truth(p), offset_pointer(again, offset, t), null, 0, 0);
}

uint32_t c_lowering::offset_pointer(uint32_t p, int64_t offset,
                                    ast_qualified_type t)
{
    ast_qualified_type byte = ast_builder_type_basic(AST_TYPE_CHAR);
    ast_qualified_type bytes = pointer_to(byte);
    uint32_t by = ast_builder_integer(c, ast_builder_type_ptrdiff(c),
                                      static_cast<uint64_t>(offset));
    uint32_t at =
        make(AST_NODE_INDEX, byte, cast(p, bytes, AST_CAST_POINTER_TO_POINTER),
             by, 0, 0, AST_NODE_FLAG_LVALUE);
    return cast(make(AST_NODE_ADDRESS, bytes, at, 0, 0, 0, 0), t,
                AST_CAST_POINTER_TO_POINTER);
}

/*
 * [abi:mangling-special-thunks]: a thunk of f.  A non-virtual one "adjusts
 * the this pointer by a constant offset" and calls f, named _ZThn <offset>
 * _ <f's encoding>; a virtual one adjusts it by the vcall offset its
 * virtual table has ([abi:vtable-components]: "the vcall offset ... is
 * used to adjust this"), named _ZTv 0 _ n<vcall offset's offset> _ <f's
 * encoding>; a destructor's for its complete object (D1) or deleting (D0)
 * variant.  It is defined where f is (with f's linkage), declared
 * elsewhere.
 */
uint32_t c_lowering::thunk(entity_index f, int64_t adjustment, token_index at,
                           int64_t vcall, int variant)
{
    for (const thunk_record &t : thunks)
        if (t.f == f && t.adjustment == adjustment && t.vcall == vcall &&
            t.variant == variant)
            return t.symbol;
    owned_buffer function_name;
    if (!itanium_symbol_name(analysis, tokens.pp, f, function_name.get(),
                             variant))
        return fail("a thunk whose function's symbol is not mangled", at);
    owned_buffer name;
    if (vcall)
        vbuffer_format_append(name.get(), "_ZTv%lld_n%llu_",
                              static_cast<long long>(adjustment),
                              static_cast<unsigned long long>(-vcall));
    else
        vbuffer_format_append(name.get(), "_ZThn%llu_",
                              static_cast<unsigned long long>(-adjustment));
    vbuffer_append(name.get(), function_name.data() + 2,
                   function_name.size() - 2);
    /* f's linkage, as symbol() gives it */
    int storage = AST_STORAGE_NONE;
    uint32_t flags = AST_SYMBOL_FLAG_GLOBAL;
    symbol_linkage(f, storage, flags);
    uint32_t s =
        ast_builder_symbol(c, AST_SYMBOL_FUNCTION, name.data(), name.size(),
                           entity_type(f, at), storage, flags, 0);
    ast_builder_toplevel(c, s);
    thunk_record r;
    r.f = f;
    r.adjustment = adjustment;
    r.vcall = vcall;
    r.variant = variant;
    r.symbol = s;
    thunks.append(r);
    return s;
}

/* where virtual function f is defined: its thunks, one per adjustment
   its class's virtual table group has for it (a destructor's slot pair
   two, D1's and D0's) */
bool c_lowering::thunk_definitions(entity_index f, token_index at)
{
    const entity &x = analysis.entity_table[f];
    const dynamic_class *d = analysis.dynamic_of(x.enclosing);
    if (!d)
        return true;
    for (uint32_t g = 1; g < d->group_count; g++) {
        /* a copy: thunk() and the definitions below do not touch the
           analysis, but keep no reference across them */
        const vtable_group group = analysis.vtable_groups[d->first_group + g];
        for (uint32_t i = 0; i < group.slot_count; i++) {
            uint32_t slot = group.first_slot + i;
            int64_t adjustment = analysis.vtable_adjustments[slot];
            int64_t vcall = vcall_offset_offset(group, i);
            if (analysis.vtable_slots[slot] != f || (!adjustment && !vcall))
                continue;
            int variant = 2;
            if (x.special == special_member::destructor) {
                variant = 1;
                if (i && analysis.vtable_slots[slot - 1] == f)
                    variant = 0;
            }
            if (!variant && !deleting_destructor(f, at, false))
                return false;
            uint32_t s = thunk(f, adjustment, at, vcall, variant);
            if (!s)
                return false;
            thunk_record r;
            bool done = false;
            for (thunk_record &t : thunks)
                if (t.symbol == s) {
                    done = t.defined;
                    t.defined = true;
                    r = t;
                }
            if (!done && !thunk_body(r, at))
                return false;
        }
    }
    return true;
}

/* the thunk's definition: return f(adjusted this, arguments...), this
   adjusted by ((char *)this + adjustment) and then by the vcall offset
   *(ptrdiff_t *)(*(char **)this + vcall) */
bool c_lowering::thunk_body(const thunk_record &r, token_index at)
{
    entity_index f = r.f;
    const entity &x = analysis.entity_table[f];
    const struct type &ft = record(x.type);
    if (ft.variadic || passed_indirectly(ft.operand))
        return fail("a thunk of a variadic function or one returning "
                    "a class by address",
                    at);
    ast_qualified_type function = entity_type(f, at);
    const qualified_type *types =
        analysis.type_table.parameters(x.type.unqualified());
    owned_array<ast_qualified_type> parameter_types;
    parameter_types.append(pointer_to(
        type(qualified_type::make(
                 analysis.entity_table[x.enclosing].type.unqualified(),
                 object_cv(x.member_cv)),
             at)));
    for (uint32_t k = 0; k < ft.parameter_count; k++)
        parameter_types.append(parameter_type(types[k], at));
    uint32_t count = ft.parameter_count + 1;
    owned_array<uint32_t> forwarded, arguments;
    for (uint32_t k = 0; k < count; k++) {
        ast_qualified_type t = parameter_types[k];
        uint32_t p = c_local(c, t);
        ast_builder_symbol_flags_add(c, p, AST_SYMBOL_FLAG_PARAMETER);
        forwarded.append(p);
        auto read = [&] { return c_variable(c, p, t); };
        uint32_t value = read();
        if (!k && r.adjustment)
            value = offset_pointer(value, r.adjustment, t);
        if (!k && r.vcall)
            /* the vcall offset at r.vcall bytes from the address point */
            value = table_adjusted(
                read(), value,
                r.vcall / static_cast<int64_t>(analysis.pointer_size()), t);
        arguments.append(value);
    }
    uint32_t target = symbol(f, at, r.variant);
    if (!target)
        return false;
    ast_builder_symbol_flags_add(c, target, AST_SYMBOL_FLAG_USED);
    uint32_t callee =
        cast(make(AST_NODE_VARIABLE, function, target, 0, 0, 0, 0),
             pointer_to(function), AST_CAST_FUNCTION_DECAY);
    ast_qualified_type result = type(ft.operand, at);
    uint32_t call =
        make(AST_NODE_CALL, result, callee,
             ast_builder_list(c, arguments.data(), count), count, 0, 0);
    uint32_t statement_node = 0;
    if (record(ft.operand).kind == type_kind::fundamental &&
        AST_QUALIFIED_TYPE_UNQUALIFIED(result) == v)
        statement_node = c_statement(c, call);
    else
        statement_node =
            ast_builder_node(c, AST_NODE_RETURN, v, call, 0, 0, 0, 0);
    uint32_t body = block(&statement_node, 1);
    struct node &b = ast_builder_ast(c)->nodes[body];
    b.a = ast_builder_list(c, forwarded.data(), count);
    b.aux = static_cast<uint16_t>(count);
    ast_builder_symbol_body(c, r.symbol, body);
    ast_builder_symbol_flags_add(c, r.symbol, AST_SYMBOL_FLAG_DEFINED);
    return true;
}

/*
 * [abi:mangling-special-ctor-dtor] D0, the deleting destructor of virtual
 * destructor e: its symbol, and
 * when define its definition (where e is defined): D1(this), then the
 * deallocation function of [class.dtor]/12 given this
 * (and the class's size when it takes one, [expr.delete]/11).
 */
bool c_lowering::deleting_destructor(entity_index e, token_index at,
                                     bool define)
{
    const entity &x = analysis.entity_table[e];
    uint32_t s = symbol(e, at, 0);
    if (!s)
        return false;
    if (!define)
        return true;
    entity_index k = x.enclosing;
    const dynamic_class *d = analysis.dynamic_of(k);
    if (!d || !d->deallocation) {
        fail("a deleting destructor without a deallocation function", at);
        return false;
    }
    ast_qualified_type self =
        pointer_to(type(analysis.entity_table[k].type, at));
    uint32_t p = c_local(c, self);
    ast_builder_symbol_flags_add(c, p, AST_SYMBOL_FLAG_PARAMETER);
    auto self_value = [&] { return c_variable(c, p, self); };
    uint32_t statements[2];
    statements[0] = special_call(e, self_value(), nullptr, 0, at);
    if (!statements[0])
        return false;
    statements[0] = c_statement(c, statements[0]);
    entity_index f = d->deallocation;
    uint32_t target = symbol(f, at);
    if (!target)
        return false;
    ast_builder_symbol_flags_add(c, target, AST_SYMBOL_FLAG_USED);
    ast_qualified_type function = entity_type(f, at);
    const qualified_type *types = analysis.type_table.parameters(
        analysis.entity_table[f].type.unqualified());
    uint32_t arguments[2];
    uint32_t count = 1;
    arguments[0] = cast(self_value(), parameter_type(types[0], at),
                        AST_CAST_POINTER_TO_POINTER);
    if (d->deallocation_sized) {
        arguments[1] = ast_builder_integer(
            c, AST_QUALIFIED_TYPE_UNQUALIFIED(parameter_type(types[1], at)),
            static_cast<int64_t>(analysis.entity_table[k].size));
        count = 2;
    }
    uint32_t callee =
        cast(make(AST_NODE_VARIABLE, function, target, 0, 0, 0, 0),
             pointer_to(function), AST_CAST_FUNCTION_DECAY);
    uint32_t call = make(AST_NODE_CALL, v, callee,
                         ast_builder_list(c, arguments, count), count, 0, 0);
    statements[1] = c_statement(c, call);
    uint32_t body = block(statements, 2);
    struct node &b = ast_builder_ast(c)->nodes[body];
    b.a = ast_builder_list(c, &p, 1);
    b.aux = 1;
    ast_builder_symbol_body(c, s, body);
    ast_builder_symbol_flags_add(c, s, AST_SYMBOL_FLAG_DEFINED);
    return true;
}

/* the final overrider of virtual function f for the object at the address
   object: (F *)(*(void ***)object)[slot] */
uint32_t c_lowering::virtual_function(entity_index f, uint32_t object,
                                      token_index at, uint32_t next)
{
    ast_qualified_type vp = pointer_to(v);
    ast_qualified_type vpp = pointer_to(vp);
    uint32_t table = dereference(
        cast(object, pointer_to(vpp), AST_CAST_POINTER_TO_POINTER), vpp);
    uint32_t slot = make(AST_NODE_INDEX, vp, table,
                         to_ptrdiff(ast_builder_integer(
                             c, ast_builder_type_basic(AST_TYPE_INT),
                             analysis.entity_table[f].vtable_slot + next)),
                         0, 0, AST_NODE_FLAG_LVALUE);
    ast_qualified_type ft = entity_type(f, at);
    return cast(slot, pointer_to(ft), AST_CAST_POINTER_TO_POINTER);
}

/* whether evaluating the lowered expression twice is evaluating it once:
   variables, their addresses, members, casts and reads through them */
bool c_lowering::side_effect_free(uint32_t lowered) const
{
    const struct node &n = ast_builder_ast(c)->nodes[lowered];
    switch (n.op) {
    case AST_NODE_VARIABLE:
    case AST_NODE_NUMBER:
        return true;
    case AST_NODE_INDEX:
    case AST_NODE_BINARY:
        return side_effect_free(n.a) && side_effect_free(n.b);
    case AST_NODE_ADDRESS:
    case AST_NODE_DEREFERENCE:
    case AST_NODE_CAST:
        return side_effect_free(n.a);
    case AST_NODE_MEMBER:
        return side_effect_free(n.a);
    default:
        return false;
    }
}

/*
 * The complete object variant of a constructor or destructor of a class
 * with virtual bases.  A constructor's is its body with the virtual bases'
 * initialization; [impl.clang] a destructor's calls the base object
 * destructor with the class's own VTT, then destroys the virtual bases.
 */
bool c_lowering::complete_variant(entity_index e)
{
    const entity &x = analysis.entity_table[e];
    const semantic_node &body = node(x.definition);
    uint32_t complete = symbol(e, body.at, 1);
    if (!complete)
        return false;
    if (x.special != special_member::destructor)
        return definition_body(e, complete, 1);
    const uint32_t *items = analysis.semantics.items(body.first);
    uint32_t self = symbol(items[0], body.at);
    if (!self)
        return false;
    ast_builder_symbol_flags_add(c, self, AST_SYMBOL_FLAG_PARAMETER);
    entity_index k = x.enclosing;
    uint32_t table = vtt(k, body.at);
    if (!table)
        return false;
    vtt_layout layout;
    vtt_of(k, layout);
    ast_qualified_type array =
        ast_builder_type_array(c, pointer_to(v), layout.entries.size(), true);
    ast_qualified_type pointer =
        pointer_to(type(analysis.entity_table[k].type, body.at));
    uint32_t call = special_call(e, c_variable(c, self, pointer), nullptr, 0,
                                 body.at, element_address(table, array, 0));
    if (!call)
        return false;
    labels.clear();
    live.clear();
    in_function = true;
    structor_variant = 3;
    structor_class = k;
    structor_this = items[0];
    uint32_t statements[2] = {c_statement(c, call), statement(body.c)};
    in_function = false;
    structor_variant = 0;
    structor_class = 0;
    structor_this = 0;
    if (failure.what)
        return false;
    uint32_t block_node = block(statements, 2);
    struct node &b = ast_builder_ast(c)->nodes[block_node];
    b.a = ast_builder_list(c, &self, 1);
    b.aux = 1;
    ast_builder_symbol_body(c, complete, block_node);
    ast_builder_symbol_flags_add(c, complete, AST_SYMBOL_FLAG_DEFINED);
    return true;
}

bool c_lowering::complete_variants()
{
    for (bool again = true; again;) {
        again = false;
        for (uint32_t i = 0; i < inline_structors.size(); i++) {
            entity_index e = inline_structors[i];
            if (!e || !complete_symbols[e])
                continue;
            inline_structors[i] = 0;
            again = true;
            if (!complete_variant(e))
                return false;
        }
    }
    return true;
}

bool c_lowering::with_virtual_bases(entity_index e) const
{
    const entity &x = analysis.entity_table[e];
    return x.special != special_member::none &&
           analysis.entity_table[x.enclosing].virtual_base_count;
}

} // namespace cplusplus::lowering
