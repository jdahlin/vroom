/* Internal header of the x86-64 lowering of VIR (src/arch/x64/vir/): the lowering state and the
 * prototypes its files share.  One unity TU with codegen, or file by file. */
#ifndef VCC_ARCH_X64_VIR_LOWERING_INTERNAL_H
#define VCC_ARCH_X64_VIR_LOWERING_INTERNAL_H

#include "../codegen/x64impl.h"
#include "../../../codegen/walk/proto.h"
#include "../../../codegen/vir/preir.h"
#include "../../../codegen/vir/lowering-driver.h"
#include "vir/layout.h"

typedef struct vx {
	ecx *e;
	vir_function *f;
	vir_alloc_out *o;
	int64_t *slot, *spill;
	int gtmp;
	uint32_t *label, *edgelab, ret, cmp, cmpblock, setrec;
	uint8_t *uses;
	const vir_loops *lp;
	uint32_t *order,*position,*rotation,*chase,at;
	unsigned dup;bool fallok,r10;
	uint32_t memv, memskip, scaledv, scaledskip; int membase, memindex, scaleindex; uint8_t memsh, scalesh; bool indexed; int64_t memoff;
	uint32_t bev; int bebase,beindex; uint8_t besh,bewidth; bool beindexed; int64_t beoff;
	uint32_t cmpmemv; uint8_t cmpmemsize; bool cmpmemside;
	uint32_t testv; uint8_t testsize; int64_t testimm;
	virbuild_lowering drv;	/* the shared driver's inputs (codegen/vir/lowering-driver.h) */
} vx;

VPRIV void *x64_vir_array_grow(void *p, uint32_t *cap, uint32_t n, size_t size);
VPRIV int x64_vir_register_number(uint8_t r);
VPRIV int x64_vir_type_size(uint32_t t);
VPRIV int64_t x64_vir_constant_bits(const vir_insn *i);
VPRIV uint8_t x64_vir_argument_register(const vir_function *f, uint32_t sig, uint32_t g, int *stack);
VPRIV bool x64_vir_value_immediate(const vir_function *f, uint32_t v, int64_t *value);
VPRIV bool x64_vir_memory_block_is_small(const vir_function *f,const vir_insn *i);
VPRIV bool x64_vir_switch_is_dense(const vir_function *f,const vir_insn *in,uint64_t *low,uint64_t *range);
VPRIV void x64_vir_fixed_registers_get(const vir_function *f, uint32_t v, vir_register_constraint *x, void *ctx);
VPRIV void x64_vir_text_append(vx *L, const char *s);
VPRIV void x64_vir_simd_emit(vx *L, const char *op, int d, int a, int b);
VPRIV void x64_vir_constant_materialize(vx *L, uint8_t dst, uint32_t v);
VPRIV void x64_vir_slot_access(vx *L, bool load, uint8_t reg, uint32_t s);
VPRIV void vx_edit(vx *L, const vir_edit *x);
VPRIV void x64_vir_edge_emit(vx *L, uint32_t b, uint32_t k);
VPRIV void vx_edge_moves(vx *L, uint32_t g);
VPRIV uint32_t x64_vir_edge_target(vx *L,uint32_t b,uint32_t k,uint32_t target);
VPRIV uint32_t x64_vir_block_next(const vx *L);
VPRIV void x64_vir_goto_emit(vx *L, uint32_t b, uint32_t target);
VPRIV bool x64_vir_value_is_supported(const vir_function *f, uint32_t v);
VPRIV void x64_vir_constant_compare_emit(vx *L, int reg, int size, uint64_t value);
VPRIV void x64_vir_switch_lower(vx *L, uint32_t block, const vir_insn *in, int selector);
VPRIV bool x64_vir_fold_edits_check(vx *L,uint32_t b,uint32_t pos,int keep);
VPRIV bool x64_vir_memory_next_fold(vx *L,uint32_t b,uint32_t v);
VPRIV bool x64_vir_memory_cast_fold(vx *L,uint32_t b,uint32_t v);
VPRIV unsigned x64_vir_word_read_match(vx *L,uint32_t b,uint32_t v,uint32_t *end);
VPRIV unsigned x64_vir_big_endian_address_match(vx *L,uint32_t b,uint32_t v,uint32_t *end);
VPRIV bool x64_vir_address_memory_fold(vx *L,uint32_t b,uint32_t v);
VPRIV bool x64_vir_value_upper_half_is_clear(const vir_function *f,uint32_t v);
VPRIV bool x64_vir_value_is_sign_extended(vx *L,uint32_t v);
VPRIV void x64_vir_memory_block_lower(vx *L,const vir_insn *i,int dst,int src);
VPRIV uint8_t x64_vir_condition_lower(vx *L,uint32_t b,uint32_t v,uint32_t value,int reg);
VPRIV void x64_vir_insn_lower(vx *L, uint32_t b, uint32_t v, const uint8_t *p, uint32_t n, uint8_t res);

#endif
