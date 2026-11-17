/*
 * lower.cpp: the semantic tree as C's AST (include/frontend/cplusplus/
 * lower.h; doc/PLAN-cxx-sema.md S1d), so that the code generator C uses
 * makes the translation unit's code.
 *
 * The semantic tree has every implicit step explicit, so each node maps to
 * the C node of the same operation; what C has no node for is said with
 * the ones it has:
 *
 *   - a reference is a pointer: a variable of reference type holds the
 *     address of the object it is bound to, a name of it is that address
 *     dereferenced, and binding it takes the address (a prvalue's
 *     temporary is a compound literal of the enclosing block);
 *   - a comparison, ! and the logical operators are int in C, so their
 *     value is converted to bool;
 *   - a condition that declares, and an init-statement, are blocks around
 *     the statement, as the standard's equivalences say;
 *   - a glvalue comma or conditional is the object whose address the C
 *     comma or conditional selects.
 *
 * The C AST takes the analysis's word for every rule: lowering checks
 * nothing.  A construct with no rule here fails the translation unit
 * (lowering_failure); nothing is guessed.
 */
#include "../cplusplus-internal.h"
#include "frontend/cplusplus/lower.h"

namespace cplusplus
{

namespace
{

class c_lowering
{
  public:
    c_lowering(const semantic_analysis &analysis, token_stream &tokens, cc *c,
               int target)
        : analysis(analysis), tokens(tokens), c(c), target(target)
    {
        symbols.resize(analysis.entity_table.size());
        for (uint32_t &s : symbols)
            s = 0;
    }

    bool translation_unit();

    lowering_failure failure;

  private:
    const semantic_analysis &analysis;
    token_stream &tokens;
    cc *c;
    int target;
    /* the C symbol of each entity, 0 until made */
    owned_array<uint32_t> symbols;
    /* the labels of the function being lowered: identifier, C label */
    struct label_record {
        uint32_t identifier = 0, label = 0;
    };
    owned_array<label_record> labels;
    /* lowering a function's body (else a namespace-scope initializer) */
    bool in_function = false;

    uint32_t fail(const char *what, token_index at)
    {
        if (!failure.what) {
            failure.what = what;
            failure.at = at;
        }
        return 0;
    }
    const struct type &record(qualified_type t) const
    {
        return analysis.type_table[t.unqualified()];
    }
    const semantic_node &node(uint32_t n) const
    {
        return analysis.semantics[n];
    }
    bool is_glvalue(const semantic_node &n) const
    {
        return n.category != value_category::prvalue;
    }

    /* types and symbols */
    ast_qualified_type type(qualified_type t, token_index at);
    ast_qualified_type fundamental(fundamental_type f, token_index at);
    bool is_reference(qualified_type t) const;
    entity_index enclosing_function(entity_index e) const;
    uint32_t symbol(entity_index e, token_index at);
    void spelling(entity_index e, owned_buffer &out);

    /* expressions */
    uint32_t make(int op, ast_qualified_type t, uint32_t a, uint32_t b,
                  uint32_t c_, uint16_t aux, uint8_t flags)
    {
        return ast_builder_node(c, op, t, a, b, c_, aux, flags);
    }
    uint32_t cast(uint32_t operand, ast_qualified_type to, int kind)
    {
        return make(AST_NODE_CAST, to, operand, 0, 0,
                    static_cast<uint16_t>(kind), 0);
    }
    ast_qualified_type type_of(uint32_t n);
    uint32_t to_bool(uint32_t int_value);
    uint32_t to_ptrdiff(uint32_t integer);
    uint32_t expression(uint32_t n);
    uint32_t read(uint32_t n);
    uint32_t discard(uint32_t n);
    uint32_t object(uint32_t n);
    uint32_t address_of(uint32_t lvalue, ast_qualified_type pointer);
    uint32_t constant(const semantic_node &n);
    uint32_t string_literal(const semantic_node &n);
    uint32_t conversion(const semantic_node &n);
    uint32_t binary(const semantic_node &n);
    uint32_t assignment(const semantic_node &n);
    uint32_t call(const semantic_node &n);
    uint32_t bind_reference(const semantic_node &n);
    uint32_t initializer_list(const semantic_node &n);
    void address_taken(uint32_t lvalue);

    /* statements */
    uint32_t statement(uint32_t n);
    void block_items(uint32_t first, uint32_t count,
                     owned_array<uint32_t> &out);
    uint32_t block(const uint32_t *items, uint32_t count);
    uint32_t declaration(const semantic_node &n);
    uint32_t condition(uint32_t n, uint32_t &declaration_statement);
    uint32_t label(uint32_t identifier);
    bool function_definition(entity_index e);
    bool variable_definition(entity_index e);
    bool constant_initializer(uint32_t n) const;
};

/* ---- types ---------------------------------------------------------- */

ast_qualified_type c_lowering::fundamental(fundamental_type f, token_index at)
{
    switch (f) {
    case fundamental_type::void_:
        return ast_builder_type_basic(AST_TYPE_VOID);
    case fundamental_type::bool_:
        return ast_builder_type_basic(AST_TYPE_BOOL);
    case fundamental_type::char_:
        return ast_builder_type_basic(AST_TYPE_CHAR);
    case fundamental_type::signed_char:
        return ast_builder_type_basic(AST_TYPE_SIGNED_CHAR);
    case fundamental_type::unsigned_char:
    case fundamental_type::char8_t_:
        return ast_builder_type_basic(AST_TYPE_UNSIGNED_CHAR);
    case fundamental_type::short_int:
        return ast_builder_type_basic(AST_TYPE_SHORT);
    case fundamental_type::unsigned_short_int:
    case fundamental_type::char16_t_:
        return ast_builder_type_basic(AST_TYPE_UNSIGNED_SHORT);
    case fundamental_type::int_:
        return ast_builder_type_basic(AST_TYPE_INT);
    case fundamental_type::unsigned_int:
    case fundamental_type::char32_t_:
        return ast_builder_type_basic(AST_TYPE_UNSIGNED_INT);
    case fundamental_type::wchar_t_:
        /* [impl.clang]: the target's wchar_t, as C's (unsigned on Linux
           arm64) */
        if (target == PP_LINUX_AARCH64)
            return ast_builder_type_basic(AST_TYPE_UNSIGNED_INT);
        return ast_builder_type_basic(AST_TYPE_INT);
    case fundamental_type::long_int:
        return ast_builder_type_basic(AST_TYPE_LONG);
    case fundamental_type::unsigned_long_int:
        return ast_builder_type_basic(AST_TYPE_UNSIGNED_LONG);
    case fundamental_type::long_long_int:
        return ast_builder_type_basic(AST_TYPE_LONG_LONG);
    case fundamental_type::unsigned_long_long_int:
        return ast_builder_type_basic(AST_TYPE_UNSIGNED_LONG_LONG);
    case fundamental_type::float_:
        return ast_builder_type_basic(AST_TYPE_FLOAT);
    case fundamental_type::double_:
        return ast_builder_type_basic(AST_TYPE_DOUBLE);
    case fundamental_type::long_double:
        return ast_builder_type_basic(AST_TYPE_LONG_DOUBLE);
    case fundamental_type::float16:
        return ast_builder_type_basic(AST_TYPE_FLOAT16);
    case fundamental_type::std_nullptr_t:
        /* [basic.fundamental]/14: "A value of type std::nullptr_t is a
           null pointer constant"; it has a pointer's size */
        return ast_builder_type_pointer(c,
                                        ast_builder_type_basic(AST_TYPE_VOID));
    default:
        fail("a bit-precise integer type", at);
        return ast_builder_type_basic(AST_TYPE_INT);
    }
}

bool c_lowering::is_reference(qualified_type t) const
{
    type_kind k = record(t).kind;
    return k == type_kind::lvalue_reference || k == type_kind::rvalue_reference;
}

/* the C type of a C++ type; a reference is a pointer to what it refers to */
ast_qualified_type c_lowering::type(qualified_type t, token_index at)
{
    const struct type &r = record(t);
    ast_qualified_type made = 0;
    switch (r.kind) {
    case type_kind::fundamental:
        made = fundamental(r.fundamental, at);
        break;
    case type_kind::pointer:
    case type_kind::lvalue_reference:
    case type_kind::rvalue_reference:
        made = ast_builder_type_pointer(c, type(r.operand, at));
        break;
    case type_kind::array:
        made = ast_builder_type_array(c, type(r.operand, at), r.bound,
                                      r.bound_known);
        break;
    case type_kind::function: {
        owned_array<ast_qualified_type> parameters;
        const qualified_type *p =
            analysis.type_table.parameters(t.unqualified());
        for (uint32_t i = 0; i < r.parameter_count; i++)
            parameters.append(type(p[i], at));
        made =
            ast_builder_type_function(c, type(r.operand, at), parameters.data(),
                                      r.parameter_count, r.variadic);
        break;
    }
    case type_kind::class_type:
        fail("a class type", at);
        return ast_builder_type_basic(AST_TYPE_INT);
    case type_kind::enumeration:
        fail("an enumeration type", at);
        return ast_builder_type_basic(AST_TYPE_INT);
    default:
        fail("a pointer to member type", at);
        return ast_builder_type_basic(AST_TYPE_INT);
    }
    /* the qualifiers are C's bits: const 1, volatile 2 */
    if (t.cv())
        made = ast_builder_type_qualified(c, made, static_cast<int>(t.cv()));
    return made;
}

/* ---- symbols -------------------------------------------------------- */

/* the function whose definition a block-scope entity is in, 0 if none */
entity_index c_lowering::enclosing_function(entity_index e) const
{
    const entities &table = analysis.entity_table;
    entity_index s = table[e].enclosing;
    while (s && table[s].kind == entity_kind::block_scope)
        s = table[s].enclosing;
    if (s && table[s].kind == entity_kind::function)
        return s;
    return 0;
}

void c_lowering::spelling(entity_index e, owned_buffer &out)
{
    uint32_t id = analysis.entity_table[e].name;
    if (!id)
        return;
    size_t length = 0;
    /* the identifier's number is the preprocessor's plus one */
    const char *s = tokens.pp.identifier_spelling(id - 1, &length);
    vbuffer_append(out.get(), s, length);
}

/* the C symbol of a function or variable, made at its first use: named by
   its symbol (itanium_symbol_name) if it has one, by its identifier if it
   is automatic */
uint32_t c_lowering::symbol(entity_index e, token_index at)
{
    if (symbols[e])
        return symbols[e];
    const entity &x = analysis.entity_table[e];
    if (x.kind != entity_kind::function && x.kind != entity_kind::variable)
        return fail("an entity that is not a function or variable", at);
    ast_qualified_type t = type(x.type, at);
    owned_buffer name;
    uint32_t s = 0;
    if (x.kind == entity_kind::variable && x.automatic) {
        spelling(e, name);
        s = ast_builder_symbol(c, AST_SYMBOL_VARIABLE, name.data(), name.size(),
                               t, AST_STORAGE_AUTO, 0, 1);
        symbols[e] = s;
        return s;
    }
    if (!itanium_symbol_name(analysis, tokens.pp, e, name.get()))
        return fail("an entity whose symbol is not mangled yet", at);
    int kind = AST_SYMBOL_VARIABLE;
    if (x.kind == entity_kind::function)
        kind = AST_SYMBOL_FUNCTION;
    if (enclosing_function(e)) {
        /* a local static: [basic.stc.static]/1, its symbol the mangled one
           (codegen writes an assembler name as given) */
        owned_buffer written;
        if (target == PP_DARWIN)
            vbuffer_string_append(written.get(), "_");
        vbuffer_append(written.get(), name.data(), name.size());
        owned_buffer identifier;
        spelling(e, identifier);
        s = ast_builder_symbol(c, kind, identifier.data(), identifier.size(), t,
                               AST_STORAGE_STATIC, 0, 1);
        ast_builder_symbol_assembler_name(c, s, written.data(), written.size());
        symbols[e] = s;
        return s;
    }
    int storage = AST_STORAGE_NONE;
    if (x.linkage_of_name == linkage::internal)
        storage = AST_STORAGE_STATIC;
    else if (!x.defined && kind == AST_SYMBOL_VARIABLE)
        storage = AST_STORAGE_EXTERN;
    uint32_t flags = AST_SYMBOL_FLAG_GLOBAL;
    /* [basic.def.odr]/11: an inline function or variable is defined in
       every translation unit that odr-uses it: emitted where referenced,
       weak so that the copies link as one ([abi:vague-inline] wants a
       COMDAT group too, which C's code generator does not make yet) */
    if (x.inline_)
        flags |= AST_SYMBOL_FLAG_LINKONCE | AST_SYMBOL_FLAG_WEAK;
    s = ast_builder_symbol(c, kind, name.data(), name.size(), t, storage, flags,
                           0);
    symbols[e] = s;
    ast_builder_toplevel(c, s);
    return s;
}

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

/* a glvalue as a C lvalue, failing when C has none for it (an assignment
   or increment used as an object) */
uint32_t c_lowering::object(uint32_t n)
{
    uint32_t made = expression(n);
    if (!made)
        return 0;
    const struct node &x = ast_builder_ast(c)->nodes[made];
    if (!(x.flags & AST_NODE_FLAG_LVALUE) &&
        record(node(n).type).kind != type_kind::function)
        return fail("an assignment or increment used as an object", node(n).at);
    return made;
}

/* [conv.lval]/3.4: the value of a glvalue: "the value contained in the
   object indicated by the glvalue is the prvalue result".  C reads an
   lvalue where it is used as a value; a comma's or conditional's glvalue
   is read in each operand, and an assignment's value is the one stored */
uint32_t c_lowering::read(uint32_t n)
{
    const semantic_node &x = node(n);
    ast_qualified_type t = type(x.type, x.at);
    if (x.kind == semantic_kind::comma)
        return make(AST_NODE_COMMA, AST_QUALIFIED_TYPE_UNQUALIFIED(t),
                    discard(x.a), read(x.b), 0, 0, 0);
    if (x.kind == semantic_kind::conditional)
        return make(AST_NODE_CONDITIONAL, AST_QUALIFIED_TYPE_UNQUALIFIED(t),
                    expression(x.a), read(x.b), read(x.c), 0, 0);
    return expression(n);
}

/* [expr.context]/2: a discarded-value expression, evaluated for its
   effects; a glvalue comma or conditional selects no object then */
uint32_t c_lowering::discard(uint32_t n)
{
    const semantic_node &x = node(n);
    if (x.kind == semantic_kind::comma) {
        uint32_t b = discard(x.b);
        return make(AST_NODE_COMMA, type_of(b), discard(x.a), b, 0, 0, 0);
    }
    if (x.kind == semantic_kind::conditional && is_glvalue(x))
        return make(AST_NODE_CONDITIONAL, ast_builder_type_basic(AST_TYPE_VOID),
                    expression(x.a),
                    cast(discard(x.b), ast_builder_type_basic(AST_TYPE_VOID),
                         AST_CAST_VOID),
                    cast(discard(x.c), ast_builder_type_basic(AST_TYPE_VOID),
                         AST_CAST_VOID),
                    0, 0);
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
    if (r.kind != type_kind::fundamental)
        return fail("a constant of this type", n.at);
    if (n.evaluation != constness::constant)
        return fail("a constant whose value analysis does not know", n.at);
    switch (r.fundamental) {
    case fundamental_type::float_:
    case fundamental_type::double_: {
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
    owned_array<uint8_t> bytes;
    uint32_t count = t.string_length + 1;
    bytes.resize(static_cast<size_t>(count) * size);
    for (uint32_t i = 0; i < count; i++) {
        uint32_t u = 0;
        if (i < t.string_length)
            u = units[t.string_offset + i];
        /* little-endian, as the targets are */
        for (uint8_t b = 0; b < size; b++)
            bytes[static_cast<size_t>(i) * size + b] =
                static_cast<uint8_t>(u >> (8 * b));
    }
    return ast_builder_string(c, type(element, n.at), bytes.data(), count, size,
                              kind);
}

uint32_t c_lowering::conversion(const semantic_node &n)
{
    ast_qualified_type t = AST_QUALIFIED_TYPE_UNQUALIFIED(type(n.type, n.at));
    switch (static_cast<conversion_kind>(n.op)) {
    case conversion_kind::lvalue_to_rvalue:
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
        return cast(expression(n.a), t, AST_CAST_BOOL);
    case conversion_kind::null_pointer: {
        /* [conv.ptr]/1: "A null pointer constant can be converted to a
           pointer type; the result is the null pointer value of that
           type"; the constant has no effect to keep */
        uint32_t zero =
            ast_builder_integer(c, ast_builder_type_basic(AST_TYPE_INT), 0);
        return cast(zero, t, AST_CAST_NULL_POINTER);
    }
    case conversion_kind::pointer:
        return cast(expression(n.a), t, AST_CAST_POINTER_TO_POINTER);
    case conversion_kind::to_void:
        return cast(discard(n.a), ast_builder_type_basic(AST_TYPE_VOID),
                    AST_CAST_VOID);
    }
    return fail("a conversion", n.at);
}

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

uint32_t c_lowering::binary(const semantic_node &n)
{
    int op = c_operator(static_cast<preprocessing_op_or_punc>(n.op));
    if (!op)
        return fail("a binary operator", n.at);
    uint32_t a = expression(n.a), b = expression(n.b);
    if (!a || !b)
        return 0;
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

uint32_t c_lowering::assignment(const semantic_node &n)
{
    uint32_t a = object(n.a);
    uint32_t b = expression(n.b);
    if (!a || !b)
        return 0;
    ast_qualified_type t = AST_QUALIFIED_TYPE_UNQUALIFIED(type(n.type, n.at));
    if (preprocessing_op_or_punc_primary(static_cast<preprocessing_op_or_punc>(
            n.op)) == preprocessing_op_or_punc::EQUAL)
        return make(AST_NODE_ASSIGN, t, a, b, 0, 0, 0);
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
uint32_t c_lowering::call(const semantic_node &n)
{
    uint32_t callee = expression(n.a);
    if (!callee)
        return 0;
    qualified_type function_type = node(n.a).type;
    if (record(function_type).kind == type_kind::pointer)
        function_type = record(function_type).operand;
    else
        callee = cast(callee, ast_builder_type_pointer(c, type_of(callee)),
                      AST_CAST_FUNCTION_DECAY);
    owned_array<uint32_t> arguments;
    const uint32_t *items = analysis.semantics.items(n.first);
    for (uint32_t i = 0; i < n.count; i++) {
        uint32_t a = expression(items[i]);
        if (!a)
            return 0;
        arguments.append(a);
    }
    qualified_type result = record(function_type).operand;
    ast_qualified_type t = AST_QUALIFIED_TYPE_UNQUALIFIED(type(result, n.at));
    uint32_t made =
        make(AST_NODE_CALL, t, callee,
             ast_builder_list(c, arguments.data(), n.count), n.count, 0, 0);
    if (!is_reference(result))
        return made;
    return make(AST_NODE_DEREFERENCE, type(n.type, n.at), made, 0, 0, 0,
                AST_NODE_FLAG_LVALUE);
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
    uint32_t value = expression(n.a);
    if (!value)
        return 0;
    ast_qualified_type t = type(bound.type, bound.at);
    uint32_t temporary =
        ast_builder_symbol(c, AST_SYMBOL_VARIABLE, "", 0, t, AST_STORAGE_AUTO,
                           AST_SYMBOL_FLAG_ADDRESS_TAKEN, 1);
    ast_builder_symbol_body(c, temporary, value);
    uint32_t literal = make(AST_NODE_COMPOUND_LITERAL, t, value, temporary, 0,
                            0, AST_NODE_FLAG_LVALUE);
    return make(AST_NODE_ADDRESS, pointer, literal, 0, 0, 0, 0);
}

/* [dcl.init.aggr]: an array's elements in order, the rest zero */
uint32_t c_lowering::initializer_list(const semantic_node &n)
{
    owned_array<uint32_t> runs;
    const uint32_t *items = analysis.semantics.items(n.first);
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

uint32_t c_lowering::expression(uint32_t index)
{
    if (failure.what)
        return 0;
    const semantic_node &n = node(index);
    switch (n.kind) {
    case semantic_kind::constant:
        return constant(n);
    case semantic_kind::string_literal:
        return string_literal(n);
    case semantic_kind::entity: {
        entity_index e = n.a;
        const entity &x = analysis.entity_table[e];
        uint32_t s = symbol(e, n.at);
        if (!s)
            return 0;
        ast_builder_symbol_flags_add(c, s, AST_SYMBOL_FLAG_USED);
        if (x.kind == entity_kind::function)
            return make(AST_NODE_VARIABLE, type(x.type, n.at), s, 0, 0, 0, 0);
        ast_qualified_type t = type(x.type, n.at);
        uint32_t v =
            make(AST_NODE_VARIABLE, t, s, 0, 0, 0, AST_NODE_FLAG_LVALUE);
        if (!is_reference(x.type))
            return v;
        /* [dcl.ref]: a name of a reference is the object it refers to */
        uint8_t flags = AST_NODE_FLAG_LVALUE;
        if (record(n.type).kind == type_kind::function)
            flags = 0;
        return make(AST_NODE_DEREFERENCE, type(n.type, n.at), v, 0, 0, 0,
                    flags);
    }
    case semantic_kind::conversion:
        return conversion(n);
    case semantic_kind::unary: {
        uint32_t a = expression(n.a);
        if (!a)
            return 0;
        int op = c_operator(static_cast<preprocessing_op_or_punc>(n.op));
        if (op == LEX_TOKEN_EXCLAMATION)
            return to_bool(make(AST_NODE_UNARY,
                                ast_builder_type_basic(AST_TYPE_INT), a, 0, 0,
                                LEX_TOKEN_EXCLAMATION, 0));
        if (!op)
            return fail("a unary operator", n.at);
        return make(AST_NODE_UNARY,
                    AST_QUALIFIED_TYPE_UNQUALIFIED(type(n.type, n.at)), a, 0, 0,
                    static_cast<uint16_t>(op), 0);
    }
    case semantic_kind::address: {
        uint32_t a = object(n.a);
        if (!a)
            return 0;
        return address_of(a, type(n.type, n.at));
    }
    case semantic_kind::indirection: {
        uint32_t a = expression(n.a);
        if (!a)
            return 0;
        uint8_t flags = AST_NODE_FLAG_LVALUE;
        if (record(n.type).kind == type_kind::function)
            flags = 0;
        return make(AST_NODE_DEREFERENCE, type(n.type, n.at), a, 0, 0, 0,
                    flags);
    }
    case semantic_kind::increment: {
        uint32_t a = object(n.a);
        if (!a)
            return 0;
        int op = AST_NODE_PRE_INCREMENT;
        switch (static_cast<increment_kind>(n.op)) {
        case increment_kind::pre_increment:
            op = AST_NODE_PRE_INCREMENT;
            break;
        case increment_kind::pre_decrement:
            op = AST_NODE_PRE_DECREMENT;
            break;
        case increment_kind::post_increment:
            op = AST_NODE_POST_INCREMENT;
            break;
        case increment_kind::post_decrement:
            op = AST_NODE_POST_DECREMENT;
            break;
        }
        return make(op, AST_QUALIFIED_TYPE_UNQUALIFIED(type_of(a)), a, 0, 0, 0,
                    0);
    }
    case semantic_kind::binary:
        return binary(n);
    case semantic_kind::logical_and:
    case semantic_kind::logical_or: {
        uint32_t a = expression(n.a), b = expression(n.b);
        if (!a || !b)
            return 0;
        int op = AST_NODE_LOGICAL_AND;
        if (n.kind == semantic_kind::logical_or)
            op = AST_NODE_LOGICAL_OR;
        return to_bool(
            make(op, ast_builder_type_basic(AST_TYPE_INT), a, b, 0, 0, 0));
    }
    case semantic_kind::assignment:
        return assignment(n);
    case semantic_kind::conditional: {
        ast_qualified_type t = type(n.type, n.at);
        if (!is_glvalue(n))
            return make(AST_NODE_CONDITIONAL, AST_QUALIFIED_TYPE_UNQUALIFIED(t),
                        expression(n.a), expression(n.b), expression(n.c), 0,
                        0);
        /* the object one of the operands is: its address selected */
        ast_qualified_type pointer = ast_builder_type_pointer(c, t);
        uint32_t b = object(n.b), c_ = object(n.c);
        if (!b || !c_)
            return 0;
        uint32_t selected =
            make(AST_NODE_CONDITIONAL, pointer, expression(n.a),
                 address_of(b, pointer), address_of(c_, pointer), 0, 0);
        return make(AST_NODE_DEREFERENCE, t, selected, 0, 0, 0,
                    AST_NODE_FLAG_LVALUE);
    }
    case semantic_kind::comma: {
        ast_qualified_type t = type(n.type, n.at);
        uint32_t a = discard(n.a);
        if (!is_glvalue(n))
            return make(AST_NODE_COMMA, AST_QUALIFIED_TYPE_UNQUALIFIED(t), a,
                        expression(n.b), 0, 0, 0);
        ast_qualified_type pointer = ast_builder_type_pointer(c, t);
        uint32_t b = object(n.b);
        if (!b)
            return 0;
        uint32_t selected =
            make(AST_NODE_COMMA, pointer, a, address_of(b, pointer), 0, 0, 0);
        return make(AST_NODE_DEREFERENCE, t, selected, 0, 0, 0,
                    AST_NODE_FLAG_LVALUE);
    }
    case semantic_kind::call:
        return call(n);
    case semantic_kind::subscript: {
        uint32_t a = expression(n.a), b = expression(n.b);
        if (!a || !b)
            return 0;
        return make(AST_NODE_INDEX, type(n.type, n.at), a, to_ptrdiff(b), 0, 0,
                    AST_NODE_FLAG_LVALUE);
    }
    case semantic_kind::zero:
        return make(AST_NODE_ZERO, type(n.type, n.at), 0, 0, 0, 0, 0);
    case semantic_kind::initializer_list:
        return initializer_list(n);
    case semantic_kind::bind_reference:
        return bind_reference(n);
    default:
        return fail("an expression of this kind", n.at);
    }
}

/* ---- statements ----------------------------------------------------- */

uint32_t c_lowering::block(const uint32_t *items, uint32_t count)
{
    return ast_builder_node(c, AST_NODE_BLOCK,
                            ast_builder_type_basic(AST_TYPE_VOID), 0,
                            ast_builder_list(c, items, count), count, 0, 0);
}

/* a block's statements; a declaration of several variables is one
   statement per variable, in the block's scope */
void c_lowering::block_items(uint32_t first, uint32_t count,
                             owned_array<uint32_t> &out)
{
    const uint32_t *items = analysis.semantics.items(first);
    for (uint32_t i = 0; i < count; i++) {
        const semantic_node &s = node(items[i]);
        if (s.kind == semantic_kind::declarations) {
            block_items(s.first, s.count, out);
            continue;
        }
        out.append(statement(items[i]));
    }
}

/* whether an initializer is a constant C emits as data */
bool c_lowering::constant_initializer(uint32_t n) const
{
    const semantic_node &x = node(n);
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
    case semantic_kind::conversion:
        return x.evaluation == constness::constant &&
               static_cast<conversion_kind>(x.op) !=
                   conversion_kind::lvalue_to_rvalue;
    default:
        return x.evaluation == constness::constant;
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
    if (n.b) {
        if (!x.automatic && !constant_initializer(n.b))
            return fail("a static local with dynamic initialization", n.at);
        init = expression(n.b);
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
        return ast_builder_node(c, AST_NODE_EXPRESSION,
                                ast_builder_type_basic(AST_TYPE_VOID), 0, 0, 0,
                                0, 0);
    }
    return ast_builder_node(c, AST_NODE_DECLARATION,
                            ast_builder_type_basic(AST_TYPE_VOID), s, init, 0,
                            0, 0);
}

/* a condition's value; a condition that declares gives its declaration in
   declaration_statement (0 if none) */
uint32_t c_lowering::condition(uint32_t n, uint32_t &declaration_statement)
{
    declaration_statement = 0;
    const semantic_node &x = node(n);
    if (x.kind != semantic_kind::condition_declaration)
        return expression(n);
    declaration_statement = declaration(node(x.a));
    return expression(x.b);
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

uint32_t c_lowering::statement(uint32_t index)
{
    if (failure.what || !index)
        return 0;
    const semantic_node &n = node(index);
    ast_qualified_type v = ast_builder_type_basic(AST_TYPE_VOID);
    switch (n.kind) {
    case semantic_kind::block: {
        owned_array<uint32_t> items;
        block_items(n.first, n.count, items);
        return block(items.data(), static_cast<uint32_t>(items.size()));
    }
    case semantic_kind::declarations: {
        owned_array<uint32_t> items;
        block_items(n.first, n.count, items);
        return block(items.data(), static_cast<uint32_t>(items.size()));
    }
    case semantic_kind::expression_statement:
        return ast_builder_node(c, AST_NODE_EXPRESSION, v, discard(n.a), 0, 0,
                                0, 0);
    case semantic_kind::null_statement:
        return ast_builder_node(c, AST_NODE_EXPRESSION, v, 0, 0, 0, 0, 0);
    case semantic_kind::declaration_statement:
        return declaration(n);
    case semantic_kind::if_statement: {
        /* [stmt.if]/3: an init-statement is the statement before the if, in
           a block around both; a condition that declares likewise
           ([stmt.select]: its scope is the if statement) */
        uint32_t declared = 0;
        uint32_t init = statement(n.d);
        uint32_t cond = condition(n.a, declared);
        uint32_t made = ast_builder_node(c, AST_NODE_IF, v, cond,
                                         statement(n.b), statement(n.c), 0, 0);
        uint32_t items[3];
        uint32_t count = 0;
        if (init)
            items[count++] = init;
        if (declared)
            items[count++] = declared;
        if (!count)
            return made;
        items[count++] = made;
        return block(items, count);
    }
    case semantic_kind::while_statement: {
        uint32_t declared = 0;
        uint32_t cond = condition(n.a, declared);
        uint32_t body = statement(n.b);
        if (!declared)
            return ast_builder_node(c, AST_NODE_WHILE, v, cond, body, 0, 0, 0);
        /* [stmt.while]/2: with a declaration the loop is
               label: { if (condition) { statement goto label; } }
           here a for without a condition whose body declares, tests and
           leaves */
        uint32_t leave = ast_builder_node(c, AST_NODE_BREAK, v, 0, 0, 0, 0, 0);
        uint32_t test =
            ast_builder_node(c, AST_NODE_IF, v, cond, body, leave, 0, 0);
        uint32_t items[2] = {declared, test};
        uint32_t step_body[2] = {0, block(items, 2)};
        return ast_builder_node(c, AST_NODE_FOR, v, 0, 0,
                                ast_builder_list(c, step_body, 2), 0, 0);
    }
    case semantic_kind::do_statement: {
        uint32_t body = statement(n.a);
        return ast_builder_node(c, AST_NODE_DO, v, expression(n.b), body, 0, 0,
                                0);
    }
    case semantic_kind::for_statement: {
        uint32_t init = 0;
        if (n.a) {
            const semantic_node &first = node(n.a);
            if (first.kind == semantic_kind::declaration_statement ||
                first.kind == semantic_kind::declarations) {
                owned_array<uint32_t> items;
                if (first.kind == semantic_kind::declarations)
                    block_items(first.first, first.count, items);
                else
                    items.append(declaration(first));
                init = block(items.data(), static_cast<uint32_t>(items.size()));
            } else if (first.kind != semantic_kind::null_statement)
                init = statement(n.a);
        }
        uint32_t declared = 0, cond = 0;
        if (n.b)
            cond = condition(n.b, declared);
        uint32_t step = 0;
        if (n.c)
            step = discard(n.c);
        uint32_t body = statement(n.d);
        if (declared) {
            uint32_t leave =
                ast_builder_node(c, AST_NODE_BREAK, v, 0, 0, 0, 0, 0);
            uint32_t test =
                ast_builder_node(c, AST_NODE_IF, v, cond, body, leave, 0, 0);
            uint32_t items[2] = {declared, test};
            body = block(items, 2);
            cond = 0;
        }
        uint32_t step_body[2] = {step, body};
        return ast_builder_node(c, AST_NODE_FOR, v, init, cond,
                                ast_builder_list(c, step_body, 2), 0, 0);
    }
    case semantic_kind::switch_statement: {
        /* [stmt.switch]/7: an init-statement is before the switch, in a
           block around both */
        uint32_t declared = 0;
        uint32_t init = statement(n.d);
        uint32_t cond = condition(n.a, declared);
        uint32_t made = ast_builder_node(c, AST_NODE_SWITCH, v, cond,
                                         statement(n.b), 0, 0, 0);
        uint32_t items[3];
        uint32_t count = 0;
        if (init)
            items[count++] = init;
        if (declared)
            items[count++] = declared;
        if (!count)
            return made;
        items[count++] = made;
        return block(items, count);
    }
    case semantic_kind::case_label:
        return ast_builder_case(c, static_cast<int64_t>(n.value),
                                statement(n.a));
    case semantic_kind::default_label:
        return ast_builder_node(c, AST_NODE_DEFAULT, v, 0, 0, statement(n.a), 0,
                                0);
    case semantic_kind::identifier_label: {
        uint32_t l = label(static_cast<uint32_t>(n.value));
        return ast_builder_label_define(c, l, statement(n.a));
    }
    case semantic_kind::goto_statement:
        return ast_builder_node(c, AST_NODE_GOTO, v,
                                label(static_cast<uint32_t>(n.value)), 0, 0, 0,
                                0);
    case semantic_kind::break_statement:
        return ast_builder_node(c, AST_NODE_BREAK, v, 0, 0, 0, 0, 0);
    case semantic_kind::continue_statement:
        return ast_builder_node(c, AST_NODE_CONTINUE, v, 0, 0, 0, 0, 0);
    case semantic_kind::return_statement: {
        uint32_t value = 0;
        if (n.a) {
            value = expression(n.a);
            if (!value)
                return 0;
        }
        return ast_builder_node(c, AST_NODE_RETURN, v, value, 0, 0, 0, 0);
    }
    default:
        return fail("a statement of this kind", n.at);
    }
}

/* ---- definitions ---------------------------------------------------- */

bool c_lowering::function_definition(entity_index e)
{
    const entity &x = analysis.entity_table[e];
    const semantic_node &body = node(x.definition);
    uint32_t s = symbol(e, body.at);
    if (!s)
        return false;
    labels.clear();
    in_function = true;
    /* every parameter has a symbol, an unnamed one too */
    owned_array<uint32_t> parameters;
    const struct type &f = record(x.type);
    const qualified_type *types =
        analysis.type_table.parameters(x.type.unqualified());
    const uint32_t *items = analysis.semantics.items(body.first);
    for (uint32_t i = 0; i < body.count; i++) {
        uint32_t p = 0;
        if (items[i])
            p = symbol(items[i], body.at);
        else if (i < f.parameter_count)
            p = ast_builder_symbol(c, AST_SYMBOL_VARIABLE, "", 0,
                                   type(types[i], body.at), AST_STORAGE_AUTO, 0,
                                   1);
        if (!p)
            return fail("a parameter", body.at);
        ast_builder_symbol_flags_add(c, p, AST_SYMBOL_FLAG_PARAMETER);
        parameters.append(p);
    }
    uint32_t block_node = statement(body.a);
    in_function = false;
    if (failure.what)
        return false;
    struct node &b = ast_builder_ast(c)->nodes[block_node];
    b.a = ast_builder_list(c, parameters.data(), body.count);
    b.aux = static_cast<uint16_t>(body.count);
    ast_builder_symbol_body(c, s, block_node);
    ast_builder_symbol_flags_add(c, s, AST_SYMBOL_FLAG_DEFINED);
    return true;
}

bool c_lowering::variable_definition(entity_index e)
{
    const entity &x = analysis.entity_table[e];
    uint32_t s = symbol(e, 0);
    if (!s)
        return false;
    if (x.definition) {
        /* [basic.start.static]/2: constant initialization; anything else is
           dynamic initialization, which needs code before main */
        if (!constant_initializer(x.definition))
            return fail("dynamic initialization of a variable",
                        node(x.definition).at);
        uint32_t init = expression(x.definition);
        if (!init)
            return false;
        ast_builder_symbol_body(c, s, init);
    }
    ast_builder_symbol_flags_add(c, s, AST_SYMBOL_FLAG_DEFINED);
    return true;
}

bool c_lowering::translation_unit()
{
    for (const analysed_declaration &d : analysis.declarations)
        if (d.result != analysis_result::well_formed) {
            token_index at = analysis.syntax()[d.node].first_token;
            if (d.result == analysis_result::unsupported &&
                d.first_unsupported) {
                const unsupported_construct &u =
                    analysis.unsupported[d.first_unsupported];
                failure.subclause = u.subclause;
                at = analysis.syntax()[u.node].first_token;
            }
            if (d.result == analysis_result::not_parsed)
                fail("a declaration the parser kept as tokens", at);
            else
                fail("a declaration that is not well-formed", at);
            return false;
        }
    /* the namespace-scope functions and variables, in the order they are
       first declared */
    for (const declared_name &d : analysis.declared) {
        if (!d.entity)
            continue;
        const entity &x = analysis.entity_table[d.entity];
        if (x.kind != entity_kind::function && x.kind != entity_kind::variable)
            continue;
        if (x.automatic || enclosing_function(d.entity))
            continue;
        if (!symbol(d.entity, 0))
            return false;
    }
    owned_array<entity_index> done;
    for (const declared_name &d : analysis.declared) {
        if (!d.entity)
            continue;
        const entity &x = analysis.entity_table[d.entity];
        if (!x.defined || x.automatic || enclosing_function(d.entity))
            continue;
        bool again = false;
        for (entity_index s : done)
            if (s == d.entity)
                again = true;
        if (again)
            continue;
        done.append(d.entity);
        if (x.kind == entity_kind::function) {
            if (!function_definition(d.entity))
                return false;
        } else if (x.kind == entity_kind::variable) {
            if (!variable_definition(d.entity))
                return false;
        }
    }
    return !failure.what;
}

} // namespace

bool lower_translation_unit(const semantic_analysis &analysis,
                            token_stream &tokens, cc *c, int target,
                            lowering_failure &why)
{
    c_lowering lowering(analysis, tokens, c, target);
    bool lowered = lowering.translation_unit();
    why = lowering.failure;
    return lowered;
}

} // namespace cplusplus
