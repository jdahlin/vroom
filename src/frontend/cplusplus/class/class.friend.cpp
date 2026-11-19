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
 * functions, members of other classes and templates are not.
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
                                                syntax_child type)
{
    entity_index space = enclosing_namespace(c);
    if (type.is_token()) {
        /* [class.friend]/3 (C++11): friend simple-type-specifier ; */
        if (!tokens.files.behavior.has(language_feature::rvalue_references) ||
            tokens[type.index()].kind != token_kind::identifier)
            return not_supported(n, "class.friend");
        lookup_result found = lookup_unqualified(
            tokens.identifier(type.index()), lookup_filter::types);
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
    if (!syntax_is(tree, type, syntax_kind::elaborated_type_specifier))
        return not_supported(n, "class.friend");
    node_index t = type.index();
    const syntax_node &node = tree[t];
    const syntax_child *parts = tree.children(t);
    standard_keyword key = leading_keyword(t);
    if (!is_class_key(key))
        return not_supported(n, "class.friend");
    if (node.child_count == 3) {
        /* a qualified name: [basic.lookup.elab]/3, a class declared
           before */
        type_index named = 0;
        analysis_result r = elaborated_type_specifier(t, named);
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
    if (node.child_count != 2 || !parts[1].is_token() ||
        tokens[parts[1].index()].kind != token_kind::identifier)
        return not_supported(n, "class.friend");
    uint32_t name = tokens.identifier(parts[1].index());
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
    const syntax_node &node = tree[n];
    const syntax_child *children = tree.children(n);
    bool defines = node.kind == syntax_kind::function_definition;
    if (defines) {
        auto def = syntax(n).function_definition();
        if (!def.plain() || def.size() != 3)
            return not_supported(n, "class.friend");
        if (!def.body().plain())
            return not_supported(n, "dcl.fct.def.general");
    } else if (node.kind != syntax_kind::member_declaration ||
               node.child_count != 3 ||
               !syntax_is_punctuator(tokens, children[2],
                                     preprocessing_op_or_punc::SEMICOLON) ||
               syntax_is(tree, children[1], syntax_kind::member_declarator) ||
               syntax_is(tree, children[1],
                         syntax_kind::member_declarator_list)) {
        return not_supported(n, "class.friend");
    }
    specified s;
    bool outer = friend_allowed;
    friend_allowed = true;
    analysis_result r = decl_specifier_seq(children[0].index(), s, false);
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
    r = declarator(n, children[1], type, m);
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
        if (parameter.default_argument.index())
            return not_supported(n, "dcl.fct.default");
    uint32_t name = tokens.identifier(m.id);
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

/* the function a friend definition n defines, 0 if n is not one */
entity_index semantic_analysis::friend_defined_by(node_index n) const
{
    for (const friend_definition &d : friend_definitions)
        if (d.definition == n)
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
        r = analyse_function_body(definition,
                                  definition.body().compound_statement());
    }
    current_scope = scope;
    return r;
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
    const syntax_node &node = tree[n];
    const syntax_child *children = tree.children(n);
    if (node.kind == syntax_kind::function_definition)
        return friend_function(c, n, bodies);
    if (node.kind != syntax_kind::member_declaration || node.child_count < 2)
        return not_supported(n, "class.friend");
    if (node.child_count == 2) {
        /* friend class-key identifier ; friend type-name ; */
        node_index seq = children[0].index();
        syntax_child type = {};
        unsigned friends_seen = 0, others = 0;
        for (uint32_t i = 0; i < tree[seq].child_count; i++) {
            syntax_child x = tree.children(seq)[i];
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
