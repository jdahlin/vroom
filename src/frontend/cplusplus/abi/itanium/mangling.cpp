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

entity_index next_enclosing_function(const entities &table, entity_index f)
{
    entity_index k = table[f].enclosing;
    if (!k || table[k].kind != entity_kind::class_type)
        return 0;
    while (table[k].enclosing &&
           table[table[k].enclosing].kind == entity_kind::class_type)
        k = table[k].enclosing;
    return enclosing_function(table, k);
}

bool visible_local_class(const entities &table, entity_index c)
{
    if (table[c].closure_variable)
        return true;
    /* (a class nested in a local class: as a member of it) */
    entity_index f = next_enclosing_function(table, c);
    if (!f)
        f = enclosing_function(table, c);
    if (!f)
        return false;
    for (entity_index g = next_enclosing_function(table, f); g;
         g = next_enclosing_function(table, g))
        f = g;
    return table[f].inline_ || table[f].instantiation;
}

namespace
{

class itanium_mangler
{
  public:
    itanium_mangler(const semantic_analysis &analysis, const preprocessor &pp,
                    vbuffer *out, int variant)
        : analysis(analysis), pp(pp), out(out), variant(variant)
    {
    }

    bool mangled_name(entity_index e);
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
    };
    owned_array<candidate> candidates;
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
    /* an entity's name: token_stream::identifier's number, the
       preprocessor's plus one (0 is no identifier) */
    const char *spelling(uint32_t identifier, size_t *length) const
    {
        return pp.identifier_spelling(identifier - 1, length);
    }
    bool encoding(entity_index e);
    bool specialization_encoding(entity_index e);
    bool constraint_expression(entity_index t, SyntaxElement c);
    bool constraint_parameter(entity_index t, entity_index parameter);
    bool constraint_template_args(entity_index t, SyntaxElement c);
    bool constraint_namespace_member(entity_index t, SyntaxElement qualifier,
                                     SyntaxElement id);
    bool dependent_expression(SyntaxElement c);
    bool dependent_fold(SyntaxElement fold);
    bool named_variable(entity_index t, SyntaxElement name);
    /* the dependent_written type being mangled: its bindings
       (sema.h: written_bindings), and the element of the packs among them
       a fold's pattern is mangled for */
    uint32_t written_first = 0, written_count = 0;
    uint32_t pack_element = UINT32_MAX;
    const semantic_analysis::written_binding *bound(SyntaxElement name) const;
    const char *binary_operator_code(SyntaxElement op) const;
    bool punctuator(SyntaxElement c, preprocessing_op_or_punc op) const
    {
        return c.is(op);
    }
    bool class_template_args(entity_index c);
    bool template_args(entity_index c, entity_index function = 0,
                       bool pack_brackets = true);
    bool dependent_value(entity_index d);
    bool dependent_value_expr(entity_index d);
    bool template_arg(entity_index c, uint32_t i, uint32_t element);
    bool template_template_arg(entity_index t, uint32_t parameter);
    void template_param(uint64_t position)
    {
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
    bool local_name(entity_index e, entity_index function);
    bool closure_type_name(entity_index c);
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
    if (x.typeinfo_of)
        return typeinfo_name(x.typeinfo_of);
    /* (a closure type's constructor or destructor has its class's name:
       none) */
    /* (a structured binding declaration's variable e: its bindings'
       names, unqualified_name_untagged) */
    bool binding = x.kind == entity_kind::variable && x.binding_count;
    if (!x.name && x.special == special_member::none && !binding)
        return false;
    size_t length = 0;
    const char *identifier = "";
    if (x.name)
        identifier = spelling(x.name, &length);
    /* an entity of the global namespace: a variable of another namespace
       is mangled, as is a function named main there */
    bool global = x.enclosing == 0;
    bool is_main = x.kind == entity_kind::function && length == 4 &&
                   !memcmp(identifier, "main", 4);
    /* [impl.clang]: a variable of the global namespace with internal
       linkage is mangled all the same (_ZL1x), so that it cannot collide
       with an extern "C" name of the translation unit; GCC writes x */
    bool internal_variable = x.kind == entity_kind::variable &&
                             x.linkage_of_name == linkage::internal;
    if (x.unnamed_type_linkage)
        internal_variable = false;
    /* [dcl.link]/7: "At most one function with a particular name can have
       C language linkage.  Two declarations for a function with C language
       linkage with the same function name ... that appear in different
       namespace scopes refer to the same function": its symbol is its
       name in any namespace, and so a variable's */
    /* (a variable template's specialization is mangled, _Z2piIdE) */
    bool specialization = x.kind == entity_kind::variable && x.instantiation;
    if (x.c_language_linkage ||
        (global && !internal_variable && !specialization && !binding &&
         (x.kind == entity_kind::variable || is_main))) {
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
    if ((x.kind == entity_kind::function || x.kind == entity_kind::variable) &&
        x.instantiation)
        return specialization_encoding(e);
    if (!name(e))
        return false;
    if (x.kind == entity_kind::variable)
        return true;
    if (!bare_function_type(x.type))
        return false;
    if (x.kind == entity_kind::function && x.first_declaration && x.enclosing &&
        analysis.entity_table[x.enclosing].instantiation) {
        SyntaxRef declaration =
            analysis.entity_table.declaration(x.first_declaration).node;
        for (SyntaxElement part : declaration.elements()) {
            if (!part.is(syntax_kind::requires_clause) || part.size() != 2)
                continue;
            entity_index pattern = analysis.specialized_template(x.enclosing);
            if (!pattern)
                return false;
            text("Q");
            return constraint_expression(pattern, part[1]);
        }
    }
    return true;
}

/*
 * A function or variable template's specialization (a variable's has no
 * type, as above).  [abi:mangle.function-type]: a
 * template function's return type is encoded: the template's return type,
 * then its parameter types, as the template declares them (a template
 * parameter as <template-param>, type()).  [abi:mangle.name]:
 *
 *     <name> ::= <unscoped-template-name> <template-args>
 *     <nested-name> ::= N [<CV-qualifiers>] [<ref-qualifier>] <template-prefix> <template-args> E
 *     <template-prefix> ::= <prefix> <template unqualified-name>
 *
 * The template's name is a substitution candidate after its prefix,
 * before the arguments.  [abi:mangle.template-arg]:
 *
 *     <template-args> ::= I <template-arg>+ E
 *     <template-arg> ::= <type>
 */
bool itanium_mangler::specialization_encoding(entity_index e)
{
    const entities &table = analysis.entity_table;
    entity_index pattern_of = analysis.specialized_template(e);
    entity_index scope = table[e].enclosing;
    bool nested = scope && !is_std(scope);
    entity_index local_class = 0;
    entity_index local = 0;
    if (nested) {
        /* (a member function template's specialization, [temp.mem]: the
           nested-name's CV-qualifiers its own, as nested_name's) */
        const entity &x = table[e];
        bool member = table[scope].kind == entity_kind::class_type &&
                      (x.kind == entity_kind::function ||
                       x.kind == entity_kind::variable);
        if (table[scope].kind != entity_kind::namespace_entity && !member)
            return false;
        /* (a member of a local class: its function scope first, as
           name()'s) */
        local = local_function(e, &local_class);
        if (member && local && !function_scope(local))
            return false;
        text("N");
        if (member && x.nonstatic_member)
            member_qualifiers(x);
        if (!prefix(scope))
            return false;
    } else if (scope) {
        text("St");
    }
    if (!unqualified_name(e))
        return false;
    candidate c;
    c.scope = pattern_of;
    candidates.append(c);
    /* [impl.clang]: only a function template's specialization has the
       parameters' types ("Tn") before its arguments; a variable's has not */
    entity_index typed_parameters = pattern_of;
    if (table[e].kind != entity_kind::function)
        typed_parameters = 0;
    /* (the parameters' types name the template's parameters, which
       their expressions resolve through it) */
    entity_index outer_encoding_template = encoding_template;
    if (typed_parameters)
        encoding_template = pattern_of;
    bool arguments = template_args(e, typed_parameters);
    encoding_template = outer_encoding_template;
    if (!arguments)
        return false;
    /* [abi:mangle.template-arg]'s template-args, with [impl.clang]'s
       requires-clause: I, the template-args, Q and the requires-clause's
       expression, E; the requires-clause of the template-head */
    SyntaxElement constraint = analysis.template_constraint(pattern_of);
    if (constraint && !table[scope].lambda) {
        out->n--;
        text("Q");
        if (!constraint_expression(pattern_of, constraint))
            return false;
        text("E");
    }
    if (nested)
        text("E");
    if (local)
        local_class_discriminator(local_class, local);
    /* a variable template's specialization: <data name> */
    if (table[e].kind == entity_kind::variable)
        return true;
    qualified_type pattern = table[pattern_of].type;
    encoding_template = pattern_of;
    /* (a constructor or conversion function template's has none:
       [abi:mangle.function-type]'s exception for constructors and conversion
       operators) */
    if (table[e].special == special_member::none &&
        !table[e].conversion_function && !type(record(pattern).operand))
        return false;
    if (!bare_function_type(pattern))
        return false;
    /* [impl.clang] a generic lambda's or a function template's trailing
       requires-clause: Q and its expression, after the parameters (one after its template-parameters
       is the closure type's, closure_type_name) */
    bool trailing = false;
    if (table[scope].lambda)
        constraint = analysis.lambda_constraint(table[scope].lambda, &trailing);
    else if ((constraint = analysis.template_trailing_constraint(pattern_of)))
        trailing = true;
    if (constraint && trailing) {
        text("Q");
        return constraint_expression(pattern_of, constraint);
    }
    return true;
}

/*
 * [abi:expressions] of a requires-clause, as far as it is written
 * with concept-ids of the template's parameters (each an
 * <unresolved-name> with its <template-args>), !, && and ||: an
 * operator's name (nt, aa, oo), then its one or two operands.
 *
 * Parentheses are not mangled.  Anything else: not mangled yet.
 */
bool itanium_mangler::constraint_expression(entity_index t, SyntaxElement c)
{
    if (c.is_token()) {
        if (entity_index parameter =
                analysis.constraint_parameter(t, c.token()))
            return constraint_parameter(t, parameter);
        if (named_variable(t, c))
            return true;
        /* a decimal integer-literal of type int: Li <value> E */
        owned_buffer spelling;
        analysis.token_spelling(c.token(), spelling.get());
        if (!spelling.size() || spelling.size() > 9 ||
            (spelling.data()[0] == '0' && spelling.size() > 1))
            return false;
        for (size_t i = 0; i < spelling.size(); i++)
            if (spelling.data()[i] < '0' || spelling.data()[i] > '9')
                return false;
        text("Li");
        vbuffer_append(out, spelling.data(), spelling.size());
        text("E");
        return true;
    }
    switch (c.kind()) {
    case syntax_kind::constraint_logical_or_expression:
    case syntax_kind::constraint_logical_and_expression:
    case syntax_kind::logical_or_expression:
    case syntax_kind::logical_and_expression:
        if (c.size() != 3)
            return false;
        if (c.kind() == syntax_kind::constraint_logical_or_expression ||
            c.kind() == syntax_kind::logical_or_expression)
            text("oo");
        else
            text("aa");
        return constraint_expression(t, c[0]) && constraint_expression(t, c[2]);
    case syntax_kind::primary_expression:
        if (c.size() != 3 ||
            !punctuator(c[0], preprocessing_op_or_punc::LEFT_PARENTHESIS))
            return false;
        return constraint_expression(t, c[1]);
    case syntax_kind::unary_expression:
        if (c.size() == 4 && c[0].is_token() &&
            punctuator(c[1], preprocessing_op_or_punc::LEFT_PARENTHESIS)) {
            /* sizeof ( T ), T a template parameter: st <type> */
            owned_buffer spelling;
            analysis.token_spelling(c[0].token(), spelling.get());
            SyntaxElement a = c[2];
            while (a.is_node() && a.size() == 1 &&
                   a.is(syntax_kind::type_id, syntax_kind::type_specifier_seq))
                a = a[0];
            if (spelling.size() != 6 || memcmp(spelling.data(), "sizeof", 6) ||
                !a.is_token())
                return false;
            entity_index pe = analysis.constraint_parameter(t, a.token());
            text("st");
            return pe && constraint_parameter(t, pe);
        }
        if (c.size() != 2 ||
            !punctuator(c[0], preprocessing_op_or_punc::EXCLAMATION))
            return false;
        text("nt");
        return constraint_expression(t, c[1]);
    case syntax_kind::relational_expression:
    case syntax_kind::equality_expression: {
        if (c.size() != 3)
            return false;
        const char *code = binary_operator_code(c[1]);
        if (!code)
            return false;
        text(code);
        return constraint_expression(t, c[0]) && constraint_expression(t, c[2]);
    }
    case syntax_kind::requires_expression: {
        /* [abi:expressions]: requires-expression without parameter list
           ("rq" <requirement>+ "E"), simple-requirement ("X" <expression>) */
        if (c.size() != 2)
            return false;
        SyntaxElement body = c[1];
        uint32_t first = 1;
        uint32_t count = 0;
        if (body.size() >= 2)
            count = body.size() - 2;
        if (count == 1 && body[1].is(syntax_kind::requirement_seq)) {
            body = body[1];
            first = 0;
            count = body.size();
        }
        if (!count)
            return false;
        text("rq");
        for (uint32_t i = first; i < first + count; i++) {
            SyntaxElement req = body[i];
            if (req.is(syntax_kind::simple_requirement) && req.size() >= 1) {
                text("X");
                if (!dependent_expression(req[0]))
                    return false;
            } else {
                return false;
            }
        }
        text("E");
        return true;
    }
    case syntax_kind::simple_template_id:
        if (!c[0].is_token() || !analysis.constraint_template(t, c[0].token()))
            return false;
        if (!source_name(analysis.token_identifier(c[0].token())))
            return false;
        return constraint_template_args(t, c);
    case syntax_kind::qualified_id: {
        /* Q<args>::name, Q a class template of the template's scope:
           [abi:expressions]' <unresolved-name>, [impl.clang] in its
           qualifier-level form,

               sr <unresolved-qualifier-level> E <base-unresolved-name>

           the level Q's name and template-args */
        if (c.size() != 2 || !c[0].is(syntax_kind::nested_name_specifier))
            return false;
        if (c[1].is(syntax_kind::simple_template_id))
            return constraint_namespace_member(t, c[0], c[1]);
        if (c[0].size() != 2 || !c[0][0].is(syntax_kind::simple_template_id) ||
            !c[1].is_token() || !c[1].is_identifier())
            return false;
        SyntaxElement q = c[0][0];
        if (!q[0].is_token())
            return false;
        if (!analysis.constraint_class_template(
                analysis.entity_table[t].enclosing, q[0].token()))
            return false;
        text("sr");
        if (!source_name(analysis.token_identifier(q[0].token())) ||
            !constraint_template_args(t, q))
            return false;
        text("E");
        return source_name(analysis.token_identifier(c[1].token()));
    }
    default:
        return false;
    }
}

/* ns::v<args> in t's requires-clause, v a concept or variable template of
   namespace ns (one or more levels, as written): [abi:expressions]'
   <unresolved-name>,

       sr <unresolved-qualifier-level>+ E <base-unresolved-name>

   each level the namespace's name as written (std, not its inline
   namespace) */
bool itanium_mangler::constraint_namespace_member(entity_index t,
                                                  SyntaxElement qualifier,
                                                  SyntaxElement id)
{
    if (!id[0].is_token() || !qualifier.size() ||
        qualifier[0].is(preprocessing_op_or_punc::COLON_COLON))
        return false;
    entity_index scope = 0;
    for (uint32_t i = 0; i + 1 < qualifier.size(); i += 2) {
        if (!qualifier[i].is_identifier())
            return false;
        uint32_t name = analysis.token_identifier(qualifier[i].token());
        entity_index next = 0;
        if (i == 0)
            next = analysis.constraint_name(analysis.entity_table[t].enclosing,
                                            qualifier[i].token());
        else
            next = analysis.entity_table.find(scope, name);
        if (!next ||
            analysis.entity_table[next].kind != entity_kind::namespace_entity)
            return false;
        scope = next;
    }
    if (!analysis.constraint_namespace_template(scope, id[0].token()))
        return false;
    text("sr");
    for (uint32_t i = 0; i + 1 < qualifier.size(); i += 2)
        if (!source_name(analysis.token_identifier(qualifier[i].token())))
            return false;
    text("E");
    if (!source_name(analysis.token_identifier(id[0].token())))
        return false;
    return constraint_template_args(t, id);
}

/* the <template-args> of a template-id c written in t's requires-clause:
   I, each argument (one of t's parameters, a fundamental type keyword or
   a class of t's scope), E */
bool itanium_mangler::constraint_template_args(entity_index t, SyntaxElement c)
{
    text("I");
    /* (more than one argument: a template-argument-list) */
    owned_array<SyntaxElement> written;
    for (uint32_t i = 2; i + 1 < c.size(); i++) {
        if (c[i].is(syntax_kind::template_argument_list))
            for (auto x : c[i].elements())
                written.append(x);
        else
            written.append(c[i]);
    }
    for (size_t wi = 0; wi < written.size(); wi++) {
        SyntaxElement a = written[wi];
        if (a.is(preprocessing_op_or_punc::COMMA))
            continue;
        /* a type-id that is one template parameter's name */
        while (a.is_node() && a.size() == 1 &&
               a.is(syntax_kind::type_id, syntax_kind::type_specifier_seq))
            a = a[0];
        if (!a.is_token()) {
            /* another type (an alias template's specialization, a
               typename, ...): analysed in the template's parameter scope */
            qualified_type argument;
            if (!analysis.constraint_type(t, written[wi], argument))
                return false;
            entity_index saved = encoding_template;
            encoding_template = t;
            bool ok = type(argument);
            encoding_template = saved;
            if (!ok)
                return false;
            continue;
        }
        entity_index pe = analysis.constraint_parameter(t, a.token());
        if (pe) {
            if (!constraint_parameter(t, pe))
                return false;
            continue;
        }
        /* a fundamental type keyword (int, long, char, ...) */
        if (a.is_token()) {
            standard_keyword kw = a.keyword();
            fundamental_type ft = fundamental_type::none;
            switch (kw) {
            case standard_keyword::void_:
                ft = fundamental_type::void_;
                break;
            case standard_keyword::bool_:
                ft = fundamental_type::bool_;
                break;
            case standard_keyword::char_:
                ft = fundamental_type::char_;
                break;
            case standard_keyword::short_:
                ft = fundamental_type::short_int;
                break;
            case standard_keyword::int_:
                ft = fundamental_type::int_;
                break;
            case standard_keyword::long_:
                ft = fundamental_type::long_int;
                break;
            case standard_keyword::float_:
                ft = fundamental_type::float_;
                break;
            case standard_keyword::double_:
                ft = fundamental_type::double_;
                break;
            default:
                break;
            }
            if (ft != fundamental_type::none) {
                if (!builtin_type(ft))
                    return false;
                continue;
            }
        }
        /* a class of the template's scope */
        entity_index k = analysis.constraint_class(
            analysis.entity_table[t].enclosing, a.token());
        if (!k || !type(analysis.entity_table[k].type))
            return false;
    }
    text("E");
    return true;
}

/*
 * One of t's template parameters in t's requires-clause.  [impl.clang]: the
 * clause is mangled unsubstituted, so a parameter keeps its level
 * ([abi:mangle.template-param]):
 *
 *     <template-param> ::= T_
 *                      ::= T <parameter-2 non-negative number> _
 *
 * and, for a parameter of an enclosing level, clang's TL forms of a later
 * revision of the ABI than docs/itanium-abi-grammar.tsv has: TL, the level
 * L-1 as a non-negative number, then __ for the first parameter or _, the
 * parameter-2 number and _.
 *
 * L the number of class templates t is a member of (0: T_); a parameter
 * of another level is a candidate of its own, not the type T_ names.
 */
bool itanium_mangler::constraint_parameter(entity_index t,
                                           entity_index parameter)
{
    const entities &table = analysis.entity_table;
    uint64_t level = 0;
    for (entity_index s = table[t].enclosing; s; s = table[s].enclosing)
        if (table[s].kind == entity_kind::class_type && table[s].instantiation)
            level++;
    if (!level || !analysis.constraint_parameter_is_own(t, parameter)) {
        /* a non-type parameter (its type is the value's, not the
           parameter's): T_ or T <n> _ alone */
        if (record(table[parameter].type).kind !=
            type_kind::template_parameter) {
            template_param(table[parameter].value);
            return true;
        }
        return type(table[parameter].type);
    }
    /* (a type parameter is a type, a candidate; a non-type one an
       expression operand, never) */
    bool is_type =
        record(table[parameter].type).kind == type_kind::template_parameter;
    if (is_type && substitution(parameter))
        return true;
    uint64_t position = table[parameter].value;
    text("TL");
    number(level - 1);
    text("_");
    if (position)
        number(position - 1);
    text("_");
    if (!is_type)
        return true;
    candidate y;
    y.scope = parameter;
    candidates.append(y);
    return true;
}

/*
 * [abi:expressions]:
 *
 *     <expression> ::= cl <expression>+ E
 *                  ::= dt <expression> <unresolved-name>
 *                  ::= pt <expression> <unresolved-name>
 *                  ::= <function-param>
 *                  ::= <expr-primary>
 *
 * Binary arithmetic uses <binary operator-name> followed by both operands.
 * The literal form here is a decimal int.
 *
 * For a named callee or member, the supported <unresolved-name> is a
 * <simple-id> consisting of a <source-name>.
 *
 * [abi:mangle.function-param]:
 *
 *     <function-param> ::= fp <top-level CV-qualifiers> _
 *                      ::= fp <top-level CV-qualifiers> <parameter-2 non-negative number> _
 *
 * The function parameter forms below omit the CV-qualifiers.
 */
const char *itanium_mangler::binary_operator_code(SyntaxElement op) const
{
    static const struct {
        preprocessing_op_or_punc token;
        const char *code;
    } table[] = {
        {preprocessing_op_or_punc::PLUS, "pl"},
        {preprocessing_op_or_punc::MINUS, "mi"},
        {preprocessing_op_or_punc::STAR, "ml"},
        {preprocessing_op_or_punc::SLASH, "dv"},
        {preprocessing_op_or_punc::PERCENT, "rm"},
        {preprocessing_op_or_punc::EQUAL_EQUAL, "eq"},
        {preprocessing_op_or_punc::EXCLAMATION_EQUAL, "ne"},
        {preprocessing_op_or_punc::LESS, "lt"},
        {preprocessing_op_or_punc::GREATER, "gt"},
        {preprocessing_op_or_punc::LESS_EQUAL, "le"},
        {preprocessing_op_or_punc::GREATER_EQUAL, "ge"},
    };
    for (const auto &x : table)
        if (punctuator(op, x.token))
            return x.code;
    return nullptr;
}

/* [abi:expressions]' <expr-primary> of a variable the template's scope
   declares, written unqualified: L_Z <encoding> E; false for another name */
bool itanium_mangler::named_variable(entity_index t, SyntaxElement name)
{
    if (!name.is_token() || !name.is_identifier() || !t)
        return false;
    uint32_t id = analysis.token_identifier(name.token());
    entity_index target = 0;
    for (entity_index s = analysis.entity_table[t].enclosing;;
         s = analysis.entity_table[s].enclosing) {
        target = analysis.entity_table.find(s, id);
        if (target || !s)
            break;
    }
    if (!target || analysis.entity_table[target].kind != entity_kind::variable)
        return false;
    text("L_Z");
    if (!encoding(target))
        return false;
    text("E");
    return true;
}

/* the type an alias template's instantiation bound the identifier name to,
   for the dependent_written type being mangled (the pack's element being
   mangled for a name bound to a pack) */
const semantic_analysis::written_binding *
itanium_mangler::bound(SyntaxElement name) const
{
    if (!name.is_token() || !name.is_identifier())
        return nullptr;
    uint32_t id = analysis.token_identifier(name.token());
    for (uint32_t i = 0; i < written_count; i++) {
        const auto &b = analysis.written_bindings[written_first + i];
        if (b.identifier == id &&
            (b.element == UINT32_MAX || b.element == pack_element))
            return &b;
    }
    return nullptr;
}

/* [abi:expressions] of a unary fold over a pack bound by an alias
   template: expanded, as clang does, for the elements: ( pattern op ... )
   is op e0 (op e1 (... en)), ( ... op pattern ) is ((e0 op e1) op ...) */
bool itanium_mangler::dependent_fold(SyntaxElement fold)
{
    if (fold.size() != 5)
        return false;
    bool right = fold[3].is(preprocessing_op_or_punc::ELLIPSIS);
    SyntaxElement pattern = fold[1];
    SyntaxElement op = fold[2];
    if (!right) {
        if (!fold[1].is(preprocessing_op_or_punc::ELLIPSIS))
            return false;
        pattern = fold[3];
    }
    const char *code = binary_operator_code(op);
    if (!code && op.is(preprocessing_op_or_punc::AMPERSAND_AMPERSAND))
        code = "aa";
    if (!code && op.is(preprocessing_op_or_punc::BAR_BAR))
        code = "oo";
    if (!code || pack_element != UINT32_MAX)
        return false;
    /* the elements of the packs bound (one pack, or packs of one size) */
    uint32_t count = 0;
    for (uint32_t i = 0; i < written_count; i++) {
        const auto &b = analysis.written_bindings[written_first + i];
        if (b.element != UINT32_MAX && b.element + 1 > count)
            count = b.element + 1;
    }
    if (count < 2)
        return false;
    bool ok = true;
    if (right) {
        for (uint32_t k = 0; k < count && ok; k++) {
            if (k + 1 < count)
                text(code);
            pack_element = k;
            ok = dependent_expression(pattern);
        }
    } else {
        for (uint32_t k = 1; k < count; k++)
            text(code);
        for (uint32_t k = 0; k < count && ok; k++) {
            pack_element = k;
            ok = dependent_expression(pattern);
        }
    }
    pack_element = UINT32_MAX;
    return ok;
}

bool itanium_mangler::dependent_expression(SyntaxElement expression)
{
    /* [abi:expressions]: ( type-id ) cast-expression, cv <type>
       <expression>, the type analysed with the alias's names bound */
    if (expression.is(syntax_kind::cast_expression) && expression.size() == 4) {
        SyntaxRef r = expression;
        for (uint32_t i = 0; i < written_count; i++) {
            const auto &b = analysis.written_bindings[written_first + i];
            if (b.cast != r.index().index())
                continue;
            text("cv");
            return type(b.type) && dependent_expression(expression[3]);
        }
        return false;
    }
    if (expression.is(syntax_kind::fold_expression))
        return dependent_fold(expression);
    /* [abi:expressions] a clang type trait of types, as clang mangles it:
       a vendor extended expression, u <source-name> <template-arg>* E
       (__is_same(T, U): u9__is_same <T> <U> E) */
    if (expression.is(syntax_kind::clang_type_trait_expression) &&
        expression.size() >= 4 && expression[0].is_token()) {
        owned_buffer name;
        analysis.token_spelling(expression[0].token(), name.get());
        text("u");
        number(name.size());
        vbuffer_append(out, name.data(), name.size());
        for (uint32_t i = 2; i + 1 < expression.size(); i++) {
            SyntaxElement a = expression[i];
            if (a.is(preprocessing_op_or_punc::COMMA))
                continue;
            if (!a.is(syntax_kind::type_id))
                return false;
            while (a.is_node() && a.size() == 1 &&
                   a.is(syntax_kind::type_id, syntax_kind::type_specifier_seq))
                a = a[0];
            const auto *b = bound(a);
            if (!b)
                return false;
            if (!type(b->type))
                return false;
        }
        text("E");
        return true;
    }
    /* [abi:expressions] sizeof ... ( P ), P a template parameter pack of
       the template or of the class template it is a member of:

           <expression> ::= sZ <template-param>

       [impl.clang] the parameter's position alone, T_ or T <n> _, at any
       depth */
    if (expression.is(syntax_kind::unary_expression) &&
        expression.size() == 5 && expression[0].is(standard_keyword::sizeof_) &&
        expression[1].is(preprocessing_op_or_punc::ELLIPSIS) &&
        expression[3].is_identifier()) {
        entity_index pe = analysis.constraint_parameter(encoding_template,
                                                        expression[3].token());
        if (!pe)
            return false;
        text("sZ");
        template_param(analysis.entity_table[pe].value);
        return true;
    }
    /* [abi:expressions] <unary operator-name> <expression> */
    if (expression.is(syntax_kind::unary_expression) &&
        expression.size() == 2) {
        static const struct {
            preprocessing_op_or_punc token;
            const char *code;
        } unary[] = {
            {preprocessing_op_or_punc::STAR, "de"},
            {preprocessing_op_or_punc::AMPERSAND, "ad"},
            {preprocessing_op_or_punc::MINUS, "ng"},
            {preprocessing_op_or_punc::PLUS, "ps"},
            {preprocessing_op_or_punc::EXCLAMATION, "nt"},
            {preprocessing_op_or_punc::TILDE, "co"},
        };
        for (const auto &u : unary)
            if (punctuator(expression[0], u.token)) {
                text(u.code);
                return dependent_expression(expression[1]);
            }
    }
    /* T(), T an alias template's parameter: cv <type> _ E */
    if (expression.is(syntax_kind::postfix_expression) &&
        expression.size() == 3 &&
        expression[1].is(preprocessing_op_or_punc::LEFT_PARENTHESIS) &&
        expression[2].is(preprocessing_op_or_punc::RIGHT_PARENTHESIS)) {
        if (const auto *b = bound(expression[0])) {
            text("cv");
            if (!type(b->type))
                return false;
            text("_E");
            return true;
        }
    }
    if (expression.is(syntax_kind::expression) && expression.size() == 3 &&
        punctuator(expression[1], preprocessing_op_or_punc::COMMA)) {
        text("cm");
        return dependent_expression(expression[0]) &&
               dependent_expression(expression[2]);
    }
    if (expression.size() == 3 &&
        (expression.kind() == syntax_kind::additive_expression ||
         expression.kind() == syntax_kind::multiplicative_expression ||
         expression.kind() == syntax_kind::relational_expression ||
         expression.kind() == syntax_kind::equality_expression)) {
        const char *op = binary_operator_code(expression[1]);
        if (!op)
            return false;
        text(op);
        return dependent_expression(expression[0]) &&
               dependent_expression(expression[2]);
    }
    if (expression.is(syntax_kind::primary_expression) &&
        expression.size() == 3 &&
        expression[0].is(preprocessing_op_or_punc::LEFT_PARENTHESIS))
        return dependent_expression(expression[1]);

    if (expression.is(syntax_kind::postfix_expression) &&
        expression.size() == 3 && expression[0].is_token() &&
        expression[0].keyword() == standard_keyword::void_ &&
        expression[1].is(preprocessing_op_or_punc::LEFT_PARENTHESIS) &&
        expression[2].is(preprocessing_op_or_punc::RIGHT_PARENTHESIS)) {
        text("cvv_E");
        return true;
    }

    if (expression.is(syntax_kind::qualified_id) && expression.size() == 2 &&
        expression[0].is(syntax_kind::nested_name_specifier)) {
        SyntaxElement qual = expression[0];
        SyntaxElement member = expression[1];
        bool global = qual.size() >= 1 &&
                      qual[0].is(preprocessing_op_or_punc::COLON_COLON);
        uint32_t qi = 0;
        if (global)
            qi = 1;
        /* [abi:expressions] T::name, T a template type parameter, its
           type a <template-param>:

               <unresolved-name> ::= sr <unresolved-type> <base-unresolved-name>
         */
        if (member.is_token() && member.is_identifier() && !global &&
            qual.size() == 2 && qual[0].is_identifier()) {
            entity_index pe = analysis.constraint_parameter(encoding_template,
                                                            qual[0].token());
            if (pe &&
                analysis.entity_table[pe].kind == entity_kind::typedef_name &&
                analysis.type_table[analysis.entity_table[pe]
                                        .type.unqualified()]
                        .kind == type_kind::template_parameter) {
                text("sr");
                return type(analysis.entity_table[pe].type) &&
                       source_name(analysis.token_identifier(member.token()));
            }
        }
        if (member.is_token() && member.is_identifier()) {
            entity_index scope = 0;
            bool first_lookup = !global;
            while (qi + 1 < qual.size()) {
                if (!qual[qi].is_identifier())
                    return false;
                uint32_t id = analysis.token_identifier(qual[qi].token());
                entity_index next = 0;
                if (first_lookup) {
                    entity_index s =
                        analysis.entity_table[encoding_template].enclosing;
                    for (;; s = analysis.entity_table[s].enclosing) {
                        next = analysis.entity_table.find(s, id);
                        if (next || !s)
                            break;
                    }
                    first_lookup = false;
                } else {
                    next = analysis.entity_table.find(scope, id);
                }
                if (!next)
                    return false;
                scope = next;
                qi += 2;
            }
            entity_index target = analysis.entity_table.find(
                scope, analysis.token_identifier(member.token()));
            if (target &&
                analysis.entity_table[target].kind == entity_kind::variable) {
                text("L_Z");
                if (!encoding(target))
                    return false;
                text("E");
                return true;
            }
            /* ns::f as a callee: sr <source-name of ns> E <source-name> (a
               function, or a name an inline namespace of ns holds) */
            if (scope && !global && qual.size() == 2 &&
                analysis.entity_table[scope].kind ==
                    entity_kind::namespace_entity &&
                (!target ||
                 analysis.entity_table[target].kind != entity_kind::variable)) {
                text("sr");
                if (!source_name(analysis.entity_table[scope].name))
                    return false;
                text("E");
                return source_name(analysis.token_identifier(member.token()));
            }
            return false;
        }
        if (member.is(syntax_kind::simple_template_id) && member.size() >= 3 &&
            member[0].is_token()) {
            if (global)
                text("gs");
            text("sr");
            while (qi + 1 < qual.size()) {
                if (!qual[qi].is_identifier())
                    return false;
                if (!source_name(analysis.token_identifier(qual[qi].token())))
                    return false;
                qi += 2;
            }
            text("E");
            if (!source_name(analysis.token_identifier(member[0].token())))
                return false;
            text("I");
            owned_array<SyntaxElement> written;
            for (uint32_t i = 2; i + 1 < member.size(); i++) {
                if (member[i].is(syntax_kind::template_argument_list))
                    for (auto x : member[i].elements())
                        written.append(x);
                else
                    written.append(member[i]);
            }
            for (SyntaxElement a : written) {
                if (a.is(preprocessing_op_or_punc::COMMA))
                    continue;
                /* (a type-id that is not a name: the type the analysis
                   gave it, collect_written_casts) */
                SyntaxElement inner = a;
                while (inner.is_node() && inner.size() == 1 &&
                       inner.is(syntax_kind::type_id,
                                syntax_kind::type_specifier_seq))
                    inner = inner[0];
                if (a.is(syntax_kind::type_id) && !inner.is_token()) {
                    SyntaxRef ar = a;
                    bool found = false;
                    for (uint32_t i = 0; i < written_count && !found; i++) {
                        const auto &w =
                            analysis.written_bindings[written_first + i];
                        if (w.identifier || w.cast != ar.index().index())
                            continue;
                        if (!type(w.type))
                            return false;
                        found = true;
                    }
                    if (!found)
                        return false;
                    continue;
                }
                while (
                    a.is_node() && a.size() == 1 &&
                    a.is(syntax_kind::type_id, syntax_kind::type_specifier_seq))
                    a = a[0];
                if (!a.is_token())
                    return false;
                if (const auto *b = bound(a)) {
                    if (!type(b->type))
                        return false;
                    continue;
                }
                entity_index pe =
                    analysis.constraint_parameter(encoding_template, a.token());
                if (!pe || !type(analysis.entity_table[pe].type))
                    return false;
            }
            text("E");
            return true;
        }
        return false;
    }

    if (expression.is(syntax_kind::postfix_expression) &&
        expression.size() >= 3) {
        auto base = expression[0];
        auto op = expression[1];
        if (expression.size() <= 4 &&
            op.is(preprocessing_op_or_punc::LEFT_PARENTHESIS) &&
            expression[expression.size() - 1].is(
                preprocessing_op_or_punc::RIGHT_PARENTHESIS)) {
            text("cl");
            uint32_t unresolved_name = 0;
            if (base.is_token() && !analysis.function_parameter_position(
                                       encoding_template, base.token()))
                unresolved_name = analysis.token_identifier(base.token());
            if (unresolved_name) {
                if (!source_name(unresolved_name))
                    return false;
            } else if (!dependent_expression(base))
                return false;
            if (expression.size() == 4)
                for (auto argument :
                     expression[2].items(syntax_kind::initializer_list))
                    if (!dependent_expression(argument))
                        return false;
            text("E");
            return true;
        }

        bool dot = op.is(preprocessing_op_or_punc::DOT);
        if (expression.size() == 3 && expression[2].is_token() &&
            (dot || op.is(preprocessing_op_or_punc::ARROW))) {
            if (dot)
                text("dt");
            else
                text("pt");
            return dependent_expression(base) &&
                   source_name(
                       analysis.token_identifier(expression[2].token()));
        }
    }

    if (!expression.is_token())
        return false;
    uint32_t parameter = analysis.function_parameter_position(
        encoding_template, expression.token());
    if (parameter) {
        text("fp");
        if (parameter > 1)
            number(parameter - 2);
        text("_");
        return true;
    }
    /* a non-type template parameter: its <template-param>
       ([abi:mangle.template-param]) */
    if (expression.is_identifier())
        if (entity_index pe = analysis.constraint_parameter(encoding_template,
                                                            expression.token()))
            return constraint_parameter(encoding_template, pe);

    owned_buffer spelling;
    analysis.token_spelling(expression.token(), spelling.get());
    if (!spelling.size() || spelling.size() > 9 ||
        (spelling.data()[0] == '0' && spelling.size() > 1))
        return false;
    for (size_t i = 0; i < spelling.size(); i++)
        if (spelling.data()[i] < '0' || spelling.data()[i] > '9')
            return false;
    text("Li");
    vbuffer_append(out, spelling.data(), spelling.size());
    text("E");
    return true;
}

/*
 * [abi:mangle.name]:
 *
 *     <name> ::= <nested-name>
 *            ::= <unscoped-name>
 *            ::= <local-name>
 *
 * No template scope yet.  An entity of the global namespace or of ::std
 * has an <unscoped-name>; a member of another namespace or of a class a
 * <nested-name>; an entity of a function's block scope a <local-name>
 * ([abi:mangling-scope]).
 */
bool itanium_mangler::name(entity_index e)
{
    const entities &table = analysis.entity_table;
    entity_index scope = table[e].enclosing;
    /* (a closure type with a <data-member-prefix> is a <nested-name>) */
    if ((scope == 0 || is_std(scope)) && !table[e].closure_variable)
        return unscoped_name(e);
    entity_index local_class = 0;
    entity_index local = local_function(e, &local_class);
    if (local && table[scope].kind == entity_kind::class_type) {
        /* a member of a local class: Z <function encoding> E
           <nested-name> */
        if (!function_scope(local))
            return false;
        /* [abi:mangling-scope] a default argument's scope:
           d [<number>] _ */
        if (uint32_t parameter =
                table[local_class].default_argument_parameter) {
            text("d");
            if (parameter > 1)
                number(parameter - 2);
            text("_");
        }
        if (!nested_name(e))
            return false;
        local_class_discriminator(local_class, local);
        return true;
    }
    if (table[scope].kind == entity_kind::class_type ||
        table[scope].kind == entity_kind::namespace_entity)
        return nested_name(e);
    entity_index function = enclosing_function(table, e);
    if (!function)
        return false;
    return local_name(e, function);
}

/*
 * [abi:mangle.qualified-type]:
 *
 *     <CV-qualifiers> ::= [r] [V] [K]
 *     <ref-qualifier> ::= R  # &
 *                     ::= O  # &&
 *
 * A non-static member function's <nested-name> places these before the
 * prefix ([abi:mangle.name]). The member qualifiers here are volatile,
 * const, and the optional ref-qualifier.
 */
void itanium_mangler::member_qualifiers(const entity &x)
{
    if (x.member_cv & cv_volatile)
        text("V");
    if (x.member_cv & cv_const)
        text("K");
    if (x.member_cv & cv_ref_lvalue)
        text("R");
    if (x.member_cv & cv_ref_rvalue)
        text("O");
}

/*
 * [abi:mangle.name]:
 *
 *     <nested-name> ::= N [<CV-qualifiers>] [<ref-qualifier>] <prefix> <unqualified-name> E
 *
 * The CV-qualifiers and ref-qualifier are a non-static member function's
 * (the ones of its declarator).
 */
bool itanium_mangler::nested_name(entity_index e)
{
    const entity &x = analysis.entity_table[e];
    text("N");
    if (x.kind == entity_kind::function && x.nonstatic_member)
        member_qualifiers(x);
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
 *     <prefix> ::= <prefix> <unqualified-name>
 *     <prefix> ::= <substitution>
 *
 * A class or a namespace: of the global namespace its name, of another
 * scope that scope's prefix and its name.  [abi:mangling-compression]: a
 * prefix is a substitution candidate, a class's being its type's (the
 * class named again as a prefix or a type is the same candidate), a
 * namespace's its own; ::std as a prefix is the substitution St, which is
 * no candidate.  A local class starts the prefix: the <local-name>'s
 * function scope is written before the <nested-name> (function_scope).
 */
bool itanium_mangler::prefix(entity_index scope)
{
    const entities &table = analysis.entity_table;
    const entity &c = table[scope];
    bool is_class = c.kind == entity_kind::class_type;
    if (!is_class && c.kind != entity_kind::namespace_entity)
        return false;
    if (!is_class && is_std(scope)) {
        text("St");
        return true;
    }
    if (is_class && substitution(c.type))
        return true;
    if (is_class && c.instantiation &&
        substitution(analysis.specialized_template(scope))) {
        if (!template_args(scope))
            return false;
        add_candidate(c.type);
        return true;
    }
    if (!is_class && substitution(scope))
        return true;
    /* a local class: the function scope (function_scope) came before the
       <nested-name>, whose prefix starts at the class */
    bool local = is_class && c.enclosing &&
                 table[c.enclosing].kind != entity_kind::class_type &&
                 table[c.enclosing].kind != entity_kind::namespace_entity;
    if (c.enclosing && !local && !prefix(c.enclosing))
        return false;
    if (!unqualified_name(scope))
        return false;
    if (is_class && !class_template_args(scope))
        return false;
    if (is_class) {
        add_candidate(c.type);
    } else {
        candidate x;
        x.scope = scope;
        candidates.append(x);
    }
    return true;
}

/* [abi:mangling-compression]: ::std, which St abbreviates: the namespace
   std of the global namespace */
bool itanium_mangler::is_std(entity_index scope) const
{
    return analysis.std_namespace(scope);
}

bool itanium_mangler::in_inline_namespace(entity_index e) const
{
    entity_index s = analysis.entity_table[e].enclosing;
    return s &&
           analysis.entity_table[s].kind == entity_kind::namespace_entity &&
           analysis.entity_table[s].inline_;
}

/* whether e is declared, directly or not, within an unnamed namespace */
bool itanium_mangler::in_unnamed_namespace(entity_index e) const
{
    const entities &table = analysis.entity_table;
    for (entity_index s = table[e].enclosing; s; s = table[s].enclosing)
        if (table[s].kind == entity_kind::namespace_entity && !table[s].name)
            return true;
    return false;
}

/*
 * [abi:mangling.named]:
 *
 *     <class-enum-type> ::= <name>
 *
 * A class or an enumeration of namespace or class scope: its name, as an
 * entity's.  One of a block scope (a <local-name>) has no rule yet.
 */
/*
 * [abi:mangle.name]:
 *
 *     <nested-name> ::= N [<CV-qualifiers>] [<ref-qualifier>] <prefix> <unqualified-name> E
 *     <prefix> ::= <template-param>
 *     <prefix> ::= <prefix> <unqualified-name>
 *
 * typename Q::name ([temp.res]/3) for a dependent Q: N, the template-param
 * Q is made of and each name after it, E.  [abi:mangling-compression]:
 * each prefix is a candidate, the same one as the type it names (Q::a in
 * Q::a::b, as clang 23); the type as a whole is the one type() adds.
 */
bool itanium_mangler::dependent_member_name(entity_index e)
{
    text("N");
    if (!dependent_member_prefix(
            static_cast<type_index>(analysis.entity_table[e].value)))
        return false;
    if (!source_name(analysis.entity_table[e].name))
        return false;
    text("E");
    return true;
}

bool itanium_mangler::dependent_member_prefix(type_index q)
{
    qualified_type t = qualified_type::make(q, cv_none);
    const struct type &r = record(t);
    if (r.kind == type_kind::template_parameter)
        return type(t);
    if (r.kind != type_kind::dependent)
        return false;

    const entity &prefix = analysis.entity_table[r.entity];
    if (prefix.kind != entity_kind::template_specialization &&
        prefix.kind != entity_kind::dependent_member)
        return false;
    if (substitution(t))
        return true;

    if (prefix.kind == entity_kind::template_specialization) {
        entity_index c = record(t).entity;
        if (!prefix.enclosing || is_std(prefix.enclosing)) {
            if (!class_enum_type(t))
                return false;
        } else if (analysis.entity_table[prefix.enclosing].kind !=
                       entity_kind::namespace_entity &&
                   analysis.entity_table[prefix.enclosing].kind !=
                       entity_kind::class_type) {
            /* (a member class template of a class: the class's <prefix>
               the same way, typename sp<int>::get<Y>::type) */
            return false;
        } else if (prefix.instantiation &&
                   substitution(analysis.specialized_template(c))) {
            /* ns::Q<args> with ns::Q seen: its <substitution>, then the
               arguments */
            if (!template_args(c))
                return false;
        } else {
            /* ns::Q<args> in the enclosing <nested-name>: the namespace's
               <prefix>, Q (a candidate as the template-prefix), the
               arguments */
            if (!this->prefix(prefix.enclosing) || !unqualified_name(c) ||
                !class_template_args(c))
                return false;
        }
    } else {
        if (!dependent_member_prefix(static_cast<type_index>(prefix.value)))
            return false;
        if (!source_name(prefix.name))
            return false;
    }
    add_candidate(t);
    return true;
}

bool itanium_mangler::class_enum_type(qualified_type t)
{
    entity_index c = record(t).entity;
    const entities &table = analysis.entity_table;
    const entity &x = table[c];
    if (x.kind != entity_kind::class_type &&
        x.kind != entity_kind::enumeration &&
        x.kind != entity_kind::template_specialization)
        return false;
    entity_index local_class = 0;
    entity_index local = local_function(c, &local_class);
    if (!x.name && !x.linkage_name && !x.lambda && c != local_class)
        return false;
    /* [abi:mangling-compression]: a template's name seen before, with its
       prefix, is a <substitution> before the arguments */
    if (x.instantiation && x.enclosing && !is_std(x.enclosing) && !local &&
        !analysis.template_template_parameter(
            analysis.specialized_template(c))) {
        /* [abi:mangle.name]:

               <nested-name> ::= N [<CV-qualifiers>] [<ref-qualifier>] <template-prefix> <template-args> E

           with the <template-prefix> a <substitution> */
        size_t before = out->n;
        text("N");
        if (substitution(analysis.specialized_template(c))) {
            if (!template_args(c))
                return false;
            text("E");
            return true;
        }
        out->n = before;
    } else if (x.instantiation &&
               substitution(analysis.specialized_template(c))) {
        return template_args(c);
    }
    if (x.instantiation && analysis.template_template_parameter(
                               analysis.specialized_template(c))) {
        /* [abi:mangling-type]: a template template-parameter's
           specialization,

               <type> ::= <template-template-param> <template-args>

           [abi:mangle.template-param]:

               <template-template-param> ::= <template-param>

           the parameter a candidate */
        entity_index t = analysis.specialized_template(c);
        template_param(table[t].value);
        candidate y;
        y.scope = t;
        candidates.append(y);
        return template_args(c);
    }
    if (x.instantiation && is_std(x.enclosing) && !in_inline_namespace(c)) {
        /* [abi:mangling-compression]:
         *
         *     <substitution> ::= Sa
         *     <substitution> ::= Sb
         *
         * ::std::allocator and ::std::basic_string, not candidates
         * themselves; the abbreviations of whole specializations (Ss, Si,
         * So, Sd) are not made, so those are not mangled */
        size_t length = 0;
        const char *identifier = spelling(x.name, &length);
        uint32_t count = analysis.specialization_argument_count(c);
        bool string = length == 12 && !memcmp(identifier, "basic_string", 12);
        if (length == 9 && !memcmp(identifier, "allocator", 9)) {
            text("Sa");
            return template_args(c);
        }
        if (string && count < 3) {
            text("Sb");
            return template_args(c);
        }
        bool stream =
            (length == 13 && (!memcmp(identifier, "basic_istream", 13) ||
                              !memcmp(identifier, "basic_ostream", 13))) ||
            (length == 14 && !memcmp(identifier, "basic_iostream", 14));
        if (string || stream)
            return false;
    }
    if (x.enclosing == 0 || is_std(x.enclosing))
        return unscoped_name(c) && class_template_args(c);
    if (local) {
        /* [abi:mangling-scope]: a local class, or one nested in it */
        if (!function_scope(local))
            return false;
        if (c == local_class) {
            if (!unqualified_name(c, true))
                return false;
            local_class_discriminator(local_class, local);
            return true;
        }
    } else if (table[x.enclosing].kind != entity_kind::class_type &&
               table[x.enclosing].kind != entity_kind::namespace_entity)
        return false;
    /* <nested-name>: the prefix and the name; the type as a whole is the
       candidate type() adds */
    text("N");
    if (!prefix(x.enclosing))
        return false;
    if (!unqualified_name(c))
        return false;
    if (!class_template_args(c))
        return false;
    text("E");
    if (local)
        local_class_discriminator(local_class, local);
    return true;
}

/*
 * A class template's specialization ([abi:mangle.name]):
 *
 *     <unscoped-template-name> ::= <unscoped-name>
 *     <prefix> ::= <template-prefix> <template-args>
 *
 * after the class's name, the template's name as a substitution candidate,
 * then the arguments; nothing for another class.
 */
bool itanium_mangler::class_template_args(entity_index c)
{
    const entity &x = analysis.entity_table[c];
    if ((x.kind != entity_kind::class_type &&
         x.kind != entity_kind::template_specialization) ||
        !x.instantiation)
        return true;
    candidate t;
    t.scope = analysis.specialized_template(c);
    candidates.append(t);
    return template_args(c);
}

/*
 * <template-args> of a template's specialization c.  [abi:mangle.template-arg]:
 *
 *     <template-arg> ::= <expr-primary>
 *
 * a non-type argument's value; [abi:expressions]:
 *
 *     <expr-primary> ::= L <type> <value number> E
 *
 * its parameter's type, then the value, n before a negative one's
 * magnitude; a bool's 0 or 1.
 */
bool itanium_mangler::template_args(entity_index c, entity_index function,
                                    bool pack_brackets)
{
    text("I");
    uint32_t count = analysis.specialization_argument_count(c);
    for (uint32_t i = 0; i < count; i++) {
        uint32_t pack = analysis.specialization_pack(c, i);
        /* [impl.clang]: a function template's non-type parameter of a
           dependent type is mangled as "Tn", its type, then the argument
           (a later revision of [abi:mangle.template-arg] than the one
           quoted here); its type in the template's own terms (T_), as
           clang 17 and later mangle it */
        qualified_type declared;
        bool is_pack = false;
        qualified_type value_type;
        if (function && pack &&
            analysis.template_parameter_kind(function, i, is_pack,
                                             value_type) ==
                semantic_analysis::template_parameter_form_kind::value) {
            /* [impl.clang]: a non-type parameter pack's template-param-decl,
               "Tp" "Tn" and its type (a later revision of
               [abi:mangle.template-arg] than the one quoted here), when the
               pack is empty or its type depends on a template parameter:
               the type is then not deduced from the arguments */
            if (pack == 1 ||
                analysis.dependent_parameter_type(function, i, declared)) {
                text("TpTn");
                if (!type(value_type))
                    return false;
            }
        } else if (function &&
                   analysis.dependent_parameter_type(function, i, declared)) {
            if (pack)
                return false;
            text("Tn");
            if (!type(declared))
                return false;
        }
        /* [impl.clang]: a type-parameter with a type-constraint, "Tk"
           and the concept's name before the argument (clang 17 and later,
           as Tn) */
        token_index q = 0;
        if (function)
            q = analysis.parameter_type_constraint(function, i);
        if (q) {
            if (pack)
                return false;
            text("Tk");
            if (!source_name(analysis.token_identifier(q)))
                return false;
        }
        if (!pack) {
            if (!template_arg(c, i, 0))
                return false;
            continue;
        }
        /* [abi:mangle.template-arg]: a template parameter pack's
           arguments,

               <template-arg> ::= J <template-arg>* E
        */
        if (pack_brackets)
            text("J");
        for (uint32_t k = 1; k < pack; k++) {
            /* a template's own pack expanded (res<A...> in its own
               declaration): [abi:mangle.function-type]'s pack expansion of the
               parameter, <type> ::= Dp <type> */
            if (entity_index own = analysis.specialization_own_pack(c, i, k)) {
                /* a non-type pack: the expansion of the parameter as an expression
                   (sp), in a template argument (X ... E) */
                if (analysis.entity_table[own].kind == entity_kind::none) {
                    text("Xsp");
                    template_param(analysis.entity_table[own].value);
                    text("E");
                    continue;
                }
                /* a pattern written as a type-id (tup<T&&...>): the pack
                   expansion of its type, Dp <type> (collect_written_bindings) */
                if (analysis.entity_table[own].kind ==
                    entity_kind::dependent_written) {
                    const entity &w = analysis.entity_table[own];
                    bool found = false;
                    for (uint32_t b = 0; b < w.member_count && !found; b++) {
                        const auto &x =
                            analysis.written_bindings[w.first_member + b];
                        if (x.identifier || x.cast != w.value)
                            continue;
                        if (!type(x.type))
                            return false;
                        found = true;
                    }
                    if (!found)
                        return false;
                    continue;
                }
                /* (a template pack: not mangled yet) */
                if (analysis.entity_table[own].kind !=
                    entity_kind::typedef_name)
                    return false;
                text("Dp");
                if (!type(analysis.entity_table[own].type))
                    return false;
                continue;
            }
            if (!template_arg(c, i, k))
                return false;
        }
        if (pack_brackets)
            text("E");
    }
    text("E");
    return true;
}

/*
 * A template as a template argument ([abi:mangle.template-arg]):
 *
 *     <template-arg> ::= <type>
 *
 * [impl.clang] the template's name (<unscoped-name>, or a <nested-name> of
 * its namespace), a candidate as a template prefix is; seen before, its
 * <substitution>.  In a template's own declaration a template
 * template-parameter, its <template-param>, a candidate too.
 */
bool itanium_mangler::template_template_arg(entity_index t, uint32_t parameter)
{
    if (substitution(t))
        return true;
    candidate x;
    x.scope = t;
    if (parameter) {
        template_param(parameter - 1);
        candidates.append(x);
        return true;
    }
    const entities &table = analysis.entity_table;
    entity_index scope = table[t].enclosing;
    if (scope && !is_std(scope)) {
        if (table[scope].kind != entity_kind::namespace_entity)
            return false;
        text("N");
        if (!prefix(scope) || !unqualified_name(t))
            return false;
        text("E");
    } else if (!unscoped_name(t)) {
        return false;
    }
    candidates.append(x);
    return true;
}

/* argument i of c's (its element - 1 if element) */
/*
 * [abi:mangle.template-arg] a value-dependent argument:
 *
 *     <template-arg> ::= X <expression> E
 *
 * [impl.clang]: the expression is "nt" before a name written with a
 * nested-name-specifier, then "sr", the qualifier levels, "E" and the
 * base name ([abi:expressions]' unresolved-name, as clang spells it).
 *
 * Q<args>::name as written, Q unqualified; clang makes the qualifier
 * level no candidate, its template-args' types are as anywhere.
 */
bool itanium_mangler::dependent_value_expr(entity_index d)
{
    const entity &x = analysis.entity_table[d];
    if (!x.value)
        return false;
    static const struct {
        uint64_t value;
        const char *code;
    } binary[] = {
        {8, "aa"},  {9, "oo"},  {10, "gt"}, {15, "lt"},
        {16, "le"}, {17, "ge"}, {18, "eq"}, {19, "ne"},
    };
    const char *code = nullptr;
    for (const auto &b : binary)
        if (x.value == b.value)
            code = b.code;
    if (code) {
        text(code);
        return dependent_value_expr(x.enclosing) &&
               dependent_value_expr(x.name);
    }
    if (x.value == 11) {
        text("st");
        return type(x.type);
    }
    if (x.value == 12) {
        text("Li");
        number(x.name);
        text("E");
        return true;
    }
    if (x.value == 13) {
        text("clsr");
        if (!type(x.type) || !source_name(x.name))
            return false;
        text("E");
        return true;
    }
    if (x.value == 20) {
        /* a static data member: an external name, L_Z <encoding> E */
        text("L_Z");
        if (!encoding(x.name))
            return false;
        text("E");
        return true;
    }
    if (x.value == 14) {
        text("nt");
        return dependent_value_expr(x.enclosing);
    }
    auto written_arguments = [&]() {
        text("I");
        for (uint32_t i = 0; i < x.member_count; i++)
            if (!type(analysis.dependent_value_arguments[x.first_member + i]))
                return false;
        text("E");
        return true;
    };
    if (x.value == 21) {
        /* [abi:expressions] Q::template f<args>(), Q a class template's
           specialization:

               <expression> ::= cl <expression>+ E
               <unresolved-name> ::= [gs] sr <unresolved-qualifier-level>+ E <base-unresolved-name>
         */
        entity_index c = record(x.type).entity;
        text("clsr");
        if (!source_name(analysis.entity_table[c].name) ||
            !template_args(c, 0, false))
            return false;
        text("E");
        if (!source_name(x.name) || !written_arguments())
            return false;
        text("E");
        return true;
    }
    if ((x.value & 0xffffffffu & ~0xff00u) == 0x10001 ||
        (x.value & 0xffffffffu & ~0xff00u) == 0x10003) {
        /* [!] [ns::] A<args>::name, A an alias template: its name and
           arguments as written (clang) */
        if (x.value & 2)
            text("nt");
        text("sr");
        if (x.enclosing) {
            uint32_t depth = static_cast<uint32_t>((x.value >> 8) & 0xff);
            if (!depth)
                depth = 1;
            owned_array<entity_index> chain;
            entity_index s = x.enclosing;
            for (uint32_t i = 0; i < depth && s; i++) {
                chain.append(s);
                s = analysis.entity_table[s].enclosing;
            }
            for (size_t i = chain.size(); i-- > 0;)
                if (!source_name(analysis.entity_table[chain[i]].name))
                    return false;
        }
        if (!source_name(static_cast<uint32_t>(x.value >> 32) - 1) ||
            !written_arguments())
            return false;
        text("E");
        return source_name(x.name);
    }
    entity_index q = record(x.type).entity;
    const entity &spec = analysis.entity_table[q];
    if (x.value == 4 || x.value == 6) {
        /* a variable template's specialization, [!] [ns::] v<args>:
           [abi:expressions]' unresolved-name */
        if (x.value & 2)
            text("nt");
        if (x.enclosing) {
            text("sr");
            if (!source_name(analysis.entity_table[x.enclosing].name))
                return false;
            text("E");
        }
        return source_name(spec.name) && template_args(q, 0, false);
    }
    if (!x.enclosing && !(x.value >> 32) && spec.instantiation &&
        analysis.template_template_parameter(
            analysis.specialized_template(q))) {
        /* D<args>::name with D a template template-parameter: clang
           spells the qualifier as an <unresolved-type>, sr T_ I <args> E
           name, with no E closing a level */
        if (x.value & 2)
            text("nt");
        text("sr");
        return class_enum_type(x.type) && source_name(x.name);
    }
    if (x.value & 2)
        text("nt");
    text("sr");
    if (x.enclosing) {
        uint32_t depth = static_cast<uint32_t>((x.value >> 8) & 0xff);
        if (!depth)
            depth = 1;
        owned_array<entity_index> chain;
        entity_index s = x.enclosing;
        for (uint32_t i = 0; i < depth && s; i++) {
            chain.append(s);
            s = analysis.entity_table[s].enclosing;
        }
        for (size_t i = chain.size(); i-- > 0;)
            if (!source_name(analysis.entity_table[chain[i]].name))
                return false;
    }
    /* (clang flattens a pack expansion written among the arguments: the
       substituted elements, no J ... E) */
    if (!source_name(spec.name) || !template_args(q, 0, false))
        return false;
    /* a member class between the specialization and the name */
    if (x.value >> 32 && !source_name(static_cast<uint32_t>(x.value >> 32) - 1))
        return false;
    text("E");
    return source_name(x.name);
}

bool itanium_mangler::dependent_value(entity_index d)
{
    if (!analysis.entity_table[d].value) {
        /* an expression kept as written (dependent_value_entity): X
           <expression> E, the names an alias template bound in it as
           their types */
        const struct type &w = record(analysis.entity_table[d].type);
        if (w.kind != type_kind::dependent ||
            analysis.entity_table[w.entity].kind !=
                entity_kind::dependent_written)
            return false;
        SyntaxRef n = SyntaxRef(node_index(
            static_cast<uint32_t>(analysis.entity_table[w.entity].value)));
        uint32_t saved_first = written_first, saved_count = written_count;
        written_first = analysis.entity_table[w.entity].first_member;
        written_count = analysis.entity_table[w.entity].member_count;
        text("X");
        bool done = dependent_expression(n);
        written_first = saved_first;
        written_count = saved_count;
        text("E");
        return done;
    }
    text("X");
    if (!dependent_value_expr(d))
        return false;
    text("E");
    return true;
}

bool itanium_mangler::template_arg(entity_index c, uint32_t i, uint32_t element)
{
    uint64_t v = 0;
    bool negative = false;
    uint32_t parameter = 0;
    entity_index named = 0;
    if (analysis.specialization_template_name(c, i, named, parameter, element))
        return template_template_arg(named, parameter);
    bool reference = false;
    if (analysis.specialization_address(c, i, named, reference, element)) {
        /* [abi:expressions]: an object or function,

               <expr-primary> ::= L _Z <encoding> E
               <expression> ::= <unary operator-name> <expression>

           its address (a pointer's argument) the second, ad
           ([abi:mangling-operator]'s & as a unary operator) before the
           first, as X <expression> E ([abi:mangle.template-arg]) */
        if (!reference)
            text("Xad");
        text("L_Z");
        if (!encoding(named))
            return false;
        text("E");
        if (!reference)
            text("E");
        return true;
    }
    if (analysis.specialization_dependent_value(c, i, element))
        return dependent_value(analysis.specialization_entity(c, i, element));
    if (!analysis.specialization_value(c, i, v, negative, parameter, element))
        return type(analysis.specialization_argument(c, i, element));
    if (parameter) {
        /* [abi:mangle.template-arg]: a parameter's name, as an
           expression:

               <template-arg> ::= X <expression> E
        */
        text("X");
        template_param(parameter - 1);
        text("E");
        return true;
    }
    text("L");
    if (!type(analysis.specialization_argument(c, i, element)))
        return false;
    if (negative)
        text("n");
    number(v);
    text("E");
    return true;
}

/*
 * [abi:mangle.name]:
 *
 *     <unscoped-name> ::= <unqualified-name>
 *                     ::= St <unqualified-name>
 *
 * The second for a member of ::std.
 */
bool itanium_mangler::unscoped_name(entity_index e)
{
    if (analysis.entity_table[e].enclosing)
        text("St");
    return unqualified_name(e);
}

/*
 * [abi:mangle.name]:
 *
 *     <unqualified-name> ::= <source-name>
 *
 * [impl.gnu] [impl.clang] Both put L before the source name of a function
 * or variable of namespace scope with internal linkage (a static
 * function), which the ABI does not have, so that it is not the symbol of
 * an external entity of that name: _ZL2f2v, _ZN1NL1fEv.  Not within an
 * unnamed namespace, whose name already keeps the symbol apart: both name
 * an unnamed namespace _GLOBAL__N_1 (_ZN12_GLOBAL__N_11fEv).
 */
/* [abi:mangle.abi-tag] "<abi-tag> ::= B <source-name>" after the
   name, from GNU's __attribute__((abi_tag("tag"))) (one tag) */
bool itanium_mangler::unqualified_name(entity_index e, bool as_type)
{
    if (!unqualified_name_untagged(e, as_type))
        return false;
    token_index tag = analysis.entity_table[e].abi_tag;
    if (tag) {
        owned_buffer written;
        analysis.token_spelling(tag, written.get());
        text("B");
        number(written.size() - 2);
        vbuffer_append(out, written.data() + 1, written.size() - 2);
    }
    /* [impl.clang] enable_if(true, ...): the vendor qualifier
       "U <source-name> <template-args>" of its condition */
    if (analysis.entity_table[e].gnu_flags & gnu_enable_if)
        text("Ua9enable_ifILb1EE");
    return true;
}

bool itanium_mangler::unqualified_name_untagged(entity_index e, bool as_type)
{
    const entities &table = analysis.entity_table;
    const entity &x = table[e];
    if (x.kind == entity_kind::namespace_entity && !x.name) {
        text("12_GLOBAL__N_1");
        return true;
    }
    if (x.kind == entity_kind::variable && x.binding_count) {
        /* [abi:mangle.name] "<unqualified-name> ::= DC <source-name>+ E":
           a structured binding declaration's variable e ([dcl.struct.bind]/1),
           by its structured bindings' names; [impl.clang] without L for
           internal linkage */
        text("DC");
        for (uint32_t k = 0; k < x.binding_count; k++)
            if (!source_name(table[x.first_binding + k].name))
                return false;
        text("E");
        return true;
    }
    bool namespace_scope =
        x.enclosing == 0 ||
        table[x.enclosing].kind == entity_kind::namespace_entity;
    /* [impl.clang]: L for a name declared with internal linkage, not for
       a specialization whose internal linkage is its arguments' */
    linkage declared = x.linkage_of_name;
    if (x.instantiation && x.kind == entity_kind::function)
        declared = table[analysis.specialized_template(e)].linkage_of_name;
    if (declared == linkage::internal && namespace_scope &&
        (x.kind == entity_kind::function || x.kind == entity_kind::variable) &&
        !in_unnamed_namespace(e) && !x.unnamed_type_linkage &&
        !x.operator_function)
        text("L");
    if (x.special != special_member::none) {
        /* [impl.clang] an inherited default constructor is named as
           the class's own */
        entity_index inherited = x.inherited_constructor;
        if (!analysis.type_table[x.type.unqualified()].parameter_count)
            inherited = 0;
        return ctor_dtor_name(x.special, inherited);
    }
    if (x.operator_function)
        return operator_name(x);
    if (x.conversion_function) {
        /* [abi:mangling-operator]: "<operator-name> ::= cv <type> # (cast)" */
        text("cv");
        if (x.instantiation) {
            entity_index tmpl = analysis.specialized_template(e);
            entity_index saved = encoding_template;
            encoding_template = tmpl;
            bool ok = type(
                analysis
                    .type_table[analysis.entity_table[tmpl].type.unqualified()]
                    .operand);
            encoding_template = saved;
            return ok;
        }
        return type(analysis.type_table[x.type.unqualified()].operand);
    }
    if (x.kind == entity_kind::function && x.name) {
        /* [abi:mangling-operator]: "<operator-name> ::= li <source-name>
           # operator \"\"": a literal operator, named operator""X */
        size_t length = 0;
        const char *bytes = spelling(x.name, &length);
        if (length > 10 && !memcmp(bytes, "operator\"\"", 10)) {
            text("li");
            number(length - 10);
            vbuffer_append(out, bytes + 10, length - 10);
            return true;
        }
    }
    if (x.kind == entity_kind::class_type && x.lambda)
        return closure_type_name(e);
    if (x.kind == entity_kind::class_type && !x.name && !x.linkage_name &&
        !(as_type && analysis.number_closures)) {
        if (entity_index function = enclosing_function(table, e)) {
            /* [abi:mangling.named]: <unnamed-type-name> ::= Ut [ <nonnegative number> ] _ */
            text("Ut");
            if (uint64_t ordinal = local_class_ordinal(e, function))
                number(ordinal - 1);
            text("_");
            return true;
        }
    }
    if (x.kind == entity_kind::class_type && !x.name && !x.linkage_name &&
        !(as_type && analysis.number_closures) && x.enclosing &&
        table[x.enclosing].kind == entity_kind::class_type) {
        /* [abi:mangling.named]: an unnamed class of a class (clang's
           _ZN1OUt0_C2Ev): Ut, the number of the unnamed classes and
           enumerations of the class before it, less one, and _ */
        uint64_t before = 0;
        for (entity_index other = 1; other < e; other++) {
            const entity &k = table[other];
            if (k.enclosing == x.enclosing && !k.name && !k.linkage_name &&
                !k.lambda &&
                (k.kind == entity_kind::class_type ||
                 k.kind == entity_kind::enumeration))
                before++;
        }
        text("Ut");
        if (before)
            number(before - 1);
        text("_");
        return true;
    }
    /* [dcl.typedef]/9: an unnamed class's name for linkage purposes */
    if (!x.name && (x.kind == entity_kind::class_type ||
                    x.kind == entity_kind::enumeration)) {
        if (x.linkage_name)
            return source_name(x.linkage_name);
        /* [impl.clang]: an unnamed local class/union without a typedef name is
           mangled as $_<n> in its enclosing function */
        if (x.kind == entity_kind::class_type && enclosing_function(table, e) &&
            analysis.number_closures) {
            entity_index context = enclosing_function(table, e);
            size_t n = 0;
            bool seen = false;
            for (entity_index k : analysis.unnumbered_closures) {
                if (k == e) {
                    seen = true;
                    break;
                }
                if (enclosing_function(table, k) == context)
                    n++;
            }
            if (!seen)
                analysis.unnumbered_closures.append(e);
            char buf[32];
            size_t len =
                static_cast<size_t>(snprintf(buf, sizeof(buf), "$_%zu", n));
            number(len);
            vbuffer_append(out, buf, len);
            return true;
        }
        return false;
    }
    return source_name(x.name);
}

/*
 * [abi:mangling-special-ctor-dtor]:
 *
 *     <ctor-dtor-name> ::= C1
 *                      ::= C2
 *                      ::= D0
 *                      ::= D1
 *                      ::= D2
 *
 * C1 and D1 are the complete object's, C2 and D2 the base object's, D0
 * the deleting destructor (variant 0).  The complete object constructor
 * of a class without virtual bases does what the base object constructor
 * does ([abi:mangling-special-ctor-dtor]); there are no allocating
 * constructors yet.
 */
bool itanium_mangler::ctor_dtor_name(special_member which,
                                     entity_index inherited)
{
    if (which == special_member::constructor)
        text("C");
    else
        text("D");
    /* [abi:mangling-special-ctor-dtor]: "<ctor-dtor-name> ::= CI1 <type>
       ::= CI2 <type>": an inheriting constructor, by the base class whose
       constructor it was inherited from */
    if (inherited)
        text("I");
    if (variant == 1)
        text("1");
    else if (variant == 0 && which == special_member::destructor)
        text("0");
    else
        text("2");
    if (inherited) {
        /* [impl.clang] one inherited through another class's inheriting
           constructors is named by the class that declares the original;
           the class is no substitution candidate here */
        const entities &table = analysis.entity_table;
        while (table[inherited].inherited_constructor)
            inherited = table[inherited].inherited_constructor;
        return class_enum_type(qualified_type::make(
            table[table[inherited].enclosing].type.unqualified(), cv_none));
    }
    return true;
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
 * [abi:mangling-operator]:
 *
 *     <operator-name> ::= nw  # new
 *                     ::= na  # new[]
 *                     ::= dl  # delete
 *                     ::= da  # delete[]
 *                     ::= ps  # + (unary)
 *                     ::= ng  # - (unary)
 *                     ::= ad  # & (unary)
 *                     ::= de  # * (unary)
 *                     ::= co  # ~
 *                     ::= pl  # +
 *                     ::= mi  # -
 *                     ::= ml  # *
 *                     ::= dv  # /
 *                     ::= rm  # %
 *                     ::= an  # &
 *                     ::= or  # |
 *                     ::= eo  # ^
 *                     ::= aS  # =
 *                     ::= pL  # +=
 *                     ::= mI  # -=
 *                     ::= mL  # *=
 *                     ::= dV  # /=
 *                     ::= rM  # %=
 *                     ::= aN  # &=
 *                     ::= oR  # |=
 *                     ::= eO  # ^=
 *                     ::= ls  # <<
 *                     ::= lS  # <<=
 *                     ::= eq  # ==
 *                     ::= ne  # !=
 *                     ::= lt  # <
 *                     ::= le  # <=
 *                     ::= ss  # <=>
 *                     ::= nt  # !
 *                     ::= aa  # &&
 *                     ::= oo  # ||
 *                     ::= pp  # ++
 *                     ::= mm  # --
 *                     ::= cm  # ,
 *                     ::= pm  # ->*
 *                     ::= pt  # ->
 *                     ::= cl  # ()
 *                     ::= ix  # []
 *
 * The switch also includes rs (>>), rS (>>=), gt (>), and ge (>=).
 * The operand count includes the implicit object ([over.unary]/1).
 */
bool itanium_mangler::operator_name(const entity &x)
{
    switch (x.allocation) {
    case allocation_function::new_:
        text("nw");
        return true;
    case allocation_function::new_array:
        text("na");
        return true;
    case allocation_function::delete_:
        text("dl");
        return true;
    case allocation_function::delete_array:
        text("da");
        return true;
    default:
        break;
    }

    using enum preprocessing_op_or_punc;
    const struct type &function = analysis.type_table[x.type.unqualified()];
    bool unary = function.parameter_count + unsigned(x.nonstatic_member) == 1;
    const char *code = nullptr;
    if (unary) {
        switch (x.implemented) {
        case PLUS:
            code = "ps";
            break;
        case MINUS:
            code = "ng";
            break;
        case AMPERSAND:
            code = "ad";
            break;
        case STAR:
            code = "de";
            break;
        default:
            break;
        }
        if (code) {
            text(code);
            return true;
        }
    }

    switch (x.implemented) {
    case TILDE:
        code = "co";
        break;
    case PLUS:
        code = "pl";
        break;
    case MINUS:
        code = "mi";
        break;
    case STAR:
        code = "ml";
        break;
    case SLASH:
        code = "dv";
        break;
    case PERCENT:
        code = "rm";
        break;
    case AMPERSAND:
        code = "an";
        break;
    case BAR:
        code = "or";
        break;
    case CARET:
        code = "eo";
        break;
    case EQUAL:
        code = "aS";
        break;
    case PLUS_EQUAL:
        code = "pL";
        break;
    case MINUS_EQUAL:
        code = "mI";
        break;
    case STAR_EQUAL:
        code = "mL";
        break;
    case SLASH_EQUAL:
        code = "dV";
        break;
    case PERCENT_EQUAL:
        code = "rM";
        break;
    case AMPERSAND_EQUAL:
        code = "aN";
        break;
    case BAR_EQUAL:
        code = "oR";
        break;
    case CARET_EQUAL:
        code = "eO";
        break;
    case LESS_LESS:
        code = "ls";
        break;
    case GREATER_GREATER:
        code = "rs";
        break;
    case LESS_LESS_EQUAL:
        code = "lS";
        break;
    case GREATER_GREATER_EQUAL:
        code = "rS";
        break;
    case EQUAL_EQUAL:
        code = "eq";
        break;
    case EXCLAMATION_EQUAL:
        code = "ne";
        break;
    case LESS:
        code = "lt";
        break;
    case GREATER:
        code = "gt";
        break;
    case LESS_EQUAL:
        code = "le";
        break;
    case GREATER_EQUAL:
        code = "ge";
        break;
    case SPACESHIP:
        code = "ss";
        break;
    case EXCLAMATION:
        code = "nt";
        break;
    case AMPERSAND_AMPERSAND:
        code = "aa";
        break;
    case BAR_BAR:
        code = "oo";
        break;
    case PLUS_PLUS:
        code = "pp";
        break;
    case MINUS_MINUS:
        code = "mm";
        break;
    case COMMA:
        code = "cm";
        break;
    case ARROW_STAR:
        code = "pm";
        break;
    case ARROW:
        code = "pt";
        break;
    case LEFT_PARENTHESIS:
        code = "cl";
        break;
    case LEFT_BRACKET:
        code = "ix";
        break;
    default:
        return false;
    }
    text(code);
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

/* Find the outermost class in the function's scope. */
entity_index itanium_mangler::local_function(entity_index e,
                                             entity_index *local_class) const
{
    const entities &table = analysis.entity_table;
    entity_index c = e;
    if (table[c].kind != entity_kind::class_type)
        c = table[c].enclosing;
    while (c && table[c].kind == entity_kind::class_type &&
           table[table[c].enclosing].kind == entity_kind::class_type)
        c = table[c].enclosing;
    if (!c || table[c].kind != entity_kind::class_type)
        return 0;
    entity_index function = enclosing_function(table, c);
    if (function && local_class)
        *local_class = c;
    return function;
}

/* [abi:mangling-scope]: count earlier classes with the same name in the
   function. Unnamed classes have their own sequence, separate from lambdas. */
uint64_t itanium_mangler::local_class_ordinal(entity_index c,
                                              entity_index function) const
{
    const entities &table = analysis.entity_table;
    uint32_t name = table[c].name;
    if (!name)
        name = table[c].linkage_name;
    uint64_t ordinal = 0;
    for (entity_index other = 1; other < c; other++) {
        const entity &x = table[other];
        uint32_t other_name = x.name;
        if (!other_name)
            other_name = x.linkage_name;
        if (x.kind == entity_kind::class_type && !x.lambda &&
            other_name == name && enclosing_function(table, other) == function)
            ordinal++;
    }
    return ordinal;
}

void itanium_mangler::local_class_discriminator(entity_index c,
                                                entity_index function)
{
    const entity &x = analysis.entity_table[c];
    if (x.lambda || (!x.name && !x.linkage_name))
        return;
    if (uint64_t ordinal = local_class_ordinal(c, function))
        discriminator(ordinal - 1);
}

/* [abi:mangling-scope]: Z <function encoding> E, the scope of a
   <local-name>; main's encoding is its name alone (local_name) */
bool itanium_mangler::function_scope(entity_index function)
{
    const entity &f = analysis.entity_table[function];
    size_t length = 0;
    const char *identifier = spelling(f.name, &length);
    bool is_main =
        !f.enclosing && length == 4 && !memcmp(identifier, "main", 4);

    text("Z");
    if (is_main) {
        if (!unqualified_name(function))
            return false;
    } else if (!encoding(function))
        return false;
    text("E");
    return true;
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
    if (!function_scope(function))
        return false;
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
 * [abi:closure-types]: "In the following contexts, however, the
 * one-definition rule requires closure types in different translation
 * units to "correspond": ... the bodies of inline or templated functions"
 * (a lambda's function call operator is inline: one in a lambda's body
 * too); a closure type elsewhere is "unique to the translation unit".
 */
bool itanium_mangler::closure_numbered(entity_index c) const
{
    const entities &table = analysis.entity_table;
    if (table[c].closure_variable)
        return true;
    /* (a member function of a local class: its enclosing function's
       too) */
    for (entity_index f = enclosing_function(table, c); f;
         f = next_enclosing_function(table, f))
        if (table[f].inline_ || table[f].instantiation)
            return true;
    return false;
}

/*
 * [abi:closure-types]:
 *
 *     <closure-type-name> ::= Ul <lambda-sig> E [ <nonnegative number> ] _
 *     <lambda-sig> ::= <parameter type>+
 *
 * "The number is omitted for the first closure type with a given
 * <lambda-sig> in a given context; it is n-2 for the nth closure type (in
 * lexical order) with that same <lambda-sig> and context."  A closure type
 * unique to the translation unit has no encoding in the ABI: [impl.clang]
 * names it $_N, a <source-name>, N the closure's number among those of
 * its function (or of namespace scope, which a generic lambda's of a
 * function counts too) in the order their names are first mangled (as
 * clang's code generator mangles them).
 */
bool itanium_mangler::closure_type_name(entity_index c)
{
    const entities &table = analysis.entity_table;
    entity_index call = table[c].call_operator;
    if (!call)
        return false;
    if (!closure_numbered(c)) {
        if (!analysis.number_closures)
            return false;
        entity_index context = enclosing_function(table, c);
        size_t n = 0;
        bool seen = false;
        for (entity_index k : analysis.unnumbered_closures) {
            if (k == c) {
                seen = true;
                break;
            }
            /* ([impl.clang] a generic lambda's closure of a function takes
               a number of the translation unit's too) */
            if (enclosing_function(table, k) == context ||
                (!context && table[table[k].call_operator].kind ==
                                 entity_kind::template_entity))
                n++;
        }
        if (!seen)
            analysis.unnumbered_closures.append(c);
        owned_buffer written;
        vbuffer_format_append(written.get(), "$_%llu",
                              static_cast<unsigned long long>(n));
        number(written.size());
        vbuffer_append(out, written.data(), written.size());
        return true;
    }
    /* [impl.clang] the context of a closure type in an inline variable's
       initializer (the ABI's later data-member-prefix): the variable's
       <source-name> and M */
    entity_index variable = table[c].closure_variable;
    if (variable) {
        if (!source_name(table[variable].name))
            return false;
        text("M");
        candidate x;
        x.scope = variable;
        candidates.append(x);
    }
    text("Ul");
    /* [impl.clang] (the ABI's later template-param-decl) before the
       parameter types, one per template-parameter of a lambda's
       template-parameter-list: Ty for a type-parameter (the only ones
       analysed) */
    if (table[call].kind == entity_kind::template_entity) {
        /* ([impl.clang] the invented ones of an auto parameter have none) */
        uint32_t explicit_count =
            analysis.template_parameter_count(call) -
            analysis.invented_parameter_count(table[c].lambda);
        /* the template-param-decls: Ty, Tn and the type, Tp before a pack's */
        for (uint32_t i = 0; i < explicit_count; i++) {
            bool pack = false;
            qualified_type value_type;
            semantic_analysis::template_parameter_form_kind kind =
                analysis.template_parameter_kind(call, i, pack, value_type);
            if (kind ==
                semantic_analysis::template_parameter_form_kind::template_)
                return false;
            if (pack)
                text("Tp");
            if (kind == semantic_analysis::template_parameter_form_kind::type) {
                text("Ty");
            } else {
                text("Tn");
                if (!type(value_type))
                    return false;
            }
        }
        /* [impl.clang] the template-head's requires-clause: Q and its
           expression */
        bool trailing = false;
        SyntaxElement constraint =
            analysis.lambda_constraint(table[c].lambda, &trailing);
        if (constraint && !trailing) {
            text("Q");
            if (!constraint_expression(call, constraint))
                return false;
        }
    }
    qualified_type signature = table[call].type;
    if (!bare_function_type(signature))
        return false;
    text("E");
    entity_index context = enclosing_function(table, c);
    uint64_t earlier = 0;
    for (entity_index other = 1; other < c; other++) {
        const entity &k = table[other];
        if (k.kind != entity_kind::class_type || !k.lambda ||
            !k.call_operator || enclosing_function(table, other) != context ||
            k.closure_variable != variable ||
            k.default_argument_parameter != table[c].default_argument_parameter)
            continue;
        /* (a default member initializer analysed in each constructor: the
           one closure type of its lambda-expression) */
        if (variable && !table[variable].inline_ && k.lambda == table[c].lambda)
            continue;
        const struct type &a = record(table[k.call_operator].type);
        const struct type &b = record(signature);
        if (a.variadic == b.variadic &&
            same_parameters(analysis.type_table,
                            table[k.call_operator].type.unqualified(),
                            signature.unqualified()))
            earlier++;
    }
    if (earlier)
        number(earlier - 1);
    text("_");
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
 *            ::= <pointer-to-member-type>
 *            ::= <template-param>
 *            ::= <template-template-param> <template-args>
 *            ::= <decltype>
 *            ::= P <type>
 *            ::= R <type>
 *            ::= O <type>
 *            ::= C <type>
 *            ::= <substitution>
 *
 * P, R and O encode pointers and the two kinds of reference. C encodes
 * a complex pair, and Dp wraps a pack expansion. Substitutable types are
 * entered after their components, in the order they are encoded
 * ([abi:mangling-compression]).
 */
bool itanium_mangler::type(qualified_type t, entity_index member_class)
{
    const struct type &r = record(t);
    if (r.kind == type_kind::fundamental && !t.cv() &&
        r.fundamental == fundamental_type::builtin_va_list) {
        /* [impl.clang] AAPCS64's std::__va_list (C++ ABI for the Arm 64-bit
           Architecture, 4.2.2), a <substitution> candidate as a
           <class-enum-type> */
        if (substitution(t))
            return true;
        text("St9__va_list");
        add_candidate(t);
        return true;
    }
    if (r.kind == type_kind::fundamental && !t.cv() &&
        r.fundamental == fundamental_type::builtin_va_list_tag) {
        /* [impl.clang] x86-64 System V's struct __va_list_tag, a
           <source-name> (a va_list parameter is P13__va_list_tag) and a
           <substitution> candidate as a <class-enum-type> */
        if (substitution(t))
            return true;
        text("13__va_list_tag");
        add_candidate(t);
        return true;
    }
    if (r.kind == type_kind::fundamental && !t.cv())
        return builtin_type(r.fundamental);
    if (r.kind == type_kind::placeholder && !t.cv()) {
        /* [abi:mangling-builtin]: a template's declared return type
           auto or decltype(auto), a builtin type:

               <builtin-type> ::= Da
                              ::= Dc

           Constrained by a concept C ([dcl.spec.auto]/1), Dk (DK of
           decltype(auto)) then C's <name>, as clang (the ABI's
           <type-constraint>, later than the version fetched) */
        static const char *const codes[2][2] = {{"Da", "Dc"}, {"Dk", "DK"}};
        entity_index constraint = r.entity >> 2;
        text(codes[constraint != 0][(r.entity & 3) == 2]);
        return !constraint || name(constraint);
    }
    if (substitution(t, member_class))
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
    } else if (r.kind == type_kind::pointer_to_member) {
        /* [abi:mangle.pointer-to-member-type]: "Pointer-to-member types
           encode the class and member types":

               <pointer-to-member-type> ::= M <class type> <member type>
        */
        text("M");
        type_index cls = analysis.entity_table[r.entity].type.unqualified();
        bool class_ok = false;
        if (cls) {
            class_ok = type(qualified_type::make(cls, cv_none));
        } else if (encoding_template) {
            bool subst = false;
            uint64_t param_pos = analysis.entity_table[r.entity].value;
            for (size_t i = 0; i < candidates.size(); i++) {
                if (candidates[i].type.unqualified() &&
                    !candidates[i].type.cv() &&
                    record(candidates[i].type).kind ==
                        type_kind::template_parameter &&
                    analysis.entity_table[record(candidates[i].type).entity]
                            .value == param_pos) {
                    type(candidates[i].type);
                    subst = true;
                    break;
                }
            }
            if (!subst)
                template_param(param_pos);
            class_ok = true;
        }
        entity_index owner = 0;
        if (record(r.operand).kind == type_kind::function)
            owner = r.entity;
        done = class_ok && type(r.operand, owner);
    } else if (r.kind == type_kind::block_pointer) {
        /* [abi:mangling-type]'s vendor extended type qualifier, as clang
           mangles a block pointer ([ext:clang_blocks]) */
        text("U13block_pointer");
        done = type(r.operand);
    } else if (r.kind == type_kind::atomic) {
        /* [abi:mangling-type]'s vendor extended type qualifier, as clang
           mangles _Atomic(T) ([ext:clang_atomic]) */
        text("U7_Atomic");
        done = type(r.operand);
    } else if (r.kind == type_kind::complex) {
        /* [abi:mangling-type]: <type> ::= C <type>  # complex pair (C99) */
        text("C");
        done = type(r.operand);
    } else if (r.kind == type_kind::bit_int) {
        /* [abi:mangling-builtin]:

               <builtin-type> ::= DB <number> _
                              ::= DB <instantiation-dependent expression> _

           A dependent width here is a non-type template parameter.
           Clang substitutes repeated _BitInt types, so keep them in
           the substitution table for interoperation. */
        if (r.fundamental == fundamental_type::unsigned_int)
            text("DU");
        else
            text("DB");
        if (r.entity)
            template_param(analysis.entity_table[r.entity].value);
        else
            number(r.bound);
        text("_");
        done = true;
    } else if ((r.kind == type_kind::ext_vector ||
                r.kind == type_kind::vector) &&
               !r.entity) {
        /* an ext_vector_type ([ext:clang_ext_vector_type]) as clang
           mangles it: Dv, the element count, _ and the element type */
        text("Dv");
        number(r.bound);
        text("_");
        done = type(r.operand);
    } else if (r.kind == type_kind::pack_expansion) {
        /* [abi:mangle.function-type]: a pack expansion,

               <type> ::= Dp <type>
        */
        text("Dp");
        done = type(r.operand);
    } else if (r.kind == type_kind::function)
        done = function_type(t);
    else if (r.kind == type_kind::array)
        done = array_type(t);
    else if (r.kind == type_kind::dependent &&
             analysis.entity_table[r.entity].kind ==
                 entity_kind::dependent_member)
        done = dependent_member_name(r.entity);
    else if (r.kind == type_kind::dependent &&
             analysis.entity_table[r.entity].kind ==
                 entity_kind::type_transformation) {
        /* [abi:mangling-builtin]'s vendor extended type, as clang
           mangles a type transformation of a dependent operand:

               <builtin-type> ::= u <source-name> [<template-args>]

           the keyword's name, its operand the one template-arg */
        uint64_t operand = analysis.entity_table[r.entity].value;
        text("u");
        done = source_name(analysis.entity_table[r.entity].name);
        if (done) {
            text("I");
            done = type(qualified_type::make(
                static_cast<type_index>(operand & 0xffffffffu),
                static_cast<uint32_t>(operand >> 32)));
            text("E");
        }
    } else if (r.kind == type_kind::dependent &&
               analysis.entity_table[r.entity].kind ==
                   entity_kind::dependent_written) {
        /* [abi:mangle.decltype]:

               <decltype> ::= Dt <expression> E
                          ::= DT <expression> E

           Dt selects the declared type of an unparenthesized identifier
           or member access. DT selects the expression form, including
           a parenthesized identifier or member access. */
        SyntaxRef n = SyntaxRef(node_index(
            static_cast<uint32_t>(analysis.entity_table[r.entity].value)));
        if (n.is(syntax_kind::typename_specifier)) {
            /* typename Q::template X<args>: N <Q's prefix> X I <args> E E */
            uint32_t wf = analysis.entity_table[r.entity].first_member;
            uint32_t wc = analysis.entity_table[r.entity].member_count;
            uint32_t first = wc;
            for (uint32_t i = 0; i < wc; i++)
                if (analysis.written_bindings[wf + i].cast ==
                    n.index().index()) {
                    first = i;
                    break;
                }
            SyntaxElement id = n[n.size() - 1];
            if (first == wc || !id[0].is_identifier())
                return false;
            const auto &q = analysis.written_bindings[wf + first];
            text("N");
            if (!dependent_member_prefix(q.type.unqualified()) ||
                !source_name(analysis.token_identifier(id[0].token())))
                return false;
            text("I");
            for (uint32_t i = first + 1; i < wc; i++) {
                const auto &a = analysis.written_bindings[wf + i];
                if (a.cast != n.index().index())
                    break;
                if (!type(a.type))
                    return false;
            }
            text("EE");
            add_candidate(t);
            return true;
        }
        if (n.size() != 4 || !encoding_template)
            return false;
        SyntaxElement operand = n[2];
        bool member_access =
            operand.is(syntax_kind::postfix_expression) &&
            operand.size() == 3 &&
            (punctuator(operand[1], preprocessing_op_or_punc::DOT) ||
             punctuator(operand[1], preprocessing_op_or_punc::ARROW));
        if (operand.is_token() || member_access)
            text("Dt");
        else
            text("DT");
        uint32_t saved_first = written_first, saved_count = written_count;
        written_first = analysis.entity_table[r.entity].first_member;
        written_count = analysis.entity_table[r.entity].member_count;
        done = dependent_expression(operand);
        pack_element = UINT32_MAX;
        written_first = saved_first;
        written_count = saved_count;
        text("E");
    } else if (r.kind == type_kind::class_type ||
               r.kind == type_kind::enumeration ||
               r.kind == type_kind::dependent)
        done = class_enum_type(t);
    else if (r.kind == type_kind::template_parameter) {
        /* [abi:mangle.template-param]:
               <template-param> ::= T_
               <template-param> ::= T <parameter-2 non-negative number> _
           the first parameter T_, the (n+2)-th T n _ */
        template_param(analysis.entity_table[r.entity].value);
        done = true;
    }
    if (!done)
        return false;
    add_candidate(t, member_class);
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
 * DF <number> _ form below); _BitInt(N) is type_kind::bit_int's (type()).
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
    case fundamental_type::float128:
        text("g");
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
    case fundamental_type::bfloat16:
        /* [impl.clang] DF16b: clang's, as the Itanium ABI's extended
           floating-point types have it */
        text("DF16b");
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
    if (t.cv() & cv_restrict)
        text("r");
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
 * A function type starts the signature with its return type. Its CV and
 * reference qualifiers belong to the same substitution candidate.
 * The supported exception specification is Do (non-throwing).
 */
bool itanium_mangler::function_type(qualified_type t)
{
    const struct type &function = record(t);
    if (function.function_cv & cv_volatile)
        text("V");
    if (function.function_cv & cv_const)
        text("K");
    if (function.non_throwing)
        text("Do");

    text("F");
    if (!type(function.operand))
        return false;
    if (!bare_function_type(t))
        return false;
    if (function.function_ref == 1)
        text("R");
    else if (function.function_ref == 2)
        text("O");
    text("E");
    return true;
}

/*
 * [abi:mangle.array-type]:
 *
 *     <array-type> ::= A [<array bound number>] _ <element type>
 *                  ::= A <instantiation-dependent array bound expression> _ <element type>
 *
 * Omit an unknown bound. A dependent bound here is a template parameter.
 * Array CV-qualifiers go on the element type ([basic.type.qualifier]/3).
 */
bool itanium_mangler::array_type(qualified_type t)
{
    const struct type &array = record(t);
    text("A");
    if (array.entity)
        template_param(analysis.entity_table[array.entity].value);
    else if (array.bound_known)
        number(array.bound);
    text("_");

    qualified_type element = array.operand;
    return type(
        qualified_type::make(element.unqualified(), element.cv() | t.cv()));
}

/*
 * [abi:mangling-compression]:
 *
 *     <substitution> ::= S <seq-id> _
 *                    ::= S_
 *
 * The first candidate is S_, the (n+2)-th S <n in base 36> _.
 * A member function type is a distinct candidate for each owning class,
 * even when its <function-type> text matches another function type.
 */
bool itanium_mangler::substitution(qualified_type t, entity_index member_class)
{
    for (size_t i = 0; i < candidates.size(); i++)
        if (!candidates[i].scope &&
            candidates[i].member_class == member_class &&
            same_candidate(candidates[i].type, t)) {
            substitution_at(i);
            return true;
        }
    return false;
}

/* [impl.clang] whether a and b are one candidate: the same type, or made
   the same way of template parameters at the same positions (clang keys a
   template parameter by its position: a function template's T_ in a
   <local-name>'s encoding and the T_ of the function encoded after it are
   one candidate) */
bool itanium_mangler::same_candidate(qualified_type a, qualified_type b) const
{
    for (;;) {
        if (a == b)
            return true;
        const struct type &x = record(a);
        const struct type &y = record(b);
        if (a.cv() != b.cv() || x.kind != y.kind)
            return false;
        switch (x.kind) {
        case type_kind::template_parameter:
            return analysis.entity_table[x.entity].value ==
                   analysis.entity_table[y.entity].value;
        case type_kind::pointer:
        case type_kind::lvalue_reference:
        case type_kind::rvalue_reference:
        case type_kind::pack_expansion:
            a = x.operand;
            b = y.operand;
            continue;
        default:
            return false;
        }
    }
}

/* a namespace seen as a prefix before */
bool itanium_mangler::substitution(entity_index scope)
{
    for (size_t i = 0; i < candidates.size(); i++)
        if (candidates[i].scope == scope) {
            substitution_at(i);
            return true;
        }
    return false;
}

void itanium_mangler::substitution_at(size_t i)
{
    text("S");
    if (i)
        seq_id(i - 1);
    text("_");
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
                         const preprocessor &pp, entity_index e, vbuffer *out,
                         int variant)
{
    itanium_mangler m(analysis, pp, out, variant);
    return m.mangled_name(e);
}

bool itanium_reference_temporary_name(const semantic_analysis &analysis,
                                      const preprocessor &pp, entity_index e,
                                      uint32_t index, vbuffer *out)
{
    itanium_mangler m(analysis, pp, out, 2);
    return m.reference_temporary_name(e, index);
}

bool itanium_typeinfo_name(const semantic_analysis &analysis,
                           const preprocessor &pp, type_index t, int what,
                           vbuffer *out)
{
    itanium_mangler m(analysis, pp, out, 2);
    return m.typeinfo_name(t, what);
}

bool itanium_vtable_name(const semantic_analysis &analysis,
                         const preprocessor &pp, entity_index c, vbuffer *out)
{
    itanium_mangler m(analysis, pp, out, 2);
    return m.vtable_name(c);
}

bool itanium_vtt_name(const semantic_analysis &analysis, const preprocessor &pp,
                      entity_index c, vbuffer *out)
{
    itanium_mangler m(analysis, pp, out, 2);
    return m.vtt_name(c);
}

bool itanium_construction_vtable_name(const semantic_analysis &analysis,
                                      const preprocessor &pp, entity_index c,
                                      uint64_t offset, entity_index base,
                                      vbuffer *out)
{
    itanium_mangler m(analysis, pp, out, 2);
    return m.construction_vtable_name(c, offset, base);
}

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
