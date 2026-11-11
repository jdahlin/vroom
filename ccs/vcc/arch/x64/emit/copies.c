/* Block-local full-width GPR copies. The shared kernel does the forward
 * search; x86 owns register effects, legal operands and width restrictions.
 * No CFG is built: every block boundary keeps all registers live.
 */
#include "x64impl.h"

#define X_COPY_BIT(r) (UINT64_C(1) << (r))
#define X_COPY_ALL ((UINT64_C(1) << (XMM15 + 1)) - 1)
#define X_COPY_EDGE UINT16_C(0x8000)
#define X_COPY_DELETED UINT8_C(0xff)

static _Thread_local struct {
	void *data;
	uint32_t cap;
} x64_copy_work;

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

VPRIV void x64_copies(ecx *e)
{
	xst *x = e->x64;
	uint32_t n = x->nin;
	if (n < 2)
		return;
	if (n > x64_copy_work.cap) {
		uint32_t cap = n <= UINT32_MAX / 2 ? n * 2 : n;
		void *p = realloc(x64_copy_work.data, (size_t)cap * (3 * sizeof(rmask) + sizeof(uint16_t)));
		if (!p)
			abort();
		x64_copy_work.data = p;
		x64_copy_work.cap = cap;
	}
	rmask *read = x64_copy_work.data;
	rmask *write = read + x64_copy_work.cap;
	rmask *live = write + x64_copy_work.cap;
	uint16_t *flags = (uint16_t *)(live + x64_copy_work.cap);
	for (uint32_t k = 0; k < n; k++) {
		receffects f = x64_effects(e, &x->in[k]);
		read[k] = f.use;
		write[k] = f.def;
		flags[k] = f.fl;
		if (f.br != RB_NONE || (f.fl & (OD_LABEL | OD_BARRIER | OD_CALL)))
			flags[k] |= X_COPY_EDGE;
	}
	em_rec_copy_scan scan = {
		.records = x->in, .stride = sizeof(rec), .op_offset = offsetof(rec, op),
		.deleted_op = X_COPY_DELETED, .flags = flags, .flags_stride = sizeof(*flags),
		.opaque_mask = X_COPY_EDGE, .read = read, .write = write, .live = live,
		.rewrite = x64_copy_rewrite, .ctx = e,
	};
	bool changed = false;
	for (uint32_t begin = 0; begin < n;) {
		if (flags[begin] & X_COPY_EDGE) {
			begin++;
			continue;
		}
		uint32_t end = begin + 1;
		while (end < n && !(flags[end] & X_COPY_EDGE))
			end++;
		rmask l = X_COPY_ALL;
		for (uint32_t j = end; j-- > begin;) {
			live[j] = l;
			l = (l & ~write[j]) | read[j];
		}
		for (uint32_t k = begin; k + 1 < end; k++) {
			const rec *i = &x->in[k];
			/* Never rewrite ABI/persistent destinations. Sources may include
			 * callee-saved homes, but not frame/stack/scratch register aliases.
			 */
			if (i->op == X_MOV && i->sz == 8 && i->r[0] < XMM0 && i->r[1] < XMM0 &&
			    i->r[0] != i->r[1] && (X_TEMPS & (1u << i->r[0])) &&
			    i->r[1] != RSP && i->r[1] != RBP && i->r[1] != R11 && i->r[1] != R12)
				changed |= em_rec_copy_forward(&scan, k, end - 1, i->r[0], i->r[1]);
		}
		begin = end;
	}
	if (!changed)
		return;
	uint32_t out = 0, dj = 0;
	for (uint32_t k = 0; k < n; k++) {
		if (e->ndl)
			dbg_remap(e, &dj, k, out);
		if (x->in[k].op != X_COPY_DELETED)
			x->in[out++] = x->in[k];
	}
	if (e->ndl)
		dbg_remap(e, &dj, UINT32_MAX, out);
	x->nin = out;
}
