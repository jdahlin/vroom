/* Bounded block-local expression identities. This is not an instruction IR. */
#ifndef VCC_EMIT_FLOW_VALUES_H
#define VCC_EMIT_FLOW_VALUES_H
#include <stdbool.h>
#include <stdint.h>

#define EM_VALUES_SLOTS 32
/* The caller assigns immutable nonzero IDs to operand values. Redefinitions
 * get fresh IDs; a table reset is required before an ID can be reused. Keys
 * describe only operations the caller has proved pure and nontrapping. */
typedef struct em_value_key {
 uint32_t operation, type, left, right;
 uint64_t immediate;
} em_value_key;
typedef struct em_value_entry {
 em_value_key key;
 uint32_t value;
} em_value_entry;
typedef struct em_value_table {
 em_value_entry entries[EM_VALUES_SLOTS];
 uint32_t budget;
 bool active;
} em_value_table;

/* At a block boundary or opaque effect, reset to prevent facts crossing it.
 * On budget exhaustion all lookups fail until the next reset. A returned ID
 * is an abstract value, not a live machine register: each adapter must prove
 * that a usable location still holds it before eliminating an operation. */
VPRIV void em_values_reset(em_value_table *t, uint32_t budget) __attribute__((unused));
VPRIV uint32_t em_values_lookup(em_value_table *t, em_value_key key) __attribute__((unused));
VPRIV bool em_values_insert(em_value_table *t, em_value_key key, uint32_t value) __attribute__((unused));
#endif
