/* Block-local full-width GPR copies. The shared kernel does the forward
 * search; x86 owns register effects, legal operands and width restrictions.
 * Effects are decoded only along candidate scans. A copy must reach an
 * explicit overwrite in the same block; no allocation or liveness is needed.
 */
#include "x64impl.h"

#define X_COPY_BIT(r) (UINT64_C(1) << (r))
#define X_COPY_DELETED UINT8_C(0xff)

/* Only ordinary explicit GPR reads are rewritable. In particular, CL shifts,
 * div/mul, calls, REP, inline asm and atomics keep their fixed registers.
 * Tied/partial reads of a destination are rejected below even for these ops.
 */
static unsigned x64_copy_fields(const rec *i)
{
	switch (i->op) {
	case X_MOV:
		return i->r[0] < XMM0 && i->r[1] < XMM0 ? 1u << 1 : 0;
	case X_LD: case X_LEA:
	case X_ADDI: case X_SUBI: case X_ANDI: case X_ORI: case X_XORI: case X_IMULI:
	case X_SHLI: case X_SHRI: case X_SARI:
	case X_NEG: case X_NOT:
	case X_CMPI: case X_TEST:
	case X_BSF: case X_BSR: case X_BSWAP: case X_EXT:
		return 1u << 1;
	case X_ST:
	case X_ADD: case X_SUB: case X_AND: case X_OR: case X_XOR: case X_IMUL: case X_ADC: case X_SBB:
	case X_CMP:
		return (1u << 1) | (1u << 2);
	default:
		return 0;
	}
}

static bool x64_copy_rewrite(void *ctx, uint32_t j, uint8_t from, uint8_t to, bool apply)
{
	ecx *e = ctx;
	rec *i = &e->x64->in[j];
	unsigned fields = x64_copy_fields(i);
	if (!fields)
		return false;
	/* A partial write or tied result may read bits not represented by an
	 * independent operand. Do not redirect that register's identity.
	 */
	if (x64_effects(e, i).def & X_COPY_BIT(from))
		return false;
	bool found = false;
	for (unsigned f = 1; f <= 2; f++) {
		if ((fields & (1u << f)) && i->r[f] == from) {
			found = true;
			if (apply)
				i->r[f] = to;
		}
	}
	return found;
}

static bool x64_copy_inspect(void *ctx, uint32_t j, uint64_t *read, uint64_t *write)
{
	ecx *e = ctx;
	receffects f = x64_effects(e, &e->x64->in[j]);
	if (f.br != RB_NONE || (f.fl & (OD_LABEL | OD_BARRIER | OD_CALL)))
		return false;
	*read = f.use;
	*write = f.def;
	return true;
}

/* Called from the existing two-address walk. No extra whole-function walk
 * or compaction is needed; the caller omits a successfully forwarded copy. */
VPRIV bool x64_copy_forward_at(ecx *e, uint32_t k)
{
	xst *x = e->x64;
	const rec *i = &x->in[k];
	/* Return copies usually precede an epilogue jump. Reject these without
	 * decoding effects or setting up a transaction. Labels also end a block. */
	if (k + 1 >= x->nin || x->in[k + 1].op == X_JMP || x->in[k + 1].op == X_LABEL)
		return false;
	if (k + 1 >= x->nin || i->op != X_MOV || i->sz != 8 ||
		i->r[0] >= XMM0 || i->r[1] >= XMM0 || i->r[0] == i->r[1] ||
		!(X_TEMPS & (1u << i->r[0])) || i->r[1] == RSP ||
		i->r[1] == RBP || i->r[1] == R11 || i->r[1] == R12)
		return false;
	em_rec_copy_scan scan = {
		.records = x->in, .stride = sizeof(rec), .op_offset = offsetof(rec, op),
		.deleted_op = X_COPY_DELETED, .inspect = x64_copy_inspect,
		.rewrite = x64_copy_rewrite, .ctx = e,
	};
	return em_rec_copy_forward(&scan, k, x->nin - 1, i->r[0], i->r[1]);
}
