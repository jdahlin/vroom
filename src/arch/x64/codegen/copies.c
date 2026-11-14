/* Block-local full-width GPR copies. The shared kernel does the forward
 * search; x86 owns register effects, legal operands and width restrictions.
 * Effects are decoded only along candidate scans. A copy must reach an
 * explicit overwrite in the same block; no allocation or liveness is needed.
 */
#include "x64-internal.h"

#define X_COPY_BIT(r) (UINT64_C(1) << (r))
#define X_COPY_DELETED UINT8_C(0xff)

/* Only ordinary explicit GPR reads are rewritable. In particular, CL shifts,
 * div/mul, calls, REP, inline asm and atomics keep their fixed registers.
 * Tied/partial reads of a destination are rejected below even for these ops.
 */
static unsigned x64_copy_fields(const record *i)
{
	switch (i->opcode) {
	case X64_OP_MOV:
		return i->registers[0] < XMM0 && i->registers[1] < XMM0 ? 1u << 1 : 0;
	case X64_OP_LDX: return (1u << 1) | (1u << 3);
	case X64_OP_TESTM: return (1u << 1) | (1u << 3);
	case X64_OP_STX: return (1u << 1) | (1u << 2) | (1u << 3);
	case X64_OP_LD: case X64_OP_LEA:
	case X64_OP_ADDI: case X64_OP_SUBI: case X64_OP_ANDI: case X64_OP_ORI: case X64_OP_XORI: case X64_OP_IMULI:
	case X64_OP_SHLI: case X64_OP_SHRI: case X64_OP_SARI:
	case X64_OP_NEG: case X64_OP_NOT:
	case X64_OP_CMPI: case X64_OP_TEST:
	case X64_OP_BSF: case X64_OP_BSR: case X64_OP_POPCNT: case X64_OP_BSWAP: case X64_OP_EXT:
		return 1u << 1;
	case X64_OP_ST:
	case X64_OP_ADD: case X64_OP_SUB: case X64_OP_AND: case X64_OP_OR: case X64_OP_XOR: case X64_OP_IMUL: case X64_OP_ADC: case X64_OP_SBB:
	case X64_OP_CMP:
	case X64_OP_CMOV:
		return (1u << 1) | (1u << 2);
	default:
		return 0;
	}
}

static bool x64_copy_rewrite(void *ctx, uint32_t j, uint8_t from, uint8_t to, bool apply)
{
	codegen_context *e = ctx;
	record *i = &e->x64_context->records[j];
	unsigned fields = x64_copy_fields(i);
	if (!fields)
		return false;
	/* A partial write or tied result may read bits not represented by an
	 * independent operand. Do not redirect that register's identity.
	 */
	if (x64_record_effects(e, i).defines & X_COPY_BIT(from))
		return false;
	bool found = false;
	for (unsigned f = 1; f <= 2; f++) {
		if ((fields & (1u << f)) && i->registers[f] == from) {
			found = true;
			if (apply)
				i->registers[f] = to;
		}
	}
	return found;
}

static bool x64_copy_inspect(void *ctx, uint32_t j, uint64_t *read, uint64_t *write)
{
	codegen_context *e = ctx;
	record_effects f = x64_record_effects(e, &e->x64_context->records[j]);
	if (f.branch != RECORD_BRANCH_NONE || (f.flags & (RECORD_EFFECT_LABEL | RECORD_EFFECT_BARRIER | RECORD_EFFECT_CALL)))
		return false;
	*read = f.used;
	*write = f.defines;
	return true;
}

/* Called from the existing two-address walk. No extra whole-function walk
 * or compaction is needed; the caller omits a successfully forwarded copy. */
VPRIV bool x64_copy_forward_at(codegen_context *e, uint32_t k)
{
	x64_lowering_context *x = e->x64_context;
	const record *i = &x->records[k];
	/* Return copies usually precede an epilogue jump. Reject these without
	 * decoding effects or setting up a transaction. Labels also end a block. */
	if (k + 1 >= x->record_count || x->records[k + 1].opcode == X64_OP_JMP || x->records[k + 1].opcode == X64_OP_LABEL)
		return false;
	if (k + 1 >= x->record_count || i->opcode != X64_OP_MOV || i->size != 8 ||
		i->registers[0] >= XMM0 || i->registers[1] >= XMM0 || i->registers[0] == i->registers[1] ||
		!(X64_TEMPORARIES & (1u << i->registers[0])) || i->registers[1] == RSP ||
		i->registers[1] == RBP || i->registers[1] == R11 || i->registers[1] == R12)
		return false;
	record_copy_scan scan = {
		.records = x->records, .stride = sizeof(record), .op_offset = offsetof(record, opcode),
		.deleted_op = X_COPY_DELETED, .inspect = x64_copy_inspect,
		.rewrite = x64_copy_rewrite, .ctx = e,
	};
	return record_copy_forward(&scan, k, x->record_count - 1, i->registers[0], i->registers[1]);
}
