/*
 * chunk-commit.c: the object-item stream: write a chunk's items while generating, and replay them into the
 * assembler and ElfObj to make the object (cc_options.emit == 2, `-c`).
 *
 * During generation a chunk is not text but a sequence of items (codegen-internal.h "object backend": CODEGEN_ITEM_TEXT,
 * CODEGEN_ITEM_CODE, CODEGEN_ITEM_X64, CODEGEN_ITEM_DATA, CODEGEN_ITEM_LABEL, CODEGEN_ITEM_BYTES, CODEGEN_ITEM_SYMBOL_HEADER, CODEGEN_ITEM_FUNCTION_END, CODEGEN_ITEM_OBJECT_SIZE), each a kind, a length
 * and a payload padded to 8 bytes: codegen_item_open/codegen_item_room_need/codegen_item_close build one in the function's text buffer
 * (`direct(e)` says whether this is the object path; otherwise those calls do nothing).  Replay
 * (codegen_commit_chunk, called by tu.c after the chunks are put in order) makes exactly the calls the assembler
 * would make for the text the printer would have produced, so the object is byte-identical to the
 * text path; bench/vcc-asdiff.py checks that.  A failure is reported through elf_error and returns -1,
 * which fails the TU.  Replay is single-threaded per TU.
 */
#include "codegen-internal.h"

VPRIV int codegen_commit_fail(codegen_commit_context *R, const char *msg)
{
	elf_error(R->object, "%s", msg);
	return -1;
}

VPRIV size_t codegen_item_open(codegen_context *e, uint32_t kind)
{
	tbuf *b = &e->out;
	size_t h;

	if (!codegen_context_is_direct(e))
		return SIZE_MAX;
	vtext_need(b, 16);
	b->n = (b->n + 7) & ~(size_t)7;
	h = b->n;
	((codegen_item *)(b->p + h))->kind = kind;
	b->n += sizeof(codegen_item);
	return h;
}

VPRIV void codegen_item_close(codegen_context *e, size_t h)
{
	tbuf *b = &e->out;

	if (h == SIZE_MAX)
		return;
	((codegen_item *)(b->p + h))->length = (uint32_t)(b->n - h - sizeof(codegen_item));
	vtext_need(b, 8);
	while (b->n & 7)
		b->p[b->n++] = 0;
}

VPRIV void *codegen_item_room_need(codegen_context *e, size_t n)
{
	void *p;

	vtext_need(&e->out, n + 8);
	p = e->out.p + e->out.n;
	e->out.n += n;
	return p;
}

/* the assembler's symbol for a reference (named as the printer names it) */
VPRIV uint32_t codegen_commit_symbol_find(codegen_commit_context *R, uint32_t ref)
{
	uint32_t *slot = NULL, i = CODEGEN_REFERENCE_INDEX(ref), y;

	switch (CODEGEN_REFERENCE_KIND(ref)) {
	case CODEGEN_REFERENCE_SYMBOL: slot = &R->symbol_cache[codegen_symbol_canonical(R->compile_context, i)]; break;
	case CODEGEN_REFERENCE_STRING: slot = &R->string_cache[i]; break;
	case CODEGEN_REFERENCE_LABEL: slot = &R->label_cache[i]; break;
	}
	if (slot && *slot)
		return *slot;
	R->name_buffer.n = 0;
	codegen_reference_name_append(R->compile_context, &R->name_buffer, ref);
	/* codegen_reference_name_append quotes non-ASCII x86 symbols for the text printer. */
	bool quoted = R->name_buffer.n >= 2 && R->name_buffer.p[0] == '"' && R->name_buffer.p[R->name_buffer.n - 1] == '"';
	y = as_symbol_by_name(R->assembler, R->name_buffer.p + quoted, R->name_buffer.n - 2 * quoted);
	if (slot)
		*slot = y;
	return y;
}

static void codegen_commit_zero_append(ElfObj *o, uint32_t frag, uint64_t n)
{
	if (n)
		elf_fill(o, frag, n, 0);
}

static int codegen_commit_data(codegen_commit_context *R, const codegen_data_header *h)
{
	const codegen_item_relocation *rel = (const codegen_item_relocation *)(h + 1);
	const uint8_t *d = (const uint8_t *)(rel + h->relocation_count);
	ElfObj *o = R->object;
	uint32_t frag = as_fragment_current(R->assembler);
	uint64_t at = 0;

	if (!h->size)
		return 0;
	elf_data(o, frag);
	if (h->zero) {
		elf_fill(o, frag, h->size, 0);
		return 0;
	}
	for (uint32_t k = 0; k <= h->relocation_count; k++) {
		uint64_t end = k < h->relocation_count ? rel[k].offset : h->size;

		if (end > at) {
			/* bytes in [at, end): what was initialised, then zeros */
			uint64_t have = at < h->count ? (end < h->count ? end : h->count) - at : 0;

			if (have)
				elf_bytes(o, frag, d + at, have);
			codegen_commit_zero_append(o, frag, end - at - have);
			at = end;
		}
		if (k < h->relocation_count) {
			uint32_t sz = rel[k].size_bytes ? rel[k].size_bytes : 8;
			uint64_t off = elf_frag(o, frag)->size;
			uint8_t *p = elf_grow(o, frag, sz);
			ElfVal v = as_symbol_value(R->assembler, codegen_commit_symbol_find(R, rel[k].reference), rel[k].addend);

			if (rel[k].subtrahend) {
				ElfVal s = as_symbol_value(R->assembler, codegen_commit_symbol_find(R, rel[k].subtrahend - 1), 0);

				v.sub = s.add, v.c -= s.c;
			}
			if (!v.add && !v.sub) {
				for (uint32_t b = 0; b < sz; b++)
					p[b] = (uint8_t)((uint64_t)v.c >> 8 * b);
			} else {
				memset(p, 0, sz);
				elf_fixup(o, frag, off, p, CODEGEN_IS_X64(R->compile_context) ? (sz == 1 ? R_X86_64_8 : sz == 2 ? R_X86_64_16 : sz == 4 ? R_X86_64_32 : R_X86_64_64) :
					  (sz == 4 ? R_AARCH64_ABS32 : R_AARCH64_ABS64), v);
			}
			at += sz;
		}
	}
	return 0;
}

static int codegen_commit_symbol_header(codegen_commit_context *R, const codegen_symbol_header *h)
{
	ElfObj *o = R->object;
	uint32_t y = codegen_commit_symbol_find(R, CODEGEN_REFERENCE_MAKE(CODEGEN_REFERENCE_SYMBOL, h->symbol)), frag;

	if (h->bind) {
		elf_reg(o, y);
		elf_bind(o, y, h->bind);
		elf_symp(o, y)->flags &= ~ESYM_TEMP;
	}
	if (h->visibility) {
		elf_reg(o, y);
		elf_vis(o, y, h->visibility);
		elf_symp(o, y)->flags &= ~ESYM_TEMP;
	}
	frag = as_fragment_current(R->assembler);
	if (CODEGEN_IS_X64(R->compile_context)) {
		/* x86 alignment participates in branch relaxation, including when
		 * an earlier branch has split the current subsection into frags. */
		R->name_buffer.n = 0;
		vtext_format_append(&R->name_buffer, "\t.p2align %u\n", h->alignment_log2);
		if (as_snippet_assemble(R->assembler, R->name_buffer.p, R->name_buffer.n)) return -1;
	} else
		elf_align(o, frag, h->alignment_log2, (elf_sec(o, elf_frag(o, frag)->sec)->flags & SHF_EXECINSTR) != 0, 0, 0);
	elf_reg(o, y);
	elf_type(o, y, h->is_function ? STT_FUNC : STT_OBJECT);
	return as_label_define_symbol(R->assembler, y);
}

static int codegen_commit_function_end(codegen_commit_context *R, uint32_t s)
{
	ElfObj *o = R->object;
	uint32_t y = codegen_commit_symbol_find(R, CODEGEN_REFERENCE_MAKE(CODEGEN_REFERENCE_SYMBOL, s)), l;
	ElfVal a, b;

	R->name_buffer.n = 0;
	vtext_string_append(&R->name_buffer, ".Lfe");
	vtext_unsigned_append(&R->name_buffer, s);
	l = as_symbol_by_name(R->assembler, R->name_buffer.p, R->name_buffer.n);
	if (as_label_define_symbol(R->assembler, l))
		return -1;
	/* .size s, .Lfe<s>-s: the operands as the expression parser sees them, then folded */
	a = as_symbol_value(R->assembler, l, 0);
	b = as_symbol_value(R->assembler, y, 0);
	if (a.sub || b.sub || (!b.add && b.c))
		return codegen_commit_fail(R, "vcc: unexpected .size operands");
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
VPRIV int codegen_commit_chunk(codegen_commit_context *R, const char *p, size_t n)
{
	const char *e = p + n;

	while (p < e) {
		const codegen_item *it = (const codegen_item *)p;
		const char *q = (const char *)(it + 1);
		int r = 0;

		switch (it->kind) {
		case CODEGEN_ITEM_TEXT:
			r = as_snippet_assemble(R->assembler, q, it->length);
			break;
		case CODEGEN_ITEM_X64:
			r = x64_replay(R, q, it->length);
			break;
		case CODEGEN_ITEM_CODE:
			r = a64_replay(R, (const codegen_code_header *)q);
			break;
		case CODEGEN_ITEM_DATA:
			r = codegen_commit_data(R, (const codegen_data_header *)q);
			break;
		case CODEGEN_ITEM_LABEL:
			r = as_label_define_symbol(R->assembler, codegen_commit_symbol_find(R, *(const uint32_t *)q));
			break;
		case CODEGEN_ITEM_SYMBOL_HEADER:
			r = codegen_commit_symbol_header(R, (const codegen_symbol_header *)q);
			break;
		case CODEGEN_ITEM_FUNCTION_END:
			r = codegen_commit_function_end(R, *(const uint32_t *)q);
			break;
		case CODEGEN_ITEM_OBJECT_SIZE: {
			const codegen_object_size *z = (const codegen_object_size *)q;
			ElfSym *y = elf_symp(R->object, codegen_commit_symbol_find(R, CODEGEN_REFERENCE_MAKE(CODEGEN_REFERENCE_SYMBOL, z->symbol)));

			y->size = z->size;
			y->flags &= ~ESYM_SIZE_EXPR;
			break;
		}
		case CODEGEN_ITEM_BYTES:
			if (it->length) {
				elf_data(R->object, as_fragment_current(R->assembler));
				elf_bytes(R->object, as_fragment_current(R->assembler), q, it->length);
			}
			break;
		}
		if (r || R->object->has_err || as_is_failed(R->assembler))
			return -1;
		p = q + ((it->length + 7) & ~7u);
	}
	return 0;
}
