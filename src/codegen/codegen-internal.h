#ifndef VCC_CODEGEN_INTERNAL_H
#define VCC_CODEGEN_INTERNAL_H
/*
 * codegen-internal.h: the code generator's shared header: the emitter's types (the unit-wide `emitter`, the
 * per-function `ecx`, values, bindings, the item stream of the object backend), the helpers every
 * backend uses (types, symbols, references, constant folding, the optimizer helpers of opt/), and the
 * prototypes of the files in codegen/.  arm64's a64impl.h is included at its end.
 *
 * Every file of the code generator is compiled into one translation unit (obj/unity/codegen.c), so
 * a shared function is VPRIV (static); every file must still compile alone (`make VCC_UNITY=`).
 * A function here is called from many threads, one ecx each: the emitter's unit-wide tables are
 * shared and either immutable after the prepass, atomic, or under a named mutex (emu, cmu, dmu);
 * everything else of a function lives in its ecx.  The comment below is the design of the direct
 * arm64 emitter; the shared walker (walk/) and VIR (codegen/vir/) reuse its helpers and tables.
 */
/*
 * vcc code generator: arm64 (AAPCS64, Linux ELF) straight from frontend/c/'s AST.
 *
 * No IR, no SSA, no optimisation passes.  What the kernel needs from an
 * -O0-class compiler, and nothing more:
 *
 *   - integer constant folding while generating (every expression yields a
 *     constant, a symbol address + offset, or a register);
 *   - no code behind constant conditions: a reachability flag follows
 *     branches, so if (IS_ENABLED(..)), compiletime_assert, constant
 *     switches and code after return/noreturn calls are not emitted;
 *   - __always_inline calls are expanded with constant arguments bound to
 *     the parameters, so asm "i" operands, __builtin_constant_p and
 *     conditions see constants again (arch_static_branch,
 *     alternative_has_cap_likely); an inlined body whose live returns all
 *     return the same constant is that constant to the caller;
 *   - lazy emission: static functions and objects are emitted only when
 *     emitted code or data refers to them;
 *   - from -O1 on (cc_options.opt), the three items below; at -O0 locals
 *     and inlined params and results live in stack slots;
 *   - the emitted function's most used scalar locals and params whose
 *     address is never needed (prepass: luse, lnoreg) live in callee-saved
 *     registers x28 down (CODEGEN_BINDING_LOCAL_REGISTER, CODEGEN_LVALUE_LOCAL_REGISTER; VCC_LREGS, default 6), the rest
 *     in stack slots; expression temporaries in x9-x15 and the remaining
 *     x19-x28.  An inlined call's params that its body never writes keep
 *     the argument's register (CODEGEN_BINDING_REGISTER), and its result comes back in a
 *     register (fnctx.retreg), not through slots.  Out of temporaries, a
 *     function is generated again without either;
 *   - a b to a label right after it is dropped, and opt.c removes the
 *     register copies this leaves (liveness, copy propagation).
 *
 * The kernel also relies on what clang -O2 folds beyond that: a
 * BUILD_BUG_ON inside a plain static or inline function that only folds
 * once the function is inlined where its arguments are constants.  Such
 * functions are inlined too: plain inline functions that call an error()
 * function directly, and static functions in which an error() call
 * survived folding (need_fold: the TU is generated again with them
 * inlined).  An error() call that still survives is one clang removes with
 * value ranges; it becomes a trap.  __builtin_constant_p follows clang's
 * rules (front-end fold, else 0 with side effects or for pointers, else
 * the operand after inlining).
 *
 * Output goes through a narrow instruction-record interface (struct ins,
 * 16 bytes): codegen appends records per function, a backend turns them
 * into assembly text (assembled by clang -c for verification) or, later,
 * into machine code via the assembler's encoder.  Functions are
 * independent: worker threads take them from a queue, each with its own
 * record buffer, text buffer and binding tables (mmap'd, indexed by 32-bit
 * sym/label/node numbers); chunks are stitched in a fixed order at the end.
 */
#include <pthread.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/uio.h>
#include <time.h>
#include <unistd.h>

#include "frontend/c/ccint.h"
#include "codegen/emit.h"
#include "as/as.h"
#include "obj/obj.h"
#include "driver/driver.h"
#include "port/job-regions.h"
#include "common/text-buffer.h"

#include "identifier-equal.h"

/* functions one file defines and another calls: static when the directory is
   compiled as one TU (VCC_UNITY, the default build), so they stay internal */
#ifndef VPRIV
#ifdef VCC_UNITY
#define VPRIV static
#else
#define VPRIV
#endif
#endif

/* Exact allocation/cell analysis shared by machine-record optimizers. */
#include "rec/slots.h"

#define N(i) (&c->nodes[i])
/* Apple arm64 (Mach-O): '_' names, GOT for symbols outside the TU, variadic
 * arguments on the stack, va_list a char *, stack arguments packed */
#define CODEGEN_IS_DARWIN(c) ((c)->o->target == PP_DARWIN)
/* WebAssembly (wasm64): arch/wasm/codegen/ generates the functions, data-objects.c the data */
#ifdef VCC_WASM
#define WASM(c) PP_IS_WASM((c)->o->target)
#else
#define WASM(c) 0	/* wasm is not built (make VCC_WASM=1): the branches vanish */
#endif
#define WASM32(c) ((c)->o->target == PP_WASM32)
/* x86-64 Linux: codegen/walk/ and arch/x64/codegen/ generate the functions (PLAN-x86.md) */
#define CODEGEN_IS_X64(c) ((c)->o->target == PP_LINUX_X86_64)

/*
 * A C99 inline definition or GNU extern inline: not emitted (another TU
 * has the definition).  The wasm target has no inliner, so it emits a local
 * copy of one it calls, as clang's inlining would make it unneeded.
 */
static inline bool codegen_symbol_is_inline_only(const sym *y)
{
	if (y->kind != AST_SYMBOL_FUNCTION || !(y->flags & AST_SYMBOL_FLAG_DEFINED) || !y->body)
		return false;
	if ((y->flags & AST_SYMBOL_FLAG_GNU_INLINE) && !(y->flags & AST_SYMBOL_FLAG_INLINE_WITHOUT_EXTERN))
		return true;
	return (y->flags & AST_SYMBOL_FLAG_INLINE) && !(y->flags & (AST_SYMBOL_FLAG_GNU_INLINE | AST_SYMBOL_FLAG_EXTERNAL_DECLARATION)) && y->sc != AST_STORAGE_EXTERN && y->sc != AST_STORAGE_STATIC;
}
/* a symbol offset the relocation itself can carry: ELF RELA 32 bits here, a
 * Mach-O ARM64_RELOC_ADDEND 24 bits signed (clang adds the rest in code) */
#define CODEGEN_SYMBOL_OFFSET_FITS(c, off) (CODEGEN_IS_DARWIN(c) ? (off) >= -(1 << 23) && (off) < (1 << 23) : (off) >= INT32_MIN && (off) <= INT32_MAX)
#define T(c, t) AST_TYPE_OF(c, t)
#define KIND(c, t) AST_TYPE_KIND_OF(c, t)

#include "../arch/a64/codegen/instruction.h"
#include "rec/record.h"
#include "flow/flow.h"
#include "flow/values.h"
#include "vir/failure.h"

/* references to things with a symbol: kind in the top 4 bits */
enum { CODEGEN_REFERENCE_SYMBOL = 0, CODEGEN_REFERENCE_STRING = 1, CODEGEN_REFERENCE_LABEL = 2, CODEGEN_REFERENCE_NAME = 3, CODEGEN_REFERENCE_TABLE = 4 };
#define CODEGEN_REFERENCE_MAKE(k, i) (((uint32_t)(k) << 28) | (uint32_t)(i))
#define CODEGEN_REFERENCE_KIND(r) ((r) >> 28)
#define CODEGEN_REFERENCE_INDEX(r) ((r) & 0x0fffffffu)

/* ---------------- emitter state ---------------- */

typedef struct codegen_chunk {
	uint64_t key;		/* output order */
	const char *text;
	size_t length;
	const uint32_t *log;	/* the item's log (ecx.rlog): what a later round needs to reuse it */
	uint32_t log_count;
} codegen_chunk;

/*
 * A round that marks functions for inlining (need_fold) is followed by
 * another; a function whose generation never asked about a function marked
 * since then comes out the same, so the next round reuses its chunk.  The
 * log says what its generation did outside the chunk: references (claims of
 * statics and strings, weak undefined syms), prepass walks (which queue
 * __used static locals), and the static functions whose mark it read.
 */
enum { CODEGEN_LOG_REFERENCE, CODEGEN_LOG_DEPENDENCY, CODEGEN_LOG_WALK };
typedef struct codegen_keep_entry {
	uint64_t key;
	const char *text;
	size_t length;
	const uint32_t *log;
	uint32_t log_count;
} codegen_keep_entry;
typedef struct codegen_keep {
	uint32_t *indexes;		/* per sym: kent index + 1 */
	codegen_keep_entry *entries;
	char *bytes;
	uint32_t *logs;
	size_t index_count, entry_count, byte_count, log_count;
} codegen_keep;

/* analysis bits (prepass, read-only while emitting) */
enum { CODEGEN_FLOAT_FACT_SEEN = 1, CODEGEN_FLOAT_FACT_NONNEGATIVE = 2, CODEGEN_FLOAT_FACT_ZERO = 4, CODEGEN_FLOAT_FACT_BAD = 8 };
enum { CODEGEN_SYMBOL_WRITTEN = 1, CODEGEN_SYMBOL_CLEANUP_FUNCTION = 2, CODEGEN_SYMBOL_MUST_FOLD = 4, CODEGEN_SYMBOL_CONSTANT_WRITTEN = 8, CODEGEN_SYMBOL_CONSTANT_MULTIPLE = 16, CODEGEN_SYMBOL_USED = 32, CODEGEN_SYMBOL_NO_INLINE = 64,
       CODEGEN_SYMBOL_ARRAY_ESCAPES = 128 /* a local array used other than as a[CONST] */ };
/* USED: a local or param read; NOINL: a static local holds a label address (one copy of the code only) */
/* CWRITE: only ever assigned integer constants (all cwval unless CMULTI) */	/* per sym; MUSTFOLD: a function that calls an error() function */
enum { CODEGEN_NODE_HAS_LABEL = 1, CODEGEN_NODE_HAS_ALLOCA = 2 };				/* per node: statement contains a label or case */

typedef struct codegen_call_edge {uint32_t callee,call;} codegen_call_edge;

#define CODEGEN_RECORD_PASS_MAX 24

typedef struct codegen_unit {
	cc *compile_context;
	const cc_options *options;
	_Atomic uint8_t *claimed;	/* per sym: queued for emission */
	_Atomic uint8_t *strings_done;	/* per string: emitted */
	_Atomic uint8_t *symbol_referenced;	/* per sym: referenced (for .weak of undefined syms) */
	_Atomic uint8_t *walked;
	uint16_t *function_node_counts;		/* per function: AST nodes (prepass) */
	uint8_t *inline_parameter_reads; /* parameter references outside a direct (void)param discard */
	uint8_t *once;			/* VIR lowering on: per function, bit 0 when its one reference is a direct call (codegen_function_once_flags), bit 1 when it calls setjmp (prepass); else NULL */
	/* VIR lowering on (with once): the direct call graph the prepass records, per
	   function its size with the callees it inlines (codegen_function_inline_sizes) and its recursion group */
	codegen_call_edge *call_edges;
	uint32_t call_edge_count, call_edge_capacity, *call_edge_offsets, *call_edge_counts, *recursion_groups;
	uint16_t *inline_sizes;
	bool call_graph_recording;			/* the prepass records calls (before the workers start) */
	uint16_t *local_use_weights;			/* per local: uses weighted by loop depth (prepass) */
	uint8_t *local_needs_memory;		/* per local: needs memory (address taken, asm operand, {} init) */
	uint32_t *function_local_span;		/* per function: [2s] lowest, [2s+1] highest local sym seen */
	int local_register_count;			/* locals given callee-saved registers per function (VCC_LREGS) */
	bool cleanups_disabled;			/* VCC_NOOPT: no opt.c clean-ups (to compare) */
	i128 *constant_written_values;		/* per CODEGEN_SYMBOL_CONSTANT_WRITTEN variable: the one constant it is assigned */	/* per function: prepass state (0 no, 1 running, 2 done) */
	uint8_t *symbol_flags;		/* SA_* per sym */
	uint8_t *float_facts;		/* FF_* per floating local: what every write stores */
	uint8_t *node_flags;		/* NA_* per node */
	uint8_t *label_flags;			/* per label: LR_* */
	uint16_t *label_cleanup_depth;		/* per label: cleanup depth at its definition */
	/* work queue (syms) */
	pthread_mutex_t queue_mutex;
	pthread_cond_t queue_condition;
	uint32_t *queue;
	uint32_t queue_head, queue_tail;
	int busy_count, thread_count, done;
	/* output */
	pthread_mutex_t chunk_mutex;
	codegen_chunk *chunks;
	uint32_t chunk_count;
	_Atomic int errors;
	_Atomic uint64_t record_total, function_total, object_total, inline_total;
	_Atomic uint64_t print_nanoseconds, walk_nanoseconds, walk_total;
	uint8_t *inline_everywhere;			/* per sym: static function to inline everywhere (kept across rounds) */
	_Atomic uint32_t newly_marked_count;		/* marked this round */
	_Atomic uint32_t nerrcall;	/* error() calls dropped (not folded) */	/* VCC_EMIT_STATS: time in finish_function */
	const codegen_keep *previous_keep;			/* the previous round's functions, or NULL */
	bool stats;
	/* VCC_VIR_STATS: functions through VIR and the reasons it gave up on one (codegen/vir/build-attempt.c) */
	bool vir_statistics;
	_Atomic uint64_t vir_attempted, vir_lowered, vir_built;	/* vbuilt: built but not lowered (VCC_VIR without l) */
	struct {
		_Atomic uint32_t count;
		uint32_t sym;		/* the first function that failed so (under emu) */
		char detail[96];	/* ... and what the reason leaves out */
	} vir_failures[VIRBUILD_FAILURE_COUNT];
	/* VCC_REC_STATS: what each record clean-up pass did, [0 direct, 1 VIR][pass][runs, changed, removed, added] (func.c) */
	bool record_statistics;
	_Atomic uint64_t record_pass_counts[2][CODEGEN_RECORD_PASS_MAX][4];
	bool object_text;			/* VCC_OBJ_TEXT: objects through the printer and as_run (checks the object backend) */
	pthread_mutex_t debug_mutex;		/* -g: the file table */
	char **debug_files;			/* .file N (1-based) names */
	uint32_t debug_file_count, debug_file_capacity;
	pthread_mutex_t diagnostic_mutex;		/* diagnostics */
	uint32_t errcall[256];		/* error() call sites already reported (under emu) */
	uint32_t nerrcall_loc;
} codegen_unit;

typedef struct codegen_value {
	uint8_t kind;		/* V_* */
	uint8_t register_number, register_high;		/* CODEGEN_VALUE_REGISTER: r; CODEGEN_VALUE_REGISTER_PAIR: r (low), r2 (high) */
	uint32_t reference;		/* CODEGEN_VALUE_ADDRESS */
	i128 integer;			/* CODEGEN_VALUE_CONSTANT value / CODEGEN_VALUE_ADDRESS addend */
} codegen_value;

enum { CODEGEN_VALUE_NONE, CODEGEN_VALUE_CONSTANT, CODEGEN_VALUE_ADDRESS, CODEGEN_VALUE_REGISTER, CODEGEN_VALUE_REGISTER_PAIR };

/* per-sym binding inside a function instance */
enum { B_NONE, CODEGEN_BINDING_SLOT, CODEGEN_BINDING_CONSTANT, CODEGEN_BINDING_INDIRECT, CODEGEN_BINDING_REGISTER, CODEGEN_BINDING_LOCAL_REGISTER };
/* CODEGEN_BINDING_REGISTER: an inlined call's read-only param in temporary v.r; CODEGEN_BINDING_LOCAL_REGISTER: a local or param of the
   emitted function that lives in callee-saved register v.r for the whole function */

/* per user label (prepass): referenced, defined so far, target of a jump back (or &&) */
enum { CODEGEN_LABEL_REFERENCED = 1, CODEGEN_LABEL_DEFINED = 2, CODEGEN_LABEL_BACKWARD = 4 };

typedef struct codegen_binding {
	uint32_t instance;
	uint8_t kind;
	uint8_t register_number;		/* register variable: asm("xN") (0xff: none) */
	uint8_t known_value_index;		/* known-value tracking: index + 1 (0: untracked) */
	int64_t slot;		/* CODEGEN_BINDING_SLOT, CODEGEN_BINDING_INDIRECT */
	codegen_value value;			/* CODEGEN_BINDING_CONSTANT */
} codegen_binding;

typedef struct codegen_label {
	uint32_t instance, id;
} codegen_label;

#define CODEGEN_KNOWN_VALUE_MAX 16
#define CODEGEN_KNOWN_VALUES_CAPACITY (1u << 18)
/* lkvi once the label is placed (or dropped): a later edge to it is a jump back */
enum { CODEGEN_KNOWN_LABEL_PLACED = UINT32_MAX - 2, CODEGEN_KNOWN_LABEL_KNOWN = UINT32_MAX - 1, CODEGEN_KNOWN_LABEL_DROPPED = UINT32_MAX };
typedef struct codegen_known_values {
	uint16_t valid;
	int64_t values[CODEGEN_KNOWN_VALUE_MAX];
} codegen_known_values;

#define CODEGEN_NESTING_CAPACITY (1u << 22)	/* loops/switches and cleanups in scope */
typedef struct codegen_loop {
	uint32_t break_label, continue_label;	/* local labels (cont 0: switch) */
	int cleanup_depth;
} codegen_loop;

typedef struct codegen_function_instance {		/* one function instance: the emitted function or an inlined body */
	uint32_t function, instance, call;
	uint32_t return_label;
	int64_t return_slot;
	uint8_t return_register;		/* inlined: the result's temporary + 1 (0: in retslot) */
	ast_qualified_type return_type;
	bool is_inlined;
	bool constant_is_ok, any_ret;
	codegen_value constant;
	int cleanup_base;		/* cleanup stack depth at entry */
	int loop_depth;		/* loop stack depth at entry */
	struct codegen_function_instance *outer;
} codegen_function_instance;

#define CODEGEN_TEMPORARY_COUNT 17
#define CODEGEN_LOCAL_REGISTER_MAX 10	/* x19-x28 */
#define CODEGEN_LOCAL_REGISTERS_DEFAULT 6	/* the rest stay expression temporaries */
#define CODEGEN_INLINE_FREE_FAST 5
#define CODEGEN_INLINE_FREE_SAFE 8

/* a stack slot handed out by slot() (offsets are reused across scopes) */
typedef struct codegen_slot_allocation {
	int64_t offset;
	uint64_t size;
} codegen_slot_allocation;
#define CODEGEN_SLOT_ALLOCATION_CAPACITY (1u << 20)
/*
 * At -O1 and up a record addressing a frame slot (A64_OP_LD/A64_OP_ST, add/sub
 * immediate with rn A64_REGISTER_FRAME) names the live allocation it is inside in
 * rm | ra << 8 (index + 1; 0: none found, or past 65535).  Slots are
 * reused once a scope ends, so this tells opt.c which accesses belong to
 * one object.  An address at the start of an allocation that is also just
 * past the end of the live one below names that one too, in cond | sh << 8
 * (CODEGEN_SLOT_TAG_SECOND).  finish_function clears the tags before the frame layout.
 */
#define CODEGEN_SLOT_TAG(i) ((uint32_t)(i)->rm | (uint32_t)(i)->ra << 8)
#define CODEGEN_SLOT_TAG_SECOND(i) ((uint32_t)(i)->cond | (uint32_t)(i)->sh << 8)

#define CODEGEN_VLA_SIZE_MAX 64		/* VLA size expressions live at once in a function */

typedef struct codegen_context {
	codegen_unit *unit;
	cc *compile_context;
	/* records of the function being emitted */
	a64_record *records;
	uint32_t record_count, record_capacity;
	codegen_slot_allocation *slot_allocations;		/* the function's slot() allocations */
	uint32_t slot_allocation_count;
	uint32_t *live_slots, live_slot_count;	/* the live ones (indices into sla, deepest last) */
	int volatile_untag_depth;		/* inside a volatile access: its records stay untagged */
	uint32_t current_location;	/* innermost statement being generated (diagnostics) */
	uint32_t reuse_from, reuse_to; /* temporary substitution within one pure expression */
	struct codegen_debug_row { uint32_t record_index, file, line, column; } *debug_rows;	/* -g: a row at record k */
	uint32_t debug_row_count, debug_row_capacity;
	const char *debug_last_file_name;	/* the last file looked up, and its number */
	uint32_t debug_last_file_name_length, debug_last_file_number;
	tbuf raw;		/* inline asm text */
	tbuf out;		/* this thread's output text */
	tbuf data;		/* scratch: data object bytes */
	struct codegen_data_relocation { uint64_t offset; uint32_t reference, sequence; int64_t addend; uint32_t subtrahend, size_bytes; } *relocations;	/* sub: ref subtracted (0: none); sz 4 or 8 (0: 8) */
	uint32_t relocation_count;
	uint64_t relocation_end;	/* end of the highest relocated word */
	/* bindings */
	codegen_binding *bindings;		/* per sym */
	codegen_label *label_map;		/* per label */
	uint32_t *case_labels;	/* per node (AST_NODE_CASE / AST_NODE_DEFAULT): local label */
	uint32_t *label_reference_counts;	/* per local label: branches to it; CODEGEN_LABEL_EXTERNAL: also reached from outside the records */
	uint32_t label_count, label_capacity;
	/*
	 * Known values of locals only ever assigned constants (CODEGEN_SYMBOL_CONSTANT_WRITTEN):
	 * forward edges carry them to their label, which keeps what all
	 * incoming edges agree on; loop heads and unknown targets drop them.
	 */
	uint8_t known_value_count;
	uint16_t known_values_valid;
	int64_t known_values[CODEGEN_KNOWN_VALUE_MAX];
	/* elements a[CONST] of local arrays that do not escape (no CODEGEN_SYMBOL_ARRAY_ESCAPES), tracked like those locals */
	uint8_t array_element_count;
	struct { uint32_t symbol; uint32_t index; uint8_t known_value_index; } array_elements[CODEGEN_KNOWN_VALUE_MAX];
	uint32_t *label_known_values_index;		/* per local label: kvst index + 1 (0: none) */
	codegen_known_values *known_value_states;
	uint32_t known_value_state_count;
	uint32_t next_instance;
	/* frame */
	int64_t frame_offset, frame_maximum;
	uint32_t frame_alignment;	/* largest slot alignment; over 16 the prologue realigns sp */
	/* VLA sizes, evaluated where their declaration is reached (walk/statement.c vla_fix) */
	struct { uint32_t node, instance; int64_t slot; } vsz[CODEGEN_VLA_SIZE_MAX];
	uint32_t vla_size_count;
	int64_t compound_literal_top;		/* frame offset above the block's compound literals (kept past statements) */
	int64_t outgoing_argument_bytes_max;		/* outgoing stack argument bytes */
	bool dynamic_frame, has_calls, is_variadic;
	bool discarded_scalar;	/* lowering exposed an already-emitted unused scalar result */
	int64_t variadic_register_save_slot;		/* variadic register save area */
	int named_general_register_count, named_stack_bytes;	/* variadic: named GPR args, named stack bytes */
	int named_vector_register_count;
	int64_t variadic_vector_save_slot;		/* variadic (Linux): named FP/SIMD args, the v0-v7 save area */
	int next_vector_register;		/* classify: next SIMD/FP argument register */
	/* temporaries */
	uint32_t temporaries_in_use;		/* bit i: tempregs[i] in use */
	uint32_t temporaries_reserved;		/* ... of those, never handed out: CODEGEN_BINDING_LOCAL_REGISTER variables', x28 once falign > 16 */
	int inline_free_temporaries;		/* free temporaries an inlined call keeps (0: CODEGEN_INLINE_FREE_SAFE) */
	int spill;		/* last try: a binary operator's left value waits in a frame slot */
	int local_register_candidate_count;		/* CODEGEN_BINDING_LOCAL_REGISTER candidates of the emitted function: sym, register */
	uint32_t local_register_symbols[CODEGEN_LOCAL_REGISTER_MAX];
	uint8_t local_register_numbers[CODEGEN_LOCAL_REGISTER_MAX];
	uint32_t callee_saved_used;	/* callee-saved registers used (bit = x number) */
	/* control */
	bool is_reachable;
	int dry_run_depth;		/* inside dry(): code is generated and thrown away, no references */
	codegen_loop *loops;		/* (nesting is bounded by the node count: reserved for it) */
	int loop_count;
	uint32_t *cleanups;	/* syms with cleanup, innermost last */
	int cleanup_count;
	codegen_function_instance *function_instance;
	int inline_depth;
	uint32_t top_symbol;		/* sym being emitted */
	uint32_t asm_statement_count;		/* inline asm statements emitted in it so far (%=) */
	uint32_t *cases;	/* scratch for switch case collection */
	uint32_t *weak_references;	/* weak undefined syms this thread referenced first */
	uint32_t weak_reference_count;
	uint32_t case_count;
	uint32_t *jump_table_labels, jump_table_label_count, jump_table_label_capacity;	/* jump tables of the function: case labels, table after table */
	struct codegen_jump_table { uint32_t table_label, first_entry, entry_count; } *jump_tables;	/* table label, first entry in jtl, entries */
	uint32_t jump_table_count, jump_table_capacity;
	uint32_t *round_log;		/* this round's item logs: (RL_*, value) pairs */
	size_t round_log_count, round_log_item_start;	/* ... and where the current item's starts */
	jmp_buf bail_jump;
	struct wst *wasm_state;		/* WASM: arch/wasm/codegen/ state (wimpl.h) */
	struct walk_state *walk_state;	/* X86: the walker's state (codegen/walk/walk.h) */
	struct xst *x64_context;	/* X86: arch/x64/codegen/ state (x64impl.h) */
	struct virbuild_context *vir_builder;	/* the VIR builder's state (codegen/vir/, PLAN-vir.md) */
	bool vir_try;		/* the VIR walker runs: an error is not reported, it makes the function fall back */
	bool vir_lowering;		/* the records are VIR's lowering (finish_function's clean-ups for it) */
	const char *vir_failure_reason;	/* ... and why (NULL: it did not fail) */
	struct em_switch_values_data *switch_values; /* accepted tables of this real function */
} codegen_context;
enum { CODEGEN_LIBRARY_MEMCPY, CODEGEN_LIBRARY_MEMSET, CODEGEN_LIBRARY_MEMMOVE, CODEGEN_LIBRARY_UDIVTI3, CODEGEN_LIBRARY_DIVTI3, CODEGEN_LIBRARY_UMODTI3, CODEGEN_LIBRARY_MODTI3, CODEGEN_LIBRARY_STRLEN,
       CODEGEN_LIBRARY_ASHLTI3, CODEGEN_LIBRARY_LSHRTI3, CODEGEN_LIBRARY_ASHRTI3, CODEGEN_LIBRARY_FLOATTISF, CODEGEN_LIBRARY_FLOATTIDF, CODEGEN_LIBRARY_FLOATUNTISF, CODEGEN_LIBRARY_FLOATUNTIDF,
       CODEGEN_LIBRARY_FIXSFTI, CODEGEN_LIBRARY_FIXDFTI, CODEGEN_LIBRARY_FIXUNSSFTI, CODEGEN_LIBRARY_FIXUNSDFTI,
       CODEGEN_LIBRARY_ADDTF3, CODEGEN_LIBRARY_SUBTF3, CODEGEN_LIBRARY_MULTF3, CODEGEN_LIBRARY_DIVTF3, CODEGEN_LIBRARY_EQTF2, CODEGEN_LIBRARY_NETF2, CODEGEN_LIBRARY_LTTF2, CODEGEN_LIBRARY_LETF2, CODEGEN_LIBRARY_GTTF2, CODEGEN_LIBRARY_GETF2, CODEGEN_LIBRARY_UNORDTF2,
       CODEGEN_LIBRARY_EXTENDDFTF2, CODEGEN_LIBRARY_EXTENDSFTF2, CODEGEN_LIBRARY_TRUNCTFDF2, CODEGEN_LIBRARY_TRUNCTFSF2, CODEGEN_LIBRARY_FLOATSITF, CODEGEN_LIBRARY_FLOATDITF, CODEGEN_LIBRARY_FLOATUNSITF,
       CODEGEN_LIBRARY_FLOATUNDITF, CODEGEN_LIBRARY_FLOATTITF, CODEGEN_LIBRARY_FLOATUNTITF, CODEGEN_LIBRARY_FIXTFSI, CODEGEN_LIBRARY_FIXTFDI, CODEGEN_LIBRARY_FIXUNSTFSI, CODEGEN_LIBRARY_FIXUNSTFDI,
       CODEGEN_LIBRARY_FIXTFTI, CODEGEN_LIBRARY_FIXUNSTFTI, CODEGEN_LIBRARY_EXTENDXFTF2, CODEGEN_LIBRARY_TRUNCTFXF2, CODEGEN_LIBRARY_EXTENDBFSF2, CODEGEN_LIBRARY_TRUNCSFBF2, CODEGEN_LIBRARY_TRUNCDFBF2, CODEGEN_LIBRARY_TRUNCXFBF2, CODEGEN_LIBRARY_TRUNCTFBF2, CODEGEN_LIBRARY_EXTENDHFSF2, CODEGEN_LIBRARY_TRUNCSFHF2, CODEGEN_LIBRARY_TRUNCDFHF2, CODEGEN_LIBRARY_TRUNCXFHF2, CODEGEN_LIBRARY_TRUNCTFHF2,
       CODEGEN_LIBRARY_FLOATTIXF, CODEGEN_LIBRARY_FLOATUNTIXF, CODEGEN_LIBRARY_FIXXFTI, CODEGEN_LIBRARY_FIXUNSXFTI,
       CODEGEN_LIBRARY_FLOORF, CODEGEN_LIBRARY_CEILF, CODEGEN_LIBRARY_TRUNCF, CODEGEN_LIBRARY_ROUNDF, CODEGEN_LIBRARY_RINTF, CODEGEN_LIBRARY_NEARBYINTF, CODEGEN_LIBRARY_FABSF };
#define CODEGEN_FIXED_REFERENCE(k) CODEGEN_REFERENCE_MAKE(CODEGEN_REFERENCE_NAME, 0x8000000u + (k))

/* ---------------- object backend: records -> machine code ---------------- */

/*
 * With cc_options.emit == 2 a chunk is a stream of items instead of text:
 *
 *   CODEGEN_ITEM_TEXT   directives (section switches, .globl/.type/.size, aliases,
 *             top-level asm), run by the TU's assembler (as_snippet_assemble);
 *   CODEGEN_ITEM_CODE   a function's records (prologue and epilogue included) and its
 *             inline asm text, encoded here with a64.h straight into the
 *             current frag; asm statements go through as_snippet_assemble in place;
 *   CODEGEN_ITEM_X64    x86 records, using the assembler encoder and relaxation;
 *             unhandled records, prologues, CFI and DWARF remain CODEGEN_ITEM_TEXT.
 *   CODEGEN_ITEM_DATA   an object's bytes and relocations; CODEGEN_ITEM_LABEL + CODEGEN_ITEM_BYTES: a
 *             string literal.
 *
 * Replaying the chunks in output order makes exactly the ElfObj calls the
 * assembler makes for the text the printer would have written (sections,
 * symbol registration order, mapping symbols, fixups), so the object is
 * byte-identical with the text path; bench/vcc-asdiff.py checks that.
 * Branches inside a function without inline asm are resolved here; with
 * inline asm (which may name the labels: asm goto) labels are .LBB symbols.
 */
enum { CODEGEN_ITEM_TEXT = 1, CODEGEN_ITEM_CODE, CODEGEN_ITEM_DATA, CODEGEN_ITEM_LABEL, CODEGEN_ITEM_BYTES, CODEGEN_ITEM_SYMBOL_HEADER, CODEGEN_ITEM_FUNCTION_END, CODEGEN_ITEM_OBJECT_SIZE, CODEGEN_ITEM_X64 };
typedef struct codegen_item { uint32_t kind, length; } codegen_item;	/* len payload bytes follow, padded to 8 */
typedef struct codegen_code_header { uint32_t function_id, record_count, raw_count, label_count, symbol_labels, padding; } codegen_code_header;
typedef struct codegen_data_header { uint64_t size, count; uint32_t relocation_count, zero; } codegen_data_header;
typedef struct codegen_item_relocation { uint64_t offset; uint32_t reference, sequence; int64_t addend; uint32_t subtrahend, size_bytes; } codegen_item_relocation;
/*
 * A symbol's header after its section directive, what the assembler does
 * for ".globl/.weak s; .hidden/.protected s; .p2align p2; .type s,@function
 * or @object; s:" (CODEGEN_ITEM_SYMBOL_HEADER), for ".Lfe<s>: .size s, .Lfe<s>-s" (CODEGEN_ITEM_FUNCTION_END)
 * and ".size s, n" (CODEGEN_ITEM_OBJECT_SIZE), without the text.
 */
typedef struct codegen_symbol_header { uint32_t symbol; uint8_t bind, visibility, alignment_log2, is_function; } codegen_symbol_header;	/* bind: 0, STB_GLOBAL, STB_WEAK */
typedef struct codegen_object_size { uint32_t symbol, padding; uint64_t size; } codegen_object_size;

/* replay state: one per TU */
typedef struct codegen_commit_context {
	cc *compile_context;
	as_context *assembler;
	ElfObj *object;
	uint32_t *symbol_cache, *string_cache, *label_cache;	/* ref -> ElfSym (0: not yet) */
	uint32_t *label_word_offsets;			/* direct labels: word offset in the function */
	uint32_t *word_buffer;			/* direct functions: the words, before they go to the frag */
	struct codegen_commit_patch *patches;		/* ... and the records with a fixup */
	uint32_t *label_symbols, *label_serials, serial;	/* functions with inline asm: label -> .LBB symbol (valid if lser == ser) */
	struct codegen_commit_label *labels;		/* ... label state */
	struct codegen_commit_pending_branch *pending;		/* ... branches waiting for their label */
	tbuf name_buffer;			/* name scratch */
} codegen_commit_context;

/* a pending fixup on word k of an instruction */
typedef struct codegen_commit_fixup { uint8_t word_index, label_flag; uint32_t type, reference; int64_t offset; } codegen_commit_fixup;
typedef struct codegen_commit_patch { uint32_t record_index, word_position; codegen_commit_fixup fixup; uint8_t is_relaxed; } codegen_commit_patch;	/* record k at word at; lng: relaxed (a64_commit_relax) */
typedef struct codegen_commit_label { uint32_t serial, flags, fragment, offset, pending; } codegen_commit_label;
typedef struct codegen_commit_pending_branch { uint32_t next, record_index, fragment, offset; uint8_t *bytes; } codegen_commit_pending_branch;	/* a branch waiting for its label */

/* ---------------- lvalues ---------------- */

enum { CODEGEN_LVALUE_FRAME, CODEGEN_LVALUE_SYMBOL, CODEGEN_LVALUE_REGISTER, CODEGEN_LVALUE_GLOBAL_REGISTER, CODEGEN_LVALUE_LOCAL_REGISTER, CODEGEN_LVALUE_VARIABLE };
/* CODEGEN_LVALUE_LOCAL_REGISTER: a CODEGEN_BINDING_LOCAL_REGISTER variable, value in r (no address).
   CODEGEN_LVALUE_VARIABLE: a variable of a virtual lowering target (PLAN-vir.md, SPEC-vir.md
   X3), its number in lval.ref, no address.  Only that target makes one; the
   real targets treat it as an internal error. */

typedef struct codegen_lvalue {
	uint8_t kind;
	uint8_t register_number;		/* CODEGEN_LVALUE_REGISTER: base register (a temp, owned); CODEGEN_LVALUE_GLOBAL_REGISTER: the register */
	uint8_t is_bitfield;		/* bit-field */
	uint8_t is_signed;		/* bit-field signedness */
	int64_t dynamic_bit_slot;	/* packed vector lane: saved dynamic bit index, 0 for static */
	uint16_t bit_offset, width;	/* bit-field: bits from the byte at off */
	uint32_t reference;		/* CODEGEN_LVALUE_SYMBOL */
	int64_t offset;		/* byte offset (CODEGEN_LVALUE_FRAME: slot offset) */
	uint32_t align;		/* known alignment of the address */
} codegen_lvalue;

enum { A64_ABI_ARGUMENT_IGNORE, A64_ABI_ARGUMENT_REGISTER, A64_ABI_ARGUMENT_STACK, A64_ABI_ARGUMENT_REFERENCE_REGISTER, A64_ABI_ARGUMENT_REFERENCE_STACK, A64_ABI_ARGUMENT_VECTOR_REGISTER, A64_ABI_ARGUMENT_HFA, A64_ABI_ARGUMENT_BOOL_LANES };	/* A64_ABI_ARGUMENT_VECTOR_REGISTER: float/double in v<reg>; A64_ABI_ARGUMENT_HFA: nreg members of fsz bytes in v<reg>.. */

typedef struct a64_abi_argument {
	uint8_t kind, register_number, register_count, float_member_size;
	int32_t stack_offset;		/* A64_ABI_ARGUMENT_STACK / A64_ABI_ARGUMENT_REFERENCE_STACK: offset in the argument area */
	uint32_t size;
} a64_abi_argument;

/* ---------------- spilling caller-saved temporaries around calls ---------------- */

/* ins.sh of the A64_OP_ST/A64_OP_LD pair a64_abi_temporaries_spill/unspill put around a call (opt.c drops dead ones) */
#define A64_SPILL_RECORD 1

typedef struct a64_abi_spill {
	uint32_t mask;
	int64_t slots[CODEGEN_TEMPORARY_COUNT];
} a64_abi_spill;

#define CODEGEN_LOG_CAPACITY ((size_t)1 << 28)

/* ---------------- calls ---------------- */

typedef struct a64_call_argument {
	codegen_value value;
	ast_qualified_type type;
	a64_abi_argument argument;
	int64_t slot;		/* value parked in a frame slot (-1: v is a constant/address) */
} a64_call_argument;

/* ---------------- across files ---------------- */

/* opt/switch-values.c: bounded integer-result tables, no AST mutation. */
typedef struct em_switch_values_recipe {
 i128 lo;
 uint64_t values[256], fallback;
 ast_qualified_type ty;
 uint32_t destination;
 uint16_t slots;
 uint8_t selector_bits, element_bytes;
 bool is_return;
} em_switch_values_recipe;
VPRIV bool em_switch_values_plan(cc *c, uint32_t n, em_switch_values_recipe *r);
VPRIV bool em_switch_values_queue(codegen_context *e, uint32_t n, const em_switch_values_recipe *r);
VPRIV void em_switch_values_reset(codegen_context *e);
VPRIV void em_switch_values_emit(codegen_context *e);
VPRIV bool a64_switch_values(codegen_context *e, uint32_t n);
VPRIV bool walk_switch_values_emit(codegen_context *e, uint32_t n);
VPRIV bool wswitch_values(codegen_context *e, uint32_t n);
/* tu.c */
VPRIV void *codegen_memory_reserve(size_t n);
VPRIV double codegen_clock_seconds(void);
/* type.c */
static inline ast_qualified_type codegen_type_primitive(int k);
static inline bool codegen_kind_is_integer(int k);
static inline bool codegen_type_is_integer(cc *c, ast_qualified_type t);
static inline bool codegen_type_is_pointer(cc *c, ast_qualified_type t);
static inline bool codegen_type_is_float(cc *c, ast_qualified_type t);
static inline bool codegen_type_is_record(cc *c, ast_qualified_type t);
static inline bool codegen_type_is_array(cc *c, ast_qualified_type t);
static inline bool codegen_type_is_function(cc *c, ast_qualified_type t);
static inline bool codegen_type_is_void(cc *c, ast_qualified_type t);
static inline bool codegen_type_is_vector(cc *c, ast_qualified_type t);
static inline bool codegen_type_is_record_like(cc *c, ast_qualified_type t);
static inline bool codegen_type_is_aggregate(cc *c, ast_qualified_type t);
static inline uint64_t codegen_type_size(cc *c, ast_qualified_type t);
static inline uint32_t codegen_type_alignment(cc *c, ast_qualified_type t);
static inline bool codegen_type_is_signed(cc *c, ast_qualified_type t);
static inline bool codegen_type_is_long_double(cc *c, ast_qualified_type t);
static inline bool codegen_type_is_wide(cc *c, ast_qualified_type t);
VPRIV i128 codegen_integer_truncate(cc *c, i128 v, ast_qualified_type t);
/* opt/divpow2.c */
VPRIV bool em_divpow2(cc *c, ast_qualified_type ty, int *op, i128 *rhs);
/* opt/sdivpow2.c: bias negative dividends to preserve truncation toward zero. */
typedef struct em_sdivpow2_recipe {
	uint8_t bits, shift;
	bool negative, remainder;
} em_sdivpow2_recipe;
VPRIV bool em_sdivpow2(cc *c, ast_qualified_type ty, int op, i128 rhs, em_sdivpow2_recipe *r);
/* opt/mulconst.c: shift for x + (x << shift), or zero to retain multiply. */
VPRIV unsigned em_mulconst(unsigned bits, i128 rhs);

/* opt/udivconst.c: unsigned32/64 reciprocal; quotient = high_bits(n*magic),
 * optional ((n-q)>>1)+q correction, then shift. compare means q=(n>=d). */
typedef struct em_udivconst_recipe {
	uint64_t divisor, magic;
	uint8_t bits, shift;
	bool add, compare, remainder;
} em_udivconst_recipe;
VPRIV bool em_udivconst32(uint32_t divisor, bool remainder, em_udivconst_recipe *r);
VPRIV bool em_udivconst64(uint64_t divisor, bool remainder, em_udivconst_recipe *r);
VPRIV bool em_udivconst(cc *c, ast_qualified_type ty, int op, i128 rhs, em_udivconst_recipe *r);
VPRIV bool a64_udivconst_ok(codegen_context *e, codegen_value a, const em_udivconst_recipe *r);
VPRIV codegen_value a64_udivconst(codegen_context *e, codegen_value a, const em_udivconst_recipe *r);
/* opt/sdivconst.c: reciprocal for a positive divisor magnitude. Signed high
 * product, optional numerator addition, arithmetic shift, then sign correction. */
typedef struct em_sdivconst_recipe {
	uint64_t divisor;
	int64_t magic;
	uint8_t bits, shift;
	bool add, negative, remainder;
} em_sdivconst_recipe;
VPRIV bool em_sdivconst32(int32_t divisor, bool remainder, em_sdivconst_recipe *r);
VPRIV bool em_sdivconst64(int64_t divisor, bool remainder, em_sdivconst_recipe *r);
VPRIV bool em_sdivconst(cc *c, ast_qualified_type ty, int op, i128 rhs, em_sdivconst_recipe *r);
VPRIV bool a64_sdivconst_ok(codegen_context *e, codegen_value a, const em_sdivconst_recipe *r);
VPRIV codegen_value a64_sdivconst(codegen_context *e, codegen_value a, const em_sdivconst_recipe *r);
/* opt/mulshift.c and opt/xor-not.c: machine recipes, no operand evaluation. */
VPRIV bool em_bound_constant(cc *c, uint32_t n, i128 *value);
VPRIV int em_cmpbounds(codegen_context *e, uint32_t n, uint32_t *operand);
VPRIV int em_repeated_integer(codegen_context *e, uint32_t n);
VPRIV bool em_sibling_values(codegen_context *e, uint32_t n);
VPRIV bool em_decl_reuse_values(codegen_context *e, uint32_t decl, uint32_t consumer, const uint32_t *killed, uint32_t nkilled, uint32_t *from, uint32_t *to);
VPRIV bool em_block_value_reuse(codegen_context *e, const node *block, uint32_t first, uint32_t i, uint32_t *from, uint32_t *to);
VPRIV uint32_t em_double_unary(codegen_context *e, uint32_t n);

VPRIV bool em_cmpzero(codegen_context *e, uint32_t n, uint32_t *operand);
VPRIV bool em_mulshift(cc *c, ast_qualified_type ty, int *op, i128 *rhs);
VPRIV bool em_xor_not(cc *c, ast_qualified_type ty, int op, i128 rhs);
/* opt/identity.c */
enum { EM_ID_NONE, EM_ID_LEFT, EM_ID_ZERO, EM_ID_ONES, EM_ID_TRUE };
VPRIV int em_bin_identity(cc *c, ast_qualified_type ty, int op, i128 rhs);
/* opt/switch.c: inclusive ranges in controlling-type order. Preparation only
 * for tree dispatch (>=8 cases); no allocation or target state in the policy.
 * Preparation rejects wrapped/overlapping ranges: callers retain the chain. */
typedef struct em_switch_case {
	i128 lo, hi;
	uint32_t target;
} em_switch_case;
enum { EM_SWITCH_LINEAR, EM_SWITCH_TREE, EM_SWITCH_TABLE };
VPRIV int em_switch_strategy(uint32_t n, i128 lo, i128 hi, bool ranges, bool tables);
VPRIV bool em_switch_prepare(em_switch_case *cases, uint32_t n, unsigned bits, bool sg);
/* One bounded membership test for a run of stacked case labels. The original
 * body remains intact; node names its final case label, not a new destination. */
typedef struct em_switch_bitset_recipe {
	i128 lo;
	uint64_t mask;
	uint32_t node;
	uint8_t span, bits;
} em_switch_bitset_recipe;
VPRIV bool em_switch_bitset_plan(cc *c, uint32_t body, uint32_t nlabels,
				 unsigned bits, bool sg, em_switch_bitset_recipe *out);
VPRIV bool a64_switch_bitset(codegen_context *e, uint32_t body, uint32_t base, int r,
			    int cx, bool sg, uint32_t dflt);
/* opt/unusedinit.c: bounded recipes for never-referenced integer declarations. */
enum { EM_UNUSED_KEEP, EM_UNUSED_OMIT, EM_UNUSED_EVAL };
VPRIV bool em_unused_local(codegen_context *e, uint32_t s, uint32_t fn);
VPRIV unsigned em_unused_init(codegen_context *e, const node *decl, uint32_t fn, unsigned budget);
/* opt/locals.c: eligibility and bounded selection from the existing prepass. */
/* opt/tail.c: exact direct forwarding wrappers; target capability/ABI checks follow. */
VPRIV bool em_tail_caller_ok(codegen_context *e, uint32_t fn);
VPRIV bool em_tail_target_ok(codegen_context *e, uint32_t s);
VPRIV bool em_tail_candidate(codegen_context *e, uint32_t fn, uint32_t expr, uint32_t maxargs, uint32_t *callee);
VPRIV bool a64_tail_return(codegen_context *e, uint32_t expr);
VPRIV uint32_t a64_tail_ref(const codegen_context *e);
VPRIV bool em_local_eligible(codegen_context *e, uint32_t s);
VPRIV bool em_local_gpr_type(cc *c, ast_qualified_type ty);
VPRIV unsigned em_select_locals(codegen_context *e, uint32_t fn, uint32_t *cand, unsigned max,
			       bool (*target_ok)(cc *, ast_qualified_type));
/* rec/fallthrough.c: opaque records; no shared struct layout is assumed. */
typedef struct record_layout {
	size_t stride, op_offset, label_offset;
	uint8_t jump_op, label_op;
} record_layout;
VPRIV uint32_t record_fallthrough_drop(void *records, uint32_t count, const record_layout *layout,
				void (*remap)(void *, uint32_t, uint32_t), void *ctx);
/* rec/unreachable.c: unknown opcodes and opaque records are entry barriers. */
VPRIV uint32_t record_unreachable_remove(void *records, uint32_t count, size_t stride, size_t op_offset,
				const uint8_t *flow, size_t nflow,
				void (*remap)(void *, uint32_t, uint32_t), void *ctx);
/* rec/copies.c: inclusive block end, whole-register t/v below 64. The target
 * preflights each rewritten record without mutation (apply=false); applying
 * the same rewrite must then succeed. Existing conservative liveness is only
 * enlarged, and no effects/liveness table is built by this scan.
 */
typedef struct record_copy_scan {
	void *records;
	size_t stride, op_offset;
	uint8_t deleted_op;
	const void *flags;
	size_t flags_stride;
	uint16_t opaque_mask;
	uint64_t *read, *live;
	const uint64_t *write;
	/* Optional on-demand effects. False is an opaque boundary. With no live
	 * array a copy must reach an explicit overwrite; block end proves nothing.
	 */
	bool (*inspect)(void *ctx, uint32_t j, uint64_t *read, uint64_t *write);
	bool (*rewrite)(void *ctx, uint32_t j, uint8_t from, uint8_t to, bool apply);
	void *ctx;
} record_copy_scan;
VPRIV bool record_copy_forward(const record_copy_scan *s, uint32_t k, uint32_t end, uint8_t t, uint8_t v);
/* logical-immediate.c */
extern const char a64_condition_names[][8];
VPRIV bool a64_logical_immediate_is_encodable(uint64_t v, int x);
static inline void a64_register_name_append(tbuf *b, int r, int x);
/* tu.c */
extern const uint8_t codegen_temporary_registers[CODEGEN_TEMPORARY_COUNT];

/* the tempregs index of x<r>, or -1 */
static inline int codegen_temporary_index(int r)
{
	static const int8_t tidx_of[34] = {
		-1, -1, -1, -1, -1, -1, -1, -1, -1, 0, 1, 2, 3, 4, 5, 6, -1, -1, -1,
		7, 8, 9, 10, 11, 12, 13, 14, 15, 16, -1, -1, -1, -1, -1,
	};

	return (unsigned)r < 34 ? tidx_of[r] : -1;
}
VPRIV __attribute__((format(printf, 3, 4))) void codegen_diagnostic_report(codegen_context *e, uint32_t loc, const char *fmt, ...);
VPRIV __attribute__((format(printf, 3, 4))) void codegen_error_report(codegen_context *e, uint32_t loc, const char *fmt, ...);
VPRIV __attribute__((format(printf, 3, 4), noreturn)) void codegen_fatal_report(codegen_context *e, uint32_t loc, const char *fmt, ...);
/* type.c */
VPRIV uint32_t codegen_symbol_canonical(cc *c, uint32_t s);
VPRIV const attrs *codegen_symbol_attributes(cc *c, uint32_t s);

/* memcpy/memmove/memset declared by a header (symbol s, na arguments): clang's builtin at -O1 and up (not
 * under -ffreestanding/-fno-builtin[-NAME]).  CC_LIBRARY_KIND_MEMCPY.., or 0 */
static inline int codegen_memory_function_kind(codegen_context *e, uint32_t s, int na)
{
	cc *c = e->compile_context;
	const sym *y = &c->syms[s];

	if (na != 3 || e->unit->options->opt < 1 || e->unit->options->no_builtin || y->sc == AST_STORAGE_STATIC || !(y->flags & AST_SYMBOL_FLAG_GLOBAL) ||
	    codegen_symbol_attributes(c, s)->asmname)
		return 0;
	for (unsigned i = 0; i < CC_DISABLED_BUILTIN_WORDS; i++)
		if (e->unit->options->disabled_builtins[i])
			return 0;
	return CODEGEN_IDENTIFIER_IS(y->name, "memcpy") ? CC_LIBRARY_KIND_MEMCPY : CODEGEN_IDENTIFIER_IS(y->name, "memmove") ? CC_LIBRARY_KIND_MEMMOVE :
	       CODEGEN_IDENTIFIER_IS(y->name, "memset") ? CC_LIBRARY_KIND_MEMSET : 0;
}
VPRIV void codegen_symbol_name_append(cc *c, tbuf *b, uint32_t s);
VPRIV bool codegen_symbol_name_is_quoted(cc *c, uint32_t s);
VPRIV void codegen_reference_name_append(cc *c, tbuf *b, uint32_t r);
VPRIV void codegen_reference_offset_append(cc *c, tbuf *b, uint32_t r, int64_t off);
/* darwin.c */
VPRIV void a64_darwin_text_translate(tbuf *o, const char *t, size_t tn);
/* print.c */
VPRIV void a64_print_label(tbuf *b, uint32_t fid, uint32_t l);
VPRIV void a64_record_print(cc *c, tbuf *b, const a64_record *i, uint32_t fid, const char *raw);
/* obj.c */
static inline bool codegen_context_is_direct(const codegen_context *e);
VPRIV size_t codegen_item_open(codegen_context *e, uint32_t kind);
VPRIV void codegen_item_close(codegen_context *e, size_t h);
VPRIV void *codegen_item_room_need(codegen_context *e, size_t n);
VPRIV int a64_function_commit(codegen_commit_context *R, const codegen_code_header *h);
VPRIV int codegen_commit_fail(codegen_commit_context *R, const char *msg);
VPRIV uint32_t codegen_commit_symbol_find(codegen_commit_context *R, uint32_t ref);
VPRIV bool x64_obj_record(codegen_context *e, const record *i, record *out);
VPRIV int x64_replay(codegen_commit_context *R, const char *p, size_t n);
VPRIV int codegen_commit_chunk(codegen_commit_context *R, const char *p, size_t n);
/* label.c */
static inline a64_record *a64_record_emit(codegen_context *e, int op);
static inline void a64_record_emit_three_registers(codegen_context *e, int op, int x, int rd, int rn, int rm);
static inline void a64_record_emit_three_registers_shifted(codegen_context *e, int op, int x, int rd, int rn, int rm, int kind, int sh);
static inline void a64_record_emit_four_registers(codegen_context *e, int op, int x, int rd, int rn, int rm, int ra);
static inline void a64_record_emit_immediate(codegen_context *e, int op, int x, int rd, int rn, int64_t imm);
static inline void a64_record_emit_bitfield(codegen_context *e, int op, int x, int rd, int rn, int immr, int imms);
static inline void a64_record_emit_move_immediate(codegen_context *e, int x, int rd, int64_t v);
static inline void a64_record_emit_move(codegen_context *e, int x, int rd, int rn);
static inline void a64_record_emit_condition_set(codegen_context *e, int rd, int cond);
static inline int a64_record_condition_set_last(codegen_context *e, const codegen_value *v);
static inline void a64_record_emit_memory(codegen_context *e, int op, int sz, int ext, int rd, int base, int64_t off);
static inline void a64_record_emit_symbol(codegen_context *e, int op, int sz, int ext, int rd, int rn, uint32_t ref, int64_t off);
VPRIV uint32_t a64_label_new(codegen_context *e);
VPRIV void a64_label_edge_add(codegen_context *e, uint32_t l);
VPRIV void deflabel(codegen_context *e, uint32_t l);
VPRIV void deflabel_loop(codegen_context *e, uint32_t l, bool forced);
VPRIV void a64_label_define_forced(codegen_context *e, uint32_t l);
/* builtin.c: atomics, LSE or ldaxr/stlxr loops */
VPRIV void at_cas(codegen_context *e, unsigned sz, int rs, int rt, int rn);
VPRIV void at_swp(codegen_context *e, unsigned sz, int rs, int rt, int rn);
VPRIV void at_ldop(codegen_context *e, unsigned sz, unsigned opc, int rs, int rt, int rn);
VPRIV void a64_jump_emit(codegen_context *e, uint32_t l);
VPRIV void a64_branch_condition_emit(codegen_context *e, int cond, uint32_t l);
VPRIV void a64_branch_compare_zero_emit(codegen_context *e, bool nz, int x, int r, uint32_t l);
VPRIV void a64_branch_test_bit_emit(codegen_context *e, bool nz, int r, int bit, uint32_t l);
VPRIV uint32_t a64_record_word_count(const a64_record *i);
/* value.c */
VPRIV int treg(codegen_context *e);
VPRIV void tfree(codegen_context *e, int r);
VPRIV void val_free(codegen_context *e, const codegen_value *v);
VPRIV int64_t a64_frame_slot_allocate(codegen_context *e, uint64_t size, uint32_t align);
VPRIV void a64_record_slot_tag(codegen_context *e, a64_record *i, int64_t off, int64_t len);
static inline codegen_value codegen_value_constant(i128 i);
static inline codegen_value codegen_value_register(int r);
static inline codegen_value codegen_value_address(uint32_t ref, i128 off);
static inline codegen_value codegen_value_none(void);
VPRIV void a64_reference_address_emit(codegen_context *e, int rd, uint32_t ref, int64_t off);
VPRIV int toreg(codegen_context *e, codegen_value *v);
VPRIV void toreg2(codegen_context *e, codegen_value *v, bool sgn);
VPRIV void lfree(codegen_context *e, const codegen_lvalue *l);
VPRIV int a64_lvalue_base_register(codegen_context *e, const codegen_lvalue *l, int64_t *off, int size);
VPRIV int a64_size_log2(uint64_t n);
VPRIV void a64_lvalue_load(codegen_context *e, int rd, const codegen_lvalue *l, int n, int ext);
VPRIV void a64_lvalue_store(codegen_context *e, int rs, const codegen_lvalue *l, int64_t extra, int n);
VPRIV codegen_value laddr(codegen_context *e, codegen_lvalue *l);
VPRIV codegen_lvalue lfromval(codegen_context *e, codegen_value v, uint32_t align);
VPRIV void copy_mem(codegen_context *e, int dst, int src, uint64_t n);
VPRIV void zero_mem(codegen_context *e, int dst, uint64_t n);
/* abi.c */
VPRIV int a64_abi_hfa_classify(cc *c, ast_qualified_type t, int *n);
VPRIV bool a64_abi_vector_is_small(cc *c, ast_qualified_type t);
VPRIV void a64_abi_bool_vector_return(codegen_context *e, ast_qualified_type t, int rs);
VPRIV void a64_abi_bool_vector_result_store(codegen_context *e, ast_qualified_type t, int64_t sl);
VPRIV void a64_abi_small_vector_return(codegen_context *e, ast_qualified_type t, int rs);
VPRIV void a64_abi_small_vector_result_store(codegen_context *e, ast_qualified_type t, int64_t sl);
VPRIV bool a64_abi_result_is_indirect(cc *c, ast_qualified_type t);
VPRIV void a64_abi_variadic_argument_classify(codegen_context *e, ast_qualified_type t, int *nsaa, a64_abi_argument *a);
VPRIV uint32_t a64_abi_type_alignment(cc *c, ast_qualified_type t);
VPRIV void a64_abi_argument_classify(codegen_context *e, ast_qualified_type t, int *ngrn, int *nsaa, a64_abi_argument *a);
VPRIV void a64_abi_temporaries_spill(codegen_context *e, a64_abi_spill *s);
VPRIV void a64_abi_temporaries_restore(codegen_context *e, const a64_abi_spill *s);
VPRIV void a64_call_memory_function_emit(codegen_context *e, int fn, int a0, int a1, int a2, int64_t n);
VPRIV void a64_function_finish(codegen_context *e, uint32_t s);	/* arch/a64/codegen/func.c: optimise, lay out and print the records */
VPRIV bool codegen_symbol_needs_got(cc *c, uint32_t ref);
/* bind.c */
static inline int64_t codegen_compound_literal_keep(const codegen_context *e, int64_t mark);
static inline int X(cc *c, ast_qualified_type t);
static inline void codegen_log_append(codegen_context *e, uint32_t tag, uint32_t v);
VPRIV void codegen_reference_note(codegen_context *e, uint32_t ref);
VPRIV void codegen_reference_claim(codegen_context *e, uint32_t ref);
VPRIV codegen_binding *codegen_symbol_binding_get(codegen_context *e, uint32_t s);
VPRIV int regvar_reg(cc *c, uint32_t s);
VPRIV int64_t bind_slot(codegen_context *e, uint32_t s);
VPRIV codegen_lvalue local_lval(codegen_context *e, uint32_t s);
VPRIV uint32_t codegen_symbol_alignment(cc *c, uint32_t s);
VPRIV bool codegen_symbol_is_local_variable(cc *c, uint32_t s);
/* lval.c */
VPRIV uint32_t align_at(uint64_t off, uint32_t a);
VPRIV uint64_t elem_size(cc *c, ast_qualified_type ptr);
VPRIV codegen_value ptr_add(codegen_context *e, codegen_value p, codegen_value i, int64_t scale, bool sub);
VPRIV codegen_value ptr_add_vla(codegen_context *e, codegen_value p, codegen_value i, ast_qualified_type ptr, bool sub);
VPRIV bool codegen_type_is_pointer_to_vla(cc *c, ast_qualified_type ptr);
VPRIV codegen_lvalue gen_lval(codegen_context *e, uint32_t n);
VPRIV codegen_value load(codegen_context *e, codegen_lvalue *l, ast_qualified_type t);
VPRIV bool is_atomic_lv(codegen_context *e, const codegen_lvalue *l, ast_qualified_type t);
VPRIV int atomic_sz(codegen_context *e, ast_qualified_type t);
VPRIV int atomic_addr(codegen_context *e, codegen_lvalue *l, bool *own);
VPRIV void store(codegen_context *e, codegen_lvalue *l, ast_qualified_type t, codegen_value *v);
/* arith.c */
VPRIV void extend(codegen_context *e, int rd, int rs, int from, bool sgn);
VPRIV int lreg_of(codegen_context *e, uint32_t s);
VPRIV void opt_function(codegen_context *e);
/* arch/a64/codegen/unreachable.c */
VPRIV void a64_unreachable_remove(codegen_context *e);
enum { OPF_ALL = 1, OPF_CALL = 2, OPF_FRAME = 4 };
VPRIV uint64_t a64_registers_used(codegen_context *e, unsigned *fl);
VPRIV bool a64_function_is_frameless(codegen_context *e, uint32_t n);
VPRIV codegen_lvalue bind_lreg(codegen_context *e, uint32_t s);
VPRIV codegen_value fold_int_bin(codegen_context *e, int op, ast_qualified_type ot, ast_qualified_type rt, i128 l, i128 r, bool *ok);
VPRIV bool codegen_operator_is_comparison(int op);
/* Pure control proofs: bounded shared walk, no committed target instructions. */
VPRIV bool em_const_strcmp(cc *c, uint32_t n, i128 *value);
VPRIV int em_const_truth(codegen_context *e, uint32_t n, int (*constant_p)(codegen_context *, uint32_t));
VPRIV int a64_constant_p(codegen_context *e, uint32_t arg);
VPRIV int emit_cmp(codegen_context *e, int op, int x, bool sg, codegen_value *a, codegen_value *b);
VPRIV int emit_cmp2(codegen_context *e, int op, bool sg, codegen_value *a, codegen_value *b);
VPRIV int test_zero(codegen_context *e, codegen_value *v, ast_qualified_type t, uint32_t l, bool nz);
VPRIV codegen_value tobool(codegen_context *e, codegen_value v, ast_qualified_type t);
/* fp.c */
static inline void a64_record_emit_word(codegen_context *e, uint32_t w);
VPRIV int fpk(codegen_context *e, ast_qualified_type t);
static inline uint32_t a64_word_fmov_to_vector(int d, int vr, int r);
static inline uint32_t a64_word_fmov_from_vector(int d, int r, int vr);
VPRIV codegen_value f_const(int d, double x);
static inline uint32_t a64_word_fmov_high_to_vector(int vr, int r);
static inline uint32_t a64_word_fmov_high_from_vector(int r, int vr);
VPRIV void vr_store(codegen_context *e, int sz, int vr, int base, int64_t off);
VPRIV void vr_load(codegen_context *e, int sz, int vr, int base, int64_t off);
VPRIV i128 ld_of_d(double x);
VPRIV codegen_value f_const_cv(codegen_context *e, ast_qualified_type t, const cc_value *cv);
VPRIV codegen_value f_const_t(codegen_context *e, ast_qualified_type t, double x);
VPRIV codegen_value ld_compare(codegen_context *e, int op, ast_qualified_type t, codegen_value a, codegen_value b);
VPRIV codegen_value f_arith(codegen_context *e, int op, ast_qualified_type ty, codegen_value a, codegen_value b);
VPRIV int f_cmp(codegen_context *e, int op, int d, codegen_value *a, codegen_value *b);
VPRIV codegen_value f_compare(codegen_context *e, int op, ast_qualified_type t, codegen_value a, codegen_value b);
VPRIV codegen_value f_tobool(codegen_context *e, codegen_value v, ast_qualified_type t);
VPRIV codegen_value f_neg(codegen_context *e, codegen_value v, ast_qualified_type t);
VPRIV codegen_value ld_cast(codegen_context *e, codegen_value v, ast_qualified_type from, ast_qualified_type to);
VPRIV codegen_value f_cast(codegen_context *e, codegen_value v, ast_qualified_type from, ast_qualified_type to);
/* arith.c */
VPRIV codegen_value cast(codegen_context *e, codegen_value v, ast_qualified_type from, ast_qualified_type to, int kind);
VPRIV codegen_value a64_sdivpow2(codegen_context *e, codegen_value a, const em_sdivpow2_recipe *r);
VPRIV bool a64_mulconst(codegen_context *e, ast_qualified_type ty, int r, i128 rhs);
VPRIV void shift_imm(codegen_context *e, int op, int x, int rd, int rn, int k);
VPRIV codegen_value arith(codegen_context *e, int op, ast_qualified_type ty, codegen_value a, codegen_value b);
VPRIV codegen_value arith2(codegen_context *e, int op, ast_qualified_type ty, codegen_value a, codegen_value b);
/* call.c */
VPRIV codegen_value do_call(codegen_context *e, uint32_t ref, codegen_value *fnv, a64_call_argument *args, int n, ast_qualified_type rty, bool noret, int nfix);
VPRIV codegen_value call_named(codegen_context *e, uint32_t ref, codegen_value *vals, ast_qualified_type *tys, int n, ast_qualified_type rty, bool noret);
VPRIV void codegen_function_prepass_request(codegen_context *e, uint32_t s);
VPRIV void kv_track(codegen_context *e, uint32_t s, const codegen_value *v);
VPRIV int codegen_array_element_slot(codegen_context *e, uint32_t n, bool create);
VPRIV bool const_var(codegen_context *e, uint32_t s, const codegen_value *v);
VPRIV bool can_inline(codegen_context *e, uint32_t s);
VPRIV codegen_value inline_call(codegen_context *e, uint32_t s, a64_call_argument *args, int n, uint32_t call);
VPRIV bool codegen_inline_object_size(codegen_context *e, uint32_t fn, uint32_t call, uint32_t pointer, uint32_t mode, i128 *size);
VPRIV uint32_t codegen_call_direct_callee(cc *c, uint32_t fn, uint32_t *ref);
VPRIV codegen_value gen_call(codegen_context *e, uint32_t n);
/* builtin.c */
VPRIV uint32_t codegen_expression_casts_strip(cc *c, uint32_t n);
VPRIV bool codegen_expression_has_side_effects(cc *c, uint32_t n);
VPRIV codegen_value dry(codegen_context *e, uint32_t n);
VPRIV codegen_value va_arg_(codegen_context *e, const node *x);
VPRIV codegen_value gen_builtin(codegen_context *e, uint32_t n);
/* expr.c */
VPRIV void move_to(codegen_context *e, int r, int r2, codegen_value *v, ast_qualified_type t);
VPRIV codegen_value gen(codegen_context *e, uint32_t n);
/* cond.c */
VPRIV int codegen_condition_fold(codegen_context *e, uint32_t n);
/* known-bits.c */
VPRIV bool codegen_constraint_immediate_fits(int ch, i128 v);
VPRIV void codegen_expression_known_bits(codegen_context *e, uint32_t n, uint64_t *k, uint64_t *v);
VPRIV void gen_cond(codegen_context *e, uint32_t n, bool jump_if, uint32_t l);
/* stmt.c */
VPRIV void gen_init_local(codegen_context *e, const codegen_lvalue *dst, ast_qualified_type ty, uint32_t init);
VPRIV void run_cleanups(codegen_context *e, int to);
VPRIV uint32_t user_label(codegen_context *e, uint32_t l);
VPRIV codegen_value gen_block(codegen_context *e, uint32_t n, bool want);
VPRIV codegen_value vla_bytes(codegen_context *e, ast_qualified_type t);
VPRIV void vla_fix(codegen_context *e, ast_qualified_type t, bool pure);
VPRIV void gen_stmt(codegen_context *e, uint32_t n);
VPRIV bool codegen_debug_location_get(codegen_context *e, uint32_t loc, uint32_t *file, uint32_t *line, uint32_t *col);
VPRIV void codegen_debug_row_add(codegen_context *e, uint32_t loc);	/* -g: a line-table row at the next record */
VPRIV void codegen_debug_rows_remap(codegen_context *e, uint32_t *j, uint32_t k, uint32_t n);	/* compaction, k ascending */
/* asm.c */
VPRIV void a64_asm_emit(codegen_context *e, uint32_t n);
/* func.c */
VPRIV void codegen_chunk_add(codegen_context *e, uint64_t key, size_t start);
VPRIV void codegen_symbol_header_append(codegen_context *e, tbuf *b, uint32_t s, bool fn, const char *defsec, uint32_t align);
VPRIV void codegen_constructor_entry_append(cc *c, tbuf *b, uint32_t s, bool fini);
VPRIV void emit_function(codegen_context *e, uint32_t s);
/* data.c */
VPRIV void codegen_data_object_emit(codegen_context *e, uint32_t s);
VPRIV void codegen_string_literal_emit(codegen_context *e, uint32_t i);
/* arch/wasm/codegen/ */
VPRIV void wasm_emit_function(codegen_context *e, uint32_t s);
VPRIV void wasm_ecx_free(codegen_context *e);
VPRIV void walk_function_emit(codegen_context *e, uint32_t s);
VPRIV void walk_context_free(codegen_context *e);
VPRIV bool x64_vir_function_lower(codegen_context *e, uint32_t s, const char **why);
VPRIV bool a64_vir_function_lower(codegen_context *e, uint32_t s, const char **why);	/* codegen/vir/a64.c: the built VIR as arm64 records */
VPRIV const virbuild_failure_info virbuild_failure_table[VIRBUILD_FAILURE_COUNT];
VPRIV void virbuild_statistics_print(codegen_unit *E);	/* VCC_VIR_STATS: one line of totals, one per reason (stderr) */
VPRIV bool virbuild_function_try(codegen_context *e, uint32_t s);	/* codegen/vir/: s through VIR (VCC_VIR); false: emit it as before */
VPRIV bool virbuild_is_enabled(const cc_options *o);	/* VIR lowers functions (VCC_VIR with l, -O2) */
VPRIV void virbuild_walk_context_free(codegen_context *e);
VPRIV void wasm_data_ref(codegen_context *e, uint32_t ref);
VPRIV void wasm_header(cc *c, tbuf *hdr, codegen_context *ctx, int nthr);
VPRIV void wasm_tail(cc *c, tbuf *t, _Atomic uint8_t *claimed);
/* prepass.c */
VPRIV bool codegen_expression_is_constant_index(cc *c, uint32_t n);
VPRIV uint32_t codegen_expression_constant_index(cc *c, uint32_t n);
VPRIV bool codegen_target_supports_sve(cc *c);
VPRIV bool codegen_target_supports_bitint(cc *c);
VPRIV void codegen_function_walk_ensure(codegen_unit *E, cc *c, uint32_t s);
VPRIV uint8_t *codegen_function_once_flags(cc *c);
#ifndef WALK_INLINE_ONCE_MAX
#define WALK_INLINE_ONCE_MAX 4000 /* under VIR: a static function with one call */
#endif
#ifndef WALK_INLINE_VIR_MAX
#define WALK_INLINE_VIR_MAX 150	/* under VIR: AST nodes of a callee with what it inlines (codegen_function_inline_sizes) */
#endif
#ifndef WALK_INLINE_CONSTANT_FACTOR
#define WALK_INLINE_CONSTANT_FACTOR 4
#endif
VPRIV void codegen_function_inline_sizes(codegen_unit *E, cc *c, uint32_t lim);	/* E->isz, E->scc: VIR's inlining sizes */
VPRIV bool codegen_inline_has_constant_argument(const codegen_unit *E, cc *c, uint32_t callee, uint32_t call);
VPRIV bool codegen_inline_is_candidate(codegen_unit *E, cc *c, uint32_t s);	/* VIR could inline a call to s (size aside) */	/* per function: 1 when its one reference in the TU is a direct call */
VPRIV int codegen_u32_compare(const void *a, const void *b);
/* tu.c */
VPRIV void codegen_work_enqueue(codegen_unit *E, uint32_t item);

/* ---------------- types (the few predicates the emitter needs) ---------------- */

static inline ast_qualified_type codegen_type_primitive(int k) { return AST_QUALIFIED_TYPE(k, 0); }
static inline bool codegen_kind_is_integer(int k) { return (k >= AST_TYPE_BOOL && k <= AST_TYPE_UINT128) || k == AST_TYPE_BITINT || k == AST_TYPE_UNSIGNED_BITINT || k == AST_TYPE_ENUM; }
static inline bool codegen_type_is_bitint_big(cc *c, ast_qualified_type t) { int k = KIND(c, t); return k == AST_TYPE_BITINT || k == AST_TYPE_UNSIGNED_BITINT; }
static inline bool codegen_type_is_integer(cc *c, ast_qualified_type t) { return codegen_kind_is_integer(KIND(c, t)); }
static inline bool codegen_type_is_bitint(cc *c, ast_qualified_type t)
{
	return ((KIND(c, t) >= AST_TYPE_CHAR && KIND(c, t) <= AST_TYPE_UINT128) || codegen_type_is_bitint_big(c, t)) &&
	       !(T(c, t)->flags & AST_TYPE_FLAG_ATOMIC) && T(c, t)->n;
}
static inline bool codegen_type_is_pointer(cc *c, ast_qualified_type t) { return KIND(c, t) == AST_TYPE_POINTER; }
static inline bool codegen_type_is_float(cc *c, ast_qualified_type t) { int k = KIND(c, t); return k >= AST_TYPE_BFLOAT16 && k <= AST_TYPE_FLOAT128; }
static inline bool codegen_type_is_record(cc *c, ast_qualified_type t) { int k = KIND(c, t); return k == AST_TYPE_STRUCT || k == AST_TYPE_UNION; }
static inline bool codegen_type_is_array(cc *c, ast_qualified_type t) { return KIND(c, t) == AST_TYPE_ARRAY; }
static inline bool codegen_type_is_function(cc *c, ast_qualified_type t) { return KIND(c, t) == AST_TYPE_FUNCTION; }
static inline bool codegen_type_is_void(cc *c, ast_qualified_type t) { return KIND(c, t) == AST_TYPE_VOID; }
static inline bool codegen_type_is_vector(cc *c, ast_qualified_type t) { return KIND(c, t) == AST_TYPE_VECTOR; }
static inline bool codegen_type_is_bool_vector(cc *c, ast_qualified_type t) { return codegen_type_is_vector(c, t) && KIND(c, T(c, t)->base) == AST_TYPE_BOOL; }
/* passed and returned like a record (vcc's own convention for vectors) */
static inline bool codegen_type_is_record_like(cc *c, ast_qualified_type t) { return codegen_type_is_record(c, t) || codegen_type_is_vector(c, t) || codegen_type_is_bitint_big(c, t); }
/* an SVE sizeless value: address-valued, in a frame slot of the largest vector length (a predicate's is 32 bytes) */
static inline bool codegen_type_is_sve(cc *c, ast_qualified_type t) { return KIND(c, t) == AST_TYPE_SVE_BUILTIN; }
static inline bool codegen_type_is_sve_predicate(cc *c, ast_qualified_type t) { return codegen_type_is_sve(c, t) && T(c, t)->size == 32; }
static inline bool codegen_type_is_aggregate(cc *c, ast_qualified_type t) { return codegen_type_is_record(c, t) || codegen_type_is_array(c, t) || codegen_type_is_vector(c, t) || codegen_type_is_sve(c, t) || codegen_type_is_bitint_big(c, t); }
static inline uint64_t codegen_type_size(cc *c, ast_qualified_type t) { return T(c, t)->size; }

/* Apple arm64: an integer under 4 bytes in a register is extended to 32 bits by its producer */
static inline bool codegen_type_is_darwin_narrow(cc *c, ast_qualified_type t) { return CODEGEN_IS_DARWIN(c) && T(c, t)->size < 4 && codegen_type_is_integer(c, t); }

/* a VLA somewhere in t's pointer/array chain (int a[n], int (*p)[n][4]) */
static inline bool codegen_type_is_variably_modified(cc *c, ast_qualified_type t)
{
	for (;;) {
		const ctype *a = T(c, t);

		if (a->kind == AST_TYPE_ARRAY && (a->flags & AST_TYPE_FLAG_VLA))
			return true;
		if (a->kind != AST_TYPE_ARRAY && a->kind != AST_TYPE_POINTER)
			return false;
		t = a->base;
	}
}
static inline uint32_t codegen_type_alignment(cc *c, ast_qualified_type t) { return T(c, t)->align; }
static inline bool codegen_type_is_signed(cc *c, ast_qualified_type t) { return cc_is_signed(c, t); }
/* long double: binary128, kept as its bits in a register pair (arithmetic: the __*tf* helpers) */
static inline bool codegen_type_is_long_double(cc *c, ast_qualified_type t) { return codegen_type_is_float(c, t) && codegen_type_size(c, t) == 16; }
static inline bool codegen_type_is_wide(cc *c, ast_qualified_type t) { return (codegen_type_is_integer(c, t) || codegen_type_is_float(c, t)) && codegen_type_size(c, t) == 16; }

static inline bool codegen_context_is_direct(const codegen_context *e) { return e->unit->options->emit == 2 && !e->unit->object_text; }

/* ---------------- values ---------------- */

static inline codegen_value codegen_value_constant(i128 i) { codegen_value v = { .kind = CODEGEN_VALUE_CONSTANT, .integer = i }; return v; }
static inline codegen_value codegen_value_register(int r) { codegen_value v = { .kind = CODEGEN_VALUE_REGISTER, .register_number = (uint8_t)r }; return v; }
static inline codegen_value codegen_value_address(uint32_t ref, i128 off) { codegen_value v = { .kind = CODEGEN_VALUE_ADDRESS, .reference = ref, .integer = off }; return v; }
/* an undefined weak symbol's address may be 0: a register, never a CODEGEN_VALUE_ADDRESS (which every fold takes as nonzero) */
static inline bool codegen_reference_is_weak(const cc *c, uint32_t ref)
{
	const sym *y = &c->syms[CODEGEN_REFERENCE_INDEX(ref)];

	return CODEGEN_REFERENCE_KIND(ref) == CODEGEN_REFERENCE_SYMBOL && (y->flags & AST_SYMBOL_FLAG_WEAK) &&
	       !(y->kind == AST_SYMBOL_FUNCTION ? (y->flags & AST_SYMBOL_FLAG_DEFINED) && y->body : (y->flags & (AST_SYMBOL_FLAG_DEFINED | AST_SYMBOL_FLAG_TENTATIVE)));
}
static inline codegen_value codegen_value_none(void) { codegen_value v = { .kind = CODEGEN_VALUE_NONE }; return v; }

/* a statement's temporaries end with it; compound literals live on to the end of their block */
static inline int64_t codegen_compound_literal_keep(const codegen_context *e, int64_t mark)
{
	return mark > e->compound_literal_top ? mark : e->compound_literal_top;
}

static inline int X(cc *c, ast_qualified_type t) { return codegen_type_size(c, t) == 8 || codegen_type_is_pointer(c, t) || codegen_type_is_function(c, t) ? 1 : 0; }
static inline void codegen_log_append(codegen_context *e, uint32_t tag, uint32_t v)
{
	if (unlikely(e->round_log_count + 2 > CODEGEN_LOG_CAPACITY)) {
		fputs("vcc: emit log full\n", stderr);
		abort();
	}
	e->round_log[e->round_log_count++] = tag;
	e->round_log[e->round_log_count++] = v;
}

#include "../arch/a64/codegen/a64-internal.h"

#endif
