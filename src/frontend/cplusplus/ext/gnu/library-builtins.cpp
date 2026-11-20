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
#include "frontend/c/ast-builder.h"
#include "frontend/cplusplus/sema.h"

namespace cplusplus
{

/* a type of a library builtin's prototype, as clang's Builtins.td writes
   them */
enum class builtin_type : uint8_t {
    end,
    void_,
    bool_,
    int_,
    float_,
    double_,
    long_double,
    size_t_,
    char_pointer,
    const_char_pointer,
    void_pointer,
    const_void_pointer,
    ellipsis,
    long_,
    long_long,
    int_pointer,
    /* <math.h>'s floating type: double, float with the suffix f, long
       double with l (C17 7.12/1's "f" and "l" suffixed functions) */
    real,
    real_pointer,
    wchar_,
    wchar_pointer,
    const_wchar_pointer,
};

struct library_builtin_rule {
    const char *name;
    bool noreturn;
    /* the result, then the parameters, ending with end (an ellipsis last
       if it is variadic) */
    builtin_type types[6];
};

using bt = builtin_type;

/* clang's Builtins.td, the LibBuiltin entries' prototypes (<stdlib.h>,
   <string.h>, <stdio.h>, <strings.h>, <math.h>) */
static const library_builtin_rule library_builtins[] = {
    {"abort", true, {bt::void_, bt::end}},
    {"abs", false, {bt::int_, bt::int_, bt::end}},
    {"ffs", false, {bt::int_, bt::int_, bt::end}},
    {"memchr",
     false,
     {bt::void_pointer, bt::const_void_pointer, bt::int_, bt::size_t_,
      bt::end}},
    {"memcmp",
     false,
     {bt::int_, bt::const_void_pointer, bt::const_void_pointer, bt::size_t_,
      bt::end}},
    {"memcpy",
     false,
     {bt::void_pointer, bt::void_pointer, bt::const_void_pointer, bt::size_t_,
      bt::end}},
    {"memmove",
     false,
     {bt::void_pointer, bt::void_pointer, bt::const_void_pointer, bt::size_t_,
      bt::end}},
    {"memset",
     false,
     {bt::void_pointer, bt::void_pointer, bt::int_, bt::size_t_, bt::end}},
    {"printf",
     false,
     {bt::int_, bt::const_char_pointer, bt::ellipsis, bt::end}},
    {"puts", false, {bt::int_, bt::const_char_pointer, bt::end}},
    {"snprintf",
     false,
     {bt::int_, bt::char_pointer, bt::size_t_, bt::const_char_pointer,
      bt::ellipsis, bt::end}},
    {"sprintf",
     false,
     {bt::int_, bt::char_pointer, bt::const_char_pointer, bt::ellipsis,
      bt::end}},
    {"strchr",
     false,
     {bt::char_pointer, bt::const_char_pointer, bt::int_, bt::end}},
    {"strcmp",
     false,
     {bt::int_, bt::const_char_pointer, bt::const_char_pointer, bt::end}},
    {"strcpy",
     false,
     {bt::char_pointer, bt::char_pointer, bt::const_char_pointer, bt::end}},
    {"strlen", false, {bt::size_t_, bt::const_char_pointer, bt::end}},
    {"strncmp",
     false,
     {bt::int_, bt::const_char_pointer, bt::const_char_pointer, bt::size_t_,
      bt::end}},
    {"strncpy",
     false,
     {bt::char_pointer, bt::char_pointer, bt::const_char_pointer, bt::size_t_,
      bt::end}},
    {"strpbrk",
     false,
     {bt::char_pointer, bt::const_char_pointer, bt::const_char_pointer,
      bt::end}},
    {"strrchr",
     false,
     {bt::char_pointer, bt::const_char_pointer, bt::int_, bt::end}},
    {"strstr",
     false,
     {bt::char_pointer, bt::const_char_pointer, bt::const_char_pointer,
      bt::end}},
    {"wcslen", false, {bt::size_t_, bt::const_wchar_pointer, bt::end}},
    {"wmemchr",
     false,
     {bt::wchar_pointer, bt::const_wchar_pointer, bt::wchar_, bt::size_t_,
      bt::end}},
    {"wmemcmp",
     false,
     {bt::int_, bt::const_wchar_pointer, bt::const_wchar_pointer, bt::size_t_,
      bt::end}},
};

#define MATH1(n)                                                               \
    {                                                                          \
        n, false,                                                              \
        {                                                                      \
            bt::real, bt::real, bt::end                                        \
        }                                                                      \
    }
#define MATH2(n)                                                               \
    {                                                                          \
        n, false,                                                              \
        {                                                                      \
            bt::real, bt::real, bt::real, bt::end                              \
        }                                                                      \
    }

/* <math.h>'s functions, each also with the suffixes f and l */
static const library_builtin_rule math_builtins[] = {
    MATH1("acos"),
    MATH1("acosh"),
    MATH1("asin"),
    MATH1("asinh"),
    MATH1("atan"),
    MATH2("atan2"),
    MATH1("atanh"),
    MATH1("cbrt"),
    MATH1("ceil"),
    MATH2("copysign"),
    MATH1("cos"),
    MATH1("cosh"),
    MATH1("erf"),
    MATH1("erfc"),
    MATH1("exp"),
    MATH1("exp2"),
    MATH1("expm1"),
    MATH1("fabs"),
    MATH2("fdim"),
    MATH1("floor"),
    {"fma", false, {bt::real, bt::real, bt::real, bt::real, bt::end}},
    MATH2("fmax"),
    MATH2("fmin"),
    MATH2("fmod"),
    {"frexp", false, {bt::real, bt::real, bt::int_pointer, bt::end}},
    MATH2("hypot"),
    {"ilogb", false, {bt::int_, bt::real, bt::end}},
    {"ldexp", false, {bt::real, bt::real, bt::int_, bt::end}},
    MATH1("lgamma"),
    {"llrint", false, {bt::long_long, bt::real, bt::end}},
    {"llround", false, {bt::long_long, bt::real, bt::end}},
    MATH1("log"),
    MATH1("log10"),
    MATH1("log1p"),
    MATH1("log2"),
    MATH1("logb"),
    {"lrint", false, {bt::long_, bt::real, bt::end}},
    {"lround", false, {bt::long_, bt::real, bt::end}},
    {"modf", false, {bt::real, bt::real, bt::real_pointer, bt::end}},
    MATH1("nearbyint"),
    MATH2("nextafter"),
    {"nexttoward", false, {bt::real, bt::real, bt::long_double, bt::end}},
    MATH2("pow"),
    MATH2("remainder"),
    {"remquo", false, {bt::real, bt::real, bt::real, bt::int_pointer, bt::end}},
    MATH1("rint"),
    MATH1("round"),
    {"scalbln", false, {bt::real, bt::real, bt::long_, bt::end}},
    {"scalbn", false, {bt::real, bt::real, bt::int_, bt::end}},
    MATH1("sin"),
    MATH1("sinh"),
    MATH1("sqrt"),
    MATH1("tan"),
    MATH1("tanh"),
    MATH1("tgamma"),
    MATH1("trunc"),
};

#undef MATH1
#undef MATH2

/* t: a builtin_type */
qualified_type semantic_analysis::builtin_parameter_type(uint8_t t,
                                                         uint8_t real)
{
    fundamental_type f = fundamental_type::void_;
    uint32_t pointee_cv = cv_none;
    bool pointer = false;
    switch (static_cast<builtin_type>(t)) {
    case builtin_type::bool_:
        f = fundamental_type::bool_;
        break;
    case builtin_type::int_:
        f = fundamental_type::int_;
        break;
    case builtin_type::float_:
        f = fundamental_type::float_;
        break;
    case builtin_type::double_:
        f = fundamental_type::double_;
        break;
    case builtin_type::long_double:
        f = fundamental_type::long_double;
        break;
    case builtin_type::long_:
        f = fundamental_type::long_int;
        break;
    case builtin_type::long_long:
        f = fundamental_type::long_long_int;
        break;
    case builtin_type::int_pointer:
        f = fundamental_type::int_;
        pointer = true;
        break;
    case builtin_type::const_wchar_pointer:
        pointee_cv = cv_const;
        [[fallthrough]];
    case builtin_type::wchar_pointer:
        pointer = true;
        [[fallthrough]];
    case builtin_type::wchar_:
        f = fundamental_type::wchar_t_;
        break;
    case builtin_type::real_pointer:
        pointer = true;
        [[fallthrough]];
    case builtin_type::real:
        f = static_cast<fundamental_type>(real);
        break;
    case builtin_type::size_t_:
        /* [support.types.layout]/3: "The type size_t is an
           implementation-defined unsigned integer type", sizeof's */
        f = fundamental_type::unsigned_long_int;
        break;
    case builtin_type::const_char_pointer:
        pointee_cv = cv_const;
        [[fallthrough]];
    case builtin_type::char_pointer:
        f = fundamental_type::char_;
        pointer = true;
        break;
    case builtin_type::const_void_pointer:
        pointee_cv = cv_const;
        [[fallthrough]];
    case builtin_type::void_pointer:
        pointer = true;
        break;
    default:
        break;
    }
    qualified_type q = qualified_type::make(type_table.fundamental(f), cv_none);
    if (!pointer)
        return q;
    q = qualified_type::make(type_table.fundamental(f), pointee_cv);
    return qualified_type::make(type_table.pointer_to(q), cv_none);
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

/* the type-generic classifications and comparisons of <math.h> (C17
   7.12.3, 7.12.14) among the C front end's builtins: their arguments
   are passed as they are, unpromoted (postfix-expression.cpp), as clang's
   custom type checking does */
static const char *const generic_builtins[] = {
    "fpclassify",
    "isfinite",
    "isgreater",
    "isgreaterequal",
    "isinf",
    "isless",
    "islessequal",
    "islessgreater",
    "isnan",
    "isnormal",
    "isunordered",
    "signbit",
    /* clang's type-generic bit counts (Builtins.td, custom type
       checking): the one argument of any unsigned integer type */
    "popcountg",
    "clzg",
    "ctzg",
};

/* __builtin_popcountg, __builtin_clzg, __builtin_ctzg: 1, 2, 3 */
static int bit_count_kind(const char *spelling, size_t length)
{
    static const char *const names[] = {"popcountg", "clzg", "ctzg"};
    for (int i = 0; i < 3; i++)
        if (length == 10 + strlen(names[i]) &&
            !memcmp(spelling + 10, names[i], length - 10))
            return i + 1;
    return 0;
}

static bool is_bit_count_builtin(const char *spelling, size_t length)
{
    return length > 10 && !memcmp(spelling, "__builtin_", 10) &&
           bit_count_kind(spelling, length) != 0;
}

static bool is_generic_builtin(const char *spelling, size_t length)
{
    for (const char *g : generic_builtins)
        if (length == 10 + strlen(g) && !memcmp(spelling + 10, g, length - 10))
            return true;
    return false;
}

/* __builtin_constant_p, __atomic_always_lock_free, __atomic_is_lock_free,
   __c11_atomic_is_lock_free: 1, 2, 3, 4 */
static int query_kind(const char *spelling, size_t length)
{
    static const char *const names[] = {
        "__builtin_constant_p", "__atomic_always_lock_free",
        "__atomic_is_lock_free", "__c11_atomic_is_lock_free"};
    for (int i = 0; i < 4; i++)
        if (length == strlen(names[i]) && !memcmp(spelling, names[i], length))
            return i + 1;
    return 0;
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

/* a builtin of vcc's C front end (frontend/c/builtin-table.cpp) whose
   signature has scalar types only, in the table's letters, or one of the
   generic_builtins: lowered to the C front end's builtin node
   (gnu_compiler_builtin) */
entity_index semantic_analysis::compiler_builtin(uint32_t name,
                                                 const char *spelling,
                                                 size_t length)
{
    const char *signature = ast_builder_builtin_signature(spelling, length);
    if (!signature)
        return 0;
    owned_array<qualified_type> types_of;
    /* a signature of other than scalar letters (a pointer to any type, a
       va_list): one function taking anything whose calls are refused */
    int query = query_kind(spelling, length);
    /* an atomic builtin's signature is C's builtin layer's ("vc": the weak
       argument a __c11_atomic_compare_exchange_* call leaves out, inserted
       there): it takes anything, atomic_builtin_result has the rules */
    int written = 0, shape = 0;
    bool atomic = ast_builder_atomic_shape(spelling, length, &written, &shape);
    for (const char *s = signature; *s; s++)
        if (query || atomic || !strchr(signature_letters, *s) ||
            (s != signature && *s == 'v')) {
            if (is_generic_builtin(spelling, length))
                break;
            uint32_t flags = gnu_compiler_builtin | gnu_untyped_builtin;
            if (query)
                flags |= gnu_query_builtin;
            return declare_builtin(
                name, name,
                qualified_type::make(
                    type_table.fundamental(fundamental_type::void_), cv_none),
                nullptr, 0, true, flags);
        }
    /* int f(...) */
    bool generic = is_generic_builtin(spelling, length);
    for (const char *s = signature; *s && !(generic && s != signature); s++) {
        const char *at = strchr(signature_letters, *s);
        /* a parameter of type void is no scalar */
        if (!at || (s != signature && *s == 'v'))
            return 0;
        types_of.append(qualified_type::make(
            type_table.fundamental(signature_types[at - signature_letters]),
            cv_none));
    }
    /* the result, then the parameters */
    uint32_t flags = gnu_compiler_builtin;
    if (is_bit_count_builtin(spelling, length))
        flags |= gnu_bit_count;
    return declare_builtin(name, name, types_of[0], types_of.data() + 1,
                           types_of.size() - 1, generic, flags);
}

bool semantic_analysis::builtin_spelled(uint32_t name) const
{
    size_t length = 0;
    const char *spelling = tokens.pp.identifier_spelling(name - 1, &length);
    return length > 10 && !memcmp(spelling, "__builtin_", 10);
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
    spelling += 10;
    length -= 10;
    /* clang's bool __builtin_is_constant_evaluated(void) (Builtins.td):
       [meta.const.eval]/1's std::is_constant_evaluated, true "if and only
       if evaluation of the call occurs within the evaluation of an
       expression or conversion that is manifestly constant-evaluated"; no
       library function: the evaluator answers true, lowering false */
    if (length == 21 && !memcmp(spelling, "is_constant_evaluated", 21)) {
        return declare_builtin(
            name, name,
            builtin_parameter_type(static_cast<uint8_t>(builtin_type::bool_)),
            nullptr, 0, false, gnu_is_constant_evaluated);
    }
    const library_builtin_rule *found = nullptr;
    for (const library_builtin_rule &b : library_builtins)
        if (strlen(b.name) == length && !memcmp(b.name, spelling, length))
            found = &b;
    fundamental_type real = fundamental_type::double_;
    for (const library_builtin_rule &b : math_builtins) {
        size_t n = strlen(b.name);
        if (n > length || memcmp(b.name, spelling, n))
            continue;
        /* NAME, or NAMEf, NAMEl; a name of the table wins (modf, modff) */
        if (n == length) {
            found = &b;
            real = fundamental_type::double_;
        } else if (n + 1 == length && !found && spelling[n] == 'f') {
            found = &b;
            real = fundamental_type::float_;
        } else if (n + 1 == length && !found && spelling[n] == 'l') {
            found = &b;
            real = fundamental_type::long_double;
        }
    }
    if (!found)
        return compiler_builtin(name, spelling - 10, length + 10);
    qualified_type result = builtin_parameter_type(
        static_cast<uint8_t>(found->types[0]), static_cast<uint8_t>(real));
    owned_array<qualified_type> parameters;
    bool variadic = false;
    for (uint32_t i = 1; found->types[i] != builtin_type::end; i++) {
        if (found->types[i] == builtin_type::ellipsis)
            variadic = true;
        else
            parameters.append(
                builtin_parameter_type(static_cast<uint8_t>(found->types[i]),
                                       static_cast<uint8_t>(real)));
    }
    uint32_t library = tokens.pp.identifier(spelling, length) + 1;
    uint32_t flags = gnu_library_function;
    if (found->noreturn)
        flags |= gnu_noreturn;
    /* bound under its __builtin_ name: the program's own declaration of
       the library function is another */
    /* clang declares a library function noexcept in C++ (the declaration
       <math.h> and <string.h> write with __THROW is of the same type) */
    return declare_builtin(name, library, result, parameters.data(),
                           parameters.size(), variadic, flags, true);
}

/* whether the spelling has the text in it */
static bool spelled_with(const char *spelling, size_t length, const char *text)
{
    return memmem(spelling, length, text, strlen(text)) != nullptr;
}

/* __builtin_va_start, __builtin_c23_va_start, __builtin_va_end,
   __builtin_va_copy: 1, 2, 3, 4; the Microsoft forms (__builtin_ms_va_*):
   5 */
static int va_list_kind(const char *spelling, size_t length)
{
    static const char *const names[] = {
        "__builtin_va_start", "__builtin_c23_va_start", "__builtin_va_end",
        "__builtin_va_copy"};
    for (int i = 0; i < 4; i++)
        if (length == strlen(names[i]) && !memcmp(spelling, names[i], length))
            return i + 1;
    if (spelled_with(spelling, length, "__builtin_ms_va_"))
        return 5;
    return 0;
}

/* __builtin_add_overflow, __builtin_sub_overflow, __builtin_mul_overflow
   and their typed forms (__builtin_sadd_overflow, __builtin_umulll_overflow,
   ...) */
static bool overflow_builtin(const char *spelling, size_t length)
{
    static const char suffix[] = "_overflow";
    if (length < sizeof suffix - 1 ||
        memcmp(spelling + length - (sizeof suffix - 1), suffix,
               sizeof suffix - 1) ||
        memcmp(spelling, "__builtin_", 10))
        return false;
    return spelled_with(spelling, length, "add") ||
           spelled_with(spelling, length, "sub") ||
           spelled_with(spelling, length, "mul");
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
semantic_analysis::va_list_builtin_result(SyntaxRef n, int which,
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
        return not_supported(n, "ext.gnu");
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
    SyntaxRef n, const char *spelling, size_t length,
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
            return not_supported(n, "ext.gnu");
        if (!enumeration_of(t) && !fundamental_type_is_integral(f)) {
            /* the typed forms convert any arithmetic operand */
            if (typed == fundamental_type::none)
                return ill_formed(n,
                                  "operand argument to overflow builtin must "
                                  "be an integer",
                                  "ext.gnu");
            return not_supported(n, "ext.gnu");
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
        return not_supported(n, "ext.gnu");
    if ((pointee.cv() & cv_const) ||
        (!enumeration_of(pointee) && !fundamental_type_is_integral(f)))
        return ill_formed(n,
                          "result argument to overflow builtin must be a "
                          "pointer to a non-const integer type",
                          "ext.gnu");
    /* (a bool result: the code generator compares the stored byte with the
       exact value, clang the one bit: not supported) */
    if (f == fundamental_type::bool_)
        return not_supported(n, "ext.gnu");
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
    if (int which = query_kind(spelling, length))
        return query_builtin_result(n, which, nodes, result);
    if (int which = va_list_kind(spelling, length)) {
        VTRY(r, va_list_builtin_result(n, which, nodes));
        result = qualified_type::make(
            type_table.fundamental(fundamental_type::void_), cv_none);
        return analysis_result::well_formed;
    }
    if (overflow_builtin(spelling, length))
        return overflow_builtin_result(n, spelling, length, nodes, result);
    int written = 0, kind = 0;
    if (!ast_builder_atomic_shape(spelling, length, &written, &kind))
        return not_supported(n, "ext.gnu");
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
            return not_supported(n, "ext.gnu");
        /* the operations that compute: an integer (a pointer for the
           additive ones) */
        bool computes = spelled_with(spelling, length, "fetch");
        bool selecting = spelled_with(spelling, length, "max") ||
                         spelled_with(spelling, length, "min") ||
                         spelled_with(spelling, length, "nand");
        if (computes && ((pointer && selecting) ||
                         (!pointer && (!fundamental_type_is_integral(f) ||
                                       f == fundamental_type::bool_))))
            return not_supported(n, "ext.gnu");
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
                return not_supported(n, "ext.gnu");
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

int semantic_analysis::query_builtin(entity_index e) const
{
    if (!(entity_table[e].gnu_flags & gnu_query_builtin))
        return 0;
    size_t length = 0;
    const char *spelling =
        tokens.pp.identifier_spelling(entity_table[e].name - 1, &length);
    return query_kind(spelling, length);
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
analysis_result
semantic_analysis::query_builtin_result(SyntaxRef n, int which,
                                        const owned_array<uint32_t> &nodes,
                                        qualified_type &result)
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
            return not_supported(n, "ext.gnu");
    }
    if (which == 1) {
        result = qualified_type::make(
            type_table.fundamental(fundamental_type::int_), cv_none);
        return analysis_result::well_formed;
    }
    fundamental_type f = fundamental_of(semantics[nodes[0]].type);
    if (!fundamental_type_is_integral(f) && f != fundamental_type::none)
        return not_supported(n, "ext.gnu");
    if (nodes.size() == 2 && !is_pointer(semantics[nodes[1]].type) &&
        fundamental_of(semantics[nodes[1]].type) !=
            fundamental_type::std_nullptr_t &&
        !fundamental_type_is_integral(fundamental_of(semantics[nodes[1]].type)))
        return not_supported(n, "ext.gnu");
    constant_value v;
    bool constant = false;
    VTRY(r, evaluate_constant(n, nodes[0], v, constant));
    if (constant && v.kind == constant_value_kind::integer) {
        bool free = false;
        if (!lock_free_decided(v.integer, free))
            return not_supported(n, "ext.gnu");
    } else if (which == 2) {
        return ill_formed(n, "the argument must be a constant integer",
                          "ext.gnu");
    }
    result = qualified_type::make(
        type_table.fundamental(fundamental_type::bool_), cv_none);
    return analysis_result::well_formed;
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
    if (!(entity_table[e].gnu_flags & gnu_bit_count))
        return 0;
    size_t length = 0;
    const char *spelling =
        tokens.pp.identifier_spelling(entity_table[e].name - 1, &length);
    return bit_count_kind(spelling, length);
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
        return not_supported(n, "ext.gnu");
    if (!fundamental_type_is_integral(f) ||
        fundamental_type_is_signed(tokens.files.behavior, f))
        return ill_formed(n,
                          "1st argument must be a scalar unsigned integer "
                          "type",
                          "ext.gnu");
    if (nodes.size() == 2) {
        fundamental_type g = fundamental_of(semantics[nodes[1]].type);
        if (g == fundamental_type::none)
            return not_supported(n, "ext.gnu");
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

/* [ext:gnu_vector_size] clang's __builtin_shufflevector(a, b, i...): a call
   of that name with nothing declared by it (the C front end's builtin) */
bool semantic_analysis::builtin_shufflevector_call(SyntaxRef n)
{
    if (n.size() != 4 || !n[0].is_identifier() ||
        !n[2].is(syntax_kind::initializer_list) ||
        !tokens.files.behavior.has(extension::gnu_library_builtins))
        return false;
    static const char spelled[] = "__builtin_shufflevector";
    uint32_t name = tokens.identifier(n[0].token());
    if (name != tokens.pp.identifier(spelled, sizeof spelled - 1) + 1)
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
    VTRY(r, list_clauses(n[2].index(), clauses));
    if (clauses.size() < 3)
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

} // namespace cplusplus
