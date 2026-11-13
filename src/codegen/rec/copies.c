/* Forward copies within a known block. Liveness and target operand semantics
 * belong to the caller; this bounded transaction neither allocates nor builds
 * a second effects table. Records and policy flags may have different strides.
 */
#include "../codegen-internal.h"

VPRIV bool record_copy_forward(const record_copy_scan *s, uint32_t k, uint32_t end, uint8_t t, uint8_t v)
{
	unsigned char *records = s->records;
	uint64_t tb = (uint64_t)1 << t, vb = (uint64_t)1 << v;
	uint32_t j, nrw = 0;
	static _Thread_local uint32_t rw[256];

	for (j = k + 1; j <= end; j++) {
		const unsigned char *i = records + (size_t)j * s->stride;
		uint16_t flags;
		uint64_t r, w;

		if (i[s->op_offset] == s->deleted_op)
			continue;
		if (s->inspect) {
			if (!s->inspect(s->ctx, j, &r, &w))
				return false;
		} else {
			memcpy(&flags, (const unsigned char *)s->flags + (size_t)j * s->flags_stride, sizeof(flags));
			if (flags & s->opaque_mask)
				return false;
			r = s->read[j], w = s->write[j];
		}
		if (r & tb) {
			/* Preflight every use before changing anything. The callback
			 * handles whole-record restrictions, including implicit and tied
			 * operands; there is no indirect callback for each operand field.
			 */
			if (!s->rewrite(s->ctx, j, t, v, false) || nrw == 256)
				return false;
			rw[nrw++] = j;
		}
		if ((w & tb) || (s->live && !(s->live[j] & tb)))
			break;
		if (w & vb)
			return false;
	}
	if (j > end)
		return false;
	for (uint32_t q = 0; q < nrw; q++) {
		s->rewrite(s->ctx, rw[q], t, v, true);
		if (s->read)
			s->read[rw[q]] = (s->read[rw[q]] & ~tb) | vb;
	}
	if (s->live)
		for (uint32_t q = k; nrw && q < rw[nrw - 1]; q++)
			s->live[q] |= vb;
	records[(size_t)k * s->stride + s->op_offset] = s->deleted_op;
	return true;
}
