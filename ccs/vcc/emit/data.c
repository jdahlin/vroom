/* vcc code generator: data: initialised objects and strings */
#include "emitimpl.h"

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

static void data_init(ecx *e, uint64_t off, qty ty, uint32_t init)
{
	cc *c = e->c;
	const node *x;
	cval cv;

	if (!init)
		return;
	x = N(init);
	if (x->op == N_ZERO)
		return;
	if (x->op == N_INIT) {
		const uint32_t *v = &c->lists[x->b];

		if (x->aux == 1) {
			qty et = T(c, ty)->base;
			uint64_t es = tsize(c, et);

			uint64_t n = 0;

			for (uint32_t i = 0; i < x->c; i++) {
				for (uint64_t k = v[3 * i]; k <= v[3 * i + 1]; k++)
					data_init(e, off + k * es, et, v[3 * i + 2]);
				if (v[3 * i + 1] + 1 > n)
					n = v[3 * i + 1] + 1;
			}
			if (T(c, ty)->flags & TF_INCOMPLETE)
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
					uint64_t bo = f->off, w = f->width, bits;

					if (!cc_eval(c, v[i], &cv) || cv.kind != CV_INT)
						efatal(e, x->loc, "initializer element is not a compile-time constant");
					bits = (uint64_t)cv.i & (w >= 64 ? ~(uint64_t)0 : (((uint64_t)1 << w) - 1));
					for (uint64_t k = 0; k < w; k++) {
						uint64_t at = off + (bo + k) / 8;
						uint8_t m = (uint8_t)(1u << ((bo + k) % 8));

						data_or(e, at, 0);
						e->data.p[at] = (char)((e->data.p[at] & ~m) | (((bits >> k) & 1) ? m : 0));
					}
					continue;
				}
				data_init(e, off + f->off / 8, f->ty, v[i]);
			}
		}
		return;
	}
	if (x->op == N_STR && is_array(c, ty)) {
		const cstr *s = &c->strs[x->a];
		uint64_t n = (uint64_t)s->n * s->esz, cap = tsize(c, ty);

		if (T(c, ty)->flags & TF_INCOMPLETE)
			cap = n;
		if (n > cap)
			n = cap;
		for (uint64_t i = 0; i < n; i++)
			data_put(e, off + i, (uint8_t)s->s[i], 1);
		return;
	}
	if (x->op == N_COMPLIT) {
		data_init(e, off, ty, x->a);
		return;
	}
	if (x->op == N_CPLX) {
		qty et = c->tags[T(c, x->ty)->aux].f[0].ty;

		data_init(e, off, et, x->a);
		data_init(e, off + tsize(c, et), et, x->b);
		return;
	}
	if (x->op == N_CAST && x->aux == CK_NOP && is_agg(c, ty)) {
		data_init(e, off, ty, x->a);
		return;
	}
	if (is_agg(c, ty)) {
		/* a const aggregate object as the initializer (clang extension) */
		uint32_t m = strip_casts(c, init);

		if (N(m)->op == N_VAR && c->syms[N(m)->a].body) {
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
		case CV_INT:
			if (n == 16) {
				data_put(e, off, (uint64_t)(u128)cv.i, 8);
				data_put(e, off + 8, (uint64_t)((u128)cv.i >> 64), 8);
			} else {
				data_put(e, off, (uint64_t)cv.i, n);
			}
			return;
		case CV_FLT:
			if (n == 4) {
				float f = (float)cv.f;
				uint32_t u;

				memcpy(&u, &f, 4);
				data_put(e, off, u, 4);
			} else {
				uint64_t u;

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
		case CV_ADDR:
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

			tb_s(b, sz == 4 ? (WASM(e->c) ? "\t.int32\t" : "\t.word\t") : WASM(e->c) ? "\t.int64\t" : "\t.xword\t");
			ref_off(e->c, b, e->rel[ri].ref, e->rel[ri].add);
			if (e->rel[ri].sub) {
				tb_c(b, '-');
				ref_name(e->c, b, e->rel[ri].sub - 1);
			}
			tb_c(b, '\n');
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
					tb_s(b, "\t.zero\t");
					tb_u(b, z - o);
					tb_c(b, '\n');
				}
				o = z;
				continue;
			}
			if (!(o & 7) && o + 8 <= end) {
				uint64_t w = 0;

				/* o < data.n here; past it the bytes are zero, not what the buffer holds */
				memcpy(&w, d + o, o + 8 <= e->data.n ? 8 : e->data.n - o);
				tb_s(b, WASM(e->c) ? "\t.int64\t" : "\t.xword\t");
				tb_hex(b, w);
				tb_c(b, '\n');
				o += 8;
				continue;
			}
			tb_s(b, "\t.byte\t");
			tb_u(b, o < e->data.n ? d[o] : 0);
			tb_c(b, '\n');
			o++;
		}
	}
}

static bool all_const(cc *c, qty t)
{
	if (QQ(t) & Q_CONST)
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
	if (size == 0 && !y->body && is_array(c, y->ty) && (T(c, y->ty)->flags & TF_INCOMPLETE))
		size = tsize(c, T(c, y->ty)->base);	/* int x[]; is one element */
	zero = e->nrel == 0;
	for (uint64_t i = 0; zero && i < e->data.n; i++)
		zero = !e->data.p[i];
	if (WASM(c)) {
		/* one section per object, as clang writes them */
		bool global = y->sc != SC_STATIC && y->depth == 0 && y->name;

		/* _Thread_local: an ordinary object, as LLVM strips TLS from a
		 * module without shared memory (no -pthread) */
		tb_s(b, "\t.section\t");
		if (a->section) {
			tb_s(b, c->strs[a->section - 1].s);
		} else {
			/* a quoted name: the whole section name in quotes, as clang */
			bool q = sym_quoted(c, s);
			size_t at;

			if (q)
				tb_c(b, '"');
			tb_s(b, all_const(c, y->ty) ? ".rodata." : zero ? ".bss." : ".data.");
			at = b->n;
			sym_name(c, b, s);
			if (q) {
				memmove(b->p + at, b->p + at + 1, b->n - at - 1);
				b->n--;
			}
		}
		tb_s(b, ",\"\",@\n");
		if (global) {
			tb_s(b, (y->flags & SF_WEAK) ? "\t.weak\t" : "\t.globl\t");
			sym_name(c, b, s);
			tb_c(b, '\n');
		}
		tb_s(b, "\t.p2align\t");
		tb_u(b, (uint64_t)__builtin_ctz(align ? align : 1));
		tb_s(b, "\n\t.type\t");
		sym_name(c, b, s);
		tb_s(b, ",@object\n");
		sym_name(c, b, s);
		tb_s(b, ":\n");
		if (zero && !e->nrel) {
			tb_s(b, "\t.zero\t");
			tb_u(b, size ? size : 1);
			tb_c(b, '\n');
		} else {
			data_bytes(e, b, size);
		}
		tb_s(b, "\t.size\t");
		sym_name(c, b, s);
		tb_s(b, ", ");
		tb_u(b, size ? size : 1);
		tb_c(b, '\n');
		e->E->nobj++;
		out_chunk(e, ((uint64_t)s << 1) | 1, start);
		return;
	}
	if (!a->section && (y->flags & SF_THREAD) && !DARWIN(c)) {
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
			tb_s(b, "\t.zero\t");
			tb_u(b, size ? size : 1);
			tb_c(b, '\n');
		} else {
			data_bytes(e, b, size);
		}
		tb_s(b, "\t.size\t");
		sym_name(c, b, s);
		tb_s(b, ", ");
		tb_u(b, size);
		tb_c(b, '\n');
		e->E->nobj++;
		out_chunk(e, ((uint64_t)s << 1) | 1, start);
		return;
	}
	if (!a->section && (y->flags & SF_THREAD) && !direct(e))
		efatal(e, y->loc, "thread-local storage is not supported");
	if (direct(e) && (y->flags & SF_THREAD)) {
		/*
		 * Darwin TLV: the initial value in __thread_data/__thread_bss under
		 * a local label, the symbol itself on a descriptor in
		 * __thread_vars: { __tlv_bootstrap, 0, initial value }.
		 */
		size_t h;
		bool global = y->sc != SC_STATIC && y->depth == 0 && y->name;

		b->n = (b->n + 7) & ~(size_t)7;
		start = b->n;
		h = it_open(e, IT_TEXT);
		tb_s(b, zero ? "\t.section\t.tbss,\"aw\",@nobits\n" : "\t.section\t.tdata,\"aw\",@progbits\n");
		tb_s(b, "\t.p2align\t");
		tb_u(b, (uint64_t)__builtin_ctz(align ? align : 1));
		tb_s(b, "\n.Ltlvi.");
		tb_u(b, s);
		tb_s(b, ":\n");
		it_close(e, h);
		data_item(e, zero && !e->nrel ? (size ? size : 1) : size, zero && !e->nrel);
		h = it_open(e, IT_TEXT);
		tb_s(b, "\t.section\t.tlv,\"aw\",@progbits\n");
		if (global) {
			tb_s(b, (y->flags & SF_WEAK) ? "\t.weak\t" : "\t.globl\t");
			sym_name(c, b, s);
			tb_c(b, '\n');
		}
		tb_s(b, "\t.p2align\t3\n");
		sym_name(c, b, s);
		tb_s(b, ":\n\t.xword\t__tlv_bootstrap\n\t.xword\t0\n\t.xword\t.Ltlvi.");
		tb_u(b, s);
		tb_c(b, '\n');
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
			tb_s(b, "\t.zero\t");
			tb_u(b, size ? size : 1);
			tb_c(b, '\n');
		} else {
			tb_s(b, "\t.zero\t");
			tb_u(b, size ? size : 1);
			tb_c(b, '\n');
		}
	} else {
		data_bytes(e, b, size);
	}
	tb_s(b, "\t.size\t");
	sym_name(c, b, s);
	tb_s(b, ", ");
	tb_u(b, size);
	tb_c(b, '\n');
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

	for (uint64_t k = 0; k + s->esz < n; k++)
		if (!s->s[k] && s->esz == 1)
			nul = true;
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
			tb_s(b, "\t.section\t.rodata,\"a\",@progbits\n");
		else
			tb_f(b, "\t.section\t.rodata.str%u.%u,\"aMS\",@progbits,%u\n", s->esz, s->esz, s->esz);
		if (s->esz > 1)
			tb_f(b, "\t.p2align\t%d\n", s->esz == 2 ? 1 : 2);
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
		tb_f(b, "\t.section\t.rodata..L.str.%u,\"\",@\n", i);
	else if (nul)
		tb_s(b, "\t.section\t.rodata,\"a\",@progbits\n");
	else
		tb_f(b, "\t.section\t.rodata.str%u.%u,\"aMS\",@progbits,%u\n", s->esz, s->esz, s->esz);
	if (s->esz > 1)
		tb_f(b, "\t.p2align\t%d\n", s->esz == 2 ? 1 : 2);
	tb_s(b, ".L.str.");
	tb_u(b, i);
	tb_s(b, ":\n");
	if (s->esz == 1 && !nul) {
		tb_s(b, "\t.asciz\t\"");
		for (uint64_t k = 0; k + 1 < n; k++) {
			unsigned char ch = (unsigned char)s->s[k];

			if (ch == '"' || ch == '\\') {
				tb_c(b, '\\');
				tb_c(b, (char)ch);
			} else if (ch >= 32 && ch < 127) {
				tb_c(b, (char)ch);
			} else {
				tb_c(b, '\\');
				tb_c(b, (char)('0' + (ch >> 6)));
				tb_c(b, (char)('0' + ((ch >> 3) & 7)));
				tb_c(b, (char)('0' + (ch & 7)));
			}
		}
		tb_s(b, "\"\n");
	} else {
		for (uint64_t k = 0; k < n; k += s->esz) {
			uint32_t v = 0;

			memcpy(&v, s->s + k, s->esz);
			tb_s(b, s->esz == 1 ? "\t.byte\t" : s->esz == 2 ? "\t.short\t" : WASM(c) ? "\t.long\t" : "\t.word\t");
			tb_u(b, v);
			tb_c(b, '\n');
		}
	}
	if (WASM(c))
		tb_f(b, "\t.size\t.L.str.%u, %llu\n", i, (unsigned long long)n);
	out_chunk(e, ((uint64_t)1 << 62) | i, start);
}
