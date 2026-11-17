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
    for (entity_index &slot : slots)
        slot = 0;
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
    for (size_t i = scope_hash(scope, name) & mask; slots[i];
         i = (i + 1) & mask) {
        const entity &e = records[slots[i]];
        if (e.enclosing == scope && e.name == name)
            return slots[i];
    }
    return 0;
}

void entities::grow()
{
    owned_array<entity_index> old;
    old.resize(slots.size());
    for (size_t i = 0; i < slots.size(); i++)
        old[i] = slots[i];
    slots.resize(slots.size() * 2);
    for (entity_index &slot : slots)
        slot = 0;
    size_t mask = slots.size() - 1;
    for (entity_index e : old) {
        if (!e)
            continue;
        size_t i = scope_hash(records[e].enclosing, records[e].name) & mask;
        while (slots[i])
            i = (i + 1) & mask;
        slots[i] = e;
    }
}

/* the entity e (whose enclosing scope and name are the ones bound) is what
   the name refers to in the scope from now on */
void entities::bind(entity_index scope, uint32_t name, entity_index e)
{
    size_t mask = slots.size() - 1;
    size_t i = scope_hash(scope, name) & mask;
    for (; slots[i]; i = (i + 1) & mask) {
        const entity &x = records[slots[i]];
        if (x.enclosing == scope && x.name == name) {
            slots[i] = e;
            return;
        }
    }
    slots[i] = e;
    size_t used = 0;
    for (entity_index slot : slots)
        used += slot != 0;
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

void entities::add_declaration(entity_index e, node_index node)
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
