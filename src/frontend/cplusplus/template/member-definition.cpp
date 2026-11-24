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

/* a function-definition's declarator-id X<A...>::Y<B...>::f: the first
   simple-template-id ([temp.mem]/1: a member of a member class template),
   else 0 */
SyntaxRef semantic_analysis::out_of_class_member_class_function(
    SyntaxRef definition) const
{
    SyntaxRef nested = out_of_class_nested(definition, nullptr);
    if (!nested || nested.size() != 4 ||
        !nested[0].is(syntax_kind::simple_template_id) ||
        !(nested[2].is(syntax_kind::simple_template_id) ||
          nested[2].is_identifier()))
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
    if (!declaration.is(syntax_kind::simple_declaration))
        return 0;
    /* ([ext:gnu_attributes] before the declaration are not among its
       parts) */
    SyntaxTail parts(declaration, leading_gnu_attributes(declaration));
    if (parts.size() != 3 || !parts[0].is(syntax_kind::decl_specifier_seq) ||
        !parts[1].is(syntax_kind::init_declarator, syntax_kind::qualified_id))
        return 0;
    auto item = parts[1];
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

/* a function-definition of a member of a member class of a class template
   (X<T>::Y::f) that has a default argument ([dcl.fct.default]/6: "default
   arguments ... shall not be specified in the definition of a member
   function outside its class") or a qualified type in its decl-specifier-seq
   without typename ([temp.res]/5): neither is judged here */
bool semantic_analysis::member_class_function_unjudged(
    const SyntaxTail &parts) const
{
    size_t last = parts.size() - 1;
    if (parts.size() == 3 && parts[0].is(syntax_kind::decl_specifier_seq)) {
        bool typename_seen = false, qualified = false;
        for (token_index t = parts[0].first_token(); t < parts[0].end_token();
             t++) {
            const token &x = tokens[t];
            if (x.kind == token_kind::operator_or_punctuator &&
                preprocessing_op_or_punc_primary(x.op_or_punc) ==
                    preprocessing_op_or_punc::COLON_COLON)
                qualified = true;
            if (p.keyword_of(x) == standard_keyword::typename_)
                typename_seen = true;
        }
        if (qualified && !typename_seen)
            return true;
    }
    for (token_index t = parts[last - 1].first_token();
         t < parts[last].first_token(); t++) {
        const token &x = tokens[t];
        if (x.kind == token_kind::operator_or_punctuator &&
            preprocessing_op_or_punc_primary(x.op_or_punc) ==
                preprocessing_op_or_punc::EQUAL)
            return true;
    }
    return false;
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
        id = out_of_class_class_member(class_member, &class_member_name,
                                       static_cast<bool>(inner));
    } else if (variable) {
        id = out_of_class_variable_member(definition);
    } else {
        id = out_of_class_member(definition);
        if (!id) {
            id = out_of_class_member_class_function(definition);
            if (id && member_class_function_unjudged(parts))
                return not_supported(n, "dcl.fct.default");
        }
    }
    lookup_result x;
    entity_index named = 0;
    if (!template_id_name(id, x, named))
        return not_supported(n, "temp.mem.func");
    uint32_t ct = class_template_of(named);
    if (!ct) {
        analysis_result refused = not_supported(n, "temp.mem.func");
        if (SyntaxElement template_name =
                id.simple_template_id().template_name())
            template_name_detail(template_name.token(), x, named);
        return refused;
    }
    if (function_templates[ct - 1].name != named)
        return not_supported_because(
            n, "temp.mem.func",
            "the name is a class template's injected-class-name");
    if (function_templates[ct - 1].primary)
        return not_supported_because(n, "temp.mem.func",
                                     "the class is a specialization's pattern");
    if (!declared_in_scope_of(function_templates[ct - 1].entity, outer))
        return not_supported_because(
            n, "temp.mem.func",
            "the class template is declared in another scope");
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
    for (uint32_t i = 0; i < mine_count; i++)
        if (!argument_is_parameter(mine[i],
                                   template_parameter_entities[first + i]))
            return not_supported(n, "temp.mem.func");
    if (parameter_names_after_declarator(definition, after_declarator_id, first,
                                         of.first_parameter, mine_count))
        return not_supported(n, "basic.lookup.qual");
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
    if (class_member && inner &&
        of.definition.is(syntax_kind::class_specifier)) {
        /* [temp.mem]/1: the class template's member template the
           definition declares, with the template-head it was declared
           with: found by its name, a member that is a template-declaration
           of the class template (the definition is analysed per
           specialization, which finds the rest; a head with a
           requires-clause, or no such member, is not judged here) */
        uint32_t wanted = tokens.identifier(class_member_name);
        bool found = false;
        for (auto member : of.definition.class_specifier()
                               .member_specification()
                               .elements()) {
            if (member.is_token() ||
                !member.is(syntax_kind::template_declaration))
                continue;
            bool writes = false;
            for (token_index t = member.first_token(); t < member.end_token();
                 t++)
                if (tokens[t].kind == token_kind::identifier &&
                    tokens.identifier(t) == wanted)
                    writes = true;
            if (!writes)
                continue;
            found = true;
            if (head_constraint(member) || trailing_constraint(member))
                return not_supported(n, "temp.mem");
        }
        if (!found || head_constraint(inner) || trailing_constraint(inner))
            return not_supported(n, "temp.mem");
    }
    if (!variable && inner && !out_of_class_member(definition) &&
        of.definition.is(syntax_kind::class_specifier)) {
        /* [temp.mem]/1, [basic.lookup.qual]/3: the member class template
           the second template-head and Y<U> name, declared in the class
           with its parameter names: where the definition's differ, a name
           after the declarator-id is the class's or the definition's, not
           judged here (as for the class's own parameters above); the
           definition is analysed for each specialization of the class */
        SyntaxRef nested = out_of_class_nested(definition);
        SyntaxElement y_name = nested[2].simple_template_id().template_name();
        if (!y_name || !y_name.is_identifier())
            return not_supported(n, "temp.mem");
        uint32_t wanted = tokens.identifier(y_name.token());
        SyntaxRef declared = 0;
        for (auto member : of.definition.class_specifier()
                               .member_specification()
                               .elements()) {
            if (member.is_token())
                continue;
            bool writes = false;
            for (token_index t = member.first_token(); t < member.end_token();
                 t++)
                if (tokens[t].kind == token_kind::identifier &&
                    tokens.identifier(t) == wanted)
                    writes = true;
            if (!writes)
                continue;
            if (declared || !member.is(syntax_kind::template_declaration) ||
                member.size() != 2 || !member[0].is(syntax_kind::template_head))
                return not_supported(n, "temp.mem");
            declared = member.index();
        }
        owned_array<SyntaxRef> mine_parameters, class_parameters;
        if (!declared || !template_head_list(inner[0], mine_parameters) ||
            !template_head_list(declared[0], class_parameters) ||
            mine_parameters.size() != class_parameters.size())
            return not_supported(n, "temp.mem");
        token_index declarator_name = 0;
        out_of_class_nested(definition, &declarator_name);
        for (uint32_t i = 0; i < mine_parameters.size(); i++) {
            SyntaxRef a = mine_parameters[i], b = class_parameters[i];
            if (!a.is(syntax_kind::type_parameter) ||
                !b.is(syntax_kind::type_parameter))
                return not_supported(n, "temp.mem");
            /* (class U, or an unnamed class: no name) */
            uint32_t names[2] = {0, 0};
            SyntaxRef both[2] = {a, b};
            for (int k = 0; k < 2; k++) {
                token_index f = both[k].first_token();
                if (both[k].end_token() - f == 1)
                    continue;
                if (tokens[f + 1].kind != token_kind::identifier)
                    return not_supported(n, "temp.mem");
                names[k] = tokens.identifier(f + 1);
            }
            uint32_t a_name = names[0], b_name = names[1];
            if (a_name == b_name)
                continue;
            for (token_index t = declarator_name + 1;
                 t < definition.end_token(); t++)
                if (tokens[t].kind == token_kind::identifier &&
                    (tokens.identifier(t) == a_name ||
                     tokens.identifier(t) == b_name))
                    return not_supported(n, "basic.lookup.qual");
        }
    }
    template_member_definition record;
    record.first_parameter = first;
    record.declaration = n;
    record.definition = definition;
    record.inner = inner;
    record.template_entity = function_templates[ct - 1].entity;
    record.pattern = pattern;
    record.member_class =
        !variable && inner && !out_of_class_member(definition);
    return record_template_member(record);
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
    bool member_variable_template =
        o.inner && o.definition.is(syntax_kind::simple_declaration) &&
        out_of_class_variable_member(o.definition);
    if (o.member_class)
        return define_member_of_member_class(member, index, recorded);
    if (o.inner && !member_variable_template)
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
    if (o.outer_record) {
        /* (the qualifier's outer class template's parameters, as bound for
           its specialization) */
        const template_member_definition outer =
            template_member_definitions[o.outer_record - 1];
        const instantiation_record ox =
            instantiations[o.outer_instantiation - 1];
        function_template oof =
            function_templates[entity_table[ox.template_entity].instantiation -
                               1];
        uint32_t ofirst = ox.first_argument;
        if (ox.pattern) {
            oof = function_templates[ox.pattern - 1];
            ofirst = ox.first_deduced;
        }
        for (uint32_t i = 0; i < oof.parameter_count; i++)
            bind_template_argument(
                scope, template_parameter_entities[outer.first_parameter + i],
                template_arguments[ofirst + i]);
    }
    instantiation_context context;
    enter_instantiation(context, scope, o.declaration);
    template_member_scope = scope;
    declaration_access = 0;
    instantiating = 0;
    class_instantiating = 0;
    analysis_result r = analysis_result::well_formed;
    if (member_variable_template) {
        /* [temp.static]/1: the member variable template of this
           specialization takes the definition (variable_template_definition) */
        r = variable_template_definition(o.inner, o.definition);
        leave_instantiation(context);
        if (r == analysis_result::well_formed)
            return r;
        return member_definition_unsupported(o.declaration, recorded);
    }
    if (variable)
        r = simple_declaration(o.definition);
    else {
        /* [temp.inst]/4: a member nobody uses is not instantiated: what
           this definition's body needs waits for its function's use */
        defer_next_body_needs = true;
        r = function_definition(o.definition);
        defer_next_body_needs = false;
    }
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
        /* [class.mfct]/1: a member of a member class Y of the class
           template, X<T>::Y::f: the class is Y of the specialization */
        entity_index owner = x.specialization;
        SyntaxRef qualifier = out_of_class_nested(o.definition);
        if (qualifier && qualifier.size() == 4 && !o.inner &&
            qualifier[2].is_identifier()) {
            owner = entity_table.find(x.specialization,
                                      tokens.identifier(qualifier[2].token()));
            if (!owner || entity_table[owner].kind != entity_kind::class_type ||
                !entity_table[owner].defined)
                return member_definition_unsupported(o.declaration, recorded);
        }
        defined = defined_special_member(owner, o, name);
        if (!defined) {
            /* (the overload this definition is the member's) */
            for (entity_index f =
                     entity_table.find(owner, tokens.identifier(name));
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

/*
 * [temp.mem]/1: template<class T> template<class U> R X<T>::Y<U>::f(P) {...}
 * for the specialization of X that instantiations[index] made: the
 * definition is a member of the member class template Y of that
 * specialization, recorded for Y's specializations as a member of a class
 * template is (member_definition_of_template), with the qualifier's
 * parameters of X bound to the specialization's arguments.  The second
 * template-head is Y's own, and the qualifier's Y<U> its parameters.
 */
analysis_result
semantic_analysis::define_member_of_member_class(uint32_t member,
                                                 uint32_t index, bool recorded)
{
    const template_member_definition o = template_member_definitions[member];
    entity_index c = instantiations[index].specialization;
    SyntaxRef nested = out_of_class_nested(o.definition);
    if (!c || !nested || nested.size() != 4 ||
        !nested[2].is(syntax_kind::simple_template_id) ||
        head_constraint(o.inner) || trailing_constraint(o.inner))
        return member_definition_unsupported(o.declaration, recorded);
    /* [basic.lookup.qual]/1: Y, looked up in the specialization of X the
       qualifier's X<T> names for these arguments */
    SyntaxRef y_id = nested[2];
    lookup_result found;
    entity_index held = 0;
    if (!template_id_name(y_id, found, held, c))
        return member_definition_unsupported(o.declaration, recorded);
    uint32_t yt = class_template_of(held);
    if (!yt || function_templates[yt - 1].name != held ||
        function_templates[yt - 1].primary ||
        !function_templates[yt - 1].is_class ||
        !function_templates[yt - 1].definition ||
        entity_table[held].enclosing != c)
        return member_definition_unsupported(o.declaration, recorded);
    const function_template d = function_templates[yt - 1];
    owned_array<SyntaxRef> parameters;
    instantiation_context context;
    enter_instantiation(context, entity_table[c].enclosing, o.declaration);
    uint32_t number = 0, first = 0;
    entity_index scope = 0;
    analysis_result r =
        template_head_parameters(o.inner, parameters, true, true, true, true);
    current_scope = c;
    if (r == analysis_result::well_formed)
        r = template_parameter_scope(o.inner, parameters, c, 0, number, scope,
                                     first);
    /* [temp.mem]/1: Y<U>'s arguments, analysed in the scope of the second
       template-head, are its own parameters in order ([temp.class]/3, as
       member_definition_of_template's) */
    owned_array<template_argument> arguments;
    if (r == analysis_result::well_formed) {
        partial_arguments++;
        declaring_specializations++;
        r = template_id_arguments_in(y_id, yt, scope, arguments);
        declaring_specializations--;
        partial_arguments--;
    }
    leave_instantiation(context);
    if (r != analysis_result::well_formed ||
        d.parameter_count != parameters.size() ||
        arguments.size() != parameters.size() ||
        !same_template_parameters(first, d.first_parameter, d.parameter_count))
        return member_definition_unsupported(o.declaration, recorded);
    for (uint32_t i = 0; i < parameters.size(); i++)
        if (!argument_is_parameter(arguments[i],
                                   template_parameter_entities[first + i]))
            return member_definition_unsupported(o.declaration, recorded);
    token_index declarator_name = 0;
    out_of_class_nested(o.definition, &declarator_name);
    if (parameter_names_after_declarator(o.definition, declarator_name + 1,
                                         first, d.first_parameter,
                                         d.parameter_count))
        return not_supported(o.declaration, "basic.lookup.qual");
    template_member_definition record;
    record.first_parameter = first;
    record.declaration = o.declaration;
    record.definition = o.definition;
    record.template_entity = d.entity;
    record.outer_record = member + 1;
    record.outer_instantiation = index + 1;
    return record_template_member(record);
}

/*
 * [basic.lookup.qual]/3: "names following the qualified-id being declared
 * are looked up in the scope of the member's class", which finds the
 * class's template-parameter names (template_parameter_entities[theirs
 * ..]), not this definition's (mine ..), where the two differ: whether the
 * definition writes either name from token after on (such a definition is
 * not analysed)
 */
bool semantic_analysis::parameter_names_after_declarator(SyntaxRef definition,
                                                         token_index after,
                                                         uint32_t mine,
                                                         uint32_t theirs,
                                                         uint32_t count) const
{
    for (uint32_t i = 0; i < count; i++) {
        uint32_t mine_name =
            entity_table[template_parameter_entities[mine + i]].name;
        uint32_t class_name =
            entity_table[template_parameter_entities[theirs + i]].name;
        if (mine_name == class_name)
            continue;
        for (token_index t = after; t < definition.end_token(); t++)
            if (tokens[t].kind == token_kind::identifier &&
                (tokens.identifier(t) == mine_name ||
                 tokens.identifier(t) == class_name))
                return true;
    }
    return false;
}

/* a member definition of a class template recorded, and defined for the
   specializations of the class (of its pattern) defined before it */
analysis_result
semantic_analysis::record_template_member(const template_member_definition &m)
{
    uint32_t member = static_cast<uint32_t>(template_member_definitions.size());
    template_member_definitions.append(m);
    analysis_result result = analysis_result::well_formed;
    for (uint32_t k = 0; k < instantiations.size(); k++) {
        const instantiation_record &y = instantiations[k];
        if (y.template_entity != m.template_entity || y.pattern != m.pattern ||
            !y.specialization || y.defining ||
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
    if (o.definition.is(syntax_kind::simple_declaration))
        return define_member_class_template_of(
            instantiations[index].specialization, o.declaration, o.inner,
            o.definition, recorded);
    return define_member_template_of(instantiations[index].specialization,
                                     o.declaration, o.inner, o.definition,
                                     recorded, false, member + 1, index + 1);
}

/*
 * [temp.mem]/1: the member class template of a class template, defined
 * outside it (template<class T> template<class U> struct S<T>::X {...};),
 * for the specialization c: the member class template of c that declares X
 * takes the definition, as a class template declared before does
 * (class_template_definition); X<B> is then instantiated as any class
 * template's specialization (instantiate_class).
 */
analysis_result semantic_analysis::define_member_class_template_of(
    entity_index c, SyntaxRef declaration, SyntaxRef inner,
    SyntaxRef definition, bool recorded)
{
    owned_array<SyntaxRef> parameters;
    instantiation_context context;
    enter_instantiation(context, entity_table[c].enclosing, declaration);
    SyntaxRef only = sole_decl_specifier(definition);
    token_index name_token = 0;
    analysis_result r = analysis_result::well_formed;
    if (!only || !out_of_class_class_member(only, &name_token, true) ||
        template_head_parameters(inner, parameters, true, true, true, true) !=
            analysis_result::well_formed ||
        head_constraint(inner) || trailing_constraint(inner)) {
        leave_instantiation(context);
        return member_definition_unsupported(declaration, recorded);
    }
    uint32_t name = tokens.identifier(name_token);
    current_scope = c;
    /* [temp.class.spec]/5: a partial specialization of the member class
       template, X<U*>: declared in the specialization as one written in
       its class is */
    if (only.class_specifier().class_head()[1][1].is(
            syntax_kind::simple_template_id)) {
        entity_index saved_class = defining_class;
        defining_class = 0;
        member_partial_class = c;
        r = class_specialization_definition(inner, only, parameters);
        member_partial_class = 0;
        defining_class = saved_class;
        leave_instantiation(context);
        if (r != analysis_result::well_formed)
            return member_definition_unsupported(declaration, recorded);
        return r;
    }
    uint32_t number = 0, first = 0;
    entity_index scope = 0;
    r = template_parameter_scope(inner, parameters, c, name, number, scope,
                                 first);
    leave_instantiation(context);
    entity_index held = entity_table.find(c, name);
    uint32_t declared = class_template_of(held);
    if (r != analysis_result::well_formed || !held || !declared ||
        function_templates[declared - 1].name != held ||
        function_templates[declared - 1].primary ||
        function_templates[declared - 1].definition ||
        !function_templates[declared - 1].is_class)
        return member_definition_unsupported(declaration, recorded);
    const function_template d = function_templates[declared - 1];
    /* [temp.over.link]/6: the same template-head */
    if (entity_table[d.entity].enclosing != c ||
        d.parameter_count != parameters.size() ||
        !same_template_parameters(first, d.first_parameter,
                                  d.parameter_count) ||
        !same_constraints(d.declaration, inner))
        return member_definition_unsupported(declaration, recorded);
    for (uint32_t i = 0; i < d.parameter_count; i++)
        if (entity_table[template_parameter_entities[first + i]].kind !=
            entity_kind::template_entity)
            entity_table[template_parameter_entities[first + i]].instantiation =
                declared;
    function_template &record = function_templates[declared - 1];
    record.declaration = inner;
    record.definition = only;
    record.first_parameter = first;
    entity_table[d.entity].defined = true;
    entity_table.add_declaration(d.entity, inner);
    return analysis_result::well_formed;
}

/* the member template of class c that the template-declaration
   (declaration, whose template-head is inner) defines */
analysis_result semantic_analysis::define_member_template_of(
    entity_index c, SyntaxRef declaration, SyntaxRef inner,
    SyntaxRef definition, bool recorded, bool allow_redefine,
    uint32_t outer_member, uint32_t outer_instantiation)
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
    /* [temp.local]/1, [basic.scope.temp]/1: the outer template-head's own
       parameter names, where they differ from the class's, name the
       specialization's arguments in the definition: bound in the member
       template's parameter scope, whose parent stays the class */
    if (r == analysis_result::well_formed && outer_member) {
        const template_member_definition od =
            template_member_definitions[outer_member - 1];
        const instantiation_record ox = instantiations[outer_instantiation - 1];
        function_template oof =
            function_templates[entity_table[ox.template_entity].instantiation -
                               1];
        uint32_t ofirst = ox.first_argument;
        if (ox.pattern) {
            oof = function_templates[ox.pattern - 1];
            ofirst = ox.first_deduced;
        }
        for (uint32_t i = 0; od.first_parameter && i < oof.parameter_count;
             i++) {
            entity_index pe =
                template_parameter_entities[od.first_parameter + i];
            if (!entity_table.find(scope, entity_table[pe].name))
                bind_template_argument(scope, pe,
                                       template_arguments[ofirst + i]);
        }
    }
    if (r == analysis_result::well_formed)
        r = function_template_declarator(definition, lead, c, scope, s, m,
                                         type);
    uint32_t cv = function_cv;
    bool exceptions_specified = m.exceptions.written;
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
        /* the outer template-head's renamed parameters, bound again in
           each specialization's scope (record_instantiation) */
        if (outer_member) {
            const template_member_definition od =
                template_member_definitions[outer_member - 1];
            const instantiation_record ox =
                instantiations[outer_instantiation - 1];
            const function_template oof = function_templates
                [entity_table[ox.template_entity].instantiation - 1];
            bool differs = false;
            for (uint32_t i = 0; od.first_parameter && i < oof.parameter_count;
                 i++)
                if (entity_table
                        [template_parameter_entities[od.first_parameter + i]]
                            .name !=
                    entity_table
                        [template_parameter_entities[oof.first_parameter + i]]
                            .name)
                    differs = true;
            if (differs) {
                record.outer_first_parameter = od.first_parameter;
                record.outer_instantiation = outer_instantiation;
            }
        }
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
