/* values.cpp: values: reads, constants, string literals, conversions (lower-internal.h) */
#include "lower-internal.h"

namespace cplusplus::lowering
{

/* ---- expressions ---------------------------------------------------- */

ast_qualified_type c_lowering::type_of(uint32_t n)
{
    return ast_builder_ast(c)->nodes[n].ty;
}

/* a C comparison's or logical operator's int as the bool C++ gives */
uint32_t c_lowering::to_bool(uint32_t int_value)
{
    return cast(int_value, ast_builder_type_basic(AST_TYPE_BOOL),
                AST_CAST_BOOL);
}

/* a value whose truth is tested (the operand of !, && and ||, a
   condition): a conversion to bool tests as its operand does
   ([conv.bool]/1: "A zero value, null pointer value, or null member
   pointer value is converted to false; any other value is converted to
   true", as C's test against 0), so the int C's comparisons and logical
   operators give is tested without being made a bool first */
uint32_t c_lowering::truth(uint32_t value)
{
    if (!value)
        return 0;
    const struct node &x = ast_builder_ast(c)->nodes[value];
    if (x.op == AST_NODE_CAST && x.aux == AST_CAST_BOOL)
        return x.a;
    return value;
}

/* C adds a pointer and an integer of ptrdiff_t's type */
uint32_t c_lowering::to_ptrdiff(uint32_t integer)
{
    ast_qualified_type d = ast_builder_type_ptrdiff(c);
    if (AST_QUALIFIED_TYPE_UNQUALIFIED(type_of(integer)) == d)
        return integer;
    return cast(integer, d, AST_CAST_INTEGER);
}

void c_lowering::address_taken(uint32_t lvalue)
{
    const struct node &n = ast_builder_ast(c)->nodes[lvalue];
    if (n.op == AST_NODE_VARIABLE)
        ast_builder_symbol_flags_add(c, n.a, AST_SYMBOL_FLAG_ADDRESS_TAKEN);
}

uint32_t c_lowering::address_of(uint32_t lvalue, ast_qualified_type pointer)
{
    address_taken(lvalue);
    return make(AST_NODE_ADDRESS, pointer, lvalue, 0, 0, 0, 0);
}

/* a glvalue as a C lvalue.  [expr.ass]/1 and [expr.pre.incr]/1: an
   assignment's or prefix increment's result is its left operand, an
   lvalue, which C's is not: *({ T *p; p = &lhs; *p op= rhs; p; }), the
   left operand evaluated once */
uint32_t c_lowering::member_access(const semantic_node &n, uint32_t object)
{
    const entity &x = analysis.entity_table[n.b];
    if (!is_reference(x.type) || is_reference(n.type))
        /* a reference member being initialized ([class.base.init]) is the
           pointer itself */
        return ast_builder_member(c, object, c_fields[n.b], type(n.type, n.at));
    /* [expr.ref]/6.1: a reference member is the object it refers to */
    uint32_t pointer =
        ast_builder_member(c, object, c_fields[n.b], type(x.type, n.at));
    return dereference(pointer, type(n.type, n.at));
}

uint32_t c_lowering::object(uint32_t n)
{
    if (node(n).kind == semantic_kind::member && !is_glvalue(node(node(n).a))) {
        /* [expr.ref]/6.3: a member of a class prvalue is an xvalue; its
           object is the temporary the prvalue is materialized into
           ([class.temporary]/2) */
        uint32_t held = temporary_object(node(n).a);
        if (!held)
            return 0;
        return member_access(node(n), held);
    }
    if ((node(n).kind == semantic_kind::conditional ||
         node(n).kind == semantic_kind::initializer_list) &&
        !is_glvalue(node(n)) &&
        record(node(n).type).kind == type_kind::class_type)
        return temporary_object(n);
    uint32_t made = expression(n);
    if (!made)
        return 0;
    const struct node &x = ast_builder_ast(c)->nodes[made];
    if ((x.flags & AST_NODE_FLAG_LVALUE) ||
        record(node(n).type).kind == type_kind::function)
        return made;
    bool assigns = x.op == AST_NODE_ASSIGN || x.op == AST_NODE_PRE_INCREMENT ||
                   x.op == AST_NODE_PRE_DECREMENT;
    if (!assigns || !in_function)
        return fail("an assignment or increment used as an object", node(n).at);
    uint32_t left = x.a;
    ast_qualified_type t = type_of(left);
    ast_qualified_type pointer = pointer_to(t);
    uint32_t symbol = c_local(c, pointer);
    auto variable = [&] { return c_variable(c, symbol, pointer); };
    uint32_t items[4];
    items[0] = c_declare(c, symbol);
    items[1] =
        c_statement(c, assign(pointer, variable(), address_of(left, pointer)));
    /* the assignment, through p */
    uint32_t through = dereference(variable(), t);
    ast_builder_ast(c)->nodes[made].a = through;
    items[2] = c_statement(c, made);
    items[3] = ast_builder_node(c, AST_NODE_EXPRESSION, pointer, variable(), 0,
                                0, 0, 0);
    uint32_t address = statement_expression(pointer, block(items, 4));
    return dereference(address, t);
}

/* [conv.lval]/3.4: the value of a glvalue: "the value contained in the
   object indicated by the glvalue is the prvalue result".  C reads an
   lvalue where it is used as a value; a comma's or conditional's glvalue
   is read in each operand, and an assignment's value is the one stored */
bool c_lowering::folded_read(const semantic_node &x) const
{
    return x.kind == semantic_kind::entity &&
           x.evaluation == constness::constant &&
           analysis.entity_table[x.a].kind == entity_kind::variable &&
           analysis.entity_table[x.a].usable == constness::constant &&
           /* (a class object has no scalar value to fold: its definition
              is read) */
           record(x.type).kind != type_kind::class_type;
}

uint32_t c_lowering::read(uint32_t n)
{
    const semantic_node &x = node(n);
    ast_qualified_type t = type(x.type, x.at);
    if (x.kind == semantic_kind::comma)
        return comma(AST_QUALIFIED_TYPE_UNQUALIFIED(t), discard(x.a),
                     read(x.b));
    if (x.kind == semantic_kind::conditional && x.b)
        return make(AST_NODE_CONDITIONAL, AST_QUALIFIED_TYPE_UNQUALIFIED(t),
                    expression(x.a), read(x.b), read(x.c), 0, 0);
    /* [basic.def.odr]/4: a variable usable in constant expressions whose
       value is read is not odr-used; its value, which a static data member
       initialized in its class ([class.static.data]/4) needs, having no
       definition to read */
    if (folded_read(x)) {
        semantic_node value = x;
        value.value = analysis.entity_table[x.a].value;
        return constant(value);
    }
    return expression(n);
}

/* [expr.context]/2: a discarded-value expression, evaluated for its
   effects; a glvalue comma or conditional selects no object then */
uint32_t c_lowering::discard(uint32_t n)
{
    const semantic_node &x = node(n);
    if (x.kind == semantic_kind::comma) {
        uint32_t b = discard(x.b);
        return comma(type_of(b), discard(x.a), b);
    }
    if (x.kind == semantic_kind::conditional && x.b && is_glvalue(x))
        return make(AST_NODE_CONDITIONAL, void_type, expression(x.a),
                    cast(discard(x.b), void_type, AST_CAST_VOID),
                    cast(discard(x.c), void_type, AST_CAST_VOID), 0, 0);
    return expression(n);
}

uint32_t c_lowering::constant(const semantic_node &n)
{
    const struct type &r = record(n.type);
    ast_qualified_type t = type(n.type, n.at);
    if (r.kind == type_kind::pointer ||
        (r.kind == type_kind::fundamental &&
         r.fundamental == fundamental_type::std_nullptr_t))
        return cast(
            ast_builder_integer(c, ast_builder_type_basic(AST_TYPE_INT), 0),
            AST_QUALIFIED_TYPE_UNQUALIFIED(t), AST_CAST_NULL_POINTER);
    fundamental_type f = r.fundamental;
    if (r.kind == type_kind::enumeration)
        f = analysis.entity_table[r.entity].underlying;
    else if (r.kind != type_kind::fundamental)
        return fail("a constant of this type", n.at);
    if (f == fundamental_type::long_double) {
        if (n.evaluation == constness::constant)
            return ast_builder_floating(c, t, static_cast<double>(n.value));
        owned_buffer sp;
        analysis.token_spelling(n.at, sp.get());
        if (sp.size()) {
            size_t len = sp.size();
            while (len > 0 &&
                   (sp.data()[len - 1] == 'l' || sp.data()[len - 1] == 'L' ||
                    sp.data()[len - 1] == 'f' || sp.data()[len - 1] == 'F'))
                len--;
            return ast_builder_floating_literal(c, t, sp.data(), len);
        }
        return fail("a floating constant of this type", n.at);
    }
    if (n.evaluation != constness::constant)
        return fail("a constant whose value analysis does not know", n.at);
    switch (f) {
    case fundamental_type::float_:
    case fundamental_type::double_: {
        /* A float constant normally reaches C as a double. Use an exact
           bit cast when narrowing that double would quiet a signaling NaN. */
        if (f == fundamental_type::float_ &&
            floating_is_signaling_nan(n.value)) {
            uint32_t bits = ast_builder_integer(
                c, ast_builder_type_basic(AST_TYPE_UNSIGNED_INT),
                float_storage_bits(n.value));
            return cast(bits, t, AST_CAST_BIT_CAST);
        }
        double d;
        memcpy(&d, &n.value, sizeof d);
        return ast_builder_floating(c, t, d);
    }
    case fundamental_type::long_double:
    case fundamental_type::float16:
        return fail("a floating constant of this type", n.at);
    default:
        return ast_builder_integer(c, AST_QUALIFIED_TYPE_UNQUALIFIED(t),
                                   static_cast<int64_t>(n.value));
    }
}

/* [lex.string]: an lvalue array of the literal's code units and a '\0' */
uint32_t c_lowering::string_literal(const semantic_node &n)
{
    const token &t = tokens[n.at];
    qualified_type element = record(n.type).operand;
    const struct type &e = record(element);
    uint8_t size = 1;
    uint8_t kind = LEX_TOKEN_STRING;
    switch (e.fundamental) {
    case fundamental_type::char16_t_:
        size = 2;
        kind = LEX_TOKEN_UTF16_STRING;
        break;
    case fundamental_type::char32_t_:
        size = 4;
        kind = LEX_TOKEN_UTF32_STRING;
        break;
    case fundamental_type::wchar_t_:
        size = 4;
        kind = LEX_TOKEN_WIDE_STRING;
        break;
    case fundamental_type::char8_t_:
        kind = LEX_TOKEN_UTF8_STRING;
        break;
    default:
        break;
    }
    const uint32_t *units = tokens.files.phase7().string_code_units();
    const literal_value &v = tokens.files.value_of(t);
    owned_array<uint8_t> bytes;
    uint32_t count = v.string_length + 1;
    bytes.resize(static_cast<size_t>(count) * size);
    for (uint32_t i = 0; i < count; i++) {
        uint32_t u = 0;
        if (i < v.string_length)
            u = units[v.string_offset + i];
        /* little-endian, as the targets are */
        for (uint8_t b = 0; b < size; b++)
            bytes[static_cast<size_t>(i) * size + b] =
                static_cast<uint8_t>(u >> (8 * b));
    }
    return ast_builder_string(c, type(element, n.at), bytes.data(), count, size,
                              kind);
}

/* [ext:clang_source_location] the place is the call's inside a default
   argument (default_argument), else where the builtin is written */
uint32_t c_lowering::source_location(const semantic_node &n)
{
    token_index at = n.at;
    entity_index function = n.b;
    if (call_site_at) {
        at = call_site_at;
        function = call_site_function;
    }
    source_position p = tokens.position(at);
    source_location_builtin which = static_cast<source_location_builtin>(n.op);
    ast_qualified_type t = AST_QUALIFIED_TYPE_UNQUALIFIED(type(n.type, n.at));
    if (which == source_location_builtin::line)
        return ast_builder_integer(c, t, p.line);
    if (which == source_location_builtin::column)
        return ast_builder_integer(c, t, p.column);
    owned_buffer text;
    if (which == source_location_builtin::function) {
        if (function)
            spelling(function, text);
    } else {
        const char *name = p.file_name;
        if (which == source_location_builtin::file_name) {
            const char *slash = strrchr(name, '/');
            if (slash)
                name = slash + 1;
        }
        vbuffer_string_append(text.get(), name);
    }
    /* the characters and a NUL: a string-literal's array of const char,
       which decays */
    owned_array<uint8_t> bytes;
    bytes.resize(text.size() + 1);
    for (size_t i = 0; i < text.size(); i++)
        bytes[i] = static_cast<uint8_t>(text.data()[i]);
    bytes[text.size()] = 0;
    qualified_type element = record(n.type).operand;
    uint32_t array = ast_builder_string(c, type(element, n.at), bytes.data(),
                                        static_cast<uint32_t>(bytes.size()), 1,
                                        LEX_TOKEN_STRING);
    return cast(array, t, AST_CAST_ARRAY_DECAY);
}

/* [dcl.fct.def.general]/8: the function's name as a string constant */
uint32_t c_lowering::predefined_name(const semantic_node &n)
{
    owned_buffer text;
    analysis.predefined_name_text(n.b, static_cast<predefined_name_form>(n.op),
                                  text);
    owned_array<uint8_t> bytes;
    bytes.resize(text.size() + 1);
    for (size_t i = 0; i < text.size(); i++)
        bytes[i] = static_cast<uint8_t>(text.data()[i]);
    bytes[text.size()] = 0;
    qualified_type element = record(n.type).operand;
    return ast_builder_string(c, type(element, n.at), bytes.data(),
                              static_cast<uint32_t>(bytes.size()), 1,
                              LEX_TOKEN_STRING);
}

/* [dcl.fct.default]/5: the argument's builtins are those of the call */
uint32_t c_lowering::default_argument(const semantic_node &n)
{
    token_index outer_at = call_site_at;
    entity_index outer_function = call_site_function;
    call_site_at = n.at;
    call_site_function = n.b;
    uint32_t made = expression(n.a);
    call_site_at = outer_at;
    call_site_function = outer_function;
    return made;
}

uint32_t c_lowering::conversion(const semantic_node &n)
{
    ast_qualified_type t = AST_QUALIFIED_TYPE_UNQUALIFIED(type(n.type, n.at));
    switch (static_cast<conversion_kind>(n.op)) {
    case conversion_kind::lvalue_to_rvalue:
        /* [ext:clang_atomic] an atomic object's value: C's atomic load, a
           conversion of the atomic lvalue to its value type */
        if (analysis.type_table[node(n.a).type.unqualified()].kind ==
            type_kind::atomic) {
            /* (a constant's value, of the value type: clang folds a
               constexpr atomic's read) */
            const semantic_node &object = node(n.a);
            if (folded_read(object)) {
                semantic_node value = object;
                value.value = analysis.entity_table[object.a].value;
                value.type = n.type;
                return constant(value);
            }
            return cast(read(n.a), t, AST_CAST_NOP);
        }
        {
            /* [expr.const]/5.8.1: a constexpr pointer variable is usable
               in constant expressions, its read the constant its
               initializer is (clang folds it: the variable is not emitted
               for a read) */
            const semantic_node &object = node(n.a);
            if (object.kind == semantic_kind::entity) {
                const entity &x = analysis.entity_table[object.a];
                if (x.kind == entity_kind::variable && x.constexpr_ &&
                    x.definition && !(x.type.cv() & cv_volatile) &&
                    analysis.type_table[x.type.unqualified()].kind ==
                        type_kind::pointer &&
                    analysis.type_table[node(x.definition).type.unqualified()]
                            .kind == type_kind::pointer)
                    return expression(x.definition);
            }
        }
        return read(n.a);
    case conversion_kind::array_to_pointer: {
        uint32_t a = object(n.a);
        address_taken(a);
        return cast(a, t, AST_CAST_ARRAY_DECAY);
    }
    case conversion_kind::function_to_pointer:
        return cast(expression(n.a), t, AST_CAST_FUNCTION_DECAY);
    case conversion_kind::integral:
        return cast(expression(n.a), t, AST_CAST_INTEGER);
    case conversion_kind::floating:
        return cast(expression(n.a), t, AST_CAST_FLOAT_TO_FLOAT);
    case conversion_kind::floating_integral: {
        const struct type &to = record(n.type);
        bool to_floating = to.fundamental == fundamental_type::float_ ||
                           to.fundamental == fundamental_type::double_ ||
                           to.fundamental == fundamental_type::long_double ||
                           to.fundamental == fundamental_type::float16;
        if (to_floating)
            return cast(expression(n.a), t, AST_CAST_INTEGER_TO_FLOAT);
        return cast(expression(n.a), t, AST_CAST_FLOAT_TO_INTEGER);
    }
    case conversion_kind::boolean:
        if (record(node(n.a).type).kind == type_kind::pointer_to_member)
            return member_pointer_test(n.a, n.at);
        return cast(expression(n.a), t, AST_CAST_BOOL);
    case conversion_kind::null_member_pointer:
    case conversion_kind::member_pointer_to_derived:
    case conversion_kind::member_pointer_to_base:
        return member_pointer_conversion(n);
    case conversion_kind::member_pointer_qualification:
        return expression(n.a);
    case conversion_kind::null_pointer: {
        /* [conv.ptr]/1: "A null pointer constant can be converted to a
           pointer type; the result is the null pointer value of that
           type"; the constant has no effect to keep */
        uint32_t zero =
            ast_builder_integer(c, ast_builder_type_basic(AST_TYPE_INT), 0);
        return cast(zero, t, AST_CAST_NULL_POINTER);
    }
    case conversion_kind::pointer:
    case conversion_kind::qualification_cast:
    case conversion_kind::pointer_cast:
        return cast(expression(n.a), t, AST_CAST_POINTER_TO_POINTER);
    case conversion_kind::derived_to_base:
        if (analysis.entity_table[n.b].virtual_base) {
            /* [class.mi]/4: a virtual base, where the object's virtual
               table says ([abi:vtable-components] vbase offset); in a
               complete object constructor or destructor, *this is a
               complete object whose layout gives the offset */
            entity_index k = analysis.entity_table[n.b].enclosing;
            entity_index v = record(analysis.entity_table[n.b].type).entity;
            if ((structor_variant == 1 || structor_variant == 3) &&
                k == structor_class && at_this(n.a)) {
                uint32_t p = expression(n.a);
                if (!p)
                    return 0;
                return offset_pointer(
                    p, static_cast<int64_t>(analysis.entity_table[n.b].offset),
                    t);
            }
            /* the pointer read three times: a temporary holds one with
               side effects */
            reread r;
            uint32_t first =
                read_first(n.a, r,
                           "a pointer to a virtual base of an expression with "
                           "side effects outside a statement");
            if (!first)
                return 0;
            uint32_t second = read_again(r);
            uint32_t third = read_again(r);
            if (node(n.a).kind == semantic_kind::address)
                return virtual_base_address(first, second, k, v, t);
            uint32_t null = cast(
                ast_builder_integer(c, ast_builder_type_basic(AST_TYPE_INT), 0),
                t, AST_CAST_NULL_POINTER);
            return make(AST_NODE_CONDITIONAL, AST_QUALIFIED_TYPE_UNQUALIFIED(t),
                        truth(first),
                        virtual_base_address(second, third, k, v, t), null, 0,
                        0);
        }
        /* [conv.ptr]/3: a base at offset 0 (which analysis makes sure
           of): the same address, a null pointer one too */
        if (analysis.entity_table[n.b].offset)
            return null_checked_offset(
                n, static_cast<int64_t>(analysis.entity_table[n.b].offset), t);
        return cast(expression(n.a), t, AST_CAST_POINTER_TO_POINTER);
    case conversion_kind::base_to_derived:
        /* [expr.static.cast]/11: "the resulting pointer points to the
           enclosing object of type D": the base's offset subtracted */
        if (analysis.entity_table[n.b].offset)
            return null_checked_offset(
                n, -static_cast<int64_t>(analysis.entity_table[n.b].offset), t);
        return cast(expression(n.a), t, AST_CAST_POINTER_TO_POINTER);
    case conversion_kind::pointer_to_integral:
        return cast(expression(n.a), t, AST_CAST_POINTER_TO_INTEGER);
    case conversion_kind::bit_cast:
        return cast(expression(n.a), t, AST_CAST_BIT_CAST);
    case conversion_kind::vector_cast: {
        int kind = AST_CAST_VECTOR_BITS;
        if (record(n.type).kind == type_kind::vector)
            kind = AST_CAST_UNION;
        if (record(n.type).kind == type_kind::vector &&
            record(node(n.a).type).kind == type_kind::vector)
            kind = AST_CAST_NOP;
        return cast(expression(n.a), t, kind);
    }
    case conversion_kind::vector_convert:
        return ast_builder_convert_vector(c, t, expression(n.a));
    case conversion_kind::integral_to_pointer:
        return cast(expression(n.a), t, AST_CAST_INTEGER_TO_POINTER);
    case conversion_kind::to_void:
        return cast(discard(n.a), void_type, AST_CAST_VOID);
    }
    return fail("a conversion", n.at);
}

} // namespace cplusplus::lowering
