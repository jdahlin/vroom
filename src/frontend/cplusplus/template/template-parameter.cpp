/*
 * template-parameter.cpp: [temp.param] Template parameters.
 *
 *     template-parameter:
 *         type-parameter
 *         parameter-declaration
 *
 *     type-parameter:
 *         type-parameter-key ..._opt identifier_opt
 *         type-parameter-key identifier_opt = type-id
 *         type-constraint ..._opt identifier_opt
 *         type-constraint identifier_opt = type-id
 *         template-head type-parameter-key ..._opt identifier_opt
 *         template-head type-parameter-key identifier_opt = id-expression
 *
 *     type-parameter-key:
 *         class
 *         typename
 *
 *     type-constraint:
 *         nested-name-specifier_opt concept-name
 *         nested-name-specifier_opt concept-name < template-argument-list_opt >
 *
 * [temp.param]/2: "There is no semantic difference between class and
 * typename in a type-parameter-key. typename followed by an
 * unqualified-id names a template type parameter. typename followed by a
 * qualified-id denotes the type in a non-type parameter-declaration. A
 * template-parameter of the form class identifier is a type-parameter."
 * So a key is a type-parameter's when ..., an identifier, ',', '>' or '='
 * follow it, and otherwise begins a parameter-declaration (class X* p,
 * typename T::type n).  A type-constraint followed by auto or decltype is
 * a placeholder-type-specifier, so the parameter is a parameter-declaration
 * ([dcl.spec.auto]).
 */
#include "../cplusplus-internal.h"

namespace cplusplus
{

namespace
{

/* after a type-parameter-key or type-constraint at tokens[ahead]: ..._opt
   identifier_opt, then what ends a type-parameter */
bool type_parameter_follows(parser &p, unsigned ahead)
{
    if (p.at(preprocessing_op_or_punc::ELLIPSIS, ahead))
        ahead++;
    if (p.at_identifier(ahead))
        ahead++;
    if (p.at(preprocessing_op_or_punc::COMMA, ahead) ||
        p.at(preprocessing_op_or_punc::EQUAL, ahead))
        return true;
    unsigned saved = p.position;
    p.position += ahead;
    bool closing = p.at_closing_angle();
    p.position = saved;
    return closing;
}

/* ..._opt identifier_opt, = default_opt of a type-parameter whose key,
   constraint or template-head is pending since mark: the identifier is a
   template parameter of kind */
node_index type_parameter_rest(parser &p, size_t mark, token_index first,
                               name_kind kind, bool template_template)
{
    bool pack = false;
    if (p.at(preprocessing_op_or_punc::ELLIPSIS)) {
        if (!p.tokens.files.behavior.has(language_feature::variadic_templates))
            return p.beyond_this_step();
        p.add_token();
        pack = true;
    }
    uint32_t name = 0;
    if (p.at_identifier()) {
        name = p.tokens.identifier(p.position);
        p.add_token();
    }
    if (p.at(preprocessing_op_or_punc::EQUAL)) {
        /* the grammar has no default for a pack ([temp.param]/14) */
        if (pack)
            return p.expected("no default argument for a template parameter "
                              "pack",
                              "temp.param");
        p.add_token();
        if (template_template) {
            /* [temp.arg.template]/1: "A template-argument for a template
               template-parameter shall be the name of a class template or
               an alias template" */
            if (p.at_identifier() &&
                !p.at(preprocessing_op_or_punc::COLON_COLON, 1) &&
                !p.at(preprocessing_op_or_punc::LESS, 1)) {
                name_kind found =
                    p.names.lookup_unqualified(p.tokens.identifier(p.position));
                if (found != name_kind::template_name &&
                    found != name_kind::not_declared)
                    return p.violates("a class template or alias template as "
                                      "a template template-parameter's "
                                      "default",
                                      "temp.arg.template", p.position);
            }
            p.id_expression(false);
        } else {
            /* [temp.res]/5: a default argument of a type-parameter is a
               type-id-only context */
            flag_set only(p.type_id_only, true);
            p.type_id();
        }
        if (p.failed())
            return 0;
    }
    /* [basic.scope.pdecl]/9: "The point of declaration for a template
       parameter is immediately after its complete template-parameter." */
    if (name)
        p.names.declare_template_parameter(name, kind);
    return p.finish_node(syntax_kind::type_parameter, mark, first);
}

} // namespace

node_index parser::template_parameter()
{
    const implementation_defined_behavior &b = tokens.files.behavior;
    size_t mark = begin_node();
    token_index first = position;
    if (at(standard_keyword::template_)) {
        /* a template template-parameter: its own parameters are in a
           template-parameter scope of their own ([basic.scope.temp]/1) */
        {
            declarative_region inner(names, scope_kind::template_parameter);
            template_head();
            if (failed())
                return 0;
        }
        if (at(standard_keyword::typename_)) {
            /* N4051 (C++17); before it [ext.clang], P9's */
            if (!b.has(language_feature::typename_template_template_parameters))
                return beyond_this_step();
        } else if (!at(standard_keyword::class_)) {
            return expected("class or typename after the template-head",
                            "temp.param");
        }
        add_token();
        return type_parameter_rest(*this, mark, first, name_kind::template_name,
                                   true);
    }
    if ((at(standard_keyword::class_) || at(standard_keyword::typename_)) &&
        type_parameter_follows(*this, 1)) {
        add_token();
        return type_parameter_rest(*this, mark, first, name_kind::type_name,
                                   false);
    }
    unsigned length = 0;
    if (type_constraint_follows(0, &length) &&
        !at(standard_keyword::auto_, length) &&
        !at(standard_keyword::decltype_, length)) {
        type_constraint();
        if (failed())
            return 0;
        return type_parameter_rest(*this, mark, first, name_kind::type_name,
                                   false);
    }
    /* [temp.res]/5: a parameter-declaration of a (non-type)
       template-parameter assumes a qualified-id names a type */
    flag_set implicit(parameters_implicit_typename, true);
    return parameter_declaration();
}

bool parser::type_constraint_follows(unsigned ahead, unsigned *length)
{
    return classify_name(ahead, length, false) == name_class::concept_;
}

node_index parser::type_constraint()
{
    if (!tokens.files.behavior.has(language_feature::concepts))
        return beyond_this_step();
    size_t mark = begin_node();
    token_index first = position;
    scope_index scope = 0;
    bool known = false;
    nested_name_specifier(&scope, &known);
    if (failed())
        return 0;
    if (!at_identifier())
        return expected("a concept-name", "temp.param");
    add_token();
    if (at(preprocessing_op_or_punc::LESS)) {
        add_token();
        {
            flag_set greater(greater_is_operator, false);
            if (!at_closing_angle()) {
                template_argument_list();
                if (failed())
                    return 0;
            }
        }
        if (!accept_closing_angle())
            return expected("'>' to end the template-argument-list",
                            "temp.param");
    }
    return finish_node(syntax_kind::type_constraint, mark, first);
}

} // namespace cplusplus
