/*
 * library-builtins.cpp: [ext:gnu_library_builtins] GCC's __builtin_ forms
 * of ISO C library functions (GCC manual 6.63, Other Built-in Functions
 * Provided by GCC: "GCC includes built-in versions of many of the
 * functions in the standard C library.  These functions come in two
 * forms: one whose names start with the __builtin_ prefix, and the other
 * without.  Both forms have the same type (including prototype), the same
 * address (when their address is taken), and the same meaning as the C
 * library functions").  clang declares one implicitly at its first use,
 * with its C library prototype, and calls the library function where it
 * does not expand it; so does analysis: an unqualified name lookup finds
 * nothing for __builtin_NAME, NAME in the table below, and the function
 * is declared in the global namespace with C language linkage ([dcl.link])
 * under the library's name, which is its symbol.
 */
#include "../../cplusplus-internal.h"
#include "common/softfloat.h"
#include "frontend/c/ast-builder.h"
#include "frontend/cplusplus/builtin-registry.h"
#include "frontend/cplusplus/sema.h"

namespace cplusplus
{

/* a pointer to a fundamental type with the cv-qualifiers on the pointee */
qualified_type semantic_analysis::builtin_pointer(fundamental_type f,
                                                  uint32_t pointee_cv)
{
    qualified_type q =
        qualified_type::make(type_table.fundamental(f), pointee_cv);
    return qualified_type::make(type_table.pointer_to(q), cv_none);
}

qualified_type semantic_analysis::builtin_scalar(fundamental_type f)
{
    return qualified_type::make(type_table.fundamental(f), cv_none);
}

/* the unsigned integer type of the signed one f */
static fundamental_type unsigned_integer(fundamental_type f)
{
    switch (f) {
    case fundamental_type::int_:
        return fundamental_type::unsigned_int;
    case fundamental_type::long_int:
        return fundamental_type::unsigned_long_int;
    case fundamental_type::long_long_int:
        return fundamental_type::unsigned_long_long_int;
    default:
        return fundamental_type::unsigned_int128;
    }
}

/*
 * One type of a library builtin's prototype at *p, in Builtins.def's
 * encoding (clang's Builtins.td; frontend/c/libbuiltins.inc, which
 * gen-libbuiltins.py makes from it): the prefixes L (long), S (signed),
 * U (unsigned), I (a required constant), X (complex); the type letters v b
 * c s i f d, z (size_t), Y (ptrdiff_t), w (wchar_t), P (FILE, as void), W
 * (int64_t), Z (int32_t); the suffixes * (pointer), C, D (const, volatile)
 * and R (restrict, no meaning here).  False for a type this front end has
 * no type for (__float128, __int128 pointers are read).
 */
bool semantic_analysis::library_type(const char *&p, qualified_type &t)
{
    int longs = 0;
    bool is_signed = false, is_unsigned = false, complex = false;
    for (;; p++) {
        if (*p == 'L')
            longs++;
        else if (*p == 'S')
            is_signed = true;
        else if (*p == 'U')
            is_unsigned = true;
        else if (*p == 'X')
            complex = true;
        else if (*p != 'I')
            break;
    }
    fundamental_type f = fundamental_type::none;
    switch (*p++) {
    case 'v':
        f = fundamental_type::void_;
        break;
    case 'b':
        f = fundamental_type::bool_;
        break;
    case 'c':
        f = fundamental_type::char_;
        if (is_unsigned)
            f = fundamental_type::unsigned_char;
        else if (is_signed)
            f = fundamental_type::signed_char;
        break;
    case 's':
        f = fundamental_type::short_int;
        if (is_unsigned)
            f = fundamental_type::unsigned_short_int;
        break;
    case 'i':
        if (longs == 0)
            f = fundamental_type::int_;
        else if (longs == 1)
            f = fundamental_type::long_int;
        else if (longs == 2)
            f = fundamental_type::long_long_int;
        else
            f = fundamental_type::int128;
        if (is_unsigned)
            f = unsigned_integer(f);
        longs = 0;
        break;
    case 'f':
        f = fundamental_type::float_;
        break;
    case 'd':
        if (longs > 1)
            return false;
        f = fundamental_type::double_;
        if (longs)
            f = fundamental_type::long_double;
        longs = 0;
        break;
    case 'z':
        /* [support.types.layout]/3: "The type size_t is an
           implementation-defined unsigned integer type", sizeof's */
        f = fundamental_type::unsigned_long_int;
        break;
    case 'Y':
        f = fundamental_type::long_int;
        break;
    case 'w':
        f = fundamental_type::wchar_t_;
        break;
    case 'P':
        f = fundamental_type::void_;
        break;
    case 'W':
        f = fundamental_type::long_int;
        break;
    case 'Z':
        f = fundamental_type::int_;
        break;
    default:
        return false;
    }
    if (longs)
        return false;
    t = builtin_scalar(f);
    if (complex)
        t = qualified_type::make(type_table.complex_of(t), cv_none);
    for (;; p++) {
        if (*p == '*') {
            t = qualified_type::make(type_table.pointer_to(t), cv_none);
            /* (an address space) */
            while (p[1] >= '0' && p[1] <= '9')
                p++;
        } else if (*p == 'C' || *p == 'D') {
            uint32_t cv = cv_volatile;
            if (*p == 'C')
                cv = cv_const;
            t = qualified_type::make(t.unqualified(), t.cv() | cv);
        } else if (*p != 'R') {
            break;
        }
    }
    return true;
}

/* [ext:clang_builtin_functions] clang's builtin functions that are not C
   library functions (__builtin_coro_resume, __builtin_launder, ...) all
   have names starting with __builtin_, which [lex.name]/3 reserves: no
   program declares a type with one, so an unqualified one is a function's
   name, and `__builtin_coro_resume(h);` is an expression statement
   ([stmt.ambig]/1) and not a declaration of h */
static bool names_clang_builtin_function(const char *spelling, size_t length)
{
    static const char prefix[] = "__builtin_";
    return length > sizeof prefix - 1 &&
           !memcmp(spelling, prefix, sizeof prefix - 1);
}

/* name is a builtin of vcc's C front end, which clang declares at its
   first use (a function's name to the parser), or any other clang builtin
   function */
bool parser::names_compiler_builtin(uint32_t name)
{
    size_t length = 0;
    /* the identifier's number is the preprocessor's plus one */
    const char *spelling = tokens.pp.identifier_spelling(name - 1, &length);
    if (ast_builder_builtin_signature(spelling, length) != nullptr)
        return true;
    return names_clang_builtin_function(spelling, length);
}

/* the function name names, declared at its first use with C language
   linkage and the GNU flags given */
entity_index semantic_analysis::declare_builtin(
    uint32_t name, uint32_t symbol, qualified_type result,
    const qualified_type *parameters, size_t count, bool variadic,
    uint32_t flags, bool non_throwing)
{
    entity_index e = entity_table.make(entity_kind::function, symbol, 0);
    entity_table[e].type = qualified_type::make(
        type_table.function(result, parameters, count, variadic, non_throwing),
        cv_none);
    entity_table[e].linkage_of_name = linkage::external;
    entity_table[e].c_language_linkage = true;
    entity_table[e].gnu_flags = flags;
    entity_table.bind(0, name, e);
    return e;
}

/* the scalar letters of the C front end's builtin signatures and their
   types */
static const char signature_letters[] = "vbcChiulLwWzfdD";
static const fundamental_type signature_types[] = {
    fundamental_type::void_,
    fundamental_type::bool_,
    fundamental_type::char_,
    fundamental_type::unsigned_char,
    fundamental_type::unsigned_short_int,
    fundamental_type::int_,
    fundamental_type::unsigned_int,
    fundamental_type::long_int,
    fundamental_type::unsigned_long_int,
    fundamental_type::long_long_int,
    fundamental_type::unsigned_long_long_int,
    fundamental_type::unsigned_long_int,
    fundamental_type::float_,
    fundamental_type::double_,
    fundamental_type::long_double,
};

/* the type of a letter of the C front end's builtin signatures (frontend/c/
   builtin-table.cpp, cc_builtin_signature_type): a scalar, or a pointer to
   void or char (p, P, s, S); not a vector (F, B, H) or a va_list (V, M) */
bool semantic_analysis::builtin_letter_type(char letter, qualified_type &t)
{
    switch (letter) {
    case 'p':
        t = builtin_pointer(fundamental_type::void_, cv_none);
        return true;
    case 'P':
        t = builtin_pointer(fundamental_type::void_, cv_const);
        return true;
    case 's':
        t = builtin_pointer(fundamental_type::char_, cv_none);
        return true;
    case 'S':
        t = builtin_pointer(fundamental_type::char_, cv_const);
        return true;
    default:
        break;
    }
    const char *at = strchr(signature_letters, letter);
    if (!letter || !at)
        return false;
    t = builtin_scalar(signature_types[at - signature_letters]);
    return true;
}

/* a builtin of vcc's C front end (frontend/c/builtin-table.cpp): its
   function type is the table's signature when the registry has no row of
   another form, and the call lowers to the C front end's builtin node
   (gnu_compiler_builtin); the form of the registry's row says how a call
   with arguments of any type is checked.  A signature with a parameter
   this front end has no type for (a vector, a va_list) is declared as a
   function of the form `unsupported`: a call is refused naming it. */
entity_index semantic_analysis::compiler_builtin(uint32_t name,
                                                 const char *spelling,
                                                 size_t length)
{
    const char *signature = ast_builder_builtin_signature(spelling, length);
    uint16_t row = 0;
    const gnu_builtin *registered = gnu_builtin_find(spelling, length, &row);
    if (!signature && !registered)
        return 0;
    /* (a builtin of this front end alone has its own prototype) */
    if (registered && registered->proto)
        signature = registered->proto;
    if (registered && registered->form == builtin_form::library)
        return 0;
    qualified_type void_type = qualified_type::make(
        type_table.fundamental(fundamental_type::void_), cv_none);
    uint32_t flags = gnu_compiler_builtin;
    entity_index e = 0;
    builtin_form form = builtin_form::signature;
    if (registered)
        form = registered->form;
    /* a function taking anything: the form checks each call */
    if (form == builtin_form::query || form == builtin_form::va_list ||
        form == builtin_form::overflow || form == builtin_form::atomic ||
        form == builtin_form::bit_count || form == builtin_form::generic ||
        form == builtin_form::launder || form == builtin_form::bswapg) {
        qualified_type result = void_type;
        /* (the type-generic ones have the signature's result type) */
        if (form == builtin_form::generic || form == builtin_form::bit_count)
            builtin_letter_type(signature[0], result);
        e = declare_builtin(name, name, result, nullptr, 0, true, flags);
    } else if (form == builtin_form::floating_constant) {
        qualified_type result;
        builtin_letter_type(signature[0], result);
        qualified_type parameter =
            builtin_pointer(fundamental_type::char_, cv_const);
        e = declare_builtin(name, name, result, &parameter, signature[1] == 'S',
                            false, flags);
    } else {
        owned_array<qualified_type> types_of;
        bool known = signature != nullptr;
        bool variadic = false;
        for (const char *s = signature; known && *s; s++) {
            qualified_type t;
            if (*s == '.') {
                variadic = s != signature;
                known = variadic && !s[1];
                break;
            }
            /* a parameter of type void is no scalar */
            if (!builtin_letter_type(*s, t) || (s != signature && *s == 'v'))
                known = false;
            else
                types_of.append(t);
        }
        if (known && types_of.size() && form == builtin_form::library_alias) {
            /* the library function's own symbol, noexcept as clang's
               declaration of a library function */
            uint32_t symbol = tokens.pp.identifier(registered->symbol,
                                                   strlen(registered->symbol)) +
                              1;
            e = declare_builtin(name, symbol, types_of[0], types_of.data() + 1,
                                types_of.size() - 1, variadic,
                                gnu_library_function, true);
            entity_table[e].gnu_builtin_row = row;
            return e;
        }
        if (known && types_of.size()) {
            e = declare_builtin(name, name, types_of[0], types_of.data() + 1,
                                types_of.size() - 1, variadic, flags);
        } else {
            form = builtin_form::unsupported;
            e = declare_builtin(name, name, void_type, nullptr, 0, true, flags);
        }
    }
    if (form == builtin_form::unsupported)
        row = gnu_builtin_unsupported_row;
    entity_table[e].gnu_builtin_row = row;
    return e;
}

bool semantic_analysis::builtin_spelled(uint32_t name) const
{
    size_t length = 0;
    const char *spelling = tokens.pp.identifier_spelling(name - 1, &length);
    return length > 10 && !memcmp(spelling, "__builtin_", 10);
}

/*
 * The C library function __builtin_NAME declares at its first use, as clang
 * does (GCC manual 6.63): the prototype is the C front end's table's, or
 * clang's Builtins.td (frontend/c/libbuiltins.inc), its symbol the
 * library's name, and it is noexcept in C++ (the declaration <math.h> and
 * <string.h> write with __THROW is of the same type).  A prototype this
 * front end has no types for is declared as a function of the form
 * `unsupported`.
 */
entity_index semantic_analysis::library_function_builtin(uint32_t name,
                                                         const char *spelling,
                                                         size_t length)
{
    const char *signature = nullptr, *symbol = nullptr;
    bool clang_encoding = false;
    if (!ast_builder_library_builtin(spelling + 10, length - 10, &signature,
                                     &clang_encoding, &symbol))
        return 0;
    owned_array<qualified_type> types_of;
    bool variadic = false, known = true;
    const char *p = signature;
    while (known && *p) {
        qualified_type t;
        if (*p == '.') {
            variadic = true;
            break;
        }
        if (clang_encoding) {
            known = library_type(p, t);
        } else {
            known = builtin_letter_type(*p, t);
            p++;
        }
        if (known)
            types_of.append(t);
    }
    if (!known || !types_of.size()) {
        entity_index e =
            declare_builtin(name, name, builtin_scalar(fundamental_type::void_),
                            nullptr, 0, true, gnu_compiler_builtin);
        entity_table[e].gnu_builtin_row = gnu_builtin_unsupported_row;
        return e;
    }
    uint32_t library = 0;
    if (symbol)
        library = tokens.pp.identifier(symbol, strlen(symbol)) + 1;
    else
        library = tokens.pp.identifier(spelling + 10, length - 10) + 1;
    uint32_t flags = gnu_library_function;
    /* (C17 7.22.4.1, 7.22.4.4, 7.22.4.5, 7.22.4.7) */
    static const char *const noreturn_functions[] = {"abort", "exit", "_Exit",
                                                     "quick_exit"};
    for (const char *f : noreturn_functions)
        if (strlen(f) == length - 10 && !memcmp(f, spelling + 10, length - 10))
            flags |= gnu_noreturn;
    /* bound under its __builtin_ name: the program's own declaration of the
       library function is another */
    entity_index e =
        declare_builtin(name, library, types_of[0], types_of.data() + 1,
                        types_of.size() - 1, variadic, flags, true);
    /* (a row of the registry says how a constant expression folds it) */
    uint16_t row = 0;
    if (gnu_builtin_find(spelling, length, &row))
        entity_table[e].gnu_builtin_row = row;
    return e;
}

entity_index semantic_analysis::library_builtin(uint32_t name)
{
    if (!tokens.files.behavior.has(extension::gnu_library_builtins))
        return 0;
    size_t length = 0;
    /* the identifier's number is the preprocessor's plus one */
    const char *spelling = tokens.pp.identifier_spelling(name - 1, &length);
    if (length <= 10 || memcmp(spelling, "__builtin_", 10))
        return compiler_builtin(name, spelling, length);
    /* clang's bool __builtin_is_constant_evaluated(void) (Builtins.td):
       [meta.const.eval]/1's std::is_constant_evaluated, true "if and only
       if evaluation of the call occurs within the evaluation of an
       expression or conversion that is manifestly constant-evaluated"; no
       library function: the evaluator answers true, lowering false */
    static const char evaluated[] = "__builtin_is_constant_evaluated";
    if (length == sizeof evaluated - 1 && !memcmp(spelling, evaluated, length))
        return declare_builtin(name, name,
                               builtin_scalar(fundamental_type::bool_), nullptr,
                               0, false, gnu_is_constant_evaluated);
    if (entity_index e = compiler_builtin(name, spelling, length))
        return e;
    return library_function_builtin(name, spelling, length);
}

/* whether the spelling has the text in it */
static bool spelled_with(const char *spelling, size_t length, const char *text)
{
    return memmem(spelling, length, text, strlen(text)) != nullptr;
}

/*
 * [ext:gnu_library_builtins] __builtin_va_start (ap, last), __builtin_va_end
 * (ap) and __builtin_va_copy (dest, src) (GCC manual 6.55; clang's Sema's
 * SemaBuiltinVAStart, SemaBuiltinVACopy): "void" calls whose va_list
 * arguments are lvalues of type __builtin_va_list (the type the AAPCS64
 * record is).  __builtin_c23_va_start takes the last parameter's name
 * too, or alone.  A call in a function that is not
 * variadic is ill-formed, as in clang.
 */
analysis_result
semantic_analysis::va_list_builtin_result(SyntaxRef n, entity_index called,
                                          int which,
                                          const owned_array<uint32_t> &nodes)
{
    size_t expected = 1;
    if (which == 4)
        expected = 2;
    bool right = nodes.size() == expected;
    if (which == 1)
        right = nodes.size() == 2;
    if (which == 2)
        right = nodes.size() == 1 || nodes.size() == 2;
    if (which == 5)
        return not_supported_builtin(n, "ext.gnu", called,
                                     "is the Microsoft va_list builtin, which "
                                     "has no lowering for this target");
    if (!right)
        return ill_formed(n,
                          "a va_list builtin takes the arguments its form does",
                          "ext.gnu");
    /* clang: "'va_start' used in function with fixed args" */
    if ((which == 1 || which == 2) && function_state.active &&
        function_state.function &&
        type_table[entity_table[function_state.function].type.unqualified()]
                .kind == type_kind::function &&
        !type_table[entity_table[function_state.function].type.unqualified()]
             .variadic)
        return ill_formed(n, "'va_start' used in function with fixed args",
                          "ext.gnu");
    size_t lists = 1;
    if (which == 4)
        lists = 2;
    for (size_t i = 0; i < lists; i++) {
        /* (the object the lvalue-to-rvalue conversion of the argument
           reads: its cv-qualification is the argument's) */
        uint32_t object = nodes[i];
        while (semantics[object].kind == semantic_kind::conversion &&
               static_cast<conversion_kind>(semantics[object].op) ==
                   conversion_kind::lvalue_to_rvalue)
            object = semantics[object].a;
        qualified_type t = semantics[object].type;
        /* (a template's own va_list is dependent: analysed when it is
           instantiated) */
        if (depends_on_template_parameter(t))
            return not_supported(n, "temp.dep.constexpr");
        /* (the lvalue-to-rvalue conversion argument passing applied is
           the lowering's no-op: the node under it is the object) */
        if (fundamental_of(t) != fundamental_type::builtin_va_list)
            return ill_formed(n,
                              "argument is of type that "
                              "is not 'va_list'",
                              "ext.gnu");
        /* "binding reference of type '__builtin_va_list' to value of type
           'const __builtin_va_list' drops 'const' qualifier" */
        bool written = which != 4 || i == 0;
        if (written && (t.cv() & cv_const))
            return ill_formed(n,
                              "a va_list argument that is written shall not "
                              "be const",
                              "ext.gnu");
    }
    return analysis_result::well_formed;
}

/*
 * [ext:gnu_library_builtins] __builtin_add_overflow (a, b, &r) and the
 * subtraction and multiplication ones (GCC manual 6.56, Integer Overflow
 * Builtins; clang's SemaBuiltinOverflow): "These built-in functions promote
 * the first two operands into infinite precision signed type and perform
 * addition on those promoted operands.  The result is then cast to the type
 * the pointer argument points to and stored there.  If the stored result is
 * equal to the infinite precision result, the built-in functions return
 * false, otherwise they return true."  The operands are of integer type
 * (bool and enumerations too, as clang accepts), the result a pointer to a
 * non-const integer type, other than bool.  The typed forms (__builtin_saddl_overflow, ...)
 * take one type for the operands and the result, int, long, long long or
 * their unsigned ones; the operands are converted to it when lowered.
 */
analysis_result semantic_analysis::overflow_builtin_result(
    SyntaxRef n, entity_index called, const char *spelling, size_t length,
    const owned_array<uint32_t> &nodes, qualified_type &result)
{
    if (nodes.size() != 3)
        return ill_formed(n,
                          "an overflow builtin takes two operands and a "
                          "pointer to the result",
                          "ext.gnu");
    const char *signature = ast_builder_builtin_signature(spelling, length);
    fundamental_type typed = fundamental_type::none;
    if (signature && signature[1] != '.')
        typed = signature_types[strchr(signature_letters, signature[1]) -
                                signature_letters];
    for (size_t i = 0; i < 2; i++) {
        qualified_type t = semantics[nodes[i]].type;
        fundamental_type f = fundamental_of(t);
        if (f == fundamental_type::none && !enumeration_of(t))
            return not_supported_builtin(
                n, "ext.gnu", called,
                "with an operand of a type that is neither an integer nor an "
                "enumeration");
        if (!enumeration_of(t) && !fundamental_type_is_integral(f)) {
            /* the typed forms convert any arithmetic operand */
            if (typed == fundamental_type::none)
                return ill_formed(n,
                                  "operand argument to overflow builtin must "
                                  "be an integer",
                                  "ext.gnu");
            return not_supported_builtin(
                n, "ext.gnu", called,
                "typed form with an operand that is not an integer");
        }
    }
    qualified_type third = semantics[nodes[2]].type;
    if (!is_pointer(third))
        return ill_formed(n,
                          "result argument to overflow builtin must be a "
                          "pointer to a non-const integer type",
                          "ext.gnu");
    qualified_type pointee = type_table[third.unqualified()].operand;
    fundamental_type f = fundamental_of(pointee);
    if (f == fundamental_type::none && !enumeration_of(pointee))
        return not_supported_builtin(n, "ext.gnu", called,
                                     "with a result of a type that is neither "
                                     "an integer nor an enumeration");
    if ((pointee.cv() & cv_const) ||
        (!enumeration_of(pointee) && !fundamental_type_is_integral(f)))
        return ill_formed(n,
                          "result argument to overflow builtin must be a "
                          "pointer to a non-const integer type",
                          "ext.gnu");
    /* (a bool result: the code generator compares the stored byte with the
       exact value, clang the one bit: not supported) */
    if (f == fundamental_type::bool_)
        return not_supported_builtin(n, "ext.gnu", called,
                                     "with a bool result");
    if (typed != fundamental_type::none && f != typed)
        return ill_formed(n,
                          "the result argument of a typed overflow builtin "
                          "points to its type",
                          "ext.gnu");
    result = qualified_type::make(
        type_table.fundamental(fundamental_type::bool_), cv_none);
    return analysis_result::well_formed;
}

/*
 * [ext:gnu_library_builtins] a call of an __atomic_*, __sync_* or
 * __c11_atomic_* builtin (GCC manual 6.55, 6.56; clang's Builtins.td, Sema's
 * SemaAtomicOpsOverloaded): "The first argument is a pointer to an object
 * of integral or pointer type of 1, 2, 4 or 8 bytes" (the generic forms any
 * scalar one); the call's type is by the builtin (cc_builtin_atomic_shape)
 * void, bool or the type the first argument points to without its
 * cv-qualifiers; the value arguments are converted to it when lowered.
 * What these rules do not cover is not supported.
 */
analysis_result
semantic_analysis::atomic_builtin_result(SyntaxRef n, entity_index called,
                                         const owned_array<uint32_t> &nodes,
                                         qualified_type &result)
{
    size_t length = 0;
    const char *spelling =
        tokens.pp.identifier_spelling(entity_table[called].name - 1, &length);
    const gnu_builtin &row =
        gnu_builtin_at(entity_table[called].gnu_builtin_row);
    switch (row.form) {
    case builtin_form::query:
        return query_builtin_result(n, called, row.which, nodes, result);
    case builtin_form::va_list: {
        VTRY(r, va_list_builtin_result(n, called, row.which, nodes));
        result = qualified_type::make(
            type_table.fundamental(fundamental_type::void_), cv_none);
        return analysis_result::well_formed;
    }
    case builtin_form::overflow:
        return overflow_builtin_result(n, called, spelling, length, nodes,
                                       result);
    case builtin_form::launder:
        return launder_result(n, called, nodes, result);
    case builtin_form::bswapg:
        return bswapg_result(n, called, nodes, result);
    case builtin_form::atomic:
        break;
    default:
        return not_supported_builtin(
            n, "ext.gnu", called,
            "has a signature whose parameter types are not supported");
    }
    int written = 0, kind = 0;
    if (!ast_builder_atomic_shape(spelling, length, &written, &kind))
        return not_supported_builtin(n, "ext.gnu", called,
                                     "is not an atomic builtin of the C "
                                     "front end");
    if (nodes.size() != static_cast<size_t>(written))
        return ill_formed(n,
                          "an atomic builtin takes the arguments its form "
                          "does",
                          "ext.gnu");
    qualified_type pointee;
    if (written > 0) {
        qualified_type first = semantics[nodes[0]].type;
        if (!is_pointer(first))
            return ill_formed(n,
                              "address argument to atomic builtin must be a "
                              "pointer",
                              "ext.gnu");
        pointee = qualified_type::make(
            type_table[first.unqualified()].operand.unqualified(), cv_none);
        /* [ext:clang_atomic] clang's SemaAtomicOpsOverloaded: a
           __c11_atomic builtin takes a pointer to an atomic object (the
           macros of clang's and vcc's <stdatomic.h> pass &a) and operates
           on its value type; the GNU forms take no atomic object */
        bool atomic_object =
            type_table[pointee.unqualified()].kind == type_kind::atomic;
        bool c11 = spelled_with(spelling, length, "__c11_");
        if (c11 && !atomic_object)
            return ill_formed(n,
                              "address argument to atomic operation must be "
                              "a pointer to _Atomic type",
                              "ext.clang");
        if (!c11 && atomic_object)
            return ill_formed(n,
                              "address argument to atomic operation must be "
                              "a pointer to integer, pointer or supported "
                              "floating point type",
                              "ext.clang");
        if (atomic_object) {
            type_index value = atomic_value_type(pointee);
            if (!value)
                return not_supported(n, "ext.clang");
            pointee = qualified_type::make(value, cv_none);
        }
        fundamental_type f = fundamental_of(pointee);
        bool pointer = is_pointer(pointee);
        if (!pointer &&
            (f == fundamental_type::none || f == fundamental_type::void_ ||
             f == fundamental_type::std_nullptr_t ||
             fundamental_type_width(tokens.files.behavior, f) > 64))
            return not_supported_builtin(
                n, "ext.gnu", called,
                "on an object that is not an integer, a pointer or a floating "
                "type of at most 64 bits");
        /* the operations that compute: an integer (a pointer for the
           additive ones) */
        bool computes = spelled_with(spelling, length, "fetch");
        bool selecting = spelled_with(spelling, length, "max") ||
                         spelled_with(spelling, length, "min") ||
                         spelled_with(spelling, length, "nand");
        if (computes && ((pointer && selecting) ||
                         (!pointer && (!fundamental_type_is_integral(f) ||
                                       f == fundamental_type::bool_))))
            return not_supported_builtin(
                n, "ext.gnu", called,
                "with an operation its operand type does not take");
        /* the arguments that are pointers to the object's type: the
           generic __atomic_load, __atomic_store, __atomic_exchange and
           __atomic_compare_exchange take them in place of values */
        size_t pointers = 0;
        bool generic = !spelled_with(spelling, length, "_n") &&
                       !spelled_with(spelling, length, "__c11_") &&
                       !spelled_with(spelling, length, "__sync_");
        if (spelled_with(spelling, length, "compare_exchange")) {
            pointers = 1;
            if (generic)
                pointers = 2;
        } else if (generic && (spelled_with(spelling, length, "load") ||
                               spelled_with(spelling, length, "store")))
            pointers = 1;
        else if (generic && spelled_with(spelling, length, "exchange"))
            pointers = 2;
        for (size_t i = 1; i < nodes.size(); i++) {
            qualified_type t = semantics[nodes[i]].type;
            if (i <= pointers && !is_pointer(t))
                return ill_formed(n,
                                  "an argument of an atomic builtin must be "
                                  "a pointer",
                                  "ext.gnu");
            /* (an unscoped enumeration converts to an integer: the
               memory_order of <stdatomic.h>) */
            if (fundamental_of(t) == fundamental_type::none && !is_pointer(t) &&
                !enumeration_of(t))
                return not_supported_builtin(
                    n, "ext.gnu", called,
                    "with an argument that is not an arithmetic, enumeration "
                    "or pointer value");
        }
    }
    switch (kind) {
    case 1:
        result = qualified_type::make(
            type_table.fundamental(fundamental_type::bool_), cv_none);
        break;
    case 2:
        result = pointee;
        break;
    case 3:
        result = qualified_type::make(
            type_table.fundamental(fundamental_type::int_), cv_none);
        break;
    case 4:
        result = qualified_type::make(
            type_table.fundamental(fundamental_type::unsigned_int), cv_none);
        break;
    default:
        result = qualified_type::make(
            type_table.fundamental(fundamental_type::void_), cv_none);
        break;
    }
    return analysis_result::well_formed;
}

/*
 * [ext:clang_builtin_functions] T *__builtin_launder(T *) (clang's
 * Builtins.td, custom type checking): "std::launder" of [ptr.launder], the
 * pointer to the object that is in the storage the argument points to;
 * clang rejects an argument that is not a pointer to an object type (a
 * function pointer, a void pointer).  The call has the argument's type.
 */
analysis_result
semantic_analysis::launder_result(SyntaxRef n, entity_index called,
                                  const owned_array<uint32_t> &nodes,
                                  qualified_type &result)
{
    if (nodes.size() != 1)
        return ill_formed(n, "__builtin_launder takes one argument",
                          "ext.clang");
    qualified_type t = semantics[nodes[0]].type;
    if (depends_on_template_parameter(t))
        return not_supported(n, "temp.dep.constexpr");
    if (!is_pointer(t))
        return ill_formed(n,
                          "the argument of __builtin_launder shall be a "
                          "pointer to an object type",
                          "ext.clang");
    qualified_type pointee = type_table[t.unqualified()].operand;
    if (type_table[pointee.unqualified()].kind == type_kind::function ||
        fundamental_of(pointee) == fundamental_type::void_)
        return ill_formed(n,
                          "the argument of __builtin_launder shall be a "
                          "pointer to an object type",
                          "ext.clang");
    result = qualified_type::make(t.unqualified(), cv_none);
    return analysis_result::well_formed;
}

/*
 * [ext:clang_builtin_functions] T __builtin_bswapg(T) (clang's
 * Builtins.td, custom type checking): the bytes of an integer of a multiple
 * of 8 bits in the opposite order; the call has the argument's type.
 */
analysis_result
semantic_analysis::bswapg_result(SyntaxRef n, entity_index called,
                                 const owned_array<uint32_t> &nodes,
                                 qualified_type &result)
{
    if (nodes.size() != 1)
        return ill_formed(n, "__builtin_bswapg takes one argument",
                          "ext.clang");
    qualified_type t = semantics[nodes[0]].type;
    fundamental_type f = fundamental_of(t);
    if (depends_on_template_parameter(t))
        return not_supported(n, "temp.dep.constexpr");
    if (f == fundamental_type::none || !fundamental_type_is_integral(f))
        return ill_formed(n,
                          "the argument of __builtin_bswapg shall be an "
                          "integer",
                          "ext.clang");
    unsigned width = fundamental_type_width(tokens.files.behavior, f);
    /* (a bool has one byte of storage, and swapping it changes nothing) */
    if (f == fundamental_type::bool_)
        width = 8;
    if (width % 8 || width > 64)
        return not_supported_builtin(n, "ext.clang", called,
                                     "of an integer wider than 64 bits or of a "
                                     "width that is not a multiple of 8");
    result = qualified_type::make(t.unqualified(), cv_none);
    return analysis_result::well_formed;
}

int semantic_analysis::query_builtin(entity_index e) const
{
    if (!entity_table[e].gnu_builtin_row)
        return 0;
    const gnu_builtin &row = gnu_builtin_at(entity_table[e].gnu_builtin_row);
    if (row.form != builtin_form::query)
        return 0;
    return row.which;
}

/* the size in bytes of a lock-free query where clang's answer is the
   same as the lowering's: 1, 2, 4 and 8 are lock-free, a size above 16 or
   not a power of two is not; 0 and 16 (clang: lock-free on aarch64, the C
   front end's lowering: libatomic) are left undecided */
static bool lock_free_decided(unsigned __int128 size, bool &free)
{
    free = size == 1 || size == 2 || size == 4 || size == 8;
    if (free)
        return true;
    return size != 0 && size != 16;
}

/*
 * [ext:gnu_library_builtins] clang's int __builtin_constant_p(expr) (the
 * operand unevaluated), bool __atomic_always_lock_free(size_t, void *),
 * bool __atomic_is_lock_free(size_t, volatile void *) and
 * bool __c11_atomic_is_lock_free(size_t) (GCC manual 6.56, clang's
 * Builtins.td).  Operands are arithmetic, enumeration, pointer or
 * std::nullptr_t; the size of a lock-free query that is a constant 0 or 16
 * is not supported (lock_free_decided).
 */
analysis_result semantic_analysis::query_builtin_result(
    SyntaxRef n, entity_index called, int which,
    const owned_array<uint32_t> &nodes, qualified_type &result)
{
    size_t expected = 2;
    if (which == 1 || which == 4)
        expected = 1;
    if (nodes.size() != expected)
        return ill_formed(n, "a builtin takes the arguments its form does",
                          "ext.gnu");
    for (size_t i = 0; i < nodes.size(); i++) {
        qualified_type t = semantics[nodes[i]].type;
        fundamental_type f = fundamental_of(t);
        if (!is_pointer(t) &&
            (f == fundamental_type::none || f == fundamental_type::void_))
            return not_supported_builtin(
                n, "ext.gnu", called,
                "of an operand that is not an arithmetic, enumeration or "
                "pointer value");
    }
    if (which == 1) {
        result = qualified_type::make(
            type_table.fundamental(fundamental_type::int_), cv_none);
        return analysis_result::well_formed;
    }
    fundamental_type f = fundamental_of(semantics[nodes[0]].type);
    if (!fundamental_type_is_integral(f) && f != fundamental_type::none)
        return not_supported_builtin(n, "ext.gnu", called,
                                     "with a size that is not an integer");
    if (nodes.size() == 2 && !is_pointer(semantics[nodes[1]].type) &&
        fundamental_of(semantics[nodes[1]].type) !=
            fundamental_type::std_nullptr_t &&
        !fundamental_type_is_integral(fundamental_of(semantics[nodes[1]].type)))
        return not_supported_builtin(
            n, "ext.gnu", called,
            "with a pointer argument that is not a pointer or null");
    constant_value v;
    bool constant = false;
    VTRY(r, evaluate_constant(n, nodes[0], v, constant));
    if (constant && v.kind == constant_value_kind::integer) {
        bool free = false;
        if (!lock_free_decided(v.integer, free))
            return not_supported_builtin(
                n, "ext.gnu", called,
                "with a constant size of 0 or 16, which clang and vcc answer "
                "differently");
    } else if (which == 2) {
        return ill_formed(n, "the argument must be a constant integer",
                          "ext.gnu");
    }
    result = qualified_type::make(
        type_table.fundamental(fundamental_type::bool_), cv_none);
    return analysis_result::well_formed;
}

constness
semantic_analysis::evaluate_floating_builtin(const semantic_node &call,
                                             constant_value &v)
{
    double number = __builtin_inf();
    if (call.count) {
        const semantic_node &argument =
            semantics[semantics.items(call.first)[0]];
        if (argument.kind != semantic_kind::conversion ||
            static_cast<conversion_kind>(argument.op) !=
                conversion_kind::array_to_pointer)
            return constness::unknown;
        const semantic_node &literal = semantics[argument.a];
        if (literal.kind != semantic_kind::string_literal)
            return constness::unknown;
        owned_buffer payload;
        gnu_asm_string_value(tokens, literal.at, payload);
        int precision = 52;
        if (fundamental_of(call.type) == fundamental_type::float_)
            precision = 23;
        unsigned __int128 bits;
        if (!vsoftfloat_nan_payload(payload.data(), precision, false, &bits))
            return constness::unknown;
        /* Constexpr evaluation holds float values as doubles. Preserve
           the NaN payload in the high fraction bits of that double. */
        uint64_t encoded = (UINT64_C(0x7ff) << 52) |
                           static_cast<uint64_t>((bits & Q_M112) >> 60);
        number = floating_value(encoded);
    }
    v = constant_value();
    v.kind = constant_value_kind::floating;
    v.floating = number;
    return constness::constant;
}

/* the value of a query when its operands are constants: __builtin_constant_p
   of an arithmetic constant is 1 (an operand that is not one is left
   undecided: pointers and non-constants are not modelled); a lock-free
   query of a constant size with a null pointer, whose alignment is then
   no limit */
constness semantic_analysis::evaluate_query_builtin(constant_evaluation &state,
                                                    int which,
                                                    const semantic_node &call,
                                                    constant_value &v)
{
    const uint32_t *items = semantics.items(call.first);
    constant_value x;
    constness c = evaluate_node(state, items[0], x);
    if (c != constness::constant)
        return constness::unknown;
    if (which == 1) {
        if (x.kind != constant_value_kind::integer &&
            x.kind != constant_value_kind::floating)
            return constness::unknown;
        v = constant_value();
        v.kind = constant_value_kind::integer;
        v.integer = 1;
        return constness::constant;
    }
    bool free = false;
    if (x.kind != constant_value_kind::integer ||
        !lock_free_decided(x.integer, free))
        return constness::unknown;
    /* clang folds __c11_atomic_is_lock_free of the sizes that are */
    if (which == 4 && !free)
        return constness::unknown;
    if (call.count == 2) {
        constant_value p;
        /* nullptr: a constant of type std::nullptr_t, no value of its own
           (evaluate_node does not model it) */
        const semantic_node &pointer = semantics[items[1]];
        if (pointer.kind == semantic_kind::constant &&
            fundamental_of(pointer.type) == fundamental_type::std_nullptr_t) {
            p.kind = constant_value_kind::pointer;
            c = constness::constant;
        } else {
            c = evaluate_node(state, items[1], p);
        }
        bool null =
            (p.kind == constant_value_kind::integer && p.integer == 0) ||
            (p.kind == constant_value_kind::pointer && !p.designator &&
             p.offset == 0);
        if (c != constness::constant || !null)
            return constness::unknown;
    }
    v = constant_value();
    v.kind = constant_value_kind::integer;
    v.integer = free;
    return constness::constant;
}

int semantic_analysis::bit_count_builtin(entity_index e) const
{
    if (!entity_table[e].gnu_builtin_row)
        return 0;
    const gnu_builtin &row = gnu_builtin_at(entity_table[e].gnu_builtin_row);
    if (row.form != builtin_form::bit_count)
        return 0;
    return row.which;
}

/* clang's __builtin_popcountg(x), __builtin_clzg(x[, fallback]) and
   __builtin_ctzg(x[, fallback]) (Sema's BuiltinPopcountg and the like): "1st
   argument must be a scalar unsigned integer type", and the fallback "must
   have type int after integer promotion" */
analysis_result
semantic_analysis::bit_count_arguments(SyntaxRef n, entity_index called,
                                       const owned_array<uint32_t> &nodes)
{
    int which = bit_count_builtin(called);
    size_t most = 2;
    if (which == 1)
        most = 1;
    if (nodes.size() < 1 || nodes.size() > most)
        return ill_formed(n,
                          "a bit count takes the value to count and, for "
                          "clzg and ctzg, a fallback",
                          "ext.gnu");
    qualified_type first = semantics[nodes[0]].type;
    fundamental_type f = fundamental_of(first);
    if (f == fundamental_type::none)
        return not_supported_builtin(n, "ext.gnu", called,
                                     "of an operand that is not an integer");
    if (!fundamental_type_is_integral(f) ||
        fundamental_type_is_signed(tokens.files.behavior, f))
        return ill_formed(n,
                          "1st argument must be a scalar unsigned integer "
                          "type",
                          "ext.gnu");
    if (nodes.size() == 2) {
        fundamental_type g = fundamental_of(semantics[nodes[1]].type);
        if (g == fundamental_type::none)
            return not_supported_builtin(
                n, "ext.gnu", called, "with a fallback that is not an integer");
        if (!fundamental_type_is_integral(g) ||
            fundamental_type_width(tokens.files.behavior, g) >
                fundamental_type_width(tokens.files.behavior,
                                       fundamental_type::int_))
            return ill_formed(n,
                              "2nd argument must have type int after "
                              "integer promotion",
                              "ext.gnu");
    }
    return analysis_result::well_formed;
}

/* the count of the value of the first argument, an unsigned integer of
   its type's width; for zero the fallback, or no constant (clang: "the
   operand is zero") when there is none */
constness semantic_analysis::evaluate_bit_count(constant_evaluation &state,
                                                int which,
                                                const semantic_node &call,
                                                constant_value &v)
{
    const uint32_t *items = semantics.items(call.first);
    constant_value x;
    constness c = evaluate_node(state, items[0], x);
    if (c == constness::constant && x.kind != constant_value_kind::integer)
        return constness::unknown;
    if (c != constness::constant)
        return c;
    unsigned width = fundamental_type_width(
        tokens.files.behavior, fundamental_of(semantics[items[0]].type));
    unsigned __int128 bits = x.integer;
    if (width < 128)
        bits &= (static_cast<unsigned __int128>(1) << width) - 1;
    uint64_t low = static_cast<uint64_t>(bits);
    uint64_t high = static_cast<uint64_t>(bits >> 64);
    if (bits == 0 && which != 1) {
        if (call.count < 2)
            return constness::not_constant;
        return evaluate_node(state, items[1], v);
    }
    unsigned count = 0;
    if (which == 1) {
        count = static_cast<unsigned>(__builtin_popcountll(low) +
                                      __builtin_popcountll(high));
    } else if (which == 2) {
        if (high)
            count = static_cast<unsigned>(__builtin_clzll(high));
        else
            count = 64 + static_cast<unsigned>(__builtin_clzll(low));
        /* the 128 bits counted: the leading zeros above the width */
        count -= 128 - width;
    } else if (low) {
        count = static_cast<unsigned>(__builtin_ctzll(low));
    } else {
        count = 64 + static_cast<unsigned>(__builtin_ctzll(high));
    }
    v = constant_value();
    v.kind = constant_value_kind::integer;
    v.integer = count;
    return constness::constant;
}

/* the value of __builtin_bswapg's argument with its bytes reversed, as the
   argument's type */
constness semantic_analysis::evaluate_bswap(constant_evaluation &state,
                                            const semantic_node &call,
                                            constant_value &v)
{
    constant_value x;
    constness c = evaluate_node(state, semantics.items(call.first)[0], x);
    if (c != constness::constant)
        return c;
    if (x.kind != constant_value_kind::integer)
        return constness::unknown;
    fundamental_type f = fundamental_of(call.type);
    unsigned width = fundamental_type_width(tokens.files.behavior, f);
    if (f == fundamental_type::bool_)
        width = 8;
    unsigned __int128 bits = x.integer, swapped = 0;
    for (unsigned i = 0; i < width / 8; i++) {
        swapped = (swapped << 8) | (bits & 0xff);
        bits >>= 8;
    }
    v = constant_value();
    v.kind = constant_value_kind::integer;
    v.integer = integer_wrap(tokens.files.behavior, f, swapped);
    return constness::constant;
}

/* clang's T *__builtin_addressof(T &) (Builtins.td, custom type checking):
   [specialized.addressof]/1's std::addressof, "the actual address of the
   object or function referenced by r, even in the presence of an
   overloaded operator&"; a call of the name unqualified lookup finds
   nothing for, with one argument */
bool semantic_analysis::builtin_addressof_call(SyntaxRef n)
{
    if (n.size() != 4 || !n[0].is_identifier() ||
        n[2].is(syntax_kind::initializer_list) ||
        !tokens.files.behavior.has(extension::gnu_library_builtins))
        return false;
    static const char spelled[] = "__builtin_addressof";
    uint32_t name = tokens.identifier(n[0].token());
    if (name != tokens.pp.identifier(spelled, sizeof spelled - 1) + 1)
        return false;
    lookup_result x = lookup_unqualified(name, lookup_filter::any);
    return !x.entity && !x.unknown && !x.ambiguous;
}

/* the built-in & of [expr.unary.op]/3.2 on the lvalue operand: a prvalue
   "pointer to T" (cv kept), constant where &x is ([expr.const]/11) */
analysis_result semantic_analysis::builtin_addressof(SyntaxRef n,
                                                     expression_meaning &m)
{
    SyntaxElement operand = n[2];
    VTRY(r, expression(operand, m));
    if (m.category != value_category::lvalue)
        return ill_formed(n,
                          "the operand of __builtin_addressof shall be an "
                          "lvalue",
                          "specialized.addressof");
    if (m.bit_field_width)
        return address_of_bit_field(n.first_token());
    address_of_glvalue(n.first_token(), m);
    return analysis_result::well_formed;
}

} // namespace cplusplus
