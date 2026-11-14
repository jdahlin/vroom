/* A fixed-size direct-mapped table; collisions discard opportunities only. */
#include "../codegen-internal.h"

_Static_assert((EM_VALUES_SLOTS & (EM_VALUES_SLOTS - 1)) == 0, "value table size must be a power of two");

static uint32_t em_values_hash(em_value_key k)
{
 uint64_t h = ((uint64_t)k.operation << 32) | k.type;
 h ^= (uint64_t)k.left * UINT64_C(0x9e3779b185ebca87);
 h ^= (uint64_t)k.right * UINT64_C(0xc2b2ae3d27d4eb4f);
 h ^= k.immediate * UINT64_C(0x165667b19e3779f9);
 h ^= h >> 33;
 return (uint32_t)h & (EM_VALUES_SLOTS - 1);
}

static bool em_values_step(em_value_table *t)
{
 if (!t->active || !t->budget) {
  t->active = false;
  return false;
 }
 --t->budget;
 return true;
}

VPRIV void em_values_reset(em_value_table *t, uint32_t budget)
{
 memset(t->entries, 0, sizeof(t->entries));
 t->budget = budget;
 t->active = budget != 0;
}

VPRIV uint32_t em_values_lookup(em_value_table *t, em_value_key key)
{
 if (!em_values_step(t)) return 0;
 const em_value_entry *e = &t->entries[em_values_hash(key)];
 const em_value_key *k = &e->key;
 return e->value && k->operation == key.operation && k->type == key.type &&
        k->left == key.left && k->right == key.right && k->immediate == key.immediate ? e->value : 0;
}

VPRIV bool em_values_insert(em_value_table *t, em_value_key key, uint32_t value)
{
 if (!em_values_step(t) || !value) return false;
 t->entries[em_values_hash(key)] = (em_value_entry){ .key = key, .value = value };
 return true;
}
