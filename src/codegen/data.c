/*
 * data.c: emit one initialised or zero-initialised object (emit_object) or string literal
 * (emit_string) into the unit's output.
 *
 * data_init walks a static initializer (the AST of `{ ... }`, strings, addresses, designators,
 * bit-fields) into e->data, the bytes, and e->rel, the relocations (symbol address plus addend at an
 * offset); a later initializer of the same element wins (rel_prep).  The bytes then go out as
 * assembler directives in text mode, or as one data item of the object stream (data_item, obj.c) when
 * -c writes the object directly.  Targets differ in the directives and in the section (Mach-O and
 * thread-local layouts, wasm's one section per object); that is decided here, not in the arch
 * directories.  Every symbol the initializer names is recorded with note_ref so that lazily emitted
 * statics it points to get emitted too.  Errors (an object too large, a non-constant initializer)
 * longjmp to e->bail and drop this object only.
 */
#include "emitimpl.h"
#include "common/softfloat.h"

/* ---------------- data ---------------- */

static void data_put(ecx *e, uint64_t off, uint64_t v, int n)
{
	if (off + (uint64_t)n > e->data.n) {
		if (off + (uint64_t)n > e->data.cap)
			efatal(e, 0, "static object too large");
		memset(e->data.p + e->data.n, 0, off + (uint64_t)n - e->data.n);
		e->data.n = off + (uint64_t)n;
	}
	for (int i = 0; i < n; i++)
		e->data.p[off + (uint64_t)i] = (char)(v >> (8 * i));
	if (unlikely(n && off < e->relend))
		for (uint32_t j = 0; j < e->nrel; j++)	/* overwritten: the relocation goes */
			if (e->rel[j].off < off + (uint64_t)n && off < e->rel[j].off + (e->rel[j].sz ? e->rel[j].sz : 8))
				e->rel[j].seq = UINT32_MAX;
}

static void data_or(ecx *e, uint64_t off, uint8_t bits)
{
	data_put(e, off, 0, 0);
	if (off >= e->data.n)
		data_put(e, off, 0, 1);
	e->data.p[off] |= (char)bits;
}

/* an n-byte address (8; 4 for wasm32) */
static void data_reloc(ecx *e, uint64_t off, uint32_t ref, int64_t add, int n)
{
	data_put(e, off, 0, n);
	e->rel[e->nrel] = (struct reloc){ off, ref, e->nrel, add, 0, n == 8 ? 0 : (uint32_t)n };
	e->nrel++;
	if (off + (uint64_t)n > e->relend)
		e->relend = off + (uint64_t)n;
	note_ref(e, ref);
}

static void data_init(ecx *e, uint64_t off, ast_qualified_type ty, uint32_t init)
{
	cc *c = e->c;
	const node *x;
	cc_value cv;

	if (!init)
		return;
	x = N(init);
	if (x->op == AST_NODE_CAST && x->aux == AST_CAST_BIT_CAST) {
		for (uint64_t pos = 0, size = tsize(c, ty); pos < size;) {
			unsigned char bytes[16], known[16];
			unsigned len = size - pos > 16 ? 16 : (unsigned)(size - pos);
			if (!cc_bit_cast_object_read(c, init, pos, len, bytes, known)) efatal(e, x->loc, "initializer element is not a compile-time constant");
			for (unsigned i = 0; i < len; i++) data_put(e, off + pos + i, bytes[i] & known[i], 1);
			pos += len;
		}
		return;
	}
	if (x->op == AST_NODE_ZERO)
		return;
	if (is_bvec(c, ty) && x->op == AST_NODE_CAST && x->aux == AST_CAST_SPLAT) {
		if (!cc_eval(c, x->a, &cv) || cv.kind != CC_VALUE_INTEGER)
			efatal(e, x->loc, "initializer element is not a compile-time constant");
		for (uint64_t k = 0; k < T(c, ty)->n; k++)
			if (cv.i) data_or(e, off + k / 8, 1u << (k % 8));
		return;
	}
	if (x->op == AST_NODE_INITIALIZER) {
		const uint32_t *v = &c->lists[x->b];

		if (x->aux == 1) {
			ast_qualified_type et = T(c, ty)->base;
			uint64_t es = tsize(c, et);

			uint64_t n = 0;

			if (is_bvec(c, ty)) {
				for (uint32_t i = 0; i < x->c; i++) {
					if (!cc_eval(c, v[3*i+2], &cv) || cv.kind != CC_VALUE_INTEGER)
						efatal(e, x->loc, "initializer element is not a compile-time constant");
					for (uint64_t k = v[3*i]; k <= v[3*i+1]; k++) {
						uint64_t at = off + k / 8;
						unsigned m = 1u << (k % 8);
						data_or(e, at, 0);
						e->data.p[at] = (char)((e->data.p[at] & ~m) | (cv.i ? m : 0));
					}
				}
				return;
			}
			for (uint32_t i = 0; i < x->c; i++) {
				for (uint64_t k = v[3 * i]; k <= v[3 * i + 1]; k++)
					data_init(e, off + k * es, et, v[3 * i + 2]);
				if (v[3 * i + 1] + 1 > n)
					n = v[3 * i + 1] + 1;
			}
			if (T(c, ty)->flags & AST_TYPE_FLAG_INCOMPLETE)
				data_put(e, off + n * es, 0, 0);	/* flexible array member: its initialized length counts */
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
						efatal(e, x->loc, "initializer element is not a compile-time constant");
					/* The per-bit loop truncates to the field width. Keep the
					   full constant so shifts through bit 127 remain defined. */
					bits = (u128)cv.i;
					for (uint64_t k = 0; k < w; k++) {
						uint64_t at = off + (bo + k) / 8;
						uint8_t m = (uint8_t)(1u << ((bo + k) % 8));

						data_or(e, at, 0);
						e->data.p[at] = (char)((e->data.p[at] & ~m) | ((cv.kind == CC_VALUE_BITINT ? (k/32 < cv.nbig && ((c->lists[cv.big+k/32] >> (k%32)) & 1)) : k < 128 ? ((bits >> k) & 1) : cv.i < 0) ? m : 0));
					}
					continue;
				}
				data_init(e, off + f->off / 8, f->ty, v[i]);
			}
		}
		return;
	}
	if (x->op == AST_NODE_COMMA) {
		data_init(e, off, ty, x->b);
		return;
	}
	{
		uint32_t selected;

		if (cc_initializer_subobject_find(c, init, &selected)) {
			data_init(e, off, ty, selected);
			return;
		}
	}
	if (x->op == AST_NODE_STRING && is_array(c, ty)) {
		const cstr *s = &c->strs[x->a];
		uint64_t n = (uint64_t)s->n * s->esz, cap = tsize(c, ty);

		if (T(c, ty)->flags & AST_TYPE_FLAG_INCOMPLETE)
			cap = n;
		if (n > cap)
			n = cap;
		for (uint64_t i = 0; i < n; i++)
			data_put(e, off + i, (uint8_t)s->s[i], 1);
		return;
	}
	if (x->op == AST_NODE_COMPOUND_LITERAL) {
		data_init(e, off, ty, x->a);
		return;
	}
	if (x->op == AST_NODE_COMPLEX) {
		ast_qualified_type et = c->tags[T(c, x->ty)->aux].f[0].ty;

		data_init(e, off, et, x->a);
		data_init(e, off + tsize(c, et), et, x->b);
		return;
	}
	if (x->op == AST_NODE_CAST && x->aux == AST_CAST_NOP && is_agg(c, ty) && !em_bigint(c, ty)) {
		data_init(e, off, ty, x->a);
		return;
	}
	if (is_agg(c, ty) && !em_bigint(c, ty)) {
		/* a const aggregate object as the initializer (clang extension) */
		uint32_t m = strip_casts(c, init);

		if (N(m)->op == AST_NODE_VARIABLE && c->syms[N(m)->a].body) {
			data_init(e, off, ty, c->syms[N(m)->a].body);
			return;
		}
		efatal(e, x->loc, "initializer element is not a compile-time constant");
	}
	if (!cc_eval(c, init, &cv))
		efatal(e, x->loc, "initializer element is not a compile-time constant");
	{
		int n = (int)tsize(c, ty);

		switch (cv.kind) {
		case CC_VALUE_BITINT:
			for (unsigned i = 0; i < cv.nbig; i++)
				data_put(e, off + 4 * i, c->lists[cv.big + i], 4);
			return;
		case CC_VALUE_INTEGER:
			if (em_bigint(c, ty)) {
				for (int i = 0; i < n; i += 8)
					data_put(e, off + i, i < 16 ? (uint64_t)((u128)cv.i >> (8*i)) : cv.i < 0 ? UINT64_MAX : 0, 8);
				return;
			}
			if (n == 16) {
				data_put(e, off, (uint64_t)(u128)cv.i, 8);
				data_put(e, off + 8, (uint64_t)((u128)cv.i >> 64), 8);
			} else {
				data_put(e, off, (uint64_t)cv.i, n);
			}
			return;
		case CC_VALUE_FLOAT:
			if (n <= 8 && cv.ld && vsoftfloat_is_nan(cv.q)) {
				data_put(e, off, vsoftfloat_nan_storage(cv.q, KIND(c, ty) == AST_TYPE_BFLOAT16 ? 7 : n == 2 ? 10 : n == 4 ? 23 : 52, KIND(c, ty) == AST_TYPE_BFLOAT16 ? 8 : n == 2 ? 5 : n == 4 ? 8 : 11), n);
				return;
			}
			if (n == 2) {
				data_put(e, off, (KIND(c, ty) == AST_TYPE_BFLOAT16 ? vsoftfloat_to_bfloat16(cv.ld ? cv.q : vsoftfloat_of_double(cv.f)) : vsoftfloat_to_half(cv.ld ? cv.q : vsoftfloat_of_double(cv.f))), 2);
			} else if (n == 4) {
				float f = (float)cv.f;
				uint32_t u;

				memcpy(&u, &f, 4);
				data_put(e, off, u, 4);
			} else {
				uint64_t u;

				if (n == 16 && X86(c) && KIND(c, ty) == AST_TYPE_LONG_DOUBLE) {	/* x87: 10 bytes, 6 of padding */
					u128 q = cv.ld ? cv.q : (u128)ld_of_d(cv.f);
					uint8_t xb[10];
					uint64_t lo;

					vsoftfloat_to_x87(q, xb);
					memcpy(&lo, xb, 8);
					data_put(e, off, lo, 8);
					data_put(e, off + 8, (uint64_t)xb[8] | (uint64_t)xb[9] << 8, 8);
					return;
				}
				if (n == 16) {
					u128 q = cv.ld ? cv.q : (u128)ld_of_d(cv.f);

					data_put(e, off, (uint64_t)q, 8);
					data_put(e, off + 8, (uint64_t)(q >> 64), 8);
					return;
				}
				memcpy(&u, &cv.f, 8);
				data_put(e, off, u, 8);
			}
			return;
		case CC_VALUE_ADDRESS:
			if (WASM(c) && cv.label) {
				/* wasm: a label's address is its number (PLAN-wasm.md W4) */
				data_put(e, off, (uint64_t)cv.label - (cv.sublabel ? (uint64_t)cv.sublabel : 0) + (uint64_t)cv.i, n);
				return;
			}
			if (cv.sublabel && (n == 4 || n == 8)) {
				/* label difference: ref - sub, resolved by the assembler */
				data_put(e, off, 0, n);
				e->rel[e->nrel] = (struct reloc){ off, REF(RK_LBL, cv.label - 1), e->nrel, (int64_t)cv.i,
								  REF(RK_LBL, cv.sublabel - 1) + 1, (uint32_t)n };
				e->nrel++;
				if (off + n > e->relend)
					e->relend = off + n;
				note_ref(e, REF(RK_LBL, cv.label - 1));
				note_ref(e, REF(RK_LBL, cv.sublabel - 1));
				return;
			}
			if (n != 8 && !(n == 4 && WASM32(c)))
				efatal(e, x->loc, "address initializer for a %d-byte object", n);
			if (cv.label)
				data_reloc(e, off, REF(RK_LBL, cv.label - 1), (int64_t)cv.i, n);
			else if (cv.str)
				data_reloc(e, off, REF(RK_STR, cv.str - 1), (int64_t)cv.i, n);
			else {
				data_reloc(e, off, REF(RK_SYM, canon_sym(c, cv.sym)), (int64_t)cv.i, n);
				if (WASM(c))
					wasm_data_ref(e, REF(RK_SYM, canon_sym(c, cv.sym)));
			}
			return;
		default:
			efatal(e, x->loc, "initializer element is not a compile-time constant");
		}
	}
}

static int rel_cmp(const void *a, const void *b)
{
	const struct reloc *x = a, *y = b;

	if (x->off != y->off)
		return x->off < y->off ? -1 : 1;
	return x->seq < y->seq ? -1 : x->seq > y->seq;
}

/* sort e->rel by offset; a later initializer for the same element overrides: keep the last */
static void rel_prep(ecx *e)
{
	uint32_t k = 0;

	qsort(e->rel, e->nrel, sizeof(e->rel[0]), rel_cmp);
	for (uint32_t j = 0; j < e->nrel; j++) {
		if (e->rel[j].seq == UINT32_MAX)
			continue;
		if (k && e->rel[k - 1].off == e->rel[j].off)
			k--;
		e->rel[k++] = e->rel[j];
	}
	e->nrel = k;
}

/* the object backend's data item: what data_bytes / .zero would assemble to */
static void data_item(ecx *e, uint64_t size, bool zero)
{
	size_t h = it_open(e, IT_DATA);
	data_hdr *dh = it_room(e, sizeof *dh);
	uint32_t nrel = 0;

	if (zero) {
		*dh = (data_hdr){ .size = size, .zero = 1 };
	} else {
		rel_prep(e);
		while (nrel < e->nrel && e->rel[nrel].off < size)
			nrel++;
		*dh = (data_hdr){ .size = size, .n = e->data.n < size ? e->data.n : size, .nrel = nrel };
		memcpy(it_room(e, nrel * sizeof(ireloc)), e->rel, nrel * sizeof(ireloc));
		memcpy(it_room(e, dh->n), e->data.p, dh->n);
	}
	it_close(e, h);
}

/* the bytes in e->data with e->rel, as directives */
static void data_bytes(ecx *e, tbuf *b, uint64_t size)
{
	uint64_t o = 0;
	uint32_t ri = 0;
	const unsigned char *d = (const unsigned char *)e->data.p;

	rel_prep(e);
	while (o < size) {
		uint64_t end = ri < e->nrel ? e->rel[ri].off : size;

		if (ri < e->nrel && o == e->rel[ri].off) {
			uint32_t sz = e->rel[ri].sz ? e->rel[ri].sz : 8;

			vtext_string_append(b, sz == 4 ? (WASM(e->c) ? "\t.int32\t" : X86(e->c) ? "\t.long\t" : "\t.word\t") : WASM(e->c) ? "\t.int64\t" : X86(e->c) ? "\t.quad\t" : "\t.xword\t");
			ref_off(e->c, b, e->rel[ri].ref, e->rel[ri].add);
			if (e->rel[ri].sub) {
				vtext_char_append(b, '-');
				ref_name(e->c, b, e->rel[ri].sub - 1);
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
				vtext_string_append(b, WASM(e->c) ? "\t.int64\t" : X86(e->c) ? "\t.quad\t" : "\t.xword\t");
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

static bool all_const(cc *c, ast_qualified_type t)
{
	if (AST_QUALIFIED_TYPE_QUALIFIERS(t) & AST_QUALIFIER_CONST)
		return true;
	if (is_array(c, t))
		return all_const(c, T(c, t)->base);
	return false;
}

VPRIV void emit_object(ecx *e, uint32_t s)
{
	cc *c = e->c;
	const sym *y = &c->syms[s];
	tbuf *b = &e->out;
	size_t start = b->n;
	uint64_t size = tsize(c, y->ty);
	uint32_t align = sym_align(c, s);
	const attrs *a = sym_attrs(c, s);
	bool zero;

	if (_setjmp(e->bail))
		return;
	e->data.n = 0;
	e->nrel = 0, e->relend = 0;
	e->reach = true;
	if (y->body)
		data_init(e, 0, y->ty, y->body);
	if (e->data.n > size)
		size = e->data.n;	/* flexible array member initialised */
	if (size == 0 && !y->body && is_array(c, y->ty) && (T(c, y->ty)->flags & AST_TYPE_FLAG_INCOMPLETE))
		size = tsize(c, T(c, y->ty)->base);	/* int x[]; is one element */
	zero = e->nrel == 0;
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
			bool q = sym_quoted(c, s);
			size_t at;

			if (q)
				vtext_char_append(b, '"');
			vtext_string_append(b, all_const(c, y->ty) ? ".rodata." : zero ? ".bss." : ".data.");
			at = b->n;
			sym_name(c, b, s);
			if (q) {
				memmove(b->p + at, b->p + at + 1, b->n - at - 1);
				b->n--;
			}
		}
		vtext_string_append(b, ",\"\",@\n");
		if (global) {
			vtext_string_append(b, (y->flags & AST_SYMBOL_FLAG_WEAK) ? "\t.weak\t" : "\t.globl\t");
			sym_name(c, b, s);
			vtext_char_append(b, '\n');
		}
		vtext_string_append(b, "\t.p2align\t");
		vtext_unsigned_append(b, (uint64_t)__builtin_ctz(align ? align : 1));
		vtext_string_append(b, "\n\t.type\t");
		sym_name(c, b, s);
		vtext_string_append(b, ",@object\n");
		sym_name(c, b, s);
		vtext_string_append(b, ":\n");
		if (zero && !e->nrel) {
			vtext_string_append(b, "\t.zero\t");
			vtext_unsigned_append(b, size ? size : 1);
			vtext_char_append(b, '\n');
		} else {
			data_bytes(e, b, size);
		}
		vtext_string_append(b, "\t.size\t");
		sym_name(c, b, s);
		vtext_string_append(b, ", ");
		vtext_unsigned_append(b, size ? size : 1);
		vtext_char_append(b, '\n');
		e->E->nobj++;
		out_chunk(e, ((uint64_t)s << 1) | 1, start);
		return;
	}
	if (!a->section && (y->flags & AST_SYMBOL_FLAG_THREAD) && !DARWIN(c)) {
		/* ELF: .tdata/.tbss; code reaches it through I_TLV (lval.c) */
		const char *sec = zero && !e->nrel ? "\t.section\t.tbss,\"awT\",@nobits\n" : "\t.section\t.tdata,\"awT\",@progbits\n";

		if (direct(e)) {
			size_t h;

			b->n = (b->n + 7) & ~(size_t)7;
			start = b->n;
			sym_header(e, b, s, false, sec, align);
			data_item(e, zero && !e->nrel ? (size ? size : 1) : size, zero && !e->nrel);
			h = it_open(e, IT_OSIZE);
			*(osize *)it_room(e, sizeof(osize)) = (osize){ .sym = s, .size = size };
			it_close(e, h);
			e->E->nobj++;
			out_chunk(e, ((uint64_t)s << 1) | 1, start);
			return;
		}
		sym_header(e, b, s, false, sec, align);
		if (zero && !e->nrel) {
			vtext_string_append(b, "\t.zero\t");
			vtext_unsigned_append(b, size ? size : 1);
			vtext_char_append(b, '\n');
		} else {
			data_bytes(e, b, size);
		}
		vtext_string_append(b, "\t.size\t");
		sym_name(c, b, s);
		vtext_string_append(b, ", ");
		vtext_unsigned_append(b, size);
		vtext_char_append(b, '\n');
		e->E->nobj++;
		out_chunk(e, ((uint64_t)s << 1) | 1, start);
		return;
	}
	if (!a->section && (y->flags & AST_SYMBOL_FLAG_THREAD) && !direct(e) && !DARWIN(e->c))
		efatal(e, y->loc, "thread-local storage is not supported");
	if (!direct(e) && DARWIN(e->c) && (y->flags & AST_SYMBOL_FLAG_THREAD)) {
		/* the same TLV layout as text; darwin_text names the sections */
		bool global = y->sc != AST_STORAGE_STATIC && y->depth == 0 && y->name;

		vtext_string_append(b, zero ? "\t.section\t.tbss,\"aw\",@nobits\n" : "\t.section\t.tdata,\"aw\",@progbits\n");
		vtext_string_append(b, "\t.p2align\t");
		vtext_unsigned_append(b, (uint64_t)__builtin_ctz(align ? align : 1));
		vtext_char_append(b, '\n');
		sym_name(c, b, s);
		vtext_string_append(b, "$tlv$init:\n");
		if (zero && !e->nrel) {
			vtext_string_append(b, "\t.zero\t");
			vtext_unsigned_append(b, size ? size : 1);
			vtext_char_append(b, '\n');
		} else {
			data_bytes(e, b, size);
		}
		vtext_string_append(b, "\t.section\t.tlv,\"aw\",@progbits\n");
		if (global) {
			vtext_string_append(b, (y->flags & AST_SYMBOL_FLAG_WEAK) ? "\t.weak\t" : "\t.globl\t");
			sym_name(c, b, s);
			vtext_char_append(b, '\n');
		}
		vtext_string_append(b, "\t.p2align\t3\n");
		sym_name(c, b, s);
		vtext_string_append(b, ":\n\t.xword\t__tlv_bootstrap\n\t.xword\t0\n\t.xword\t");
		sym_name(c, b, s);
		vtext_string_append(b, "$tlv$init\n");
		e->E->nobj++;
		out_chunk(e, ((uint64_t)s << 1) | 1, start);
		return;
	}
	if (direct(e) && (y->flags & AST_SYMBOL_FLAG_THREAD)) {
		/*
		 * Darwin TLV: the initial value in __thread_data/__thread_bss under
		 * a local label, the symbol itself on a descriptor in
		 * __thread_vars: { __tlv_bootstrap, 0, initial value }.
		 */
		size_t h;
		bool global = y->sc != AST_STORAGE_STATIC && y->depth == 0 && y->name;

		b->n = (b->n + 7) & ~(size_t)7;
		start = b->n;
		h = it_open(e, IT_TEXT);
		vtext_string_append(b, zero ? "\t.section\t.tbss,\"aw\",@nobits\n" : "\t.section\t.tdata,\"aw\",@progbits\n");
		vtext_string_append(b, "\t.p2align\t");
		vtext_unsigned_append(b, (uint64_t)__builtin_ctz(align ? align : 1));
		vtext_char_append(b, '\n');
		sym_name(c, b, s);
		vtext_string_append(b, "$tlv$init:\n");
		it_close(e, h);
		data_item(e, zero && !e->nrel ? (size ? size : 1) : size, zero && !e->nrel);
		h = it_open(e, IT_TEXT);
		vtext_string_append(b, "\t.section\t.tlv,\"aw\",@progbits\n");
		if (global) {
			vtext_string_append(b, (y->flags & AST_SYMBOL_FLAG_WEAK) ? "\t.weak\t" : "\t.globl\t");
			sym_name(c, b, s);
			vtext_char_append(b, '\n');
		}
		vtext_string_append(b, "\t.p2align\t3\n");
		sym_name(c, b, s);
		vtext_string_append(b, ":\n\t.xword\t__tlv_bootstrap\n\t.xword\t0\n\t.xword\t");
		sym_name(c, b, s);
		vtext_string_append(b, "$tlv$init\n");
		it_close(e, h);
		e->E->nobj++;
		out_chunk(e, ((uint64_t)s << 1) | 1, start);
		return;
	}
	if (direct(e)) {
		size_t h;

		b->n = (b->n + 7) & ~(size_t)7;
		start = b->n;
		sym_header(e, b, s, false,
			   all_const(c, y->ty) ? "\t.section\t.rodata,\"a\",@progbits\n" : zero ? "\t.bss\n" : "\t.data\n", align);
		data_item(e, zero && !e->nrel ? (size ? size : 1) : size, zero && !e->nrel);
		h = it_open(e, IT_OSIZE);
		*(osize *)it_room(e, sizeof(osize)) = (osize){ .sym = s, .size = size };
		it_close(e, h);
		e->E->nobj++;
		out_chunk(e, ((uint64_t)s << 1) | 1, start);
		return;
	}
	sym_header(e, b, s, false,
		   all_const(c, y->ty) ? "\t.section\t.rodata,\"a\",@progbits\n" : zero ? "\t.bss\n" : "\t.data\n", align);
	if (zero && !e->nrel) {
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
		data_bytes(e, b, size);
	}
	vtext_string_append(b, "\t.size\t");
	sym_name(c, b, s);
	vtext_string_append(b, ", ");
	vtext_unsigned_append(b, size);
	vtext_char_append(b, '\n');
	e->E->nobj++;
	out_chunk(e, ((uint64_t)s << 1) | 1, start);
}

VPRIV void emit_string(ecx *e, uint32_t i)
{
	cc *c = e->c;
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
	if (direct(e)) {
		size_t h;
		uint32_t ref = REF(RK_STR, i);
		bool asciz = s->esz == 1 && !nul;
		uint64_t m = asciz ? (n ? n - 1 : 0) : n;
		char *q;

		b->n = (b->n + 7) & ~(size_t)7;
		start = b->n;
		h = it_open(e, IT_TEXT);
		if (nul)
			vtext_string_append(b, "\t.section\t.rodata,\"a\",@progbits\n");
		else
			vtext_format_append(b, "\t.section\t.rodata.str%u.%u,\"aMS\",@progbits,%u\n", s->esz, s->esz, s->esz);
		if (s->esz > 1)
			vtext_format_append(b, "\t.p2align\t%d\n", s->esz == 2 ? 1 : 2);
		it_close(e, h);
		h = it_open(e, IT_LABEL);
		memcpy(it_room(e, 4), &ref, 4);
		it_close(e, h);
		h = it_open(e, IT_BYTES);
		q = it_room(e, m + asciz);
		memcpy(q, s->s, m);
		if (asciz)
			q[m] = 0;
		it_close(e, h);
		out_chunk(e, ((uint64_t)1 << 62) | i, start);
		return;
	}
	if (WASM(c))
		vtext_format_append(b, "\t.section\t.rodata..L.str.%u,\"\",@\n", i);
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
			vtext_string_append(b, s->esz == 1 ? "\t.byte\t" : s->esz == 2 ? "\t.short\t" : WASM(c) || X86(c) ? "\t.long\t" : "\t.word\t");
			vtext_unsigned_append(b, v);
			vtext_char_append(b, '\n');
		}
	}
	if (WASM(c))
		vtext_format_append(b, "\t.size\t.L.str.%u, %llu\n", i, (unsigned long long)n);
	out_chunk(e, ((uint64_t)1 << 62) | i, start);
}
