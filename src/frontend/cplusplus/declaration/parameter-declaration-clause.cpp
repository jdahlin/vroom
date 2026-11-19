/*
 * parameter-declaration-clause.cpp: [dcl.fct] Functions: the parameters of a
 * function declarator.
 *
 *     parameters-and-qualifiers:
 *         ( parameter-declaration-clause ) cv-qualifier-seq_opt
 *             ref-qualifier_opt noexcept-specifier_opt attribute-specifier-seq_opt
 */
#include "../cplusplus-internal.h"
#include "frontend/cplusplus/sema.h"

namespace cplusplus
{

/*
 * Whether a '(' in an abstract declarator, or a parameter's declarator,
 * starts parameters-and-qualifiers rather than ( ptr-abstract-declarator ).
 * [dcl.ambig.res]/3: "the choice is between the declaration of a parameter
 * of type pointer to function and the declaration of a parameter with
 * redundant parentheses around the declarator-id. The resolution is to
 * consider the type-name as a simple-type-specifier rather than a
 * declarator-id."  So '(' followed by ')', '...' or anything that starts a
 * decl-specifier-seq (a type-name among them) is parameters.
 */
bool parser::parameters_follow(declarator_form form)
{
    if (!at(preprocessing_op_or_punc::LEFT_PARENTHESIS))
        return false;
    if (at(preprocessing_op_or_punc::RIGHT_PARENTHESIS, 1) ||
        at(preprocessing_op_or_punc::ELLIPSIS, 1))
        return true;
    if (at_attribute_specifier(1))
        return true; /* a parameter's attribute-specifier-seq */
    /* a type-name, not a name the parser cannot classify: (a) with a
       undeclared is a parenthesized declarator-id (unless this is the
       reading in which such a name is a type) */
    position++;
    bool specifier = at_decl_specifier(unknown_names_are_types);
    position--;
    (void)form;
    return specifier;
}

node_index parser::parameters_and_qualifiers(scope_index *parameters)
{
    /* [temp.names]/3: a > in brackets nested in a template-argument-list
       is an operator */
    flag_set greater(greater_is_operator, true);
    size_t mark = begin_node();
    token_index first = position;
    add_token();
    /* [basic.scope.param]/1: "A function parameter (including one appearing
       in a lambda-declarator) or function-local predefined variable has
       function parameter scope. ... If the nearest enclosing function
       declarator is not the declarator of a function definition, the
       potential scope ends at the end of that function declarator": the
       noexcept-specifier is in it, and so is the trailing-return-type
       (declarator.cpp enters the scope again for it) */
    struct parameter_scope {
        declared_names &names;
        ~parameter_scope()
        {
            names.leave();
        }
    } scope{names};
    scope_index made = names.enter(scope_kind::function_prototype);
    if (parameters)
        *parameters = made;
    parameter_declaration_clause();
    if (failed())
        return 0;
    if (!accept(preprocessing_op_or_punc::RIGHT_PARENTHESIS))
        return expected("')' to close the parameters", "dcl.fct");
    cv_qualifier_seq();
    /* ref-qualifier: & or && */
    if (!accept(preprocessing_op_or_punc::AMPERSAND))
        accept(preprocessing_op_or_punc::AMPERSAND_AMPERSAND);
    if (at(standard_keyword::noexcept_)) {
        noexcept_specifier();
        if (failed())
            return 0;
    } else if (at(standard_keyword::throw_)) {
        /* C++14's parameters-and-qualifiers has exception-specification_opt
           here, a dynamic-exception-specification or noexcept-specifier */
        dynamic_exception_specification();
        if (failed())
            return 0;
    }
    attribute_specifier_seq(true);
    if (failed())
        return 0;
    return finish_node(syntax_kind::parameters_and_qualifiers, mark, first);
}

/*
 *     parameter-declaration-clause:
 *         parameter-declaration-list_opt ..._opt
 *         parameter-declaration-list , ...
 *
 *     parameter-declaration-list:
 *         parameter-declaration
 *         parameter-declaration-list , parameter-declaration
 *
 * Nothing when the clause is empty.
 */
node_index parser::parameter_declaration_clause()
{
    size_t mark = begin_node();
    token_index first = position;
    if (at(preprocessing_op_or_punc::RIGHT_PARENTHESIS))
        return 0;
    if (accept(preprocessing_op_or_punc::ELLIPSIS))
        return finish_node(syntax_kind::parameter_declaration_clause, mark,
                           first);
    size_t list_mark = begin_node();
    token_index list_first = position;
    unsigned count = 0;
    bool ellipsis = false;
    for (;;) {
        parameter_declaration();
        if (failed())
            return 0;
        count++;
        if (at(preprocessing_op_or_punc::COMMA) &&
            at(preprocessing_op_or_punc::ELLIPSIS, 1)) {
            if (count > 1)
                finish_node(syntax_kind::parameter_declaration_list, list_mark,
                            list_first);
            add_token();
            add_token();
            ellipsis = true;
            break;
        }
        if (!accept(preprocessing_op_or_punc::COMMA))
            break;
    }
    if (!ellipsis) {
        if (count > 1)
            finish_node(syntax_kind::parameter_declaration_list, list_mark,
                        list_first);
        /* parameter-declaration-list ...: [dcl.fct]/4 "Where syntactically
           correct and where ... is not part of an abstract-declarator, , ...
           is synonymous with ..." */
        ellipsis = accept(preprocessing_op_or_punc::ELLIPSIS);
    }
    if (!ellipsis)
        return 0; /* parameter-declaration-list alone passes through */
    return finish_node(syntax_kind::parameter_declaration_clause, mark, first);
}

/*
 *     parameter-declaration:
 *         attribute-specifier-seq_opt decl-specifier-seq declarator
 *         attribute-specifier-seq_opt decl-specifier-seq declarator = initializer-clause
 *         attribute-specifier-seq_opt decl-specifier-seq abstract-declarator_opt
 *         attribute-specifier-seq_opt decl-specifier-seq abstract-declarator_opt = initializer-clause
 *
 * [dcl.fct.default]/1: "If an initializer-clause is specified in a
 * parameter-declaration this initializer-clause is used as a default
 * argument."
 */
node_index parser::parameter_declaration()
{
    size_t mark = begin_node();
    token_index first = position;
    {
        /* [ext.gnu] clang: in a parameter-declaration the standard's
           attribute-specifiers come before GCC's */
        flag_set ordered(standard_attributes_first, true);
        attribute_specifier_seq();
    }
    if (failed())
        return 0;
    /* C++23's explicit object parameter (P0847R7, "this
       decl-specifier-seq declarator") is not C++20's grammar: a later
       edition's, P9's */
    if (at(standard_keyword::this_))
        return beyond_this_step();
    decl_specifier_facts specifiers;
    /* [temp.res]/5: where the parameter's context says (a member's,
       a requires-expression's, a template-parameter's ...) */
    specifiers.implicit_typename =
        parameters_implicit_typename &&
        tokens.files.behavior.has(language_feature::implicit_typename);
    decl_specifier_seq(specifiers, false);
    if (failed())
        return 0;
    if (!specifiers.node)
        return expected("a decl-specifier-seq", "dcl.fct");
    /* [dcl.typedef]/1: "it shall not be used in the decl-specifier-seq of
       a parameter-declaration" */
    if (specifiers.typedef_)
        return violates("no typedef in a parameter-declaration", "dcl.typedef",
                        specifiers.typedef_at);
    declarator_facts facts;
    declarator(declarator_form::either, facts);
    if (failed())
        return 0;
    if (facts.name && !facts.qualified_id)
        names.declare_in(names.current(), facts.name, name_kind::other);
    if (accept(preprocessing_op_or_punc::EQUAL)) {
        /* [temp.res]/5: not in a default argument */
        flag_set implicit(parameters_implicit_typename, false);
        if (list_initialization_missing())
            return 0;
        if (collecting_members) {
            /* a default argument is a complete-class context
               ([class.mem]/6): not read in the first pass over the
               member-specification (member-declaration.cpp) */
            static const preprocessing_op_or_punc stops[] = {
                preprocessing_op_or_punc::COMMA,
                preprocessing_op_or_punc::RIGHT_PARENTHESIS,
                preprocessing_op_or_punc::ELLIPSIS};
            unparsed_until(stops, 3);
        } else if (member_declarator_context) {
            complete_class_context context(names);
            initializer_clause();
        } else {
            initializer_clause();
        }
        if (failed())
            return 0;
    }
    return finish_node(syntax_kind::parameter_declaration, mark, first);
}

/*
 *     parameter-declaration:
 *         attribute-specifier-seq_opt decl-specifier-seq declarator
 *         attribute-specifier-seq_opt decl-specifier-seq declarator = initializer-clause
 *         attribute-specifier-seq_opt decl-specifier-seq abstract-declarator_opt
 *         attribute-specifier-seq_opt decl-specifier-seq abstract-declarator_opt = initializer-clause
 *
 * [dcl.fct]/5: "The type of each parameter (including function parameter
 * packs) is determined from its own decl-specifier-seq and declarator."
 */
analysis_result semantic_analysis::parameter_declaration(
    ParameterDeclaration n, qualified_type &type, bool &unnamed,
    token_index &id, SyntaxElement &default_argument)
{
    id = 0;
    default_argument = SyntaxElement();
    /* [dcl.fct]/3: the attribute-specifier-seq_opt before the
       decl-specifier-seq appertains to the parameter: those without effect
       (libc++'s [[__maybe_unused__]]), and GCC's whose rules set nothing
       (libc++'s __attribute__((noescape))), are left out */
    using enum preprocessing_op_or_punc;
    uint32_t lead = 0;
    while (n[lead].is_node()) {
        auto c = n[lead];
        if (c.gnu_attribute_specifier()) {
            gnu_attributes g;
            analysis_result r = gnu_attribute_specifier(c, g);
            if (r != analysis_result::well_formed)
                return r;
            if (g.flags || g.aligned || g.section || g.abi_tag)
                break;
        } else if (!c.is(syntax_kind::attribute_specifier_seq,
                         syntax_kind::attribute_specifier) ||
                   !attributes_without_effect(c))
            break;
        lead++;
    }
    /* the children after the attributes left out */
    auto at = [&](uint32_t i) { return n[lead + i]; };
    uint32_t child_count = n.size() - lead;
    if (!at(0).decl_specifier_seq())
        return not_supported(n, "dcl.attr");
    specified s;
    analysis_result r = decl_specifier_seq(at(0), s, true);
    if (r != analysis_result::well_formed)
        return r;
    /* [dcl.type]/3: "at least one defining-type-specifier that is not a
       cv-qualifier shall appear in a complete type-specifier-seq or a
       complete decl-specifier-seq" */
    if (!s.has_type)
        return ill_formed(n,
                          "at least one defining-type-specifier that is not a "
                          "cv-qualifier shall appear in a complete "
                          "decl-specifier-seq",
                          "dcl.type");
    /* [ext:gnu_attributes] among a parameter's decl-specifiers: noreturn
       changes the type of the function it points to, the others have no
       rules for a parameter */
    if (s.gnu_flags || s.aligned || s.section)
        return not_supported(n, "ext.gnu");
    type = s.type;
    unnamed = true;
    declared_pack = 0;
    /* [dcl.fct.default]/1: "If an initializer-clause is specified in a
       parameter-declaration this initializer-clause is used as a default
       argument" (default_arguments analyses it) */
    uint32_t declarator_count = child_count;
    for (uint32_t i = 1; i + 1 < child_count; i++)
        if (at(i).is(EQUAL)) {
            if (i + 2 != child_count)
                return not_supported(n, "dcl.fct.default");
            default_argument = at(i + 1);
            declarator_count = i;
        }
    if (declarator_count == 1)
        return analysis_result::well_formed;
    if (declarator_count > 2)
        return not_supported(n, "dcl.fct");
    declarator_meaning m;
    pack_declarator = true;
    r = declarator(n, at(1), type, m);
    pack_declarator = false;
    declared_pack = m.pack;
    unnamed = !m.has_id;
    if (m.has_id)
        id = m.id;
    /* [ext:gnu_attributes] an attribute on a parameter: noreturn changes
       the type of the function it points to, the others have no rules for
       a parameter */
    if (r == analysis_result::well_formed &&
        (m.gnu_flags || m.aligned || m.section || m.asm_label))
        return not_supported(n, "ext.gnu");
    return r;
}

/*
 * [dcl.fct]/1: "In a declaration T D where D has the form
 *     D1 ( parameter-declaration-clause ) cv-qualifier-seq_opt
 *         ref-qualifier_opt noexcept-specifier_opt attribute-specifier-seq_opt
 * ... the type of the declarator-id in D is derived-declarator-type-list
 * noexcept_opt function of parameter-type-list cv-qualifier-seq_opt
 * ref-qualifier_opt returning T".
 */
analysis_result semantic_analysis::function(SyntaxRef parameters,
                                            qualified_type &type)
{
    /* [dcl.fct]/11: "Functions shall not have a return type of type array or
       function" */
    type_kind returned = type_table[type.unqualified()].kind;
    if (returned == type_kind::array || returned == type_kind::function)
        return ill_formed(parameters,
                          "functions shall not have a return type of type "
                          "array or function",
                          "dcl.fct");
    using enum preprocessing_op_or_punc;
    /* the parameter-declarations and the ellipsis, from the clause and its
       list: ( clause ) is the first and last child */
    owned_array<SyntaxRef> declarations;
    bool variadic = false, closed = false;
    owned_array<SyntaxElement> work;
    uint32_t cv = cv_none;
    bool may_qualify = in_member_declarator;
    /* [except.spec]/1: the noexcept-specifier */
    bool specified_exceptions = false, non_throwing = false;
    SyntaxRef exceptions = 0;
    for (uint32_t i = parameters.size(); i-- > 0;) {
        auto c = parameters[i];
        if (c.is(RIGHT_PARENTHESIS)) {
            closed = true;
        } else if (!closed) {
            /* a lambda-declarator's decl-specifier-seq (mutable, constexpr,
               consteval) and trailing-return-type: lambda_expression's */
            if (parameters.lambda_declarator() &&
                (c.decl_specifier_seq() || c.trailing_return_type() ||
                 (c.is_token() &&
                  tokens[c.token()].kind == token_kind::keyword)))
                continue;
            /* [ext:gnu_attributes] after the parameters: the
               declarator's (declarator takes them) */
            if (c.is_node() && only_gnu_attributes(tree, c))
                continue;
            /* [dcl.fct]/1: the cv-qualifier-seq of a member function's
               declarator ([class.mfct.non-static]/5) */
            standard_keyword kw = standard_keyword::none;
            if (c.is_token())
                kw = p.keyword_of(tokens[c.token()]);
            if (may_qualify && kw == standard_keyword::const_ &&
                !(cv & cv_const)) {
                cv |= cv_const;
                continue;
            }
            if (may_qualify && kw == standard_keyword::volatile_ &&
                !(cv & cv_volatile)) {
                cv |= cv_volatile;
                continue;
            }
            if (c.noexcept_specifier() && !specified_exceptions) {
                /* analysed once the parameters are known */
                specified_exceptions = true;
                exceptions = c;
                continue;
            }
            /* a generic lambda's requires-clause (lambda_constraint) */
            if (c.requires_clause())
                continue;
            /* a ref-qualifier, attributes, a cv-qualifier-seq of a type
               that is not a member function's */
            return not_supported(parameters, "dcl.fct");
        }
    }
    /* a function declarator of the parameters is not the member's */
    in_member_declarator = false;
    for (auto c : parameters.elements()) {
        if (c.is(RIGHT_PARENTHESIS))
            break;
        if (!c.is(LEFT_PARENTHESIS))
            work.append(c);
    }
    for (size_t k = 0; k < work.size(); k++) {
        auto c = work[k];
        if (c.is(COMMA))
            continue;
        if (c.is(ELLIPSIS)) {
            variadic = true;
            continue;
        }
        if (c.is(syntax_kind::parameter_declaration_clause,
                 syntax_kind::parameter_declaration_list)) {
            for (auto item : c.elements())
                work.append(item);
            continue;
        }
        if (!c.parameter_declaration())
            return not_supported(parameters, "dcl.fct");
        declarations.append(c);
    }
    owned_array<qualified_type> list;
    owned_array<parameter_meaning> named;
    uint32_t empty_pack = 0;
    for (size_t k = 0; k < declarations.size(); k++) {
        qualified_type t;
        bool unnamed = true;
        token_index id = 0;
        SyntaxElement default_argument;
        /* [temp.variadic]/5: a function parameter pack of an
           instantiation, whose type names a pack: one parameter per
           element */
        uint32_t elements = 0;
        token_index ellipsis = 0;
        analysis_result r = parameter_pack_expansion(
            declarations[k], k + 1 == declarations.size() && !variadic,
            elements, ellipsis);
        if (r != analysis_result::well_formed)
            return r;
        if (ellipsis) {
            uint32_t pack_name = 0;
            if (tokens[ellipsis + 1].kind == token_kind::identifier)
                pack_name = tokens.identifier(ellipsis + 1);
            bool saved = expanding;
            uint32_t saved_element = expansion_element;
            for (uint32_t e = 0; e < elements; e++) {
                expanding = true;
                expansion_element = e;
                r = parameter_declaration(declarations[k], t, unnamed, id,
                                          default_argument);
                expanding = saved;
                expansion_element = saved_element;
                if (r != analysis_result::well_formed)
                    return r;
                if (declared_pack != ellipsis || default_argument ||
                    depends_on_template_parameter(t))
                    return not_supported(declarations[k], "temp.variadic");
                const struct type &element = type_table[t.unqualified()];
                if ((element.kind == type_kind::fundamental &&
                     element.fundamental == fundamental_type::void_))
                    return not_supported(declarations[k], "temp.variadic");
                if (element.kind == type_kind::array)
                    t = qualified_type::make(
                        type_table.pointer_to(element.operand), cv_none);
                else if (element.kind == type_kind::function)
                    t = qualified_type::make(type_table.pointer_to(t), cv_none);
                list.append(qualified_type::make(t.unqualified(), cv_none));
                parameter_meaning parameter;
                parameter.type = t;
                parameter.declaration = declarations[k];
                parameter.pack_name = pack_name;
                named.append(parameter);
            }
            /* an empty pack's name: the pack of no parameters */
            if (!elements)
                empty_pack = pack_name;
            continue;
        }
        r = parameter_declaration(declarations[k], t, unnamed, id,
                                  default_argument);
        if (r != analysis_result::well_formed)
            return r;
        if (declared_pack) {
            /* [temp.variadic]/5: a template's own function parameter
               pack, of a type that names a template parameter pack
               (function_template_definition checks which), the last
               parameter; [dcl.fct]/22's T... of a type that names none is
               the clause's ellipsis, not analysed */
            if (k + 1 != declarations.size() || variadic || default_argument ||
                !depends_on_template_parameter(t))
                return not_supported(declarations[k], "temp.variadic");
            t = qualified_type::make(
                type_table.pack_expansion_of(
                    qualified_type::make(t.unqualified(), cv_none)),
                cv_none);
        }
        const struct type &record = type_table[t.unqualified()];
        bool is_void = record.kind == type_kind::fundamental &&
                       record.fundamental == fundamental_type::void_;
        if (is_void) {
            /* [dcl.fct]/4: "A parameter list consisting of a single unnamed
               parameter of non-dependent type void is equivalent to an
               empty parameter list.  Except for this special case, a
               parameter shall not have type cv void." */
            if (declarations.size() == 1 && unnamed && !t.cv() && !variadic)
                break;
            return ill_formed(declarations[k],
                              "except for a parameter list consisting of a "
                              "single unnamed parameter of non-dependent type "
                              "void, a parameter shall not have type cv void",
                              "dcl.fct");
        }
        /* [dcl.fct]/5: "After determining the type of each parameter, any
           parameter of type array of T or of function type T is adjusted to
           be pointer to T.  After producing the list of parameter types,
           any top-level cv-qualifiers modifying a parameter type are
           deleted when forming the function type." */
        /* [class.abstract]/3 (Note), [dcl.fct.def.general]/2: a
           parameter of abstract class type is checked where the function
           is defined or called: not analysed */
        if (abstract_class(t))
            return not_supported(declarations[k], "class.abstract");
        if (record.kind == type_kind::array)
            t = qualified_type::make(type_table.pointer_to(record.operand),
                                     cv_none);
        else if (record.kind == type_kind::function)
            t = qualified_type::make(type_table.pointer_to(t), cv_none);
        list.append(qualified_type::make(t.unqualified(), cv_none));
        parameter_meaning parameter;
        parameter.id = id;
        parameter.type = t;
        parameter.declaration = declarations[k];
        parameter.default_argument = default_argument;
        named.append(parameter);
    }
    /* the last function declarator to finish is a function definition's
       own: the ones in its parameters finish before it */
    function_parameters.clear();
    for (const parameter_meaning &parameter : named)
        function_parameters.append(parameter);
    function_empty_pack = empty_pack;
    function_cv = cv;
    function_exceptions_specified = specified_exceptions;
    if (exceptions) {
        analysis_result r =
            parameter_scope_noexcept(exceptions, may_qualify, non_throwing);
        if (r != analysis_result::well_formed)
            return r;
    }
    if (abstract_class(type))
        /* a return type of abstract class type: as a parameter's */
        return not_supported(parameters, "class.abstract");
    /* [except.spec]/2: part of the type since C++17 (P0012R1); before it
       a function's own, which nothing analysed asks about */
    if (!tokens.files.behavior.has(
            language_feature::exception_specification_in_type))
        non_throwing = false;
    type =
        qualified_type::make(type_table.function(type, list.data(), list.size(),
                                                 variadic, non_throwing),
                             cv_none);
    return analysis_result::well_formed;
}

/* [dcl.fct]/22: "An abbreviated function template is equivalent to a
   function template whose template-parameter-list includes one invented
   type template-parameter for each generic parameter type placeholder of
   the function declaration, in order of appearance": auto or
   type-constraint auto among a parameter's decl-specifiers */
bool semantic_analysis::generic_placeholder(SyntaxRef seq) const
{
    for (auto c : seq.elements()) {
        if (c.placeholder_type_specifier())
            c = c.back();
        if (c.is(standard_keyword::auto_))
            return true;
    }
    return false;
}

bool semantic_analysis::abbreviated_parameters(
    SyntaxRef declaration, owned_array<SyntaxRef> &parameters) const
{
    auto d = declaration;
    if (d.template_declaration()) {
        if (d.size() != 2 || !d[1].is_node())
            return true;
        d = d[1];
    }
    if (!d.function_definition() && !d.simple_declaration())
        return true;
    /* the declarator's parameters-and-qualifiers */
    SyntaxRef declarator;
    for (auto c : d.elements())
        if (c.is(syntax_kind::ptr_declarator, syntax_kind::noptr_declarator)) {
            declarator = c;
            break;
        }
    while (declarator.is(syntax_kind::ptr_declarator))
        declarator = declarator.back();
    if (!declarator.is(syntax_kind::noptr_declarator))
        return true;
    SyntaxElement list;
    for (auto c : declarator.elements())
        if (c.parameters_and_qualifiers())
            list = c;
    if (!list)
        return true;
    owned_array<SyntaxElement> items;
    for (auto c : list.elements()) {
        if (c.is(syntax_kind::parameter_declaration_list))
            for (auto item : c.elements())
                items.append(item);
        else
            items.append(c);
    }
    for (auto c : items) {
        if (!c.parameter_declaration())
            continue;
        for (auto seq : c.elements()) {
            if (!seq.decl_specifier_seq())
                continue;
            if (!generic_placeholder(seq))
                break;
            /* [dcl.fct]/22: "The invented type template-parameter is a
               template parameter pack if the corresponding parameter-
               declaration declares a function parameter pack" */
            for (token_index t = c.first_token(); t < c.end_token(); t++)
                if (SyntaxElement::from_token(t).is(
                        preprocessing_op_or_punc::ELLIPSIS))
                    return false;
            parameters.append(seq);
            break;
        }
    }
    return true;
}

uint32_t semantic_analysis::invented_parameter_name(SyntaxRef seq) const
{
    char spelling[24];
    int length =
        snprintf(spelling, sizeof spelling, "auto:%u", seq.index().index());
    return tokens.pp.identifier(spelling, static_cast<size_t>(length)) + 1;
}

bool semantic_analysis::invented_parameter_type(SyntaxRef seq,
                                                qualified_type &type) const
{
    entity_index e = lookup(invented_parameter_name(seq));
    if (!e || entity_table[e].kind != entity_kind::typedef_name)
        return false;
    type = entity_table[e].type;
    return true;
}

} // namespace cplusplus
