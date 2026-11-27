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

/* the terminals of productions.inc */
enum class terminal : uint8_t {
#define ITANIUM_TERMINAL(name, code) name,
#include "productions.inc"
#undef ITANIUM_TERMINAL
};

/* [abi:mangling-compression]: a substitution candidate, as the builder
   tells them apart: a type is one candidate however it is spelled, since
   the type table has one record per type.  A namespace or template as a
   <prefix> is a candidate too: its entity (scope), with no type.  A member
   function type also carries the class that owns the member. */
struct mangling_candidate {
    qualified_type type;
    entity_index scope = 0;
    entity_index member_class = 0;
    /* Dp <type>, the expansion of the pattern type */
    bool expansion = false;
};

/*
 * The encoder: the bytes of a symbol.  It writes the terminals of the
 * tables (productions.inc, operators.inc, builtin-types.inc), numbers,
 * <source-name>s and <seq-id>s, and keeps the substitution candidates in
 * the order they were seen ([abi:mangling-compression]); what is the same
 * candidate is the builder's to say.  It reads nothing of the analysis.
 * The builder (itanium_mangler) decides which production to write; it
 * writes nothing itself.
 */
class itanium_encoder
{
  public:
    explicit itanium_encoder(vbuffer *out) : out(out)
    {
    }
    void write(terminal t);
    void write(itanium_operator o);
    /* [abi:mangling-builtin]: false for a type with no code */
    bool builtin_type(fundamental_type t);
    void number(uint64_t n);
    /* [abi:mangle.number]'s n before a negative one's magnitude */
    void signed_number(int64_t n);
    void value_number(bool negative, uint64_t magnitude);
    /* [abi:mangle.name] <source-name> ::= <positive length number> <identifier> */
    void source_name(const char *bytes, size_t length);
    void source_name(const char *identifier);
    /* the [<number>] _ of several productions: nothing before the _ for
       the first (n 0), n - 1 for another (T_, T0_, Ut_, Ut0_, ...) */
    void ordinal_underscore(uint64_t n);
    void discriminator(uint64_t n);
    void seq_id(size_t n);
    /* a symbol that is not mangled: its identifier as it is */
    void unmangled(const char *bytes, size_t length);

    /* the candidates seen, but those hidden */
    size_t candidate_count() const
    {
        return candidates.size() - hidden;
    }
    const mangling_candidate &candidate_at(size_t i) const
    {
        return candidates[hidden + i];
    }
    void add_candidate(const mangling_candidate &c)
    {
        candidates.append(c);
    }
    /* <substitution> of candidate i: S_, then S <seq-id> _ */
    void substitution(size_t i);
    /* the candidates seen so far hidden from the ones after (the number
       hidden before, to show them again with) */
    size_t hide_candidates()
    {
        size_t before = hidden;
        hidden = candidates.size();
        return before;
    }
    void show_candidates(size_t before)
    {
        hidden = before;
    }

    /* what was written and seen so far, to take back to (rewind) */
    struct mark {
        size_t bytes;
        size_t candidates;
    };
    mark position() const;
    void rewind(mark m);

  private:
    vbuffer *out;
    owned_array<mangling_candidate> candidates;
    size_t hidden = 0;
};

class itanium_mangler
{
  public:
    itanium_mangler(const semantic_analysis &analysis, const preprocessor &pp,
                    vbuffer *out, int variant)
        : analysis(analysis), pp(pp), enc(out), ctor_dtor_variant(variant)
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
        if (what == 0) {
            enc.write(terminal::mangled_name);
            enc.write(terminal::typeinfo);
        } else if (what == 1) {
            enc.write(terminal::mangled_name);
            enc.write(terminal::typeinfo_name);
        }
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
        enc.write(terminal::mangled_name);
        enc.write(terminal::reference_temporary);
        if (!encoding(e))
            return false;
        if (index)
            enc.seq_id(index - 1);
        enc.write(terminal::underscore);
        return true;
    }
    /* [abi:mangling-special-vtables]: <special-name> ::= TV <type> */
    bool vtable_name(entity_index c)
    {
        enc.write(terminal::mangled_name);
        enc.write(terminal::virtual_table);
        return type(analysis.entity_table[c].type);
    }
    /* [abi:mangling-special-vtables]: <special-name> ::= TT <type>, the VTT
       ([abi:vtable-ctor-vtt]) */
    bool vtt_name(entity_index c)
    {
        enc.write(terminal::mangled_name);
        enc.write(terminal::vtt);
        return type(analysis.entity_table[c].type);
    }
    /* [[impl.clang]] a construction virtual table ([abi:vtable-ctor]):
       TC <type> <offset> _ <base type>, the base's at offset in c */
    bool construction_vtable_name(entity_index c, uint64_t offset,
                                  entity_index base)
    {
        enc.write(terminal::mangled_name);
        enc.write(terminal::construction_virtual_table);
        if (!type(analysis.entity_table[c].type))
            return false;
        enc.number(offset);
        enc.write(terminal::underscore);
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
        enc.write(terminal::mangled_name);
        enc.write(terminal::guard_variable);
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
        enc.write(terminal::mangled_name);
        enc.write(terminal::thunk);
        if (vcall) {
            enc.write(terminal::virtual_offset);
            enc.signed_number(adjustment);
            enc.write(terminal::underscore);
            enc.signed_number(vcall);
        } else {
            enc.write(terminal::non_virtual_offset);
            enc.signed_number(adjustment);
        }
        enc.write(terminal::underscore);
        return encoding(f);
    }
    /* [abi:rtti-layout]: the virtual table of the run-time library's
       class __cxxabiv1::NAME (a std::type_info subclass), TV and its
       <nested-name> */
    void runtime_vtable_name(const char *name)
    {
        enc.write(terminal::mangled_name);
        enc.write(terminal::virtual_table);
        enc.write(terminal::nested_name);
        enc.source_name("__cxxabiv1");
        enc.source_name(name);
        enc.write(terminal::end);
    }

  private:
    const semantic_analysis &analysis;
    const preprocessor &pp;
    itanium_encoder enc;
    using candidate = mangling_candidate;

    /* ---- the state of the symbol being mangled ---- */
    /* a constructor's or destructor's: 1 complete, 2 base object */
    int ctor_dtor_variant;
    /* the part of the symbol being mangled, for refuse */
    const char *mangling_part = "the name";
    /* the function template whose specialization's encoding is being
       mangled: what its function parameters' names are */
    entity_index encoding_template = 0;
    /* while a type analysed in the parameter scope of a member template of
       a specialization is mangled: that template, whose own parameters the
       type's template parameter types are */
    entity_index leveled_template = 0;
    /* the dependent_written type being mangled: its bindings
       (sema.h: written_bindings), and the element of the packs among them
       a fold's pattern is mangled for */
    uint32_t written_first = 0, written_count = 0;
    uint32_t pack_element = UINT32_MAX;
    /* (set by type_operand_of_written when a type it mangled is dependent) */
    bool operand_dependent = false;
    /* (the dependent values being mangled, template_arg_of_dependent_value) */
    uint32_t dependent_value_depth = 0;
    /* (the <template-param>s written so far; an alias template's
       specialization in a value-dependent argument was refused, and is to
       be written as its type: non_type_parameter_type) */
    uint64_t template_params_written = 0;
    bool alias_value_refused = false, alias_value_trial = false;

    const struct type &record(qualified_type t) const
    {
        return analysis.type_table[t.unqualified()];
    }
    /* an entity's name: token_stream::identifier's number, the
       preprocessor's plus one (0 is no identifier) */
    const char *spelling(uint32_t identifier, size_t *length) const
    {
        return pp.identifier_spelling(identifier - 1, length);
    }
    bool punctuator(SyntaxElement c, preprocessing_op_or_punc op) const
    {
        return c.is(op);
    }
    bool refuse(const char *what);
    void refuse_expression(SyntaxElement x);

    /* ---- symbols.cpp: [abi:mangling-structure] ---- */
    bool encoding(entity_index e);
    bool encoding_of_specialization(entity_index e);

    /* ---- names.cpp: [abi:mangle.name] and the names of operators,
       constructors and destructors ---- */
    bool name(entity_index e);
    bool nested_name(entity_index e);
    void member_cv_ref_qualifiers(const entity &x);
    bool prefix(entity_index scope);
    bool unscoped_name(entity_index e);
    bool unqualified_name(entity_index e, bool as_type = false);
    bool unqualified_name_without_tags(entity_index e, bool as_type);
    void abi_tags(entity_index e);
    bool source_name(uint32_t identifier);
    bool operator_name(const entity &x);
    bool ctor_dtor_name(special_member which, entity_index inherited = 0);
    bool is_std(entity_index scope) const;
    bool in_inline_namespace(entity_index e) const;
    bool in_unnamed_namespace(entity_index e) const;

    /* ---- scopes.cpp: [abi:mangling-scope], [abi:closure-types] ---- */
    bool local_name(entity_index e, entity_index function);
    bool local_name_scope(entity_index function);
    entity_index local_function(entity_index e,
                                entity_index *local_class = nullptr) const;
    uint64_t local_class_ordinal(entity_index c, entity_index function) const;
    void local_class_discriminator(entity_index c, entity_index function);
    bool closure_type_name(entity_index c);
    bool closure_numbered(entity_index c) const;
    bool translation_unit_number(entity_index c);

    /* ---- types.cpp: [abi:mangling-type] ---- */
    bool type(qualified_type t, entity_index member_class = 0);
    bool type_alternative(qualified_type t, entity_index member_class);
    static const char *type_kind_description(type_kind kind);
    bool builtin_type(fundamental_type t);
    bool va_list_type(qualified_type t);
    bool placeholder_type(const struct type &r);
    bool cv_qualified_type(qualified_type t);
    void cv_qualifiers(unsigned cv);
    void ref_qualifier(int ref);
    bool function_type(qualified_type t);
    bool bare_function_type(qualified_type t);
    bool class_enum_type(qualified_type t);
    bool array_type(qualified_type t);
    bool pointer_to_member_type(qualified_type t);
    bool bit_int_type(const struct type &r);
    bool vector_type(const struct type &r);
    bool type_transformation(const struct type &r);
    bool nested_name_of_dependent_member(entity_index e);
    bool prefix_of_dependent_member(type_index q);
    bool type_of_written(qualified_type t, entity_index member_class);
    bool typename_of_written(qualified_type t, SyntaxRef n);
    bool make_integer_seq_of_written(qualified_type t);

    /* ---- template-arguments.cpp: [abi:mangle.template-arg] ---- */
    void template_param(uint64_t position)
    {
        template_params_written++;
        enc.write(terminal::template_param);
        enc.ordinal_underscore(position);
    }
    bool class_template_args(entity_index c);
    /* (close false: the E left to the caller, which writes a
       requires-clause before it) */
    bool template_args(entity_index c, entity_index function = 0,
                       bool pack_brackets = true, uint32_t limit = UINT32_MAX,
                       bool close = true);
    bool template_arg(entity_index c, uint32_t i, uint32_t element);
    bool template_arg_of(entity_index c, uint32_t i, uint32_t element);
    bool template_template_arg(entity_index t, uint32_t parameter);
    void template_template_param_decl(entity_index parameter);
    bool own_pack_expansion(qualified_type pattern);
    bool non_type_parameter_type(entity_index c, uint32_t i,
                                 qualified_type declared);
    bool template_arg_of_dependent_value(entity_index d);

    /* ---- expressions.cpp: what the four sources share ---- */
    bool expr_primary_external(entity_index e);
    bool expr_primary_value(qualified_type t, bool negative,
                            uint64_t magnitude);
    bool expr_primary_value(fundamental_type t, bool negative,
                            uint64_t magnitude);
    void expr_primary_nullptr();
    bool expr_primary_end(bool negative, uint64_t magnitude);
    static void template_argument_elements(SyntaxElement id,
                                           owned_array<SyntaxElement> &out);
    void function_param(uint32_t position);
    void sizeof_pack(uint64_t position);
    bool expr_primary_of_variable(entity_index t, SyntaxElement name);
    bool expr_primary_of_enumerator(entity_index e);
    bool expr_primary_of_integer_literal(token_index token);

    /* ---- expressions-constraint.cpp: a requires-clause ---- */
    bool expression_of_constraint(entity_index t, SyntaxElement c);
    bool compound_requirement(entity_index t, SyntaxElement req);
    bool unresolved_name_of_constraint_member(entity_index t,
                                              SyntaxElement qualifier,
                                              SyntaxElement id);
    bool template_args_of_constraint(entity_index t, SyntaxElement c);
    bool constraint_namespace(entity_index t, SyntaxElement qualifier,
                              entity_index &scope) const;
    uint64_t constraint_level(entity_index t) const;
    bool type_constraint(entity_index t, SyntaxElement q);
    bool template_param_of_constraint(entity_index t, entity_index parameter);

    /* ---- expressions-written.cpp: an expression as written ---- */
    bool expression_of_written(SyntaxElement c);
    bool expression_of_written_forms(SyntaxElement c);
    /* what a form of the written expression did: not that form, mangled,
       or that form and not mangled */
    enum class form : uint8_t { none, mangled, failed };
    static form written_form(bool mangled)
    {
        if (mangled)
            return form::mangled;
        return form::failed;
    }
    form fold_of_written_expression(SyntaxElement expression);
    form vendor_type_trait_of_written(SyntaxElement expression);
    form sizeof_of_written(SyntaxElement expression);
    form template_id_of_written(SyntaxElement expression);
    form cast_of_written(SyntaxElement expression);
    form prefix_operator_of_written(SyntaxElement expression);
    form operator_of_written(SyntaxElement expression);
    form unresolved_name_of_written(SyntaxElement expression);
    form postfix_of_written(SyntaxElement expression);
    form primary_of_written(SyntaxElement expression);
    bool fold_of_written(SyntaxElement fold);
    /* an initializer-list's expressions, a pattern followed by ... as
       sp <expression> */
    bool expression_list_of_written(SyntaxElement list);
    const semantic_analysis::written_binding *
    written_binding(SyntaxElement name) const;
    const semantic_analysis::written_binding *
    written_object_binding(SyntaxElement name) const;
    bool type_of_written_operand(SyntaxElement a, qualified_type &t,
                                 fundamental_type &keyword_type) const;
    bool type_operand_of_written(SyntaxElement a);
    bool template_args_of_written(SyntaxElement id, bool &dependent);
    bool expansion_of_written(SyntaxElement a, bool &expanded);
    enum class qualifier_level : uint8_t {
        none,
        template_param,
        specialization,
        alias_template_id
    };
    qualifier_level unresolved_qualifier_form(SyntaxElement q,
                                              qualified_type &t) const;
    bool unresolved_qualifier_level(SyntaxElement q, qualifier_level level,
                                    qualified_type t);

    /* ---- expressions-dependent-value.cpp, expressions-structure.cpp ---- */
    bool expression_of_dependent_value(entity_index d);
    bool namespace_levels(entity_index s, uint64_t value);
    bool namespace_levels_of(entity_index s, uint32_t depth);
    bool expression_of_structure(uint32_t s);

    /* ---- compression.cpp: [abi:mangling-compression], the builder's side ---- */
    void add_candidate(qualified_type t, entity_index member_class = 0)
    {
        candidate c;
        c.type = t;
        c.member_class = member_class;
        enc.add_candidate(c);
    }
    void add_candidate(entity_index scope)
    {
        candidate c;
        c.scope = scope;
        enc.add_candidate(c);
    }
    bool substitution(qualified_type t, entity_index member_class = 0);
    bool same_candidate(qualified_type a, qualified_type b) const;
    bool substitution(entity_index scope);
    static constexpr size_t no_candidate = SIZE_MAX;
    size_t candidate_of(entity_index scope) const;
    bool is_std_class_of_char(qualified_type t, const char *name,
                              size_t n) const;
    terminal std_substitution(entity_index c) const;
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
