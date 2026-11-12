/* Discard records after a definite transfer, up to the next possible entry.
 * No CFG or allocation is needed: every label and opaque record starts a
 * conservatively reachable region, even when no direct branch names it.
 */
#include "../emitimpl.h"

VPRIV uint32_t em_rec_unreachable(void *records, uint32_t count, size_t stride, size_t op_offset,
				const uint8_t *flow, size_t nflow,
				void (*remap)(void *, uint32_t, uint32_t), void *ctx)
{
	unsigned char *base = records;
	uint32_t n = 0;
	bool live = true;

	for (uint32_t k = 0; k < count; k++) {
		const unsigned char *i = base + (size_t)k * stride;
		unsigned op = i[op_offset];
		unsigned kind = op < nflow ? flow[op] : EM_FLOW_ENTRY;

		if (remap)
			remap(ctx, k, n);
		/* Default/unknown classifications are entry barriers as well. */
		if (kind != EM_FLOW_NEXT && kind != EM_FLOW_STOP)
			live = true;
		if (!live)
			continue;
		if (n != k)
			memcpy(base + (size_t)n * stride, i, stride);
		n++;
		if (kind == EM_FLOW_STOP)
			live = false;
	}
	if (remap)
		remap(ctx, UINT32_MAX, n);
	return n;
}
