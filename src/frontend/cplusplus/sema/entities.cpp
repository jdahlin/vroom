/*
 * entities.cpp: the entities of a translation unit and their declarations
 * (doc/PLAN-cxx-sema.md).
 *
 * [basic.pre]/5: "Every name that denotes an entity is introduced by a
 * declaration."  An entity has one record however often it is declared
 * ([basic.link]/1); each declaration is the syntax node that made it, in
 * the order they appeared.
 */
#include "../cplusplus-internal.h"
#include "common/hash.h"
#include "frontend/cplusplus/sema.h"

namespace cplusplus
{

entities::entities()
{
    records.append(entity());
    declarations.append(entity_declaration());
    slots.resize(64);
    for (binding &slot : slots)
        slot = binding();
}

namespace
{

uint32_t scope_hash(entity_index scope, uint32_t name)
{
    uint32_t key[2] = {scope, name};
    return vhash_string(reinterpret_cast<const char *>(key), sizeof key);
}

} // namespace

entity_index entities::find(entity_index scope, uint32_t name) const
{
    size_t mask = slots.size() - 1;
    for (size_t i = scope_hash(scope, name) & mask; slots[i].entity;
         i = (i + 1) & mask)
        if (slots[i].scope == scope && slots[i].name == name)
            return slots[i].entity;
    return 0;
}

/* [dcl.link]/7: the first namespace-scope declaration of this C name. */
entity_index entities::find_c_linkage(uint32_t name, entity_kind kind) const
{
    const entity *entries = records.data();
    size_t count = records.size();
    c_linkage_by_name.update(c_linkage,
                             [&](entity_index e) { return entries[e].name; });
    /* (the first made of them) */
    entity_index found = 0;
    for (uint32_t i = c_linkage_by_name.first(name); i;
         i = c_linkage_by_name.after(i)) {
        entity_index at = c_linkage[i - 1];
        const entity &e = entries[at];
        if (at < count && e.kind == kind &&
            (!e.enclosing ||
             entries[e.enclosing].kind == entity_kind::namespace_entity) &&
            (!found || at < found))
            found = at;
    }
    return found;
}

void entities::give_c_linkage(entity_index e)
{
    if (records[e].c_language_linkage)
        return;
    records[e].c_language_linkage = true;
    c_linkage.append(e);
}

bool entities::bound_by_using(entity_index scope, uint32_t name) const
{
    size_t mask = slots.size() - 1;
    for (size_t i = scope_hash(scope, name) & mask; slots[i].entity;
         i = (i + 1) & mask)
        if (slots[i].scope == scope && slots[i].name == name)
            return slots[i].by_using_declaration;
    return false;
}

void entities::grow()
{
    owned_array<binding> old;
    old.resize(slots.size());
    for (size_t i = 0; i < slots.size(); i++)
        old[i] = slots[i];
    slots.resize(slots.size() * 2);
    for (binding &slot : slots)
        slot = binding();
    size_t mask = slots.size() - 1;
    for (const binding &b : old) {
        if (!b.entity)
            continue;
        size_t i = scope_hash(b.scope, b.name) & mask;
        while (slots[i].entity)
            i = (i + 1) & mask;
        slots[i] = b;
    }
}

/* the entity e is what the name refers to in the scope from now on */
void entities::bind(entity_index scope, uint32_t name, entity_index e,
                    bool by_using_declaration)
{
    size_t mask = slots.size() - 1;
    size_t i = scope_hash(scope, name) & mask;
    for (; slots[i].entity; i = (i + 1) & mask)
        if (slots[i].scope == scope && slots[i].name == name) {
            slots[i].entity = e;
            slots[i].by_using_declaration = by_using_declaration;
            return;
        }
    slots[i].scope = scope;
    slots[i].name = name;
    slots[i].entity = e;
    slots[i].by_using_declaration = by_using_declaration;
    used++;
    if (used * 2 > slots.size())
        grow();
}

entity_index entities::make(entity_kind kind, uint32_t name,
                            entity_index enclosing)
{
    entity e;
    e.kind = kind;
    e.name = name;
    e.enclosing = enclosing;
    records.append(e);
    return static_cast<entity_index>(records.size() - 1);
}

void entities::add_declaration(entity_index e, SyntaxRef node)
{
    entity_declaration d;
    d.node = node;
    uint32_t index = static_cast<uint32_t>(declarations.size());
    declarations.append(d);
    entity &record = records[e];
    if (record.last_declaration)
        declarations[record.last_declaration].next = index;
    else
        record.first_declaration = index;
    record.last_declaration = index;
}

} // namespace cplusplus
