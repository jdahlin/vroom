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
#include "frontend/builtin-fold.h"
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
static const char signature_letters[] = "vbcChiulLwWzfdDFBHIQEN";
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
    /* F, B and H: the x86 builtins' vectors of 16 bytes (v4sf, v16qi, v8hi),
       made from these elements */
    fundamental_type::float_,
    fundamental_type::char_,
    fundamental_type::short_int,
    /* I, Q and E: vectors of 16 bytes of int, long long and double (v4si,
       v2di, v2df); N: short */
    fundamental_type::int_,
    fundamental_type::long_long_int,
    fundamental_type::double_,
    fundamental_type::short_int,
};

enum class floating_builtin {
    none,
    infinity,
    quiet_nan,
    signaling_nan,
    infinity_sign,
    is_signaling,
};

static floating_builtin floating_builtin_kind(const char *spelling,
                                              size_t length)
{
    static const struct {
        const char *name;
        floating_builtin kind;
    } builtins[] = {
        {"__builtin_huge_val", floating_builtin::infinity},
        {"__builtin_huge_valf", floating_builtin::infinity},
        {"__builtin_inf", floating_builtin::infinity},
        {"__builtin_inff", floating_builtin::infinity},
        {"__builtin_nan", floating_builtin::quiet_nan},
        {"__builtin_nanf", floating_builtin::quiet_nan},
        {"__builtin_nanl", floating_builtin::quiet_nan},
        {"__builtin_nans", floating_builtin::signaling_nan},
        {"__builtin_nansf", floating_builtin::signaling_nan},
        {"__builtin_nansl", floating_builtin::signaling_nan},
        {"__builtin_isinf_sign", floating_builtin::infinity_sign},
        {"__builtin_issignaling", floating_builtin::is_signaling},
    };
    for (const auto &builtin : builtins)
        if (strlen(builtin.name) == length &&
            !memcmp(spelling, builtin.name, length))
            return builtin.kind;
    return floating_builtin::none;
}

/* the type of a letter of the C front end's builtin signatures (frontend/c/
   builtins-registry.cpp, cc_builtin_signature_type): a scalar, a vector of 16
   bytes, or a pointer to void or char (p, P, s, S); not a va_list (V, M) */
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
    /* [ext:gnu_vector_size] F, B, H, I, Q and E: the x86 builtins' vectors of
       16 bytes (v4sf, v16qi, v8hi, v4si, v2di, v2df) of that element */
    if (strchr("FBHIQE", letter))
        t = qualified_type::make(type_table.vector_of(t, 16 / size_of_type(t)),
                                 cv_none);
    return true;
}

/* a builtin of vcc's C front end (frontend/c/builtins-registry.cpp): its
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
        form == builtin_form::launder || form == builtin_form::clear_padding ||
        form == builtin_form::bswapg || form == builtin_form::stdc_bits ||
        form == builtin_form::align || form == builtin_form::vtable_pointer ||
        form == builtin_form::invoke || form == builtin_form::complex) {
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
        if (known && (form == builtin_form::complex_part ||
                      form == builtin_form::complex_conjugate)) {
            /* the C table's d. f. D.: the real type of the complex argument */
            qualified_type complex_type = qualified_type::make(
                type_table.complex_of(types_of[0]), cv_none);
            if (form == builtin_form::complex_part) {
                types_of.append(complex_type);
            } else {
                types_of[0] = complex_type;
                types_of.append(complex_type);
            }
            variadic = false;
        }
        if (known && form == builtin_form::carry && types_of.size() == 4) {
            /* the C table writes the carry out as the variadic rest: a
               pointer to the type */
            types_of.append(qualified_type::make(
                type_table.pointer_to(types_of[0]), cv_none));
            variadic = false;
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
    /* [ext:clang_builtin_functions] coroutine handle builtins */
    if (length >= 15 && !memcmp(spelling, "__builtin_coro_", 15)) {
        qualified_type void_t = builtin_scalar(fundamental_type::void_);
        qualified_type void_ptr =
            qualified_type::make(type_table.pointer_to(void_t), cv_none);
        qualified_type bool_t = builtin_scalar(fundamental_type::bool_);
        qualified_type int_t = builtin_scalar(fundamental_type::int_);
        const char *rest = spelling + 15;
        size_t rlen = length - 15;
        if ((rlen == 6 && !memcmp(rest, "resume", 6)) ||
            (rlen == 7 && !memcmp(rest, "destroy", 7)))
            return declare_builtin(name, name, void_t, &void_ptr, 1, false,
                                   gnu_coroutine_builtin);
        if (rlen == 4 && !memcmp(rest, "done", 4))
            return declare_builtin(name, name, bool_t, &void_ptr, 1, false,
                                   gnu_coroutine_builtin);
        if (rlen == 4 && !memcmp(rest, "noop", 4))
            return declare_builtin(name, name, void_ptr, nullptr, 0, false,
                                   gnu_coroutine_builtin);
        if (rlen == 7 && !memcmp(rest, "promise", 7)) {
            qualified_type params[3] = {void_ptr, int_t, bool_t};
            return declare_builtin(name, name, void_ptr, params, 3, false,
                                   gnu_coroutine_builtin);
        }
    }
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
               (static_cast<conversion_kind>(semantics[object].op) ==
                    conversion_kind::lvalue_to_rvalue ||
                static_cast<conversion_kind>(semantics[object].op) ==
                    conversion_kind::array_to_pointer))
            object = semantics[object].a;
        qualified_type t = semantics[object].type;
        /* (a template's own va_list is dependent: analysed when it is
           instantiated) */
        if (depends_on_template_parameter(t))
            return not_supported(n, "temp.dep.constexpr");
        /* (the lvalue-to-rvalue conversion argument passing applied is
           the lowering's no-op: the node under it is the object) */
        uint32_t list_cv = 0;
        if (!is_va_list(t, list_cv))
            return ill_formed(n,
                              "argument is of type that "
                              "is not 'va_list'",
                              "ext.gnu");
        /* "binding reference of type '__builtin_va_list' to value of type
           'const __builtin_va_list' drops 'const' qualifier" */
        bool written = which != 4 || i == 0;
        if (written && (list_cv & cv_const))
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
    case builtin_form::complex:
        return complex_result(n, called, nodes, result);
    case builtin_form::launder:
        return launder_result(n, called, nodes, result);
    case builtin_form::clear_padding:
        return clear_padding_result(n, called, nodes, result);
    case builtin_form::bswapg:
        return bswapg_result(n, called, nodes, result);
    case builtin_form::align:
        return align_result(n, called, nodes, result);
    case builtin_form::vtable_pointer:
        return vtable_pointer_result(n, called, nodes, result);
    case builtin_form::stdc_bits:
        return stdc_bits_result(n, called, nodes, result);
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
        /* an unscoped enumeration is its underlying integer ([dcl.enum]/5:
           clang rejects a scoped one) */
        entity_index enumeration = enumeration_of(pointee);
        if (enumeration) {
            if (entity_table[enumeration].scoped)
                return ill_formed(n,
                                  "address argument to atomic operation must "
                                  "be a pointer to integer, pointer or "
                                  "supported floating point type",
                                  "ext.clang");
            f = entity_table[enumeration].underlying;
        }
        /* the generic forms take an object of any trivially copyable class
           type that is as large as an atomic one ([intro.object]) */
        bool generic_form = !spelled_with(spelling, length, "_n") &&
                            !spelled_with(spelling, length, "__c11_") &&
                            !spelled_with(spelling, length, "__sync_");
        bool class_object =
            type_table[pointee.unqualified()].kind == type_kind::class_type;
        if (class_object && generic_form) {
            bool known = false;
            entity_index c = type_table[pointee.unqualified()].entity;
            if (!class_trivially_copyable(c, known) || !known)
                return not_supported_builtin(
                    n, "ext.gnu", called,
                    "on an object of a class that is not known to be "
                    "trivially copyable");
            require_complete(c);
            uint64_t bytes = entity_table[c].size;
            if (bytes != 1 && bytes != 2 && bytes != 4 && bytes != 8 &&
                bytes != 16)
                return not_supported_builtin(
                    n, "ext.gnu", called,
                    "on an object of a class whose size is not 1, 2, 4, 8 or "
                    "16 bytes");
            f = fundamental_type::unsigned_long_int;
        }
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
 * [ext:gnu_library_builtins] _Complex T __builtin_complex (T real, T imag)
 * (GCC manual 6.14, "Complex Numbers": "The two operands must have the same
 * real floating-point type, and the result has the corresponding complex
 * type"): not a constant expression's operands' conversion; each operand
 * keeps its type.
 */
analysis_result
semantic_analysis::complex_result(SyntaxRef n, entity_index called,
                                  const owned_array<uint32_t> &nodes,
                                  qualified_type &result)
{
    if (nodes.size() != 2)
        return ill_formed(n, "__builtin_complex takes two arguments",
                          "ext.gnu");
    qualified_type a = semantics[nodes[0]].type;
    qualified_type b = semantics[nodes[1]].type;
    if (depends_on_template_parameter(a) || depends_on_template_parameter(b))
        return not_supported(n, "temp.dep.constexpr");
    fundamental_type f = fundamental_of(a);
    if (a.unqualified() != b.unqualified())
        return ill_formed(n,
                          "the operands of __builtin_complex shall have the "
                          "same type",
                          "ext.gnu");
    if (f != fundamental_type::float_ && f != fundamental_type::double_ &&
        f != fundamental_type::long_double)
        return ill_formed(n,
                          "the operands of __builtin_complex shall have real "
                          "floating-point type",
                          "ext.gnu");
    result = qualified_type::make(
        type_table.complex_of(qualified_type::make(a.unqualified(), cv_none)),
        cv_none);
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

/* [ext:clang_builtin_functions] void __builtin_clear_padding(T *) */
analysis_result
semantic_analysis::clear_padding_result(SyntaxRef n, entity_index called,
                                        const owned_array<uint32_t> &nodes,
                                        qualified_type &result)
{
    (void)called;
    if (nodes.size() != 1)
        return ill_formed(n, "__builtin_clear_padding takes one argument",
                          "ext.clang");
    qualified_type t = semantics[nodes[0]].type;
    if (depends_on_template_parameter(t))
        return not_supported(n, "temp.dep.constexpr");
    if (!is_pointer(t))
        return ill_formed(n,
                          "the argument of __builtin_clear_padding shall be a "
                          "pointer to an object type",
                          "ext.clang");
    qualified_type pointee = type_table[t.unqualified()].operand;
    if (type_table[pointee.unqualified()].kind == type_kind::function ||
        fundamental_of(pointee) == fundamental_type::void_)
        return ill_formed(n,
                          "the argument of __builtin_clear_padding shall be a "
                          "pointer to an object type",
                          "ext.clang");
    result = qualified_type::make(
        type_table.fundamental(fundamental_type::void_), cv_none);
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

/*
 * [ext:clang_builtin_functions] __builtin_align_up(x, alignment),
 * __builtin_align_down and bool __builtin_is_aligned (clang's
 * Builtins.td, custom type checking): x an integer (not bool) or a pointer
 * to an object or void, alignment an integer; the call has x's type (a
 * bool for is_aligned).  A constant alignment shall be a power of two;
 * other operands are unsupported.
 */
analysis_result
semantic_analysis::align_result(SyntaxRef n, entity_index called,
                                const owned_array<uint32_t> &nodes,
                                qualified_type &result)
{
    const gnu_builtin &row =
        gnu_builtin_at(entity_table[called].gnu_builtin_row);
    if (nodes.size() != 2)
        return ill_formed(n, "the builtin takes two arguments", "ext.clang");
    qualified_type t = semantics[nodes[0]].type;
    qualified_type a = semantics[nodes[1]].type;
    if (depends_on_template_parameter(t) || depends_on_template_parameter(a))
        return not_supported(n, "temp.dep.constexpr");
    fundamental_type f = fundamental_of(t);
    bool integer = f != fundamental_type::none &&
                   fundamental_type_is_integral(f) &&
                   f != fundamental_type::bool_;
    const struct type &tk = type_table[t.unqualified()];
    bool pointer = tk.kind == type_kind::pointer;
    if (pointer) {
        type_kind ok = type_table[tk.operand.unqualified()].kind;
        if (ok == type_kind::function)
            pointer = false;
    }
    if (!integer && !pointer)
        return not_supported_builtin(n, "ext.clang", called,
                                     "of an operand that is no integer and "
                                     "no pointer to an object");
    fundamental_type af = fundamental_of(a);
    if (af == fundamental_type::none || !fundamental_type_is_integral(af))
        return not_supported_builtin(n, "ext.clang", called,
                                     "of an alignment that is no integer");
    constant_value v;
    if (evaluate_value(nodes[1], v) == constness::constant) {
        if (v.kind != constant_value_kind::integer || !v.integer ||
            (v.integer & (v.integer - 1)))
            return not_supported_builtin(n, "ext.clang", called,
                                         "of a constant alignment that is no "
                                         "power of two");
    }
    if (row.which == 2) {
        result = qualified_type::make(
            type_table.fundamental(fundamental_type::bool_), cv_none);
        return analysis_result::well_formed;
    }
    result = qualified_type::make(t.unqualified(), cv_none);
    return analysis_result::well_formed;
}

/*
 * [ext:clang_builtin_functions] const void *__builtin_get_vtable_pointer(p)
 * (clang's Builtins.td, custom type checking, C++): p a pointer (or an array,
 * which decays) to a complete polymorphic class; the result is the virtual
 * table pointer stored in the object, a `const void *`.
 */
analysis_result
semantic_analysis::vtable_pointer_result(SyntaxRef n, entity_index called,
                                         const owned_array<uint32_t> &nodes,
                                         qualified_type &result)
{
    (void)called;
    if (nodes.size() != 1)
        return ill_formed(n, "__builtin_get_vtable_pointer takes one argument",
                          "ext.clang");
    qualified_type t = semantics[nodes[0]].type;
    if (depends_on_template_parameter(t))
        return not_supported(n, "temp.dep.constexpr");
    const struct type &tk = type_table[t.unqualified()];
    qualified_type pointee;
    if (tk.kind == type_kind::pointer || tk.kind == type_kind::array)
        pointee = tk.operand;
    else
        return ill_formed(n,
                          "__builtin_get_vtable_pointer requires an argument "
                          "of class pointer type",
                          "ext.clang");
    if (type_table[pointee.unqualified()].kind != type_kind::class_type)
        return ill_formed(n,
                          "__builtin_get_vtable_pointer requires an argument "
                          "of class pointer type",
                          "ext.clang");
    if (!require_complete(pointee))
        return ill_formed(n,
                          "__builtin_get_vtable_pointer requires an argument "
                          "with a complete type",
                          "ext.clang");
    if (!polymorphic_class(type_table[pointee.unqualified()].entity))
        return ill_formed(n,
                          "__builtin_get_vtable_pointer requires an argument "
                          "of polymorphic class pointer type",
                          "ext.clang");
    result = qualified_type::make(
        type_table.pointer_to(qualified_type::make(
            type_table.fundamental(fundamental_type::void_), cv_const)),
        cv_none);
    return analysis_result::well_formed;
}

/*
 * [ext:gnu_library_builtins] C23's <stdbit.h> functions as clang's
 * __builtin_stdc_* (C23 7.18) and __builtin_bitreverseg, one rule for the
 * family: the first argument is a scalar unsigned integer (bitreverseg:
 * any integer), not a bool except for the rotations and bitreverseg; a
 * rotation's second argument is any integer.  The call has the unsigned int
 * of the counts, the bool of has_single_bit, or the first argument's type
 * (bit_ceil, bit_floor, the rotations, bitreverseg).
 */
analysis_result
semantic_analysis::stdc_bits_result(SyntaxRef n, entity_index called,
                                    const owned_array<uint32_t> &nodes,
                                    qualified_type &result)
{
    const gnu_builtin &row =
        gnu_builtin_at(entity_table[called].gnu_builtin_row);
    builtin_stdc_op op = static_cast<builtin_stdc_op>(row.which);
    bool rotation = op == builtin_stdc_op::rotate_left ||
                    op == builtin_stdc_op::rotate_right;
    bool reverse = op == builtin_stdc_op::bitreverse;
    size_t wanted = 1;
    if (rotation)
        wanted = 2;
    if (nodes.size() != wanted)
        return ill_formed(n, "wrong number of arguments to a bit function",
                          "ext.gnu");
    qualified_type first = semantics[nodes[0]].type;
    fundamental_type f = fundamental_of(first);
    if (depends_on_template_parameter(first))
        return not_supported(n, "temp.dep.constexpr");
    if (f == fundamental_type::none)
        return not_supported_builtin(n, "ext.gnu", called,
                                     "of an operand that is not an integer");
    if (!fundamental_type_is_integral(f))
        return ill_formed(n, "1st argument must be a scalar integer type",
                          "ext.gnu");
    if (!reverse && fundamental_type_is_signed(tokens.files.behavior, f))
        return ill_formed(n,
                          "1st argument must be a scalar unsigned integer "
                          "type",
                          "ext.gnu");
    if (!reverse && !rotation && f == fundamental_type::bool_)
        return ill_formed(n,
                          "1st argument must not be a boolean or "
                          "enumeration type",
                          "ext.gnu");
    if (rotation) {
        fundamental_type g = fundamental_of(semantics[nodes[1]].type);
        if (g == fundamental_type::none)
            return not_supported_builtin(
                n, "ext.gnu", called,
                "with a rotation count that is not an integer");
        if (!fundamental_type_is_integral(g))
            return ill_formed(n, "2nd argument must be a scalar integer type",
                              "ext.gnu");
    }
    switch (op) {
    case builtin_stdc_op::has_single_bit:
        result = qualified_type::make(
            type_table.fundamental(fundamental_type::bool_), cv_none);
        break;
    case builtin_stdc_op::bit_ceil:
    case builtin_stdc_op::bit_floor:
    case builtin_stdc_op::rotate_left:
    case builtin_stdc_op::rotate_right:
    case builtin_stdc_op::bitreverse:
        result = qualified_type::make(first.unqualified(), cv_none);
        break;
    default:
        result = qualified_type::make(
            type_table.fundamental(fundamental_type::unsigned_int), cv_none);
        break;
    }
    return analysis_result::well_formed;
}

/* the value of a stdc_bits call, on the first argument's width in bits (a
   bool's rotation and reversal have one bit): C23 7.18.2-7.18.14 */
constness semantic_analysis::evaluate_stdc_bits(constant_evaluation &state,
                                                int which,
                                                const semantic_node &call,
                                                constant_value &v)
{
    const uint32_t *items = semantics.items(call.first);
    constant_value x[2];
    for (uint32_t i = 0; i < call.count && i < 2; i++) {
        constness c = evaluate_node(state, items[i], x[i]);
        if (c != constness::constant)
            return c;
        if (x[i].kind != constant_value_kind::integer)
            return constness::unknown;
    }
    fundamental_type f = fundamental_of(semantics[items[0]].type);
    unsigned width = fundamental_type_width(tokens.files.behavior, f);
    if (f == fundamental_type::bool_)
        width = 1;
    unsigned __int128 bits = vfold_mask(x[0].integer, width);
    unsigned __int128 inverse = vfold_mask(~bits, width);
    /* the counts of zero bits from the most or the least significant end
       (the width for a value with none) */
    auto leading = [&](unsigned __int128 value) -> unsigned {
        return vfold_leading_zeros(value, width);
    };
    auto trailing = [&](unsigned __int128 value) -> unsigned {
        if (!value)
            return width;
        return vfold_trailing_zeros(value);
    };
    unsigned __int128 r = 0;
    switch (static_cast<builtin_stdc_op>(which)) {
    case builtin_stdc_op::leading_zeros:
        r = leading(bits);
        break;
    case builtin_stdc_op::leading_ones:
        r = leading(inverse);
        break;
    case builtin_stdc_op::trailing_ones:
        r = trailing(inverse);
        break;
    case builtin_stdc_op::first_leading_zero:
        if (inverse)
            r = leading(inverse) + 1;
        break;
    case builtin_stdc_op::first_leading_one:
        if (bits)
            r = leading(bits) + 1;
        break;
    case builtin_stdc_op::first_trailing_zero:
        if (inverse)
            r = trailing(inverse) + 1;
        break;
    case builtin_stdc_op::first_trailing_one:
        if (bits)
            r = trailing(bits) + 1;
        break;
    case builtin_stdc_op::count_zeros:
        r = width - vfold_count_ones(bits);
        break;
    case builtin_stdc_op::bit_width:
        r = width - leading(bits);
        break;
    case builtin_stdc_op::has_single_bit:
        r = vfold_count_ones(bits) == 1;
        break;
    case builtin_stdc_op::bit_ceil: {
        /* 1 for 0 and 1; the power of two above x - 1, which wraps to 0
           when the type has none (clang) */
        r = 1;
        if (bits > 1) {
            unsigned shift = width - leading(bits - 1);
            if (shift < width)
                r = static_cast<unsigned __int128>(1) << shift;
            else
                r = 0;
        }
        break;
    }
    case builtin_stdc_op::bit_floor:
        if (bits)
            r = static_cast<unsigned __int128>(1)
                << (width - 1 - leading(bits));
        break;
    case builtin_stdc_op::rotate_left:
    case builtin_stdc_op::rotate_right: {
        r = bits;
        if (width > 1) {
            /* a count of a signed type rotates the other way when
               negative: its value modulo the width */
            fundamental_type g = fundamental_of(semantics[items[1]].type);
            __int128 count = static_cast<__int128>(x[1].integer);
            bool negative =
                fundamental_type_is_signed(tokens.files.behavior, g) &&
                count < 0;
            bool right =
                which == static_cast<int>(builtin_stdc_op::rotate_right);
            unsigned __int128 magnitude = x[1].integer;
            if (negative) {
                magnitude = static_cast<unsigned __int128>(-count);
                right = !right;
            }
            r = vfold_rotate(bits, width, magnitude % width, right);
        }
        break;
    }
    case builtin_stdc_op::bitreverse:
        r = bits;
        if (width > 1 && !vfold_integer(vfold_bitreverse, bits, width, &r))
            return constness::not_constant;
        break;
    }
    /* (a 128-bit result that does not fit in 64 bits has no value kept) */
    if (!integer_kept_exactly(tokens.files.behavior, fundamental_of(call.type),
                              r))
        return constness::unknown;
    v = constant_value();
    v.kind = constant_value_kind::integer;
    v.integer =
        integer_wrap(tokens.files.behavior, fundamental_of(call.type), r);
    return constness::constant;
}

/*
 * [ext:clang_builtin_functions] the value of __builtin_align_up,
 * __builtin_align_down and __builtin_is_aligned of an integer: modulo the
 * operand's type, (x + a - 1) & ~(a - 1), x & ~(a - 1), x & (a - 1) == 0,
 * for a power of two a of at most 2^(width-1) (clang: other alignments are
 * errors).  A pointer operand has no value kept here (unknown).
 */
constness semantic_analysis::evaluate_align(constant_evaluation &state,
                                            int which,
                                            const semantic_node &call,
                                            constant_value &v)
{
    const uint32_t *items = semantics.items(call.first);
    if (call.count != 2)
        return constness::unknown;
    fundamental_type f = fundamental_of(semantics[items[0]].type);
    if (f == fundamental_type::none || !fundamental_type_is_integral(f))
        return constness::unknown;
    constant_value x[2];
    for (uint32_t i = 0; i < 2; i++) {
        constness c = evaluate_node(state, items[i], x[i]);
        if (c != constness::constant)
            return c;
        if (x[i].kind != constant_value_kind::integer)
            return constness::unknown;
    }
    unsigned width = fundamental_type_width(tokens.files.behavior, f);
    fundamental_type g = fundamental_of(semantics[items[1]].type);
    if (g == fundamental_type::none ||
        (fundamental_type_is_signed(tokens.files.behavior, g) &&
         static_cast<__int128>(x[1].integer) <= 0))
        return constness::unknown;
    unsigned __int128 a = x[1].integer;
    if (!a || (a & (a - 1)) || width > 64 ||
        a > (static_cast<unsigned __int128>(1) << (width - 1)))
        return constness::unknown;
    unsigned __int128 bits = vfold_mask(x[0].integer, width);
    unsigned __int128 mask = vfold_mask(~(a - 1), width);
    unsigned __int128 r = 0;
    switch (which) {
    case 0:
        r = vfold_mask(bits + a - 1, width) & mask;
        break;
    case 1:
        r = bits & mask;
        break;
    default:
        r = (bits & (a - 1)) == 0;
        break;
    }
    if (!integer_kept_exactly(tokens.files.behavior, fundamental_of(call.type),
                              r))
        return constness::unknown;
    v = constant_value();
    v.kind = constant_value_kind::integer;
    v.integer =
        integer_wrap(tokens.files.behavior, fundamental_of(call.type), r);
    return constness::constant;
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

constness semantic_analysis::evaluate_floating_builtin(
    constant_evaluation &state, entity_index builtin, const semantic_node &call,
    constant_value &v)
{
    size_t length = 0;
    const char *name =
        tokens.pp.identifier_spelling(entity_table[builtin].name - 1, &length);
    floating_builtin kind = floating_builtin_kind(name, length);
    if (kind == floating_builtin::infinity_sign ||
        kind == floating_builtin::is_signaling) {
        constant_value argument;
        constness r =
            evaluate_node(state, semantics.items(call.first)[0], argument);
        if (r != constness::constant)
            return r;
        if (argument.kind != constant_value_kind::floating)
            return constness::unknown;
        uint64_t bits = floating_kept(argument.floating);
        v = constant_value();
        v.kind = constant_value_kind::integer;
        if (kind == floating_builtin::is_signaling)
            v.integer = floating_is_signaling_nan(bits);
        else if (bits == UINT64_C(0x7ff0000000000000))
            v.integer = 1;
        else if (bits == UINT64_C(0xfff0000000000000))
            v.integer = static_cast<unsigned __int128>(-1);
        return constness::constant;
    }
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
        bool signaling = kind == floating_builtin::signaling_nan;
        unsigned __int128 bits;
        if (!vsoftfloat_nan_payload(payload.data(), precision, signaling,
                                    &bits))
            return constness::unknown;
        /* Constexpr evaluation holds float values as doubles. Preserve
           the NaN payload in the high fraction bits of that double. */
        number = floating_value(vsoftfloat_nan_storage(bits, 52, 11));
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
    if (which == 1 || which >= 4)
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
    unsigned __int128 bits = vfold_mask(x.integer, width);
    /* C23's stdc_trailing_zeros of 0 is the width; the g forms take the
       fallback */
    if (bits == 0 && which == 5) {
        v = constant_value();
        v.kind = constant_value_kind::integer;
        v.integer = width;
        return constness::constant;
    }
    if (bits == 0 && which != 1 && which != 4) {
        if (call.count < 2)
            return constness::not_constant;
        return evaluate_node(state, items[1], v);
    }
    int operation = vfold_popcount;
    if (which == 2)
        operation = vfold_clz;
    else if (which == 3 || which == 5)
        operation = vfold_ctz;
    unsigned __int128 count = 0;
    if (!vfold_integer(operation, bits, width, &count))
        return constness::not_constant;
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
    atomic_object_operand = true;
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

/* which of clang's vector builtins that nothing declares a name is: 1
   __builtin_shufflevector, 2 and 3 __builtin_elementwise_min and _max */
int semantic_analysis::vector_builtin_kind(uint32_t name)
{
    static const char shuffle[] = "__builtin_shufflevector";
    static const char minimum[] = "__builtin_elementwise_min";
    static const char maximum[] = "__builtin_elementwise_max";
    static const char add_sat[] = "__builtin_elementwise_add_sat";
    static const char sub_sat[] = "__builtin_elementwise_sub_sat";
    static const char store[] = "__builtin_nontemporal_store";
    static const char load[] = "__builtin_nontemporal_load";
    static const char abs_[] = "__builtin_elementwise_abs";
    static const char red_add[] = "__builtin_reduce_add";
    static const char red_max[] = "__builtin_reduce_max";
    static const char red_min[] = "__builtin_reduce_min";
    static const char red_and[] = "__builtin_reduce_and";
    static const char red_or[] = "__builtin_reduce_or";
    static const char red_xor[] = "__builtin_reduce_xor";
    if (name == tokens.pp.identifier(shuffle, sizeof shuffle - 1) + 1)
        return 1;
    if (name == tokens.pp.identifier(minimum, sizeof minimum - 1) + 1)
        return 2;
    if (name == tokens.pp.identifier(maximum, sizeof maximum - 1) + 1)
        return 3;
    if (name == tokens.pp.identifier(add_sat, sizeof add_sat - 1) + 1)
        return 4;
    if (name == tokens.pp.identifier(sub_sat, sizeof sub_sat - 1) + 1)
        return 5;
    if (name == tokens.pp.identifier(store, sizeof store - 1) + 1)
        return 6;
    if (name == tokens.pp.identifier(load, sizeof load - 1) + 1)
        return 7;
    if (name == tokens.pp.identifier(abs_, sizeof abs_ - 1) + 1)
        return 8;
    if (name == tokens.pp.identifier(red_add, sizeof red_add - 1) + 1)
        return 9;
    if (name == tokens.pp.identifier(red_max, sizeof red_max - 1) + 1)
        return 10;
    if (name == tokens.pp.identifier(red_min, sizeof red_min - 1) + 1)
        return 11;
    if (name == tokens.pp.identifier(red_and, sizeof red_and - 1) + 1)
        return 12;
    if (name == tokens.pp.identifier(red_or, sizeof red_or - 1) + 1)
        return 13;
    if (name == tokens.pp.identifier(red_xor, sizeof red_xor - 1) + 1)
        return 14;
    return 0;
}

/* clang's __builtin_nontemporal_store(value, pointer) and
   __builtin_nontemporal_load(pointer): the store and the load through the
   pointer, which only differ in the cache hint the code does not give */
analysis_result
semantic_analysis::nontemporal_access(SyntaxRef n,
                                      const owned_array<SyntaxElement> &clauses,
                                      bool store, expression_meaning &m)
{
    size_t arguments = 1, pointer_at = 0;
    if (store) {
        arguments = 2;
        pointer_at = 1;
    }
    if (clauses.size() != arguments)
        return ill_formed(n, "wrong number of arguments to the builtin",
                          "ext.clang");
    token_index at = n.first_token();
    expression_meaning pointer;
    VTRY(r, expression(clauses[pointer_at], pointer));
    VTRY_SET(r, operand_value(n, pointer));
    if (!is_object_pointer(pointer.type))
        return not_supported(n, "ext.clang");
    VTRY_SET(r, indirection_of(n, at, clauses[pointer_at], pointer));
    if (!store) {
        m = pointer;
        return operand_value(n, m);
    }
    expression_meaning value;
    VTRY_SET(r, expression(clauses[0], value));
    return assignment_of(n, at, preprocessing_op_or_punc::EQUAL, clauses[1],
                         clauses[0], pointer, value, m);
}

/* [ext:gnu_vector_size] clang's __builtin_shufflevector(a, b, i...): a call
   of that name with nothing declared by it (the C front end's builtin) */
bool semantic_analysis::builtin_shufflevector_call(SyntaxRef n)
{
    if (n.size() != 4 || !n[0].is_identifier() ||
        !tokens.files.behavior.has(extension::gnu_library_builtins))
        return false;
    uint32_t name = tokens.identifier(n[0].token());
    int kind = vector_builtin_kind(name);
    if (kind == 0)
        return false;
    if (kind < 7 && !n[2].is(syntax_kind::initializer_list))
        return false;
    lookup_result x = lookup_unqualified(name, lookup_filter::any);
    return !x.entity && !x.unknown && !x.ambiguous;
}

/* the lanes of the two vectors of one type, as many as there are indices
   (constants in [-1, 2N), -1 any lane): a prvalue vector of the same
   element type; the count a power of two within 64 bytes, as C's front end
   takes it */
analysis_result semantic_analysis::builtin_shufflevector(SyntaxRef n,
                                                         expression_meaning &m)
{
    owned_array<SyntaxElement> clauses;
    analysis_result r = analysis_result::well_formed;
    if (n[2].is(syntax_kind::initializer_list)) {
        VTRY_SET(r, list_clauses(n[2].index(), clauses));
    } else {
        clauses.append(n[2]);
    }
    int kind = vector_builtin_kind(tokens.identifier(n[0].token()));
    if (kind >= 9) {
        if (clauses.size() != 1)
            return ill_formed(n, "wrong number of arguments to the builtin",
                              "ext.clang");
        expression_meaning a;
        VTRY_SET(r, expression(clauses[0], a));
        VTRY_SET(r, operand_value(n, a));
        const struct type t = type_table[a.type.unqualified()];
        if (t.kind != type_kind::vector || !is_integral(t.operand))
            return ill_formed(n, "argument must be an integer vector",
                              "ext.clang");
        m = expression_meaning();
        m.type = qualified_type::make(t.operand.unqualified(), cv_none);
        m.category = value_category::prvalue;
        m.evaluation = constness::not_constant;
        m.beyond_integral_constant = true;
        m.node =
            expression_node(semantic_kind::vector_shuffle, m, n.first_token(),
                            a.node, a.node, static_cast<uint8_t>(kind));
        return analysis_result::well_formed;
    }
    if (kind == 8) {
        if (clauses.size() != 1)
            return ill_formed(n, "wrong number of arguments to the builtin",
                              "ext.clang");
        expression_meaning a;
        VTRY_SET(r, expression(clauses[0], a));
        VTRY_SET(r, operand_value(n, a));
        const struct type t = type_table[a.type.unqualified()];
        qualified_type et = a.type;
        if (t.kind == type_kind::vector)
            et = t.operand;
        fundamental_type f = fundamental_of(et);
        if (!is_floating(et) &&
            !(is_integral(et) && f != fundamental_type::bool_ &&
              fundamental_type_is_signed(tokens.files.behavior, f)))
            return ill_formed(n,
                              "argument to '__builtin_elementwise_abs' must be "
                              "a scalar or vector of signed integer or "
                              "floating-point type",
                              "ext.clang");
        m = expression_meaning();
        m.type = qualified_type::make(a.type.unqualified(), cv_none);
        m.category = value_category::prvalue;
        m.evaluation = constness::not_constant;
        m.beyond_integral_constant = true;
        m.node =
            expression_node(semantic_kind::vector_shuffle, m, n.first_token(),
                            a.node, a.node, static_cast<uint8_t>(kind));
        return analysis_result::well_formed;
    }
    if (kind >= 6)
        return nontemporal_access(n, clauses, kind == 6, m);
    if (kind != 1 && clauses.size() != 2)
        return ill_formed(n, "this builtin takes two vectors", "ext.gnu");
    if (clauses.size() < 3 && kind == 1)
        return ill_formed(n,
                          "__builtin_shufflevector takes two vectors and the "
                          "lane indices",
                          "ext.gnu");
    expression_meaning a, b;
    VTRY_SET(r, expression(clauses[0], a));
    VTRY_SET(r, expression(clauses[1], b));
    VTRY_SET(r, operand_value(n, a));
    VTRY_SET(r, operand_value(n, b));
    const struct type vector = type_table[a.type.unqualified()];
    if (vector.kind != type_kind::vector ||
        a.type.unqualified() != b.type.unqualified())
        return ill_formed(n,
                          "the first two arguments to __builtin_shufflevector "
                          "shall be vectors of the same type",
                          "ext.gnu");
    owned_array<uint32_t> indices;
    if (kind != 1) {
        /* __builtin_elementwise_min, _max, _add_sat and _sub_sat of
           integer vectors */
        if (!is_integral(vector.operand))
            return not_supported(n, "ext.gnu");
        m = expression_meaning();
        m.type = qualified_type::make(a.type.unqualified(), cv_none);
        m.category = value_category::prvalue;
        m.evaluation = constness::not_constant;
        m.beyond_integral_constant = true;
        m.node =
            expression_node(semantic_kind::vector_shuffle, m, n.first_token(),
                            a.node, b.node, static_cast<uint8_t>(kind));
        return analysis_result::well_formed;
    }
    for (size_t i = 2; i < clauses.size(); i++) {
        expression_meaning e;
        VTRY_SET(r, expression(clauses[i], e));
        bool constant = false;
        if (!is_integral(e.type))
            return not_supported(n, "ext.gnu");
        VTRY_SET(r, integral_constant_expression(n, e, constant));
        if (!constant)
            return not_supported(n, "ext.gnu");
        __int128 v = integer_value(tokens.files.behavior,
                                   fundamental_of(e.type), e.value);
        if (v < -1 || v >= static_cast<__int128>(2 * vector.bound))
            return ill_formed(n,
                              "an index of __builtin_shufflevector shall be "
                              "a constant integer in range",
                              "ext.gnu");
        indices.append(e.node);
    }
    uint64_t lanes = indices.size();
    if ((lanes & (lanes - 1)) || lanes * size_of_type(vector.operand) > 64)
        return not_supported(n, "ext.gnu");
    m = expression_meaning();
    m.type = qualified_type::make(type_table.vector_of(vector.operand, lanes),
                                  cv_none);
    m.category = value_category::prvalue;
    m.evaluation = constness::not_constant;
    m.beyond_integral_constant = true;
    m.node = expression_node(semantic_kind::vector_shuffle, m, n.first_token(),
                             a.node, b.node);
    semantics.set_list(m.node, indices.data(), indices.size());
    return analysis_result::well_formed;
}

static int complex_builtin_kind(const char *s, size_t len)
{
    if (len == 17 && !memcmp(s, "__builtin_complex", 17))
        return 1;
    if ((len == 15 && !memcmp(s, "__builtin_creal", 15)) ||
        (len == 16 && (!memcmp(s, "__builtin_crealf", 16) ||
                       !memcmp(s, "__builtin_creall", 16))))
        return 2;
    if ((len == 15 && !memcmp(s, "__builtin_cimag", 15)) ||
        (len == 16 && (!memcmp(s, "__builtin_cimagf", 16) ||
                       !memcmp(s, "__builtin_cimagl", 16))))
        return 3;
    if ((len == 14 && !memcmp(s, "__builtin_conj", 14)) ||
        (len == 15 && (!memcmp(s, "__builtin_conjf", 15) ||
                       !memcmp(s, "__builtin_conjl", 15))))
        return 4;
    return 0;
}

bool semantic_analysis::builtin_complex_call(SyntaxRef n)
{
    if (n.size() != 4 || !n[0].is_identifier() ||
        !tokens.files.behavior.has(extension::gnu_library_builtins))
        return false;
    uint32_t name = tokens.identifier(n[0].token());
    size_t len = 0;
    const char *s = tokens.pp.identifier_spelling(name - 1, &len);
    if (!complex_builtin_kind(s, len))
        return false;
    lookup_result x = lookup_unqualified(name, lookup_filter::any);
    return !x.entity && !x.unknown && !x.ambiguous;
}

analysis_result semantic_analysis::builtin_complex(SyntaxRef n,
                                                   expression_meaning &m)
{
    uint32_t name = tokens.identifier(n[0].token());
    size_t len = 0;
    const char *s = tokens.pp.identifier_spelling(name - 1, &len);
    int kind = complex_builtin_kind(s, len);
    owned_array<SyntaxElement> clauses;
    if (n[2].is(syntax_kind::initializer_list)) {
        VTRY(r, list_clauses(n[2].index(), clauses));
    } else {
        clauses.append(n[2]);
    }
    if (kind == 1) {
        if (clauses.size() != 2)
            return ill_formed(n, "__builtin_complex takes two arguments",
                              "ext.gnu");
        expression_meaning re, im;
        VTRY(r1, expression(clauses[0], re));
        VTRY(r2, expression(clauses[1], im));
        VTRY(r3, operand_value(n, re));
        VTRY(r4, operand_value(n, im));
        if (!is_floating(re.type) ||
            re.type.unqualified() != im.type.unqualified())
            return ill_formed(n,
                              "arguments to __builtin_complex must have the "
                              "same real floating type",
                              "ext.gnu");
        type_index ct = type_table.complex_of(
            qualified_type::make(re.type.unqualified(), cv_none));
        m = expression_meaning();
        m.type = qualified_type::make(ct, cv_none);
        m.category = value_category::prvalue;
        m.evaluation = constness::unknown;
        if (re.evaluation == constness::not_constant ||
            im.evaluation == constness::not_constant)
            m.evaluation = constness::not_constant;
        m.beyond_integral_constant = true;
        m.node = expression_node(semantic_kind::complex_value, m,
                                 n.first_token(), re.node, im.node);
        return analysis_result::well_formed;
    }
    if (clauses.size() != 1)
        return ill_formed(n, "wrong number of arguments to the builtin",
                          "ext.gnu");
    expression_meaning arg;
    VTRY(r1, expression(clauses[0], arg));
    VTRY(r2, operand_value(n, arg));
    const struct type at = type_table[arg.type.unqualified()];
    if (at.kind != type_kind::complex)
        return not_supported(n, "ext.gnu");
    m = expression_meaning();
    if (kind == 4)
        m.type = qualified_type::make(arg.type.unqualified(), cv_none);
    else
        m.type = qualified_type::make(at.operand.unqualified(), cv_none);
    m.category = value_category::prvalue;
    m.evaluation = arg.evaluation;
    m.beyond_integral_constant = true;
    m.node = expression_node(semantic_kind::complex_part, m, n.first_token(),
                             arg.node, 0, static_cast<uint8_t>(kind - 2));
    return analysis_result::well_formed;
}

} // namespace cplusplus
