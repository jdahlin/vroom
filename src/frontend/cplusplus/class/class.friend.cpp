/*
 * class.friend.cpp: [class.friend] friends (doc/PLAN-cxx-sema.md R10).
 *
 * [class.friend]/1: "A friend of a class is a function or class that is
 * given permission to name the private and protected members of the
 * class."  What is analysed: in a class that is not local, a friend class
 * named by class-key identifier, by a qualified elaborated-type-specifier
 * or (C++11, [class.friend]/3) by a type-name; a friend function declared
 * (not defined) by an unqualified identifier, of the innermost enclosing
 * namespace ([namespace.memdef]/3).  Friend definitions, operator
 * functions, members of other classes and templates are not.  A class
 * template's friends are declared per specialization (instantiate_class),
 * each granting only that specialization ([temp.friend]).
 */
#include "../cplusplus-internal.h"
#include "frontend/cplusplus/sema.h"

namespace cplusplus
{

/* whether the friend declaration f names e */
bool semantic_analysis::friend_names(const friend_record &f,
                                     entity_index e) const
{
    if (f.befriended)
        return f.befriended == e;
    const entity &x = entity_table[e];
    return x.kind == entity_kind::class_type && x.enclosing == f.scope &&
           x.name == f.name;
}

/* whether class k declares e its friend */
bool semantic_analysis::grants(entity_index k, entity_index e) const
{
    for (const friend_record &f : friends)
        if (f.granting == k && friend_names(f, e))
            return true;
    return false;
}

/* whether the name used now is in a friend of class k: the function
   analysed, the class whose member-specification is, or a class or
   function around them ([class.access.nest]/1, [class.local]/1), as
   accessible() walks them */
bool semantic_analysis::in_friend_of(entity_index k) const
{
    entity_index starts[3] = {0, defining_class, 0};
    if (function_state.active)
        starts[0] = function_state.function;
    if (!is_namespace(current_scope))
        starts[2] = current_scope;
    for (entity_index start : starts)
        for (entity_index here = start; !is_namespace(here);
             here = entity_table[here].enclosing)
            if (grants(k, here))
                return true;
    return false;
}

entity_index semantic_analysis::declared_unbound(entity_index scope,
                                                 uint32_t name,
                                                 qualified_type type) const
{
    for (entity_index e : block_declared) {
        const entity &x = entity_table[e];
        if (x.enclosing == scope && x.name == name &&
            x.kind == entity_kind::function &&
            same_parameters(type_table, x.type.unqualified(),
                            type.unqualified()))
            return e;
    }
    return 0;
}

/*
 * [class.friend]/2, /3: a friend class.  type is the elaborated-type-specifier,
 * or the type-name ([class.friend]/3: "friend simple-type-specifier ;").
 * [namespace.memdef]/3: "If the name in a friend declaration is neither
 * qualified nor a template-id and the declaration is a function or an
 * elaborated-type-specifier, the lookup to determine whether the entity
 * has been previously declared shall not consider any scopes outside the
 * innermost enclosing namespace."
 */
analysis_result semantic_analysis::friend_class(entity_index c, node_index n,
                                                SyntaxElement type)
{
    entity_index space = enclosing_namespace(c);
    if (type.is_token()) {
        /* [class.friend]/3 (C++11): friend simple-type-specifier ; */
        if (!tokens.files.behavior.has(language_feature::rvalue_references) ||
            tokens[type.token()].kind != token_kind::identifier)
            return not_supported(n, "class.friend");
        lookup_result found = lookup_unqualified(
            tokens.identifier(type.token()), lookup_filter::types);
        if (found.unknown || found.ambiguous || !found.entity)
            return not_supported(n, "class.friend");
        entity_index e = found.entity;
        if (entity_table[e].kind == entity_kind::typedef_name) {
            qualified_type t = entity_table[e].type;
            if (type_table[t.unqualified()].kind != type_kind::class_type)
                return not_supported(n, "class.friend");
            e = type_table[t.unqualified()].entity;
        }
        if (entity_table[e].kind != entity_kind::class_type)
            return not_supported(n, "class.friend");
        friend_record f;
        f.granting = c;
        f.befriended = e;
        friends.append(f);
        return analysis_result::well_formed;
    }
    if (!syntax(type).is(syntax_kind::elaborated_type_specifier))
        return not_supported(n, "class.friend");
    node_index t = type.index();
    auto node = syntax(t);
    standard_keyword key = leading_keyword(t);
    if (!is_class_key(key))
        return not_supported(n, "class.friend");
    if (node.size() == 3) {
        /* a qualified name: [basic.lookup.elab]/3, a class declared
           before */
        type_index named = 0;
        analysis_result r = elaborated_type_specifier(
            syntax(t).elaborated_type_specifier(), named);
        if (r != analysis_result::well_formed)
            return r;
        const struct type &record = type_table[named];
        if (record.kind != type_kind::class_type ||
            entity_table[record.entity].kind != entity_kind::class_type)
            return not_supported(n, "class.friend");
        friend_record f;
        f.granting = c;
        f.befriended = record.entity;
        friends.append(f);
        return analysis_result::well_formed;
    }
    if (node.size() != 2 || !node[1].is_identifier())
        return not_supported(n, "class.friend");
    uint32_t name = tokens.identifier(node[1].token());
    if (entity_table[space].unknown_names)
        return not_supported(n, "class.friend");
    lookup_result found = lookup_unqualified(name, lookup_filter::types);
    if (found.unknown || found.ambiguous)
        return not_supported(n, "class.friend");
    friend_record f;
    f.granting = c;
    f.scope = space;
    f.name = name;
    if (found.entity) {
        const entity &x = entity_table[found.entity];
        bool outside = is_namespace(found.found_in) &&
                       found.found_in != space &&
                       namespace_encloses(found.found_in, space);
        if (x.kind == entity_kind::class_type && x.enclosing == space &&
            found.found_in == space &&
            !entity_table.bound_by_using(space, name)) {
            /* the class declared before (its key is [dcl.type.elab]/3's,
               which is not checked) */
            if (x.union_ != (key == standard_keyword::union_))
                return not_supported(n, "dcl.type.elab");
            f.befriended = found.entity;
        } else if (!outside || tokens.files.behavior.microsoft_extensions) {
            /* a member of a class around, a typedef-name, a name brought
               by a using-declaration or directive */
            return not_supported(n, "class.friend");
        }
    }
    /* [ext.microsoft] an unqualified friend class's name is looked up
       outside the innermost enclosing namespace */
    if (!f.befriended && tokens.files.behavior.microsoft_extensions)
        return not_supported(n, "class.friend");
    friends.append(f);
    return analysis_result::well_formed;
}

/*
 * [class.friend]/6: "No storage-class-specifier shall appear in the
 * decl-specifier-seq of a friend declaration."  [namespace.memdef]/3: a
 * function first declared by the friend declaration is a member of the
 * innermost enclosing namespace, not found by ordinary lookup until
 * declared there (argument-dependent lookup finds it, [basic.lookup.argdep]
 * /4.3).  /6: "A function can be defined in a friend declaration of a class
 * if and only if the class is a non-local class and the function name is
 * unqualified"; /7: "Such a function is implicitly an inline function.  A
 * friend function defined in a class is in the (lexical) scope of the class
 * in which it is defined": its body is analysed with the class's members
 * (friend_function_body), after the class is complete as a member
 * function's (bodies).
 */
analysis_result
semantic_analysis::friend_function(entity_index c, node_index n,
                                   owned_array<node_index> &bodies)
{
    /* [ext:gnu_attributes] before it all: GCC's, which a friend's
       definition has no rules for beyond its form (visibility as the
       class's members') */
    uint32_t leading = leading_gnu_attributes(n);
    auto children_syntax = syntax(n);
    uint32_t count = children_syntax.size() - leading;
    bool defines = children_syntax.is(syntax_kind::function_definition);
    if (defines) {
        auto body = syntax(n).function_definition().function_body();
        if (count != 3 ||
            !children_syntax[leading].is(syntax_kind::decl_specifier_seq) ||
            !body.is(syntax_kind::function_body))
            return not_supported(n, "class.friend");
        if (!body.defaulted() && !body.plain())
            return not_supported(n, "dcl.fct.def.general");
    } else if (!children_syntax.is(syntax_kind::member_declaration) ||
               count != 3 ||
               !children_syntax[leading + 2].is(
                   preprocessing_op_or_punc::SEMICOLON) ||
               children_syntax[leading + 1].is(
                   syntax_kind::member_declarator) ||
               children_syntax[leading + 1].is(
                   syntax_kind::member_declarator_list)) {
        return not_supported(n, "class.friend");
    }
    specified s;
    bool outer = friend_allowed;
    friend_allowed = true;
    analysis_result r =
        decl_specifier_seq(children_syntax[leading].index(), s, false);
    friend_allowed = outer;
    if (r != analysis_result::well_formed)
        return r;
    /* (inline or constexpr only on a definition here) */
    if (!s.friend_ || s.typedef_ || s.static_ || s.extern_ ||
        ((s.inline_ || s.constexpr_) && !defines) || s.virtual_ ||
        s.declares_class || s.declares_enumeration || !s.has_type)
        return not_supported(n, "class.friend");
    qualified_type type = s.type;
    declarator_meaning m;
    function_cv = 0;
    function_parameters.clear();
    r = declarator(n, children_syntax[leading + 1], type, m);
    if (r != analysis_result::well_formed)
        return r;
    /* an operator function only as a definition ([over.oper]: its
       declaration's checks are operator_function_declaration's) */
    if (!m.has_id || m.qualifier || m.qualified_namespace ||
        (m.operator_function &&
         (!defines || m.allocation != allocation_function::none)) ||
        m.literal_operator || m.destructor || m.aligned || m.gnu_flags ||
        function_cv || c_language ||
        type_table[type.unqualified()].kind != type_kind::function)
        return not_supported(n, "class.friend");
    for (const parameter_meaning &parameter : function_parameters)
        if (parameter.default_argument)
            return not_supported(n, "dcl.fct.default");
    uint32_t name = tokens.identifier(m.id);
    if (defines &&
        explicit_body(children_syntax[leading + 2],
                      standard_keyword::default_) &&
        !defaulted_equality_declared(c, name, type))
        return not_supported(n, "class.compare.default");
    entity_index space = enclosing_namespace(c);
    if (entity_table[space].unknown_names ||
        entity_table.bound_by_using(space, name))
        return not_supported(n, "class.friend");
    entity_index befriended = 0;
    if (entity_index prior = entity_table.find(space, name)) {
        if (entity_table[prior].kind != entity_kind::function)
            return not_supported(n, "class.friend");
        for (entity_index o = prior; o && !befriended;
             o = entity_table[o].next_overload)
            if (same_parameters(type_table, entity_table[o].type.unqualified(),
                                type.unqualified()))
                befriended = o;
    }
    if (!befriended)
        befriended = declared_unbound(space, name, type);
    if (befriended) {
        /* [over.load]/2, [dcl.link]: a return type or language linkage of
           its own is not analysed */
        if (!(entity_table[befriended].type == type) ||
            entity_table[befriended].c_language_linkage)
            return not_supported(n, "class.friend");
    } else {
        if (type_without_external_linkage(type) ||
            tokens.files.behavior.microsoft_extensions)
            return not_supported(n, "class.friend");
        befriended = entity_table.make(entity_kind::function, name, space);
        entity &made = entity_table[befriended];
        made.type = type;
        made.implemented = m.implemented;
        made.operator_function = m.operator_function;
        made.linkage_of_name = linkage::external;
        /* [basic.link]/4: within an unnamed namespace, internal */
        if (within_unnamed_namespace(space))
            made.linkage_of_name = linkage::internal;
        block_declared.append(befriended);
    }
    if (defines) {
        /* [basic.def.odr]/1: one definition */
        if (entity_table[befriended].defined)
            return not_supported(n, "basic.def.odr");
        entity_table[befriended].inline_ = true;
        if (s.constexpr_)
            entity_table[befriended].constexpr_ = true;
        if (m.operator_function) {
            analysis_result o = operator_function_declaration(n, befriended);
            if (o != analysis_result::well_formed)
                return o;
        }
        bodies.append(n);
        friend_definitions.append({n, befriended, c});
    }
    entity_table.add_declaration(befriended, n);
    friend_record f;
    f.granting = c;
    f.befriended = befriended;
    friends.append(f);
    return analysis_result::well_formed;
}

entity_index semantic_analysis::lexical_parent(entity_index s) const
{
    if (entity_table[s].kind == entity_kind::function &&
        entity_table[s].inline_)
        for (const friend_definition &d : friend_definitions)
            if (d.function == s)
                return d.granting;
    return entity_table[s].enclosing;
}

bool semantic_analysis::defined_by_friend(entity_index f) const
{
    for (const friend_definition &d : friend_definitions)
        if (d.function == f)
            return true;
    return false;
}

/* the function a friend definition n in class c defines (a class
   template's defines one per specialization, [temp.friend]), 0 if n is
   not one */
entity_index semantic_analysis::friend_defined_by(node_index n,
                                                  entity_index c) const
{
    for (const friend_definition &d : friend_definitions)
        if (d.definition == n && d.granting == c)
            return d.function;
    return 0;
}

/* [class.friend]/7: the body of a friend defined in class c, in the
   class's scope: its declarator again for the parameters, then the body */
analysis_result semantic_analysis::friend_function_body(
    entity_index c, FunctionDefinition definition, entity_index f)
{
    current_declaration = definition;
    entity_index scope = current_scope;
    current_scope = c;
    specified s;
    bool outer = friend_allowed;
    friend_allowed = true;
    analysis_result r =
        decl_specifier_seq(definition.decl_specifier_seq().index(), s, false);
    friend_allowed = outer;
    qualified_type type = s.type;
    declarator_meaning m;
    if (r == analysis_result::well_formed) {
        function_cv = 0;
        function_parameters.clear();
        r = declarator(definition, definition.declarator(), type, m);
    }
    if (r == analysis_result::well_formed) {
        last_declared = f;
        auto body = definition.function_body();
        if (body.defaulted()) {
            scoped_value<entity_index> compared(defaulted_comparison, c);
            r = analyse_function_body(definition, element(body.index()));
        } else {
            r = analyse_function_body(definition, body.compound_statement());
        }
    }
    current_scope = scope;
    return r;
}

/*
 * [class.compare.default]/1: "A defaulted comparison operator function
 * ([over.binary]) for some class C shall be a non-template function that
 * is ... a friend of C having either two parameters of type const C& or
 * two parameters of type C."  [class.eq]/1: "A defaulted equality
 * operator function shall have a declared return type bool."  Only the
 * friend operator== of a class without base classes is analysed.
 */
bool semantic_analysis::defaulted_equality_declared(entity_index c,
                                                    uint32_t name,
                                                    qualified_type type)
{
    const struct type &f = type_table[type.unqualified()];
    if (name != operator_name(preprocessing_op_or_punc::EQUAL_EQUAL) ||
        f.kind != type_kind::function || f.variadic || f.parameter_count != 2 ||
        entity_table[c].base_count ||
        fundamental_of(f.operand) != fundamental_type::bool_)
        return false;
    const qualified_type *parameters =
        type_table.parameters(type.unqualified());
    qualified_type object =
        qualified_type::make(entity_table[c].type.unqualified(), cv_none);
    qualified_type reference = qualified_type::make(
        type_table.lvalue_reference_to(
            qualified_type::make(object.unqualified(), cv_const)),
        cv_none);
    return parameters[0] == parameters[1] &&
           (parameters[0] == object || parameters[0] == reference);
}

/*
 * [class.eq]/3: "The return value V of a defaulted == operator function
 * with parameters x and y is determined by comparing corresponding
 * elements x_i and y_i in the expanded lists of subobjects for x and y
 * (in increasing index order) until the first index i where x_i == y_i
 * yields a result value which, when contextually converted to bool,
 * yields false.  If no such index exists, V is true.  Otherwise, V is
 * false."  The body: return x.m0 == y.m0 && x.m1 == y.m1 ...; for the
 * class's non-static data members, at least one, none an array.
 */
analysis_result semantic_analysis::defaulted_equality(
    token_index at, const owned_array<uint32_t> &parameters, uint32_t &block)
{
    const entity &x = entity_table[defaulted_comparison];
    if (!x.member_count || parameters.size() != 2)
        return not_supported(current_declaration, "class.compare.default");
    expression_meaning all;
    for (uint32_t i = 0; i < x.member_count; i++) {
        entity_index member = class_members[x.first_member + i];
        if (type_table[entity_table[member].type.unqualified()].kind ==
            type_kind::array)
            return not_supported(current_declaration, "class.compare.default");
        expression_meaning sides[2], equal;
        for (int k = 0; k < 2; k++) {
            analysis_result r =
                named_entity(at, parameters[k], sides[k], false);
            if (r == analysis_result::well_formed)
                r = member_access(at, member, sides[k]);
            if (r != analysis_result::well_formed)
                return r;
        }
        analysis_result r = binary_of(
            current_declaration, at, preprocessing_op_or_punc::EQUAL_EQUAL,
            SyntaxElement(), sides[0], SyntaxElement(), sides[1], equal);
        if (r != analysis_result::well_formed)
            return r;
        /* a bool prvalue each ([class.eq]/2), joined by && ([expr.log.and]) */
        if (i) {
            if (all.evaluation == constness::not_constant)
                equal.evaluation = constness::not_constant;
            equal.node = expression_node(semantic_kind::logical_and, equal, at,
                                         all.node, equal.node);
        }
        all = equal;
    }
    owned_array<uint32_t> statements;
    statements.append(
        statement_node(semantic_kind::return_statement, at, all.node));
    block = block_statement(at, statements);
    return analysis_result::well_formed;
}

/* a member-declaration whose decl-specifier-seq has friend */
analysis_result
semantic_analysis::friend_declaration(entity_index c, node_index n,
                                      owned_array<node_index> &bodies)
{
    /* [class.friend]/11: a friend declaration in a local class */
    for (entity_index k = c; !is_namespace(k); k = entity_table[k].enclosing)
        if (entity_table[k].kind != entity_kind::class_type)
            return not_supported(n, "class.friend");
    return syntax(n).member_declaration().visit(Overloaded{
        [&](FunctionDefinition f) { return friend_function(c, f, bodies); },
        [&](MemberSimpleDeclaration d) {
            return friend_member_declaration(c, d, bodies);
        },
        [&](MemberDeclaration) { return not_supported(n, "class.friend"); }});
}

/* a friend member-declaration that is not a function-definition */
analysis_result semantic_analysis::friend_member_declaration(
    entity_index c, MemberSimpleDeclaration d, owned_array<node_index> &bodies)
{
    node_index n = d;
    auto node = syntax(n);
    uint32_t leading = leading_gnu_attributes(n);
    auto children_syntax = syntax(n);
    if (node.size() < leading + 2)
        return not_supported(n, "class.friend");
    if (node.size() == leading + 2) {
        /* friend class-key identifier ; friend type-name ; */
        node_index seq = children_syntax[leading].index();
        SyntaxElement type = {};
        unsigned friends_seen = 0, others = 0;
        for (SyntaxElement x : syntax(seq).elements()) {
            if (is_keyword(x, standard_keyword::friend_)) {
                friends_seen++;
                continue;
            }
            type = x;
            others++;
        }
        if (friends_seen != 1 || others != 1)
            return not_supported(n, "class.friend");
        return friend_class(c, n, type);
    }
    return friend_function(c, n, bodies);
}

} // namespace cplusplus
