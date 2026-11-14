#ifndef VCC_CODEGEN_RECORD_SLOTS_H
#define VCC_CODEGEN_RECORD_SLOTS_H
/* Exact memory cells, independent of instruction format and register model.
 * Allocation identities must survive stack-offset reuse. ID 0 is an untagged
 * or observable access: any intersecting cell is excluded, even for DSE.
 */
enum { RECORD_SLOT_BAD = 1, RECORD_SLOT_ESCAPED = 2 };
typedef struct record_slot_range {
	uint32_t id;
	int64_t offset;
	uint32_t size;
	bool bad, escaped;
} record_slot_range;
typedef struct record_slot_allocation {
	int64_t offset;
	uint32_t size;
	uint8_t state; /* 0: no access, 1: exact range, 2: several ranges */
	bool escaped;
	uint32_t multi_begin, multi_count;
} record_slot_allocation;
typedef struct record_slot_cells {
	record_slot_allocation *allocations;
	uint32_t allocation_capacity, allocation_count;
	record_slot_range *cells, *writes;
	uint32_t cell_capacity, write_capacity, cell_count, write_count;
	int64_t *write_ends;
	uint32_t write_end_capacity;
	uint8_t *flags;
	uint32_t flag_capacity, count;
	bool has_multi_range, valid;
} record_slot_cells;
/* Reusable caller-owned scratch: begin resets facts without freeing capacity.
 * First submit all accesses and escaped allocation IDs, then submit details
 * for multi-range allocations (the caller may replay all accesses). Finish
 * resolves overlaps; find returns a cell index, including BAD cells.
 */
VPRIV bool record_slot_begin(record_slot_cells *s, uint32_t allocations);
VPRIV void record_slot_access(record_slot_cells *s, uint32_t id, int64_t off, uint32_t size);
/* Single-pass alternative: collects exact details when an allocation first
 * becomes multi-range, so callers do not replay their complete record stream.
 * Use collect for every access, without a separate detail phase. Never mix
 * modes after an allocation has become multi-range; access alone retains no
 * exact history for subsequent streaming collection. */
VPRIV void record_slot_collect(record_slot_cells *s, uint32_t id, int64_t off, uint32_t size);
VPRIV void record_slot_escape(record_slot_cells *s, uint32_t id);
VPRIV void record_slot_detail(record_slot_cells *s, uint32_t id, int64_t off, uint32_t size);
VPRIV bool record_slot_finish(record_slot_cells *s);
VPRIV uint32_t record_slot_find(const record_slot_cells *s, uint32_t id, int64_t off, uint32_t size);
/* Value identity is target-owned: a native register with a write version, or
 * a stable wasm local. No physical register numbering or SSA is assumed here.
 * The caller supplies nonzero block generations and advances value versions
 * for every definition, including partial/implicit writes and opaque records.
 */
#define RECORD_SLOT_NO_VALUE UINT32_MAX
#define RECORD_SLOT_NO_STORE UINT32_MAX
typedef struct record_slot_value {
	uint32_t generation, version, memory, position, last, value, loads;
} record_slot_value;
typedef struct record_slot_values {
	record_slot_value *values;
	uint32_t capacity, count;
} record_slot_values;
VPRIV bool record_slot_values_begin(record_slot_values *s, uint32_t cells);
/* Only classified good cells may be queried. A load clears the pending store
 * even when forwarding fails. Load counts survive block generation changes.
 */
VPRIV record_slot_value *record_slot_value_get(record_slot_values *s, uint32_t cell, uint32_t gen, bool load);
VPRIV bool record_slot_value_is_available(const record_slot_value *a, uint32_t version, uint32_t memory, bool escaped);
/* Record an unforwarded load or a store after the target's value definitions.
 * Return the overwritten, unread store's position, or RECORD_SLOT_NO_STORE.
 * After a forwarded load the adapter only extends a->pos as needed for its
 * existing liveness; it must not count the removed memory read as a load.
 */
VPRIV uint32_t record_slot_value_note(record_slot_value *a, bool load, bool escaped,
		uint32_t value, uint32_t version, uint32_t memory, uint32_t pos);
VPRIV bool record_slot_value_is_dead(const record_slot_values *s, uint32_t cell, bool escaped);
#endif
