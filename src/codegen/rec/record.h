/*
 * vcc code generator: machine records shared by the CPUs that use the
 * walker (codegen/walk/) and the passes over records (PLAN-x86.md, layer 3).
 *
 * A record is arm64's struct ins with its register fields on fixed
 * places: r[0] the result, r[1..3] the operands, so that a pass can ask a
 * CPU's op table (opdesc) which fields a record reads and writes without
 * knowing the instruction.  x86-64 is the only user so far; arm64 keeps
 * its own struct ins until it moves (TODO.md "arm64 and wasm on the
 * shared walker"), so this one is called rec.
 */
#ifndef VCC_CODEGEN_RECORD_H
#define VCC_CODEGEN_RECORD_H
#include <stdint.h>

typedef struct record {
	uint8_t opcode, size;		/* the CPU's opcode; width or access size in bytes */
	uint8_t registers[4];		/* r[0] result, r[1..3] operands (the CPU's register numbers) */
	uint8_t condition, shift;	/* condition, extension kind, scale */
	union {
		int64_t immediate;
		struct { int32_t offset; uint32_t reference; };	/* memory: displacement, symbol (CODEGEN_REFERENCE_MAKE(), 0: none) */
		uint32_t label;
	};
} record;
_Static_assert(sizeof(record) == 16, "rec is 16 bytes");

/* register sets for the passes: one bit per register number below 64 */
typedef uint64_t record_register_mask;

/* Linear reachability: an unclassified opcode is a barrier, never removable.
 * ENTRY includes labels and opaque records that may contain incoming edges.
 */
enum { RECORD_FLOW_ENTRY, RECORD_FLOW_NEXT, RECORD_FLOW_STOP };

/* what a pass needs to know about an op (a CPU's table, indexed by op) */
enum { RECORD_OPERAND_DEFINES_0 = 1, RECORD_OPERAND_USES_1 = 2, RECORD_OPERAND_USES_2 = 4, RECORD_OPERAND_USES_3 = 8, RECORD_OPERAND_USES_0 = 16 };	/* which r[i] */
enum { RECORD_BRANCH_NONE, RECORD_BRANCH_JUMP, RECORD_BRANCH_CONDITIONAL, RECORD_BRANCH_RETURN, RECORD_BRANCH_INDIRECT };
enum { RECORD_EFFECT_PURE = 1, RECORD_EFFECT_READS_FLAGS = 2, RECORD_EFFECT_WRITES_FLAGS = 4, RECORD_EFFECT_LOAD = 8, RECORD_EFFECT_STORE = 16, RECORD_EFFECT_CALL = 32, RECORD_EFFECT_LABEL = 64,
       RECORD_EFFECT_TRAP = 128, RECORD_EFFECT_BARRIER = 256, RECORD_EFFECT_FLOAT = 512 };
typedef struct record_op_descriptor {
	uint8_t registers;		/* RECORD_OPERAND_DEFINES_0 | RECORD_OPERAND_USES_1 ... */
	uint8_t branch;		/* RB_* */
	uint16_t flags;		/* RECORD_EFFECT_PURE ... */
	record_register_mask implicit_defined, implicit_used;	/* implicit registers (x86: div rax rdx, shifts rcx, calls) */
} record_op_descriptor;

/* Whole physical registers. A partial write also uses the old register, so
 * liveness cannot discard the untouched bits. def still invalidates copies.
 * RECORD_EFFECT_PURE excludes memory, trapping and floating-environment effects; it
 * does not waive flag/register dependencies. BARRIER forbids propagation.
 */
typedef struct record_effects {
	record_register_mask defines, used;
	uint16_t flags;
	uint8_t branch;
} record_effects;

#endif
