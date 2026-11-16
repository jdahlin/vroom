/*
 * declared_names.cpp: the names the grammar depends on.
 *
 * The grammar is not context-free: [dcl.type.simple] "type-name",
 * [temp.names] "template-name" and [namespace.def] "namespace-name" are
 * identifiers that were declared as such, so `T * x;` is a declaration or
 * an expression by what T is.  [basic.lookup]/1: "Name lookup associates the
 * use of a name with a set of declarations of that name."  The parser keeps
 * only what decides between productions: per declarative region
 * ([basic.scope.declarative]/1), which names it declares and as which kind.
 *
 * This is a stand-in for [basic.lookup], not an implementation of it: no
 * using-directives yet, no class member lookup, no hiding rules beyond the
 * order of the scopes (doc/PLAN-cxx-parse.md lists what each step adds).
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

const declared_names::declaration *declared_names::find(scope_index in,
                                                        uint32_t name) const
{
    const scope &s = scopes[in];
    /* the latest declaration first: a later one of another kind is the one
       in effect ([basic.scope.hiding] is not modelled further) */
    for (size_t i = s.declarations.size(); i-- > 0;)
        if (s.declarations[i].name == name)
            return &s.declarations[i];
    return nullptr;
}

scope_index declared_names::enter_namespace(uint32_t name)
{
    /* [namespace.def]/1: "In a named-namespace-definition, the identifier
       is the name of the namespace."  /2: an extension-namespace-definition
       "is an original-namespace-name": the same declarative region again. */
    if (name) {
        const declaration *d = find(current_scope, name);
        if (d && d->kind == name_kind::namespace_name) {
            current_scope = d->scope;
            return current_scope;
        }
    }
    scope_index enclosing = current_scope;
    scope &s = scopes.make();
    s.kind = scope_kind::namespace_;
    s.enclosing = enclosing;
    scope_index made = static_cast<scope_index>(scopes.size() - 1);
    if (name)
        scopes[enclosing].declarations.append(
            {name, name_kind::namespace_name, made});
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

void declared_names::leave()
{
    current_scope = scopes[current_scope].enclosing;
}

void declared_names::declare(uint32_t name, name_kind kind)
{
    scopes[current_scope].declarations.append({name, kind, 0});
}

name_kind declared_names::lookup_unqualified(uint32_t name)
{
    /* [basic.lookup.unqual]/1: "In all the cases listed in
       [basic.lookup.unqual], the scopes are searched for a declaration in
       the order listed in each of the respective categories; name lookup
       ends as soon as a declaration is found for the name." */
    name_kind found = name_kind::not_declared;
    for (scope_index s = current_scope;; s = scopes[s].enclosing) {
        const declaration *d = find(s, name);
        if (d) {
            found = d->kind;
            break;
        }
        if (s == global_namespace)
            break;
    }
    lookups.append({name, current_scope, false, found});
    return found;
}

name_kind declared_names::lookup_qualified(scope_index in, uint32_t name,
                                           scope_index *scope)
{
    /* [basic.lookup.qual]/1: "The name of a class or namespace member or
       enumerator can be referred to after the :: scope resolution operator
       applied to a nested-name-specifier that denotes its class, namespace,
       or enumeration." */
    name_kind found = name_kind::not_declared;
    const declaration *d = find(in, name);
    if (d) {
        found = d->kind;
        if (scope && d->kind == name_kind::namespace_name)
            *scope = d->scope;
    }
    lookups.append({name, in, true, found});
    return found;
}

} // namespace cplusplus
