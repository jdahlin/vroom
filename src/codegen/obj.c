/* Shared object-item stream: data, symbols and target record replay. */
#include "emitimpl.h"

VPRIV int r_fail(rctx *R, const char *msg)
{
	elf_error(R->o, "%s", msg);
	return -1;
}

VPRIV size_t it_open(ecx *e, uint32_t kind)
{
	tbuf *b = &e->out;
	size_t h;

	if (!direct(e))
		return SIZE_MAX;
	vtext_need(b, 16);
	b->n = (b->n + 7) & ~(size_t)7;
	h = b->n;
	((item *)(b->p + h))->kind = kind;
	b->n += sizeof(item);
	return h;
}

VPRIV void it_close(ecx *e, size_t h)
{
	tbuf *b = &e->out;

	if (h == SIZE_MAX)
		return;
	((item *)(b->p + h))->len = (uint32_t)(b->n - h - sizeof(item));
	vtext_need(b, 8);
	while (b->n & 7)
		b->p[b->n++] = 0;
}

VPRIV void *it_room(ecx *e, size_t n)
{
	void *p;

	vtext_need(&e->out, n + 8);
	p = e->out.p + e->out.n;
	e->out.n += n;
	return p;
}

/* the assembler's symbol for a reference (named as the printer names it) */
VPRIV uint32_t r_sym(rctx *R, uint32_t ref)
{
	uint32_t *slot = NULL, i = RIDX(ref), y;

	switch (RKIND(ref)) {
	case RK_SYM: slot = &R->symc[canon_sym(R->c, i)]; break;
	case RK_STR: slot = &R->strc[i]; break;
	case RK_LBL: slot = &R->lblc[i]; break;
	}
	if (slot && *slot)
		return *slot;
	R->nb.n = 0;
	ref_name(R->c, &R->nb, ref);
	/* ref_name quotes non-ASCII x86 symbols for the text printer. */
	bool quoted = R->nb.n >= 2 && R->nb.p[0] == '"' && R->nb.p[R->nb.n - 1] == '"';
	y = as_symbol_by_name(R->as, R->nb.p + quoted, R->nb.n - 2 * quoted);
	if (slot)
		*slot = y;
	return y;
}

static void r_zero(ElfObj *o, uint32_t frag, uint64_t n)
{
	if (n)
		elf_fill(o, frag, n, 0);
}

static int r_data(rctx *R, const data_hdr *h)
{
	const ireloc *rel = (const ireloc *)(h + 1);
	const uint8_t *d = (const uint8_t *)(rel + h->nrel);
	ElfObj *o = R->o;
	uint32_t frag = as_fragment_current(R->as);
	uint64_t at = 0;

	if (!h->size)
		return 0;
	elf_data(o, frag);
	if (h->zero) {
		elf_fill(o, frag, h->size, 0);
		return 0;
	}
	for (uint32_t k = 0; k <= h->nrel; k++) {
		uint64_t end = k < h->nrel ? rel[k].off : h->size;

		if (end > at) {
			/* bytes in [at, end): what was initialised, then zeros */
			uint64_t have = at < h->n ? (end < h->n ? end : h->n) - at : 0;

			if (have)
				elf_bytes(o, frag, d + at, have);
			r_zero(o, frag, end - at - have);
			at = end;
		}
		if (k < h->nrel) {
			uint32_t sz = rel[k].sz ? rel[k].sz : 8;
			uint64_t off = elf_frag(o, frag)->size;
			uint8_t *p = elf_grow(o, frag, sz);
			ElfVal v = as_symbol_value(R->as, r_sym(R, rel[k].ref), rel[k].add);

			if (rel[k].sub) {
				ElfVal s = as_symbol_value(R->as, r_sym(R, rel[k].sub - 1), 0);

				v.sub = s.add, v.c -= s.c;
			}
			if (!v.add && !v.sub) {
				for (uint32_t b = 0; b < sz; b++)
					p[b] = (uint8_t)((uint64_t)v.c >> 8 * b);
			} else {
				memset(p, 0, sz);
				elf_fixup(o, frag, off, p, X86(R->c) ? (sz == 1 ? R_X86_64_8 : sz == 2 ? R_X86_64_16 : sz == 4 ? R_X86_64_32 : R_X86_64_64) :
					  (sz == 4 ? R_AARCH64_ABS32 : R_AARCH64_ABS64), v);
			}
			at += sz;
		}
	}
	return 0;
}

static int r_symhdr(rctx *R, const symhdr *h)
{
	ElfObj *o = R->o;
	uint32_t y = r_sym(R, REF(RK_SYM, h->sym)), frag;

	if (h->bind) {
		elf_reg(o, y);
		elf_bind(o, y, h->bind);
		elf_symp(o, y)->flags &= ~ESYM_TEMP;
	}
	if (h->vis) {
		elf_reg(o, y);
		elf_vis(o, y, h->vis);
		elf_symp(o, y)->flags &= ~ESYM_TEMP;
	}
	frag = as_fragment_current(R->as);
	if (X86(R->c)) {
		/* x86 alignment participates in branch relaxation, including when
		 * an earlier branch has split the current subsection into frags. */
		R->nb.n = 0;
		vtext_format_append(&R->nb, "\t.p2align %u\n", h->p2);
		if (as_snippet_assemble(R->as, R->nb.p, R->nb.n)) return -1;
	} else
		elf_align(o, frag, h->p2, (elf_sec(o, elf_frag(o, frag)->sec)->flags & SHF_EXECINSTR) != 0, 0, 0);
	elf_reg(o, y);
	elf_type(o, y, h->fn ? STT_FUNC : STT_OBJECT);
	return as_label_define_symbol(R->as, y);
}

static int r_fend(rctx *R, uint32_t s)
{
	ElfObj *o = R->o;
	uint32_t y = r_sym(R, REF(RK_SYM, s)), l;
	ElfVal a, b;

	R->nb.n = 0;
	vtext_string_append(&R->nb, ".Lfe");
	vtext_unsigned_append(&R->nb, s);
	l = as_symbol_by_name(R->as, R->nb.p, R->nb.n);
	if (as_label_define_symbol(R->as, l))
		return -1;
	/* .size s, .Lfe<s>-s: the operands as the expression parser sees them, then folded */
	a = as_symbol_value(R->as, l, 0);
	b = as_symbol_value(R->as, y, 0);
	if (a.sub || b.sub || (!b.add && b.c))
		return r_fail(R, "vcc: unexpected .size operands");
	{
		ElfVal v = { a.add, b.add, a.c - b.c };
		ElfSym *x = v.add ? elf_symp(o, v.add) : NULL, *z = v.sub ? elf_symp(o, v.sub) : NULL;

		if (v.add && v.add == v.sub) {
			v.add = v.sub = 0;
		} else if (x && z && (x->flags & ESYM_DEFINED) && (z->flags & ESYM_DEFINED) && x->frag == z->frag) {
			v.c += (int64_t)x->value - (int64_t)z->value;
			v.add = v.sub = 0;
		}
		if (!v.add && !v.sub) {
			elf_symp(o, y)->size = (uint64_t)v.c;
			elf_symp(o, y)->flags &= ~ESYM_SIZE_EXPR;
		} else {
			elf_size_expr(o, y, v);
		}
	}
	return 0;
}

/* replay one chunk's items */
VPRIV int r_chunk(rctx *R, const char *p, size_t n)
{
	const char *e = p + n;

	while (p < e) {
		const item *it = (const item *)p;
		const char *q = (const char *)(it + 1);
		int r = 0;

		switch (it->kind) {
		case IT_TEXT:
			r = as_snippet_assemble(R->as, q, it->len);
			break;
		case IT_X64:
			r = x64_replay(R, q, it->len);
			break;
		case IT_CODE:
			r = a64_replay(R, (const code_hdr *)q);
			break;
		case IT_DATA:
			r = r_data(R, (const data_hdr *)q);
			break;
		case IT_LABEL:
			r = as_label_define_symbol(R->as, r_sym(R, *(const uint32_t *)q));
			break;
		case IT_SYMHDR:
			r = r_symhdr(R, (const symhdr *)q);
			break;
		case IT_FEND:
			r = r_fend(R, *(const uint32_t *)q);
			break;
		case IT_OSIZE: {
			const osize *z = (const osize *)q;
			ElfSym *y = elf_symp(R->o, r_sym(R, REF(RK_SYM, z->sym)));

			y->size = z->size;
			y->flags &= ~ESYM_SIZE_EXPR;
			break;
		}
		case IT_BYTES:
			if (it->len) {
				elf_data(R->o, as_fragment_current(R->as));
				elf_bytes(R->o, as_fragment_current(R->as), q, it->len);
			}
			break;
		}
		if (r || R->o->has_err || as_is_failed(R->as))
			return -1;
		p = q + ((it->len + 7) & ~7u);
	}
	return 0;
}
