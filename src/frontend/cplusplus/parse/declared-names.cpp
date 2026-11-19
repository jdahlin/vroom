/*
 * declared-names.cpp: the names the grammar depends on.
 *
 * The grammar is not context-free: [dcl.type.simple] "type-name",
 * [temp.names] "template-name" and [namespace.def] "namespace-name" are
 * identifiers that were declared as such, so `T * x;` is a declaration or
 * an expression by what T is.  [basic.lookup]/1: "Name lookup associates the
 * use of a name with a set of declarations of that name."  The parser keeps
 * only what decides between productions: per declarative region
 * ([basic.scope.declarative]/1), which names it declares and as which kind.
 *
 * This is a stand-in for [basic.lookup], not an implementation of it: a
 * class's members and its bases' when they are classes it knows, no
 * hiding rules beyond the order of the scopes; of templates their kinds,
 * their parameters' scopes and which names depend on a parameter
 * (doc/PLAN-cxx-parse.md lists what each step adds).
 */
#include "../cplusplus-internal.h"

namespace cplusplus
{

declared_names::declared_names()
{
    /* [basic.scope.namespace]/3: "The outermost declarative region of a
       translation unit is also a namespace, called the global namespace." */
    scope &global = scopes.make();
    global.kind = scope_kind::namespace_;
    global.enclosing = global_namespace;
}

const declared_names::declaration *
declared_names::find(scope_index in, uint32_t name, unsigned depth) const
{
    const scope &s = scopes[in];
    /* the latest declaration first: a later one of another kind is the one
       in effect ([basic.scope.hiding] is not modelled further) */
    for (size_t i = s.declarations.size(); i-- > 0;) {
        const declaration &d = s.declarations[i];
        if (d.name != name)
            continue;
        if (ignoring_non_types && d.kind != name_kind::type_name &&
            d.kind != name_kind::template_name)
            continue;
        return &d;
    }
    /* a using-directive can nominate a namespace that encloses this one:
       the depth stops a cycle */
    if (depth > 16)
        return nullptr;
    for (scope_index other : s.searched_with) {
        const declaration *d = find(other, name, depth + 1);
        if (d)
            return d;
    }
    return nullptr;
}

scope_index declared_names::enter_namespace(uint32_t name, bool inline_)
{
    /* [namespace.def]/2: "If the identifier, when looked up, refers to a
       namespace-name ... that was introduced in the namespace in which the
       named-namespace-definition appears ..., the namespace-definition
       extends the previously-declared namespace." */
    if (name) {
        const scope &here = scopes[current_scope];
        for (size_t i = here.declarations.size(); i-- > 0;)
            if (here.declarations[i].name == name &&
                here.declarations[i].kind == name_kind::namespace_name) {
                current_scope = here.declarations[i].scope;
                return current_scope;
            }
    }
    scope_index enclosing = current_scope;
    scope &s = scopes.make();
    s.kind = scope_kind::namespace_;
    s.enclosing = enclosing;
    scope_index made = static_cast<scope_index>(scopes.size() - 1);
    if (name) {
        scopes[enclosing].declarations.append(
            {name, name_kind::namespace_name, made});
        journal.append(enclosing);
    }
    /* [namespace.unnamed]/1: an unnamed-namespace-definition behaves as if
       followed by `using namespace unique;`; [namespace.def]/7: "Members of
       an inline namespace can be used in most respects as though they were
       members of the enclosing namespace." */
    if (!name || inline_) {
        scopes[enclosing].searched_with.append(made);
        journal.append(enclosing | 0x80000000u);
    }
    current_scope = made;
    return made;
}

scope_index declared_names::enter(scope_kind kind)
{
    scope &s = scopes.make();
    s.kind = kind;
    s.enclosing = current_scope;
    current_scope = static_cast<scope_index>(scopes.size() - 1);
    return current_scope;
}

void declared_names::reenter(scope_index region)
{
    current_scope = region;
}

void declared_names::leave()
{
    current_scope = scopes[current_scope].enclosing;
}

void declared_names::declare(uint32_t name, name_kind kind, scope_index region,
                             bool dependent)
{
    declare_in(declaring_scope(), name, kind, region, dependent);
}

void declared_names::declare_in(scope_index in, uint32_t name, name_kind kind,
                                scope_index region, bool dependent)
{
    /* [temp.names]/2: a name is a template-name when "name lookup finds
       ... an overload set that contains a function template": a function
       declared beside a function template of the same name does not hide
       it */
    if (kind == name_kind::function)
        for (const declaration &d : scopes[in].declarations)
            if (d.name == name && d.kind == name_kind::function_template)
                kind = name_kind::function_template;
    scopes[in].declarations.append({name, kind, region, dependent});
    journal.append(in);
}

void declared_names::declare_template_parameter(uint32_t name, name_kind kind)
{
    /* [temp.param]/3: "A type-parameter whose identifier does not follow an
       ellipsis defines its identifier to be a typedef-name (if declared
       without template) or template-name (if declared with template) in
       the scope of the template declaration"; a type or template parameter
       is a dependent type ([temp.dep.type]/7) */
    bool dependent =
        kind == name_kind::type_name || kind == name_kind::template_name;
    scopes[current_scope].declarations.append({name, kind, 0, dependent});
    journal.append(current_scope);
}

scope_index declared_names::declaring_scope() const
{
    scope_index s = current_scope;
    while (scopes[s].kind == scope_kind::template_parameter)
        s = scopes[s].enclosing;
    return s;
}

bool declared_names::encloses_current(scope_index region) const
{
    for (scope_index s = current_scope;; s = scopes[s].enclosing) {
        if (s == region)
            return true;
        if (s == global_namespace)
            return false;
    }
}

scope_index declared_names::nearest_namespace_or_block() const
{
    scope_index s = current_scope;
    while (scopes[s].kind != scope_kind::namespace_ &&
           scopes[s].kind != scope_kind::block)
        s = scopes[s].enclosing;
    return s;
}

void declared_names::add_base(scope_index derived, scope_index base)
{
    /* a class is not its own base ([class.derived]/2: "shall not be an
       incompletely defined class"): no cycle */
    if (derived == base)
        return;
    scopes[derived].searched_with.append(base);
    journal.append(derived | 0x80000000u);
}

scope_index declared_names::snapshot(scope_index region)
{
    /* not journaled: the copy is reachable only through set_complete, from
       the region whose member-specification is parsed again */
    scope &copy = scopes.make();
    const scope &s = scopes[region];
    copy.kind = s.kind;
    copy.enclosing = s.enclosing;
    for (const declaration &d : s.declarations)
        copy.declarations.append(d);
    for (scope_index other : s.searched_with)
        copy.searched_with.append(other);
    return static_cast<scope_index>(scopes.size() - 1);
}

void declared_names::define_class(scope_index in, uint32_t name,
                                  scope_index region)
{
    const scope &s = scopes[in];
    for (size_t i = s.declarations.size(); i-- > 0;) {
        const declaration &d = s.declarations[i];
        if (d.name != name)
            continue;
        if ((d.kind == name_kind::type_name ||
             d.kind == name_kind::template_name) &&
            d.scope && d.scope != region) {
            scopes[d.scope].searched_with.append(region);
            journal.append(d.scope | 0x80000000u);
        }
        return;
    }
}

void declared_names::set_complete(scope_index region, scope_index complete)
{
    scopes[region].complete = complete;
}

void declared_names::declare_namespace_alias(uint32_t name,
                                             scope_index nominated)
{
    scopes[current_scope].declarations.append(
        {name, name_kind::namespace_name, nominated});
    journal.append(current_scope);
}

void declared_names::use_namespace(scope_index nominated)
{
    scopes[current_scope].searched_with.append(nominated);
    journal.append(current_scope | 0x80000000u);
}

void declared_names::rollback(mark m)
{
    while (journal.size() > m.journal) {
        uint32_t entry = journal.back();
        journal.resize(journal.size() - 1);
        scope &s = scopes[entry & 0x7FFFFFFFu];
        if (entry & 0x80000000u)
            s.searched_with.resize(s.searched_with.size() - 1);
        else
            s.declarations.resize(s.declarations.size() - 1);
    }
    current_scope = m.current;
}

name_kind declared_names::lookup_unqualified(uint32_t name, scope_index *scope,
                                             bool *dependent)
{
    /* [basic.lookup.unqual]/1: "In all the cases listed in
       [basic.lookup.unqual], the scopes are searched for a declaration in
       the order listed in each of the respective categories; name lookup
       ends as soon as a declaration is found for the name." */
    name_kind found = name_kind::not_declared;
    const declaration *d = nullptr;
    bool parameters_searched = false;
    for (scope_index s = current_scope;; s = scopes[s].enclosing) {
        /* [temp.local]/8: in the definition of a member of a class
           template outside the class, the template's parameters are found
           after the class's members and before the namespace's */
        if (!parameters_searched && scopes[s].kind == scope_kind::namespace_) {
            parameters_searched = true;
            for (size_t i = template_parameters_in_effect.size();
                 !d && i-- > 0;)
                d = find(template_parameters_in_effect[i], name);
            if (d)
                break;
        }
        /* [class.mem]/6: in a complete-class context the class is
           complete: the region with every member is searched first, so
           that a member template declared after a function of the same
           name keeps the overload set a template-name ([temp.names]/2) */
        if (complete_contexts && scopes[s].complete)
            d = find(scopes[s].complete, name);
        if (!d)
            d = find(s, name);
        if (d || s == global_namespace)
            break;
    }
    if (d) {
        found = d->kind;
        if (scope)
            *scope = d->scope;
        if (dependent)
            *dependent = d->dependent;
        if (d->dependent)
            dependent_found++;
    }
    lookups.append({name, current_scope, false, found});
    return found;
}

name_kind declared_names::lookup_elaborated(uint32_t name)
{
    ignoring_non_types = true;
    name_kind found = lookup_unqualified(name);
    ignoring_non_types = false;
    return found;
}

name_kind declared_names::lookup_qualified(scope_index in, uint32_t name,
                                           scope_index *scope, bool *dependent)
{
    /* [basic.lookup.qual]/1: "The name of a class or namespace member or
       enumerator can be referred to after the :: scope resolution operator
       applied to a nested-name-specifier that denotes its class, namespace,
       or enumeration." */
    name_kind found = name_kind::not_declared;
    const declaration *d = nullptr;
    if (complete_contexts && scopes[in].complete)
        d = find(scopes[in].complete, name);
    if (!d)
        d = find(in, name);
    if (d) {
        found = d->kind;
        if (scope)
            *scope = d->scope;
        if (dependent)
            *dependent = d->dependent;
        if (d->dependent)
            dependent_found++;
    }
    lookups.append({name, in, true, found});
    return found;
}

} // namespace cplusplus
