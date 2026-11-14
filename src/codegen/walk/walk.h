/*
 * vcc code generator: the AST walker (PLAN-x86.md, layer 1) and the
 * lowering interface it calls (layer 2).
 *
 * The walker makes the decisions at C level, once for every target that uses
 * it: what a node means, constant folding of the values it builds, frame
 * slots for locals, labels and the loop stack, the call sequence, switch
 * strategy.  What an operation becomes is the target's: every call into it
 * goes through WALK_LOWER(name), x64_name (arch/x64/codegen/) in the first
 * instantiation and virbuild_function_name_get, the VIR builder, in the second, which
 * codegen/vir/walker-instance.c compiles with WALK_PREFIX and WALK_LOWER_PREFIX defined.
 *
 * Values are the emitter's val (constant, address, physical register);
 * lvalues its lval (frame slot, symbol, register base).  Frame slots are
 * offsets from the frame base, negative for locals.
 *
 * Part of the codegen unity TU, next to arch/a64/codegen/: every name here
 * starts with wk_ (virbuild_walk_ in the VIR instantiation).  The helpers it shares
 * with the other backends (note_ref, codegen_function_prepass_request, codegen_chunk_add, codegen_symbol_header_append,
 * codegen_symbol_is_local_variable, ...) are in codegen/ (references.c, function-output.c, ...).
 */
#ifndef VCC_CODEGEN_WALK_H
#define VCC_CODEGEN_WALK_H
#include "prefix.h"	/* generated (src/tools/gen-walkprefix.py): WALK_PREFIX renames the walker's functions; first, so that codegen-internal.h's wk_ prototypes are renamed too */
#include "../codegen-internal.h"
#include "../rec/record.h"

/*
 * An argument, parameter or result as the ABI passes it (WALK_LOWER(classify)): n
 * eightbytes of class cls[i] (WALK_ARGUMENT_INTEGER, WALK_ARGUMENT_SSE), or in memory (mem), or
 * nothing (n 0, not mem: an empty record).  Its bytes are in the frame slot
 * slot (8-byte words; a scalar extended to 64 bits).
 */
enum { WALK_ARGUMENT_INTEGER, WALK_ARGUMENT_SSE };
typedef struct walk_argument { uint8_t eightbyte_count, classes[2]; bool is_in_memory, is_x87, is_sse_up, is_vector, is_wide_record, is_unnamed; uint32_t align, bool_lane_count; uint64_t size; int64_t slot; } walk_argument;	/* x87: returned in st(0); up: one 16-byte SSE word (x86-64 SSEUP) */
/* ---------------- walker state (one per ecx) ---------------- */

#define WALK_LOOP_MAX 1024
#define WALK_CASE_MAX (1u << 20)

typedef struct walk_loop { uint32_t break_label, continue_label; int cleanup_depth; } walk_loop;	/* cdepth: the cleanups live at its entry */
#define WALK_CLEANUP_MAX 1024
typedef struct walk_case { i128 low, high; uint32_t node; } walk_case;

/* an inlined call: where its returns go */
typedef struct walk_array_element { uint32_t symbol, generation, instance; int64_t index; i128 value; } walk_array_element;	/* a[K] = constant (wkst.ael) */

typedef struct walk_inline_call {
	struct walk_inline_call *outer;
	uint32_t function, end_label, instance, call;
	ast_qualified_type return_type;
	int64_t return_slot;		/* the result's slot (a value held by address: its object) */
	int cleanup_base;		/* the cleanups live at the call */
	int vla_base;		/* caller VLA saves must outlive callee alloca */
	bool any_return, constant_is_ok;	/* a return seen; every return so far the constant cst */
	codegen_value constant;
	walk_array_element caller_array_elements[16];		/* the caller's known array elements at the call (restored after the copy) */
	int caller_array_element_count;
} walk_inline_call;

#define WALK_TEMPORARY_SLOT_MAX 1024

typedef struct walk_state {
	int64_t frame_offset;		/* bytes of locals below the frame base so far */
	uint32_t frame_alignment;
	/* Temporary slots: one handed out inside a statement is dead when the
	   statement ends (statement expressions give their value outside the
	   inner statements), so later statements reuse it.  tu: those of the
	   statements being walked (a stack, marked per statement); tf: free. */
	struct walk_temporary_slot { int64_t offset; uint32_t size, align; } temporary_slots_in_use[WALK_TEMPORARY_SLOT_MAX], temporary_slots_free[WALK_TEMPORARY_SLOT_MAX];
	uint32_t temporary_slot_in_use_count, temporary_slot_free_count, statement_depth;
	/* the locals of the inline copies being walked: free (and unbound) when
	   the copy ends; inlocal: inside a copy's parameters or body */
	struct walk_local_slot_entry { int64_t offset; uint32_t size, align, symbol; } local_slots[WALK_TEMPORARY_SLOT_MAX];
	uint32_t local_slot_count, inline_local_depth;
	uint32_t label_count;	/* local labels handed out in this function (1..) */
	uint32_t generation;		/* bindings below are valid when their gen is this */
	uint32_t *symbol_generations;		/* per sym */
	int64_t *symbol_offsets;		/* per sym: frame offset */
	uint32_t *label_generations, *user_labels;	/* per user label: local label */
	uint32_t *case_labels;		/* per node (AST_NODE_CASE/AST_NODE_DEFAULT): local label, valid when cgen == gen */
	uint32_t *case_generations;
	uint32_t *vla_generations, *vla_instances;	/* per-node VLA size slot, keyed by function and inline instance */
	int64_t *vla_offsets;
	uint32_t *vla_zero_generations;		/* per sym (a VLA): its saved sp was zeroed on entry when zgen == gen */
	walk_loop loop[WALK_LOOP_MAX];
	int loop_count;
	walk_case *cases;
	uint32_t case_count;
	uint32_t return_label;
	ast_qualified_type return_type;
	walk_argument return_record;		/* a record result: as the ABI returns it */
	uint32_t function_symbol;
	uint32_t cleanups[WALK_CLEANUP_MAX];	/* variables with __attribute__((cleanup)) in scope, innermost last */
	int cleanup_count;
	uint32_t vla[WALK_CLEANUP_MAX];	/* active VLA restore points, lowered by explicit alloca */
	int vla_count;
	/* the cleanup scopes as a tree (wk_scan): node k (1..) is a cleanup variable and the one
	   enclosing it; per sym its node, per user label the innermost node in scope there */
	struct { uint32_t symbol, parent; } cleanup_nodes[WALK_CLEANUP_MAX];
	uint32_t cleanup_node_count;
	uint32_t *cleanup_symbol_nodes, *cleanup_symbol_generations, *label_cleanup_nodes, *label_cleanup_generations;
	/* Constants for locals or parameters never written, bound per sym when kgen == gen. */
	uint32_t *constant_generations;
	codegen_value *constant_values;
	/* inlining: the copy being generated (0: the function itself), labels per copy */
	uint32_t instance, instance_count, *label_instances, *case_instances;
	struct walk_inline_call *inline_call;	/* the innermost inlined call, NULL in the function itself */
	int depth;
	bool condition_jumped;	/* walk_condition_emit jumped whatever the condition (AST_NODE_IF: the branch it skips is dead) */
	/* a[K] = constant for local arrays used only as a[CONST] (no CODEGEN_SYMBOL_ARRAY_ESCAPES): straight-line
	   code only (a label drops them all), as arm64's ael */
	walk_array_element array_elements[16];
	int array_element_count;
	/* a parameter of an inlined copy that is written later, bound to its constant argument
	   until its first write or a label: only an asm "i" operand reads it (rip_rel_ptr) */
	struct { uint32_t symbol, generation, instance; codegen_value value; } parameter_constants[16];
	int parameter_constant_count;
	/* -fstack-protector*: the canary's slot (made first: above every local), and whether this
	   function needs it (an array, an aggregate or an address-taken local; alloca) */
	int32_t canary;
	/* a switch on a constant: only its selected case's label is placed (swon, swsel) */
	bool switch_is_constant;
	uint32_t switch_selected_case;
	bool stack_pointer_needed;
} walk_state;

/* x86-64: the calling convention of the call, function or va_list the next WALK_LOWER(classify), WALK_LOWER(call),
   WALK_LOWER(params), WALK_LOWER(va_start) or WALK_LOWER(va_arg) is for (e->msabi; PLAN-win64.md WIN2): fty a function
   type or a pointer to one, 0 the target's own (library helpers).  Set right before each: the
   evaluation of an argument may make other calls in between.  (em_: one copy for both walkers) */
static inline void em_abi(codegen_context *e, ast_qualified_type fty)
{
	e->msabi = CODEGEN_IS_X64(e->compile_context) && (fty ? cc_func_abi(e->compile_context, fty) == CC_FNABI_MS : CODEGEN_IS_WIN64(e->compile_context));
}

enum { WALK_FLOAT_COMPARE_UNORDERED = 0x10000, WALK_FLOAT_COMPARE_ORDERED_NOT_EQUAL };
enum { WALK_ATOMIC_LOAD, WALK_ATOMIC_STORE, WALK_ATOMIC_EXCHANGE, WALK_ATOMIC_ADD, WALK_ATOMIC_COMPARE_EXCHANGE, WALK_ATOMIC_FENCE };
enum { WALK_SLOT_TEMPORARY, WALK_SLOT_LOCAL, WALK_SLOT_PARAMETER, WALK_SLOT_FUNCTION };	/* WALK_SLOT_FUNCTION: lives for the whole function */
#endif

/* the prototypes: again for every instantiation of the walker (proto.h has no guard) */
#include "proto.h"
