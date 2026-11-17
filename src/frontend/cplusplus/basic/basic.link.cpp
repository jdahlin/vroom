/*
 * basic.link.cpp: [basic.link] Program and linkage: the translation unit.
 *
 *     translation-unit:
 *         declaration-seq_opt
 *         global-module-fragment_opt module-declaration declaration-seq_opt private-module-fragment_opt
 *
 * [basic.link]/1: "A translation unit consists of a sequence of
 * declarations."
 */
#include "../cplusplus-internal.h"
#include "frontend/cplusplus/sema.h"

namespace cplusplus
{

node_index parser::translation_unit()
{
    size_t mark = begin_node();
    token_index first = position;
    /* the second alternative: a module unit ([module.unit]), from C++20 */
    if (tokens.files.behavior.has(language_feature::modules) &&
        (at_module_declaration() ||
         (at_phase4_keyword(preprocessing_token_kind::module_keyword) &&
          at(preprocessing_op_or_punc::SEMICOLON, 1))))
        module_unit_rest(!at_module_declaration());
    else
        declaration_seq(false);
    tree.root = finish_node(syntax_kind::translation_unit, mark, first);
    pending.resize(mark);
    return tree.root;
}

/* [basic.type.qualifier]/3: "An array type whose elements are
   cv-qualified is also considered to have the same cv-qualifications as its
   elements." */
uint32_t cv_of(const types &table, qualified_type t)
{
    const type &record = table[t.unqualified()];
    if (record.kind == type_kind::array)
        return cv_of(table, record.operand);
    return t.cv();
}

namespace
{

/* [basic.link]/11: "declarations for an array object can specify array
   types that differ by the presence or absence of a major array bound" */
bool differ_in_major_bound(const types &table, qualified_type a,
                           qualified_type b)
{
    const type &x = table[a.unqualified()];
    const type &y = table[b.unqualified()];
    return x.kind == type_kind::array && y.kind == type_kind::array &&
           x.operand == y.operand && x.bound_known != y.bound_known;
}

bool same_parameters(const types &table, type_index a, type_index b)
{
    const type &x = table[a];
    const type &y = table[b];
    if (x.variadic != y.variadic || x.parameter_count != y.parameter_count)
        return false;
    for (uint32_t i = 0; i < x.parameter_count; i++)
        if (!(table.parameters(a)[i] == table.parameters(b)[i]))
            return false;
    return true;
}

} // namespace

/*
 * A name declared at global namespace scope by declaration, or declared
 * again.  definition is whether the declaration is a function-definition;
 * an object declaration is a definition unless it is extern ([basic.def]/2:
 * "it contains the extern specifier or a linkage-specification and neither
 * an initializer nor a function-body"); a typedef declaration is none.
 */
analysis_result semantic_analysis::declare(node_index declaration,
                                           token_index id, entity_kind kind,
                                           qualified_type type,
                                           const specified &s, bool definition,
                                           bool initialized,
                                           entity_index &declared_entity)
{
    declared_entity = 0;
    uint32_t name = tokens.identifier(id);
    entity_index scope = current_scope;
    entity_index prior = entity_table.find(scope, name);
    if (scope) {
        /* [dcl.inline]/4: "The inline specifier shall not appear on a block
           scope declaration" */
        if (s.inline_)
            return ill_formed_at_token(id,
                                       "the inline specifier shall not "
                                       "appear on a block scope declaration",
                                       "dcl.inline");
        /* a block-scope declaration of a function, or with extern: it
           declares the namespace's entity of the name ([basic.link]/7),
           not analysed yet */
        if (kind == entity_kind::function || s.extern_)
            return not_supported(declaration, "basic.link");
    }
    /* [dcl.array]/8: "if there is a preceding declaration of the entity in
       the same scope in which the bound was specified, an omitted array
       bound is taken to be the same as in that earlier declaration" */
    if (prior && kind == entity_kind::variable &&
        entity_table[prior].kind == kind &&
        type_table[type.unqualified()].kind == type_kind::array &&
        !type_table[type.unqualified()].bound_known &&
        differ_in_major_bound(type_table, entity_table[prior].type, type))
        type = entity_table[prior].type;
    const struct type &record = type_table[type.unqualified()];
    bool is_reference = record.kind == type_kind::lvalue_reference ||
                        record.kind == type_kind::rvalue_reference;
    if (kind == entity_kind::variable) {
        /* [basic.def]/2: a declaration is a definition unless "it
           contains the extern specifier ... and neither an initializer nor
           a function-body" */
        definition = !s.extern_ || initialized;
        /* [dcl.ref]/5: "The declaration of a reference shall contain an
           initializer except when the declaration contains an explicit
           extern specifier" */
        if (is_reference && !s.extern_ && !initialized)
            return ill_formed_at_token(id,
                                       "the declaration of a reference shall "
                                       "contain an initializer except when "
                                       "the declaration contains an explicit "
                                       "extern specifier",
                                       "dcl.ref");
        bool is_void = record.kind == type_kind::fundamental &&
                       record.fundamental == fundamental_type::void_;
        /* [basic.def]/5: "In the definition of an object, the type of that
           object shall not be an incomplete type" */
        if (definition && !initialized &&
            (is_void ||
             (record.kind == type_kind::array && !record.bound_known)))
            return ill_formed_at_token(id,
                                       "in the definition of an object, the "
                                       "type of that object shall not be an "
                                       "incomplete type",
                                       "basic.def");
        /* an extern object of type cv void: clang's error, a rule not found
           yet */
        if (is_void)
            return not_supported(declaration, "basic.types");
        /* [dcl.init]/7: "If a program calls for the default-initialization
           of an object of a const-qualified type T, T shall be a
           const-default-constructible class type or array thereof." */
        if (definition && !initialized && (cv_of(type_table, type) & cv_const))
            return ill_formed_at_token(id,
                                       "the default-initialization of an "
                                       "object of a const-qualified type "
                                       "needs a const-default-constructible "
                                       "class type",
                                       "dcl.init");
    }
    if (kind == entity_kind::typedef_name && definition)
        return not_supported(declaration, "dcl.fct.def.general");
    if (kind != entity_kind::function && s.inline_ &&
        kind != entity_kind::variable)
        return not_supported(declaration, "dcl.inline");
    /* [basic.link]/3: "A name having namespace scope has internal linkage
       if it is the name of a variable, variable template, function, or
       function template that is explicitly declared static; or a
       non-template variable of non-volatile const-qualified type, unless it
       is explicitly declared extern, or it is inline or exported, or it was
       previously declared and the prior declaration did not have internal
       linkage" */
    linkage implied = linkage::external;
    if (kind == entity_kind::typedef_name)
        implied = linkage::none;
    else if (s.static_)
        implied = linkage::internal;
    uint32_t cv = cv_of(type_table, type);
    bool const_object = kind == entity_kind::variable && (cv & cv_const) &&
                        !(cv & cv_volatile) && !s.extern_ && !s.inline_;
    if (scope)
        return declare_local(declaration, id, kind, type, s, declared_entity);
    if (prior) {
        entity &e = entity_table[prior];
        /* declared by a declaration not analysed (hold_names) */
        if (e.kind == entity_kind::none)
            return not_supported(declaration, "basic.scope.declarative");
        /* [basic.scope.declarative]/4: "Given a set of declarations in a
           single declarative region, each of which specifies the same
           unqualified name, they shall all refer to the same entity, or all
           refer to functions and function templates" */
        if (e.kind != kind)
            return ill_formed_at_token(id,
                                       "declarations of the same name in a "
                                       "declarative region shall all refer to "
                                       "the same entity, or all to functions",
                                       "basic.scope.declarative");
        if (kind == entity_kind::typedef_name) {
            /* [dcl.typedef]/6: "In a given scope, a typedef specifier shall
               not be used to redeclare the name of any type declared in
               that scope to refer to a different type." */
            if (!(e.type == type))
                return ill_formed_at_token(id,
                                           "a typedef specifier shall not be "
                                           "used to redeclare the name of a "
                                           "type to refer to a different type",
                                           "dcl.typedef");
        } else if (kind == entity_kind::function) {
            if (!same_parameters(type_table, e.type.unqualified(),
                                 type.unqualified())) {
                /* another function of the name: an overload set, which
                   expressions cannot name yet */
                e.kind = entity_kind::none;
                return not_supported(declaration, "over");
            }
            /* [over.load]/2: "Function declarations that differ only in the
               return type, the exception specification, or both cannot be
               overloaded." */
            if (!(e.type == type))
                return ill_formed_at_token(id,
                                           "function declarations that differ "
                                           "only in the return type cannot be "
                                           "overloaded",
                                           "over.load");
        } else if (!(e.type == type) &&
                   !differ_in_major_bound(type_table, e.type, type)) {
            /* [basic.link]/11: "the types specified by all declarations
               referring to a given variable or function shall be
               identical" */
            return ill_formed_at_token(id,
                                       "the types specified by all "
                                       "declarations referring to a given "
                                       "variable shall be identical",
                                       "basic.link");
        }
        /* [basic.def.odr]/1: "No translation unit shall contain more than
           one definition of any variable, function" */
        if (definition && e.defined)
            return ill_formed_at_token(id,
                                       "no translation unit shall contain "
                                       "more than one definition of any "
                                       "variable or function",
                                       "basic.def.odr");
        /* [dcl.stc]/6: "The linkages implied by successive declarations for
           a given entity shall agree." */
        if (implied == linkage::internal &&
            e.linkage_of_name == linkage::external &&
            tokens.files.behavior.microsoft_extensions) {
            /* Microsoft's extension: the name keeps its external linkage */
            analysis_result r = extension_used(
                diagnostic_id::sema_static_after_non_static, id, nullptr);
            if (r != analysis_result::well_formed)
                return r;
        } else if (implied == linkage::internal &&
                   e.linkage_of_name == linkage::external)
            return ill_formed_at_token(id,
                                       "the linkages implied by successive "
                                       "declarations for a given entity "
                                       "shall agree",
                                       "dcl.stc");
        if (definition)
            e.defined = true;
        /* the bound a later declaration gives an array */
        if (type_table[type.unqualified()].kind == type_kind::array &&
            type_table[type.unqualified()].bound_known)
            e.type = type;
        entity_table.add_declaration(prior, declaration);
        declared_entity = prior;
    } else {
        if (const_object)
            implied = linkage::internal;
        entity_index e = entity_table.make(kind, name, 0);
        entity_table[e].type = type;
        entity_table[e].linkage_of_name = implied;
        entity_table[e].defined = definition;
        entity_table.add_declaration(e, declaration);
        entity_table.bind(0, name, e);
        declared_entity = e;
    }
    /* [basic.def.odr]/11: "A definition of an inline function or variable
       shall be reachable from the end of every definition domain in which
       it is odr-used outside of a discarded statement."  So it is emitted
       where it is used; an entity is inline once a declaration says so */
    if (s.inline_)
        entity_table[declared_entity].inline_ = true;
    declared_name d;
    d.declaration = declaration;
    d.outermost = current_declaration;
    d.entity = declared_entity;
    d.id = id;
    d.kind = kind;
    d.type = type;
    d.claimable = declarations_claimable;
    declared.append(d);
    return analysis_result::well_formed;
}

/*
 * [basic.scope.block]/1: "A name declared in a block is local to that
 * block; it has block scope."  A local variable or typedef-name, its
 * object's rules checked by declare.
 */
analysis_result semantic_analysis::declare_local(
    node_index declaration, token_index id, entity_kind kind,
    qualified_type type, const specified &s, entity_index &declared_entity)
{
    uint32_t name = tokens.identifier(id);
    entity_index scope = current_scope;
    entity_index prior = entity_table.find(scope, name);
    if (prior) {
        const entity &e = entity_table[prior];
        if (e.kind == entity_kind::none)
            return not_supported(declaration, "basic.scope.declarative");
        /* C++26's name-independent declarations (P2169, not in C++20): a
           variable named _ may be declared again */
        owned_buffer spelling;
        tokens.spelling(id, spelling.get());
        if (spelling.size() == 1 && spelling.data()[0] == '_')
            return not_supported(declaration, "basic.scope.declarative");
        /* [dcl.typedef]/6: "In a given non-class scope, a typedef
           specifier can be used to redeclare the name of any type declared
           in that scope to refer to the type to which it already refers." */
        if (e.kind == entity_kind::typedef_name &&
            kind == entity_kind::typedef_name) {
            if (!(e.type == type))
                return ill_formed_at_token(id,
                                           "a typedef specifier shall not be "
                                           "used to redeclare the name of a "
                                           "type to refer to a different type",
                                           "dcl.typedef");
            entity_table.add_declaration(prior, declaration);
            declared_entity = prior;
            return analysis_result::well_formed;
        }
        if (e.kind != kind)
            return ill_formed_at_token(id,
                                       "declarations of the same name in a "
                                       "declarative region shall all refer to "
                                       "the same entity, or all to functions",
                                       "basic.scope.declarative");
        /* [basic.def.odr]/1: "No translation unit shall contain more than
           one definition of any variable" */
        return ill_formed_at_token(id,
                                   "no translation unit shall contain more "
                                   "than one definition of any variable",
                                   "basic.def.odr");
    }
    entity_index conflicts = entity_table[scope].conflicts;
    if (conflicts) {
        entity_index other = entity_table.find(conflicts, name);
        if (other && entity_table[other].kind == entity_kind::none)
            return not_supported(declaration, "basic.scope.block");
        if (other && entity_table[conflicts].parameter_scope)
            /* [basic.scope.param]/1: "A parameter name shall not be
               redeclared in the outermost block of the function
               definition" */
            return ill_formed_at_token(id,
                                       "a parameter name shall not be "
                                       "redeclared in the outermost block of "
                                       "the function definition",
                                       "basic.scope.param");
        if (other)
            /* [basic.scope.block]/3: "Names declared in the init-statement,
               ... and in the condition of if, while, for, and switch
               statements are local to the if, while, for, or switch
               statement (including the controlled statement), and shall not
               be redeclared in a subsequent condition of that statement nor
               in the outermost block (or, for the if statement, any of the
               outermost blocks) of the controlled statement." */
            return ill_formed_at_token(id,
                                       "a name declared in the init-statement "
                                       "or condition of a statement shall not "
                                       "be redeclared in the outermost block "
                                       "of the controlled statement",
                                       "basic.scope.block");
    }
    entity_index e = entity_table.make(kind, name, scope);
    entity_table[e].type = type;
    entity_table[e].linkage_of_name = linkage::none;
    entity_table[e].defined = kind == entity_kind::variable;
    /* [basic.stc.auto]/1: "Variables that belong to a block or parameter
       scope and are not explicitly declared static, thread_local, or
       extern have automatic storage duration." */
    entity_table[e].automatic = kind == entity_kind::variable && !s.static_;
    entity_table.add_declaration(e, declaration);
    entity_table.bind(scope, name, e);
    declared_entity = e;
    declared_name d;
    d.declaration = declaration;
    d.outermost = current_declaration;
    d.entity = declared_entity;
    d.id = id;
    d.kind = kind;
    d.type = type;
    d.claimable = declarations_claimable;
    declared.append(d);
    return analysis_result::well_formed;
}

/* [basic.lookup.unqual]/1: "the scopes are searched for a declaration in
   the order listed in each of the respective categories; name lookup ends
   as soon as a declaration is found for the name" (labels are not
   entities: [basic.funscope]) */
entity_index semantic_analysis::lookup(uint32_t name) const
{
    for (entity_index s = current_scope;; s = entity_table[s].enclosing) {
        entity_index e = entity_table.find(s, name);
        if (e || !s)
            return e;
    }
}

entity_index semantic_analysis::open_scope(entity_index conflicts,
                                           bool parameters)
{
    entity_index s =
        entity_table.make(entity_kind::block_scope, 0, current_scope);
    entity_table[s].conflicts = conflicts;
    entity_table[s].parameter_scope = parameters;
    current_scope = s;
    return s;
}

} // namespace cplusplus
