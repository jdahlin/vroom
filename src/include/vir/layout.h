/* Allocated VIR block placement and duplication policy. Caller-owned arrays
 * have f->block_count entries; the module allocates no memory and emits no CPU code. */
#ifndef VCC_VIR_LAYOUT_H
#define VCC_VIR_LAYOUT_H
#include "vir/register-allocation.h"

void vir_layout_chases_compute(const vir_function *f, const vir_alloc_out *o, uint32_t *chase, uint32_t *path);
/* rotation[b] is the loop ID if header b moves below its body, VIR_BLOCK_NONE
 * otherwise. Emitters must duplicate the test on entry edges when requested
 * by vir_layout_duplicate, in addition to following order/position. */
void vir_layout_order(const vir_function *f, const vir_loops *lp, const uint32_t *chase,
                      bool rotate, uint32_t *order, uint32_t *position, uint32_t *rotation);
bool vir_layout_duplicate(const vir_function *f, const vir_loops *lp, const uint32_t *rotation,
                          uint32_t from, uint32_t to, uint32_t vir_parse_token_next, unsigned depth, bool optimize);
#endif
