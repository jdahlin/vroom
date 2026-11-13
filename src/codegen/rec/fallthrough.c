/* Remove an unconditional jump when its destination labels the next instruction.
 * Records remain opaque: arm64's ins and x86's rec are distinct C types.
 */
#include "../emitimpl.h"

VPRIV uint32_t em_rec_fallthrough(void *records, uint32_t count, const em_rec_layout *layout,
				void (*remap)(void *, uint32_t, uint32_t), void *ctx)
{
	unsigned char *base = records;
	uint32_t n = 0;

	for (uint32_t k = 0; k < count; k++) {
		const unsigned char *i = base + (size_t)k * layout->stride;
		bool drop = false;

		if (remap)
			remap(ctx, k, n);
		if (i[layout->op_offset] == layout->jump_op) {
			uint32_t target;

			memcpy(&target, i + layout->label_offset, sizeof(target));
			/* Only consecutive local labels can be crossed. Raw asm, named
			 * labels and every other record stop the search. Each label run
			 * can follow only one jump, so this remains linear in count.
			 */
			for (uint32_t j = k + 1; j < count; j++) {
				const unsigned char *next = base + (size_t)j * layout->stride;
				uint32_t label;

				if (next[layout->op_offset] != layout->label_op)
					break;
				memcpy(&label, next + layout->label_offset, sizeof(label));
				if (label == target) {
					drop = true;
					break;
				}
			}
		}
		if (!drop) {
			if (n != k)
				memcpy(base + (size_t)n * layout->stride, i, layout->stride);
			n++;
		}
	}
	if (remap)
		remap(ctx, UINT32_MAX, n);
	return n;
}
