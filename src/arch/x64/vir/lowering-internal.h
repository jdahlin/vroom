/* Internal header of the x86-64 lowering of VIR (src/arch/x64/vir/): the lowering state and the
 * prototypes its files share.  One unity TU with codegen, or file by file. */
#ifndef VCC_ARCH_X64_VIR_LOWERING_INTERNAL_H
#define VCC_ARCH_X64_VIR_LOWERING_INTERNAL_H

#include "../codegen/x64impl.h"
#include "../../../codegen/walk/proto.h"
#include "../../../codegen/vir/preir.h"
#include "../../../codegen/vir/lowering-driver.h"
#include "vir/layout.h"

typedef struct x64_vir_context {
	codegen_context *context;
	vir_function *function;
	vir_alloc_out *allocation;
	int64_t *slot, *spill;
	int general_temporary;
	uint32_t *label, *edge_labels, ret, compare_condition, compare_block, setcc_record;
	uint8_t *uses;
	const vir_loops *loops;
	uint32_t *order,*position,*rotations,*chase,block_current;
	bool map;
	unsigned probe, site;
	uint32_t map_block, map_value;
	unsigned dup;bool may_fall_through,scratch_register10;
	bool ms;	/* the Microsoft x64 convention (Windows): arguments by position, rcx rdx r8 r9 / xmm0-3, shadow space */
	uint32_t memory_value, memory_skip, scaled_value, scaled_skip; int memory_base, memory_index, scaled_index; uint8_t memory_shift, scaled_shift; bool memory_is_indexed; int64_t memory_offset;
	uint32_t big_endian_value; int big_endian_base,big_endian_index; uint8_t big_endian_shift,big_endian_width; bool big_endian_is_indexed; int64_t big_endian_offset;
	uint32_t compare_memory_value; uint8_t compare_memory_size; bool compare_memory_side;
	uint32_t test_value; uint8_t test_size; int64_t test_immediate;
	virbuild_lowering drv;	/* the shared driver's inputs (codegen/vir/lowering-driver.h) */
} x64_vir_context;

VPRIV void *x64_vir_array_grow(void *p, uint32_t *cap, uint32_t n, size_t size);
VPRIV int x64_vir_register_number(uint8_t r);
VPRIV int x64_vir_type_size(uint32_t t);
VPRIV int64_t x64_vir_constant_bits(const vir_insn *i);
VPRIV uint8_t x64_vir_argument_register(const vir_function *f, uint32_t sig, uint32_t g, int *stack, bool ms);
VPRIV bool x64_vir_value_immediate(const vir_function *f, uint32_t v, int64_t *value);
VPRIV bool x64_vir_memory_block_is_small(const vir_function *f,const vir_insn *i);
VPRIV bool x64_vir_switch_is_dense(const vir_function *f,const vir_insn *in,uint64_t *low,uint64_t *range);
VPRIV void x64_vir_fixed_registers_get(const vir_function *f, uint32_t v, vir_register_constraint *x, void *ctx);
VPRIV void x64_vir_text_append(x64_vir_context *L, const char *s);
VPRIV void vx_mark(x64_vir_context *L, bool begin, unsigned bit, uint32_t b, uint32_t pos,
		    uint32_t v, uint32_t dst, uint32_t src, uint32_t to);
VPRIV void vx_probe(x64_vir_context *L, unsigned bit);
VPRIV void x64_vir_simd_emit(x64_vir_context *L, const char *op, int d, int a, int b);
VPRIV void x64_vir_constant_materialize(x64_vir_context *L, uint8_t dst, uint32_t v);
VPRIV void x64_vir_slot_access(x64_vir_context *L, bool load, uint8_t reg, uint32_t s);
VPRIV void vx_edit(x64_vir_context *L, const vir_edit *x);
VPRIV void x64_vir_edge_emit(x64_vir_context *L, uint32_t b, uint32_t k);
VPRIV void vx_edge_moves(x64_vir_context *L, uint32_t g);
VPRIV uint32_t x64_vir_edge_target(x64_vir_context *L,uint32_t b,uint32_t k,uint32_t target);
VPRIV uint32_t x64_vir_block_next(const x64_vir_context *L);
VPRIV void x64_vir_goto_emit(x64_vir_context *L, uint32_t b, uint32_t target);
VPRIV bool x64_vir_value_is_supported(const vir_function *f, uint32_t v);
VPRIV void x64_vir_constant_compare_emit(x64_vir_context *L, int reg, int size, uint64_t value);
VPRIV void x64_vir_switch_lower(x64_vir_context *L, uint32_t block, const vir_insn *in, int selector);
VPRIV bool x64_vir_fold_edits_check(x64_vir_context *L,uint32_t b,uint32_t pos,int keep);
VPRIV bool x64_vir_memory_next_fold(x64_vir_context *L,uint32_t b,uint32_t v);
VPRIV bool x64_vir_memory_cast_fold(x64_vir_context *L,uint32_t b,uint32_t v);
VPRIV unsigned x64_vir_word_read_match(x64_vir_context *L,uint32_t b,uint32_t v,uint32_t *end);
VPRIV unsigned x64_vir_big_endian_address_match(x64_vir_context *L,uint32_t b,uint32_t v,uint32_t *end);
VPRIV bool x64_vir_address_memory_fold(x64_vir_context *L,uint32_t b,uint32_t v);
VPRIV bool x64_vir_value_upper_half_is_clear(const vir_function *f,uint32_t v);
VPRIV bool x64_vir_value_is_sign_extended(x64_vir_context *L,uint32_t v);
VPRIV void x64_vir_memory_block_lower(x64_vir_context *L,const vir_insn *i,int dst,int src);
VPRIV uint8_t x64_vir_condition_lower(x64_vir_context *L,uint32_t b,uint32_t v,uint32_t value,int reg);
VPRIV void x64_vir_insn_lower(x64_vir_context *L, uint32_t b, uint32_t v, const uint8_t *p, uint32_t n, uint8_t res);

#endif
