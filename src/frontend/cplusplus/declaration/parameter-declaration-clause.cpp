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
    explicit_object_clause = true;
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
    bool explicit_object = explicit_object_clause;
    explicit_object_clause = false;
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
        explicit_object_ok = explicit_object && count == 0;
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
    bool explicit_object = explicit_object_ok;
    explicit_object_ok = false;
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
    /* C++23's explicit object parameter (P0847R7: parameter-declaration:
       attribute-specifier-seq_opt this_opt decl-specifier-seq declarator,
       [dcl.fct]/6 of n4861's successor) is not C++20's grammar: the
       parser keeps `this` as a token of the parameter-declaration; which
       parameter it may be and what it means are the analysis's
       ([dcl.fct]/6 of C++23: the first parameter of a member function) */
    if (at(standard_keyword::this_)) {
        /* only the first parameter of a function's declarator; in a
           template the declaration's analysis (a redeclaration the
           explicit object parameter conflicts with, [over.load]/2.2) is
           not made until instantiation: a later step's */
        if (!explicit_object || names.within_template())
            return beyond_this_step();
        add_token();
    }
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
    using punct = preprocessing_op_or_punc;
    uint32_t lead = 0;
    while (n[lead].is_node()) {
        auto c = n[lead];
        if (c.gnu_attribute_specifier()) {
            gnu_attributes g;
            VTRY(r, gnu_attribute_specifier(c, g));
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
    /* [dcl.fct]/6 (C++23): this decl-specifier-seq declarator, an explicit
       object parameter: its meaning (the member function's object, its
       deduced type) is not analysed yet */
    if (at(0).is(standard_keyword::this_))
        return not_supported_because(n, "dcl.fct",
                                     "an explicit object parameter");
    if (!at(0).decl_specifier_seq())
        return not_supported_because(
            n, "dcl.attr", "a parameter without a decl-specifier-seq");
    specified s;
    VTRY(r, decl_specifier_seq(at(0), s, true));
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
        return not_supported_because(
            n, "ext.gnu", "a GNU attribute in a parameter's specifiers");
    type = s.type;
    unnamed = true;
    declared_pack = 0;
    /* [dcl.fct.default]/1: "If an initializer-clause is specified in a
       parameter-declaration this initializer-clause is used as a default
       argument" (default_arguments analyses it) */
    uint32_t declarator_count = child_count;
    for (uint32_t i = 1; i + 1 < child_count; i++)
        if (at(i).is(punct::EQUAL)) {
            if (i + 2 != child_count)
                return not_supported_because(
                    n, "dcl.fct.default",
                    "a default argument not at the end of the parameter");
            default_argument = at(i + 1);
            declarator_count = i;
        }
    if (declarator_count == 1)
        return misplaced_function_qualifiers(n, type, false);
    if (declarator_count > 2)
        return not_supported_because(
            n, "dcl.fct", "a parameter-declaration with extra parts");
    declarator_meaning m;
    pack_declarator = true;
    r = declarator(n, at(1), type, m);
    pack_declarator = false;
    if (r == analysis_result::well_formed) {
        VTRY_SET(r, misplaced_function_qualifiers(n, type, false));
    }
    declared_pack = m.pack;
    unnamed = !m.has_id;
    if (m.has_id)
        id = m.id;
    /* [ext:gnu_attributes] an attribute on a parameter: noreturn changes
       the type of the function it points to, the others have no rules for
       a parameter */
    if (r == analysis_result::well_formed &&
        (m.gnu_flags || m.aligned || m.section || m.asm_label))
        return not_supported_because(
            n, "ext.gnu", "a GNU attribute or asm label on a parameter");
    return r;
}

/*
 * [dcl.fct]/6: "A function type with a cv-qualifier-seq or a ref-qualifier
 * (including a type named by typedef-name) shall appear only as: the
 * function type for a non-static member function, the function type to
 * which a pointer to member refers, the top-level function type of a
 * function typedef declaration or alias-declaration, the type-id of a
 * template-argument for a type-parameter, or the type-id of the default
 * argument of a type-parameter."  Whether type, the type of what n
 * declares (or an operand of a pointer, a reference, a parameter or a
 * return type), is such a type where it may not appear; a member declared
 * by a typedef-name of one is a member function whose cv-qualifier-seq
 * analysis keeps as the entity's, not analysed.
 */
analysis_result semantic_analysis::misplaced_function_qualifiers(
    SyntaxRef n, qualified_type type, bool member)
{
    const struct type &f = type_table[type.unqualified()];
    if (f.kind != type_kind::function || (!f.function_cv && !f.function_ref))
        return analysis_result::well_formed;
    if (member)
        return not_supported_because(
            n, "dcl.fct", "a member's parameter of a qualified function type");
    return ill_formed(n,
                      "a function type with a cv-qualifier-seq or a "
                      "ref-qualifier shall appear only as the function type "
                      "for a non-static member function, the function type "
                      "to which a pointer to member refers, the top-level "
                      "function type of a typedef declaration or "
                      "alias-declaration, or a template-argument",
                      "dcl.fct");
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
    if (returned == type_kind::array || returned == type_kind::function) {
        /* a qualified function type is the more specific rule's */
        VTRY(r, misplaced_function_qualifiers(parameters, type, false));
        return ill_formed(parameters,
                          "functions shall not have a return type of type "
                          "array or function",
                          "dcl.fct");
    }
    using punct = preprocessing_op_or_punc;
    /* the parameter-declarations and the ellipsis, from the clause and its
       list: ( clause ) is the first and last child */
    owned_array<SyntaxRef> declarations2;
    bool variadic = false, closed = false;
    token_index clause_ellipsis = 0;
    owned_array<SyntaxElement> work;
    uint32_t cv = cv_none, type_cv = cv_none, type_ref = 0;
    bool may_qualify = in_member_declarator;
    /* [except.spec]/1: the noexcept-specifier */
    bool specified_exceptions = false, non_throwing = false;
    SyntaxRef exceptions = 0, dynamic_exceptions = 0;
    for (uint32_t i = parameters.size(); i-- > 0;) {
        auto c = parameters[i];
        if (c.is(punct::RIGHT_PARENTHESIS)) {
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
               declarator ([class.mfct.non-static]/5); [dcl.fct]/6 of
               another function type: part of the type, valid where the
               type is a pointer to member's or a typedef-name's
               (checked where the function type is used) */
            uint32_t *qualified = &cv;
            if (!may_qualify)
                qualified = &type_cv;
            if (auto seq = c.cv_qualifier_seq()) {
                bool all = true;
                uint32_t added = 0;
                for (auto q : seq.elements()) {
                    standard_keyword k = standard_keyword::none;
                    if (q.is_token())
                        k = q.keyword();
                    if (k == standard_keyword::const_)
                        added |= cv_const;
                    else if (k == standard_keyword::volatile_)
                        added |= cv_volatile;
                    else
                        all = false;
                }
                if (all && !(*qualified & added)) {
                    *qualified |= added;
                    continue;
                }
                return not_supported_because(
                    parameters, "dcl.fct",
                    "a misplaced cv-qualifier-seq in a function declarator");
            }
            standard_keyword kw = standard_keyword::none;
            if (c.is_token())
                kw = c.keyword();
            if (kw == standard_keyword::const_ && !(*qualified & cv_const)) {
                *qualified |= cv_const;
                continue;
            }
            if (kw == standard_keyword::volatile_ &&
                !(*qualified & cv_volatile)) {
                *qualified |= cv_volatile;
                continue;
            }
            /* [dcl.fct]/1 ref-qualifier: a member function's own is kept
               with the cv-qualifier-seq (cv_ref_lvalue, cv_ref_rvalue) */
            if (c.is(punct::AMPERSAND) || c.is(punct::AMPERSAND_AMPERSAND)) {
                bool lvalue = c.is(punct::AMPERSAND);
                if (may_qualify) {
                    if (cv & (cv_ref_lvalue | cv_ref_rvalue))
                        return not_supported_because(parameters, "dcl.fct",
                                                     "a second ref-qualifier");
                    if (lvalue)
                        cv |= cv_ref_lvalue;
                    else
                        cv |= cv_ref_rvalue;
                    continue;
                }
                if (!type_ref) {
                    if (lvalue)
                        type_ref = 1;
                    else
                        type_ref = 2;
                    continue;
                }
            }
            if (c.noexcept_specifier() && !specified_exceptions) {
                /* analysed once the parameters are known */
                specified_exceptions = true;
                exceptions = c;
                continue;
            }
            /* [except.spec]/1 (C++14): a dynamic-exception-specification;
               analysed once the parameters are known */
            if (c.dynamic_exception_specification() && !specified_exceptions) {
                specified_exceptions = true;
                dynamic_exceptions = c;
                continue;
            }
            /* a generic lambda's requires-clause (lambda_constraint) */
            if (c.requires_clause())
                continue;
            /* a ref-qualifier, attributes, a cv-qualifier-seq of a type
               that is not a member function's */
            {
                auto r = not_supported(parameters, "dcl.fct");
                char text[96];
                const char *what = "a token";
                if (c.is_node())
                    what = syntax_kind_name(c.kind());
                snprintf(text, sizeof text, "after the parameters: %s", what);
                last_unsupported_detail(text, strlen(text));
                return r;
            }
        }
    }
    /* a function declarator of the parameters is not the member's */
    in_member_declarator = false;
    for (auto c : parameters.elements()) {
        if (c.is(punct::RIGHT_PARENTHESIS))
            break;
        if (!c.is(punct::LEFT_PARENTHESIS))
            work.append(c);
    }
    for (size_t k = 0; k < work.size(); k++) {
        auto c = work[k];
        if (c.is(punct::COMMA))
            continue;
        if (c.is(punct::ELLIPSIS)) {
            variadic = true;
            clause_ellipsis = c.token();
            continue;
        }
        if (c.is(syntax_kind::parameter_declaration_clause,
                 syntax_kind::parameter_declaration_list)) {
            for (auto item : c.elements())
                work.append(item);
            continue;
        }
        if (!c.parameter_declaration())
            return not_supported_because(
                parameters, "dcl.fct",
                "an unexpected item in a parameter list");
        declarations2.append(c);
    }
    owned_array<qualified_type> list;
    owned_array<parameter_meaning> named;
    uint32_t empty_pack = 0;
    for (size_t k = 0; k < declarations2.size(); k++) {
        qualified_type t;
        bool unnamed = true;
        token_index id = 0;
        SyntaxElement default_argument;
        /* [temp.variadic]/5: a function parameter pack of an
           instantiation, whose type names a pack: one parameter per
           element */
        uint32_t elements = 0;
        token_index ellipsis = 0;
        VTRY(r, parameter_pack_expansion(declarations2[k],
                                         k + 1 == declarations2.size() &&
                                             !variadic,
                                         elements, ellipsis));
        /* [dcl.fct]/22: the clause's own "..." after the last parameter,
           whose type names a pack, is that pack's expansion (an
           instantiation's: R (*)(A...) with A bound to its elements) */
        bool clause_expansion = false;
        if (!ellipsis && variadic && k + 1 == declarations2.size() &&
            expansion_count(declarations2[k].first_token(),
                            declarations2[k].end_token(), elements)) {
            clause_expansion = true;
            ellipsis = clause_ellipsis;
        }
        if (ellipsis) {
            uint32_t pack_name = 0;
            if (!clause_expansion &&
                tokens[ellipsis + 1].kind == token_kind::identifier)
                pack_name = tokens.identifier(ellipsis + 1);
            for (uint32_t e = 0; e < elements; e++) {
                {
                    expansion_scope scope(*this, true, e);
                    r = parameter_declaration(declarations2[k], t, unnamed, id,
                                              default_argument);
                }
                if (r != analysis_result::well_formed)
                    return r;
                if ((!clause_expansion && declared_pack != ellipsis) ||
                    default_argument || depends_on_template_parameter(t))
                    return not_supported_because(
                        declarations2[k], "temp.variadic",
                        "a parameter pack form that is not expanded");
                const struct type &element = type_table[t.unqualified()];
                if ((element.kind == type_kind::fundamental &&
                     element.fundamental == fundamental_type::void_))
                    return not_supported_because(
                        declarations2[k], "temp.variadic",
                        "a parameter pack expanding to void");
                if (element.kind == type_kind::array)
                    t = qualified_type::make(
                        type_table.pointer_to(element.operand), cv_none);
                else if (element.kind == type_kind::function)
                    t = qualified_type::make(type_table.pointer_to(t), cv_none);
                list.append(qualified_type::make(t.unqualified(), cv_none));
                parameter_meaning parameter;
                parameter.type = t;
                parameter.declaration = declarations2[k];
                parameter.pack_name = pack_name;
                named.append(parameter);
            }
            /* an empty pack's name: the pack of no parameters */
            if (!elements)
                empty_pack = pack_name;
            if (clause_expansion)
                variadic = false;
            continue;
        }
        /* [basic.scope.param]/1: a parameter's name is in scope from its
           point of declaration ([basic.scope.pdecl]/1), so a later
           parameter's declaration (an array bound, a decltype) finds it */
        entity_index outer = current_scope;
        if (names_earlier_parameter(declarations2[k], named))
            open_parameter_scope(named, 0);
        bool named_before = argument_named;
        argument_named = false;
        r = parameter_declaration(declarations2[k], t, unnamed, id,
                                  default_argument);
        bool parameter_dependent = argument_named;
        argument_named = named_before || parameter_dependent;
        current_scope = outer;
        if (r != analysis_result::well_formed)
            return r;
        if (declared_pack) {
            /* [temp.variadic]/5: a template's own function parameter
               pack, of a type that names a template parameter pack
               (function_template_definition checks which), the last
               parameter; [dcl.fct]/22's T... of a type that names none is
               the clause's ellipsis, not analysed */
            /* ([dcl.fct]/? a pack need not be last: it is then a
               non-deduced context, which calls and ordering do not yet
               analyse) */
            if (variadic)
                return not_supported_because(
                    declarations2[k], "temp.variadic",
                    "a parameter pack before the ellipsis");
            if (default_argument)
                return not_supported_because(
                    declarations2[k], "temp.variadic",
                    "a parameter pack with a default argument");
            if (!depends_on_template_parameter(t))
                return not_supported_because(
                    declarations2[k], "temp.variadic",
                    "a parameter pack of a type naming no template parameter");
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
            if (declarations2.size() == 1 && unnamed && !t.cv() && !variadic)
                break;
            return ill_formed(declarations2[k],
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
            return not_supported_because(declarations2[k], "class.abstract",
                                         "a parameter of abstract class type");
        if (record.kind == type_kind::array)
            t = qualified_type::make(type_table.pointer_to(record.operand),
                                     cv_none);
        else if (record.kind == type_kind::function)
            t = qualified_type::make(type_table.pointer_to(t), cv_none);
        list.append(qualified_type::make(t.unqualified(), cv_none));
        parameter_meaning parameter;
        parameter.id = id;
        parameter.type = t;
        parameter.declaration = declarations2[k];
        parameter.default_argument = default_argument;
        parameter.argument_dependent = parameter_dependent;
        named.append(parameter);
    }
    /* [dcl.fct]/22: a trailing "..." after a parameter whose type is a
       template parameter pack (R (*)(A...)) is that pack's expansion, not
       C's ellipsis */
    if (variadic && !list.empty() &&
        names_template_parameter_pack(list.back())) {
        list.back() = qualified_type::make(
            type_table.pack_expansion_of(list.back()), cv_none);
        variadic = false;
    }
    /* the last function declarator to finish is a function definition's
       own: the ones in its parameters finish before it */
    function_parameters.clear();
    for (const parameter_meaning &parameter : named)
        function_parameters.append(parameter);
    function_empty_pack = empty_pack;
    function_cv = cv;
    /* [except.spec]/1: none written, the function's implicit one
       (resolved_exceptions) */
    exception_specification value;
    value.written = specified_exceptions;
    /* [temp.inst]/15: the exception specification of a specialization's
       member function is instantiated when it is needed, not with the class:
       its types are not analysed with the member's declaration (C++98
       made it with the class) */
    if (dynamic_exceptions &&
        !(may_qualify &&
          tokens.files.behavior.has(language_feature::rvalue_references) &&
          defining_class && entity_table[defining_class].instantiation)) {
        /* [diff.cpp14.except]/1: removed from C++17; clang's error,
           -Wno-dynamic-exception-spec turns it off, and keeps the
           function; an error of the template's definition, whatever the
           arguments, not of a specialization's */
        if (analysing &&
            !tokens.files.behavior.has(
                language_feature::dynamic_exception_specifications)) {
            VTRY(r, recoverable_extension_used(
                        diagnostic_id::sema_dynamic_exception_specification,
                        dynamic_exceptions.first_token()));
        }
        owned_array<uint32_t> types;
        VTRY(r,
             dynamic_exception_types(dynamic_exceptions, may_qualify, types));
        value = dynamic_exception_value(dynamic_exceptions, types);
    } else if (dynamic_exceptions) {
        /* not analysed with the declaration ([temp.inst]/15) */
        value.allows = exception_allowance::unknown;
    }
    deferred_noexcept = 0;
    /* [temp.inst]/15's specification, which is not instantiated yet: one
       that needs itself without end is not accepted for that */
    if (exceptions && may_qualify && defining_class &&
        entity_table[defining_class].instantiation &&
        noexcept_needs_itself(exceptions.first_token() - 1,
                              exceptions.end_token(),
                              entity_table[defining_class].name))
        return not_supported_because(
            exceptions, "temp.inst",
            "an exception specification that needs itself through its own "
            "template");
    if (exceptions) {
        size_t dependent_before = dependent_exception_specifications.size();
        VTRY(r,
             parameter_scope_noexcept(exceptions, may_qualify, non_throwing));
        if (dependent_exception_specifications.size() != dependent_before ||
            deferred_noexcept)
            value.allows = exception_allowance::unknown;
        else if (non_throwing)
            value.allows = exception_allowance::none;
        else
            value.allows = exception_allowance::all;
    }
    function_exceptions = value;
    if (abstract_class(type))
        /* a return type of abstract class type: as a parameter's */
        return not_supported_because(parameters, "class.abstract",
                                     "a return type that is an abstract class");
    /* [except.spec]/2: part of the type since C++17 (P0012R1); before it
       a function's own, which nothing analysed asks about */
    if (!tokens.files.behavior.has(
            language_feature::exception_specification_in_type))
        non_throwing = false;
    type = qualified_type::make(
        type_table.function(type, list.data(), list.size(), variadic,
                            non_throwing, type_cv, type_ref),
        cv_none);
    return analysis_result::well_formed;
}

/*
 * [except.spec]/1 (C++14): "type-id-list: type-id ..._opt ...", and
 * /3 of C++14: "A type cv T, 'array of T', or 'function returning T' in an
 * exception-specification is adjusted to T, pointer to T, or pointer to
 * function returning T, respectively.  A type denoted in an
 * exception-specification shall not denote an incomplete type, a pointer or
 * reference to an incomplete type other than cv void*, or an rvalue
 * reference type."  A member function's specification is a complete-class
 * context ([class.mem]/6): a class being defined is not incomplete there.
 * The exception specification is not part of the type before C++17 and a
 * dynamic one is potentially throwing after it.
 */
analysis_result
semantic_analysis::dynamic_exception_types(SyntaxRef n, bool member,
                                           owned_array<uint32_t> &types)
{
    for (auto c : n.elements()) {
        if (c.is(syntax_kind::type_id_list)) {
            VTRY(r, dynamic_exception_types(c, member, types));
            continue;
        }
        if (!c.is(syntax_kind::type_id))
            continue;
        qualified_type type;
        VTRY(r, type_id(c, type));
        types.append(type.unqualified());
        qualified_type base = type;
        const struct type *t = &type_table[base.unqualified()];
        bool through = false;
        if (t->kind == type_kind::pointer ||
            t->kind == type_kind::lvalue_reference) {
            through = true;
            base = t->operand;
            t = &type_table[base.unqualified()];
        }
        if (t->kind == type_kind::fundamental &&
            (t->fundamental != fundamental_type::void_ || through))
            continue;
        if (t->kind == type_kind::enumeration ||
            t->kind == type_kind::pointer_to_member ||
            t->kind == type_kind::template_parameter ||
            t->kind == type_kind::dependent)
            continue;
        if (t->kind == type_kind::class_type &&
            entity_table[t->entity].kind == entity_kind::class_type &&
            (member || size_of_type(base)))
            continue;
        return not_supported_because(
            c, "except.spec",
            "a dynamic exception specification type of this kind");
    }
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

/* whether a declarator's ... precedes its declarator-id, outside the
   parameters of a function declarator in it */
static bool declares_pack(SyntaxElement n)
{
    /* (a parameter-declaration's declarator, or one nested in it; not a
       parameters-and-qualifiers or an array bound's expression) */
    for (auto c : n.elements()) {
        if (c.is(preprocessing_op_or_punc::ELLIPSIS))
            return true;
        if (c.is(syntax_kind::ptr_declarator, syntax_kind::noptr_declarator,
                 syntax_kind::ptr_abstract_declarator,
                 syntax_kind::noptr_abstract_declarator) &&
            declares_pack(c))
            return true;
    }
    return false;
}

/* the parameter-declarations of a parameter-declaration-clause (or its
   list) and the "..." tokens, in order */
static void flatten_parameters(SyntaxElement n,
                               owned_array<SyntaxElement> &items)
{
    for (auto c : n.elements()) {
        if (c.is(syntax_kind::parameter_declaration_clause,
                 syntax_kind::parameter_declaration_list))
            flatten_parameters(c, items);
        else
            items.append(c);
    }
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
    /* the parameters-and-qualifiers of the function the declarator
       declares (not those of a function type in its return type) */
    SyntaxElement list;
    for (auto c : d.elements())
        if (c.is(syntax_kind::init_declarator, syntax_kind::declarator,
                 syntax_kind::gnu_declarator, syntax_kind::ptr_declarator,
                 syntax_kind::noptr_declarator)) {
            list = DeclaratorParts(c).parameters();
            break;
        }
    if (!list)
        return true;
    return abbreviated_clause(list, parameters);
}

/* the generic parameter type placeholders among the parameter-declarations
   of list (a parameters-and-qualifiers or a lambda-declarator), in order:
   abbreviated_parameters' */
bool semantic_analysis::abbreviated_clause(
    SyntaxElement list, owned_array<SyntaxRef> &parameters) const
{
    owned_array<SyntaxElement> items;
    flatten_parameters(list, items);
    for (size_t i = 0; i < items.size(); i++) {
        auto c = items[i];
        if (!c.parameter_declaration())
            continue;
        for (auto seq : c.elements()) {
            if (!seq.decl_specifier_seq())
                continue;
            if (!generic_placeholder(seq))
                break;
            /* [dcl.fct]/22: "The invented type template-parameter is a
               template parameter pack if the corresponding parameter-
               declaration declares a function parameter pack": its
               declarator's ... (not one of a nested function
               declarator's parameters), or the clause's own after the
               parameter (auto&&...: [dcl.fct]/21's ambiguity resolves to
               a pack, as the placeholder's type depends on its
               invented parameter) */
            bool pack = declares_pack(c);
            if (i + 1 < items.size() &&
                items[i + 1].is(preprocessing_op_or_punc::ELLIPSIS) && !pack) {
                /* the clause's ...: after a declarator-id C's variadic
                   ellipsis ([dcl.fct]/21: auto x...), after no
                   declarator or one of ptr-operators a pack (clang:
                   auto&&...); another abstract declarator ((*)(int)...)
                   is not decided here */
                bool named = false, plain = true;
                for (auto d : c.elements()) {
                    if (d.decl_specifier_seq() || d.is_token())
                        continue;
                    if (!d.is(syntax_kind::ptr_abstract_declarator))
                        named = true;
                    for (auto q : d.elements())
                        if (!q.is(syntax_kind::ptr_operator))
                            plain = false;
                }
                if (named)
                    pack = false;
                else if (plain)
                    pack = true;
                else
                    return false;
            }
            if (pack && !invented_pack(seq))
                invented_packs.append(seq.index().index());
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

bool semantic_analysis::invented_pack(SyntaxRef seq) const
{
    for (uint32_t k : invented_packs)
        if (k == seq.index().index())
            return true;
    return false;
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
