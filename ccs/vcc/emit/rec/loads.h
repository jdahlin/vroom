/* (a header: inlined into each CPU's pass) Repeated loads within a block, for any CPU's records.  The caller says per
 * record what it does to memory and registers; the scan keeps a few
 * (base, displacement, symbol, size, extension) -> register entries and
 * hands a load of the same memory back to the caller as a copy.
 */
#ifndef VCC_REC_LOADS_H
#define VCC_REC_LOADS_H
#include "../emitimpl.h"

#define NLV 8

/* always_inline: the scan's callbacks are constants at each use, so the calls fold */
static inline __attribute__((always_inline)) void em_rec_load_reuse(const em_rec_load_scan *s, uint32_t begin, uint32_t end)
{
	struct { int64_t imm; uint32_t ref, pos; uint64_t kill; uint8_t base, sz, cond, reg; } t[NLV];
	uint8_t ok[NLV] = { 0 };	/* the only part cleared per block */
	unsigned next = 0, live = 0;	/* entries that may be valid */

	for (uint32_t k = begin; k <= end; k++) {
		const unsigned char *rp = (const unsigned char *)s->records + (size_t)k * s->stride;
		em_rec_mem m;
		bool reused = false;

		if (rp[s->op_offset] == s->deleted_op)
			continue;
		m.kind = 0, m.write = 0, m.forward = false;
		s->inspect(s->ctx, k, &m);
		if (m.kind == EM_MEM_LOAD && live) {
			for (unsigned q = 0; q < NLV; q++) {
				if (ok[q] && t[q].base == m.base && t[q].imm == m.imm && t[q].ref == m.ref && t[q].sz == m.sz &&
				    t[q].cond == m.cond) {
					s->reuse(s->ctx, k, t[q].reg, t[q].pos);
					reused = true;
					break;
				}
			}
			if (reused) {
				/* now a copy (or gone): its writes are those of the copy */
				m.kind = 0, m.write = 0;
				s->inspect(s->ctx, k, &m);
				m.kind = EM_MEM_NONE;
			}
		}
		if (m.kind >= EM_MEM_STORE) {
			if (live)
				memset(ok, 0, sizeof(ok)), live = 0;
		} else if (live && m.write) {
			for (unsigned q = 0; q < NLV; q++)
				if (ok[q] && (m.write & t[q].kill))
					ok[q] = 0;
		}
		if (!reused && (m.kind == EM_MEM_LOAD || (m.kind == EM_MEM_STORE && m.forward)) && !(m.kind == EM_MEM_LOAD && m.reg == m.base)) {
			unsigned q = next++ % NLV;

			ok[q] = 1, t[q].imm = m.imm, t[q].ref = m.ref, t[q].pos = k, t[q].base = m.base;
			t[q].sz = m.sz, t[q].cond = m.cond, t[q].reg = m.reg;
			live = 1;
			t[q].kill = ((uint64_t)1 << m.reg) | (m.base < 64 ? (uint64_t)1 << m.base : 0);
		}
	}
}
#endif
