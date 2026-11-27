/*
 * mangling/: [abi:mangling] the Itanium C++ ABI's external names, the
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
 *
 * The files are by the ABI's sections (doc/PLAN-mangling.md 3.1); this header
 * is what they share: the tables of letters, the mangler's class.
 */
#ifndef VCC_CPLUSPLUS_ABI_ITANIUM_MANGLING_INTERNAL_H
#define VCC_CPLUSPLUS_ABI_ITANIUM_MANGLING_INTERNAL_H

#include "../../../cplusplus-internal.h"
#include "frontend/cplusplus/sema.h"

namespace cplusplus
{

/* [abi:mangling-operator]: one enumerator per row of operators.inc */
enum class itanium_operator : uint8_t {
#define ITANIUM_OPERATOR(name, code, arity, token) name,
#include "operators.inc"
#undef ITANIUM_OPERATOR
    none
};

constexpr struct {
    const char *code;
    uint8_t arity;
    preprocessing_op_or_punc token;
} operator_encodings[] = {
#define ITANIUM_OPERATOR(name, code, arity, token)                             \
    {code, arity, preprocessing_op_or_punc::token},
#include "operators.inc"
#undef ITANIUM_OPERATOR
};

inline const char *operator_code(itanium_operator o)
{
    return operator_encodings[static_cast<size_t>(o)].code;
}

/* the operator written with op that has arity operands; none for another */
inline itanium_operator operator_written(preprocessing_op_or_punc op,
                                         unsigned arity)
{
    for (size_t i = 0;
         i < sizeof operator_encodings / sizeof *operator_encodings; i++)
        if (operator_encodings[i].token == op &&
            operator_encodings[i].token != preprocessing_op_or_punc::count &&
            operator_encodings[i].arity == arity)
            return static_cast<itanium_operator>(i);
    return itanium_operator::none;
}

/* the same for a token of the syntax tree (an alternative token is its
   primary one's) */
inline itanium_operator operator_written(SyntaxElement op, unsigned arity)
{
    for (size_t i = 0;
         i < sizeof operator_encodings / sizeof *operator_encodings; i++)
        if (operator_encodings[i].token != preprocessing_op_or_punc::count &&
            operator_encodings[i].arity == arity &&
            op.is(operator_encodings[i].token))
            return static_cast<itanium_operator>(i);
    return itanium_operator::none;
}

/* the <unary operator-name> of a prefix operator of an expression:
   [abi:expressions] writes prefix ++ and -- as pp_ and mm_, which are not
   mangled yet, and co_await is a keyword */
inline itanium_operator prefix_operator(itanium_operator o)
{
    if (o == itanium_operator::increment || o == itanium_operator::decrement)
        return itanium_operator::none;
    return o;
}

/* [abi:mangling-builtin]: builtin-types.inc's code of t; nullptr for a
   type that has none */
inline const char *builtin_type_code(fundamental_type t)
{
    switch (t) {
#define ITANIUM_BUILTIN_TYPE(type, code)                                       \
    case fundamental_type::type:                                               \
        return code;
#include "builtin-types.inc"
#undef ITANIUM_BUILTIN_TYPE
    default:
        return nullptr;
    }
}

/* [dcl.type.simple]: the type of a simple-type-specifier that is one
   keyword alone (unsigned(4), int()); none for another keyword */
inline fundamental_type simple_type_keyword_alone(standard_keyword k)
{
    static const struct {
        standard_keyword keyword;
        fundamental_type type;
    } alone[] = {
        {standard_keyword::void_, fundamental_type::void_},
        {standard_keyword::bool_, fundamental_type::bool_},
        {standard_keyword::char_, fundamental_type::char_},
        {standard_keyword::wchar_t_, fundamental_type::wchar_t_},
        {standard_keyword::char8_t_, fundamental_type::char8_t_},
        {standard_keyword::char16_t_, fundamental_type::char16_t_},
        {standard_keyword::char32_t_, fundamental_type::char32_t_},
        {standard_keyword::short_, fundamental_type::short_int},
        {standard_keyword::int_, fundamental_type::int_},
        {standard_keyword::signed_, fundamental_type::int_},
        {standard_keyword::unsigned_, fundamental_type::unsigned_int},
        {standard_keyword::long_, fundamental_type::long_int},
        {standard_keyword::float_, fundamental_type::float_},
        {standard_keyword::double_, fundamental_type::double_},
    };
    if (k != standard_keyword::none)
        for (const auto &a : alone)
            if (k == a.keyword)
                return a.type;
    return fundamental_type::none;
}

class itanium_mangler
{
  public:
    itanium_mangler(const semantic_analysis &analysis, const preprocessor &pp,
                    vbuffer *out, int variant)
        : analysis(analysis), pp(pp), out(out), variant(variant)
    {
    }

    bool mangled_name(entity_index e);
    /* the first construct the mangler had no rule for (refuse), and the
       part of the symbol it was in; nullptr none */
    const char *refused = nullptr;
    const char *refused_in = nullptr;
    owned_buffer refused_text;
    /* [abi:mangling-special-vtables]: <special-name> ::= TI <type>, the
       std::type_info object of typeid(<type>) */
    bool typeinfo_name(type_index t, int what = 0)
    {
        /* [abi:mangling-special-vtables]: TS <type>, the name string
           the object points to; what 2: the <type> alone, the
           string's characters */
        if (what == 0)
            text("_ZTI");
        else if (what == 1)
            text("_ZTS");
        return type(qualified_type::make(t, cv_none));
    }
    /* [abi:mangling-special-temporaries]:
           <special-name> ::= GR <object name> _
                          ::= GR <object name> <seq-id> _
       the object name a variable's encoding (its <data name>), the
       first temporary without a seq-id, the next ones 0, 1, ... */
    bool reference_temporary_name(entity_index e, uint32_t index)
    {
        if (analysis.entity_table[e].kind != entity_kind::variable)
            return false;
        text("_ZGR");
        if (!encoding(e))
            return false;
        if (index)
            seq_id(index - 1);
        text("_");
        return true;
    }
    /* [abi:mangling-special-vtables]: <special-name> ::= TV <type> */
    bool vtable_name(entity_index c)
    {
        text("_ZTV");
        return type(analysis.entity_table[c].type);
    }
    /* [abi:mangling-special-vtables]: <special-name> ::= TT <type>, the VTT
       ([abi:vtable-ctor-vtt]) */
    bool vtt_name(entity_index c)
    {
        text("_ZTT");
        return type(analysis.entity_table[c].type);
    }
    /* [[impl.clang]] a construction virtual table ([abi:vtable-ctor]):
       TC <type> <offset> _ <base type>, the base's at offset in c */
    bool construction_vtable_name(entity_index c, uint64_t offset,
                                  entity_index base)
    {
        text("_ZTC");
        if (!type(analysis.entity_table[c].type))
            return false;
        vbuffer_format_append(out, "%llu_",
                              static_cast<unsigned long long>(offset));
        return type(analysis.entity_table[base].type);
    }

    /* [abi:mangling-special-guards]: <special-name> ::= GV <object name>,
       the guard variable of a variable's initialization, the object name
       its encoding */
    bool guard_variable_name(entity_index e)
    {
        const entity &x = analysis.entity_table[e];
        if (x.kind != entity_kind::variable)
            return refuse("a guard variable of an entity that is no variable");
        text("_ZGV");
        return encoding(e);
    }
    /* [abi:mangling-special-thunks]:
           <special-name> ::= T <call-offset> <base encoding>
           <call-offset> ::= h <nv-offset> _
                         ::= v <v-offset> _
           <nv-offset> ::= <offset number>
           <v-offset> ::= <offset number> _ <virtual offset number>
       a this-adjusting thunk of function f (its variant): h and the
       adjustment, or v, the adjustment and the vcall offset's offset in
       the virtual table */
    bool thunk_name(entity_index f, int64_t adjustment, int64_t vcall)
    {
        text("_ZT");
        if (vcall) {
            text("v");
            signed_number(adjustment);
            text("_");
            signed_number(vcall);
        } else {
            text("h");
            signed_number(adjustment);
        }
        text("_");
        return encoding(f);
    }
    /* [abi:rtti-layout]: the virtual table of the run-time library's
       class __cxxabiv1::NAME (a std::type_info subclass), TV and its
       <nested-name> */
    void runtime_vtable_name(const char *name)
    {
        text("_ZTVN");
        source_name_text("__cxxabiv1");
        source_name_text(name);
        text("E");
    }

  private:
    const semantic_analysis &analysis;
    const preprocessor &pp;
    vbuffer *out;
    /* a constructor's or destructor's: 1 complete, 2 base object */
    int variant;
    /* [abi:mangling-compression]: the substitution candidates seen so far,
       in order; a type is one candidate however it is spelled, since the
       type table has one record per type.  A namespace as a <prefix> is a
       candidate too: its entity, with no type. A member function type
       also carries the class that owns the member. */
    struct candidate {
        qualified_type type;
        entity_index scope = 0;
        entity_index member_class = 0;
        /* Dp <type>, the expansion of the pattern type */
        bool expansion = false;
    };
    owned_array<candidate> candidates;
    bool own_pack_expansion(qualified_type pattern);
    /* the function template whose specialization's encoding is being
       mangled: what its function parameters' names are */
    entity_index encoding_template = 0;
    void add_candidate(qualified_type t, entity_index member_class = 0)
    {
        candidate c;
        c.type = t;
        c.member_class = member_class;
        candidates.append(c);
    }

    const struct type &record(qualified_type t) const
    {
        return analysis.type_table[t.unqualified()];
    }
    void text(const char *s)
    {
        vbuffer_string_append(out, s);
    }
    void text(itanium_operator o)
    {
        text(operator_code(o));
    }
    /* an entity's name: token_stream::identifier's number, the
       preprocessor's plus one (0 is no identifier) */
    const char *spelling(uint32_t identifier, size_t *length) const
    {
        return pp.identifier_spelling(identifier - 1, length);
    }
    bool encoding(entity_index e);
    bool specialization_encoding(entity_index e);
    bool constraint_expression(entity_index t, SyntaxElement c);
    bool integer_literal(token_index token);
    bool enumerator_literal(entity_index e);
    bool constraint_parameter(entity_index t, entity_index parameter);
    uint64_t constraint_level(entity_index t) const;
    bool type_constraint_concept(entity_index t, SyntaxElement q);
    /* while a type analysed in the parameter scope of a member template of
       a specialization is mangled: that template, whose own parameters the
       type's template parameter types are */
    entity_index leveled_template = 0;
    bool constraint_template_args(entity_index t, SyntaxElement c);
    bool compound_requirement(entity_index t, SyntaxElement req);
    bool constraint_namespace_member(entity_index t, SyntaxElement qualifier,
                                     SyntaxElement id);
    bool dependent_expression(SyntaxElement c);
    bool dependent_fold(SyntaxElement fold);
    bool structure_expression(uint32_t s);
    bool named_variable(entity_index t, SyntaxElement name);
    const semantic_analysis::written_binding *
    written_object(SyntaxElement name) const;
    /* the dependent_written type being mangled: its bindings
       (sema.h: written_bindings), and the element of the packs among them
       a fold's pattern is mangled for */
    uint32_t written_first = 0, written_count = 0;
    uint32_t pack_element = UINT32_MAX;
    const semantic_analysis::written_binding *bound(SyntaxElement name) const;
    bool type_operand(SyntaxElement a);
    bool written_template_args(SyntaxElement id, bool &dependent);
    bool written_expansion(SyntaxElement a, bool &expanded);
    int unresolved_qualifier(SyntaxElement q);
    bool operand_type(SyntaxElement a, qualified_type &t,
                      fundamental_type &keyword_type);
    /* (set by type_operand when a type it mangled is dependent) */
    bool operand_dependent = false;
    /* the part of the symbol being mangled, for refuse */
    const char *mangling_part = "the name";
    bool refuse(const char *what);
    void refuse_expression(SyntaxElement x);
    bool dependent_expression_of(SyntaxElement c);
    /* an initializer-list's expressions, a pattern followed by ... as
       sp <expression> */
    bool expression_list(SyntaxElement list);
    /* (the dependent values being mangled, dependent_value) */
    uint32_t dependent_value_depth = 0;
    bool punctuator(SyntaxElement c, preprocessing_op_or_punc op) const
    {
        return c.is(op);
    }
    bool class_template_args(entity_index c);
    bool template_args(entity_index c, entity_index function = 0,
                       bool pack_brackets = true, uint32_t limit = UINT32_MAX);
    bool dependent_value(entity_index d);
    bool dependent_value_expr(entity_index d);
    bool template_arg(entity_index c, uint32_t i, uint32_t element);
    bool template_arg_of(entity_index c, uint32_t i, uint32_t element);
    bool template_template_arg(entity_index t, uint32_t parameter);
    /* (the <template-param>s written so far; an alias template's
       specialization in a value-dependent argument was refused, and is to
       be written as its type: parameter_type) */
    uint64_t template_params_written = 0;
    bool alias_value_refused = false, alias_value_trial = false;
    bool parameter_type(entity_index c, uint32_t i, qualified_type declared);
    void template_param(uint64_t position)
    {
        template_params_written++;
        text("T");
        if (position)
            number(position - 1);
        text("_");
    }
    bool name(entity_index e);
    bool unscoped_name(entity_index e);
    bool unqualified_name(entity_index e, bool as_type = false);
    bool unqualified_name_untagged(entity_index e, bool as_type);
    bool source_name(uint32_t identifier);
    bool operator_name(const entity &x);
    bool ctor_dtor_name(special_member which, entity_index inherited = 0);
    void number(uint64_t n);
    /* [abi:mangle.number]'s n before a negative one's magnitude */
    void signed_number(int64_t n);
    /* a <source-name> of an identifier the mangler knows by its spelling */
    void source_name_text(const char *identifier);
    bool local_name(entity_index e, entity_index function);
    bool closure_type_name(entity_index c);
    bool translation_unit_number(entity_index c);
    bool closure_numbered(entity_index c) const;
    entity_index local_function(entity_index e,
                                entity_index *local_class = nullptr) const;
    uint64_t local_class_ordinal(entity_index c, entity_index function) const;
    void local_class_discriminator(entity_index c, entity_index function);
    bool function_scope(entity_index function);
    bool nested_name(entity_index e);
    void member_qualifiers(const entity &x);
    bool prefix(entity_index scope);
    bool class_enum_type(qualified_type t);
    bool std_of_char(qualified_type t, const char *name, size_t n) const;
    const char *std_abbreviation(entity_index c) const;
    bool dependent_member_name(entity_index e);
    bool dependent_member_prefix(type_index q);
    void discriminator(uint64_t n);
    bool bare_function_type(qualified_type t);
    bool type(qualified_type t, entity_index member_class = 0);
    bool builtin_type(fundamental_type t);
    bool qualified(qualified_type t);
    bool function_type(qualified_type t);
    bool array_type(qualified_type t);
    bool substitution(qualified_type t, entity_index member_class = 0);
    bool same_candidate(qualified_type a, qualified_type b) const;
    bool substitution(entity_index scope);
    void substitution_at(size_t i);
    void seq_id(size_t n);
    bool is_std(entity_index scope) const;
    bool in_inline_namespace(entity_index e) const;
    bool in_unnamed_namespace(entity_index e) const;
};

} // namespace cplusplus

/*
 * Itanium C++ ABI §5.1 grammar reference
 * https://itanium-cxx-abi.github.io/cxx-abi/abi.html#mangling
 *
 * This reference includes unsupported alternatives such as transaction-safe
 * entry points and dependent exception specifications. The production
 * comments beside the emitters identify the alternatives handled there.
 * The ABI text also specifies when to select each alternative.
 *
 * := introduces a production, and | introduces another alternative.
 * Brackets make a term optional. A star allows zero or more repetitions,
 * and a plus requires at least one. Spaces separate terms in the grammar
 * and contribute no bytes to the encoded name.
 *
 * 5.1.2 General Structure
 *   <mangled-name> := _Z <encoding>
 *     | _Z <encoding> . <vendor-specific suffix>
 *   <encoding> := <function name> <bare-function-type>
 *     | <data name>
 *     | <special-name>
 *
 *  Names
 *   <name> := <nested-name>
 *     | <unscoped-name>
 *     | <unscoped-template-name> <template-args>
 *     | <local-name>
 *   <unscoped-name> := <unqualified-name>
 *     | St <unqualified-name>
 *   <unscoped-template-name> := <unscoped-name>
 *     | <substitution>
 *   <nested-name> := N [<CV-qualifiers>] [<ref-qualifier>] <prefix> <unqualified-name> E
 *     | N [<CV-qualifiers>] [<ref-qualifier>] <template-prefix> <template-args> E
 *   <prefix> := <unqualified-name>
 *     | <prefix> <unqualified-name>
 *     | <template-prefix> <template-args>
 *     | <closure-prefix>
 *     | <template-param>
 *     | <decltype>
 *     | <substitution>
 *   <template-prefix> := <template unqualified-name>
 *     | <prefix> <template unqualified-name>
 *     | <template-param>
 *     | <substitution>
 *   <unqualified-name> := <operator-name> [<abi-tags>]
 *     | <ctor-dtor-name>
 *     | <source-name>
 *     | <unnamed-type-name>
 *     | DC <source-name>+ E
 *   <source-name> := <positive length number> <identifier>
 *   <identifier> := <unqualified source code identifier>
 *
 *  ABI tags
 *   <abi-tags> := <abi-tag> [<abi-tags>]
 *   <abi-tag> := B <source-name>
 *
 *  Numbers
 *   <number> := [n] <non-negative decimal integer>
 *
 *  Sequence numbers
 *   <seq-id> := <0-9A-Z>+
 *
 * 5.1.3 Operator Encodings
 *   <operator-name> := nw
 *     | na
 *     | dl
 *     | da
 *     | aw
 *     | ps
 *     | ng
 *     | ad
 *     | de
 *     | co
 *     | pl
 *     | mi
 *     | ml
 *     | dv
 *     | rm
 *     | an
 *     | or
 *     | eo
 *     | aS
 *     | pL
 *     | mI
 *     | mL
 *     | dV
 *     | rM
 *     | aN
 *     | oR
 *     | eO
 *     | ls
 *     | lS
 *     | eq
 *     | ne
 *     | lt
 *     | le
 *     | ss
 *     | nt
 *     | aa
 *     | oo
 *     | pp
 *     | mm
 *     | cm
 *     | pm
 *     | pt
 *     | cl
 *     | ix
 *     | qu
 *     | cv <type>
 *     | li <source-name>
 *     | v <digit> <source-name>
 *
 * 5.1.4.1 Virtual Tables and RTTI
 *   <special-name> := TV <type>
 *     | TT <type>
 *     | TI <type>
 *     | TS <type>
 *
 * 5.1.4.2 Virtual Override Thunks
 *   <special-name> := T <call-offset> <base encoding>
 *   <call-offset> := h <nv-offset> _
 *     | v <v-offset> _
 *   <nv-offset> := <offset number>
 *   <v-offset> := <offset number> _ <virtual offset number>
 *   <special-name> := Tc <call-offset> <call-offset> <base encoding>
 *
 * 5.1.4.3 Constructors and Destructors
 *   <ctor-dtor-name> := C1
 *     | C2
 *     | C3
 *     | CI1 <base class type>
 *     | CI2 <base class type>
 *     | D0
 *     | D1
 *     | D2
 *
 * 5.1.4.4 Guard Variables
 *   <special-name> := GV <object name>
 *
 * 5.1.4.5 Lifetime-Extended Temporaries
 *   <special-name> := GR <object name> _
 *     | GR <object name> <seq-id> _
 *
 * 5.1.4.6 Transaction-Safe Function
 *   <special-name> := GTt <encoding>
 *
 * 5.1.5 Type encodings
 *   <type> := <builtin-type>
 *     | <qualified-type>
 *     | <function-type>
 *     | <class-enum-type>
 *     | <array-type>
 *     | <pointer-to-member-type>
 *     | <template-param>
 *     | <template-template-param> <template-args>
 *     | <decltype>
 *     | P <type>
 *     | R <type>
 *     | O <type>
 *     | C <type>
 *     | G <type>
 *     | <substitution>
 *
 * 5.1.5.1 Qualified types
 *   <qualified-type> := <qualifiers> <type>
 *   <qualifiers> := <extended-qualifier>* <CV-qualifiers>
 *   <extended-qualifier> := U <source-name> [<template-args>]
 *   <CV-qualifiers> := [r] [V] [K]
 *   <ref-qualifier> := R
 *     | O
 *
 * 5.1.5.2 Builtin types
 *   <builtin-type> := v
 *     | w
 *     | b
 *     | c
 *     | a
 *     | h
 *     | s
 *     | t
 *     | i
 *     | j
 *     | l
 *     | m
 *     | x
 *     | y
 *     | n
 *     | o
 *     | f
 *     | d
 *     | e
 *     | g
 *     | z
 *     | Dd
 *     | De
 *     | Df
 *     | Dh
 *     | DF <number> _
 *     | DF <number> x
 *     | DF16b
 *     | DB <number> _
 *     | DB <instantiation-dependent expression> _
 *     | DU <number> _
 *     | DU <instantiation-dependent expression> _
 *     | Di
 *     | Ds
 *     | Du
 *     | Da
 *     | Dc
 *     | Dn
 *     | [DS] DA
 *     | [DS] DR
 *     | u <source-name> [<template-args>]
 *
 * 5.1.5.3 Function types
 *   <function-type> := [<CV-qualifiers>] [<exception-spec>] [Dx] F [Y] <bare-function-type> [<ref-qualifier>] E
 *   <bare-function-type> := <signature type>+
 *   <exception-spec> := Do
 *     | DO <expression> E
 *     | Dw <type>+ E
 *   <type> := Dp <type>
 *
 * 5.1.5.4 C++11 decltype
 *   <decltype> := Dt <expression> E
 *     | DT <expression> E
 *
 * 5.1.5.5 Class, union, and enum types
 *   <class-enum-type> := <name>
 *     | Ts <name>
 *     | Tu <name>
 *     | Te <name>
 *   <unnamed-type-name> := Ut [ <nonnegative number> ] _
 *
 * 5.1.5.6 Array types
 *   <array-type> := A [<array bound number>] _ <element type>
 *     | A <instantiation-dependent array bound expression> _ <element type>
 *
 * 5.1.5.7 Pointer-to-member types
 *   <pointer-to-member-type> := M <class type> <member type>
 *
 * 5.1.5.8 Template parameters
 *   <template-param> := T_
 *     | T <parameter-2 non-negative number> _
 *   <template-template-param> := <template-param>
 *     | <substitution>
 *
 * 5.1.5.9 Function parameter references
 *   <function-param> := fp <top-level CV-qualifiers> _
 *     | fp <top-level CV-qualifiers> <parameter-2 non-negative number> _
 *     | fL <L-1 non-negative number> p <top-level CV-qualifiers> _
 *     | fL <L-1 non-negative number> p <top-level CV-qualifiers>
 *     | fpT
 *
 * 5.1.5.10 Template Arguments
 *   <template-args> := I <template-arg>+ E
 *   <template-arg> := <type>
 *     | X <expression> E
 *     | <expr-primary>
 *     | J <template-arg>* E
 *
 * 5.1.6 Expressions
 *   <expression> := <unary operator-name> <expression>
 *     | <binary operator-name> <expression> <expression>
 *     | <ternary operator-name> <expression> <expression> <expression>
 *     | pp_ <expression>
 *     | mm_ <expression>
 *     | cl <expression>+ E
 *     | cp <base-unresolved-name> <expression>* E
 *     | cv <type> <expression>
 *     | cv <type> _ <expression>* E
 *     | tl <type> <braced-expression>* E
 *     | il <braced-expression>* E
 *     | [gs] nw <expression>* _ <type> E
 *     | [gs] nw <expression>* _ <type> <initializer>
 *     | [gs] na <expression>* _ <type> E
 *     | [gs] na <expression>* _ <type> <initializer>
 *     | [gs] dl <expression>
 *     | [gs] da <expression>
 *     | dc <type> <expression>
 *     | sc <type> <expression>
 *     | cc <type> <expression>
 *     | rc <type> <expression>
 *     | ti <type>
 *     | te <expression>
 *     | st <type>
 *     | sz <expression>
 *     | at <type>
 *     | az <expression>
 *     | nx <expression>
 *     | <template-param>
 *     | <function-param>
 *     | dt <expression> <unresolved-name>
 *     | pt <expression> <unresolved-name>
 *     | ds <expression> <expression>
 *     | sZ <template-param>
 *     | sZ <function-param>
 *     | sP <template-arg>* E
 *     | sp <expression>
 *     | fl <binary operator-name> <expression>
 *     | fr <binary operator-name> <expression>
 *     | fL <binary operator-name> <expression> <expression>
 *     | fR <binary operator-name> <expression> <expression>
 *     | tw <expression>
 *     | tr
 *     | u <source-name> <template-arg>* E
 *     | <unresolved-name>
 *     | <expr-primary>
 *   <unresolved-name> := [gs] <base-unresolved-name>
 *     | sr <unresolved-type> <base-unresolved-name>
 *     | srN <unresolved-type> <unresolved-qualifier-level>+ E <base-unresolved-name>
 *     | [gs] sr <unresolved-qualifier-level>+ E <base-unresolved-name>
 *   <unresolved-type> := <template-param> [ <template-args> ]
 *     | <decltype>
 *     | <substitution>
 *   <unresolved-qualifier-level> := <simple-id>
 *   <simple-id> := <source-name> [ <template-args> ]
 *   <base-unresolved-name> := <simple-id>
 *     | on <operator-name>
 *     | on <operator-name> <template-args>
 *     | dn <destructor-name>
 *   <destructor-name> := <unresolved-type>
 *     | <simple-id>
 *   <expr-primary> := L <type> <value number> E
 *     | L <type> <value float> E
 *     | L <string type> E
 *     | L <nullptr type> E
 *     | L <pointer type> 0 E
 *     | L <type> <real-part float> _ <imag-part float> E
 *     | L _Z <encoding> E
 *   <braced-expression> := <expression>
 *     | di <field source-name> <braced-expression>
 *     | dx <index expression> <braced-expression>
 *     | dX <range begin expression> <range end expression> <braced-expression>
 *   <initializer> := pi <expression>* E
 *
 * 5.1.6.1 Literals
 *   <float> := <0-9a-f>+
 *
 * 5.1.7 Scope Encoding
 *   <local-name> := Z <function encoding> E <entity name> [<discriminator>]
 *     | Z <function encoding> E s [<discriminator>]
 *   <discriminator> := _ <non-negative number>
 *   <unnamed-type-name> := Ut [<nonnegative number> ] _
 *
 * 5.1.8 Closure Types (Lambdas)
 *   <unnamed-type-name> := <closure-type-name>
 *   <closure-type-name> := Ul <lambda-sig> E [ <nonnegative number> ] _
 *   <lambda-sig> := <parameter type>+
 *   <local-name> := Z <function encoding> Ed [ <parameter number> ] _ <entity name>
 *   <closure-prefix> := [ <prefix> ] <variable or member unqualified-name> M
 *     | <variable template template-prefix> <template-args> M
 *
 * 5.1.10 Compression
 *   <substitution> := S <seq-id> _
 *     | S_
 *     | St
 *     | Sa
 *     | Sb
 *     | Ss
 *     | Si
 *     | So
 *     | Sd
 */

#endif
