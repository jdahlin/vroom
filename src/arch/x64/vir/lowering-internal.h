/* Internal header of the x86-64 lowering of VIR (src/arch/x64/vir/): the lowering state and the
 * prototypes its files share.  One unity TU with codegen, or file by file. */
#ifndef VIR_X64_LOWERING_INTERNAL_H
#define VIR_X64_LOWERING_INTERNAL_H

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

VPRIV void *vx_grow(void *p, uint32_t *cap, uint32_t n, size_t size);
VPRIV int vx_reg(uint8_t r);
VPRIV int vx_size(uint32_t t);
VPRIV int64_t vx_bits(const vir_insn *i);
VPRIV uint8_t vx_arg(const vir_function *f, uint32_t sig, uint32_t g, int *stack);
VPRIV bool vx_imm(const vir_function *f, uint32_t v, int64_t *value);
VPRIV bool vx_smallmem(const vir_function *f,const vir_insn *i);
VPRIV bool vx_dense_switch(const vir_function *f,const vir_insn *in,uint64_t *low,uint64_t *range);
VPRIV void vx_fixed(const vir_function *f, uint32_t v, vir_register_constraint *x, void *ctx);
VPRIV void vx_raw(vx *L, const char *s);
VPRIV void vx_simd(vx *L, const char *op, int d, int a, int b);
VPRIV void vx_mat(vx *L, uint8_t dst, uint32_t v);
VPRIV void vx_memslot(vx *L, bool load, uint8_t reg, uint32_t s);
VPRIV void vx_edit(vx *L, const vir_edit *x);
VPRIV void vx_edge(vx *L, uint32_t b, uint32_t k);
VPRIV void vx_edge_moves(vx *L, uint32_t g);
VPRIV uint32_t vx_target(vx *L,uint32_t b,uint32_t k,uint32_t target);
VPRIV uint32_t vx_next(const vx *L);
VPRIV void vx_goto(vx *L, uint32_t b, uint32_t target);
VPRIV bool vx_supported(const vir_function *f, uint32_t v);
VPRIV void vx_cmpconst(vx *L, int reg, int size, uint64_t value);
VPRIV void vx_switch(vx *L, uint32_t block, const vir_insn *in, int selector);
VPRIV bool vx_foldedits(vx *L,uint32_t b,uint32_t pos,int keep);
VPRIV bool vx_memnext(vx *L,uint32_t b,uint32_t v);
VPRIV bool vx_castmem(vx *L,uint32_t b,uint32_t v);
VPRIV unsigned vx_readword(vx *L,uint32_t b,uint32_t v,uint32_t *end);
VPRIV unsigned vx_beaddr(vx *L,uint32_t b,uint32_t v,uint32_t *end);
VPRIV bool vx_addrmem(vx *L,uint32_t b,uint32_t v);
VPRIV bool vx_clear32(const vir_function *f,uint32_t v);
VPRIV bool vx_sign64(vx *L,uint32_t v);
VPRIV void vx_memblock(vx *L,const vir_insn *i,int dst,int src);
VPRIV uint8_t vx_condition(vx *L,uint32_t b,uint32_t v,uint32_t value,int reg);
VPRIV void vx_ins(vx *L, uint32_t b, uint32_t v, const uint8_t *p, uint32_t n, uint8_t res);

#endif
