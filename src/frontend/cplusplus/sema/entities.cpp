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
#include "frontend/cplusplus/sema.h"

namespace cplusplus
{

entities::entities()
{
    records.append(entity());
    declarations.append(entity_declaration());
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
