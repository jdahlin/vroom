/*
 * vcc code generator: machine records shared by the CPUs that use the
 * walker (emit/walk/) and the passes over records (PLAN-x86.md, layer 3).
 *
 * A record is arm64's struct ins with its register fields on fixed
 * places: r[0] the result, r[1..3] the operands, so that a pass can ask a
 * CPU's op table (opdesc) which fields a record reads and writes without
 * knowing the instruction.  x86-64 is the only user so far; arm64 keeps
 * its own struct ins until it moves (TODO.md "arm64 and wasm on the
 * shared walker"), so this one is called rec.
 */
#ifndef VCC_REC_H
#define VCC_REC_H
#include <stdint.h>

typedef struct rec {
	uint8_t op, sz;		/* the CPU's opcode; width or access size in bytes */
	uint8_t r[4];		/* r[0] result, r[1..3] operands (the CPU's register numbers) */
	uint8_t cond, sh;	/* condition, extension kind, scale */
	union {
		int64_t imm;
		struct { int32_t off; uint32_t ref; };	/* memory: displacement, symbol (REF(), 0: none) */
		uint32_t label;
	};
} rec;
_Static_assert(sizeof(rec) == 16, "rec is 16 bytes");

/* register sets for the passes: one bit per register number below 64 */
typedef uint64_t rmask;

/* what a pass needs to know about an op (a CPU's table, indexed by op) */
enum { OD_DEF0 = 1, OD_USE1 = 2, OD_USE2 = 4, OD_USE3 = 8, OD_USE0 = 16 };	/* which r[i] */
enum { RB_NONE, RB_JUMP, RB_COND, RB_RET, RB_INDIRECT };
enum { OD_PURE = 1, OD_RFLAGS = 2, OD_WFLAGS = 4, OD_LOAD = 8, OD_STORE = 16, OD_CALL = 32, OD_LABEL = 64 };
typedef struct opdesc {
	uint8_t regs;		/* OD_DEF0 | OD_USE1 ... */
	uint8_t br;		/* RB_* */
	uint8_t fl;		/* OD_PURE ... */
	rmask idef, iuse;	/* implicit registers (x86: div rax rdx, shifts rcx, calls) */
} opdesc;

#endif
