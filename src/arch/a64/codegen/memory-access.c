/* vcc arm64 code generator: frame slots, addresses and typed loads and stores that VIR's lowering shares with the walker */
#include "../../../codegen/codegen-internal.h"

/* a stack slot: returns its offset (FRAME base) */
VPRIV int64_t slot(codegen_context *e, uint64_t size, uint32_t align)
{
	if (align < 1)
		align = 1;
	if (align > 16) {	/* x28: the base of the realigned frame if it turns out dynamic */
		if (e->nlr)
			_longjmp(e->bail, 2);	/* it holds a register variable (they start at x28): again without */
		e->resv |= 1u << codegen_temporary_index(R_BP);
	}
	if (align > e->falign)
		e->falign = align;	/* over 16: finish_function realigns sp to it */
	/* Keep the allocation and its alignment rounding within signed offsets. */
	uint64_t limit = (uint64_t)INT64_MAX - align;
	if ((uint64_t)e->foff > limit || size > limit - (uint64_t)e->foff)
		codegen_fatal_report(e, 0, "stack object too large");
	/* allocations past the current top ended with their scope */
	while (e->nsstk && e->sla[e->sstk[e->nsstk - 1]].off < -e->foff)
		e->nsstk--;
	e->foff = (int64_t)(((uint64_t)e->foff + size + align - 1) & ~(uint64_t)(align - 1));
	if (e->foff > e->fmax)
		e->fmax = e->foff;
	if (e->nsla < CODEGEN_SLOT_ALLOCATION_CAPACITY) {
		e->sla[e->nsla] = (codegen_slot_allocation){ -e->foff, size };
		e->sstk[e->nsstk++] = e->nsla;
	}
	e->nsla++;
	return -e->foff;
}

/* tag record i (frame offset off, len bytes; -1: an address) with its live allocation */
VPRIV void slot_tag(codegen_context *e, ins *i, int64_t off, int64_t len)
{
	uint32_t lo = 0, hi = e->nsstk;

	if (e->E->o->opt < 1 || e->novtag)
		return;
	/* live allocations are disjoint, offsets decreasing with depth */
	while (lo < hi) {
		uint32_t mid = (lo + hi) / 2;

		if (e->sla[e->sstk[mid]].off > off)
			lo = mid + 1;
		else
			hi = mid;
	}
	if (lo < e->nsstk) {
		const codegen_slot_allocation *a = &e->sla[e->sstk[lo]];
		uint32_t id = e->sstk[lo] + 1;

		if (off >= a->off && (len < 0 ? off <= a->off + (int64_t)a->size : off + len <= a->off + (int64_t)a->size) &&
		    id <= 0xffff)
			i->rm = (uint8_t)id, i->ra = (uint8_t)(id >> 8);
		/* an address at the start of one allocation is also just past the end of the one below */
		if (len < 0 && off == a->off && lo + 1 < e->nsstk) {
			const codegen_slot_allocation *b = &e->sla[e->sstk[lo + 1]];
			uint32_t id2 = e->sstk[lo + 1] + 1;

			if (b->off + (int64_t)b->size == off) {
				if (id2 > 0xffff)
					i->rm = i->ra = 0;	/* untagged */
				else
					i->cond = (uint8_t)id2, i->sh = (uint8_t)(id2 >> 8);
			}
		}
	}
}

/* address of a symbol into rd */
VPRIV void addr_of_ref(codegen_context *e, int rd, uint32_t ref, int64_t off)
{
	cc *c = e->c;

	codegen_reference_note(e, ref);
	if (codegen_symbol_needs_got(c, ref)) {
		symop(e, I_LDGOT, 1, 0, rd, 0, ref, 0);
		if (off)
			opi(e, I_ADDI, 1, rd, rd, off);
		return;
	}
	if (!CODEGEN_SYMBOL_OFFSET_FITS(c, off)) {
		symop(e, I_ADRP, 1, 0, rd, 0, ref, 0);
		symop(e, I_ADDLO, 1, 0, rd, rd, ref, 0);
		opi(e, I_ADDI, 1, rd, rd, (int64_t)off);
		return;
	}
	symop(e, I_ADRP, 1, 0, rd, 0, ref, off);
	symop(e, I_ADDLO, 1, 0, rd, rd, ref, off);
}

/* base register + offset for a memory access; may load a symbol page into x16 */
VPRIV int lbase(codegen_context *e, const codegen_lvalue *l, int64_t *off, int size)
{
	switch (l->k) {
	case CODEGEN_LVALUE_FRAME:
		*off = l->off;
		return FRAME;
	case CODEGEN_LVALUE_REGISTER:
		*off = l->off;
		return l->r;
	case CODEGEN_LVALUE_LOCAL_REGISTER:
		codegen_fatal_report(e, e->curloc, "internal: memory access to a register variable");
	default:
		addr_of_ref(e, R_X16, l->ref, l->off);
		*off = 0;
		(void)size;
		return R_X16;
	}
}

VPRIV int log2sz(uint64_t n) { return n == 1 ? 0 : n == 2 ? 1 : n == 4 ? 2 : 3; }

/* the alignment the object a reference names is known to have */
static uint32_t ref_align(codegen_context *e, const codegen_lvalue *l)
{
	cc *c = e->c;
	uint32_t a;

	switch (CODEGEN_REFERENCE_KIND(l->ref)) {
	case CODEGEN_REFERENCE_SYMBOL: a = codegen_symbol_alignment(c, codegen_symbol_canonical(c, CODEGEN_REFERENCE_INDEX(l->ref))); break;
	case CODEGEN_REFERENCE_STRING: a = c->strs[CODEGEN_REFERENCE_INDEX(l->ref)].esz; break;
	case CODEGEN_REFERENCE_LABEL: a = 1; break;
	default: return l->align;
	}
	return a < l->align ? a : l->align;
}

/* load a scalar of size n (1,2,4,8) into rd; ext: 0 zero, 1 sign to x */
VPRIV void load_n(codegen_context *e, int rd, const codegen_lvalue *l, int n, int ext)
{
	int64_t off;
	int sz = log2sz((uint64_t)n);

	if (l->k == CODEGEN_LVALUE_SYMBOL && !codegen_symbol_needs_got(e->c, l->ref) && ref_align(e, l) >= (uint32_t)n && (l->off & (n - 1)) == 0 &&
	    CODEGEN_SYMBOL_OFFSET_FITS(e->c, l->off)) {
		codegen_reference_note(e, l->ref);
		symop(e, I_ADRP, 1, 0, R_X16, 0, l->ref, l->off);
		symop(e, I_LDLO, sz, ext, rd, R_X16, l->ref, l->off);
		return;
	}
	{
		int b = lbase(e, l, &off, n);

		mem(e, I_LD, sz, ext, rd, b, off);
	}
}

VPRIV void store_n(codegen_context *e, int rs, const codegen_lvalue *l, int64_t extra, int n)
{
	int64_t off;
	int sz = log2sz((uint64_t)n);

	if (l->k == CODEGEN_LVALUE_SYMBOL && !codegen_symbol_needs_got(e->c, l->ref) && ref_align(e, l) >= (uint32_t)n && ((l->off + extra) & (n - 1)) == 0 &&
	    CODEGEN_SYMBOL_OFFSET_FITS(e->c, l->off + extra)) {
		codegen_reference_note(e, l->ref);
		symop(e, I_ADRP, 1, 0, R_X16, 0, l->ref, l->off + extra);
		symop(e, I_STLO, sz, 0, rs, R_X16, l->ref, l->off + extra);
		return;
	}
	{
		int b = lbase(e, l, &off, n);

		mem(e, I_ST, sz, 0, rs, b, off + extra);
	}
}
