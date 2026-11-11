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
#endif
