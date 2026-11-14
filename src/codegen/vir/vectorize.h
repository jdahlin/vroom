/* The loop vectorizer's state (vectorize.c) and what loop-constants.c shares with it. */
#ifndef VCC_CODEGEN_VIR_VECTORIZE_H
#define VCC_CODEGEN_VIR_VECTORIZE_H
#include "preir.h"

enum { VIRBUILD_VECTORIZE_KIND_NONE, VIRBUILD_VECTORIZE_KIND_INV, VIRBUILD_VECTORIZE_KIND_LIN, VIRBUILD_VECTORIZE_KIND_VEC, VIRBUILD_VECTORIZE_KIND_MASK, VIRBUILD_VECTORIZE_KIND_RED, VIRBUILD_VECTORIZE_KIND_REDUPD, VIRBUILD_VECTORIZE_KIND_BAD,
       VIRBUILD_VECTORIZE_KIND_W32, VIRBUILD_VECTORIZE_KIND_SCALAR, VIRBUILD_VECTORIZE_KIND_CARRY, VIRBUILD_VECTORIZE_KIND_FOLD };	/* an i64 that is an i32 lane widened (ext, or a select of such): only its trunc is a lane */
enum { VIRBUILD_VECTORIZE_OBJECT_NONE, VIRBUILD_VECTORIZE_OBJECT_SYM, VIRBUILD_VECTORIZE_OBJECT_PTR };

typedef struct virbuild_vectorize_info {
	uint8_t kind, is_exact, has_object;
	uint8_t is_unsigned;		/* an i32 whose form is its unsigned value (zero-extends; a signed operation on it is not exact) */
	uint16_t use_count;		/* uses by the loop's instructions and terminators */
	int64_t step;		/* what one iteration adds */
	uint32_t object_value, term_value;	/* the object addressed (a symbol ref, a pointer value); an invariant term */
	int64_t term_factor, constant;		/* ... its factor; the constant term */
	uint32_t entry_value, body_value, first_lane_value, lanes;	/* its value at entry (in VPRE), in the vector body or VPH, at t = 0 (in VPH), its lanes */
} virbuild_vectorize_info;

typedef struct virbuild_vectorize_access { uint32_t insn, position; bool store; } virbuild_vectorize_access;
typedef struct virbuild_vectorize_reduction { uint32_t parameter, update, operand, accumulator, accumulator_next; } virbuild_vectorize_reduction;

typedef struct virbuild_vectorize_state {
	codegen_context *context;
	virbuild_vectorize_info *infos; uint32_t value_count;			/* per value below n0 */
	uint32_t *touched; uint32_t touched_count, touched_capacity;
	uint8_t *in_loop;				/* per block: in the loop */
	uint32_t loop_blocks[64], loop_block_count;			/* the loop's blocks, H first */
	uint32_t header, predecessor, latch, exit, predecessor_index, latch_index;		/* header, entry predecessor, latch, exit; their slots in H's predecessors */
	uint32_t preheader_block, vector_header_block, vector_body_block, vector_exit_block, join_block;
	virbuild_vectorize_access accesses[64]; uint32_t access_count;
	uint32_t gathers[64], gather_count;
	uint32_t invariant_loads[64], invariant_load_count;		/* loads taken as invariant */
	virbuild_vectorize_reduction reductions[8]; uint32_t reduction_count;
	struct { uint32_t p, next, acc, last, cur; } fold[8]; uint32_t nfold;
	struct { uint32_t p, next, par, last; bool busy; } carry[8]; uint32_t ncarry;
	uint32_t ind[32], indb[32], indn[32], nind;	/* H's inductions; VB's parameter and its next value */
	int64_t indk[32];			/* ... what an iteration adds */
	uint32_t one[8], none;			/* steps that are values: the vector loop runs when each is 1 */
	uint32_t stepv[4]; int64_t stepc[4]; uint32_t nstep;	/* lanes 0, c, 2c, 3c (in VPH) */
	struct { uint32_t a, b; } chk[8]; uint32_t nchk;	/* pairs whose ranges must not meet (run time): an access's index, or 64 + an invariant load's */
	struct { uint32_t a, par; bool rev; } pin[32]; uint32_t npin;	/* per address: VB's pointer to its four elements */
	uint32_t vf, esz, lg, ity, fty;		/* lanes; a lane's bytes, their log2; its integer and float types */
	uint32_t pos;
	const char *why;			/* why not (NULL: so far, yes) */
} virbuild_vectorize_state;

enum { VIRBUILD_VECTORIZE_MODE_ENTRY, VIRBUILD_VECTORIZE_MODE_BODY, VIRBUILD_VECTORIZE_MODE_T0 };

/* vectorize.c */
VPRIV bool virbuild_vectorize_insn_is_skipped(virbuild_context *V, uint32_t q);
VPRIV bool virbuild_byte_or_reduce(virbuild_vectorize_state *S);
VPRIV void virbuild_function_vectorize(codegen_context *e);

/* loop-constants.c */
VPRIV void virbuild_loop_constants_pin(virbuild_vectorize_state *S, uint32_t pre);

#endif
