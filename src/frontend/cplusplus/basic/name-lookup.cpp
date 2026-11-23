/*
 * name-lookup.cpp: [basic.lookup] Name lookup.
 *
 * [basic.lookup]/1: "Name lookup associates the use of a name with a set of
 * declarations of that name.  ... The name lookup rules apply uniformly to
 * all names (including typedef-names, namespace-names, and class-names)
 * wherever the grammar allows such names in the context discussed by a
 * particular rule."
 *
 * A scope is the entity whose names are bound in it: a namespace (0 is the
 * global namespace), a class, an enumeration, a block or function parameter
 * scope.  A function's parameter scope is enclosed by the function, which
 * is enclosed by its class or namespace, so a member function defined
 * outside its class searches the class ([basic.lookup.unqual]/8) and a
 * namespace member defined outside its namespace the namespace (/6) by
 * following entity::enclosing.
 *
 * An overload set is not a result yet ([over]): a name that finds functions
 * of more than one namespace is unknown, and so is one a declaration that
 * was not analysed may have declared (hold_names).
 */
#include "../cplusplus-internal.h"
#include "frontend/cplusplus/sema.h"

namespace cplusplus
{

bool semantic_analysis::is_namespace(entity_index s) const
{
    return !s || entity_table[s].kind == entity_kind::namespace_entity;
}

entity_index semantic_analysis::enclosing_namespace(entity_index s) const
{
    while (!is_namespace(s))
        s = entity_table[s].enclosing;
    return s;
}

bool semantic_analysis::namespace_encloses(entity_index n,
                                           entity_index inner) const
{
    for (;; inner = entity_table[inner].enclosing) {
        if (inner == n)
            return true;
        if (!inner)
            return false;
    }
}

/* [namespace.udir]/2: "During unqualified name lookup, the names appear as
   if they were declared in the nearest enclosing namespace which contains
   both the using-directive and the nominated namespace." */
entity_index semantic_analysis::common_namespace(entity_index a,
                                                 entity_index b) const
{
    while (!namespace_encloses(a, b))
        a = entity_table[a].enclosing;
    return a;
}

/* [basic.link]/4: "An unnamed namespace or a namespace declared directly
   or indirectly within an unnamed namespace has internal linkage." */
bool semantic_analysis::within_unnamed_namespace(entity_index n) const
{
    for (; n; n = entity_table[n].enclosing)
        if (entity_table[n].kind == entity_kind::namespace_entity &&
            !entity_table[n].name)
            return true;
    return false;
}

/*
 * The declaration of the name in the scope itself, as the filter sees it.
 * [basic.scope.hiding]/2: "If a class name or enumeration name and a variable,
 * data member, function, or enumerator are declared in the same declarative
 * region (in any order) with the same name ..., the class or enumeration name
 * is hidden wherever the variable, data member, function, or enumerator name is
 * visible."  The name is bound to the one that hides, which keeps the hidden
 * class or enumeration (entity::hidden_type) for the lookups that ignore
 * non-type names.
 */
entity_index semantic_analysis::declared_in(entity_index scope, uint32_t name,
                                            lookup_filter filter) const
{
    entity_index e = entity_table.find(scope, name);
    /* [temp.variadic]/7: in a pack expansion's pattern analysed for one
       element, a pack's name is that element (variadic-template.cpp) */
    if (e && expanding && entity_table[e].kind == entity_kind::pack &&
        expansion_element < entity_table[e].member_count)
        e = pack_elements[entity_table[e].value + expansion_element];
    if (!e || filter == lookup_filter::any ||
        entity_table[e].kind == entity_kind::none)
        return e;
    entity_kind kind = entity_table[e].kind;
    bool is_type = kind == entity_kind::class_type ||
                   kind == entity_kind::enumeration ||
                   kind == entity_kind::typedef_name;
    bool is_namespace_name = kind == entity_kind::namespace_entity;
    switch (filter) {
    case lookup_filter::types:
        if (is_type)
            return e;
        return entity_table[e].hidden_type;
    case lookup_filter::scopes:
        if (is_type || is_namespace_name)
            return e;
        return entity_table[e].hidden_type;
    case lookup_filter::namespaces:
        if (is_namespace_name)
            return e;
        return 0;
    default:
        return e;
    }
}

const char *
semantic_analysis::unknown_name_subclause(const lookup_result &r,
                                          const char *otherwise) const
{
    if (!r.unknown_entity && r.unknown_because)
        return r.unknown_because;
    return unknown_name_subclause(r.unknown_entity, otherwise);
}

const char *
semantic_analysis::unknown_name_subclause(entity_index e,
                                          const char *otherwise) const
{
    /* a name a refused declaration held: that declaration's reason (a
       use of it must not hide it), else, for a template's name, [temp.names]/1: "A
       template specialization ... can be referred to by a template-id" */
    if (e && entity_table[e].held_because)
        return entity_table[e].held_because;
    if (e && entity_table[e].held_by_template)
        return "temp.names";
    return otherwise;
}

void semantic_analysis::note_held(entity_index e)
{
    held_pending.append(e);
    /* a local declaration refused inside a body: the names after it need
       the reason now, not when the whole declaration ends */
    if (!entity_table[e].held_because &&
        unsupported.size() > declaration_refusals)
        entity_table[e].held_because =
            unsupported[declaration_refusals].subclause;
}

/* scope's names are not all known from now on, for the reason the
   declaration being analysed is refused for */
void semantic_analysis::note_unknown_scope(entity_index scope)
{
    entity_table[scope].unknown_names = true;
    unknown_scopes_pending.append(scope);
    if (!entity_table[scope].unknown_because &&
        unsupported.size() > declaration_refusals)
        entity_table[scope].unknown_because =
            unsupported[declaration_refusals].subclause;
}

/* a name that the member function templates, or the constructor
   templates, of scope hold: no entity of its own kind ([temp.over]/1),
   noted for the declaration analysed */
entity_index semantic_analysis::make_held_name(uint32_t name,
                                               entity_index scope)
{
    entity_index held = entity_table.make(entity_kind::none, name, scope);
    entity_table[held].held_by_template = true;
    note_held(held);
    return held;
}

/* a name that is not known, and the subclause whose rules would tell
   (the first reason stays) */
void semantic_analysis::mark_unknown(lookup_result &r, const char *because,
                                     held_cause by) const
{
    r.unknown = true;
    if (!r.unknown_because) {
        r.unknown_because = because;
        r.unknown_by = by;
    }
}

/* why a scope's names are not all known: the refusal of the declaration
   that made them so, else the using-directive's */
const char *semantic_analysis::unknown_scope_subclause(entity_index s) const
{
    if (entity_table[s].unknown_because)
        return entity_table[s].unknown_because;
    return "namespace.udir";
}

void semantic_analysis::found_also(lookup_result &r, entity_index e,
                                   entity_index in) const
{
    if (entity_table[e].kind == entity_kind::none) {
        r.unknown = true;
        if (!r.unknown_entity) {
            r.unknown_entity = e;
            r.unknown_by = entity_table[e].held_by;
        }
        return;
    }
    if (!r.entity) {
        r.entity = e;
        r.found_in = in;
        return;
    }
    if (r.entity == e)
        return;
    /* names of one type: [dcl.typedef]/1 "a typedef-name is thus a synonym for
       another type.  A typedef-name does not introduce a new type the way a
       class declaration or enum declaration does", so a class and typedef-names
       for it found in different namespaces do not make the name ambiguous
       (clang; CWG issue 407).  The class or enumeration itself is the result
       when it is among them */
    {
        auto names_type = [&](entity_index x) {
            entity_kind k = entity_table[x].kind;
            return k == entity_kind::typedef_name ||
                   k == entity_kind::class_type ||
                   k == entity_kind::enumeration;
        };
        if (names_type(r.entity) && names_type(e) &&
            entity_table[r.entity].type == entity_table[e].type) {
            if (entity_table[r.entity].kind == entity_kind::typedef_name &&
                entity_table[e].kind != entity_kind::typedef_name) {
                r.entity = e;
                r.found_in = in;
            }
            return;
        }
    }
    /* [namespace.udir]/6 (a note): "If name lookup finds a declaration for a
       name in two different namespaces, and the declarations do not declare the
       same entity and do not declare functions or function templates, the use
       of the name is ill-formed".  Functions of two namespaces are an overload
       set */
    if (entity_table[r.entity].kind == entity_kind::function &&
        entity_table[e].kind == entity_kind::function)
        mark_unknown(r, "namespace.udir");
    else
        r.ambiguous = true;
}

namespace
{

/* a namespace a using-directive made visible, and the namespace its names
   appear in ([namespace.udir]/2) */
struct visible_namespace {
    entity_index nominated = 0;
    entity_index appears_in = 0;
};

} // namespace

/*
 * [basic.lookup.unqual]/1: "In all the cases listed in
 * [basic.lookup.unqual], the scopes are searched for a declaration in the
 * order listed in each of the respective categories; name lookup ends as
 * soon as a declaration is found for the name.  If no declaration is found,
 * the program is ill-formed."  /2: "The declarations from the namespace
 * nominated by a using-directive become visible in a namespace enclosing
 * the using-directive; see [namespace.udir].  For the purpose of the
 * unqualified name lookup rules described in [basic.lookup.unqual], the
 * declarations from the namespace nominated by the using-directive are
 * considered members of that enclosing namespace."
 *
 * The scopes from the current one out: a block and the blocks around it (/6),
 * the function's parameters, for a member function its class and the class's
 * bases (/8, [class.member.lookup]), the classes around a nested class (/7),
 * then each enclosing namespace up to the global one.  The using-directives of
 * each scope passed take effect in the namespace [namespace.udir]/2 names,
 * which is one of the enclosing namespaces still to be searched;
 * [namespace.udir]/4: "For unqualified lookup, the using-directive is
 * transitive: if a scope contains a using-directive that nominates a second
 * namespace that itself contains using-directives, the effect is as if the
 * using-directives from the second namespace also appeared in the first."
 */
semantic_analysis::lookup_result
semantic_analysis::lookup_unqualified(uint32_t name, lookup_filter filter) const
{
    lookup_result r;
    owned_array<visible_namespace> visible;
    /* [basic.scope.class]: in a member-specification, the names its
       members declared before */
    entity_index first = current_scope;
    if ((is_namespace(first) ||
         (first == template_member_scope &&
          is_namespace(entity_table[first].enclosing))) &&
        defining_class)
        first = defining_class;
    /* (a member class template's specialization, defined in its
       enclosing class's scope: the specialization's first) */
    for (entity_index s = defining_class; s && !is_namespace(first);
         s = entity_table[s].enclosing)
        if (entity_table[s].enclosing == first) {
            first = defining_class;
            break;
        }
    for (entity_index s = first;; s = lexical_parent(s)) {
        if (entity_table[s].has_using_directive) {
            entity_index from = enclosing_namespace(s);
            /* the directives of s, and of each namespace they nominate
               (visible grows as they are added) */
            size_t next = visible.size();
            for (const using_directive_record &u : using_directives)
                if (u.scope == s)
                    visible.append(
                        {u.nominated, common_namespace(from, u.nominated)});
            for (; next < visible.size(); next++) {
                entity_index n = visible[next].nominated;
                if (!entity_table[n].has_using_directive)
                    continue;
                for (const using_directive_record &u : using_directives) {
                    if (u.scope != n)
                        continue;
                    visible_namespace v = {u.nominated,
                                           common_namespace(from, u.nominated)};
                    bool seen = false;
                    for (const visible_namespace &other : visible)
                        if (other.nominated == v.nominated &&
                            other.appears_in == v.appears_in)
                            seen = true;
                    if (!seen)
                        visible.append(v);
                }
            }
        }
        if (is_namespace(s)) {
            entity_index e = declared_in(s, name, filter);
            if (e)
                found_also(r, e, s);
            for (const visible_namespace &v : visible) {
                if (v.appears_in != s)
                    continue;
                e = declared_in(v.nominated, name, filter);
                if (e)
                    found_also(r, e, v.nominated);
                else if (entity_table[v.nominated].unknown_names)
                    mark_unknown(r, unknown_scope_subclause(v.nominated),
                                 entity_table[v.nominated].unknown_by);
            }
        } else if (entity_table[s].kind == entity_kind::class_type) {
            if (filter == lookup_filter::non_members) {
                if (r.unknown || r.ambiguous || !s)
                    break;
                continue;
            }
            /* [class.member.lookup]: a class scope's lookup includes its
               bases'; an ambiguous one finds nothing analysis uses */
            bool ambiguous = false;
            entity_index e = class_member_lookup(s, name, ambiguous);
            bool unknown = false;
            if ((e || ambiguous) && in_dependent_base_only(s, name, unknown)) {
                e = 0;
                ambiguous = false;
                /* (clang's -fms-compatibility finds a name
                   of a dependent base that nothing else declares) */
                if (tokens.files.behavior.microsoft_compatibility)
                    unknown = true;
            }
            if (unknown)
                mark_unknown(r, "temp.dep");
            if (ambiguous)
                mark_unknown(r, "class.member.lookup");
            else if (e) {
                /* the class or base the member is declared in */
                entity_index in = entity_table[e].enclosing;
                if (entity_table.find(in, name) == e)
                    e = declared_in(in, name, filter);
                if (e)
                    found_also(r, e, in);
            }
        } else {
            entity_index e = declared_in(s, name, filter);
            if (e)
                found_also(r, e, s);
        }
        /* a scope with names analysis did not follow: one not found so
           far may be among them */
        if (!r.entity && entity_table[s].unknown_names)
            mark_unknown(r, unknown_scope_subclause(s),
                         entity_table[s].unknown_by);
        if (r.entity || r.unknown || r.ambiguous || !s)
            break;
    }
    /* [temp.res]/10: "If a name does not depend on a template-parameter
       (as defined in [temp.dep]), a declaration (or set of declarations)
       for that name shall be in scope at the point where the name appears
       in the template definition".  An instantiation analyses the
       definition again later: a namespace's declaration after the
       template, which the template's lookup would not find, makes what
       the name denotes not known (a dependent call's lookup at the point
       of instantiation, [temp.dep.candidate], is not told apart) */
    if (lookup_horizon && r.entity && is_namespace(r.found_in))
        for (entity_index o = r.entity; o; o = entity_table[o].next_overload) {
            uint32_t d = entity_table[o].first_declaration;
            if (d &&
                entity_table.declaration(d).node.first_token() > lookup_horizon)
                mark_unknown(r, "temp.dep");
            if (entity_table[o].kind != entity_kind::function)
                break;
        }
    /* a pack named outside a pack expansion's pattern: ill-formed
       ([temp.variadic]/7) unless it is one analysis has no rules for */
    if (r.entity && entity_table[r.entity].kind == entity_kind::pack)
        mark_unknown(r, "temp.variadic");
    if (r.unknown || r.ambiguous)
        r.entity = 0;
    return r;
}

/* [basic.lookup.unqual]/1 for the callers that want one entity or none */
entity_index semantic_analysis::lookup(uint32_t name) const
{
    return lookup_unqualified(name, lookup_filter::any).entity;
}

/*
 * [namespace.qual]/2: "For a namespace X and name m, the namespace-qualified
 * lookup set S(X, m) is defined as follows: Let S'(X, m) be the set of all
 * declarations of m in X and the inline namespace set of X ... If S'(X, m) is
 * not empty, S(X, m) is S'(X, m); otherwise, S(X, m) is the union of S(N_i, m)
 * for all namespaces N_i nominated by using-directives in X and its inline
 * namespace set" (N_i for the standard's N with a subscript i).
 * [namespace.def]/8: "The inline namespace set of N is the transitive closure
 * of all inline namespaces in N."  [namespace.qual]/6: "During the lookup of a
 * qualified namespace member name, if the lookup finds more than one
 * declaration of the member, and if one declaration introduces a class name or
 * enumeration name and the other declarations introduce either the same
 * variable, the same enumerator, or a set of functions, the non-type name hides
 * the class or enumeration name if and only if the declarations are from the
 * same namespace; otherwise (the declarations are from different namespaces),
 * the program is ill-formed."  A namespace is searched once: one reached again
 * through a using-directive adds nothing ([namespace.qual]/2's example).
 */
semantic_analysis::lookup_result
semantic_analysis::lookup_in_namespace(entity_index n, uint32_t name,
                                       lookup_filter filter) const
{
    lookup_result r;
    owned_array<entity_index> searched;
    /* the namespaces whose S(N, m) is still to be added */
    owned_array<entity_index> work;
    work.append(n);
    while (!work.empty()) {
        entity_index x = work.back();
        work.resize(work.size() - 1);
        bool again = false;
        for (entity_index other : searched)
            if (other == x)
                again = true;
        if (again)
            continue;
        /* x and its inline namespace set */
        size_t first = searched.size();
        searched.append(x);
        for (size_t i = first; i < searched.size(); i++) {
            entity_index y = searched[i];
            if (!entity_table[y].has_using_directive)
                continue;
            for (const using_directive_record &u : using_directives) {
                const entity &nominated = entity_table[u.nominated];
                if (u.scope != y || !nominated.inline_ ||
                    nominated.enclosing != y)
                    continue;
                bool seen = false;
                for (entity_index other : searched)
                    if (other == u.nominated)
                        seen = true;
                if (!seen)
                    searched.append(u.nominated);
            }
        }
        bool found = false;
        for (size_t i = first; i < searched.size(); i++) {
            entity_index e = declared_in(searched[i], name, filter);
            if (!e) {
                if (entity_table[searched[i]].unknown_names)
                    mark_unknown(r, unknown_scope_subclause(searched[i]),
                                 entity_table[searched[i]].unknown_by);
                continue;
            }
            found_also(r, e, searched[i]);
            found = true;
        }
        if (found)
            continue;
        for (size_t i = first; i < searched.size(); i++) {
            entity_index y = searched[i];
            if (!entity_table[y].has_using_directive)
                continue;
            for (const using_directive_record &u : using_directives)
                if (u.scope == y)
                    work.append(u.nominated);
        }
    }
    if (r.unknown || r.ambiguous)
        r.entity = 0;
    return r;
}

/*
 * [basic.lookup.qual]/1: "The name of a class or namespace member or
 * enumerator can be referred to after the :: scope resolution operator
 * applied to a nested-name-specifier that denotes its class, namespace, or
 * enumeration."  [class.qual]/1: "If the nested-name-specifier of a
 * qualified-id nominates a class, the name specified after the
 * nested-name-specifier is looked up in the scope of the class
 * ([class.member.lookup])"; [namespace.qual]/1 for a namespace; an
 * enumeration's enumerators are the names of its scope ([dcl.enum]/11).
 */
semantic_analysis::lookup_result
semantic_analysis::lookup_qualified(entity_index scope, uint32_t name,
                                    lookup_filter filter) const
{
    if (is_namespace(scope))
        return lookup_in_namespace(scope, name, filter);
    lookup_result r;
    if (entity_table[scope].kind == entity_kind::class_type) {
        bool ambiguous = false;
        entity_index e = class_member_lookup(scope, name, ambiguous);
        if (ambiguous) {
            mark_unknown(r, "class.member.lookup");
            return r;
        }
        if (!e)
            return r;
        entity_index in = entity_table[e].enclosing;
        if (entity_table.find(in, name) == e)
            e = declared_in(in, name, filter);
        if (e)
            found_also(r, e, in);
    } else {
        entity_index e = declared_in(scope, name, filter);
        if (e)
            found_also(r, e, scope);
    }
    if (r.unknown || r.ambiguous)
        r.entity = 0;
    return r;
}

/*
 * [basic.lookup.argdep]/2: "For each argument type T in the function call,
 * there is a set of zero or more associated namespaces and a set of zero or
 * more associated entities (other than namespaces) to be considered. ...
 * Typedef names and using-declarations used to specify the types do not
 * contribute to this set. The sets of namespaces and entities are determined in
 * the following way: (2.1) If T is a fundamental type, its associated sets of
 * namespaces and entities are both empty. (2.2) If T is a class type (including
 * unions), its associated entities are: the class itself; the class of which it
 * is a member, if any; and its direct and indirect base classes.  Its
 * associated namespaces are the innermost enclosing namespaces of its
 * associated entities. ... (2.3) If T is an enumeration type, its associated
 * namespace is the innermost enclosing namespace of its declaration, and its
 * associated entities are T and, if it is a class member, the member's class.
 * (2.4) If T is a pointer to U or an array of U, its associated namespaces and
 * entities are those associated with U. (2.5) If T is a function type, its
 * associated namespaces and entities are those associated with the function
 * parameter types and those associated with the return type. (2.6) If T is a
 * pointer to a member function of a class X, its associated namespaces and
 * entities are those associated with the function parameter types and return
 * type, together with those associated with X. (2.7) If T is a pointer to a
 * data member of class X, its associated namespaces and entities are those
 * associated with the member type together with those associated with X." A
 * parameter of reference type contributes what the type referred to does.
 */
bool semantic_analysis::associated_with(
    qualified_type t, owned_array<entity_index> &classes,
    owned_array<entity_index> &namespaces) const
{
    auto add_namespace = [&](entity_index scope) {
        entity_index n = enclosing_namespace(scope);
        for (entity_index other : namespaces)
            if (other == n)
                return;
        namespaces.append(n);
    };
    const struct type &record = type_table[t.unqualified()];
    switch (record.kind) {
    case type_kind::class_type: {
        entity_index c = record.entity;
        if (entity_table[c].kind != entity_kind::class_type)
            return false;
        /* the class and its direct and indirect base classes, each once */
        owned_array<entity_index> work;
        work.append(c);
        while (!work.empty()) {
            entity_index x = work.back();
            work.resize(work.size() - 1);
            bool seen = false;
            for (entity_index other : classes)
                if (other == x)
                    seen = true;
            if (seen)
                continue;
            if (entity_table[x].kind != entity_kind::class_type)
                return false;
            classes.append(x);
            add_namespace(entity_table[x].enclosing);
            const entity &cx = entity_table[x];
            for (uint32_t i = 0; i < cx.base_count; i++)
                work.append(
                    type_table[entity_table[class_bases[cx.first_base + i]]
                                   .type.unqualified()]
                        .entity);
        }
        /* "the class of which it is a member, if any" */
        entity_index outer = entity_table[c].enclosing;
        if (outer && entity_table[outer].kind == entity_kind::class_type) {
            bool seen = false;
            for (entity_index other : classes)
                if (other == outer)
                    seen = true;
            if (!seen)
                classes.append(outer);
            add_namespace(entity_table[outer].enclosing);
        }
        return true;
    }
    case type_kind::enumeration: {
        entity_index e = record.entity;
        if (entity_table[e].kind != entity_kind::enumeration)
            return false;
        add_namespace(entity_table[e].enclosing);
        return true;
    }
    case type_kind::pointer:
    case type_kind::array:
    case type_kind::lvalue_reference:
    case type_kind::rvalue_reference:
    case type_kind::block_pointer:
        return associated_with(record.operand, classes, namespaces);
    case type_kind::function: {
        const qualified_type *parameters =
            type_table.parameters(t.unqualified());
        for (uint32_t i = 0; i < record.parameter_count; i++)
            if (!associated_with(parameters[i], classes, namespaces))
                return false;
        return associated_with(record.operand, classes, namespaces);
    }
    case type_kind::pointer_to_member:
        if (!associated_with(entity_table[record.entity].type, classes,
                             namespaces))
            return false;
        return associated_with(record.operand, classes, namespaces);
    default:
        return true;
    }
}

/* [basic.lookup.argdep]/2's associated entities and namespaces of the
   arguments' types: "If an associated namespace is an inline namespace,
   its enclosing namespace is also included in the set. If an associated
   namespace directly contains inline namespaces, those inline namespaces
   are also included in the set." */
bool semantic_analysis::associated_namespaces(
    const owned_array<qualified_type> &arguments,
    owned_array<entity_index> &classes,
    owned_array<entity_index> &namespaces) const
{
    for (qualified_type t : arguments)
        if (!associated_with(t, classes, namespaces))
            return false;
    /* the namespaces grow as inline namespaces and their enclosing ones
       are added */
    for (size_t i = 0; i < namespaces.size(); i++) {
        entity_index n = namespaces[i];
        auto add = [&](entity_index x) {
            for (entity_index other : namespaces)
                if (other == x)
                    return;
            namespaces.append(x);
        };
        if (n && entity_table[n].inline_)
            add(entity_table[n].enclosing);
        if (!entity_table[n].has_using_directive)
            continue;
        for (const using_directive_record &u : using_directives)
            if (u.scope == n && entity_table[u.nominated].inline_ &&
                entity_table[u.nominated].enclosing == n)
                add(u.nominated);
    }
    return true;
}

/*
 * [basic.lookup.argdep]/1: "When the postfix-expression in a function call is
 * an unqualified-id, other namespaces not considered during the usual
 * unqualified lookup may be searched, and in those namespaces, namespace-scope
 * friend function or function template declarations not otherwise visible may
 * be found."  /2: "If an associated namespace is an inline namespace, its
 * enclosing namespace is also included in the set. If an associated namespace
 * directly contains inline namespaces, those inline namespaces are also
 * included in the set."  /4: "When considering an associated namespace N, the
 * lookup is the same as the lookup performed when N is used as a qualifier
 * ([namespace.qual]) except that: (4.1) Any using-directives in N are ignored.
 * (4.2) All names except those of (possibly overloaded) functions and function
 * templates are ignored.  (4.3) Any namespace-scope friend functions or friend
 * function templates declared in classes with reachable definitions in the set
 * of associated entities are visible within their respective namespaces even if
 * they are not visible during an ordinary lookup."
 */
bool semantic_analysis::argument_dependent_lookup(
    uint32_t name, const owned_array<qualified_type> &arguments,
    owned_array<entity_index> &found, entity_index known,
    owned_array<entity_index> *templates) const
{
    owned_array<entity_index> classes, namespaces;
    if (!associated_namespaces(arguments, classes, namespaces))
        return false;
    for (entity_index n : namespaces) {
        entity_index e = entity_table.find(n, name);
        if (!e && entity_table[n].unknown_names)
            return false;
        /* (a name analysis holds: the one the caller found already, a
           function template's, is no other) */
        if (!e || e == known)
            continue;
        if (entity_table[e].kind == entity_kind::none) {
            /* [temp.over]/1: a function template's specializations are
               candidates too */
            if (!templates || !function_template_of(e))
                return false;
            bool listed = false;
            for (entity_index other : *templates)
                if (other == e)
                    listed = true;
            if (!listed)
                templates->append(e);
            continue;
        }
        if (entity_table[e].kind != entity_kind::function)
            continue;
        bool seen = false;
        for (entity_index other : found)
            if (other == e)
                seen = true;
        if (!seen)
            found.append(e);
    }
    /* /4.3: the friend functions of the associated classes not declared
       in their namespaces */
    for (const friend_record &f : friends) {
        entity_index e = f.befriended;
        if (!e || entity_table[e].kind != entity_kind::function ||
            entity_table[e].name != name)
            continue;
        bool associated = false, visible = false;
        for (entity_index k : classes)
            if (k == f.granting)
                associated = true;
        for (entity_index n : namespaces)
            if (n == entity_table[e].enclosing)
                visible = true;
        if (!associated || !visible)
            continue;
        for (entity_index o =
                 entity_table.find(entity_table[e].enclosing, name);
             o; o = entity_table[o].next_overload)
            if (o == e)
                visible = false;
        for (entity_index other : found)
            if (other == e)
                visible = false;
        if (visible)
            found.append(e);
    }
    return true;
}

/*
 * [expr.prim.id.qual]/1:
 *
 *     nested-name-specifier:
 *         ::
 *         type-name ::
 *         namespace-name ::
 *         decltype-specifier ::
 *         nested-name-specifier identifier ::
 *         nested-name-specifier template_opt simple-template-id ::
 *
 * [basic.lookup.qual]/1: "If a :: scope resolution operator in a
 * nested-name-specifier is not preceded by a decltype-specifier, lookup of
 * the name preceding that :: considers only namespaces, types, and
 * templates whose specializations are types.  If the name found does not
 * designate a namespace or a class, enumeration, or dependent type, the
 * program is ill-formed."  /3: "A name prefixed by the unary scope operator
 * :: is looked up in global scope".  The first name is looked up as an
 * unqualified name, each later one in the scope the names before it
 * nominate.  A simple-template-id names its class template's
 * specialization; a decltype-specifier has no rules yet.
 */
analysis_result semantic_analysis::nested_name_specifier(NestedNameSpecifier n,
                                                         nominated_scope &scope,
                                                         bool dependent_allowed)
{
    bool qualified = false;
    scope = nominated_scope();
    if (n.global()) {
        scope.global = true;
        qualified = true;
    }
    for (auto part : n.parts()) {
        /* [temp.names]/4: template before a simple-template-id after a
           nested-name-specifier */
        if (part.has_template && !qualified)
            return not_supported(n, "basic.lookup.qual");
        if (scope.dependent) {
            /* [temp.dep.type]/6: "A qualified-id in which the
               nested-name-specifier names a dependent type that is not the
               current instantiation" is a member of an unknown
               specialization, a dependent type (/9): Q::name.  Q::X<A>,
               a member template of it specialized, is one too, known by
               how the nested-name-specifier is written (an instantiation
               analyses it again in the substituted class) */
            if (part.name.is(syntax_kind::simple_template_id)) {
                scope.dependent = dependent_written_type(n);
                break;
            }
            if (!part.name.is_identifier() || !part.scoped)
                return not_supported(n, "temp.res");
            scope.dependent = dependent_member_type(
                scope.dependent, tokens.identifier(part.name.token()));
            continue;
        }
        if (part.name.is(syntax_kind::simple_template_id) && part.scoped) {
            /* [temp.names]/1: a simple-template-id naming a class template
               names the specialization ([temp.spec]/4), a class; its
               members are looked up there ([class.qual]/1) */
            type_index named = 0;
            /* (its class is looked into: defined, even in a
               specialization's head) */
            uint32_t declaring = declaring_specializations;
            declaring_specializations = 0;
            require_complete_class++;
            analysis_result r =
                template_type(part.name, scope.entity, qualified, named);
            require_complete_class--;
            declaring_specializations = declaring;
            if (r != analysis_result::well_formed)
                return r;
            const struct type &st = type_table[named];
            if (st.kind == type_kind::dependent && dependent_allowed) {
                scope.entity = 0;
                scope.global = false;
                scope.dependent = named;
                qualified = true;
                continue;
            }
            if (st.kind != type_kind::class_type) {
                if (st.kind != type_kind::dependent &&
                    st.kind != type_kind::enumeration)
                    return ill_formed(n,
                                      "a nested-name-specifier shall name a "
                                      "class, enumeration, or namespace",
                                      "basic.lookup.qual");
                return not_supported(n, "temp.res");
            }
            const entity &x = entity_table[st.entity];
            if (!x.size && st.entity != defining_class && !x.defined)
                return not_supported(n, "basic.lookup.qual");
            scope.entity = st.entity;
            scope.global = false;
            qualified = true;
            continue;
        }
        if (part.name.is(syntax_kind::decltype_specifier) && part.scoped &&
            !part.has_template) {
            /* [expr.prim.id.qual]/1: decltype-specifier ::, its class or
               enumeration ([basic.lookup.qual]/1: a decltype-specifier is
               not looked up as a name); a reference or other type is
               ill-formed, not analysed */
            qualified_type named;
            VTRY(dr, decltype_type(part.name, named));
            const struct type &dt = type_table[named.unqualified()];
            if (dt.kind == type_kind::dependent && dependent_allowed) {
                scope.entity = 0;
                scope.global = false;
                scope.dependent = named.unqualified();
                qualified = true;
                continue;
            }
            if (dt.kind != type_kind::class_type &&
                dt.kind != type_kind::enumeration)
                return not_supported(n, "basic.lookup.qual");
            complete_instantiation(dt.entity);
            const entity &dx = entity_table[dt.entity];
            if (!dx.size && dt.entity != defining_class && !dx.defined)
                return not_supported(n, "basic.lookup.qual");
            scope.entity = dt.entity;
            scope.global = false;
            qualified = true;
            continue;
        }
        if (!part.name.is_identifier() || !part.scoped) {
            /* [temp.names]: template before a simple-template-id */
            if (part.name.is(syntax_kind::simple_template_id))
                return not_supported(n, "temp.names");
            return not_supported(n, "basic.lookup.qual");
        }
        token_index t = part.name.token();
        uint32_t name = tokens.identifier(t);
        lookup_result r;
        if (qualified)
            r = lookup_qualified(scope.entity, name, lookup_filter::scopes);
        else
            r = lookup_unqualified(name, lookup_filter::scopes);
        if (r.ambiguous)
            return ill_formed_at_token(t,
                                       "a name found in two different "
                                       "namespaces whose declarations do not "
                                       "declare the same entity is ill-formed",
                                       "namespace.udir");
        /* a name that is not found may be one a declaration analysis has
           no rules for declared */
        if (r.unknown || !r.entity)
            return not_supported(n, "basic.lookup.qual");
        entity_index e = r.entity;
        if (entity_table[e].kind == entity_kind::typedef_name) {
            /* [dcl.typedef]/1: "a typedef-name is thus a synonym for another
               type" */
            const struct type &named =
                type_table[entity_table[e].type.unqualified()];
            /* a template-parameter: a dependent type ([temp.dep.type]/9),
               whose members are looked up per specialization */
            if (named.kind == type_kind::template_parameter ||
                named.kind == type_kind::dependent) {
                if (!dependent_allowed)
                    return not_supported(n, "temp.res");
                scope.entity = 0;
                scope.global = false;
                scope.dependent = entity_table[e].type.unqualified();
                qualified = true;
                continue;
            }
            if (named.kind != type_kind::class_type &&
                named.kind != type_kind::enumeration)
                return ill_formed_at_token(
                    t,
                    "the name found before :: shall designate a namespace "
                    "or a class, enumeration, or dependent type",
                    "basic.lookup.qual");
            e = named.entity;
        }
        if (entity_table[e].kind == entity_kind::class_type &&
            !entity_table[e].size && entity_table[e].instantiation &&
            e != defining_class && !entity_table[e].defined) {
            uint32_t k = entity_table[e].instantiation - 1;
            entity_index t_ent = instantiations[k].template_entity;
            uint32_t ct = entity_table[t_ent].instantiation;
            if (ct && !instantiations[k].defining) {
                entity_index done = e;
                VTRY(ir,
                     instantiate_class(
                         n, ct,
                         &template_arguments[instantiations[k].first_argument],
                         instantiations[k].argument_count, done));
            }
        }
        const entity &x = entity_table[e];
        if (x.kind == entity_kind::class_type) {
            /* [basic.lookup.qual]/1 with [class.qual]: the class's members
               are looked up, so it is complete or being defined */
            if (!x.size && e != defining_class && !x.defined)
                return not_supported(n, "basic.lookup.qual");
        } else if (x.kind != entity_kind::namespace_entity &&
                   x.kind != entity_kind::enumeration) {
            return not_supported(n, "basic.lookup.qual");
        }
        if (x.kind != entity_kind::namespace_entity && x.enclosing &&
            entity_table[x.enclosing].kind == entity_kind::class_type &&
            !accessible(e))
            return ill_formed_at_token(t,
                                       "a private or protected member's name "
                                       "can be used only by members and "
                                       "friends of the class in which it is "
                                       "declared",
                                       "class.access");
        scope.entity = e;
        scope.global = false;
        qualified = true;
    }
    if (!qualified)
        return not_supported(n, "basic.lookup.qual");
    return analysis_result::well_formed;
}

} // namespace cplusplus
