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
       candidate too: its entity, with no type */
    struct candidate {
        qualified_type type;
        entity_index scope = 0;
    };
    owned_array<candidate> candidates;
    /* the function template whose specialization's encoding is being
       mangled: what its function parameters' names are */
    entity_index encoding_template = 0;
    void add_candidate(qualified_type t)
    {
        candidate c;
        c.type = t;
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
    bool constraint_expression(entity_index t, syntax_child c);
    bool dependent_expression(syntax_child c);
    bool punctuator(syntax_child c, preprocessing_op_or_punc op) const
    {
        return analysis.punctuator_is(c, op);
    }
    bool class_template_args(entity_index c);
    bool template_args(entity_index c, entity_index function = 0);
    bool dependent_value(entity_index d);
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
    bool unqualified_name(entity_index e);
    bool unqualified_name_untagged(entity_index e);
    bool source_name(uint32_t identifier);
    bool operator_name(const entity &x);
    bool ctor_dtor_name(special_member which);
    void number(uint64_t n);
    bool local_name(entity_index e, entity_index function);
    bool closure_type_name(entity_index c);
    bool closure_numbered(entity_index c) const;
    entity_index local_function(entity_index e) const;
    bool function_scope(entity_index function);
    bool nested_name(entity_index e);
    bool prefix(entity_index scope);
    bool class_enum_type(qualified_type t);
    bool dependent_member_name(entity_index e);
    bool dependent_member_prefix(type_index q);
    void discriminator(uint64_t n);
    bool bare_function_type(qualified_type t);
    bool type(qualified_type t);
    bool builtin_type(fundamental_type t);
    bool qualified(qualified_type t);
    bool function_type(qualified_type t);
    bool array_type(qualified_type t);
    bool substitution(qualified_type t);
    bool same_candidate(qualified_type a, qualified_type b) const;
    bool substitution(entity_index scope);
    void substitution_at(size_t i);
    void seq_id(size_t n);
    bool is_std(entity_index scope) const;
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
    return bare_function_type(x.type);
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
    if (nested) {
        /* (a member function template's specialization, [temp.mem]: the
           nested-name's CV-qualifiers its own, as nested_name's) */
        const entity &x = table[e];
        bool member = table[scope].kind == entity_kind::class_type &&
                      x.kind == entity_kind::function;
        if (table[scope].kind != entity_kind::namespace_entity && !member)
            return false;
        /* (a member of a local class: its function scope first, as
           name()'s) */
        entity_index local = local_function(e);
        if (member && local && !function_scope(local))
            return false;
        text("N");
        if (member && x.nonstatic_member) {
            if (x.member_cv & cv_volatile)
                text("V");
            if (x.member_cv & cv_const)
                text("K");
        }
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
    if (!template_args(e, pattern_of))
        return false;
    /* [abi:mangle.template-arg]'s template-args, with [impl.clang]'s
       requires-clause: I, the template-args, Q and the requires-clause's
       expression, E; the requires-clause of the template-head */
    syntax_child constraint = analysis.template_constraint(pattern_of);
    if (constraint.bits && !table[scope].lambda) {
        out->n--;
        text("Q");
        if (!constraint_expression(pattern_of, constraint))
            return false;
        text("E");
    }
    if (nested)
        text("E");
    /* a variable template's specialization: <data name> */
    if (table[e].kind == entity_kind::variable)
        return true;
    qualified_type pattern = table[pattern_of].type;
    encoding_template = pattern_of;
    /* (a constructor template's has none: [abi:mangle.function-type]'s
       exception for constructors) */
    if (table[e].special == special_member::none &&
        !type(record(pattern).operand))
        return false;
    if (!bare_function_type(pattern))
        return false;
    /* [impl.clang] a generic lambda's trailing requires-clause: Q and its
       expression, after the parameters (one after its template-parameters
       is the closure type's, closure_type_name) */
    bool trailing = false;
    if (table[scope].lambda)
        constraint = analysis.lambda_constraint(table[scope].lambda, &trailing);
    if (constraint.bits && trailing) {
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
bool itanium_mangler::constraint_expression(entity_index t, syntax_child c)
{
    if (c.is_token())
        return false;
    const syntax_tree &tree = analysis.syntax();
    const syntax_node &node = tree[c.index()];
    const syntax_child *parts = tree.children(c.index());
    switch (node.kind) {
    case syntax_kind::constraint_logical_or_expression:
    case syntax_kind::constraint_logical_and_expression:
    case syntax_kind::logical_or_expression:
    case syntax_kind::logical_and_expression:
        if (node.child_count != 3)
            return false;
        if (node.kind == syntax_kind::constraint_logical_or_expression ||
            node.kind == syntax_kind::logical_or_expression)
            text("oo");
        else
            text("aa");
        return constraint_expression(t, parts[0]) &&
               constraint_expression(t, parts[2]);
    case syntax_kind::primary_expression:
        if (node.child_count != 3 ||
            !punctuator(parts[0], preprocessing_op_or_punc::LEFT_PARENTHESIS))
            return false;
        return constraint_expression(t, parts[1]);
    case syntax_kind::unary_expression:
        if (node.child_count != 2 ||
            !punctuator(parts[0], preprocessing_op_or_punc::EXCLAMATION))
            return false;
        text("nt");
        return constraint_expression(t, parts[1]);
    case syntax_kind::simple_template_id: {
        if (!parts[0].is_token() ||
            !analysis.constraint_concept(t, parts[0].index()))
            return false;
        if (!source_name(analysis.token_identifier(parts[0].index())))
            return false;
        text("I");
        /* (more than one argument: a template-argument-list) */
        owned_array<syntax_child> written;
        for (uint32_t i = 2; i + 1 < node.child_count; i++) {
            if (!parts[i].is_token() && tree[parts[i].index()].kind ==
                                            syntax_kind::template_argument_list)
                for (uint32_t k = 0; k < tree[parts[i].index()].child_count;
                     k++)
                    written.append(tree.children(parts[i].index())[k]);
            else
                written.append(parts[i]);
        }
        for (syntax_child a : written) {
            if (punctuator(a, preprocessing_op_or_punc::COMMA))
                continue;
            /* a type-id that is one template parameter's name */
            while (!a.is_token() && tree[a.index()].child_count == 1 &&
                   (tree[a.index()].kind == syntax_kind::type_id ||
                    tree[a.index()].kind == syntax_kind::type_specifier_seq))
                a = tree.children(a.index())[0];
            if (!a.is_token())
                return false;
            entity_index pe = analysis.constraint_parameter(t, a.index());
            if (!pe || !type(analysis.entity_table[pe].type))
                return false;
        }
        text("E");
        return true;
    }
    default:
        return false;
    }
}

/*
 * [abi:expressions] of a decltype-specifier's type-dependent operand in a
 * function template's declarator (encoding_template), as far as it is
 * written with its function parameters, class member accesses and calls:
 *
 *     <expression> ::= cl <expression>+ E
 *                  ::= <unresolved-name>
 *                  ::= dt <expression> <unresolved-name>
 *                  ::= pt <expression> <unresolved-name>
 *                  ::= <function-param>
 *
 * [abi:mangle.function-param]:
 *
 *     <function-param> ::= fp <top-level CV-qualifiers> _
 *                      ::= fp <top-level CV-qualifiers> <parameter-2 non-negative number> _
 *
 * the member's <unresolved-name> its <source-name>.  Anything else: not
 * mangled yet.
 */
bool itanium_mangler::dependent_expression(syntax_child c)
{
    if (c.is_token()) {
        uint32_t position =
            analysis.function_parameter_position(encoding_template, c.index());
        if (!position)
            return false;
        text("fp");
        if (position > 1)
            number(position - 2);
        text("_");
        return true;
    }
    const syntax_tree &tree = analysis.syntax();
    const syntax_node &node = tree[c.index()];
    const syntax_child *parts = tree.children(c.index());
    if (node.kind != syntax_kind::postfix_expression || node.child_count < 3)
        return false;
    bool arrow = punctuator(parts[1], preprocessing_op_or_punc::ARROW);
    if (node.child_count == 3 &&
        (arrow || punctuator(parts[1], preprocessing_op_or_punc::DOT)) &&
        parts[2].is_token()) {
        if (arrow)
            text("pt");
        else
            text("dt");
        return dependent_expression(parts[0]) &&
               source_name(analysis.token_identifier(parts[2].index()));
    }
    if (!punctuator(parts[1], preprocessing_op_or_punc::LEFT_PARENTHESIS) ||
        !punctuator(parts[node.child_count - 1],
                    preprocessing_op_or_punc::RIGHT_PARENTHESIS) ||
        node.child_count > 4)
        return false;
    text("cl");
    /* a callee named by an identifier that is no function parameter: its
       <unresolved-name>, the <source-name> (the overload set is resolved
       per instantiation, [temp.dep]/2) */
    syntax_child callee = parts[0];
    if (callee.is_token() &&
        !analysis.function_parameter_position(encoding_template,
                                              callee.index()) &&
        analysis.token_identifier(callee.index())) {
        if (!source_name(analysis.token_identifier(callee.index())))
            return false;
    } else if (!dependent_expression(callee))
        return false;
    if (node.child_count == 4) {
        syntax_child a = parts[2];
        if (!a.is_token() &&
            tree[a.index()].kind == syntax_kind::initializer_list) {
            for (uint32_t i = 0; i < tree[a.index()].child_count; i++) {
                syntax_child x = tree.children(a.index())[i];
                if (!punctuator(x, preprocessing_op_or_punc::COMMA) &&
                    !dependent_expression(x))
                    return false;
            }
        } else if (!dependent_expression(a))
            return false;
    }
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
    entity_index local = local_function(e);
    if (local && table[scope].kind == entity_kind::class_type) {
        /* a member of a local class: Z <function encoding> E
           <nested-name> */
        if (!function_scope(local))
            return false;
        return nested_name(e);
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
    const struct type &r = record(qualified_type::make(q, cv_none));
    if (r.kind == type_kind::template_parameter)
        return type(qualified_type::make(q, cv_none));
    if (r.kind == type_kind::dependent &&
        analysis.entity_table[r.entity].kind ==
            entity_kind::template_specialization) {
        /* a class template's specialization at namespace scope as the
           prefix, its template-prefix and template-args
           (only_int<T>::type: N8only_intIT_E4typeE, as clang 23), itself
           a candidate */
        qualified_type t = qualified_type::make(q, cv_none);
        if (substitution(t))
            return true;
        if (analysis.entity_table[r.entity].enclosing != 0 ||
            !class_enum_type(t))
            return false;
        add_candidate(t);
        return true;
    }
    if (r.kind != type_kind::dependent ||
        analysis.entity_table[r.entity].kind != entity_kind::dependent_member)
        return false;
    qualified_type t = qualified_type::make(q, cv_none);
    if (substitution(t))
        return true;
    const entity &x = analysis.entity_table[r.entity];
    if (!dependent_member_prefix(static_cast<type_index>(x.value)))
        return false;
    if (!source_name(x.name))
        return false;
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
    if (!x.name && !x.linkage_name && !x.lambda)
        return false;
    /* [abi:mangling-compression]: a template's name seen before, with its
       prefix, is a <substitution> before the arguments */
    if (x.instantiation && x.enclosing && !is_std(x.enclosing) &&
        !local_function(c)) {
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
    if (x.instantiation && is_std(x.enclosing)) {
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
    if (entity_index local = local_function(c)) {
        /* [abi:mangling-scope]: a local class, or one nested in it */
        if (!function_scope(local))
            return false;
        if (table[x.enclosing].kind != entity_kind::class_type)
            return unqualified_name(c);
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
bool itanium_mangler::template_args(entity_index c, entity_index function)
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
        if (function &&
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
        text("J");
        for (uint32_t k = 1; k < pack; k++) {
            /* a template's own pack expanded (res<A...> in its own
               declaration): [abi:mangle.function-type]'s pack expansion of the
               parameter, <type> ::= Dp <type> */
            if (entity_index own = analysis.specialization_own_pack(c, i, k)) {
                /* (a non-type or template pack: not mangled yet) */
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
bool itanium_mangler::dependent_value(entity_index d)
{
    const entity &x = analysis.entity_table[d];
    if (!x.value)
        return false;
    entity_index q = record(x.type).entity;
    const entity &spec = analysis.entity_table[q];
    if (x.value == 4) {
        /* a variable template's specialization, v<args>: [abi:expressions]'
           unresolved-name, <source-name> <template-args> */
        text("X");
        if (!source_name(spec.name) || !template_args(q))
            return false;
        text("E");
        return true;
    }
    text("X");
    if (x.value & 2)
        text("nt");
    text("sr");
    if (!source_name(spec.name) || !template_args(q))
        return false;
    text("E");
    if (!source_name(x.name))
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
bool itanium_mangler::unqualified_name(entity_index e)
{
    if (!unqualified_name_untagged(e))
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

bool itanium_mangler::unqualified_name_untagged(entity_index e)
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
        !in_unnamed_namespace(e) && !x.unnamed_type_linkage)
        text("L");
    if (x.special != special_member::none)
        return ctor_dtor_name(x.special);
    if (x.operator_function)
        return operator_name(x);
    if (x.conversion_function) {
        /* [abi:mangling-operator]: "<operator-name> ::= cv <type> # (cast)" */
        text("cv");
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
    /* [dcl.typedef]/9: an unnamed class's name for linkage purposes */
    if (!x.name && (x.kind == entity_kind::class_type ||
                    x.kind == entity_kind::enumeration))
        return source_name(x.linkage_name);
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
bool itanium_mangler::ctor_dtor_name(special_member which)
{
    if (which == special_member::constructor)
        text("C");
    else
        text("D");
    if (variant == 1)
        text("1");
    else if (variant == 0 && which == special_member::destructor)
        text("0");
    else
        text("2");
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
 *     <operator-name> ::= nw        # new
 *                     ::= na        # new[]
 *                     ::= dl        # delete
 *                     ::= da        # delete[]
 *                     ::= ps        # + (unary)
 *                     ::= ng        # - (unary)
 *                     ::= ad        # & (unary)
 *                     ::= de        # * (unary)
 *                     ::= co        # ~
 *                     ::= pl        # +
 *                     ::= mi        # -
 *                     ::= ml        # *
 *                     ::= dv        # /
 *                     ::= rm        # %
 *                     ::= an        # &
 *                     ::= or        # |
 *                     ::= eo        # ^
 *                     ::= pL        # +=
 *                     ::= mI        # -=
 *                     ::= mL        # *=
 *                     ::= dV        # /=
 *                     ::= rM        # %=
 *                     ::= aN        # &=
 *                     ::= oR        # |=
 *                     ::= eO        # ^=
 *                     ::= ls        # <<
 *                     ::= lS        # <<=
 *                     ::= eq        # ==
 *                     ::= ne        # !=
 *                     ::= lt        # <
 *                     ::= le        # <=
 *                     ::= nt        # !
 *                     ::= aa        # &&
 *                     ::= oo        # ||
 *                     ::= pp        # ++ (postfix in <expression> context)
 *                     ::= mm        # --
 *                     ::= aS        # =
 *                     ::= cl        # ()
 *                     ::= ix        # []
 *                     ::= cm        # ,
 *                     ::= pm        # ->*
 *                     ::= pt        # ->
 *                     ::= ss        # <=>
 *
 * and rs (>>), rS (>>=), gt (>) and ge (>=), whose lines the grammar
 * index lost.  The unary forms of + - & * are the operator functions of one operand,
 * the implicit object parameter counted ([over.unary]/1).
 */
bool itanium_mangler::operator_name(const entity &x)
{
    const types &table = analysis.type_table;
    uint32_t operands = table[x.type.unqualified()].parameter_count;
    if (x.nonstatic_member)
        operands++;
    bool unary = operands == 1;
    const char *code = nullptr;
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
    switch (x.implemented) {
    case preprocessing_op_or_punc::PLUS:
        code = "pl";
        if (unary)
            code = "ps";
        break;
    case preprocessing_op_or_punc::MINUS:
        code = "mi";
        if (unary)
            code = "ng";
        break;
    case preprocessing_op_or_punc::AMPERSAND:
        code = "an";
        if (unary)
            code = "ad";
        break;
    case preprocessing_op_or_punc::STAR:
        code = "ml";
        if (unary)
            code = "de";
        break;
    case preprocessing_op_or_punc::TILDE:
        code = "co";
        break;
    case preprocessing_op_or_punc::SLASH:
        code = "dv";
        break;
    case preprocessing_op_or_punc::PERCENT:
        code = "rm";
        break;
    case preprocessing_op_or_punc::BAR:
        code = "or";
        break;
    case preprocessing_op_or_punc::CARET:
        code = "eo";
        break;
    case preprocessing_op_or_punc::PLUS_EQUAL:
        code = "pL";
        break;
    case preprocessing_op_or_punc::MINUS_EQUAL:
        code = "mI";
        break;
    case preprocessing_op_or_punc::STAR_EQUAL:
        code = "mL";
        break;
    case preprocessing_op_or_punc::SLASH_EQUAL:
        code = "dV";
        break;
    case preprocessing_op_or_punc::PERCENT_EQUAL:
        code = "rM";
        break;
    case preprocessing_op_or_punc::AMPERSAND_EQUAL:
        code = "aN";
        break;
    case preprocessing_op_or_punc::BAR_EQUAL:
        code = "oR";
        break;
    case preprocessing_op_or_punc::CARET_EQUAL:
        code = "eO";
        break;
    case preprocessing_op_or_punc::LESS_LESS:
        code = "ls";
        break;
    case preprocessing_op_or_punc::GREATER_GREATER:
        code = "rs";
        break;
    case preprocessing_op_or_punc::LESS_LESS_EQUAL:
        code = "lS";
        break;
    case preprocessing_op_or_punc::GREATER_GREATER_EQUAL:
        code = "rS";
        break;
    case preprocessing_op_or_punc::EQUAL_EQUAL:
        code = "eq";
        break;
    case preprocessing_op_or_punc::EXCLAMATION_EQUAL:
        code = "ne";
        break;
    case preprocessing_op_or_punc::LESS:
        code = "lt";
        break;
    case preprocessing_op_or_punc::GREATER:
        code = "gt";
        break;
    case preprocessing_op_or_punc::LESS_EQUAL:
        code = "le";
        break;
    case preprocessing_op_or_punc::GREATER_EQUAL:
        code = "ge";
        break;
    case preprocessing_op_or_punc::EXCLAMATION:
        code = "nt";
        break;
    case preprocessing_op_or_punc::AMPERSAND_AMPERSAND:
        code = "aa";
        break;
    case preprocessing_op_or_punc::BAR_BAR:
        code = "oo";
        break;
    case preprocessing_op_or_punc::PLUS_PLUS:
        code = "pp";
        break;
    case preprocessing_op_or_punc::MINUS_MINUS:
        code = "mm";
        break;
    case preprocessing_op_or_punc::EQUAL:
        code = "aS";
        break;
    case preprocessing_op_or_punc::LEFT_PARENTHESIS:
        code = "cl";
        break;
    case preprocessing_op_or_punc::LEFT_BRACKET:
        code = "ix";
        break;
    case preprocessing_op_or_punc::COMMA:
        code = "cm";
        break;
    case preprocessing_op_or_punc::ARROW_STAR:
        code = "pm";
        break;
    case preprocessing_op_or_punc::ARROW:
        code = "pt";
        break;
    case preprocessing_op_or_punc::SPACESHIP:
        code = "ss";
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

/*
 * The function a class of block scope (a local class, [class.local]) is
 * declared in, for e that is such a class, a class nested in one, or a
 * member of either; 0 otherwise.  A local class whose name another class
 * of the function also has would need a discriminator, which is not
 * written: no function then.
 */
entity_index itanium_mangler::local_function(entity_index e) const
{
    const entities &table = analysis.entity_table;
    entity_index c = e;
    if (table[c].kind != entity_kind::class_type)
        c = table[c].enclosing;
    while (c && table[c].kind == entity_kind::class_type &&
           table[table[c].enclosing].kind == entity_kind::class_type)
        c = table[c].enclosing;
    if (!c || table[c].kind != entity_kind::class_type || !table[c].enclosing ||
        table[table[c].enclosing].kind == entity_kind::namespace_entity)
        return 0;
    entity_index function = enclosing_function(table, c);
    if (!function)
        return 0;
    /* (a closure type's name is numbered: closure_type_name) */
    if (table[c].lambda)
        return function;
    for (entity_index other = 1; other < table.size(); other++)
        if (other != c && table[other].kind == entity_kind::class_type &&
            table[other].name == table[c].name && !table[other].lambda &&
            enclosing_function(table, other) == function)
            return UINT32_MAX;
    return function;
}

/* [abi:mangling-scope]: Z <function encoding> E, the scope of a
   <local-name>; main's encoding is its name alone (local_name) */
bool itanium_mangler::function_scope(entity_index function)
{
    if (function == UINT32_MAX)
        return false;
    const entity &f = analysis.entity_table[function];
    text("Z");
    size_t length = 0;
    const char *identifier = spelling(f.name, &length);
    if (f.enclosing == 0 && length == 4 && !memcmp(identifier, "main", 4)) {
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
    /* [impl.gnu] [impl.clang] the encoding of a function whose own symbol
       is not mangled (main) is its name alone, without its type:
       _ZZ4mainE1r */
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
        for (uint32_t i = 0; i < analysis.template_parameter_count(call); i++)
            text("Ty");
        /* [impl.clang] the template-head's requires-clause: Q and its
           expression */
        bool trailing = false;
        syntax_child constraint =
            analysis.lambda_constraint(table[c].lambda, &trailing);
        if (constraint.bits && !trailing) {
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
            k.closure_variable != variable)
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
    if (r.kind == type_kind::fundamental && !t.cv())
        return builtin_type(r.fundamental);
    if (r.kind == type_kind::placeholder && !t.cv()) {
        /* [abi:mangling-builtin]: a template's declared return type
           auto or decltype(auto), a builtin type:

               <builtin-type> ::= Da
                              ::= Dc
        */
        if (r.entity == 2)
            text("Dc");
        else
            text("Da");
        return true;
    }
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
    } else if (r.kind == type_kind::pointer_to_member) {
        /* [abi:mangle.pointer-to-member-type]: "Pointer-to-member types
           encode the class and member types":

               <pointer-to-member-type> ::= M <class type> <member type>
        */
        text("M");
        done =
            type(qualified_type::make(
                analysis.entity_table[r.entity].type.unqualified(), cv_none)) &&
            type(r.operand);
    } else if (r.kind == type_kind::atomic) {
        /* [abi:mangling-type]'s vendor extended type qualifier, as clang
           mangles _Atomic(T) ([ext:clang_atomic]) */
        text("U7_Atomic");
        done = type(r.operand);
    } else if (r.kind == type_kind::bit_int && !r.entity) {
        /* [abi:mangling-builtin]:

               <builtin-type> ::= DB <number> _
        */
        text("DB");
        number(r.bound);
        text("_");
        done = true;
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
        /* [abi:mangle.decltype]: decltype of a type-dependent expression,

               <decltype> ::= DT <expression> E

           the decltype-specifier's operand */
        node_index n =
            static_cast<node_index>(analysis.entity_table[r.entity].value);
        const syntax_tree &tree = analysis.syntax();
        text("DT");
        done = tree[n].child_count == 4 && encoding_template &&
               dependent_expression(tree.children(n)[2]);
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
    add_candidate(t);
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
 * A function type that is not a function's name (a parameter's pointer to
 * function, say) encodes its return type first; the section treats the
 * delimited and the bare type as one for substitution.  S1's functions
 * have no qualifiers, exception specification or C language linkage (Y).
 */
bool itanium_mangler::function_type(qualified_type t)
{
    /* "<exception-spec> ::= Do # non-throwing exception specification" */
    if (record(t).non_throwing)
        text("Do");
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
    /* [abi:mangle.array-type] with the bound a template parameter's
       <template-param>:
           <array-type> ::= A <instantiation-dependent array bound expression> _ <element type>
     */
    if (r.entity)
        template_param(analysis.entity_table[r.entity].value);
    else if (r.bound_known)
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
        if (!candidates[i].scope && same_candidate(candidates[i].type, t)) {
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
