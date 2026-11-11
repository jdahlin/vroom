/* Repeated loads within a block, for any CPU's records.  The caller says per
 * record what it does to memory and registers; the scan keeps a few
 * (base, displacement, symbol, size, extension) -> register entries and
 * hands a load of the same memory back to the caller as a copy.
 */
#include "../emitimpl.h"

#define NLV 8

VPRIV void em_rec_load_reuse(const em_rec_load_scan *s, uint32_t begin, uint32_t end)
{
	struct { int64_t imm; uint32_t ref, pos; uint64_t kill; uint8_t base, sz, cond, reg, ok; } t[NLV];
	unsigned next = 0;

	memset(t, 0, sizeof(t));
	for (uint32_t k = begin; k <= end; k++) {
		const unsigned char *rp = (const unsigned char *)s->records + (size_t)k * s->stride;
		em_rec_mem m = { 0 };
		bool reused = false;

		if (rp[s->op_offset] == s->deleted_op)
			continue;
		s->inspect(s->ctx, k, &m);
		if (m.kind == EM_MEM_LOAD) {
			for (unsigned q = 0; q < NLV; q++) {
				if (t[q].ok && t[q].base == m.base && t[q].imm == m.imm && t[q].ref == m.ref && t[q].sz == m.sz &&
				    t[q].cond == m.cond) {
					s->reuse(s->ctx, k, t[q].reg, t[q].pos);
					reused = true;
					break;
				}
			}
			if (reused) {
				/* now a copy (or gone): its writes are those of the copy */
				s->inspect(s->ctx, k, &m);
				m.kind = EM_MEM_NONE;
			}
		}
		if (m.kind == EM_MEM_BOUNDARY || m.kind == EM_MEM_OTHER || m.kind == EM_MEM_STORE)
			memset(t, 0, sizeof(t));
		for (unsigned q = 0; q < NLV; q++)
			if (t[q].ok && (m.write & t[q].kill))
				t[q].ok = 0;
		if (!reused && (m.kind == EM_MEM_LOAD || (m.kind == EM_MEM_STORE && m.forward)) && !(m.kind == EM_MEM_LOAD && m.reg == m.base)) {
			unsigned q = next++ % NLV;

			t[q].ok = 1, t[q].imm = m.imm, t[q].ref = m.ref, t[q].pos = k, t[q].base = m.base;
			t[q].sz = m.sz, t[q].cond = m.cond, t[q].reg = m.reg;
			t[q].kill = ((uint64_t)1 << m.reg) | (m.base < 64 ? (uint64_t)1 << m.base : 0);
		}
	}
}
