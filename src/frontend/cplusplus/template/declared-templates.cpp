/*
 * declared-templates.cpp: the semantic side of [temp.pre]: the declaration
 * of a template, its template-head and template-parameters, finding the
 * templates a name denotes, which types depend on template parameters
 * ([temp.dep.type]), template-ids ([temp.names]) and when two template
 * declarations are the same ([temp.over.link]).  Each kind of template has
 * its file: function-template.cpp ([temp.fct]), class-template.cpp
 * ([temp.class]), alias-template.cpp ([temp.alias]), variable-template.cpp,
 * partial-specialization.cpp ([temp.class.spec]), member-definition.cpp
 * ([temp.mem.func]) and deduction-guide.cpp ([temp.deduct.guide]); what
 * they share is declared-templates.h.  Explicit specializations and
 * instantiations are in specializations.cpp, template arguments in
 * template-argument.cpp, deduction in argument-deduction.cpp.  (The grammar
 * is the parser's, in template-declaration.cpp and concept-definition.cpp.)
 *
 * The skeleton of S6b: each declaration answers the subclause whose rules
 * it waits for, by what it declares, so that cxx-sema.py's list of what is
 * unsupported says which kind of template blocks most.  hold_names (called
 * for any declaration that is not well-formed) keeps the names they
 * declare, marked as a template's ([temp.names]).
 */
#include "../cplusplus-internal.h"
#include "frontend/cplusplus/sema.h"
#include "declared-templates.h"

namespace cplusplus
{

/* a nested-name-specifier's components' kinds into out: i an identifier,
   t a simple-template-id */
static void qualifier_form(SyntaxRef nested, char (&out)[16])
{
    size_t n = 0;
    for (size_t k = 0; k < nested.size() && n < 12; k++) {
        if (nested[static_cast<uint32_t>(k)].is(
                syntax_kind::simple_template_id))
            out[n++] = 't';
        else if (nested[static_cast<uint32_t>(k)].is_identifier())
            out[n++] = 'i';
    }
    out[n] = 0;
}

/*
 * [temp.pre]/1: "A template defines a family of classes, functions, or
 * variables, an alias for a family of types, or a concept."
 */
analysis_result semantic_analysis::template_declaration(TemplateDeclaration t)
{
    /* [temp.concept]/1: "A concept is a template that defines constraints
       on its template arguments." */
    if (auto c = t.concept_definition())
        return concept_definition(t, c);
    return t.declaration().visit(Overloaded{
        /* [temp.fct]/1: "A function template defines an unbounded set of
           related functions." */
        [&](FunctionDefinition f) {
            /* [temp.mem.func]/1: "A member function of a class template
               may be defined outside of the class template definition in
               which it is declared." */
            if (out_of_class_member(f) || out_of_class_member_class_function(f))
                return member_definition_of_template(t, f);
            if (SyntaxRef nested = out_of_class_nested(f))
                if (entity_index c = plain_class_nominated(nested))
                    return define_plain_member_template(t, f, c);
            return function_template_definition(t, f);
        },
        /* [temp.mem]/1: "A member template can be defined within or
           outside its class definition or class template definition"
           (template<...> template<...> ...) */
        [&](TemplateDeclaration inner) {
            /* [temp.mem.func]/1's member template of a class template,
               defined outside it */
            SyntaxRef d = inner.declaration();
            if (d.is(syntax_kind::function_definition) &&
                (out_of_class_member(d) ||
                 out_of_class_member_class_function(d)))
                return member_definition_of_template(t, d, inner);
            /* [temp.mem]/1's member class template of a class template,
               defined outside it */
            /* [temp.static]/1's static data member template, defined
               outside the class template */
            if (d.is(syntax_kind::simple_declaration) &&
                out_of_class_variable_member(d))
                return member_definition_of_template(t, d, inner);
            if (d.is(syntax_kind::simple_declaration)) {
                SyntaxRef only = sole_decl_specifier(d);
                if (only && only.is(syntax_kind::class_specifier) &&
                    out_of_class_class_member(only, nullptr, true))
                    return member_definition_of_template(t, d, inner);
            }
            /* what the nested template declaration is, for the detail */
            if (d.is(syntax_kind::function_definition)) {
                SyntaxRef nested = out_of_class_nested(d);
                if (!nested)
                    return not_supported_because(
                        t, "temp.mem",
                        "a member template function definition without a "
                        "nested-name-specifier");
                if (nested.size() != 2) {
                    char form[16];
                    qualifier_form(nested, form);
                    char why[160];
                    snprintf(why, sizeof why,
                             "a member template function definition of a "
                             "member class (the nested-name-specifier has "
                             "more than one component: %s)",
                             form);
                    return not_supported_because(t, "temp.mem", why);
                }
                return not_supported_because(
                    t, "temp.mem",
                    "a member template function definition whose "
                    "nested-name-specifier is not a simple-template-id");
            }
            if (d.is(syntax_kind::simple_declaration)) {
                /* by form: the qualifier's components (i an identifier, t a
                   simple-template-id) of a class definition or of a
                   variable's declarator-id */
                char form[16] = "";
                const char *kind = "other";
                SyntaxRef only = sole_decl_specifier(d);
                if (only && only.is(syntax_kind::class_specifier)) {
                    kind = "a class definition";
                    auto head = only.class_specifier().class_head();
                    if (head && head.size() == 2 &&
                        head[1].is(syntax_kind::class_head_name) &&
                        head[1].size() == 2 &&
                        head[1][0].is(syntax_kind::nested_name_specifier))
                        qualifier_form(head[1][0], form);
                    else
                        kind = "a class definition without a qualifier";
                } else if (d.size() == 3) {
                    SyntaxElement q = d[1];
                    if (q.is(syntax_kind::init_declarator) && q.size())
                        q = q[0];
                    if (q.is(syntax_kind::qualified_id) && q.size() == 2 &&
                        q[0].is(syntax_kind::nested_name_specifier)) {
                        kind = "a variable";
                        qualifier_form(q[0], form);
                    }
                }
                char why[160];
                snprintf(why, sizeof why,
                         "a member template simple-declaration that is no "
                         "variable or class definition outside its class: %s "
                         "(qualifier %s)",
                         kind, form);
                return not_supported_because(t, "temp.mem", why);
            }
            return not_supported_because(t, "temp.mem",
                                         "a member template of another kind of "
                                         "declaration");
        },
        /* [temp.alias]/1: "A template-declaration in which the declaration
           is an alias-declaration declares the identifier to be an alias
           template." */
        [&](AliasDeclaration a) { return alias_template_definition(t, a); },
        [&](SimpleDeclaration s) { return template_simple_declaration(t, s); },
        [&](DeductionGuide g) { return deduction_guide(t, g); },
        /* (or no declaration: a token) */
        [&](Declaration) {
            return not_supported_because(
                t, "temp.pre",
                "a template-declaration with no declaration of a handled kind");
        }});
}

/* [temp.mem.class]/1: a member class of a class template defined outside
   it (template<class T> class O<T>::S { ... };): returns the
   simple-template-id O<T> and optionally the member class name token S */
VPRIV SyntaxRef out_of_class_class_member(SyntaxRef only, token_index *name,
                                          bool partial)
{
    if (!only || !only.is(syntax_kind::class_specifier))
        return 0;
    auto head = only.class_specifier().class_head();
    if (!head || head.size() < 2 || !head[1].is(syntax_kind::class_head_name) ||
        head[1].size() != 2 ||
        !head[1][0].is(syntax_kind::nested_name_specifier) ||
        head[1][0].size() != 2 ||
        !head[1][0][0].is(syntax_kind::simple_template_id))
        return 0;
    /* ... or a partial specialization's simple-template-id, O<T>::X<U*>
       ([temp.class.spec]/5) */
    if (partial && head[1][1].is(syntax_kind::simple_template_id)) {
        if (name)
            *name = head[1][1].first_token();
        return head[1][0][0].index();
    }
    if (!head[1][1].is_identifier())
        return 0;
    if (name)
        *name = head[1][1].token();
    return head[1][0][0].index();
}

/* a template-declaration whose declaration is a simple-declaration */
analysis_result
semantic_analysis::template_simple_declaration(TemplateDeclaration t,
                                               SimpleDeclaration s)
{
    /* a function template's declaration or a variable template of several
       declarators: which needs the declarator's type */
    if (s.init_declarator_list().is(syntax_kind::init_declarator_list))
        return not_supported_because(
            t, "temp.pre",
            "a templated simple-declaration with several declarators");
    /* [temp.class]/1: "A class template defines the layout and operations
       for an unbounded set of related types." (a declaration with no
       declarator: a class's) */
    SyntaxRef only = sole_decl_specifier(s);
    if (only && only.is(syntax_kind::class_specifier)) {
        if (out_of_class_class_member(only))
            return member_definition_of_template(t, s);
        return class_template_definition(t, only);
    }
    /* [temp.class]/1's declaration without a definition (template<class T>
       struct S;) */
    if (only && only.is(syntax_kind::elaborated_type_specifier))
        return class_template_declaration(t, only);
    /* [temp.pre]/1: a variable template, its one init-declarator with an
       initializer, after attributes without effect (libc++'s deprecated
       is_pod_v) */
    uint32_t lead = attributes_without_effect_before(s);
    uint32_t gnu_before = leading_gnu_attributes(s);
    if (s.size() == 3 + lead &&
        s.is(s[lead], syntax_kind::decl_specifier_seq) &&
        s.is(s[lead + 1], syntax_kind::init_declarator)) {
        /* (a specialization's template-id after attributes: no rules
           yet) */
        auto item = s[lead + 1];
        if (lead && item.is(item[0], syntax_kind::simple_template_id))
            return not_supported_because(
                t, "temp.pre",
                "an attributed declaration of a variable specialization");
        if (SyntaxRef id = variable_specialization_id(s))
            return variable_partial_specialization(t, s, id, 0);
        /* [dcl.meaning]/1: the same named by a qualified template-id,
           template<class T> constexpr bool N::v<T*> = true; */
        entity_index qualifier = 0;
        if (SyntaxRef id = qualified_variable_specialization_id(s, qualifier))
            return variable_partial_specialization(t, s, id, qualifier);
        /* [temp.static]/1: a static data member of a class template,
           defined outside it */
        if (out_of_class_variable_member(s))
            return member_definition_of_template(t, s);
        return variable_template_definition(t, s);
    }
    /* [temp.class.spec]/1: a partial specialization of a variable
       template declared without an initializer (template<class T> int
       v<T*>;), named plainly or through its namespace */
    if (s.size() == 3 && !lead && s.is(s[0], syntax_kind::decl_specifier_seq)) {
        if (SyntaxRef id = variable_specialization_id(s))
            return variable_partial_specialization(t, s, id, 0);
        entity_index qualifier = 0;
        if (SyntaxRef id = qualified_variable_specialization_id(s, qualifier))
            return variable_partial_specialization(t, s, id, qualifier);
    }
    /* [temp.static]/1: a static data member of a class template defined
       outside it without an initializer (template<class T> int S<T>::m;) */
    if (s.size() == 3 + gnu_before && out_of_class_variable_member(s))
        return member_definition_of_template(t, s);
    /* [temp.fct]/1: a function template's declaration (template<class T>
       T f(T);), one declarator not in an init-declarator-list */
    /* [temp.pre]/1: a variable template without an initializer
       (template<class T> int v;), its one declarator not an
       init-declarator; [basic.def]/2: a definition */
    if (s.size() == 3 + lead && !lead &&
        s.is(s[0], syntax_kind::decl_specifier_seq) && s[1].is_identifier())
        return variable_template_definition(t, s);
    uint32_t gnu = leading_gnu_attributes(s);
    /* a declarator with a trailing-return-type is a function's
       ([dcl.fct]/1; template<class T> auto f() -> T;) */
    if (s.size() == 3 + gnu &&
        (s.is(s[1 + gnu], syntax_kind::noptr_declarator) ||
         s.is(s[1 + gnu], syntax_kind::ptr_declarator) ||
         s.is(s[1 + gnu], syntax_kind::declarator)))
        return function_template_definition(t, s);
    char shape[160];
    snprintf(shape, sizeof shape, "%u parts, part 1 is %s", s.size(),
             syntax_kind_name(s[1 + gnu].kind()));
    return not_supported_because(t, "temp.class", shape);
}

/*
 * [temp.pre]/1: the template-head's template-parameter-list: type
 * parameters of the form class identifier (typename identifier), with no
 * default, pack or type-constraint; a requires-clause is [temp.constr].
 * The template is one of namespace scope.
 */
/* [temp.pre]/1: a template-head's requires-clause's constraint-expression,
   or none */
SyntaxElement semantic_analysis::head_constraint(SyntaxRef n) const
{
    if (n.is(syntax_kind::lambda_expression))
        return lambda_constraint(n);
    if (!n.size() || !n[0].is(syntax_kind::template_head))
        return SyntaxElement();
    auto head = n[0];
    for (SyntaxElement c : head.elements()) {
        if (c.is(syntax_kind::requires_clause) && c.size() == 2)
            return c[1];
    }
    return SyntaxElement();
}

/* [dcl.decl]/4: a function template's trailing requires-clause, the
   function-definition's child before its function-body, or none */
SyntaxElement semantic_analysis::trailing_constraint(SyntaxRef n) const
{
    if (n.size() != 2 || n[1].is_token())
        return SyntaxElement();
    auto d = n[1];
    if (!d.is(syntax_kind::function_definition))
        return SyntaxElement();
    for (SyntaxElement c : d.elements()) {
        if (c.is(syntax_kind::requires_clause) && c.size() == 2)
            return c[1];
    }
    return SyntaxElement();
}

analysis_result semantic_analysis::template_head_parameters(
    SyntaxRef n, owned_array<SyntaxRef> &parameters, bool packs,
    bool constrained, bool pack_anywhere, bool type_constraints)
{
    /* [dcl.fct]/18: an abbreviated function template without a
       template-head, its parameters all invented */
    if (n.is(syntax_kind::function_definition)) {
        if (!abbreviated_parameters(n, parameters) || parameters.empty())
            return not_supported_because(
                n, "temp.variadic",
                "a function definition with no template-head or invented "
                "parameters");
        return analysis_result::well_formed;
    }
    if (n.size() != 2 || !n[0].is(syntax_kind::template_head))
        return not_supported_because(n, "temp.pre",
                                     "a template-declaration that is not a "
                                     "template-head and a declaration");
    /* a member alias template of a class of namespace scope
       (member_template_declaration) is the one member template here */
    entity_index top = current_scope;
    while (top && entity_table[top].kind == entity_kind::class_type)
        top = entity_table[top].enclosing;
    bool member_alias =
        entity_table[current_scope].kind == entity_kind::class_type &&
        is_namespace(top) && n[1].is(syntax_kind::alias_declaration);
    /* ... and a member class template (member_template_declaration) */
    bool member_class =
        entity_table[current_scope].kind == entity_kind::class_type &&
        n[1].is(syntax_kind::member_declaration);
    /* ... and a member deduction guide ([temp.deduct.guide]) */
    bool member_guide =
        entity_table[current_scope].kind == entity_kind::class_type &&
        is_namespace(top) && n[1].is(syntax_kind::deduction_guide);
    /* ... and the template-head of a member variable template's definition
       outside its class (define_template_member), in the scope that binds
       the class's parameters */
    bool member_definition =
        template_member_scope && current_scope == template_member_scope;
    if (!member_alias && !member_class && !member_guide && !member_definition &&
        (!is_namespace(current_scope) || (defining_class && !friend_allowed)))
        return not_supported_because(
            n, "temp.mem", "a member template of this kind or in this scope");
    /* template-head: template < template-parameter-list >; a
       requires-clause is [temp.constr] */
    auto head = n[0];
    if (!template_head_list(head, parameters))
        return not_supported_because(
            n, "temp.pre", "a template-head with an unhandled parameter list");
    /* [dcl.fct]/22: the invented template-parameters after the declared */
    if (!abbreviated_parameters(n, parameters))
        return not_supported_because(n, "temp.variadic",
                                     "unhandled invented parameters of an "
                                     "abbreviated function template");
    if (head_constraint(n) && !constrained)
        return not_supported_because(
            n, "temp.constr", "a requires-clause on this kind of template");
    return validate_template_parameters(n, parameters, packs, pack_anywhere,
                                        type_constraints);
}

analysis_result semantic_analysis::validate_template_parameters(
    SyntaxRef n, const owned_array<SyntaxRef> &parameters, bool packs,
    bool pack_anywhere, bool type_constraints)
{
    /* a type-parameter class identifier_opt, a template template-parameter
       template < type-parameters > class identifier_opt, a
       parameter-declaration decl-specifier-seq identifier_opt, each with =
       and a default template-argument or not: a type-constraint or another
       declarator is a rule of its own */
    for (size_t i = 0; i < parameters.size(); i++) {
        SyntaxRef p = parameters[i];
        template_parameter_parts x;
        if (!template_parameter_form(p, x))
            return not_supported_because(
                p, "temp.param", "a template parameter of an unhandled form");
        /* [temp.param]/4: a type-constraint's immediately-declared
           constraint is one of the template's associated constraints
           ([temp.constr.decl]/3), checked with its requires-clause
           (template_constraints_satisfied): a function template's and a
           partial specialization's ([temp.class.spec.match]/2) */
        if (x.constraint && (!type_constraints || x.pack))
            return not_supported_because(
                p, "temp.param",
                "a type-constraint on this template parameter");
        /* [temp.param]/18: "If a template-parameter is a type-parameter
           with an ellipsis prior to its optional identifier ... then the
           template-parameter is a template parameter pack"; /17 a
           parameter-declaration's, int... N: the template's last
           parameter */
        if (x.pack) {
            /* [temp.param]/14: "If a template-parameter of a primary class
               template ... is a template parameter pack, it shall be the
               last template-parameter.  A template parameter pack of a
               function template shall not be followed by another
               template parameter unless that template parameter can be
               deduced": a partial specialization's are all deduced
               ([temp.class.spec]/8.3 aside) */
            if (!packs || (i + 1 != parameters.size() && !pack_anywhere) ||
                x.template_template || x.has_default)
                return not_supported_because(
                    p, "temp.variadic",
                    "a template parameter pack not allowed in this position or "
                    "form");
            continue;
        }
        if (!x.template_template)
            continue;
        /* [temp.param]/1's template-head type-parameter-key: its own
           parameters type-parameters or integral parameter-declarations,
           without defaults; a pack its last */
        owned_array<SyntaxRef> inner;
        if (!template_head_list(x.head, inner) || inner.empty())
            return not_supported_because(p, "temp.arg.template",
                                         "a template template parameter with "
                                         "an unhandled or empty head");
        for (size_t k = 0; k < inner.size(); k++) {
            template_parameter_parts y;
            if (!template_parameter_form(inner[k], y) || y.has_declarator ||
                y.template_template || (y.pack && k + 1 != inner.size()))
                return not_supported_because(
                    p, "temp.arg.template",
                    "a template template parameter with an unhandled inner "
                    "parameter");
        }
    }
    if (parameters.empty())
        return not_supported_because(n, "temp.param",
                                     "an empty template parameter list");
    return analysis_result::well_formed;
}

/*
 * The position among the template's parameters of the type parameter whose
 * type t is (cv-qualified or not), when it is one of the parameters of this
 * template-parameter-list declared before (their entities from
 * template_parameter_entities[first] on, in scope); UINT32_MAX otherwise.
 */
uint32_t semantic_analysis::own_type_parameter_position(qualified_type t,
                                                        entity_index scope,
                                                        uint32_t first) const
{
    const struct type &record = type_table[t.unqualified()];
    if (record.kind != type_kind::template_parameter)
        return UINT32_MAX;
    entity_index q = record.entity;
    for (size_t k = first; k < template_parameter_entities.size(); k++)
        if (template_parameter_entities[k] == q &&
            entity_table[q].enclosing == scope)
            return static_cast<uint32_t>(entity_table[q].value);
    return UINT32_MAX;
}

/* whether e is a template template-parameter ([temp.param]/1): a template
   of its own parameters whose record is_parameter */
bool semantic_analysis::is_template_template_parameter(entity_index e) const
{
    const entity &named = entity_table[e];
    return named.kind == entity_kind::template_entity && named.instantiation &&
           named.instantiation <= function_templates.size() &&
           function_templates[named.instantiation - 1].is_parameter &&
           function_templates[named.instantiation - 1].entity == e;
}

/* [temp.param]/1's template template-parameter e: a class template of its
   own parameters (in a scope of their own), never defined */
analysis_result semantic_analysis::template_template_record(SyntaxRef n,
                                                            SyntaxRef parameter,
                                                            entity_index e,
                                                            entity_index scope)
{
    template_parameter_parts form;
    template_parameter_form(parameter, form);
    owned_array<SyntaxRef> inner;
    template_head_list(form.head, inner);
    uint32_t own = static_cast<uint32_t>(function_templates.size());
    entity_index inner_scope =
        entity_table.make(entity_kind::block_scope, 0, scope);
    function_template record;
    record.entity = e;
    record.name = e;
    record.declaration = n;
    record.first_parameter =
        static_cast<uint32_t>(template_parameter_entities.size());
    record.parameter_count = static_cast<uint32_t>(inner.size());
    record.is_class = true;
    record.is_parameter = true;
    VTRY(r, declare_template_parameters(n, inner, inner_scope, own, 0));
    entity_table[e].instantiation = own + 1;
    function_templates.append(record);
    return analysis_result::well_formed;
}

/*
 * [temp.param]/2: "There is no semantic difference between class and
 * typename in a type-parameter-key."  A type parameter's name is a
 * typedef-name of its template_parameter type; a non-type parameter's
 * (/4: of integral type here) names a value the template's own
 * declarator does not know: an entity of kind none with the parameter's
 * type, which an instantiation binds to a constant.
 */
analysis_result semantic_analysis::declare_template_parameters(
    SyntaxRef n, const owned_array<SyntaxRef> &parameters, entity_index scope,
    uint32_t &index, uint32_t reserved)
{
    owned_array<uint32_t> template_templates;
    uint32_t first = static_cast<uint32_t>(template_parameter_entities.size());
    /* the template's own parameters stay contiguous: their slots are
       reserved first, a template template-parameter's inner parameters
       (declared as it is met, so that a later parameter's type can use
       it) come after them */
    for (size_t i = 0; i < parameters.size(); i++) {
        template_parameter_entities.append(0);
        template_parameter_nodes.append(parameters[i]);
        template_parameter_default_names.append(first);
    }
    for (size_t i = 0; i < parameters.size(); i++) {
        SyntaxRef node = parameters[i];
        template_parameter_parts form;
        if (!template_parameter_form(node, form))
            return not_supported_because(
                node, "temp.param",
                "a template parameter of an unhandled form");
        token_index id = form.id;
        uint32_t name = 0;
        if (form.named)
            name = tokens.identifier(id);
        else if (node.is(syntax_kind::decl_specifier_seq))
            name = invented_parameter_name(node);
        /* [temp.local]/6: "The name of a template-parameter shall not be
           redeclared within its scope (including nested scopes)." */
        if (name && (entity_table.find(scope, name) || name == reserved))
            return ill_formed_at_token(id,
                                       "the name of a template-parameter "
                                       "shall not be redeclared within its "
                                       "scope",
                                       "temp.local");
        entity_index e = 0;
        if (form.template_template) {
            e = entity_table.make(entity_kind::template_entity, name, scope);
            template_templates.append(static_cast<uint32_t>(i));
            template_parameter_entities[first + i] = e;
            VTRY(rr, template_template_record(n, node, e, scope));
        } else if (form.type) {
            e = entity_table.make(entity_kind::typedef_name, name, scope);
            entity_table[e].type = qualified_type::make(
                type_table.of_entity(type_kind::template_parameter, e),
                cv_none);
        } else {
            /* the parameter's type, in the scope of those before it */
            entity_index saved = current_scope;
            current_scope = scope;
            specified sp;
            placeholder_allowed = true;
            analysis_result r = decl_specifier_seq(node[0], sp, false);
            qualified_type type = sp.type;
            if (r == analysis_result::well_formed && form.has_declarator) {
                declarator_meaning dm;
                flag_set pack(pack_declarator, true);
                r = declarator(node, form.declarator, type, dm);
                if (r == analysis_result::well_formed &&
                    (dm.qualifier || dm.qualified_namespace ||
                     !dm.pack != !form.pack || dm.gnu_flags || dm.aligned ||
                     dm.template_id || dm.has_id != form.named))
                    r = not_supported_because(node, "temp.param",
                                              "a non-type template parameter "
                                              "with an unhandled declarator");
            }
            current_scope = saved;
            if (r != analysis_result::well_formed)
                return r;
            /* [temp.param]/4 with a type that depends on this template's
               parameters declared before it: template <class T, T v>
               (libc++'s integral_constant), whose argument gives its
               type, or typename enable_if<B, int>::type = 0 (libc++'s
               SFINAE), analysed again with the arguments
               (template_argument_for) */
            /* [temp.param]/8: "A non-type template-parameter of type
               'array of T' or 'function type T' is adjusted to be of type
               'pointer to T'." */
            {
                const struct type &adjusted = type_table[type.unqualified()];
                if (adjusted.kind == type_kind::array)
                    type = qualified_type::make(
                        type_table.pointer_to(adjusted.operand), cv_none);
                else if (adjusted.kind == type_kind::function)
                    type = qualified_type::make(type_table.pointer_to(type),
                                                cv_none);
            }
            /* [temp.dep.constexpr]/2: a parameter type whose array bound is
               an expression of another parameter (int (*)[sizeof(f(V))]):
               the arguments' conversions to it are not analysed */
            for (qualified_type walk = type;;) {
                const struct type &w = type_table[walk.unqualified()];
                if (w.kind == type_kind::array && w.entity && w.bound)
                    return not_supported_because(
                        node, "temp.param",
                        "a parameter type with an array bound depending on "
                        "another parameter");
                if (w.kind != type_kind::pointer &&
                    w.kind != type_kind::array &&
                    w.kind != type_kind::lvalue_reference &&
                    w.kind != type_kind::rvalue_reference)
                    break;
                walk = w.operand;
            }
            bool own_parameter_type =
                own_type_parameter_position(type, scope, first) != UINT32_MAX;
            bool placeholder = contains_placeholder(type);
            if ((!own_parameter_type && !placeholder &&
                 !made_of_template_parameter(type_table, type) &&
                 !structural_parameter_type(type)) ||
                sp.typedef_ || sp.static_ || sp.extern_ || sp.constexpr_ ||
                sp.inline_ || sp.gnu_flags) {
                analysis_result refused = not_supported(node, "temp.param");
                const char *why = parameter_refusal_reason(type, sp);
                last_unsupported_detail(why, strlen(why));
                return refused;
            }
            e = entity_table.make(entity_kind::none, name, scope);
            /* /6: "the top-level cv-qualifiers on the template-parameter
               are ignored when determining its type" */
            entity_table[e].type =
                qualified_type::make(type.unqualified(), cv_none);
        }
        entity_table[e].value = i;
        entity_table[e].defined = true;
        if (!form.template_template)
            entity_table[e].instantiation = index + 1;
        if (name)
            entity_table.bind(scope, name, e);
        template_parameter_entities[first + i] = e;
    }
    if (template_templates.empty())
        return analysis_result::well_formed;
    index = static_cast<uint32_t>(function_templates.size());
    for (size_t i = 0; i < parameters.size(); i++) {
        entity_index e = template_parameter_entities[first + i];
        if (entity_table[e].kind != entity_kind::template_entity)
            entity_table[e].instantiation = index + 1;
    }
    return analysis_result::well_formed;
}

/* the usable function template (member: member function template) whose
   name the held entity is, plus one */
uint32_t semantic_analysis::function_template_of(entity_index held,
                                                 bool member) const
{
    for (size_t i = 0; i < function_templates.size(); i++) {
        const function_template &f = function_templates[i];
        if (f.name == held && held && !f.unusable && !f.is_class &&
            !f.is_alias && !f.is_variable &&
            is_namespace(entity_table[f.entity].enclosing) != member)
            return static_cast<uint32_t>(i + 1);
    }
    return 0;
}

entity_index semantic_analysis::member_templates_of(entity_index e) const
{
    if (!e)
        return 0;
    if (entity_table[e].kind == entity_kind::function)
        return entity_table[e].member_templates;
    if (entity_table[e].kind == entity_kind::none &&
        member_function_template_of(e))
        return e;
    return 0;
}

void semantic_analysis::function_template_overloads(
    entity_index held, owned_array<uint32_t> &out) const
{
    out.clear();
    if (!held)
        return;
    /* [namespace.def]/7: members of an inline namespace are treated as if they
       were members of the enclosing namespace for lookup and overload sets */
    auto non_inline_ns = [&](entity_index s) {
        while (s && entity_table[s].kind == entity_kind::namespace_entity &&
               entity_table[s].inline_)
            s = entity_table[s].enclosing;
        return s;
    };
    bool in_ns = is_namespace(entity_table[held].enclosing);
    entity_index base_ns = 0;
    uint32_t held_id = entity_table[held].name;
    if (in_ns)
        base_ns = non_inline_ns(entity_table[held].enclosing);
    auto matches_held = [&](entity_index fn) {
        for (entity_index h = held; h; h = entity_table[h].using_shadow_of)
            if (fn == h)
                return true;
        return false;
    };
    for (size_t i = 0; i < function_templates.size(); i++) {
        if (function_templates[i].unusable || function_templates[i].is_class ||
            function_templates[i].is_alias || function_templates[i].is_variable)
            continue;
        entity_index fn = function_templates[i].name;
        if (matches_held(fn) ||
            (in_ns && fn && entity_table[fn].name == held_id &&
             is_namespace(entity_table[fn].enclosing) &&
             non_inline_ns(entity_table[fn].enclosing) == base_ns))
            out.append(static_cast<uint32_t>(i + 1));
    }
}

void semantic_analysis::inline_set_functions(
    entity_index held, owned_array<entity_index> &out) const
{
    if (!held)
        return;
    auto add = [&](entity_index f) {
        for (entity_index g : out)
            if (g == f)
                return;
        out.append(f);
    };
    auto add_held = [&](entity_index h) {
        for (const template_name_function &x : template_name_functions)
            if (x.name == h)
                add(x.function);
    };
    add_held(held);
    entity_index scope = entity_table[held].enclosing;
    if (!is_namespace(scope))
        return;
    /* [namespace.def]/7: "Members of an inline namespace can be used in
       most respects as though they were members of the enclosing
       namespace": the namespace that is no inline one, then its inline
       namespaces and theirs (as function_template_overloads takes their
       templates) */
    while (scope && entity_table[scope].kind == entity_kind::namespace_entity &&
           entity_table[scope].inline_)
        scope = entity_table[scope].enclosing;
    owned_array<entity_index> set;
    set.append(scope);
    for (size_t i = 0; i < set.size(); i++) {
        if (!entity_table[set[i]].has_using_directive)
            continue;
        for (const using_directive_record &u : using_directives) {
            const entity &nominated = entity_table[u.nominated];
            if (u.scope != set[i] || !nominated.inline_ ||
                nominated.enclosing != set[i])
                continue;
            bool seen = false;
            for (entity_index other : set)
                if (other == u.nominated)
                    seen = true;
            if (!seen)
                set.append(u.nominated);
        }
    }
    uint32_t name = entity_table[held].name;
    for (entity_index s : set) {
        entity_index e = entity_table.find(s, name);
        if (!e || e == held)
            continue;
        if (entity_table[e].kind == entity_kind::function) {
            owned_array<entity_index> chain;
            overload_set(e, chain);
            for (entity_index f : chain)
                add(f);
        } else if (entity_table[e].kind == entity_kind::none)
            add_held(e);
    }
}

uint32_t semantic_analysis::class_template_of(entity_index e) const
{
    if (!e)
        return 0;
    /* [temp.local]/1: "the injected-class-name can be used as a
       template-name or a type-name.  When it is used with a
       template-argument-list ... it refers to the specified class template
       specialization" */
    if (entity_table[e].kind == entity_kind::class_type &&
        entity_table[e].instantiation)
        return entity_table[specialized_template(e)].instantiation;
    return template_of_form(e, &function_template::is_class);
}

uint32_t semantic_analysis::alias_template_of(entity_index e) const
{
    return template_of_form(e, &function_template::is_alias);
}

/* the usable template of the form (is_class, is_alias, is_variable) whose
   name the held entity e is, plus one */
uint32_t
semantic_analysis::template_of_form(entity_index e,
                                    bool function_template::*form) const
{
    if (!e)
        return 0;
    for (size_t i = 0; i < function_templates.size(); i++)
        if (function_templates[i].name == e && function_templates[i].*form &&
            !function_templates[i].unusable)
            return static_cast<uint32_t>(i + 1);
    return 0;
}

/* the simple-template-id id's template-name, an identifier, looked up
   unqualified or in the qualifier's scope (x), the entity it names or holds (named); false for another
   form */
bool semantic_analysis::template_id_name(SyntaxRef id, lookup_result &x,
                                         entity_index &named,
                                         entity_index qualifier) const
{
    SyntaxElement template_name = id.simple_template_id().template_name();
    if (!template_name)
        return false;
    uint32_t name = tokens.identifier(template_name.token());
    /* [basic.lookup.qual]: after a nested-name-specifier, in the scope it
       nominates */
    if (qualifier)
        x = lookup_qualified(qualifier, name, lookup_filter::any);
    else
        x = lookup_unqualified(name, lookup_filter::any);
    named = x.entity;
    if (!named)
        named = x.unknown_entity;
    return true;
}

/* the dependent type template t's simple-template-id with arguments
   denotes ([temp.dep.type]/9), not instantiated; one per template and
   arguments */
type_index semantic_analysis::dependent_specialization(
    entity_index t, const owned_array<template_argument> &arguments)
{
    for (const instantiation_record &x : instantiations) {
        if (x.template_entity != t || x.argument_count != arguments.size())
            continue;
        bool same = true;
        for (uint32_t i = 0; i < x.argument_count && same; i++)
            same = template_arguments[x.first_argument + i] == arguments[i];
        if (same)
            return entity_table[x.specialization].type.unqualified();
    }
    instantiation_record record;
    record.template_entity = t;
    record.first_argument = static_cast<uint32_t>(template_arguments.size());
    record.argument_count = static_cast<uint32_t>(arguments.size());
    for (const template_argument &a : arguments)
        template_arguments.append(a);
    uint32_t index = static_cast<uint32_t>(instantiations.size());
    entity_index e =
        entity_table.make(entity_kind::template_specialization,
                          entity_table[t].name, entity_table[t].enclosing);
    entity_table[e].type = qualified_type::make(
        type_table.of_entity(type_kind::dependent, e), cv_none);
    entity_table[e].instantiation = index + 1;
    entity_table[e].linkage_of_name = entity_table[t].linkage_of_name;
    record.specialization = e;
    instantiations.append(record);
    return entity_table[e].type.unqualified();
}

/* whether a type is made of a template-parameter type, of one at
   position from or after if from is given */
VPRIV bool made_of_template_parameter(const types &table, qualified_type t,
                                      const entities *parameters, uint64_t from)
{
    const struct type &r = table[t.unqualified()];
    switch (r.kind) {
    case type_kind::template_parameter:
        return !parameters || (*parameters)[r.entity].value >= from;
    case type_kind::dependent:
        /* typename Q::name depends on what Q does */
        if (parameters &&
            (*parameters)[r.entity].kind == entity_kind::dependent_member)
            return made_of_template_parameter(
                table,
                qualified_type::make(
                    static_cast<type_index>((*parameters)[r.entity].value),
                    cv_none),
                parameters, from);
        return true;
    case type_kind::bit_int:
        return r.entity &&
               (!parameters || (*parameters)[r.entity].value >= from);
    case type_kind::vector:
        return made_of_template_parameter(table, r.operand, parameters, from);
    case type_kind::ext_vector:
        if (r.entity && (!parameters || (*parameters)[r.entity].value >= from))
            return true;
        return made_of_template_parameter(table, r.operand, parameters, from);
    case type_kind::array:
        /* T[N]: N a non-type template-parameter */
        if (r.entity && (!parameters || (*parameters)[r.entity].value >= from))
            return true;
        return made_of_template_parameter(table, r.operand, parameters, from);
    case type_kind::pointer_to_member:
        /* [temp.dep.type]/9: a pointer to member of a dependent class, or
           to a dependent member type */
        if (!r.entity &&
            made_of_template_parameter(
                table,
                qualified_type::make(static_cast<type_index>(r.bound), cv_none),
                parameters, from))
            return true;
        return made_of_template_parameter(table, r.operand, parameters, from);
    case type_kind::pointer:
    case type_kind::lvalue_reference:
    case type_kind::rvalue_reference:
    case type_kind::pack_expansion:
    case type_kind::atomic:
    case type_kind::complex:
    case type_kind::block_pointer:
        return made_of_template_parameter(table, r.operand, parameters, from);
    case type_kind::function: {
        if (made_of_template_parameter(table, r.operand, parameters, from))
            return true;
        const qualified_type *p = table.parameters(t.unqualified());
        for (uint32_t i = 0; i < r.parameter_count; i++)
            if (made_of_template_parameter(table, p[i], parameters, from))
                return true;
        return false;
    }
    default:
        return false;
    }
}

bool semantic_analysis::depends_on_template_parameter(qualified_type t) const
{
    return made_of_template_parameter(type_table, t);
}

/* whether template argument a is simply the template parameter pe (or its
   unexpanded pack pe...) */
bool semantic_analysis::argument_is_parameter(const template_argument &a,
                                              entity_index pe) const
{
    return a.template_entity == pe ||
           (a.kind == template_argument::kind_t::type && !a.template_entity &&
            a.type == entity_table[pe].type) ||
           (a.kind == template_argument::kind_t::pack && a.count == 1 &&
            pack_arguments[a.value].count == UINT32_MAX &&
            pack_arguments[a.value].template_entity == pe);
}

/* whether a template argument, or a pack's element, is dependent */
bool semantic_analysis::dependent_arguments(
    const owned_array<template_argument> &arguments) const
{
    for (const template_argument &a : arguments) {
        if (depends_on_template_parameter(a.type) || a.template_entity)
            return true;
        if (a.kind == template_argument::kind_t::pack) {
            if (a.count == UINT32_MAX)
                return true;
            for (uint32_t k = 0; k < a.count; k++)
                if (depends_on_template_parameter(
                        pack_arguments[a.value + k].type) ||
                    pack_arguments[a.value + k].template_entity ||
                    pack_arguments[a.value + k].count == UINT32_MAX)
                    return true;
        }
    }
    return false;
}

/* the scope of the template-parameters ([basic.scope.temp]) in outer, the
   parameters declared in it (declare_template_parameters, reserved the
   template's name), the template's record index-to-be and its first
   parameter */
analysis_result semantic_analysis::template_parameter_scope(
    SyntaxRef n, const owned_array<SyntaxRef> &parameters, entity_index outer,
    uint32_t reserved, uint32_t &index, entity_index &scope, uint32_t &first)
{
    index = static_cast<uint32_t>(function_templates.size());
    scope = entity_table.make(entity_kind::block_scope, 0, outer);
    first = static_cast<uint32_t>(template_parameter_entities.size());
    return declare_template_parameters(n, parameters, scope, index, reserved);
}

/* whether a template-parameter without a default follows one with a
   default ([temp.param]/14) */
bool semantic_analysis::default_missing_after_one(
    const owned_array<SyntaxRef> &parameters) const
{
    for (size_t i = 1; i < parameters.size(); i++)
        if (template_parameter_has_default(parameters[i - 1]) &&
            !template_parameter_has_default(parameters[i]))
            return true;
    return false;
}

/* template_id_arguments with the template-parameters of scope in scope;
   what they record for claims is dropped */
analysis_result semantic_analysis::template_id_arguments_in(
    SyntaxRef id, uint32_t ct, entity_index scope,
    owned_array<template_argument> &arguments)
{
    size_t expressions_before = expressions.size();
    size_t uses_before = name_uses.size();
    entity_index saved_scope = current_scope;
    current_scope = scope;
    analysis_result r = template_id_arguments(id, ct, arguments);
    current_scope = saved_scope;
    expressions.resize(expressions_before);
    name_uses.resize(uses_before);
    return r;
}

/* the items of a comma-separated list (written), each marked whether an
   ellipsis follows it (expansion, [temp.variadic]/5); false for an
   ellipsis that follows none or another */
bool semantic_analysis::expansion_list(SyntaxRef list,
                                       owned_array<SyntaxElement> &written,
                                       owned_array<bool> &expansion) const
{
    for (SyntaxElement a : list.elements()) {
        if (a.is(preprocessing_op_or_punc::COMMA))
            continue;
        if (a.is(preprocessing_op_or_punc::ELLIPSIS)) {
            if (written.empty() || expansion.back())
                return false;
            expansion.back() = true;
            continue;
        }
        written.append(a);
        expansion.append(false);
    }
    return true;
}

/* a declaration's decl-specifier-seq when it is all the declaration has
   (no declarator) and one decl-specifier, a node: that decl-specifier;
   else 0 */
SyntaxRef semantic_analysis::sole_decl_specifier(SyntaxRef declaration) const
{
    if (declaration.size() != 2 ||
        !declaration[0].is(syntax_kind::decl_specifier_seq))
        return 0;
    auto seq = declaration[0];
    if (seq.size() != 1 || seq[0].is_token())
        return 0;
    return seq[0].index();
}

bool semantic_analysis::depends_on_parameter_from(qualified_type t,
                                                  uint64_t from) const
{
    /* a dependent alias template-id (void_t<typename U::type>, kept by
       alias_template_specialization): it depends on what its arguments
       do, the template-parameters at from or after */
    qualified_type inner = t;
    while (type_table[inner.unqualified()].kind == type_kind::pointer ||
           type_table[inner.unqualified()].kind ==
               type_kind::lvalue_reference ||
           type_table[inner.unqualified()].kind == type_kind::rvalue_reference)
        inner = type_table[inner.unqualified()].operand;
    const struct type &r = type_table[inner.unqualified()];
    if (r.kind == type_kind::dependent && r.entity &&
        entity_table[r.entity].kind == entity_kind::template_specialization &&
        entity_table[r.entity].instantiation) {
        const instantiation_record &x =
            instantiations[entity_table[r.entity].instantiation - 1];
        auto depends_on = [&](const template_argument &a) {
            return a.kind != template_argument::kind_t::type ||
                   made_of_template_parameter(type_table, a.type, &entity_table,
                                              from);
        };
        for (uint32_t i = 0; i < x.argument_count; i++) {
            const template_argument &a =
                template_arguments[x.first_argument + i];
            if (a.kind != template_argument::kind_t::pack) {
                if (depends_on(a))
                    return true;
                continue;
            }
            /* (a pack's elements: void_t's) */
            for (uint32_t k = 0; k < a.count; k++)
                if (depends_on(pack_arguments[a.value + k]))
                    return true;
        }
        return false;
    }
    return made_of_template_parameter(type_table, t, &entity_table, from);
}

/*
 * [temp.names]/1 with [temp.arg.type]/1: template-name <
 * template-argument-list > naming a class template, its arguments type-ids;
 * the specialization instantiated ([temp.inst]/2), its type the type.
 */
analysis_result semantic_analysis::template_type(SyntaxRef id,
                                                 entity_index scope,
                                                 bool qualified,
                                                 type_index &type, uint32_t *cv)
{
    SyntaxElement template_name = id.simple_template_id().template_name();
    if (!template_name)
        return not_supported_because(id, "temp.names",
                                     "a template-id with no template-name");
    uint32_t name = tokens.identifier(template_name.token());
    lookup_result x;
    if (qualified)
        x = lookup_qualified(scope, name, lookup_filter::any);
    else
        x = lookup_unqualified(name, lookup_filter::any);
    entity_index named = x.entity;
    if (!named)
        named = x.unknown_entity;
    if (qualified && !in_destructor_name && named && scope &&
        !is_namespace(scope) &&
        entity_table[scope].kind == entity_kind::class_type &&
        entity_table[scope].name == name &&
        entity_table[named].kind == entity_kind::class_type &&
        entity_table[named].name == name)
        /* [class.qual]/2: the injected-class-name of the nominated class C
           after C:: "is instead considered to name the constructor of class
           C", which is not a template-name: [impl.clang] clang's "qualified
           reference to 'X' is a constructor name rather than a template
           name" */
        return ill_formed_at_token(template_name.token(),
                                   "a constructor name is not a template "
                                   "name: C::C names the constructor of C",
                                   "class.qual");
    if (!named && qualified && !x.unknown) {
        VTRY(m, missing_member(template_name.token(), scope));
    }
    if (!named && !qualified && is_builtin_common_type(name))
        return builtin_common_type(id, type);
    if (!named && !qualified && is_builtin_type_pack_element(name))
        return builtin_type_pack_element(id, type);
    if (!named && !qualified && is_builtin_make_integer_seq(name))
        return builtin_make_integer_seq(id, type);
    uint32_t ct = class_template_of(named);
    if (!ct) {
        uint32_t at = alias_template_of(named);
        if (at)
            return alias_template_type(id, at, type, cv);
        analysis_result refused = not_supported(id, "temp.names");
        template_name_detail(template_name.token(), x, named);
        return refused;
    }
    owned_array<template_argument> arguments;
    VTRY(filled, template_id_arguments(id, ct, arguments));
    /* [temp.dep.type]/9: "the template name is a template parameter" */
    bool dependent = function_templates[ct - 1].is_parameter ||
                     dependent_arguments(arguments);
    if (dependent) {
        /* [temp.dep.type]/9: "A type is dependent if it is ... a
           simple-template-id in which either the template name is a
           template parameter or any of the template arguments is a
           dependent type": a template's own declarator names it (not
           instantiated); one per template and arguments */
        type = dependent_specialization(function_templates[ct - 1].entity,
                                        arguments);
        return analysis_result::well_formed;
    }
    /* [temp.names]/8: "When the template-name of a simple-template-id
       names a constrained non-function template ... the constraints of
       that template shall be satisfied" ([temp.constr.decl]/3: its
       requires-clause) */
    if (head_constraint(function_templates[ct - 1].declaration)) {
        bool satisfied = true;
        VTRY(s, template_constraints_satisfied(ct, arguments, satisfied));
        if (!satisfied && substituting)
            return analysis_result::ill_formed;
        if (!satisfied)
            return ill_formed(id,
                              "the template's constraints are not satisfied",
                              "temp.names");
    }
    entity_index c = 0;
    VTRY(r, instantiate_class(id, ct, &arguments[0],
                              static_cast<uint32_t>(arguments.size()), c,
                              !(qualified && explicit_instantiating)));
    type = entity_table[c].type.unqualified();
    return analysis_result::well_formed;
}

/* the record (index plus one) of the template template-parameter whose
   argument the unqualified template-name of the simple-template-id id
   names now, when it has default template-arguments of its own
   ([temp.param]/17); else 0 */
uint32_t semantic_analysis::bound_template_template_of(SyntaxRef id) const
{
    SyntaxElement first = id[0];
    while (!first.is_token() && first.size() == 1)
        first = first[0];
    if (!first.is_token() ||
        tokens[first.token()].kind != token_kind::identifier)
        return 0;
    uint32_t name = tokens.identifier(first.token());
    lookup_result x = lookup_unqualified(name, lookup_filter::any);
    entity_index named = x.entity;
    if (!named)
        named = x.unknown_entity;
    if (!named)
        return 0;
    for (size_t i = bound_template_templates.size(); i-- > 0;) {
        const bound_template_template &b = bound_template_templates[i];
        if (b.name != name || b.target != named)
            continue;
        uint32_t record = entity_table[b.parameter].instantiation;
        if (!record)
            return 0;
        const function_template &formal = function_templates[record - 1];
        /* (only within the template that declares the parameter: the
           definition of its member outside it redeclares the parameter
           without the defaults, [temp.param]/17) */
        if (id.first_token() < formal.declaration.first_token() ||
            id.first_token() >= formal.declaration.end_token())
            return 0;
        for (uint32_t k = 0; k < formal.parameter_count; k++) {
            template_parameter_parts form;
            if (template_parameter_form(
                    template_parameter_nodes[formal.first_parameter + k],
                    form) &&
                form.has_default)
                return record;
        }
        return 0;
    }
    return 0;
}

/*
 * [temp.names]/1: a simple-template-id's template-arguments for the class
 * template function_templates[ct - 1], in order, then [temp.arg]/4's
 * defaults for those not given.
 */
analysis_result semantic_analysis::template_id_arguments(
    SyntaxRef id, uint32_t ct, owned_array<template_argument> &arguments)
{
    /* [temp.param]/17: a template-id naming the argument of a template
       template-parameter that has defaults takes them */
    uint32_t formal = bound_template_template_of(id);
    if (formal && function_templates[formal - 1].parameter_count ==
                      function_templates[ct - 1].parameter_count)
        ct = formal;
    const function_template ft = function_templates[ct - 1];
    uint32_t count = ft.parameter_count;
    /* [temp.inst]/2: the class a nested-name-specifier names is complete;
       a specialization named in its template-arguments
       (And<B, E<U...>>::value) is only named */
    uint32_t complete_needed = require_complete_class;
    if (nested_template_id)
        complete_needed = 0;
    nested_template_id = false;
    scoped_value<uint32_t> complete(require_complete_class, complete_needed);
    /* [temp.arg]/1: "When the parameter declared by the template is a
       template parameter pack, it will correspond to zero or more
       template-arguments": the last parameter's, the arguments from its
       position on */
    uint32_t pack = count;
    if (count && template_parameter_is_pack(ft.first_parameter + count - 1))
        pack = count - 1;
    owned_array<template_argument> elements;
    owned_array<SyntaxElement> written;
    owned_array<bool> expansion;
    for (uint32_t i = 2; i + 1 < id.size(); i++) {
        if (id[i].is(preprocessing_op_or_punc::ELLIPSIS)) {
            if (written.empty() || expansion.back())
                return not_supported_because(
                    id, "temp.variadic",
                    "a misplaced ellipsis in a template argument list");
            expansion.back() = true;
            continue;
        }
        if (id[i].is(syntax_kind::template_argument_list)) {
            if (!expansion_list(id[i], written, expansion))
                return not_supported_because(id, "temp.variadic",
                                             "a template argument list whose "
                                             "expansions cannot be listed");
        } else {
            written.append(id[i]);
            expansion.append(false);
        }
    }
    /* a type-id that ends in the ellipsis: its pattern the type-id before
       it */
    for (size_t w = 0; w < written.size(); w++)
        if (written[w].is(syntax_kind::type_id) && written[w].size() == 2 &&
            written[w][1].is(preprocessing_op_or_punc::ELLIPSIS)) {
            if (expansion[w])
                return not_supported_because(
                    id, "temp.variadic",
                    "a template argument with two ellipses");
            expansion[w] = true;
        }
    {
        for (size_t w = 0; w < written.size(); w++) {
            SyntaxElement a = written[w];
            /* [temp.variadic]/5: a pack expansion's pattern, once per
               element of an instantiation's packs (a template's own
               pack expansion is not analysed) */
            uint32_t times = 1;
            if (expansion[w]) {
                token_index first = first_token(a);
                token_index end = first + 1;
                if (!a.is_token())
                    end = a.end_token();
                if (end > first + 1 && SyntaxElement::from_token(end - 1).is(
                                           preprocessing_op_or_punc::ELLIPSIS))
                    end--;
                /* a partial specialization's own pack, In... (its
                   pattern the pack's name alone): the rest of the
                   primary's pack, deduced when it is matched
                   (matching_specialization) */
                uint32_t own = 0;
                if (end == first + 1 &&
                    tokens[first].kind == token_kind::identifier)
                    own = own_pack_parameter(tokens.identifier(first));
                if (own) {
                    if (arguments.size() < pack) {
                        if (w + 1 != written.size())
                            return not_supported_because(
                                id, "temp.variadic",
                                "an own pack expansion with arguments missing "
                                "before the pack");
                        template_argument v;
                        v.kind = template_argument::kind_t::pack;
                        v.template_entity =
                            template_parameter_entities[own - 1];
                        v.value = entity_table[v.template_entity].value;
                        v.count = UINT32_MAX;
                        arguments.append(make_pack(&v, 1));
                        return analysis_result::well_formed;
                    }
                    if (pack >= count)
                        return not_supported_because(
                            id, "temp.variadic",
                            "an own pack expansion with arguments missing "
                            "before the pack");
                    template_argument v;
                    v.kind = template_argument::kind_t::pack;
                    v.template_entity = template_parameter_entities[own - 1];
                    v.value = entity_table[v.template_entity].value;
                    v.count = UINT32_MAX;
                    elements.append(v);
                    continue;
                }
                /* a pattern naming the template's own packs (in its own
                   declaration, __enable_if_t<_Pred::value>...): one
                   element of unknown count, the pattern's dependent
                   written type, analysed again with the elements when
                   the template is instantiated */
                bool own_pattern = false;
                if (!expansion_count(first, end, times, &own_pattern) ||
                    own_pattern) {
                    if (!own_pattern || a.is_token() || pack >= count ||
                        arguments.size() < pack)
                        return not_supported_because(
                            id, "temp.variadic",
                            "a template argument pack expansion that cannot be "
                            "expanded");
                    template_argument v;
                    v.kind = template_argument::kind_t::pack;
                    v.type = qualified_type::make(dependent_written_type(a),
                                                  cv_none);
                    v.template_entity = type_table[v.type.unqualified()].entity;
                    v.value = UINT32_MAX;
                    v.count = UINT32_MAX;
                    elements.append(v);
                    continue;
                }
            }
            for (uint32_t k = 0; k < times; k++) {
                uint32_t position = static_cast<uint32_t>(arguments.size());
                if (position > pack)
                    position = pack;
                /* too many: clang's "too many template arguments" */
                if (position >= count)
                    return not_supported_because(id, "temp.arg",
                                                 "more template arguments than "
                                                 "the template has parameters");
                template_argument v;
                analysis_result r = analysis_result::well_formed;
                {
                    expansion_scope scope(*this, expansion[w], k, true);
                    r = template_argument_for(id, ct, position, a,
                                              arguments.data(), v);
                }
                if (r != analysis_result::well_formed)
                    return r;
                if (position == pack)
                    elements.append(v);
                else
                    arguments.append(v);
            }
        }
    }
    /* [temp.arg]/4: the parameters after those given take their default
       template-arguments; one without is clang's "too few template
       arguments" */
    owned_array<bool> set;
    for (uint32_t i = 0; i < count; i++)
        set.append(i < arguments.size() || i == pack);
    while (arguments.size() < count)
        arguments.append(template_argument());
    if (pack < count)
        arguments[pack] =
            make_pack(elements.data(), static_cast<uint32_t>(elements.size()));
    /* a default the arguments make ill-formed: [impl.clang] clang reports it at
       the default, "in instantiation of default argument" the use; neither
       declaration is judged */
    analysis_result r = default_template_arguments(id, ct, arguments, set);
    if (r == analysis_result::ill_formed)
        return not_supported_because(
            id, "temp.arg",
            "a default template argument that is ill-formed here");
    return r;
}

/* the template parameter pack name names from the current scope (a
   partial specialization's own, not bound to elements), its index in
   template_parameter_entities plus one; else 0 */
uint32_t semantic_analysis::own_pack_parameter(uint32_t name) const
{
    for (entity_index s = current_scope;; s = entity_table[s].enclosing) {
        entity_index e = entity_table.find(s, name);
        if (e) {
            for (size_t i = template_parameter_entities.size(); i-- > 0;)
                if (template_parameter_entities[i] == e &&
                    template_parameter_is_pack(static_cast<uint32_t>(i)))
                    return static_cast<uint32_t>(i + 1);
            return 0;
        }
        if (!s)
            return 0;
    }
}

static token_index default_argument_start(SyntaxElement e)
{
    if (e.is_token())
        return e.token();
    return e.first_token();
}

/* whether two non-type template-parameters are written with the same
   tokens up to their default template-argument, naming a type that
   depends on template parameters */
bool semantic_analysis::same_dependent_parameter_type(SyntaxRef a, SyntaxRef b)
{
    template_parameter_parts x, y;
    if (!template_parameter_form(a, x) || !template_parameter_form(b, y))
        return false;
    token_index ea = a.end_token(), eb = b.end_token();
    /* (the default-argument follows the "=") */
    if (x.has_default)
        ea = default_argument_start(x.default_argument) - 1;
    if (y.has_default)
        eb = default_argument_start(y.default_argument) - 1;
    if (ea - a.first_token() != eb - b.first_token())
        return false;
    for (token_index i = 0; i < ea - a.first_token(); i++)
        if (!same_token_spelling(a.first_token() + i, b.first_token() + i))
            return false;
    return ea > a.first_token();
}

/*
 * [temp.over.link]/7: whether f is equivalent to the template declared by
 * n, whose template-parameters are template_parameter_entities[first ..
 * + count) and whose function type is type: the same template-head and
 * requires-clauses, and the function types the same once each of f's
 * parameters is the other's.
 */
bool semantic_analysis::equivalent_template(const function_template &f,
                                            SyntaxRef n, uint32_t first,
                                            uint32_t count, qualified_type type)
{
    if (f.parameter_count != count ||
        !same_template_parameters(first, f.first_parameter, count) ||
        !same_constraints(f.declaration, n))
        return false;
    /* two definitions: [impl.clang] clang reports a redefinition; analysis
       does not claim it for a non-type parameter (a refusal holds the name) */
    bool both_defined = f.definition && n.size() &&
                        n[n.size() - 1].is(syntax_kind::function_definition);
    owned_array<template_argument> values;
    owned_array<bool> set;
    for (uint32_t i = 0; i < count; i++) {
        values.append(template_argument());
        set.append(false);
    }
    if (!deduce_type(entity_table[f.entity].type, type, values, set, true))
        return false;
    for (uint32_t i = 0; i < count; i++) {
        entity_index pe = template_parameter_entities[first + i];
        if (!set[i])
            continue;
        if (template_parameter_is_pack(first + i)) {
            if (values[i].kind == template_argument::kind_t::type) {
                if (entity_table[pe].kind != entity_kind::typedef_name ||
                    !(values[i].type == entity_table[pe].type))
                    return false;
            } else {
                if (values[i].kind != template_argument::kind_t::pack ||
                    values[i].count != 1)
                    return false;
                const template_argument &el = pack_arguments[values[i].value];
                if (el.kind != template_argument::kind_t::pack ||
                    el.count != UINT32_MAX || el.template_entity != pe)
                    return false;
            }
        } else if ((f.friend_class || friend_allowed || !both_defined) &&
                   entity_table[pe].kind != entity_kind::typedef_name &&
                   values[i].kind == template_argument::kind_t::value &&
                   values[i].template_entity) {
            /* a friend template's non-type parameter: the argument is the
               parameter itself, value-dependent (elsewhere the equivalence
               of such templates is not claimed) */
            if (values[i].template_entity != pe)
                return false;
        } else if (entity_table[pe].kind != entity_kind::typedef_name ||
                   !(values[i].type == entity_table[pe].type)) {
            return false;
        }
    }
    return true;
}

/*
 * [temp.param]/12: "A template-parameter shall not be given default
 * arguments by two different declarations in the same scope"; /10: "The
 * set of default template-arguments available for use is obtained by
 * merging the default arguments from all prior declarations": the new
 * declaration's parameters take the prior declaration's defaults, which
 * keep the prior declaration's parameter names
 * (template_parameter_default_names).
 */
analysis_result
semantic_analysis::merge_template_defaults(SyntaxRef n, uint32_t old_first,
                                           uint32_t new_first, uint32_t count,
                                           bool require_trailing_defaults)
{
    for (uint32_t i = 0; i < count; i++) {
        SyntaxRef before = template_parameter_nodes[old_first + i];
        if (!template_parameter_has_default(before))
            continue;
        if (template_parameter_has_default(
                template_parameter_nodes[new_first + i]))
            return not_supported_because(
                n, "temp.param", "a default template argument given twice");
        template_parameter_nodes[new_first + i] = before;
        template_parameter_default_names[new_first + i] =
            template_parameter_default_names[old_first + i];
    }
    if (require_trailing_defaults) {
        /* [temp.param]/14 on the merged defaults (clang's error is the
           definition's) */
        for (uint32_t i = 1; i < count; i++)
            if (template_parameter_has_default(
                    template_parameter_nodes[new_first + i - 1]) &&
                !template_parameter_has_default(
                    template_parameter_nodes[new_first + i]))
                return not_supported_because(
                    n, "temp.param",
                    "merged defaults with a parameter lacking one after a "
                    "defaulted one");
    }
    return analysis_result::well_formed;
}

bool semantic_analysis::same_template_parameters(uint32_t first,
                                                 uint32_t other_first,
                                                 uint32_t count)
{
    for (uint32_t i = 0; i < count; i++) {
        const entity &a = entity_table[template_parameter_entities[first + i]];
        const entity &b =
            entity_table[template_parameter_entities[other_first + i]];
        if (a.kind != b.kind || template_parameter_is_pack(first + i) !=
                                    template_parameter_is_pack(other_first + i))
            return false;
        /* (a non-type parameter's type: the same, or each list's type
           parameter at the same position, template<class T, T...>) */
        if (a.kind == entity_kind::none && !(a.type == b.type)) {
            uint32_t at =
                own_type_parameter_position(a.type, a.enclosing, first);
            if (at == UINT32_MAX) {
                /* a dependent type written the same in both heads
                   (template<class C, typename C::T>: [temp.over.link]/5's
                   equivalent expressions as the same tokens); another
                   spelling is not judged */
                if (!same_dependent_parameter_type(
                        template_parameter_nodes[first + i],
                        template_parameter_nodes[other_first + i]))
                    return false;
            } else if (a.type.cv() != b.type.cv() ||
                       at != own_type_parameter_position(b.type, b.enclosing,
                                                         other_first))
                return false;
        }
        if (a.kind == entity_kind::template_entity) {
            /* template template-parameters: their own parameters */
            const function_template x = function_templates[a.instantiation - 1];
            const function_template y = function_templates[b.instantiation - 1];
            if (x.parameter_count != y.parameter_count ||
                !same_template_parameters(x.first_parameter, y.first_parameter,
                                          x.parameter_count))
                return false;
            /* ([temp.param]/17: a default of an inner parameter that the
               other head does not repeat is not judged) */
            for (uint32_t k = 0; k < x.parameter_count; k++) {
                template_parameter_parts p, q;
                if (!template_parameter_form(
                        template_parameter_nodes[x.first_parameter + k], p) ||
                    !template_parameter_form(
                        template_parameter_nodes[y.first_parameter + k], q) ||
                    p.has_default != q.has_default)
                    return false;
            }
        }
    }
    return true;
}

/* a template's entity named name in outer, declared by n
   (function_templates[index]), and its record of parameters
   template_parameter_entities[first .. + count); [basic.link]/4: a name of
   an unnamed namespace has internal linkage */
semantic_analysis::function_template
semantic_analysis::new_template(uint32_t name, entity_index outer, SyntaxRef n,
                                uint32_t index, uint32_t first, uint32_t count)
{
    entity_index t =
        entity_table.make(entity_kind::template_entity, name, outer);
    entity_table[t].instantiation = index + 1;
    entity_table[t].linkage_of_name = linkage::external;
    if (within_unnamed_namespace(outer))
        entity_table[t].linkage_of_name = linkage::internal;
    entity_table.add_declaration(t, n);
    function_template record;
    record.entity = t;
    record.declaration = n;
    record.first_parameter = first;
    record.parameter_count = count;
    return record;
}

/* the template's name in outer, held by an entity of its own
   (held_by_template), and its record kept */
void semantic_analysis::hold_template_name(function_template &record,
                                           uint32_t name, entity_index outer)
{
    entity_index held = entity_table.make(entity_kind::none, name, outer);
    entity_table[held].held_by_template = true;
    entity_table.bind(outer, name, held);
    record.name = held;
    function_templates.append(record);
}

/* the number of a simple-declaration's leading attribute-specifier-seqs
   and gnu-attribute-specifiers whose attributes change nothing vcc
   generates */
uint32_t
semantic_analysis::attributes_without_effect_before(SyntaxRef declaration) const
{
    uint32_t k = 0;
    while (k < declaration.size() &&
           class_head_attributes_without_effect(declaration[k]))
        k++;
    return k;
}

/* [temp.variadic]/7: whether each of the tokens [first, end) that is the
   pack's name is within the pattern of a pack expansion: an ellipsis
   follows it that ends the template argument or parenthesized element it
   is in, or one enclosing it (A..., tuple<A>&..., x<y<A::v>...>), not a
   sibling's (res<A, B...>) */
bool semantic_analysis::pack_only_expanded(uint32_t name, token_index first,
                                           token_index end) const
{
    auto is = [&](token_index t, preprocessing_op_or_punc op) {
        return SyntaxElement::from_token(t).is(op);
    };
    for (token_index t = first; t < end; t++) {
        if (tokens[t].kind != token_kind::identifier ||
            tokens.identifier(t) != name)
            continue;
        /* sizeof...(A) names the pack and expands it ([expr.sizeof]/5) */
        if (t >= first + 3 && t + 1 < end &&
            tokens[t - 3].kind == token_kind::keyword &&
            syntax_parser.keyword_of(tokens[t - 3]) ==
                standard_keyword::sizeof_ &&
            is(t - 2, preprocessing_op_or_punc::ELLIPSIS) &&
            is(t - 1, preprocessing_op_or_punc::LEFT_PARENTHESIS) &&
            is(t + 1, preprocessing_op_or_punc::RIGHT_PARENTHESIS))
            continue;
        /* depth of brackets from the name's; level the element the scan
           is in, sibling once a comma passed into the next element */
        int depth = 0, level = 0;
        bool sibling = false, expanded = false;
        for (token_index u = t + 1; u < end && !expanded; u++) {
            if (is(u, preprocessing_op_or_punc::LESS) ||
                is(u, preprocessing_op_or_punc::LEFT_PARENTHESIS) ||
                is(u, preprocessing_op_or_punc::LEFT_BRACKET))
                depth++;
            else if (is(u, preprocessing_op_or_punc::GREATER) ||
                     is(u, preprocessing_op_or_punc::RIGHT_PARENTHESIS) ||
                     is(u, preprocessing_op_or_punc::RIGHT_BRACKET))
                depth--;
            else if (is(u, preprocessing_op_or_punc::GREATER_GREATER))
                depth -= 2;
            else if (is(u, preprocessing_op_or_punc::COMMA) && depth == level)
                sibling = true;
            else if (is(u, preprocessing_op_or_punc::ELLIPSIS) &&
                     depth == level && !sibling)
                expanded = true;
            if (depth < level) {
                level = depth;
                sibling = false;
            }
        }
        if (!expanded)
            return false;
    }
    return true;
}

} // namespace cplusplus
