/*
 * member-definition.cpp: [temp.mem.func], [temp.class.spec.mfunc],
 * [temp.static]: members of class templates (and of their partial
 * specializations) defined outside the class, and their definitions per
 * specialization ([temp.inst]).
 * Split from declared-templates.cpp, which has [temp.pre]'s declarations of
 * templates and what the kinds share.
 */
#include "../cplusplus-internal.h"
#include "frontend/cplusplus/sema.h"
#include "declared-templates.h"

namespace cplusplus
{

/* a function-definition's declarator-id S<A...>::f (behind ptr-operators):
   the simple-template-id, else 0 */
SyntaxRef semantic_analysis::out_of_class_member(SyntaxRef definition,
                                                 token_index *name) const
{
    SyntaxRef nested = out_of_class_nested(definition, name);
    if (!nested || nested.size() != 2 ||
        !nested[0].is(syntax_kind::simple_template_id))
        return 0;
    return nested[0].index();
}

/* the simple-template-id of the declarator-id S<A...>::m of a
   simple-declaration of one init-declarator (template<class T> int S<T>::m
   = 1;) or of one declarator without an initializer (template<class T> int
   S<T>::m;: [temp.static]/1, a definition, [basic.def]/2), else 0; and the
   member's name */
SyntaxRef
semantic_analysis::out_of_class_variable_member(SyntaxRef declaration,
                                                token_index *name) const
{
    if (!declaration.is(syntax_kind::simple_declaration) ||
        declaration.size() != 3 ||
        !declaration[0].is(syntax_kind::decl_specifier_seq) ||
        !declaration[1].is(syntax_kind::init_declarator,
                           syntax_kind::qualified_id))
        return 0;
    auto item = declaration[1];
    if (item.is(syntax_kind::init_declarator) &&
        (item.size() != 2 || !item[0].is(syntax_kind::qualified_id)))
        return 0;
    auto q = item;
    if (item.is(syntax_kind::init_declarator))
        q = item[0];
    if (q.size() != 2 || !q[0].is(syntax_kind::nested_name_specifier) ||
        !q[1].is_identifier())
        return 0;
    auto nested = q[0];
    if (nested.size() != 2 || !nested[0].is(syntax_kind::simple_template_id))
        return 0;
    if (name)
        *name = q[1].token();
    return nested[0].index();
}

/* a function-definition's declarator-id N::f (behind ptr-operators): the
   nested-name-specifier N::, else 0; and the member's name */
SyntaxRef semantic_analysis::out_of_class_nested(SyntaxRef definition,
                                                 token_index *name) const
{
    /* ([ext:gnu_attributes] before the declaration are not among its
       parts) */
    SyntaxTail parts(definition, leading_gnu_attributes(definition));
    /* (a constructor or destructor has no decl-specifier-seq) */
    size_t at = parts.size() - 2;
    if ((parts.size() != 3 && parts.size() != 2) || parts[at].is_token() ||
        (parts.size() == 3 && !parts[0].is(syntax_kind::decl_specifier_seq)))
        return 0;
    DeclaratorParts d(parts[at]);
    SyntaxElement nested = d.qualifier();
    if (!d.noptr || !nested)
        return 0;
    SyntaxElement member = d.unqualified();
    /* (an operator function's name is made when the declarator is
       analysed: [over.oper]) */
    bool operator_name = static_cast<bool>(member.operator_function_id());
    bool conversion_name = member.is(syntax_kind::conversion_function_id);
    /* ([class.dtor]/1: ~ and the class's name) */
    bool destructor = member.is(syntax_kind::unqualified_id) &&
                      member.size() == 2 && member[1].is_identifier();
    if (!operator_name && !conversion_name && !destructor &&
        !member.is_identifier())
        return 0;
    if (name && destructor)
        *name = member[1].token();
    else if (name && operator_name)
        *name = member.operator_function_id().operator_keyword().token();
    else if (name && conversion_name)
        *name = member[0].token();
    else if (name)
        *name = member.token();
    return nested.index();
}

/*
 * [temp.mem.func]/1: "A member function of a class template may be
 * defined outside of the class template definition in which it is
 * declared."  /1's "the template-arguments are the same as the
 * template-parameters" of its class: template<class T> R S<T>::f(P...)
 * {...}, or of a partial specialization's ([temp.class.spec.mfunc]/1:
 * "The template parameter list of a member of a class template partial
 * specialization shall match the template parameter list of the class
 * template partial specialization.  The template argument list of a member
 * of a class template partial specialization shall match the template
 * argument list of the class template partial specialization."), with the
 * class's parameter names (its body finds them in the class).  Analysed
 * for each specialization of the class there is or will be
 * (define_template_member).
 */
analysis_result semantic_analysis::member_definition_of_template(
    SyntaxRef n, SyntaxRef definition, SyntaxRef inner)
{
    entity_index outer = current_scope;
    owned_array<SyntaxRef> parameters;
    VTRY(r, template_head_parameters(n, parameters, true, true, true, true));
    /* [temp.static]/1: a static data member's definition is a
       simple-declaration */
    bool variable = definition.is(syntax_kind::simple_declaration);
    SyntaxRef class_member = 0;
    if (variable) {
        SyntaxRef only = sole_decl_specifier(definition);
        if (only && only.is(syntax_kind::class_specifier))
            class_member = only;
    }
    SyntaxTail parts(definition, leading_gnu_attributes(definition));
    /* (a constructor or destructor has no decl-specifier-seq) */
    if (!variable && ((parts.size() != 3 && parts.size() != 2) ||
                      !parts[parts.size() - 1].is(syntax_kind::function_body)))
        return not_supported(n, "temp.mem.func");
    /* [temp.param]/12: "A default template-argument shall not be specified
       in the template-parameter-lists of the definition of a member of a
       class template that appears outside of the member's class." */
    for (SyntaxRef x : parameters)
        if (template_parameter_has_default(x))
            return not_supported(x, "temp.param");
    /* ... nor in the member template's own template-head */
    if (inner) {
        owned_array<SyntaxRef> own;
        VTRY(ri, template_head_parameters(inner, own, true, true, true, true));
        for (SyntaxRef x : own)
            if (template_parameter_has_default(x))
                return ill_formed(x,
                                  "cannot add a default template argument to "
                                  "the definition of a member of a class "
                                  "template",
                                  "temp.param");
    }
    SyntaxRef id = 0;
    token_index class_member_name = 0;
    if (class_member) {
        id = out_of_class_class_member(class_member, &class_member_name);
    } else if (variable) {
        id = out_of_class_variable_member(definition);
    } else {
        id = out_of_class_member(definition);
    }
    lookup_result x;
    entity_index named = 0;
    if (!template_id_name(id, x, named))
        return not_supported(n, "temp.mem.func");
    uint32_t ct = class_template_of(named);
    if (!ct || function_templates[ct - 1].name != named ||
        function_templates[ct - 1].primary ||
        !declared_in_scope_of(function_templates[ct - 1].entity, outer))
        return not_supported(n, "temp.mem.func");
    /* [class.static.data]/1: "The declaration of a static data member
       ... static ... the keyword static shall appear only in its
       declaration in the class" ([dcl.stc]/1: the storage class specifier
       is not part of a definition outside the class): clang, "'static'
       can only be specified inside the class definition" */
    if (variable && !class_member && definition.size() &&
        definition[0].is(syntax_kind::decl_specifier_seq)) {
        for (auto sp : definition[0].elements())
            if (sp.is(standard_keyword::static_))
                return ill_formed(definition,
                                  "'static' can only be specified inside "
                                  "the class definition",
                                  "class.static.data");
    }
    uint32_t index = 0, first = 0;
    entity_index scope = 0;
    VTRY_SET(r, template_parameter_scope(n, parameters, outer, 0, index, scope,
                                         first));
    /* the template-id's arguments in terms of the parameters; what they
       record for claims is the template's */
    owned_array<template_argument> arguments;
    partial_arguments++;
    declaring_specializations++;
    r = template_id_arguments_in(id, ct, scope, arguments);
    declaring_specializations--;
    partial_arguments--;
    if (r != analysis_result::well_formed)
        return r;
    uint32_t pattern = 0;
    owned_array<template_argument> deduced;
    partial_arguments++;
    declaring_specializations++;
    r = matching_specialization(id, ct, arguments.data(),
                                static_cast<uint32_t>(arguments.size()),
                                pattern, deduced);
    declaring_specializations--;
    partial_arguments--;
    if (r != analysis_result::well_formed)
        return r;
    token_index after_declarator_id = 0;
    if (class_member) {
        after_declarator_id = class_member_name + 1;
    } else if (variable) {
        after_declarator_id = definition[1].end_token();
        if (definition[1].is(syntax_kind::init_declarator))
            after_declarator_id = definition[1][0].end_token();
    } else {
        token_index name = 0;
        out_of_class_member(definition, &name);
        after_declarator_id = name + 1;
    }
    const template_argument *mine = arguments.data();
    uint32_t mine_count = static_cast<uint32_t>(arguments.size());
    function_template of = function_templates[ct - 1];
    if (pattern) {
        of = function_templates[pattern - 1];
        mine = deduced.data();
        mine_count = static_cast<uint32_t>(deduced.size());
    }
    /* each argument the parameter of the same position, with the
       class's parameter's name */
    if (!of.definition || !of.parameter_count ||
        mine_count != parameters.size() ||
        of.parameter_count != parameters.size() ||
        !same_template_parameters(first, of.first_parameter,
                                  of.parameter_count))
        return not_supported(n, "temp.mem.func");
    for (uint32_t i = 0; i < mine_count; i++) {
        entity_index pe = template_parameter_entities[first + i];
        uint32_t mine_name = entity_table[pe].name;
        uint32_t class_name =
            entity_table[template_parameter_entities[of.first_parameter + i]]
                .name;
        if (!argument_is_parameter(mine[i], pe))
            return not_supported(n, "temp.mem.func");
        if (mine_name == class_name)
            continue;
        /* [basic.lookup.qual]/3: "names following the qualified-id being
           declared are looked up in the scope of the member's class", which
           finds the class's template-parameter names, not this
           definition's (its decl-specifiers and the template-id's
           arguments do find them): a definition that writes either of the
           two names after its declarator-id is not analysed */
        for (token_index t = after_declarator_id; t < definition.end_token();
             t++)
            if (tokens[t].kind == token_kind::identifier &&
                (tokens.identifier(t) == mine_name ||
                 tokens.identifier(t) == class_name))
                return not_supported(n, "basic.lookup.qual");
    }
    if (!variable && !inner && of.definition.is(syntax_kind::class_specifier)) {
        /* [temp.mem]/1: "A member template of a class template that is
           defined outside of its class template definition shall be
           specified with a template-head equivalent to that of the class
           template followed by a template-head equivalent to that of the
           member template": a definition with the class's template-head
           alone, of a name only member templates of the class declare
           (each member that writes the name, a template-declaration) */
        token_index name = 0;
        out_of_class_member(definition, &name);
        uint32_t wanted = 0;
        if (name && tokens[name].kind == token_kind::identifier)
            wanted = tokens.identifier(name);
        bool by_template = false, by_other = false;
        for (auto member : of.definition.class_specifier()
                               .member_specification()
                               .elements()) {
            if (!wanted || member.is_token())
                continue;
            bool writes = false;
            for (token_index t = member.first_token(); t < member.end_token();
                 t++)
                if (tokens[t].kind == token_kind::identifier &&
                    tokens.identifier(t) == wanted)
                    writes = true;
            if (writes && member.is(syntax_kind::template_declaration))
                by_template = true;
            else if (writes)
                by_other = true;
        }
        if (by_template && !by_other)
            return ill_formed_at_token(
                name,
                "a member template of a class template defined outside of "
                "its class template definition shall be specified with the "
                "class template's template-head followed by the member "
                "template's",
                "temp.mem");
    }
    template_member_definition record;
    record.first_parameter = first;
    record.declaration = n;
    record.definition = definition;
    record.inner = inner;
    record.template_entity = function_templates[ct - 1].entity;
    record.pattern = pattern;
    uint32_t member = static_cast<uint32_t>(template_member_definitions.size());
    template_member_definitions.append(record);
    /* the specializations of the class defined before this */
    analysis_result result = analysis_result::well_formed;
    for (uint32_t k = 0; k < instantiations.size(); k++) {
        const instantiation_record &y = instantiations[k];
        if (y.template_entity != record.template_entity ||
            y.pattern != pattern || !y.specialization || y.defining ||
            entity_table[y.specialization].kind != entity_kind::class_type ||
            !entity_table[y.specialization].defined)
            continue;
        analysis_result one = define_template_member(member, k, false);
        if (one != analysis_result::well_formed &&
            result == analysis_result::well_formed)
            result = one;
    }
    return result;
}

/* the class (not a specialization of a class template) that a
   nested-name-specifier names, in the current scope; else 0 */
entity_index semantic_analysis::plain_class_nominated(SyntaxRef nested)
{
    if (!nested.is(syntax_kind::nested_name_specifier) || nested.size() != 2 ||
        !nested[0].is_identifier())
        return 0;
    nominated_scope scope;
    if (nested_name_specifier(nested, scope) != analysis_result::well_formed)
        return 0;
    if (!scope.entity ||
        entity_table[scope.entity].kind != entity_kind::class_type ||
        !entity_table[scope.entity].size)
        return 0;
    return scope.entity;
}

/*
 * [temp.mem]/1: "A member template can be defined within or outside its
 * class definition": template<class T> R C::f(P) {...} for a class C that
 * is no template; the definition of the member template C declares, as
 * define_member_template does for a specialization of a class template.
 */
analysis_result semantic_analysis::define_plain_member_template(
    SyntaxRef n, SyntaxRef definition, entity_index c)
{
    /* [class.mfct]/2: in a namespace scope enclosing the class definition */
    if (!namespace_encloses(current_scope, entity_table[c].enclosing))
        return not_supported(n, "class.mfct");
    return define_member_template_of(c, n, n, definition, false);
}

/* [class.ctor]/1, [class.dtor]/1: the constructor or destructor of the
   specialization that a definition outside the class defines (they are not
   found by name); 0 for any other member */
entity_index
semantic_analysis::defined_special_member(entity_index specialization,
                                          const template_member_definition &o,
                                          token_index name)
{
    if (tokens.identifier(name) != entity_table[specialization].name)
        return 0;
    entity_index d = entity_table[specialization].destructor;
    if (d && (declared_by(d, o.definition) || declared_by(d, o.declaration)))
        return d;
    for (entity_index k = entity_table[specialization].first_constructor; k;
         k = entity_table[k].next_constructor)
        if (declared_by(k, o.definition) || declared_by(k, o.declaration))
            return k;
    return 0;
}

/*
 * [temp.inst]/4: "the implicit instantiation of a class template
 * specialization causes ... the implicit instantiation of the definitions
 * of ... member functions ... when used"; here with the class (as its
 * members defined in it).  The function-definition analysed again where
 * the class's template-parameters' names are its arguments: its
 * nested-name-specifier names the specialization, whose member it defines
 * ([class.mfct]/1), inline as the class's own member functions are
 * ([basic.def.odr]/13: emitted where used, weak).  Errors are the
 * instantiation's, not judged here ([temp.inst]/1; [impl.clang] clang reports
 * them at the definition "in instantiation of" the use): unsupported.
 */
analysis_result semantic_analysis::define_template_member(uint32_t member,
                                                          uint32_t index,
                                                          bool recorded)
{
    const template_member_definition o = template_member_definitions[member];
    const instantiation_record x = instantiations[index];
    if (instantiation_depth >= 64)
        return not_supported(o.declaration, "temp.inst");
    if (o.inner)
        return define_member_template(member, index, recorded);
    function_template of =
        function_templates[entity_table[x.template_entity].instantiation - 1];
    uint32_t first = x.first_argument;
    if (x.pattern) {
        of = function_templates[x.pattern - 1];
        first = x.first_deduced;
    }
    entity_index scope = entity_table.make(
        entity_kind::block_scope, 0, entity_table[x.template_entity].enclosing);
    bool variable = o.definition.is(syntax_kind::simple_declaration);
    /* [temp.local]/1, [basic.scope.temp]/1: the definition's own parameter
       names ([impl.clang] clang does not find the class's, if they differ) */
    uint32_t names = o.first_parameter;
    for (uint32_t i = 0; i < of.parameter_count; i++)
        bind_template_argument(scope, template_parameter_entities[names + i],
                               template_arguments[first + i]);
    instantiation_context context;
    enter_instantiation(context, scope, o.declaration);
    template_member_scope = scope;
    declaration_access = 0;
    instantiating = 0;
    class_instantiating = 0;
    analysis_result r = analysis_result::well_formed;
    if (variable)
        r = simple_declaration(o.definition);
    else
        r = function_definition(o.definition);
    leave_instantiation(context);
    /* the member defined: the class's function this definition declares
       (last_declared may be a name its body declared) */
    entity_index defined = 0;
    if (r == analysis_result::well_formed && variable) {
        SyntaxRef only = sole_decl_specifier(o.definition);
        token_index class_name = 0;
        if (out_of_class_class_member(only, &class_name)) {
            entity_index mc = entity_table.find(x.specialization,
                                                tokens.identifier(class_name));
            if (mc && entity_table[mc].kind == entity_kind::class_type &&
                entity_table[mc].defined)
                return r;
            r = analysis_result::unsupported;
        } else {
            /* [temp.static]/1: the specialization's static data member */
            token_index name = 0;
            out_of_class_variable_member(o.definition, &name);
            entity_index v =
                entity_table.find(x.specialization, tokens.identifier(name));
            if (v && entity_table[v].kind == entity_kind::variable &&
                (declared_by(v, o.definition) || declared_by(v, o.declaration)))
                defined = v;
            if (!defined)
                r = analysis_result::unsupported;
        }
    } else if (r == analysis_result::well_formed) {
        token_index name = 0;
        out_of_class_member(o.definition, &name);
        defined = defined_special_member(x.specialization, o, name);
        if (!defined) {
            /* (the overload this definition is the member's) */
            for (entity_index f = entity_table.find(x.specialization,
                                                    tokens.identifier(name));
                 f && !defined && entity_table[f].kind == entity_kind::function;
                 f = entity_table[f].next_overload)
                if (declared_by(f, o.definition) ||
                    declared_by(f, o.declaration))
                    defined = f;
        }
        if (!defined)
            r = analysis_result::unsupported;
    }
    if (r == analysis_result::well_formed) {
        /* (a static data member of a class template is weak and emitted
           where used, as an inline variable: [basic.def.odr]/13) */
        entity_table[defined].inline_ = true;
        return r;
    }
    return member_definition_unsupported(o.declaration, recorded);
}

/* a member's definition outside its class that is not known to be
   well-formed for a specialization of the class: unsupported, and the
   declaration already analysed (recorded) too */
analysis_result
semantic_analysis::member_definition_unsupported(SyntaxRef declaration,
                                                 bool recorded)
{
    size_t unsupported_before = unsupported.size();
    not_supported(declaration, "temp.inst");
    if (!recorded)
        return analysis_result::unsupported;
    for (analysed_declaration &d : declarations) {
        if (d.node != declaration || d.result != analysis_result::well_formed)
            continue;
        d.result = analysis_result::unsupported;
        d.first_unsupported = static_cast<uint32_t>(unsupported_before);
        d.has_unsupported = unsupported.size() > unsupported_before;
    }
    return analysis_result::unsupported;
}

/*
 * [temp.mem]/1, [temp.mem.func]/1: the member function template of a
 * class template, defined outside it (template<class T> template<class U>
 * R S<T>::f(P) {...}), for the specialization of the class that
 * instantiations[index] made: the definition of the member template of the
 * specialization that declares it (the declaration's template-head and
 * function type the definition's, [temp.over.link]/7, the same parameter
 * names so that the body finds the declaration's), kept for its
 * specializations (instantiate_definition).
 */
analysis_result semantic_analysis::define_member_template(uint32_t member,
                                                          uint32_t index,
                                                          bool recorded)
{
    const template_member_definition o = template_member_definitions[member];
    return define_member_template_of(instantiations[index].specialization,
                                     o.declaration, o.inner, o.definition,
                                     recorded);
}

/* the member template of class c that the template-declaration
   (declaration, whose template-head is inner) defines */
analysis_result semantic_analysis::define_member_template_of(
    entity_index c, SyntaxRef declaration, SyntaxRef inner,
    SyntaxRef definition, bool recorded, bool allow_redefine)
{
    owned_array<SyntaxRef> parameters;
    instantiation_context context;
    enter_instantiation(context, entity_table[c].enclosing, declaration);
    if (template_head_parameters(inner, parameters, true, true, true, true) !=
            analysis_result::well_formed ||
        head_constraint(inner) || trailing_constraint(inner)) {
        leave_instantiation(context);
        return member_definition_unsupported(declaration, recorded);
    }
    for (SyntaxRef x : parameters)
        if (template_parameter_has_default(x)) {
            leave_instantiation(context);
            return member_definition_unsupported(declaration, recorded);
        }
    current_scope = c;
    uint32_t number = 0, first = 0;
    entity_index scope = 0;
    specified s;
    declarator_meaning m;
    qualified_type type;
    uint32_t lead = leading_gnu_attributes(definition);
    analysis_result r =
        template_parameter_scope(inner, parameters, c, 0, number, scope, first);
    /* (the parameters' scope stands for the namespace of the qualified
       declarator-id, as define_template_member's) */
    template_member_scope = scope;
    if (r == analysis_result::well_formed)
        r = function_template_declarator(definition, lead, c, scope, s, m,
                                         type);
    uint32_t cv = function_cv;
    bool exceptions_specified = function_exceptions_specified;
    leave_instantiation(context);
    bool constructor = (lead < definition.size() &&
                        declares_special_member(c, definition[lead])) ||
                       (lead + 1 < definition.size() &&
                        definition[lead].is(syntax_kind::decl_specifier_seq) &&
                        declares_special_member(c, definition[lead + 1]));
    entity_index held = 0;
    if (r == analysis_result::well_formed && constructor) {
        held = entity_table[c].constructor_templates;
    } else if (r == analysis_result::well_formed && m.has_id) {
        held = entity_table.find(c, tokens.identifier(m.id));
        if (held)
            held = member_templates_of(held);
    }
    if (!held)
        return member_definition_unsupported(declaration, recorded);
    /* the declaration the definition matches: not defined yet, the same
       template-parameters and function type ([temp.over.link]/7) */
    owned_array<uint32_t> group;
    function_template_overloads(held, group);
    for (uint32_t g : group) {
        const function_template f = function_templates[g - 1];
        const entity old = entity_table[f.entity];
        if ((!allow_redefine && f.definition) || old.enclosing != c ||
            old.member_cv != cv || old.constexpr_ != s.constexpr_)
            continue;
        if (!equivalent_template(f, inner, first,
                                 static_cast<uint32_t>(parameters.size()),
                                 type))
            continue;
        bool renamed = false;
        for (uint32_t i = 0; i < f.parameter_count; i++) {
            entity_index pe = template_parameter_entities[first + i];
            entity_index de =
                template_parameter_entities[f.first_parameter + i];
            if (entity_table[pe].name != entity_table[de].name)
                renamed = true;
        }
        if (renamed) {
            VTRY(merged,
                 merge_template_defaults(declaration, f.first_parameter, first,
                                         f.parameter_count, false));
        }
        /* a declaration (template<> template<class U> R C<int>::f(U);)
           redeclares the member template: nothing to define */
        if (definition.is(syntax_kind::simple_declaration)) {
            /* (a deleted template's redeclaration un-deletes it, and the
               exception specifications are not compared: not yet) */
            if (old.deleted || exceptions_specified ||
                (f.definition &&
                 f.definition.is(syntax_kind::function_definition) &&
                 !f.definition.function_definition().function_body().plain()))
                return member_definition_unsupported(declaration, recorded);
            return analysis_result::well_formed;
        }
        function_template &record = function_templates[g - 1];
        record.declaration = declaration;
        record.definition = definition;
        if (renamed) {
            record.first_parameter = first;
            entity_table[f.entity].type = type;
        }
        entity_table[f.entity].defined = true;
        return analysis_result::well_formed;
    }
    return member_definition_unsupported(declaration, recorded);
}

} // namespace cplusplus
