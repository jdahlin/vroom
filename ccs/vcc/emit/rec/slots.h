#ifndef VCC_EMIT_REC_SLOTS_H
#define VCC_EMIT_REC_SLOTS_H
/* Exact memory cells, independent of instruction format and register model.
 * Allocation identities must survive stack-offset reuse. ID 0 is an untagged
 * or observable access: any intersecting cell is excluded, even for DSE.
 */
enum { EM_SLOT_BAD = 1, EM_SLOT_ESCAPED = 2 };
typedef struct em_slot_range {
	uint32_t id;
	int64_t off;
	uint32_t size;
	bool bad, esc;
} em_slot_range;
typedef struct em_slot_alloc {
	int64_t off;
	uint32_t size;
	uint8_t state; /* 0: no access, 1: exact range, 2: several ranges */
	bool esc;
	uint32_t mb, mn;
} em_slot_alloc;
typedef struct em_slot_cells {
	em_slot_alloc *a;
	uint32_t capa, na;
	em_slot_range *c, *w;
	uint32_t capc, capw, nm, nw;
	int64_t *wend;
	uint32_t capwe;
	uint8_t *flags;
	uint32_t capflags, count;
	bool multi, valid;
} em_slot_cells;
/* Reusable caller-owned scratch: begin resets facts without freeing capacity.
 * First submit all accesses and escaped allocation IDs, then submit details
 * for multi-range allocations (the caller may replay all accesses). Finish
 * resolves overlaps; find returns a cell index, including BAD cells.
 */
VPRIV bool em_slot_begin(em_slot_cells *s, uint32_t allocations);
VPRIV void em_slot_access(em_slot_cells *s, uint32_t id, int64_t off, uint32_t size);
VPRIV void em_slot_escape(em_slot_cells *s, uint32_t id);
VPRIV void em_slot_detail(em_slot_cells *s, uint32_t id, int64_t off, uint32_t size);
VPRIV bool em_slot_finish(em_slot_cells *s);
VPRIV uint32_t em_slot_find(const em_slot_cells *s, uint32_t id, int64_t off, uint32_t size);
/* Value identity is target-owned: a native register with a write version, or
 * a stable wasm local. No physical register numbering or SSA is assumed here.
 * The caller supplies nonzero block generations and advances value versions
 * for every definition, including partial/implicit writes and opaque records.
 */
#define EM_SLOT_NO_VALUE UINT32_MAX
#define EM_SLOT_NO_STORE UINT32_MAX
typedef struct em_slot_value {
	uint32_t gen, version, memory, pos, last, value, loads;
} em_slot_value;
typedef struct em_slot_values {
	em_slot_value *a;
	uint32_t cap, count;
} em_slot_values;
VPRIV bool em_slot_values_begin(em_slot_values *s, uint32_t cells);
/* Only classified good cells may be queried. A load clears the pending store
 * even when forwarding fails. Load counts survive block generation changes.
 */
VPRIV em_slot_value *em_slot_value_get(em_slot_values *s, uint32_t cell, uint32_t gen, bool load);
VPRIV bool em_slot_value_available(const em_slot_value *a, uint32_t version, uint32_t memory, bool escaped);
/* Record an unforwarded load or a store after the target's value definitions.
 * Return the overwritten, unread store's position, or EM_SLOT_NO_STORE.
 * After a forwarded load the adapter only extends a->pos as needed for its
 * existing liveness; it must not count the removed memory read as a load.
 */
VPRIV uint32_t em_slot_value_record(em_slot_value *a, bool load, bool escaped,
		uint32_t value, uint32_t version, uint32_t memory, uint32_t pos);
VPRIV bool em_slot_value_dead(const em_slot_values *s, uint32_t cell, bool escaped);
#endif
