/*
 * data-objects.c: emit one initialised or zero-initialised object (codegen_data_object_emit) or string literal
 * (codegen_string_literal_emit) into the unit's output.
 *
 * codegen_data_initializer_store walks a static initializer (the AST of `{ ... }`, strings, addresses, designators,
 * bit-fields) into e->data, the bytes, and e->rel, the relocations (symbol address plus addend at an
 * offset); a later initializer of the same element wins (codegen_data_relocations_sort).  The bytes then go out as
 * assembler directives in text mode, or as one data item of the object stream (codegen_data_item_append, obj.c) when
 * -c writes the object directly.  Targets differ in the directives and in the section (Mach-O and
 * thread-local layouts, wasm's one section per object); that is decided here, not in the arch
 * directories.  Every symbol the initializer names is recorded with note_ref so that lazily emitted
 * statics it points to get emitted too.  Errors (an object too large, a non-constant initializer)
 * longjmp to e->bail and drop this object only.
 */
#include "codegen-internal.h"
#include "common/softfloat.h"

/* ---------------- data ---------------- */

static void codegen_data_store(codegen_context *e, uint64_t off, uint64_t v, int n)
{
	if (off + (uint64_t)n > e->data.n) {
		if (off + (uint64_t)n > e->data.cap)
			codegen_fatal_report(e, 0, "static object too large");
		memset(e->data.p + e->data.n, 0, off + (uint64_t)n - e->data.n);
		e->data.n = off + (uint64_t)n;
	}
	for (int i = 0; i < n; i++)
		e->data.p[off + (uint64_t)i] = (char)(v >> (8 * i));
	if (unlikely(n && off < e->relocation_end))
		for (uint32_t j = 0; j < e->relocation_count; j++)	/* overwritten: the relocation goes */
			if (e->relocations[j].offset < off + (uint64_t)n && off < e->relocations[j].offset + (e->relocations[j].size_bytes ? e->relocations[j].size_bytes : 8))
				e->relocations[j].sequence = UINT32_MAX;
}

static void codegen_data_bits_or(codegen_context *e, uint64_t off, uint8_t bits)
{
	codegen_data_store(e, off, 0, 0);
	if (off >= e->data.n)
		codegen_data_store(e, off, 0, 1);
	e->data.p[off] |= (char)bits;
}

/* an n-byte address (8; 4 for wasm32) */
static void codegen_data_relocation_add(codegen_context *e, uint64_t off, uint32_t ref, int64_t add, int n)
{
	codegen_data_store(e, off, 0, n);
	e->relocations[e->relocation_count] = (struct codegen_data_relocation){ off, ref, e->relocation_count, add, 0, n == 8 ? 0 : (uint32_t)n };
	e->relocation_count++;
	if (off + (uint64_t)n > e->relocation_end)
		e->relocation_end = off + (uint64_t)n;
	codegen_reference_note(e, ref);
}

static void codegen_data_initializer_store(codegen_context *e, uint64_t off, ast_qualified_type ty, uint32_t init)
{
	cc *c = e->compile_context;
	const node *x;
	cc_value cv;

	if (!init)
		return;
	x = N(init);
	if (x->op == AST_NODE_CAST && x->aux == AST_CAST_BIT_CAST) {
		for (uint64_t pos = 0, size = codegen_type_size(c, ty); pos < size;) {
			unsigned char bytes[16], known[16];
			unsigned len = size - pos > 16 ? 16 : (unsigned)(size - pos);
			if (!cc_bit_cast_object_read(c, init, pos, len, bytes, known)) codegen_fatal_report(e, x->loc, "initializer element is not a compile-time constant");
			for (unsigned i = 0; i < len; i++) codegen_data_store(e, off + pos + i, bytes[i] & known[i], 1);
			pos += len;
		}
		return;
	}
	if (x->op == AST_NODE_ZERO)
		return;
	if (codegen_type_is_bool_vector(c, ty) && x->op == AST_NODE_CAST && x->aux == AST_CAST_SPLAT) {
		if (!cc_eval(c, x->a, &cv) || cv.kind != CC_VALUE_INTEGER)
			codegen_fatal_report(e, x->loc, "initializer element is not a compile-time constant");
		for (uint64_t k = 0; k < T(c, ty)->n; k++)
			if (cv.i) codegen_data_bits_or(e, off + k / 8, 1u << (k % 8));
		return;
	}
	if (x->op == AST_NODE_INITIALIZER) {
		const uint32_t *v = &c->lists[x->b];

		if (x->aux == 1) {
			ast_qualified_type et = T(c, ty)->base;
			uint64_t es = codegen_type_size(c, et);

			uint64_t n = 0;

			if (codegen_type_is_bool_vector(c, ty)) {
				for (uint32_t i = 0; i < x->c; i++) {
					if (!cc_eval(c, v[3*i+2], &cv) || cv.kind != CC_VALUE_INTEGER)
						codegen_fatal_report(e, x->loc, "initializer element is not a compile-time constant");
					for (uint64_t k = v[3*i]; k <= v[3*i+1]; k++) {
						uint64_t at = off + k / 8;
						unsigned m = 1u << (k % 8);
						codegen_data_bits_or(e, at, 0);
						e->data.p[at] = (char)((e->data.p[at] & ~m) | (cv.i ? m : 0));
					}
				}
				return;
			}
			for (uint32_t i = 0; i < x->c; i++) {
				for (uint64_t k = v[3 * i]; k <= v[3 * i + 1]; k++)
					codegen_data_initializer_store(e, off + k * es, et, v[3 * i + 2]);
				if (v[3 * i + 1] + 1 > n)
					n = v[3 * i + 1] + 1;
			}
			if (T(c, ty)->flags & AST_TYPE_FLAG_INCOMPLETE)
				codegen_data_store(e, off + n * es, 0, 0);	/* flexible array member: its initialized length counts */
			return;
		}
		{
			const tag *r = &c->tags[T(c, ty)->aux];

			for (uint32_t i = 0; i < x->c && i < r->nfield; i++) {
				const field *f = &r->f[i];

				if (x->aux == 2 && i != x->a)
					continue;
				if (!v[i])
					continue;
				if (f->bitfield) {
					uint64_t bo = f->off, w = f->width;
					u128 bits;

					if (!cc_eval(c, v[i], &cv) || (cv.kind != CC_VALUE_INTEGER && cv.kind != CC_VALUE_BITINT))
						codegen_fatal_report(e, x->loc, "initializer element is not a compile-time constant");
					/* The per-bit loop truncates to the field width. Keep the
					   full constant so shifts through bit 127 remain defined. */
					bits = (u128)cv.i;
					for (uint64_t k = 0; k < w; k++) {
						uint64_t at = off + (bo + k) / 8;
						uint8_t m = (uint8_t)(1u << ((bo + k) % 8));

						codegen_data_bits_or(e, at, 0);
						e->data.p[at] = (char)((e->data.p[at] & ~m) | ((cv.kind == CC_VALUE_BITINT ? (k/32 < cv.nbig && ((c->lists[cv.big+k/32] >> (k%32)) & 1)) : k < 128 ? ((bits >> k) & 1) : cv.i < 0) ? m : 0));
					}
					continue;
				}
				codegen_data_initializer_store(e, off + f->off / 8, f->ty, v[i]);
			}
		}
		return;
	}
	if (x->op == AST_NODE_COMMA) {
		codegen_data_initializer_store(e, off, ty, x->b);
		return;
	}
	{
		uint32_t selected;

		if (cc_initializer_subobject_find(c, init, &selected)) {
			codegen_data_initializer_store(e, off, ty, selected);
			return;
		}
	}
	if (x->op == AST_NODE_STRING && codegen_type_is_array(c, ty)) {
		const cstr *s = &c->strs[x->a];
		uint64_t n = (uint64_t)s->n * s->esz, cap = codegen_type_size(c, ty);

		if (T(c, ty)->flags & AST_TYPE_FLAG_INCOMPLETE)
			cap = n;
		if (n > cap)
			n = cap;
		for (uint64_t i = 0; i < n; i++)
			codegen_data_store(e, off + i, (uint8_t)s->s[i], 1);
		return;
	}
	if (x->op == AST_NODE_COMPOUND_LITERAL) {
		codegen_data_initializer_store(e, off, ty, x->a);
		return;
	}
	if (x->op == AST_NODE_COMPLEX) {
		ast_qualified_type et = c->tags[T(c, x->ty)->aux].f[0].ty;

		codegen_data_initializer_store(e, off, et, x->a);
		codegen_data_initializer_store(e, off + codegen_type_size(c, et), et, x->b);
		return;
	}
	if (x->op == AST_NODE_CAST && x->aux == AST_CAST_NOP && codegen_type_is_aggregate(c, ty) && !codegen_type_is_bitint_big(c, ty)) {
		codegen_data_initializer_store(e, off, ty, x->a);
		return;
	}
	if (codegen_type_is_aggregate(c, ty) && !codegen_type_is_bitint_big(c, ty)) {
		/* a const aggregate object as the initializer (clang extension) */
		uint32_t m = codegen_expression_casts_strip(c, init);

		if (N(m)->op == AST_NODE_VARIABLE && c->syms[N(m)->a].body) {
			codegen_data_initializer_store(e, off, ty, c->syms[N(m)->a].body);
			return;
		}
		codegen_fatal_report(e, x->loc, "initializer element is not a compile-time constant");
	}
	if (!cc_eval(c, init, &cv))
		codegen_fatal_report(e, x->loc, "initializer element is not a compile-time constant");
	{
		int n = (int)codegen_type_size(c, ty);

		switch (cv.kind) {
		case CC_VALUE_BITINT:
			for (unsigned i = 0; i < cv.nbig; i++)
				codegen_data_store(e, off + 4 * i, c->lists[cv.big + i], 4);
			return;
		case CC_VALUE_INTEGER:
			if (codegen_type_is_bitint_big(c, ty)) {
				for (int i = 0; i < n; i += 8)
					codegen_data_store(e, off + i, i < 16 ? (uint64_t)((u128)cv.i >> (8*i)) : cv.i < 0 ? UINT64_MAX : 0, 8);
				return;
			}
			if (n == 16) {
				codegen_data_store(e, off, (uint64_t)(u128)cv.i, 8);
				codegen_data_store(e, off + 8, (uint64_t)((u128)cv.i >> 64), 8);
			} else {
				codegen_data_store(e, off, (uint64_t)cv.i, n);
			}
			return;
		case CC_VALUE_FLOAT:
			if (n <= 8 && cv.ld && vsoftfloat_is_nan(cv.q)) {
				codegen_data_store(e, off, vsoftfloat_nan_storage(cv.q, KIND(c, ty) == AST_TYPE_BFLOAT16 ? 7 : n == 2 ? 10 : n == 4 ? 23 : 52, KIND(c, ty) == AST_TYPE_BFLOAT16 ? 8 : n == 2 ? 5 : n == 4 ? 8 : 11), n);
				return;
			}
			if (n == 2) {
				codegen_data_store(e, off, (KIND(c, ty) == AST_TYPE_BFLOAT16 ? vsoftfloat_to_bfloat16(cv.ld ? cv.q : vsoftfloat_of_double(cv.f)) : vsoftfloat_to_half(cv.ld ? cv.q : vsoftfloat_of_double(cv.f))), 2);
			} else if (n == 4) {
				float f = (float)cv.f;
				uint32_t u;

				memcpy(&u, &f, 4);
				codegen_data_store(e, off, u, 4);
			} else {
				uint64_t u;

				if (n == 16 && CODEGEN_IS_X64(c) && KIND(c, ty) == AST_TYPE_LONG_DOUBLE) {	/* x87: 10 bytes, 6 of padding */
					u128 q = cv.ld ? cv.q : (u128)ld_of_d(cv.f);
					uint8_t xb[10];
					uint64_t lo;

					vsoftfloat_to_x87(q, xb);
					memcpy(&lo, xb, 8);
					codegen_data_store(e, off, lo, 8);
					codegen_data_store(e, off + 8, (uint64_t)xb[8] | (uint64_t)xb[9] << 8, 8);
					return;
				}
				if (n == 16) {
					u128 q = cv.ld ? cv.q : (u128)ld_of_d(cv.f);

					codegen_data_store(e, off, (uint64_t)q, 8);
					codegen_data_store(e, off + 8, (uint64_t)(q >> 64), 8);
					return;
				}
				memcpy(&u, &cv.f, 8);
				codegen_data_store(e, off, u, 8);
			}
			return;
		case CC_VALUE_ADDRESS:
			if (WASM(c) && cv.label) {
				/* wasm: a label's address is its number (PLAN-wasm.md W4) */
				codegen_data_store(e, off, (uint64_t)cv.label - (cv.sublabel ? (uint64_t)cv.sublabel : 0) + (uint64_t)cv.i, n);
				return;
			}
			if (cv.sublabel && (n == 4 || n == 8)) {
				/* label difference: ref - sub, resolved by the assembler */
				codegen_data_store(e, off, 0, n);
				e->relocations[e->relocation_count] = (struct codegen_data_relocation){ off, CODEGEN_REFERENCE_MAKE(CODEGEN_REFERENCE_LABEL, cv.label - 1), e->relocation_count, (int64_t)cv.i,
								  CODEGEN_REFERENCE_MAKE(CODEGEN_REFERENCE_LABEL, cv.sublabel - 1) + 1, (uint32_t)n };
				e->relocation_count++;
				if (off + n > e->relocation_end)
					e->relocation_end = off + n;
				codegen_reference_note(e, CODEGEN_REFERENCE_MAKE(CODEGEN_REFERENCE_LABEL, cv.label - 1));
				codegen_reference_note(e, CODEGEN_REFERENCE_MAKE(CODEGEN_REFERENCE_LABEL, cv.sublabel - 1));
				return;
			}
			if (n != 8 && !(n == 4 && WASM32(c)))
				codegen_fatal_report(e, x->loc, "address initializer for a %d-byte object", n);
			if (cv.label)
				codegen_data_relocation_add(e, off, CODEGEN_REFERENCE_MAKE(CODEGEN_REFERENCE_LABEL, cv.label - 1), (int64_t)cv.i, n);
			else if (cv.str)
				codegen_data_relocation_add(e, off, CODEGEN_REFERENCE_MAKE(CODEGEN_REFERENCE_STRING, cv.str - 1), (int64_t)cv.i, n);
			else {
				codegen_data_relocation_add(e, off, CODEGEN_REFERENCE_MAKE(CODEGEN_REFERENCE_SYMBOL, codegen_symbol_canonical(c, cv.sym)), (int64_t)cv.i, n);
				if (WASM(c))
					wasm_data_ref(e, CODEGEN_REFERENCE_MAKE(CODEGEN_REFERENCE_SYMBOL, codegen_symbol_canonical(c, cv.sym)));
			}
			return;
		default:
			codegen_fatal_report(e, x->loc, "initializer element is not a compile-time constant");
		}
	}
}

static int codegen_data_relocation_compare(const void *a, const void *b)
{
	const struct codegen_data_relocation *x = a, *y = b;

	if (x->offset != y->offset)
		return x->offset < y->offset ? -1 : 1;
	return x->sequence < y->sequence ? -1 : x->sequence > y->sequence;
}

/* sort e->rel by offset; a later initializer for the same element overrides: keep the last */
static void codegen_data_relocations_sort(codegen_context *e)
{
	uint32_t k = 0;

	qsort(e->relocations, e->relocation_count, sizeof(e->relocations[0]), codegen_data_relocation_compare);
	for (uint32_t j = 0; j < e->relocation_count; j++) {
		if (e->relocations[j].sequence == UINT32_MAX)
			continue;
		if (k && e->relocations[k - 1].offset == e->relocations[j].offset)
			k--;
		e->relocations[k++] = e->relocations[j];
	}
	e->relocation_count = k;
}

/* the object backend's data item: what codegen_data_directives_append / .zero would assemble to */
static void codegen_data_item_append(codegen_context *e, uint64_t size, bool zero)
{
	size_t h = codegen_item_open(e, CODEGEN_ITEM_DATA);
	codegen_data_header *dh = codegen_item_room_need(e, sizeof *dh);
	uint32_t nrel = 0;

	if (zero) {
		*dh = (codegen_data_header){ .size = size, .zero = 1 };
	} else {
		codegen_data_relocations_sort(e);
		while (nrel < e->relocation_count && e->relocations[nrel].offset < size)
			nrel++;
		*dh = (codegen_data_header){ .size = size, .count = e->data.n < size ? e->data.n : size, .relocation_count = nrel };
		memcpy(codegen_item_room_need(e, nrel * sizeof(codegen_item_relocation)), e->relocations, nrel * sizeof(codegen_item_relocation));
		memcpy(codegen_item_room_need(e, dh->count), e->data.p, dh->count);
	}
	codegen_item_close(e, h);
}

/* the bytes in e->data with e->rel, as directives */
static void codegen_data_directives_append(codegen_context *e, tbuf *b, uint64_t size)
{
	uint64_t o = 0;
	uint32_t ri = 0;
	const unsigned char *d = (const unsigned char *)e->data.p;

	codegen_data_relocations_sort(e);
	while (o < size) {
		uint64_t end = ri < e->relocation_count ? e->relocations[ri].offset : size;

		if (ri < e->relocation_count && o == e->relocations[ri].offset) {
			uint32_t sz = e->relocations[ri].size_bytes ? e->relocations[ri].size_bytes : 8;

			vtext_string_append(b, sz == 4 ? (WASM(e->compile_context) ? "\t.int32\t" : CODEGEN_IS_X64(e->compile_context) ? "\t.long\t" : "\t.word\t") : WASM(e->compile_context) ? "\t.int64\t" : CODEGEN_IS_X64(e->compile_context) ? "\t.quad\t" : "\t.xword\t");
			codegen_reference_offset_append(e->compile_context, b, e->relocations[ri].reference, e->relocations[ri].addend);
			if (e->relocations[ri].subtrahend) {
				vtext_char_append(b, '-');
				codegen_reference_name_append(e->compile_context, b, e->relocations[ri].subtrahend - 1);
			}
			vtext_char_append(b, '\n');
			o += sz;
			ri++;
			continue;
		}
		while (o < end) {
			uint64_t z = o;

			while (z < end && (z >= e->data.n || !d[z]))
				z++;
			if (z - o >= 8 || z == end) {
				if (z > o) {
					vtext_string_append(b, "\t.zero\t");
					vtext_unsigned_append(b, z - o);
					vtext_char_append(b, '\n');
				}
				o = z;
				continue;
			}
			if (!(o & 7) && o + 8 <= end) {
				uint64_t w = 0;

				/* o < data.n here; past it the bytes are zero, not what the buffer holds */
				memcpy(&w, d + o, o + 8 <= e->data.n ? 8 : e->data.n - o);
				vtext_string_append(b, WASM(e->compile_context) ? "\t.int64\t" : CODEGEN_IS_X64(e->compile_context) ? "\t.quad\t" : "\t.xword\t");
				vtext_hex_prefixed_append(b, w);
				vtext_char_append(b, '\n');
				o += 8;
				continue;
			}
			vtext_string_append(b, "\t.byte\t");
			vtext_unsigned_append(b, o < e->data.n ? d[o] : 0);
			vtext_char_append(b, '\n');
			o++;
		}
	}
}

static bool codegen_type_is_all_const(cc *c, ast_qualified_type t)
{
	if (AST_QUALIFIED_TYPE_QUALIFIERS(t) & AST_QUALIFIER_CONST)
		return true;
	if (codegen_type_is_array(c, t))
		return codegen_type_is_all_const(c, T(c, t)->base);
	return false;
}

VPRIV void codegen_data_object_emit(codegen_context *e, uint32_t s)
{
	cc *c = e->compile_context;
	const sym *y = &c->syms[s];
	tbuf *b = &e->out;
	size_t start = b->n;
	uint64_t size = codegen_type_size(c, y->ty);
	uint32_t align = codegen_symbol_alignment(c, s);
	const attrs *a = codegen_symbol_attributes(c, s);
	bool zero;

	if (_setjmp(e->bail_jump))
		return;
	e->data.n = 0;
	e->relocation_count = 0, e->relocation_end = 0;
	e->is_reachable = true;
	if (y->body)
		codegen_data_initializer_store(e, 0, y->ty, y->body);
	if (e->data.n > size)
		size = e->data.n;	/* flexible array member initialised */
	if (size == 0 && !y->body && codegen_type_is_array(c, y->ty) && (T(c, y->ty)->flags & AST_TYPE_FLAG_INCOMPLETE))
		size = codegen_type_size(c, T(c, y->ty)->base);	/* int x[]; is one element */
	zero = e->relocation_count == 0;
	for (uint64_t i = 0; zero && i < e->data.n; i++)
		zero = !e->data.p[i];
	if (WASM(c)) {
		/* one section per object, as clang writes them */
		bool global = y->sc != AST_STORAGE_STATIC && y->depth == 0 && y->name;

		/* _Thread_local: an ordinary object, as LLVM strips TLS from a
		 * module without shared memory (no -pthread) */
		vtext_string_append(b, "\t.section\t");
		if (a->section) {
			vtext_string_append(b, c->strs[a->section - 1].s);
		} else {
			/* a quoted name: the whole section name in quotes, as clang */
			bool q = codegen_symbol_name_is_quoted(c, s);
			size_t at;

			if (q)
				vtext_char_append(b, '"');
			vtext_string_append(b, codegen_type_is_all_const(c, y->ty) ? ".rodata." : zero ? ".bss." : ".data.");
			at = b->n;
			codegen_symbol_name_append(c, b, s);
			if (q) {
				memmove(b->p + at, b->p + at + 1, b->n - at - 1);
				b->n--;
			}
		}
		vtext_string_append(b, ",\"\",@\n");
		if (global) {
			vtext_string_append(b, (y->flags & AST_SYMBOL_FLAG_WEAK) ? "\t.weak\t" : "\t.globl\t");
			codegen_symbol_name_append(c, b, s);
			vtext_char_append(b, '\n');
		}
		vtext_string_append(b, "\t.p2align\t");
		vtext_unsigned_append(b, (uint64_t)__builtin_ctz(align ? align : 1));
		vtext_string_append(b, "\n\t.type\t");
		codegen_symbol_name_append(c, b, s);
		vtext_string_append(b, ",@object\n");
		codegen_symbol_name_append(c, b, s);
		vtext_string_append(b, ":\n");
		if (zero && !e->relocation_count) {
			vtext_string_append(b, "\t.zero\t");
			vtext_unsigned_append(b, size ? size : 1);
			vtext_char_append(b, '\n');
		} else {
			codegen_data_directives_append(e, b, size);
		}
		vtext_string_append(b, "\t.size\t");
		codegen_symbol_name_append(c, b, s);
		vtext_string_append(b, ", ");
		vtext_unsigned_append(b, size ? size : 1);
		vtext_char_append(b, '\n');
		e->unit->object_total++;
		codegen_chunk_add(e, ((uint64_t)s << 1) | 1, start);
		return;
	}
	if (CODEGEN_IS_WIN64(c)) {
		/* COFF (clang's sections): .data, .bss, .rdata; thread-locals in .tls$, which the
		   linker puts between the CRT's __tls_start and __tls_end; no .type or .size.  -c: the
		   same text, as items the TU's assembler reads in place (codegen_item_open) */
		size_t h;

		if (codegen_context_is_direct(e)) {
			b->n = (b->n + 7) & ~(size_t)7;
			start = b->n;
		}
		codegen_symbol_header_append(e, b, s, false, (y->flags & AST_SYMBOL_FLAG_THREAD) && !a->section ? "\t.section\t.tls$,\"dw\"\n" :
			   codegen_type_is_all_const(c, y->ty) ? "\t.section\t.rdata,\"dr\"\n" : zero ? "\t.bss\n" : "\t.data\n", align);
		h = codegen_item_open(e, CODEGEN_ITEM_TEXT);
		if (zero && !e->relocation_count) {
			vtext_string_append(b, "\t.zero\t");
			vtext_unsigned_append(b, size ? size : 1);
			vtext_char_append(b, '\n');
		} else {
			codegen_data_directives_append(e, b, size);
		}
		codegen_item_close(e, h);
		e->unit->object_total++;
		codegen_chunk_add(e, ((uint64_t)s << 1) | 1, start);
		return;
	}
	if (!a->section && (y->flags & AST_SYMBOL_FLAG_THREAD) && !CODEGEN_IS_DARWIN(c)) {
		/* ELF: .tdata/.tbss; code reaches it through A64_OP_TLV (lval.c) */
		const char *sec = zero && !e->relocation_count ? "\t.section\t.tbss,\"awT\",@nobits\n" : "\t.section\t.tdata,\"awT\",@progbits\n";

		if (codegen_context_is_direct(e)) {
			size_t h;

			b->n = (b->n + 7) & ~(size_t)7;
			start = b->n;
			codegen_symbol_header_append(e, b, s, false, sec, align);
			codegen_data_item_append(e, zero && !e->relocation_count ? (size ? size : 1) : size, zero && !e->relocation_count);
			h = codegen_item_open(e, CODEGEN_ITEM_OBJECT_SIZE);
			*(codegen_object_size *)codegen_item_room_need(e, sizeof(codegen_object_size)) = (codegen_object_size){ .symbol = s, .size = size };
			codegen_item_close(e, h);
			e->unit->object_total++;
			codegen_chunk_add(e, ((uint64_t)s << 1) | 1, start);
			return;
		}
		codegen_symbol_header_append(e, b, s, false, sec, align);
		if (zero && !e->relocation_count) {
			vtext_string_append(b, "\t.zero\t");
			vtext_unsigned_append(b, size ? size : 1);
			vtext_char_append(b, '\n');
		} else {
			codegen_data_directives_append(e, b, size);
		}
		vtext_string_append(b, "\t.size\t");
		codegen_symbol_name_append(c, b, s);
		vtext_string_append(b, ", ");
		vtext_unsigned_append(b, size);
		vtext_char_append(b, '\n');
		e->unit->object_total++;
		codegen_chunk_add(e, ((uint64_t)s << 1) | 1, start);
		return;
	}
	if (!a->section && (y->flags & AST_SYMBOL_FLAG_THREAD) && !codegen_context_is_direct(e) && !CODEGEN_IS_DARWIN(e->compile_context))
		codegen_fatal_report(e, y->loc, "thread-local storage is not supported");
	if (!codegen_context_is_direct(e) && CODEGEN_IS_DARWIN(e->compile_context) && (y->flags & AST_SYMBOL_FLAG_THREAD)) {
		/* the same TLV layout as text; a64_darwin_text_translate names the sections */
		bool global = y->sc != AST_STORAGE_STATIC && y->depth == 0 && y->name;

		vtext_string_append(b, zero ? "\t.section\t.tbss,\"aw\",@nobits\n" : "\t.section\t.tdata,\"aw\",@progbits\n");
		vtext_string_append(b, "\t.p2align\t");
		vtext_unsigned_append(b, (uint64_t)__builtin_ctz(align ? align : 1));
		vtext_char_append(b, '\n');
		codegen_symbol_name_append(c, b, s);
		vtext_string_append(b, "$tlv$init:\n");
		if (zero && !e->relocation_count) {
			vtext_string_append(b, "\t.zero\t");
			vtext_unsigned_append(b, size ? size : 1);
			vtext_char_append(b, '\n');
		} else {
			codegen_data_directives_append(e, b, size);
		}
		vtext_string_append(b, "\t.section\t.tlv,\"aw\",@progbits\n");
		if (global) {
			vtext_string_append(b, (y->flags & AST_SYMBOL_FLAG_WEAK) ? "\t.weak\t" : "\t.globl\t");
			codegen_symbol_name_append(c, b, s);
			vtext_char_append(b, '\n');
		}
		vtext_string_append(b, "\t.p2align\t3\n");
		codegen_symbol_name_append(c, b, s);
		vtext_string_append(b, ":\n\t.xword\t__tlv_bootstrap\n\t.xword\t0\n\t.xword\t");
		codegen_symbol_name_append(c, b, s);
		vtext_string_append(b, "$tlv$init\n");
		e->unit->object_total++;
		codegen_chunk_add(e, ((uint64_t)s << 1) | 1, start);
		return;
	}
	if (codegen_context_is_direct(e) && (y->flags & AST_SYMBOL_FLAG_THREAD)) {
		/*
		 * Darwin TLV: the initial value in __thread_data/__thread_bss under
		 * a local label, the symbol itself on a descriptor in
		 * __thread_vars: { __tlv_bootstrap, 0, initial value }.
		 */
		size_t h;
		bool global = y->sc != AST_STORAGE_STATIC && y->depth == 0 && y->name;

		b->n = (b->n + 7) & ~(size_t)7;
		start = b->n;
		h = codegen_item_open(e, CODEGEN_ITEM_TEXT);
		vtext_string_append(b, zero ? "\t.section\t.tbss,\"aw\",@nobits\n" : "\t.section\t.tdata,\"aw\",@progbits\n");
		vtext_string_append(b, "\t.p2align\t");
		vtext_unsigned_append(b, (uint64_t)__builtin_ctz(align ? align : 1));
		vtext_char_append(b, '\n');
		codegen_symbol_name_append(c, b, s);
		vtext_string_append(b, "$tlv$init:\n");
		codegen_item_close(e, h);
		codegen_data_item_append(e, zero && !e->relocation_count ? (size ? size : 1) : size, zero && !e->relocation_count);
		h = codegen_item_open(e, CODEGEN_ITEM_TEXT);
		vtext_string_append(b, "\t.section\t.tlv,\"aw\",@progbits\n");
		if (global) {
			vtext_string_append(b, (y->flags & AST_SYMBOL_FLAG_WEAK) ? "\t.weak\t" : "\t.globl\t");
			codegen_symbol_name_append(c, b, s);
			vtext_char_append(b, '\n');
		}
		vtext_string_append(b, "\t.p2align\t3\n");
		codegen_symbol_name_append(c, b, s);
		vtext_string_append(b, ":\n\t.xword\t__tlv_bootstrap\n\t.xword\t0\n\t.xword\t");
		codegen_symbol_name_append(c, b, s);
		vtext_string_append(b, "$tlv$init\n");
		codegen_item_close(e, h);
		e->unit->object_total++;
		codegen_chunk_add(e, ((uint64_t)s << 1) | 1, start);
		return;
	}
	if (codegen_context_is_direct(e)) {
		size_t h;

		b->n = (b->n + 7) & ~(size_t)7;
		start = b->n;
		codegen_symbol_header_append(e, b, s, false,
			   codegen_type_is_all_const(c, y->ty) ? "\t.section\t.rodata,\"a\",@progbits\n" : zero ? "\t.bss\n" : "\t.data\n", align);
		codegen_data_item_append(e, zero && !e->relocation_count ? (size ? size : 1) : size, zero && !e->relocation_count);
		h = codegen_item_open(e, CODEGEN_ITEM_OBJECT_SIZE);
		*(codegen_object_size *)codegen_item_room_need(e, sizeof(codegen_object_size)) = (codegen_object_size){ .symbol = s, .size = size };
		codegen_item_close(e, h);
		e->unit->object_total++;
		codegen_chunk_add(e, ((uint64_t)s << 1) | 1, start);
		return;
	}
	codegen_symbol_header_append(e, b, s, false,
		   codegen_type_is_all_const(c, y->ty) ? "\t.section\t.rodata,\"a\",@progbits\n" : zero ? "\t.bss\n" : "\t.data\n", align);
	if (zero && !e->relocation_count) {
		if (a->section && !strncmp(c->strs[a->section - 1].s, ".bss", 4)) {
			vtext_string_append(b, "\t.zero\t");
			vtext_unsigned_append(b, size ? size : 1);
			vtext_char_append(b, '\n');
		} else {
			vtext_string_append(b, "\t.zero\t");
			vtext_unsigned_append(b, size ? size : 1);
			vtext_char_append(b, '\n');
		}
	} else {
		codegen_data_directives_append(e, b, size);
	}
	vtext_string_append(b, "\t.size\t");
	codegen_symbol_name_append(c, b, s);
	vtext_string_append(b, ", ");
	vtext_unsigned_append(b, size);
	vtext_char_append(b, '\n');
	e->unit->object_total++;
	codegen_chunk_add(e, ((uint64_t)s << 1) | 1, start);
}

VPRIV void codegen_string_literal_emit(codegen_context *e, uint32_t i)
{
	cc *c = e->compile_context;
	const cstr *s = &c->strs[i];
	tbuf *b = &e->out;
	size_t start = b->n;
	bool nul = false;
	uint64_t n = (uint64_t)s->n * s->esz;

	/* SHF_STRINGS splits at zero code units, including wide ones.
	   Only the final terminator may be merged independently. */
	for (uint64_t k = 0; k + s->esz < n; k += s->esz) {
		uint32_t unit = 0;

		memcpy(&unit, s->s + k, s->esz);
		if (!unit) {
			nul = true;
			break;
		}
	}
	if (codegen_context_is_direct(e) && !CODEGEN_IS_WIN64(c)) {	/* (COFF: the text below, assembled in place) */
		size_t h;
		uint32_t ref = CODEGEN_REFERENCE_MAKE(CODEGEN_REFERENCE_STRING, i);
		bool asciz = s->esz == 1 && !nul;
		uint64_t m = asciz ? (n ? n - 1 : 0) : n;
		char *q;

		b->n = (b->n + 7) & ~(size_t)7;
		start = b->n;
		h = codegen_item_open(e, CODEGEN_ITEM_TEXT);
		if (nul)
			vtext_string_append(b, "\t.section\t.rodata,\"a\",@progbits\n");
		else
			vtext_format_append(b, "\t.section\t.rodata.str%u.%u,\"aMS\",@progbits,%u\n", s->esz, s->esz, s->esz);
		if (s->esz > 1)
			vtext_format_append(b, "\t.p2align\t%d\n", s->esz == 2 ? 1 : 2);
		codegen_item_close(e, h);
		h = codegen_item_open(e, CODEGEN_ITEM_LABEL);
		memcpy(codegen_item_room_need(e, 4), &ref, 4);
		codegen_item_close(e, h);
		h = codegen_item_open(e, CODEGEN_ITEM_BYTES);
		q = codegen_item_room_need(e, m + asciz);
		memcpy(q, s->s, m);
		if (asciz)
			q[m] = 0;
		codegen_item_close(e, h);
		codegen_chunk_add(e, ((uint64_t)1 << 62) | i, start);
		return;
	}
	size_t th = SIZE_MAX;

	if (codegen_context_is_direct(e)) {	/* (COFF -c: the text as an item) */
		b->n = (b->n + 7) & ~(size_t)7;
		start = b->n;
		th = codegen_item_open(e, CODEGEN_ITEM_TEXT);
	}
	if (WASM(c))
		vtext_format_append(b, "\t.section\t.rodata..L.str.%u,\"\",@\n", i);
	else if (CODEGEN_IS_WIN64(c))
		vtext_string_append(b, "\t.section\t.rdata,\"dr\"\n");
	else if (nul)
		vtext_string_append(b, "\t.section\t.rodata,\"a\",@progbits\n");
	else
		vtext_format_append(b, "\t.section\t.rodata.str%u.%u,\"aMS\",@progbits,%u\n", s->esz, s->esz, s->esz);
	if (s->esz > 1)
		vtext_format_append(b, "\t.p2align\t%d\n", s->esz == 2 ? 1 : 2);
	vtext_string_append(b, ".L.str.");
	vtext_unsigned_append(b, i);
	vtext_string_append(b, ":\n");
	if (s->esz == 1 && !nul) {
		vtext_string_append(b, "\t.asciz\t\"");
		for (uint64_t k = 0; k + 1 < n; k++) {
			unsigned char ch = (unsigned char)s->s[k];

			if (ch == '"' || ch == '\\') {
				vtext_char_append(b, '\\');
				vtext_char_append(b, (char)ch);
			} else if (ch >= 32 && ch < 127) {
				vtext_char_append(b, (char)ch);
			} else {
				vtext_char_append(b, '\\');
				vtext_char_append(b, (char)('0' + (ch >> 6)));
				vtext_char_append(b, (char)('0' + ((ch >> 3) & 7)));
				vtext_char_append(b, (char)('0' + (ch & 7)));
			}
		}
		vtext_string_append(b, "\"\n");
	} else {
		for (uint64_t k = 0; k < n; k += s->esz) {
			uint32_t v = 0;

			memcpy(&v, s->s + k, s->esz);
			vtext_string_append(b, s->esz == 1 ? "\t.byte\t" : s->esz == 2 ? "\t.short\t" : WASM(c) || CODEGEN_IS_X64(c) ? "\t.long\t" : "\t.word\t");
			vtext_unsigned_append(b, v);
			vtext_char_append(b, '\n');
		}
	}
	if (WASM(c))
		vtext_format_append(b, "\t.size\t.L.str.%u, %llu\n", i, (unsigned long long)n);
	if (th != SIZE_MAX)
		codegen_item_close(e, th);
	codegen_chunk_add(e, ((uint64_t)1 << 62) | i, start);
}
