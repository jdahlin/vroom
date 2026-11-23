/*
 * placeholder-type-specifier.cpp: [dcl.spec.auto] Placeholder type specifiers
 * and [dcl.type.auto.deduct] Placeholder type deduction.  The grammar is in
 * decl-specifier.cpp.
 *
 * [dcl.spec.auto]/1: "A placeholder-type-specifier designates a
 * placeholder type that will be replaced later by deduction from an
 * initializer."  Analysed: a variable's declaration with an initializer
 * (/4, /7), a function's declared return type deduced from its return
 * statements (/3, /8, /9, /11; a function template's per specialization,
 * /12), auto before a trailing-return-type
 * (declarator-meaning.cpp), and decltype(auto) ([dcl.type.auto.deduct]/5).
 *
 * Not analysed: a generic parameter type placeholder (/2), a placeholder in
 * a new-expression or a template-parameter (/5), a type-constraint
 * ([dcl.type.auto.deduct]/6), copy-list-initialization (std::initializer_list,
 * [dcl.type.auto.deduct]/4), a member function's deduced return type, and a redeclaration of a function whose declared
 * return type has a placeholder (/13).
 */
#include "../cplusplus-internal.h"
#include "frontend/cplusplus/sema.h"

namespace cplusplus
{

/* (the type's entity: 1 auto, 2 decltype(auto), with the concept's entity
   above those two bits) */
qualified_type semantic_analysis::placeholder_type(bool decltype_auto,
                                                   entity_index named_concept)
{
    uint32_t which = named_concept << 2 | 1;
    if (decltype_auto)
        which = named_concept << 2 | 2;
    return qualified_type::make(
        type_table.of_entity(type_kind::placeholder, which), cv_none);
}

bool semantic_analysis::contains_placeholder(qualified_type t,
                                             bool *decltype_auto,
                                             entity_index *named_concept) const
{
    const struct type &r = type_table[t.unqualified()];
    switch (r.kind) {
    case type_kind::placeholder:
        if (decltype_auto)
            *decltype_auto = (r.entity & 3) == 2;
        if (named_concept)
            *named_concept = r.entity >> 2;
        return true;
    case type_kind::pointer:
    case type_kind::lvalue_reference:
    case type_kind::rvalue_reference:
    case type_kind::array:
    case type_kind::function:
        return contains_placeholder(r.operand, decltype_auto, named_concept);
    default:
        return false;
    }
}

/* [dcl.type.auto.deduct]/4: "T' is obtained by substituting the deduced U
   into P"; [dcl.ref]/6: a reference to a reference type U is a reference
   ("lvalue reference to T" if either is one, else "rvalue reference to
   T") */
qualified_type semantic_analysis::replace_placeholder(qualified_type t,
                                                      qualified_type by)
{
    const struct type r = type_table[t.unqualified()];
    switch (r.kind) {
    case type_kind::placeholder:
        return add_cv(by, t.cv());
    case type_kind::pointer:
        return qualified_type::make(
            type_table.pointer_to(replace_placeholder(r.operand, by)), t.cv());
    case type_kind::lvalue_reference:
    case type_kind::rvalue_reference:
        return reference_to(replace_placeholder(r.operand, by),
                            r.kind == type_kind::rvalue_reference);
    case type_kind::array:
        return qualified_type::make(
            array_with_element(r, replace_placeholder(r.operand, by)), t.cv());
    case type_kind::function:
        return function_returning(t, replace_placeholder(r.operand, by));
    default:
        return t;
    }
}

qualified_type semantic_analysis::function_returning(qualified_type f,
                                                     qualified_type returns)
{
    const struct type r = type_table[f.unqualified()];
    owned_array<qualified_type> parameters;
    const qualified_type *list = type_table.parameters(f.unqualified());
    for (uint32_t i = 0; i < r.parameter_count; i++)
        parameters.append(list[i]);
    return qualified_type::make(
        type_table.function(returns, parameters.data(), parameters.size(),
                            r.variadic, r.non_throwing, r.function_cv,
                            r.function_ref),
        f.cv());
}

/* a type deduced as an array, a function or void: what the declaration
   then means (an object of that type, a function returning it) is not
   analysed */
analysis_result semantic_analysis::deduced_type_supported(SyntaxRef at,
                                                          qualified_type t)
{
    const struct type record = type_table[t.unqualified()];
    if (record.kind == type_kind::array || record.kind == type_kind::function ||
        (record.kind == type_kind::fundamental &&
         record.fundamental == fundamental_type::void_))
        return not_supported(at, "dcl.type.auto.deduct");
    return analysis_result::well_formed;
}

/*
 * [over.load]/3: functions of one name whose parameter-type-lists differ are
 * different functions, so a declaration with a placeholder return type of
 * parameters no function of the name has redeclares none ([dcl.spec.auto]/13
 * is not analysed for redeclarations).  PRIOR is what the name finds: a
 * function and its overloads, or the name a function template holds (the
 * non-template functions of the name are template_name_functions,
 * [temp.over]/1).  Anything else, or a function of the same parameters, is
 * not decided here.
 */
bool semantic_analysis::other_function_of_placeholder(entity_index prior,
                                                      entity_index scope,
                                                      qualified_type type) const
{
    const entity &first = entity_table[prior];
    if (first.kind == entity_kind::function) {
        for (entity_index o = prior; o; o = entity_table[o].next_overload)
            if (entity_table[o].kind != entity_kind::function ||
                same_parameters(type_table, entity_table[o].type.unqualified(),
                                type.unqualified()))
                return false;
        return true;
    }
    if (first.kind == entity_kind::none && first.held_by_template &&
        first.enclosing == scope) {
        for (const template_name_function &x : template_name_functions)
            if (x.name == prior &&
                same_parameters(type_table,
                                entity_table[x.function].type.unqualified(),
                                type.unqualified()))
                return false;
        return true;
    }
    return false;
}

/*
 * [dcl.spec.auto]/4: "The placeholder type shall appear as one of the
 * decl-specifiers in the decl-specifier-seq and the decl-specifier-seq
 * shall be followed by one or more declarators, each of which shall be
 * followed by a non-empty initializer."  /3: a function's declared return
 * type.  /13: "Redeclarations or specializations of a function or function
 * template with a declared return type that uses a placeholder type shall
 * also use that placeholder, not a deduced type.  Similarly, redeclarations
 * or specializations of a function or function template with a declared
 * return type that does not use a placeholder type shall not use a
 * placeholder": not analysed.  A declaration with a placeholder of a name
 * already declared in its scope is unsupported, and so is a function
 * declared without a placeholder whose name has a function with a
 * placeholder return type; a later declaration without a placeholder of
 * a variable declared with one is analysed as any redeclaration of a
 * variable ([basic.link]/11: the types are the same, the deduced type
 * being the variable's).
 */
analysis_result semantic_analysis::placeholder_declaration(
    SyntaxRef declaration, const declarator_meaning &m, qualified_type type,
    entity_kind kind, bool placeholder, bool initialized)
{
    /* [dcl.spec.auto]/7: "If the init-declarator-list contains more than
       one init-declarator, they shall all form declarations of
       variables."  (CWG1265: a declarator with a trailing-return-type, a
       function's, in such a list is so ill-formed) */
    if (placeholder_group && kind == entity_kind::function)
        return ill_formed_at_token(m.id,
                                   "if the init-declarator-list contains more "
                                   "than one init-declarator, they shall all "
                                   "form declarations of variables",
                                   "dcl.spec.auto");
    /* [dcl.type.auto.deduct]/5: "If the placeholder-type-specifier is of
       the form type-constraint_opt decltype(auto), T shall be the
       placeholder alone." */
    bool decltype_auto = false;
    if (placeholder && contains_placeholder(type, &decltype_auto) &&
        decltype_auto) {
        qualified_type t = type;
        if (kind == entity_kind::function)
            t = type_table[type.unqualified()].operand;
        if (type_table[t.unqualified()].kind != type_kind::placeholder ||
            t.cv())
            return ill_formed_at_token(m.id,
                                       "if the placeholder-type-specifier is "
                                       "of the form decltype(auto), T shall "
                                       "be the placeholder alone",
                                       "dcl.type.auto.deduct");
    }
    /* N3638 (C++14): a function's deduced return type */
    if (placeholder && kind == entity_kind::function &&
        !tokens.files.behavior.has(language_feature::return_type_deduction))
        return not_supported(declaration, "dcl.spec.auto");
    if (placeholder &&
        (kind == entity_kind::typedef_name || m.qualifier ||
         m.qualified_namespace ||
         /* (a member function template's: its name is the
                           template's, held by the class,
                           member-template.cpp) */
         /* (an allocation function's return type is checked by
            allocation_function_declaration) */
         (m.operator_function && !in_member_template_declarator &&
          m.allocation != allocation_function::none) ||
         m.literal_operator || m.template_id || m.destructor))
        return not_supported(declaration, "dcl.spec.auto");
    if (tokens[m.id].kind != token_kind::identifier)
        return analysis_result::well_formed;
    entity_index scope = current_scope;
    if (m.qualifier)
        scope = m.qualifier;
    else if (m.qualified_namespace)
        scope = m.namespace_qualifier;
    entity_index prior = entity_table.find(scope, tokens.identifier(m.id));
    if (!placeholder) {
        for (entity_index f = prior; f; f = entity_table[f].next_overload)
            if (entity_table[f].kind == entity_kind::function &&
                entity_table[f].placeholder_return)
                return not_supported(declaration, "dcl.spec.auto");
        return analysis_result::well_formed;
    }
    if (prior && (kind != entity_kind::function ||
                  !other_function_of_placeholder(prior, scope, type)))
        return not_supported(declaration, "dcl.spec.auto");
    if (kind == entity_kind::variable && !initialized)
        return ill_formed_at_token(m.id,
                                   "the decl-specifier-seq shall be followed "
                                   "by one or more declarators, each of which "
                                   "shall be followed by a non-empty "
                                   "initializer",
                                   "dcl.spec.auto");
    return analysis_result::well_formed;
}

/*
 * [dcl.type.auto.deduct]/4: "If the placeholder-type-specifier is of the
 * form type-constraint_opt auto, the deduced type T' replacing T is
 * determined using the rules for template argument deduction.  Obtain P
 * from T by replacing the occurrences of type-constraint_opt auto either
 * with a new invented type template parameter U ...  Deduce a value for U
 * using the rules of template argument deduction from a function call
 * ([temp.deduct.call]), where P is a function template parameter type and
 * the corresponding argument is E.  If the deduction fails, the
 * declaration is ill-formed."  /5: "If the placeholder-type-specifier is of
 * the form type-constraint_opt decltype(auto), T shall be the placeholder
 * alone.  The type deduced for T is determined as described in
 * [dcl.type.simple], as though E had been the operand of the decltype."
 */
analysis_result semantic_analysis::deduce_placeholder(
    SyntaxRef at, qualified_type declared, const expression_meaning &e,
    bool named, qualified_type &deduced, qualified_type &replacement)
{
    bool decltype_auto = false;
    entity_index named_concept = 0;
    contains_placeholder(declared, &decltype_auto, &named_concept);
    const struct type argument = type_table[e.type.unqualified()];
    if (argument.kind == type_kind::none ||
        argument.kind == type_kind::placeholder ||
        argument.kind == type_kind::dependent || e.bit_field_width)
        return not_supported(at, "dcl.type.auto.deduct");
    if (decltype_auto) {
        /* (T is the placeholder alone: placeholder_declaration) */
        if (!decltype_of(e, named, deduced))
            return not_supported(at, "dcl.type.auto.deduct");
        replacement = deduced;
        return placeholder_constraint(at, named_concept, replacement);
    }
    if (!invented_parameter) {
        invented_parameter = entity_table.make(entity_kind::typedef_name, 0, 0);
        entity_table[invented_parameter].type = qualified_type::make(
            type_table.of_entity(type_kind::template_parameter,
                                 invented_parameter),
            cv_none);
        entity_table[invented_parameter].value = 0;
    }
    qualified_type p =
        replace_placeholder(declared, entity_table[invented_parameter].type);
    qualified_type a = e.type;
    adjust_call_deduction(p, a, e.category);
    owned_array<template_argument> values;
    owned_array<bool> set;
    values.append(template_argument());
    set.append(false);
    /* (a deduction deduce_type does not make may still succeed by
       [temp.deduct.call]/4's derived class or qualification conversion) */
    if (!deduce_type(p, a, values, set) || !set[0])
        return not_supported(at, "dcl.type.auto.deduct");
    replacement = values[0].type;
    deduced = replace_placeholder(declared, replacement);
    return placeholder_constraint(at, named_concept, replacement);
}

/*
 * [dcl.type.auto.deduct]/6: "For a placeholder-type-specifier with a
 * type-constraint, the immediately-declared constraint ([temp.param]) of
 * the type-constraint for the type deduced for the placeholder shall be
 * satisfied."  ([temp.param]/4: of C, C<U>.)
 */
analysis_result semantic_analysis::placeholder_constraint(
    SyntaxRef at, entity_index named_concept, qualified_type u)
{
    if (!named_concept)
        return analysis_result::well_formed;
    owned_array<template_argument> arguments;
    template_argument a;
    a.kind = template_argument::kind_t::type;
    a.type = u;
    arguments.append(a);
    bool satisfied = false;
    analysis_result r =
        concept_satisfied(at, concept_of(named_concept), arguments, satisfied);
    if (r != analysis_result::well_formed || satisfied)
        return r;
    return ill_formed(at,
                      "the immediately-declared constraint of the "
                      "type-constraint for the type deduced for the "
                      "placeholder shall be satisfied",
                      "dcl.type.auto.deduct");
}

/* the assignment-expression of an initializer "= E", "( E )" or "{ E }"
   (direct: not the first; list: the last), false for another form */
bool semantic_analysis::initializer_form(SyntaxElement init,
                                         SyntaxElement &clause, bool &direct,
                                         bool &list) const
{
    InitializerParts parts(init);
    direct = parts.form != InitializerParts::syntax_form::equals;
    list = parts.form == InitializerParts::syntax_form::braced;
    SyntaxElement only;
    switch (parts.form) {
    case InitializerParts::syntax_form::equals:
        only = parts.clause;
        break;
    case InitializerParts::syntax_form::parenthesized:
        only = parts.list;
        break;
    case InitializerParts::syntax_form::braced:
        /* { E }: one initializer-clause between the braces */
        if (parts.clause.size() == 3 &&
            !parts.clause[1].is(preprocessing_op_or_punc::RIGHT_BRACE))
            only = parts.clause[1];
        break;
    case InitializerParts::syntax_form::none:
        break;
    }
    if (!only ||
        only.is(syntax_kind::initializer_list, syntax_kind::braced_init_list))
        return false;
    clause = only;
    return true;
}

/*
 * [dcl.type.auto.deduct]/2: "for a variable declared with a type that
 * contains a placeholder type, T is the declared type of the variable and E
 * is the initializer.  If the initialization is direct-list-initialization,
 * the initializer shall be a braced-init-list containing only a single
 * assignment-expression and E is the assignment-expression".
 * [dcl.spec.auto]/4: "In an initializer of the form ( expression-list ) the
 * expression-list shall be a single assignment-expression."
 */
analysis_result semantic_analysis::deduced_initializer(SyntaxElement init,
                                                       entity_index e,
                                                       qualified_type &type,
                                                       initialized_value &value)
{
    if (init.is_token())
        return not_supported(current_declaration, "dcl.init");
    SyntaxElement clause;
    bool direct = false, list = false;
    if (!initializer_form(init, clause, direct, list))
        /* copy-list-initialization (std::initializer_list<U>), an
           expression-list or braced-init-list of other than one
           expression */
        return not_supported(init.index(), "dcl.type.auto.deduct");
    expression_meaning m;
    VTRY(r, expression(clause, m));
    return deduced_initialization(init, m, names_entity(clause), direct, list,
                                  e, type, value);
}

/* ... the initializer's expression analysed as m (named: names_entity of
   it) */
analysis_result semantic_analysis::deduced_initialization(
    SyntaxRef n, expression_meaning &m, bool named, bool direct, bool list,
    entity_index e, qualified_type &type, initialized_value &value)
{
    qualified_type replacement;
    VTRY(r, deduce_placeholder(n, type, m, named, type, replacement));
    /* [dcl.spec.auto]/7: "if the type that replaces the placeholder type is
       not the same in each deduction, the program is ill-formed" */
    if (declaration_deduced.unqualified() &&
        !(declaration_deduced == replacement))
        return ill_formed(n,
                          "if the type that replaces the placeholder type is "
                          "not the same in each deduction, the program is "
                          "ill-formed",
                          "dcl.spec.auto");
    declaration_deduced = replacement;
    entity_table[e].type = type;
    /* [basic.link]/8: the linkage a variable's type implies (declare's
       rules) was not known when its name was declared */
    entity_index closure = 0;
    if (type_table[type.unqualified()].kind == type_kind::class_type &&
        entity_table[type_table[type.unqualified()].entity].lambda)
        closure = type_table[type.unqualified()].entity;
    if (closure && entity_table[closure].closure_variable == e) {
        /* [abi:closure-types]: a closure type of an inline variable's
           initializer is named after the variable in every translation
           unit */
    } else if (entity_table[e].linkage_of_name == linkage::external &&
               (type_without_external_linkage(type) ||
                unnamed_class_object(type))) {
        /* [impl.clang] a closure type unique to the translation unit: the
           variable's symbol is local, as declare() makes one of an unnamed
           class's */
        if (!closure)
            return not_supported(n, "basic.link");
        entity_table[e].linkage_of_name = linkage::internal;
        entity_table[e].unnamed_type_linkage = true;
    }
    VTRY_SET(r, deduced_type_supported(n, type));
    return initialize_with(n, m, type, direct, list, value);
}

/*
 * [dcl.type.auto.deduct]/2: "for a non-discarded return statement that
 * occurs in a function declared with a return type that contains a
 * placeholder type, T is the declared return type and E is the operand of
 * the return statement.  If the return statement has no operand, then E is
 * void().  In the case of a return statement with no operand or with an
 * operand of type void, T shall be either type-constraint_opt
 * decltype(auto) or cv type-constraint_opt auto."  [dcl.spec.auto]/8: "If
 * the type deduced is not the same in each deduction, the program is
 * ill-formed."  /11: "Once a non-discarded return statement has been seen
 * in a function, however, the return type deduced from that statement can
 * be used in the rest of the function".
 */
analysis_result semantic_analysis::deduce_return(SyntaxRef at,
                                                 const expression_meaning *e,
                                                 bool named)
{
    qualified_type declared = function_state.declared_returns;
    qualified_type deduced;
    bool void_operand =
        !e || (e->category == value_category::prvalue &&
               fundamental_of(e->type) == fundamental_type::void_);
    if (void_operand) {
        if (type_table[declared.unqualified()].kind != type_kind::placeholder)
            return ill_formed(at,
                              "in the case of a return statement with no "
                              "operand or with an operand of type void, T "
                              "shall be either decltype(auto) or cv auto",
                              "dcl.type.auto.deduct");
        /* (cv auto deduces cv void) */
        if (declared.cv())
            return not_supported(at, "dcl.type.auto.deduct");
        deduced = qualified_type::make(
            type_table.fundamental(fundamental_type::void_), cv_none);
    } else {
        qualified_type replacement;
        VTRY(r,
             deduce_placeholder(at, declared, *e, named, deduced, replacement));
        VTRY_SET(r, deduced_type_supported(at, deduced));
    }
    if (function_state.deduced) {
        if (!(deduced == function_state.returns))
            return ill_formed(at,
                              "if the type deduced is not the same in each "
                              "deduction, the program is ill-formed",
                              "dcl.spec.auto");
        return analysis_result::well_formed;
    }
    function_state.deduced = true;
    function_state.returns = deduced;
    entity_index f = function_state.function;
    entity_table[f].type = function_returning(entity_table[f].type, deduced);
    for (declared_name &d : this->declared)
        if (d.entity == f)
            d.type = entity_table[f].type;
    return analysis_result::well_formed;
}

/*
 * [dcl.spec.auto]/11: "If the name of an entity with an undeduced
 * placeholder type appears in an expression, the program is ill-formed."
 * A variable in its own initializer, a function declared but not defined,
 * or used in its body before a return statement.  A variable or function
 * whose initializer or body analysis did not finish is unsupported.
 */
analysis_result semantic_analysis::undeduced_use(token_index t, entity_index e)
{
    const entity &x = entity_table[e];
    bool undeduced = false;
    if (x.kind == entity_kind::variable)
        undeduced = e == deducing_variable;
    else if (x.kind == entity_kind::function)
        undeduced = !x.defined ||
                    (function_state.active && function_state.function == e);
    if (!undeduced)
        return not_supported(current_declaration, "dcl.spec.auto");
    return ill_formed_at_token(t,
                               "if the name of an entity with an undeduced "
                               "placeholder type appears in an expression, "
                               "the program is ill-formed",
                               "dcl.spec.auto");
}

/*
 * [dcl.fct]/2: "In a declaration T D where D has the form D1 (
 * parameter-declaration-clause ) cv-qualifier-seq_opt ref-qualifier_opt
 * noexcept-specifier_opt attribute-specifier-seq_opt trailing-return-type
 * and the type of the contained declarator-id in the declaration T D1 is
 * "derived-declarator-type-list T", T shall be the single type-specifier
 * auto.  The type of the declarator-id in D is "derived-declarator-type-list
 * noexcept_opt function of parameter-type-list cv-qualifier-seq_opt
 * ref-qualifier_opt returning U", where ... U is the type specified by the
 * trailing-return-type".  Analysed when the derived-declarator-type-list is
 * empty (D1 is the declarator-id and its parameters), the
 * trailing-return-type in the scope of the parameters
 * ([basic.scope.param]/1).
 */
analysis_result semantic_analysis::trailing_return(SyntaxRef declaration,
                                                   SyntaxRef n,
                                                   qualified_type &type,
                                                   declarator_meaning &m)
{
    if (n.size() != 2 || !n[1].is(syntax_kind::trailing_return_type))
        return not_supported(n, "dcl.fct");
    qualified_type auto_type = placeholder_type(false);
    /* (T other than auto: clang takes S(s)()->m, which [dcl.ambig.res]/1
       makes such a declaration, for an expression) */
    if (!(type == auto_type))
        return not_supported(n, "dcl.fct");
    VTRY(r, declarator(declaration, n[0], type, m));
    const struct type f = type_table[type.unqualified()];
    if (f.kind != type_kind::function || !(f.operand == auto_type))
        return not_supported(n, "dcl.fct");
    auto trailing = n[1];
    if (trailing.size() != 2 || !trailing[1].is(syntax_kind::type_id))
        return not_supported(n, "dcl.fct");
    /* the type-id's own function declarators would replace the
       parameters and cv-qualifier-seq this declarator's has */
    owned_array<parameter_meaning> parameters;
    for (const parameter_meaning &parameter : function_parameters)
        parameters.append(parameter);
    uint32_t cv = function_cv, empty_pack = function_empty_pack;
    qualified_type returned;
    entity_index outer = current_scope;
    open_parameter_scope();
    r = type_id(trailing[1], returned);
    current_scope = outer;
    function_parameters.clear();
    for (const parameter_meaning &parameter : parameters)
        function_parameters.append(parameter);
    function_cv = cv;
    function_empty_pack = empty_pack;
    if (r != analysis_result::well_formed)
        return r;
    /* [dcl.fct]/11: "Functions shall not have a return type of type array
       or function" */
    type_kind k = type_table[returned.unqualified()].kind;
    if (k == type_kind::array || k == type_kind::function)
        return ill_formed(n,
                          "functions shall not have a return type of type "
                          "array or function",
                          "dcl.fct");
    if (abstract_class(returned))
        return not_supported(n, "class.abstract");
    type = replace_placeholder(type, returned);
    return analysis_result::well_formed;
}

/* [dcl.spec.auto]/3: "If the declared return type of the function
   contains a placeholder type, the return type of the function is deduced
   from non-discarded return statements, if any, in the body of the
   function" */
void semantic_analysis::begin_return_deduction(qualified_type returns)
{
    function_state.declared_returns = returns;
    function_state.deducing = contains_placeholder(returns);
    function_state.deduced = false;
}

/* [dcl.spec.auto]/9: "If a function with a declared return type that uses
   a placeholder type has no non-discarded return statements, the return
   type is deduced as though from a return statement with no operand at the
   closing brace of the function body." */
analysis_result semantic_analysis::finish_return_deduction(
    SyntaxRef definition, analysis_result result, qualified_type &returns)
{
    if (function_state.deducing && !function_state.deduced &&
        result == analysis_result::well_formed)
        result = deduce_return(definition, nullptr, false);
    function_state.deducing = false;
    returns = function_state.returns;
    return result;
}

} // namespace cplusplus
