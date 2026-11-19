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

/* name is a builtin of vcc's C front end, which clang declares at its
   first use (a function's name to the parser) */
bool parser::names_compiler_builtin(uint32_t name)
{
    size_t length = 0;
    /* the identifier's number is the preprocessor's plus one */
    const char *spelling = tokens.pp.identifier_spelling(name - 1, &length);
    return ast_builder_builtin_signature(spelling, length) != nullptr;
}

/* the function name names, declared at its first use with C language
   linkage and the GNU flags given */
entity_index
semantic_analysis::declare_builtin(uint32_t name, uint32_t symbol,
                                   qualified_type result,
                                   const qualified_type *parameters,
                                   size_t count, bool variadic, uint32_t flags)
{
    entity_index e = entity_table.make(entity_kind::function, symbol, 0);
    entity_table[e].type = qualified_type::make(
        type_table.function(result, parameters, count, variadic), cv_none);
    entity_table[e].linkage_of_name = linkage::external;
    entity_table[e].c_language_linkage = true;
    entity_table[e].gnu_flags = flags;
    entity_table.bind(0, name, e);
    return e;
}

/* the type-generic classifications and comparisons of <math.h> (C17
   7.12.3, 7.12.14) among the C front end's builtins: their arguments
   are passed as they are, unpromoted (expr.post.cpp), as clang's custom
   type checking does */
static const char *const generic_builtins[] = {
    "fpclassify", "isfinite", "isgreater",   "isgreaterequal",
    "isinf",      "isless",   "islessequal", "islessgreater",
    "isnan",      "isnormal", "isunordered", "signbit",
};

static bool is_generic_builtin(const char *spelling, size_t length)
{
    for (const char *g : generic_builtins)
        if (length == 10 + strlen(g) && !memcmp(spelling + 10, g, length - 10))
            return true;
    return false;
}

/* a builtin of vcc's C front end (frontend/c/builtin-table.cpp) whose
   signature has scalar types only, in the table's letters, or one of the
   generic_builtins: lowered to the C front end's builtin node
   (gnu_compiler_builtin) */
entity_index semantic_analysis::compiler_builtin(uint32_t name,
                                                 const char *spelling,
                                                 size_t length)
{
    static const char letters[] = "vbcChiulLwWzfdD";
    static const fundamental_type types[] = {
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
    const char *signature = ast_builder_builtin_signature(spelling, length);
    if (!signature)
        return 0;
    owned_array<qualified_type> types_of;
    /* int f(...) */
    bool generic = is_generic_builtin(spelling, length);
    for (const char *s = signature; *s && !(generic && s != signature); s++) {
        const char *at = strchr(letters, *s);
        /* a parameter of type void is no scalar */
        if (!at || (s != signature && *s == 'v'))
            return 0;
        types_of.append(qualified_type::make(
            type_table.fundamental(types[at - letters]), cv_none));
    }
    /* the result, then the parameters */
    return declare_builtin(name, name, types_of[0], types_of.data() + 1,
                           types_of.size() - 1, generic, gnu_compiler_builtin);
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
    uint32_t flags = 0;
    if (found->noreturn)
        flags = gnu_noreturn;
    /* bound under its __builtin_ name: the program's own declaration of
       the library function is another */
    return declare_builtin(name, library, result, parameters.data(),
                           parameters.size(), variadic, flags);
}

/* clang's T *__builtin_addressof(T &) (Builtins.td, custom type checking):
   [specialized.addressof]/1's std::addressof, "the actual address of the
   object or function referenced by r, even in the presence of an
   overloaded operator&"; a call of the name unqualified lookup finds
   nothing for, with one argument */
bool semantic_analysis::builtin_addressof_call(node_index n)
{
    auto node = syntax(n);
    if (node.size() != 4 || !node[0].is_identifier() ||
        node[2].is(syntax_kind::initializer_list) ||
        !tokens.files.behavior.has(extension::gnu_library_builtins))
        return false;
    static const char spelled[] = "__builtin_addressof";
    uint32_t name = tokens.identifier(node[0].token());
    if (name != tokens.pp.identifier(spelled, sizeof spelled - 1) + 1)
        return false;
    lookup_result x = lookup_unqualified(name, lookup_filter::any);
    return !x.entity && !x.unknown && !x.ambiguous;
}

/* the built-in & of [expr.unary.op]/3.2 on the lvalue operand: a prvalue
   "pointer to T" (cv kept), constant where &x is ([expr.const]/11) */
analysis_result semantic_analysis::builtin_addressof(node_index n,
                                                     expression_meaning &m)
{
    syntax_child operand = syntax(n)[2];
    analysis_result r = expression(operand, m);
    if (r != analysis_result::well_formed)
        return r;
    if (m.category != value_category::lvalue)
        return ill_formed(n,
                          "the operand of __builtin_addressof shall be an "
                          "lvalue",
                          "specialized.addressof");
    if (m.bit_field_width)
        return address_of_bit_field(syntax(n).first_token());
    address_of_glvalue(syntax(n).first_token(), m);
    return analysis_result::well_formed;
}

} // namespace cplusplus
