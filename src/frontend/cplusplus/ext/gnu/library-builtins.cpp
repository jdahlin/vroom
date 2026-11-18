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
    {"fabs", false, {bt::double_, bt::double_, bt::end}},
    {"fabsf", false, {bt::float_, bt::float_, bt::end}},
    {"fabsl", false, {bt::long_double, bt::long_double, bt::end}},
    {"ffs", false, {bt::int_, bt::int_, bt::end}},
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
};

/* t: a builtin_type */
qualified_type semantic_analysis::builtin_parameter_type(uint8_t t)
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

entity_index semantic_analysis::library_builtin(uint32_t name)
{
    if (!tokens.files.behavior.has(extension::gnu_library_builtins))
        return 0;
    size_t length = 0;
    /* the identifier's number is the preprocessor's plus one */
    const char *spelling = tokens.pp.identifier_spelling(name - 1, &length);
    if (length <= 10 || memcmp(spelling, "__builtin_", 10))
        return 0;
    spelling += 10;
    length -= 10;
    /* clang's bool __builtin_is_constant_evaluated(void) (Builtins.td):
       [meta.const.eval]/1's std::is_constant_evaluated, true "if and only
       if evaluation of the call occurs within the evaluation of an
       expression or conversion that is manifestly constant-evaluated"; no
       library function: the evaluator answers true, lowering false */
    if (length == 21 && !memcmp(spelling, "is_constant_evaluated", 21)) {
        qualified_type type = qualified_type::make(
            type_table.function(builtin_parameter_type(
                                    static_cast<uint8_t>(builtin_type::bool_)),
                                nullptr, 0, false),
            cv_none);
        entity_index e = entity_table.make(entity_kind::function, name, 0);
        entity_table[e].type = type;
        entity_table[e].linkage_of_name = linkage::external;
        entity_table[e].c_language_linkage = true;
        entity_table[e].gnu_flags = gnu_is_constant_evaluated;
        entity_table.bind(0, name, e);
        return e;
    }
    const library_builtin_rule *found = nullptr;
    for (const library_builtin_rule &b : library_builtins)
        if (strlen(b.name) == length && !memcmp(b.name, spelling, length))
            found = &b;
    if (!found)
        return 0;
    qualified_type result =
        builtin_parameter_type(static_cast<uint8_t>(found->types[0]));
    owned_array<qualified_type> parameters;
    bool variadic = false;
    for (uint32_t i = 1; found->types[i] != builtin_type::end; i++) {
        if (found->types[i] == builtin_type::ellipsis)
            variadic = true;
        else
            parameters.append(
                builtin_parameter_type(static_cast<uint8_t>(found->types[i])));
    }
    qualified_type type =
        qualified_type::make(type_table.function(result, parameters.data(),
                                                 parameters.size(), variadic),
                             cv_none);
    uint32_t library = tokens.pp.identifier(spelling, length) + 1;
    entity_index e = entity_table.make(entity_kind::function, library, 0);
    entity_table[e].type = type;
    entity_table[e].linkage_of_name = linkage::external;
    entity_table[e].c_language_linkage = true;
    if (found->noreturn)
        entity_table[e].gnu_flags = gnu_noreturn;
    /* bound under its __builtin_ name: the program's own declaration of
       the library function is another */
    entity_table.bind(0, name, e);
    return e;
}

} // namespace cplusplus
