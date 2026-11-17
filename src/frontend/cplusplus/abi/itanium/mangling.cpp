/*
 * mangling.cpp: [abi:mangling] the Itanium C++ ABI's external names, the
 * symbol of each function and variable (doc/PLAN-cxx-sema.md, S1d).
 *
 * Written against the ABI as the front end is against the standard: one
 * function per nonterminal of the mangling grammar, each quoting the
 * alternatives it produces after the citation of the section that has
 * them (bench/cxx-spec-refs.py checks the quotes against
 * docs/itanium-abi-grammar.tsv and counts the coverage).  The ABI's prose
 * is cited by label and said in our own words.  An entity whose name needs
 * an alternative not written yet has no symbol (the caller reports it
 * unsupported); nothing is guessed.
 */
#include "../../cplusplus-internal.h"
#include "frontend/cplusplus/sema.h"

namespace cplusplus
{

namespace
{

class itanium_mangler
{
  public:
    itanium_mangler(const semantic_analysis &analysis, const preprocessor &pp,
                    vbuffer *out)
        : analysis(analysis), pp(pp), out(out)
    {
    }

    bool mangled_name(entity_index e);

  private:
    const semantic_analysis &analysis;
    const preprocessor &pp;
    vbuffer *out;
    /* [abi:mangling-compression]: the substitution candidates seen so far,
       in order; a type is one candidate however it is spelled, since the
       type table has one record per type */
    owned_array<qualified_type> candidates;

    const struct type &record(qualified_type t) const
    {
        return analysis.type_table[t.unqualified()];
    }
    void text(const char *s)
    {
        vbuffer_string_append(out, s);
    }
    /* an entity's name: token_stream::identifier's number, the
       preprocessor's plus one (0 is no identifier) */
    const char *spelling(uint32_t identifier, size_t *length) const
    {
        return pp.identifier_spelling(identifier - 1, length);
    }
    bool encoding(entity_index e);
    bool name(entity_index e);
    bool unscoped_name(entity_index e);
    bool unqualified_name(entity_index e);
    bool source_name(uint32_t identifier);
    void number(uint64_t n);
    bool local_name(entity_index e, entity_index function);
    bool nested_name(entity_index e);
    bool prefix(entity_index scope);
    bool class_enum_type(qualified_type t);
    void discriminator(uint64_t n);
    bool bare_function_type(qualified_type t);
    bool type(qualified_type t);
    bool builtin_type(fundamental_type t);
    bool qualified(qualified_type t);
    bool function_type(qualified_type t);
    bool array_type(qualified_type t);
    bool substitution(qualified_type t);
    void seq_id(size_t n);
};

/* the function whose definition a block-scope entity is declared in: the
   enclosing block scopes end at its parameter scope, whose enclosing entity
   is the function (function_body); 0 for an entity not in a function */
entity_index enclosing_function(const entities &table, entity_index e)
{
    entity_index s = table[e].enclosing;
    while (s && table[s].kind == entity_kind::block_scope)
        s = table[s].enclosing;
    if (s && table[s].kind == entity_kind::function)
        return s;
    return 0;
}

/*
 * [abi:mangling-structure]:
 *
 *     <mangled-name> ::= _Z <encoding>
 *
 * The section exempts two kinds of names from mangling: those of entities
 * with C language linkage, and those of variables of the global namespace;
 * their symbol is the identifier.  main is not mangled either: its symbol
 * is the one the C run-time calls ([basic.start.main]/1 makes it the
 * program's start, and every Itanium implementation spells it main).
 */
bool itanium_mangler::mangled_name(entity_index e)
{
    const entity &x = analysis.entity_table[e];
    if (x.kind != entity_kind::variable && x.kind != entity_kind::function)
        return false;
    if (!x.name)
        return false;
    size_t length = 0;
    const char *identifier = spelling(x.name, &length);
    bool global = x.enclosing == 0;
    bool is_main = x.kind == entity_kind::function && length == 4 &&
                   !memcmp(identifier, "main", 4);
    /* [impl.clang]: a variable of the global namespace with internal
       linkage is mangled all the same (_ZL1x), so that it cannot collide
       with an extern "C" name of the translation unit; GCC writes x */
    bool internal_variable = x.kind == entity_kind::variable &&
                             x.linkage_of_name == linkage::internal;
    if (global && !internal_variable &&
        (x.kind == entity_kind::variable || is_main)) {
        vbuffer_append(out, identifier, length);
        return true;
    }
    text("_Z");
    return encoding(e);
}

/*
 * [abi:mangling-structure]:
 *
 *     <encoding> ::= <function name> <bare-function-type>
 *                ::= <data name>
 *
 * A variable's encoding has no type.
 */
bool itanium_mangler::encoding(entity_index e)
{
    const entity &x = analysis.entity_table[e];
    if (!name(e))
        return false;
    if (x.kind == entity_kind::variable)
        return true;
    return bare_function_type(x.type);
}

/*
 * [abi:mangle.name]:
 *
 *     <name> ::= <nested-name>
 *            ::= <unscoped-name>
 *            ::= <local-name>
 *
 * No namespace or template scope yet.  A member of a class has a
 * <nested-name>; an entity of a function's block scope has a <local-name>
 * ([abi:mangling-scope]).
 */
bool itanium_mangler::name(entity_index e)
{
    const entities &table = analysis.entity_table;
    if (table[e].enclosing == 0)
        return unscoped_name(e);
    if (table[table[e].enclosing].kind == entity_kind::class_type)
        return nested_name(e);
    entity_index function = enclosing_function(table, e);
    if (!function)
        return false;
    return local_name(e, function);
}

/*
 * [abi:mangle.name]:
 *
 *     <nested-name> ::= N [<CV-qualifiers>] [<ref-qualifier>] <prefix> <unqualified-name> E
 *
 * The CV-qualifiers are a non-static member function's (the ones of its
 * declarator, which qualify `this`); there is no ref-qualifier yet.
 */
bool itanium_mangler::nested_name(entity_index e)
{
    const entity &x = analysis.entity_table[e];
    text("N");
    if (x.kind == entity_kind::function && x.nonstatic_member) {
        if (x.member_cv & cv_volatile)
            text("V");
        if (x.member_cv & cv_const)
            text("K");
    }
    if (!prefix(x.enclosing))
        return false;
    if (!unqualified_name(e))
        return false;
    text("E");
    return true;
}

/*
 * [abi:mangle.name]:
 *
 *     <prefix> ::= <unqualified-name>
 *     <prefix> ::= <substitution>
 *
 * A class of the global namespace.  [abi:mangling-compression]: a prefix
 * is a substitution candidate, the class's type's: the class named again
 * as a prefix or a type is the same candidate.
 */
bool itanium_mangler::prefix(entity_index scope)
{
    const entity &c = analysis.entity_table[scope];
    if (c.kind != entity_kind::class_type || c.enclosing)
        return false;
    if (substitution(c.type))
        return true;
    if (!unqualified_name(scope))
        return false;
    candidates.append(c.type);
    return true;
}

/*
 * [abi:mangling.named]:
 *
 *     <class-enum-type> ::= <name>
 *
 * A class of the global namespace: its unscoped name.
 */
bool itanium_mangler::class_enum_type(qualified_type t)
{
    entity_index c = record(t).entity;
    const entity &x = analysis.entity_table[c];
    if (x.kind != entity_kind::class_type || x.enclosing)
        return false;
    return unscoped_name(c);
}

/*
 * [abi:mangle.name]:
 *
 *     <unscoped-name> ::= <unqualified-name>
 */
bool itanium_mangler::unscoped_name(entity_index e)
{
    return unqualified_name(e);
}

/*
 * [abi:mangle.name]:
 *
 *     <unqualified-name> ::= <source-name>
 *
 * [impl.gnu] [impl.clang] Both put L before the source name of an entity
 * of namespace scope with internal linkage (a static function), which the
 * ABI does not have, so that it is not the symbol of an external entity of
 * that name: _ZL2f2v.
 */
bool itanium_mangler::unqualified_name(entity_index e)
{
    const entity &x = analysis.entity_table[e];
    if (x.linkage_of_name == linkage::internal && x.enclosing == 0)
        text("L");
    /* [dcl.typedef]/9: an unnamed class's name for linkage purposes */
    if (!x.name && x.kind == entity_kind::class_type)
        return source_name(x.linkage_name);
    return source_name(x.name);
}

/*
 * [abi:mangle.name]:
 *
 *     <source-name> ::= <positive length number> <identifier>
 *     <identifier> ::= <unqualified source code identifier>
 *
 * The length is of the identifier's bytes (UTF-8 for one with a universal
 * character name, as clang and GCC write it).
 */
bool itanium_mangler::source_name(uint32_t identifier)
{
    if (!identifier)
        return false;
    size_t length = 0;
    const char *bytes = spelling(identifier, &length);
    number(length);
    vbuffer_append(out, bytes, length);
    return true;
}

/*
 * [abi:mangle.number]:
 *
 *     <number> ::= [n] <non-negative decimal integer>
 *
 * In decimal, without leading zeros (0 is "0"); the n of a negative
 * number is never needed here.
 */
void itanium_mangler::number(uint64_t n)
{
    vbuffer_format_append(out, "%llu", static_cast<unsigned long long>(n));
}

/*
 * [abi:mangling-scope]:
 *
 *     <local-name> ::= Z <function encoding> E <entity name> [<discriminator>]
 *
 * The entity's name is mangled as if the function were the global scope:
 * an unscoped name for a variable declared directly in it.  When several
 * entities of the function have the same name (in different blocks), the
 * section numbers them in lexical order and gives the second and later a
 * discriminator, the n-th one's number being n - 2.  The ones numbered
 * here are the function's static local variables, the only local entities
 * with a symbol in S1.
 */
bool itanium_mangler::local_name(entity_index e, entity_index function)
{
    const entities &table = analysis.entity_table;
    const entity &x = table[e];
    if (x.kind != entity_kind::variable || x.automatic)
        return false;
    text("Z");
    /* [impl.gnu] [impl.clang] the encoding of a function whose own symbol
       is not mangled (main) is its name alone, without its type:
       _ZZ4mainE1r */
    const entity &f = table[function];
    size_t length = 0;
    const char *identifier = spelling(f.name, &length);
    if (f.enclosing == 0 && length == 4 && !memcmp(identifier, "main", 4)) {
        if (!unqualified_name(function))
            return false;
    } else if (!encoding(function))
        return false;
    text("E");
    if (!unqualified_name(e))
        return false;
    uint64_t earlier = 0;
    for (entity_index other = 1; other < e; other++)
        if (table[other].kind == entity_kind::variable &&
            !table[other].automatic && table[other].name == x.name &&
            table[other].enclosing &&
            table[table[other].enclosing].kind == entity_kind::block_scope &&
            enclosing_function(table, other) == function)
            earlier++;
    if (earlier)
        discriminator(earlier - 1);
    return true;
}

/*
 * [abi:mangling-scope]:
 *
 *     <discriminator> ::= _ <non-negative number>
 *
 * The section's second form, for a number of 10 or more, puts it between
 * "__" and "_".
 */
void itanium_mangler::discriminator(uint64_t n)
{
    if (n < 10) {
        text("_");
        number(n);
        return;
    }
    text("__");
    number(n);
    text("_");
}

/*
 * [abi:mangle.function-type]:
 *
 *     <bare-function-type> ::= <signature type>+
 *
 * A non-template function's name encodes its parameter types and not its
 * return type; an empty parameter list is the single type v, and an
 * ellipsis is z after the parameters ([abi:mangling-builtin]'s z).  The
 * parameter types are those of the function's type, top-level
 * cv-qualifiers already gone ([dcl.fct]/5).
 */
bool itanium_mangler::bare_function_type(qualified_type t)
{
    const struct type &f = record(t);
    if (f.kind != type_kind::function)
        return false;
    const qualified_type *parameters =
        analysis.type_table.parameters(t.unqualified());
    for (uint32_t i = 0; i < f.parameter_count; i++)
        if (!type(parameters[i]))
            return false;
    if (f.variadic)
        text("z");
    else if (!f.parameter_count)
        text("v");
    return true;
}

/*
 * [abi:mangling-type]:
 *
 *     <type> ::= <builtin-type>
 *            ::= <qualified-type>
 *            ::= <function-type>
 *            ::= <class-enum-type>
 *            ::= <array-type>
 *            ::= P <type>
 *            ::= R <type>
 *            ::= O <type>
 *            ::= <substitution>
 *
 * P is a pointer, R an lvalue reference, O an rvalue reference.  Every
 * type but a builtin one is a substitution candidate
 * ([abi:mangling-compression]): one seen before is a <substitution>, and
 * one that is not is added after its parts, so the parts have the earlier
 * numbers.
 */
bool itanium_mangler::type(qualified_type t)
{
    const struct type &r = record(t);
    if (r.kind == type_kind::fundamental && !t.cv())
        return builtin_type(r.fundamental);
    if (substitution(t))
        return true;
    bool done = false;
    if (t.cv() && r.kind != type_kind::array)
        done = qualified(t);
    else if (r.kind == type_kind::pointer) {
        text("P");
        done = type(r.operand);
    } else if (r.kind == type_kind::lvalue_reference) {
        text("R");
        done = type(r.operand);
    } else if (r.kind == type_kind::rvalue_reference) {
        text("O");
        done = type(r.operand);
    } else if (r.kind == type_kind::function)
        done = function_type(t);
    else if (r.kind == type_kind::array)
        done = array_type(t);
    else if (r.kind == type_kind::class_type)
        done = class_enum_type(t);
    if (!done)
        return false;
    candidates.append(t);
    return true;
}

/*
 * [abi:mangling-builtin]:
 *
 *     <builtin-type> ::= v
 *                    ::= w
 *                    ::= b
 *                    ::= c
 *                    ::= a
 *                    ::= h
 *                    ::= s
 *                    ::= t
 *                    ::= i
 *                    ::= j
 *                    ::= l
 *                    ::= m
 *                    ::= x
 *                    ::= y
 *                    ::= f
 *                    ::= d
 *                    ::= e
 *                    ::= Di
 *                    ::= Ds
 *                    ::= Du
 *                    ::= Dn
 *                    ::= n
 *                    ::= o
 *
 * In the order of the section's table: void, wchar_t, bool, char, signed
 * char, unsigned char, short, unsigned short, int, unsigned int, long,
 * unsigned long, long long, unsigned long long, float, double, long double,
 * char32_t, char16_t, char8_t, std::nullptr_t, __int128, unsigned
 * __int128.  _Float16 is DF16_ (the
 * DF <number> _ form below); _BitInt(N)'s DB <number> _ needs N, which the
 * type does not keep yet.
 *
 *     <builtin-type> ::= DF <number> _
 */
bool itanium_mangler::builtin_type(fundamental_type t)
{
    switch (t) {
    case fundamental_type::void_:
        text("v");
        return true;
    case fundamental_type::wchar_t_:
        text("w");
        return true;
    case fundamental_type::bool_:
        text("b");
        return true;
    case fundamental_type::char_:
        text("c");
        return true;
    case fundamental_type::signed_char:
        text("a");
        return true;
    case fundamental_type::unsigned_char:
        text("h");
        return true;
    case fundamental_type::short_int:
        text("s");
        return true;
    case fundamental_type::unsigned_short_int:
        text("t");
        return true;
    case fundamental_type::int_:
        text("i");
        return true;
    case fundamental_type::unsigned_int:
        text("j");
        return true;
    case fundamental_type::long_int:
        text("l");
        return true;
    case fundamental_type::unsigned_long_int:
        text("m");
        return true;
    case fundamental_type::long_long_int:
        text("x");
        return true;
    case fundamental_type::unsigned_long_long_int:
        text("y");
        return true;
    case fundamental_type::int128:
        text("n");
        return true;
    case fundamental_type::unsigned_int128:
        text("o");
        return true;
    case fundamental_type::float_:
        text("f");
        return true;
    case fundamental_type::double_:
        text("d");
        return true;
    case fundamental_type::long_double:
        text("e");
        return true;
    case fundamental_type::char32_t_:
        text("Di");
        return true;
    case fundamental_type::char16_t_:
        text("Ds");
        return true;
    case fundamental_type::char8_t_:
        text("Du");
        return true;
    case fundamental_type::std_nullptr_t:
        text("Dn");
        return true;
    case fundamental_type::float16:
        text("DF");
        number(16);
        text("_");
        return true;
    default:
        return false;
    }
}

/*
 * [abi:mangle.qualified-type]:
 *
 *     <qualified-type> ::= <qualifiers> <type>
 *     <qualifiers> ::= <extended-qualifier>* <CV-qualifiers>
 *     <CV-qualifiers> ::= [r] [V] [K]
 *
 * With no extended qualifier (no vendor's).  The section orders the
 * qualifiers so K is closest to the type, and makes both the qualified
 * type and the type without its qualifiers substitution candidates, not a
 * type with only some of them.
 */
bool itanium_mangler::qualified(qualified_type t)
{
    if (t.cv() & cv_volatile)
        text("V");
    if (t.cv() & cv_const)
        text("K");
    return type(qualified_type::make(t.unqualified(), cv_none));
}

/*
 * [abi:mangle.function-type]:
 *
 *     <function-type> ::= [<CV-qualifiers>] [<exception-spec>] [Dx] F [Y] <bare-function-type> [<ref-qualifier>] E
 *
 * A function type that is not a function's name (a parameter's pointer to
 * function, say) encodes its return type first; the section treats the
 * delimited and the bare type as one for substitution.  S1's functions
 * have no qualifiers, exception specification or C language linkage (Y).
 */
bool itanium_mangler::function_type(qualified_type t)
{
    text("F");
    if (!type(record(t).operand))
        return false;
    if (!bare_function_type(t))
        return false;
    text("E");
    return true;
}

/*
 * [abi:mangle.array-type]:
 *
 *     <array-type> ::= A [<array bound number>] _ <element type>
 *
 * The bound is left out for an array of unknown bound.  cv-qualifiers on
 * an array type are its elements' ([basic.type.qualifier]/3: "An array
 * type whose elements are cv-qualified is also considered to have the same
 * cv-qualifications as its elements"), and mangled there.
 */
bool itanium_mangler::array_type(qualified_type t)
{
    const struct type &r = record(t);
    text("A");
    if (r.bound_known)
        number(r.bound);
    text("_");
    qualified_type element = r.operand;
    if (t.cv())
        element =
            qualified_type::make(element.unqualified(), element.cv() | t.cv());
    return type(element);
}

/*
 * [abi:mangling-compression]:
 *
 *     <substitution> ::= S <seq-id> _
 *                    ::= S_
 *
 * The first candidate is S_, the (n+2)-th S <n in base 36> _.
 */
bool itanium_mangler::substitution(qualified_type t)
{
    for (size_t i = 0; i < candidates.size(); i++)
        if (candidates[i] == t) {
            text("S");
            if (i)
                seq_id(i - 1);
            text("_");
            return true;
        }
    return false;
}

/*
 * [abi:mangle.seq-id]:
 *
 *     <seq-id> ::= <0-9A-Z>+
 *
 * Base 36 with upper case letters, no leading zero.
 */
void itanium_mangler::seq_id(size_t n)
{
    char digits[16];
    size_t count = 0;
    do {
        unsigned d = static_cast<unsigned>(n % 36);
        char c = static_cast<char>('0' + d);
        if (d >= 10)
            c = static_cast<char>('A' + d - 10);
        digits[count++] = c;
        n /= 36;
    } while (n);
    while (count)
        vbuffer_append(out, &digits[--count], 1);
}

} // namespace

bool itanium_symbol_name(const semantic_analysis &analysis,
                         const preprocessor &pp, entity_index e, vbuffer *out)
{
    itanium_mangler m(analysis, pp, out);
    return m.mangled_name(e);
}

} // namespace cplusplus
