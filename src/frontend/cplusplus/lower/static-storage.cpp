/* static-storage.cpp: static storage: zero, dynamic and guarded initialization, destruction at exit (lower-internal.h) */
#include "lower-internal.h"

namespace cplusplus::lowering
{

/*
 * [basic.start.static]/2: "If constant initialization is not performed, a
 * variable with static storage duration ... is zero-initialized", which
 * for "a scalar type" is "the value obtained by converting the integer
 * literal 0 to T" ([dcl.init]/6.1): the null member pointer value, which
 * [abi:data-member-pointers] represents as -1, not C's zero bits.  A
 * pointer to data member without an initializer gets it; one inside a
 * class or array that is not constant-initialized is not lowered.
 */
bool c_lowering::static_zero_initialization(const entity &x, uint32_t s,
                                            token_index at)
{
    if (!holds_data_member_pointer(x.type))
        return true;
    if (x.definition && constant_initializer(x.definition))
        return true;
    if (!x.definition && record(x.type).kind == type_kind::pointer_to_member) {
        ast_builder_symbol_body(c, s, null_member_pointer(x.type, at));
        return true;
    }
    fail("a pointer to data member zero-initialized as part of an object", at);
    return false;
}

/* a function void name(void) of internal linkage, named as written (its
   body to come) */
uint32_t c_lowering::generated_function(const char *base)
{
    bool used = false;
    for (const char *n : generated_names)
        used = used || !strcmp(n, base);
    owned_buffer name;
    vbuffer_string_append(name.get(), base);
    if (used)
        vbuffer_format_append(name.get(), ".%u", ++generated_suffix);
    else
        generated_names.append(base);
    return local_function(name.data(), name.size());
}

uint32_t c_lowering::local_function(const char *name, size_t length)
{
    ast_qualified_type f = ast_builder_type_function(c, v, nullptr, 0, 0);
    uint32_t s = ast_builder_symbol(c, AST_SYMBOL_FUNCTION, name, length, f,
                                    AST_STORAGE_STATIC, 0, 0);
    owned_buffer written;
    if (target == PP_DARWIN)
        vbuffer_string_append(written.get(), "_");
    vbuffer_append(written.get(), name, length);
    ast_builder_symbol_assembler_name(c, s, written.data(), written.size());
    ast_builder_toplevel(c, s);
    return s;
}

/* the initialization of variable e of static storage duration by
   initializer init, as an expression: a constructor's call or an
   assignment */
uint32_t c_lowering::initialization(entity_index e, uint32_t init,
                                    token_index at)
{
    uint32_t s = symbol(e, at);
    if (!s)
        return 0;
    qualified_type t = analysis.entity_table[e].type;
    return initialize_into(s, t, type(t, at), init);
}

/* [intro.execution]/5.3: the initialization of a static variable, a
   full-expression whose temporaries are made and destroyed with it */
void c_lowering::static_initialization(entity_index e, uint32_t init,
                                       owned_array<uint32_t> &out,
                                       token_index at)
{
    collection k = collect();
    owned_array<uint32_t> after;
    uint32_t done = initialization(e, init, at);
    collected(k, out, after);
    if (!done)
        return;
    out.append(c_statement(c, done));
    for (uint32_t a : after)
        out.append(a);
}

/* a call of the run time's function name (declared on first use, in
   slot) */
uint32_t c_lowering::runtime_call(uint32_t &slot, const char *name,
                                  ast_qualified_type result,
                                  const ast_qualified_type *parameters,
                                  const uint32_t *arguments, uint32_t count)
{
    ast_qualified_type ft =
        ast_builder_type_function(c, result, parameters, count, false);
    if (!slot) {
        slot =
            ast_builder_symbol(c, AST_SYMBOL_FUNCTION, name, strlen(name), ft,
                               AST_STORAGE_NONE, AST_SYMBOL_FLAG_GLOBAL, 0);
        ast_builder_toplevel(c, slot);
    }
    ast_builder_symbol_flags_add(c, slot, AST_SYMBOL_FLAG_USED);
    uint32_t callee = cast(make(AST_NODE_VARIABLE, ft, slot, 0, 0, 0, 0),
                           pointer_to(ft), AST_CAST_FUNCTION_DECAY);
    owned_array<uint32_t> all;
    for (uint32_t i = 0; i < count; i++) {
        if (!arguments[i])
            return 0;
        ast_qualified_type a = type_of(arguments[i]);
        if (a == parameters[i])
            all.append(arguments[i]);
        else
            all.append(
                cast(arguments[i], parameters[i], AST_CAST_POINTER_TO_POINTER));
    }
    return make(AST_NODE_CALL, result, callee,
                ast_builder_list(c, all.data(), count), count, 0, 0);
}

/* [basic.start.term]/3, [abi:dso-dtor]: "__cxa_atexit(f, p, d) ... f(p)
   is called when the shared object d is unloaded": the complete object
   destructor of the object at address object, registered for
   __dso_handle's module, as clang does */
void c_lowering::static_destruction(entity_index e, owned_array<uint32_t> &out,
                                    token_index at)
{
    const entity &x = analysis.entity_table[e];
    qualified_type object;
    entity_index destructor = element_destructor(x.type, object);
    if (!destructor)
        return;
    uint32_t s = symbol(e, at);
    if (!s)
        return;
    ast_qualified_type t = type(x.type, at);
    uint64_t count = element_count(x.type, object);
    uint32_t registered = 0;
    if (!count) {
        registered = destructor_registration(
            special_callee(destructor, at), entity_type(destructor, at),
            address_of(c_variable(c, s, t), pointer_to(t)));
    } else {
        /* [class.dtor]/13, [impl.clang]: __cxx_global_array_dtor (then
           .1, ...), destroying the elements in reverse order */
        uint32_t f = generated_function("__cxx_global_array_dtor");
        uint32_t loop = destruction(s, x.type, at);
        if (!loop)
            return;
        ast_builder_symbol_body(c, f, block(&loop, 1));
        ast_builder_symbol_flags_add(c, f, AST_SYMBOL_FLAG_DEFINED);
        registered = destructor_registration(
            f, ast_builder_type_function(c, v, nullptr, 0, 0),
            cast(ast_builder_integer(c, ast_builder_type_basic(AST_TYPE_LONG),
                                     0),
                 pointer_to(v), AST_CAST_INTEGER_TO_POINTER));
    }
    if (registered)
        out.append(c_statement(c, registered));
}

uint32_t c_lowering::destructor_registration(uint32_t d, ast_qualified_type f,
                                             uint32_t object)
{
    if (!d || !object)
        return 0;
    ast_builder_symbol_flags_add(c, d, AST_SYMBOL_FLAG_ADDRESS_TAKEN);
    ast_qualified_type pointer = pointer_to(v);
    ast_qualified_type dt = ast_builder_type_function(c, v, &pointer, 1, false);
    ast_qualified_type byte = ast_builder_type_basic(AST_TYPE_CHAR);
    if (!dso_handle) {
        dso_handle =
            ast_builder_symbol(c, AST_SYMBOL_VARIABLE, "__dso_handle", 12, byte,
                               AST_STORAGE_EXTERN, AST_SYMBOL_FLAG_GLOBAL, 0);
        ast_builder_toplevel(c, dso_handle);
    }
    ast_builder_symbol_flags_add(c, dso_handle, AST_SYMBOL_FLAG_USED);
    uint32_t function = cast(make(AST_NODE_VARIABLE, f, d, 0, 0, 0, 0),
                             pointer_to(f), AST_CAST_FUNCTION_DECAY);
    uint32_t handle =
        address_of(c_variable(c, dso_handle, byte), pointer_to(byte));
    ast_qualified_type parameters[3] = {pointer_to(dt), pointer, pointer};
    uint32_t arguments[3] = {function, object, handle};
    return runtime_call(cxa_atexit, "__cxa_atexit",
                        ast_builder_type_basic(AST_TYPE_INT), parameters,
                        arguments, 3);
}

/*
 * [stmt.dcl]/4: "Dynamic initialization of a block-scope variable with
 * static storage duration ... is performed the first time control passes
 * through its declaration ... If control enters the declaration
 * concurrently while the variable is being initialized, the concurrent
 * execution shall wait for completion of the initialization."
 * [abi:guards]: a 64-bit guard variable _ZGV<the variable's mangled name
 * without _Z>, its first byte read with acquire semantics, then
 * __cxa_guard_acquire, the initialization, the destructor's registration
 * and __cxa_guard_release, as clang does at -O0.  A constant-initialized
 * object with a destructor is guarded for its registration alone.
 */
void c_lowering::guarded_initialization(const semantic_node &n,
                                        entity_index destructor,
                                        owned_array<uint32_t> &out)
{
    entity_index e = n.a;
    entity_index function = enclosing_function(e);
    if (!function || analysis.entity_table[function].inline_) {
        /* [abi:guards]: the guard and the variable are COMDAT with the
           inline function, which C's code generator does not make yet */
        fail("a guarded local static in an inline function", n.at);
        return;
    }
    bool constant = !n.b || constant_initializer(n.b);
    if (!constant && node(n.b).kind == semantic_kind::initializer_list &&
        record(analysis.entity_table[e].type).kind == type_kind::array) {
        fail("dynamic initialization of an array", n.at);
        return;
    }
    uint32_t s = symbol(e, n.at);
    if (!s)
        return;
    uint32_t data = 0;
    if (n.b && constant) {
        data = initializer(n.b);
        if (!data)
            return;
    }
    ast_builder_symbol_flags_add(c, s, AST_SYMBOL_FLAG_DEFINED);
    ast_builder_symbol_keep(c, s);
    if (data)
        ast_builder_symbol_body(c, s, data);
    owned_buffer name;
    if (!itanium_symbol_name(analysis, tokens.pp, e, name.get(), 0) ||
        name.size() < 2) {
        fail("an entity whose symbol is not mangled", n.at);
        return;
    }
    owned_buffer guard_name;
    vbuffer_string_append(guard_name.get(), "_ZGV");
    vbuffer_append(guard_name.get(), name.data() + 2, name.size() - 2);
    owned_buffer written;
    if (target == PP_DARWIN)
        vbuffer_string_append(written.get(), "_");
    vbuffer_append(written.get(), guard_name.data(), guard_name.size());
    ast_qualified_type i64 =
        ast_builder_type_basic(AST_TYPE_UNSIGNED_LONG_LONG);
    ast_qualified_type i64p = pointer_to(i64);
    ast_qualified_type int_type = ast_builder_type_basic(AST_TYPE_INT);
    uint32_t guard =
        ast_builder_symbol(c, AST_SYMBOL_VARIABLE, guard_name.data(),
                           guard_name.size(), i64, AST_STORAGE_STATIC, 0, 1);
    ast_builder_symbol_assembler_name(c, guard, written.data(), written.size());
    ast_builder_symbol_flags_add(c, guard, AST_SYMBOL_FLAG_DEFINED);
    ast_builder_symbol_keep(c, guard);
    auto guard_address = [&]() {
        return address_of(c_variable(c, guard, i64), i64p);
    };
    owned_array<uint32_t> inner;
    if (!constant)
        static_initialization(e, n.b, inner, n.at);
    if (destructor)
        static_destruction(e, inner, n.at);
    if (failure.what)
        return;
    uint32_t argument = guard_address();
    uint32_t release = runtime_call(guard_release, "__cxa_guard_release", v,
                                    &i64p, &argument, 1);
    inner.append(c_statement(c, release));
    argument = guard_address();
    uint32_t acquire = runtime_call(guard_acquire, "__cxa_guard_acquire",
                                    int_type, &i64p, &argument, 1);
    uint32_t acquired = if_statement(make(AST_NODE_BINARY, int_type, acquire,
                                          ast_builder_integer(c, int_type, 0),
                                          0, LEX_TOKEN_EXCLAMATION_EQUAL, 0),
                                     block(inner), 0);
    /* the guard's first byte, read with acquire semantics
       (__atomic_load_n(p, __ATOMIC_ACQUIRE)) */
    ast_qualified_type byte = ast_builder_type_basic(AST_TYPE_UNSIGNED_CHAR);
    uint32_t loaded = ast_builder_atomic_load(
        c, byte,
        cast(guard_address(), pointer_to(byte), AST_CAST_POINTER_TO_POINTER),
        ast_builder_integer(c, int_type, 2));
    uint32_t test = make(
        AST_NODE_BINARY, int_type, cast(loaded, int_type, AST_CAST_INTEGER),
        ast_builder_integer(c, int_type, 0), 0, LEX_TOKEN_EQUAL_EQUAL, 0);
    out.append(if_statement(test, acquired, 0));
}

/*
 * [basic.start.dynamic]/2: "Dynamic initialization of non-block variables
 * ... Within a single translation unit, the initialization of ... variables
 * with ordered initialization is sequenced before ... initialization of
 * the variables defined later in the translation unit."  [impl.clang]: one
 * internal function per variable, __cxx_global_var_init (then .1, .2,
 * ...), called in order by _GLOBAL__sub_I_<the source file's name>, which
 * .init_array runs before main ([basic.start.dynamic]/5 lets it).
 */
/* the function initializing the static variable e (or, only_destroy,
   registering its destruction), its call appended to initializers */
bool c_lowering::global_initializer(entity_index e, bool only_destroy)
{
    const entity &x = analysis.entity_table[e];
    const semantic_node &init = node(x.definition);
    if (!only_destroy && init.kind == semantic_kind::initializer_list &&
        record(x.type).kind == type_kind::array)
        /* an array's elements, some not constant: each stored */
        return fail("dynamic initialization of an array", init.at);
    uint32_t f = generated_function("__cxx_global_var_init");
    in_function = true;
    labels.clear();
    live.clear();
    owned_array<uint32_t> statements;
    if (!only_destroy) {
        static_owner = e;
        static_root = x.definition;
        static_initialization(e, x.definition, statements, init.at);
        static_owner = 0;
        static_root = 0;
    }
    /* [basic.start.term]/3: registered with __cxa_atexit once
       constructed ([abi:dso-dtor]) */
    if (!failure.what)
        static_destruction(e, statements, init.at);
    in_function = false;
    if (failure.what || statements.empty())
        return false;
    ast_builder_symbol_body(c, f, block(statements));
    ast_builder_symbol_flags_add(
        c, f, AST_SYMBOL_FLAG_DEFINED | AST_SYMBOL_FLAG_USED);
    ast_qualified_type ft = ast_builder_type_function(c, v, nullptr, 0, 0);
    uint32_t callee = cast(make(AST_NODE_VARIABLE, ft, f, 0, 0, 0, 0),
                           pointer_to(ft), AST_CAST_FUNCTION_DECAY);
    initializers.append(
        c_statement(c, make(AST_NODE_CALL, v, callee, 0, 0, 0, 0)));
    return true;
}

bool c_lowering::dynamic_initialization()
{
    owned_buffer name;
    vbuffer_string_append(name.get(), "_GLOBAL__sub_I_");
    const char *base = main_path;
    if (!base)
        base = "";
    for (const char *p = base; *p; p++)
        if (*p == '/')
            base = p + 1;
    /* [impl.clang]: the file's name with each character that cannot
       continue a pp-number ([lex.ppnumber]: a letter, a digit, _ or .)
       replaced by _ */
    for (const char *p = base; *p; p++) {
        char k = *p;
        if (!((k >= 'a' && k <= 'z') || (k >= 'A' && k <= 'Z') ||
              (k >= '0' && k <= '9') || k == '_' || k == '.'))
            k = '_';
        vbuffer_append(name.get(), &k, 1);
    }
    uint32_t start = local_function(name.data(), name.size());
    ast_builder_symbol_body(c, start, block(initializers));
    ast_builder_symbol_flags_add(c, start, AST_SYMBOL_FLAG_DEFINED);
    ast_builder_symbol_constructor(c, start);
    return true;
}

} // namespace cplusplus::lowering
