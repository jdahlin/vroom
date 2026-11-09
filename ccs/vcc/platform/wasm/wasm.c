/*
 * The wasm object writer: the model of arch/wasm/as/was.h as a relocatable
 * WebAssembly object (tool-conventions Linking.md), in the layout of LLVM's
 * WasmObjectWriter: symbols, functions, imports and types in the order the
 * assembler registered the symbols, section sizes and relocatable fields as
 * padded LEBs, one data segment per section, the linking section and then
 * reloc.CODE and reloc.DATA.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../arch/wasm/as/was.h"

enum { SEC_CUSTOM = 0, SEC_TYPE = 1, SEC_IMPORT = 2, SEC_FUNCTION = 3, SEC_ELEM = 9, SEC_CODE = 10, SEC_DATA = 11,
       SEC_DATACOUNT = 12 };

static void put8(vbuf *b, uint8_t v) { vbuf_put(b, &v, 1); }

static void uleb(vbuf *b, uint64_t v)
{
	do {
		uint8_t c = v & 0x7f;

		v >>= 7;
		put8(b, c | (v ? 0x80 : 0));
	} while (v);
}

static void sleb(vbuf *b, int64_t v)
{
	for (;;) {
		uint8_t c = v & 0x7f;

		v >>= 7;
		if ((v == 0 && !(c & 0x40)) || (v == -1 && (c & 0x40))) {
			put8(b, c);
			return;
		}
		put8(b, c | 0x80);
	}
}

static void str(vbuf *b, const char *s)
{
	size_t n = strlen(s);

	uleb(b, n);
	vbuf_put(b, s, n);
}

/* a padded LEB in place: n bytes */
static void pad_uleb(uint8_t *p, uint64_t v, int n)
{
	for (int i = 0; i < n; i++) {
		p[i] = (v & 0x7f) | (i + 1 < n ? 0x80 : 0);
		v >>= 7;
	}
}

static void pad_sleb(uint8_t *p, int64_t v, int n)
{
	for (int i = 0; i < n; i++) {
		p[i] = (v & 0x7f) | (i + 1 < n ? 0x80 : 0);
		v >>= 7;
	}
}

/* a section: id and a 5-byte size patched by sec_end */
static size_t sec_begin(vbuf *b, uint8_t id, const char *name)
{
	static const uint8_t z[5];
	size_t at;

	put8(b, id);
	at = b->n;
	vbuf_put(b, z, 5);
	if (name)
		str(b, name);
	return at;
}

static void sec_end(vbuf *b, size_t at)
{
	pad_uleb((uint8_t *)b->p + at, b->n - at - 5, 5);
}

/* a linking subsection's size: a padded LEB, as LLVM writes it */
static void sub_size(vbuf *b, size_t n)
{
	uint8_t p[5];

	pad_uleb(p, n, 5);
	vbuf_put(b, p, 5);
}

/* follow aliases */
static wsym *base(wmod *m, wsym *y)
{
	for (int i = 0; y->alias >= 0 && i < 64; i++)
		y = &m->sym[y->alias];
	return y;
}

static bool defined(wmod *m, wsym *y) { wsym *b = base(m, y); return b->defined || (b->kind == WS_FUNCTION && b->seg >= 0); }

static bool rel_addend(uint8_t t)
{
	return t == R_MEMORY_ADDR_LEB || t == R_MEMORY_ADDR_SLEB || t == R_MEMORY_ADDR_I32 || t == R_MEMORY_ADDR_LEB64 ||
	       t == R_MEMORY_ADDR_SLEB64 || t == R_MEMORY_ADDR_I64;
}

typedef struct lay {
	wmod *m;
	uint32_t *tmap;		/* parse signature -> type index + 1 */
	uint32_t *types;	/* type index -> parse signature */
	uint32_t ntypes;
	uint32_t nfimp, ngimp, ntimp, ntagimp;
	uint32_t *funcs;	/* function index - nfimp -> wfunc */
	uint32_t nfuncs;
	uint32_t *elems;	/* table slot - 1 -> function index */
	uint32_t nelems, celems;
	uint32_t *imps;		/* imported symbols, in order */
	uint32_t nimps;
	uint32_t nsymtab;
} lay;

static uint32_t type_of(lay *L, int32_t sig)
{
	if (!L->tmap[sig]) {
		L->types[L->ntypes] = (uint32_t)sig;
		L->tmap[sig] = ++L->ntypes;
	}
	return L->tmap[sig] - 1;
}

/* the placeholder of a relocation and its final type */
static int resolve(lay *L, wrel *r, uint64_t *val)
{
	wmod *m = L->m;
	wsym *y, *b;

	if (r->type == R_TYPE_INDEX_LEB) {
		*val = type_of(L, (int32_t)r->sym);
		return 0;
	}
	y = &m->sym[r->sym];
	b = base(m, y);
	switch (r->type) {
	case R_ADDR_SLEB64: case R_ADDR_SLEB: case R_ADDR_I64: case R_ADDR_I32:
		if (b->kind == WS_FUNCTION) {
			r->type = r->type == R_ADDR_SLEB64 ? R_TABLE_INDEX_SLEB64 : r->type == R_ADDR_SLEB ? R_TABLE_INDEX_SLEB
				: r->type == R_ADDR_I64 ? R_TABLE_INDEX_I64 : R_TABLE_INDEX_I32;
			*val = b->elem;
			return 0;
		}
		if (b->kind == WS_GLOBAL || b->kind == WS_TABLE)
			return was_error(m, "the address of '%s', which is not data or a function", y->name);
		r->type = r->type == R_ADDR_SLEB64 ? R_MEMORY_ADDR_SLEB64 : r->type == R_ADDR_SLEB ? R_MEMORY_ADDR_SLEB
			: r->type == R_ADDR_I64 ? R_MEMORY_ADDR_I64 : R_MEMORY_ADDR_I32;
		*val = b->defined && b->seg >= 0 ? m->seg[b->seg].vaddr + b->off + (uint64_t)r->addend : 0;
		return 0;
	case R_MEMORY_ADDR_LEB: case R_MEMORY_ADDR_LEB64:
		if (b->kind != WS_DATA && b->kind != WS_NONE)
			return was_error(m, "'%s' as a memory offset, which is not data", y->name);
		*val = b->defined && b->seg >= 0 ? m->seg[b->seg].vaddr + b->off + (uint64_t)r->addend : 0;
		return 0;
	case R_FUNCTION_INDEX_LEB:
		if (b->kind != WS_FUNCTION)
			return was_error(m, "call to '%s', which is not a function", y->name);
		*val = b->index;
		return 0;
	case R_GLOBAL_INDEX_LEB:
		if (b->kind != WS_GLOBAL)
			return was_error(m, "'%s' is not a global (.globaltype)", y->name);
		*val = b->index;
		return 0;
	case R_TAG_INDEX_LEB:
		if (b->kind != WS_TAG)
			return was_error(m, "'%s' is not a tag (.tagtype)", y->name);
		*val = b->index;
		return 0;
	case R_TABLE_NUMBER_LEB:
		if (b->kind != WS_TABLE)
			return was_error(m, "'%s' is not a table (.tabletype)", y->name);
		*val = b->index;
		return 0;
	}
	return was_error(m, "relocation type %d", r->type);
}

static int patch(lay *L, wrel *r, uint8_t *p)
{
	uint64_t v;

	if (resolve(L, r, &v))
		return -1;
	switch (r->type) {
	case R_FUNCTION_INDEX_LEB: case R_TYPE_INDEX_LEB: case R_GLOBAL_INDEX_LEB: case R_TABLE_NUMBER_LEB:
	case R_MEMORY_ADDR_LEB: case R_TAG_INDEX_LEB:
		pad_uleb(p, v, 5);
		break;
	case R_MEMORY_ADDR_LEB64:
		pad_uleb(p, v, 10);
		break;
	case R_TABLE_INDEX_SLEB: case R_MEMORY_ADDR_SLEB:
		pad_sleb(p, (int64_t)(int32_t)v, 5);
		break;
	case R_TABLE_INDEX_SLEB64: case R_MEMORY_ADDR_SLEB64:
		pad_sleb(p, (int64_t)v, 10);
		break;
	case R_TABLE_INDEX_I32: case R_MEMORY_ADDR_I32: {
		uint32_t w = (uint32_t)v;

		memcpy(p, &w, 4);
		break;
	}
	default:
		memcpy(p, &v, 8);
		break;
	}
	return 0;
}

static void reloc_section(vbuf *o, lay *L, const char *name, uint32_t secidx, wrel **rels, uint32_t *nrels,
			  uint64_t *bases, uint32_t ngroups)
{
	uint32_t n = 0;
	size_t at;

	for (uint32_t g = 0; g < ngroups; g++)
		n += nrels[g];
	if (!n)
		return;
	at = sec_begin(o, SEC_CUSTOM, name);
	uleb(o, secidx);
	uleb(o, n);
	for (uint32_t g = 0; g < ngroups; g++)
		for (uint32_t i = 0; i < nrels[g]; i++) {
			wrel *r = &rels[g][i];

			put8(o, r->type);
			uleb(o, bases[g] + r->off);
			uleb(o, r->type == R_TYPE_INDEX_LEB ? type_of(L, (int32_t)r->sym) : L->m->sym[r->sym].symidx);
			if (rel_addend(r->type))
				sleb(o, r->addend);
		}
	sec_end(o, at);
}

int wasm_write(wmod *m, vbuf *o)
{
	lay L = { .m = m };
	uint64_t vaddr = 0;
	int r = -1;
	uint32_t secidx = 0, code_idx = 0, data_idx = 0;
	uint64_t *cbase = NULL, *dbase = NULL;
	wrel **crel = NULL, **drel = NULL;
	uint32_t *cn = NULL, *dn = NULL;
	vbuf lb = { 0 };

	/* a function's address is a table slot: the table is imported (its symbol last, as LLVM adds it) */
	for (uint32_t pass = 0, need = 0; pass < 2 && !need; pass++)
		for (uint32_t k = 0; k < (pass ? m->nseg : m->nfn) && !need; k++) {
			wrel *rl = pass ? m->seg[k].rel : m->fn[k].rel;
			uint32_t nr = pass ? m->seg[k].nrel : m->fn[k].nrel;

			for (uint32_t i = 0; i < nr && !need; i++) {
				uint8_t t = rl[i].type;

				if ((t == R_ADDR_SLEB64 || t == R_ADDR_SLEB || t == R_ADDR_I64 || t == R_ADDR_I32) &&
				    base(m, &m->sym[rl[i].sym])->kind == WS_FUNCTION) {
					uint32_t s = was_sym(m, "__indirect_function_table", 25);

					m->sym[s].kind = WS_TABLE;
					m->sym[s].used_reloc = true;
					was_reg(m, s);
					need = 1;
				}
			}
		}
	L.tmap = calloc(m->nsig + 1, sizeof(uint32_t));
	L.types = calloc(m->nsig + 1, sizeof(uint32_t));
	L.funcs = calloc(m->nfn + 1, sizeof(uint32_t));
	L.imps = calloc(m->nsym + 1, sizeof(uint32_t));
	if (!L.tmap || !L.types || !L.funcs || !L.imps)
		abort();

	/* kinds, the symbol table */
	for (uint32_t i = 0; i < m->nreg; i++) {
		wsym *y = &m->sym[m->reg[i]], *b = base(m, y);

		if (y->alias >= 0 && b->alias >= 0) {
			was_error(m, "alias chain too long at '%s'", y->name);
			goto out;
		}
		if (b->kind == WS_NONE)
			b->kind = WS_DATA;
		if (y->alias >= 0 && !defined(m, y)) {
			was_error(m, "'%s' is an alias of the undefined '%s'", y->name, b->name);
			goto out;
		}
		y->in_symtab = !(y->temp && !y->used_reloc);
	}
	/* functions, types and imports in the order of the symbols */
	for (uint32_t i = 0, ci = 0; i <= m->nreg; i++) {
		wsym *y, *b;

		/* call_indirect signatures registered before this symbol */
		for (; ci < m->nci && m->ci_at[ci] <= i; ci++)
			type_of(&L, (int32_t)m->ci_sig[ci]);
		if (i == m->nreg)
			break;
		y = &m->sym[m->reg[i]];
		b = base(m, y);
		if (!y->in_symtab)
			continue;
		if (b->kind == WS_FUNCTION || b->kind == WS_TAG) {
			if (b->sig < 0) {
				was_error(m, b->kind == WS_TAG ? "tag '%s' has no .tagtype" : "function '%s' has no .functype", b->name);
				goto out;
			}
			type_of(&L, b->sig);
		}
		if (b->kind == WS_TAG && defined(m, y)) {
			was_error(m, "defining tag '%s' is not supported", b->name);
			goto out;
		}
		if (y->alias >= 0 || defined(m, y))
			continue;
		if (b->kind == WS_FUNCTION)
			b->index = L.nfimp++;
		else if (b->kind == WS_GLOBAL)
			b->index = L.ngimp++;
		else if (b->kind == WS_TABLE)
			b->index = L.ntimp++;
		else if (b->kind == WS_TAG)
			b->index = L.ntagimp++;
		else
			continue;
		L.imps[L.nimps++] = m->reg[i];
	}
	for (uint32_t i = 0; i < m->nreg; i++) {
		wsym *y = &m->sym[m->reg[i]];

		if (y->alias < 0 && y->kind == WS_FUNCTION && y->seg >= 0) {
			if (!m->fn[y->seg].done) {
				was_error(m, "function '%s' has no end_function", y->name);
				goto out;
			}
			y->index = L.nfimp + L.nfuncs;
			L.funcs[L.nfuncs++] = (uint32_t)y->seg;
		}
	}
	for (uint32_t i = 0; i < m->nfn; i++)
		if (!m->sym[m->fn[i].sym].reg) {
			was_error(m, "function '%s' was not registered", m->sym[m->fn[i].sym].name);
			goto out;
		}
	/* call_indirect's types, then the table slots of function addresses */
	for (uint32_t k = 0; k < L.nfuncs; k++) {
		wfunc *f = &m->fn[L.funcs[k]];

		for (uint32_t i = 0; i < f->nrel; i++)
			if (f->rel[i].type == R_TYPE_INDEX_LEB)
				type_of(&L, (int32_t)f->rel[i].sym);
	}
	for (int pass = 0; pass < 2; pass++)
		for (uint32_t k = 0; k < (pass ? m->nseg : L.nfuncs); k++) {
			wrel *rl = pass ? m->seg[k].rel : m->fn[L.funcs[k]].rel;
			uint32_t nr = pass ? m->seg[k].nrel : m->fn[L.funcs[k]].nrel;

			for (uint32_t i = 0; i < nr; i++) {
				uint8_t t = rl[i].type;
				wsym *b = base(m, &m->sym[rl[i].sym]);

				if ((t == R_ADDR_SLEB64 || t == R_ADDR_SLEB || t == R_ADDR_I64 || t == R_ADDR_I32) &&
				    b->kind == WS_FUNCTION && !b->elem) {
					if (L.nelems >= L.celems) {
						L.celems = L.celems ? 2 * L.celems : 16;
						L.elems = realloc(L.elems, L.celems * sizeof(uint32_t));
					}
					L.elems[L.nelems++] = b->index;
					b->elem = L.nelems;
				}
			}
		}
	/* data layout */
	for (uint32_t i = 0; i < m->nseg; i++) {
		uint64_t a = (uint64_t)1 << m->seg[i].p2align;

		vaddr = (vaddr + a - 1) & ~(a - 1);
		m->seg[i].vaddr = vaddr;
		vaddr += m->seg[i].data.n;
	}
	/* symbol table indices */
	for (uint32_t i = 0; i < m->nreg; i++) {
		wsym *y = &m->sym[m->reg[i]];

		if (y->in_symtab)
			y->symidx = L.nsymtab++;
	}
	/* placeholders */
	for (uint32_t k = 0; k < L.nfuncs; k++) {
		wfunc *f = &m->fn[L.funcs[k]];

		for (uint32_t i = 0; i < f->nrel; i++)
			if (patch(&L, &f->rel[i], (uint8_t *)f->body.p + f->rel[i].off))
				goto out;
	}
	for (uint32_t k = 0; k < m->nseg; k++)
		for (uint32_t i = 0; i < m->seg[k].nrel; i++)
			if (patch(&L, &m->seg[k].rel[i], (uint8_t *)m->seg[k].data.p + m->seg[k].rel[i].off))
				goto out;

	/* ---- the object ---- */
	vbuf_put(o, "\0asm\1\0\0\0", 8);
	if (L.ntypes) {
		size_t at = sec_begin(o, SEC_TYPE, NULL);

		uleb(o, L.ntypes);
		for (uint32_t i = 0; i < L.ntypes; i++) {
			const wsig *g = &m->sig[L.types[i]];

			put8(o, 0x60);
			uleb(o, g->np);
			vbuf_put(o, g->t, g->np);
			uleb(o, g->nr);
			vbuf_put(o, g->t + g->np, g->nr);
		}
		sec_end(o, at);
		secidx++;
	}
	{
		size_t at = sec_begin(o, SEC_IMPORT, NULL);

		uleb(o, 1 + L.nimps);
		str(o, "env");
		str(o, "__linear_memory");
		put8(o, 2);
		put8(o, 0x04);		/* limits: 64-bit, no maximum */
		uleb(o, (vaddr + 65535) / 65536);
		for (uint32_t i = 0; i < L.nimps; i++) {
			wsym *y = &m->sym[L.imps[i]];

			str(o, "env");
			str(o, y->name);
			if (y->kind == WS_FUNCTION) {
				put8(o, 0);
				uleb(o, type_of(&L, y->sig));
			} else if (y->kind == WS_GLOBAL) {
				put8(o, 3);
				put8(o, y->gtype ? y->gtype : WV_I64);
				put8(o, y->gmut);
			} else if (y->kind == WS_TAG) {
				put8(o, 4);
				put8(o, 0);
				uleb(o, type_of(&L, y->sig));
			} else {
				put8(o, 1);
				put8(o, WV_FUNCREF);
				put8(o, 0x04);
				uleb(o, L.nelems);
			}
		}
		sec_end(o, at);
		secidx++;
	}
	if (L.nfuncs) {
		size_t at = sec_begin(o, SEC_FUNCTION, NULL);

		uleb(o, L.nfuncs);
		for (uint32_t k = 0; k < L.nfuncs; k++)
			uleb(o, type_of(&L, m->fn[L.funcs[k]].sig));
		sec_end(o, at);
		secidx++;
	}
	if (L.nelems) {
		size_t at = sec_begin(o, SEC_ELEM, NULL);

		uleb(o, 1);
		put8(o, 0);
		put8(o, 0x42);		/* i64.const 1: slot 0 is the null function */
		sleb(o, 1);
		put8(o, 0x0b);
		uleb(o, L.nelems);
		for (uint32_t i = 0; i < L.nelems; i++)
			uleb(o, L.elems[i]);
		sec_end(o, at);
		secidx++;
	}
	if (m->nseg) {
		size_t at = sec_begin(o, SEC_DATACOUNT, NULL);

		uleb(o, m->nseg);
		sec_end(o, at);
		secidx++;
	}
	if (L.nfuncs) {
		size_t at = sec_begin(o, SEC_CODE, NULL), p0 = at + 5;

		cbase = calloc(L.nfuncs, sizeof(uint64_t));
		crel = calloc(L.nfuncs, sizeof(wrel *));
		cn = calloc(L.nfuncs, sizeof(uint32_t));
		uleb(o, L.nfuncs);
		for (uint32_t k = 0; k < L.nfuncs; k++) {
			wfunc *f = &m->fn[L.funcs[k]];
			uint32_t ng = 0;

			/* the locals, run-length encoded, then the size in front of them */
			lb.n = 0;
			for (size_t i = 0; i < f->locals.n; i++)
				if (!i || f->locals.p[i] != f->locals.p[i - 1])
					ng++;
			uleb(&lb, ng);
			for (size_t i = 0; i < f->locals.n;) {
				size_t j = i;

				while (j < f->locals.n && f->locals.p[j] == f->locals.p[i])
					j++;
				uleb(&lb, j - i);
				put8(&lb, (uint8_t)f->locals.p[i]);
				i = j;
			}
			uleb(o, lb.n + f->body.n);
			vbuf_put(o, lb.p, lb.n);
			cbase[k] = o->n - p0;
			crel[k] = f->rel;
			cn[k] = f->nrel;
			vbuf_put(o, f->body.p, f->body.n);
		}
		sec_end(o, at);
		code_idx = secidx++;
	}
	if (m->nseg) {
		size_t at = sec_begin(o, SEC_DATA, NULL), p0 = at + 5;

		dbase = calloc(m->nseg, sizeof(uint64_t));
		drel = calloc(m->nseg, sizeof(wrel *));
		dn = calloc(m->nseg, sizeof(uint32_t));
		uleb(o, m->nseg);
		for (uint32_t k = 0; k < m->nseg; k++) {
			wseg *g = &m->seg[k];

			put8(o, 0);
			put8(o, 0x42);
			sleb(o, (int64_t)g->vaddr);
			put8(o, 0x0b);
			uleb(o, g->data.n);
			dbase[k] = o->n - p0;
			drel[k] = g->rel;
			dn[k] = g->nrel;
			vbuf_put(o, g->data.p, g->data.n);
		}
		sec_end(o, at);
		data_idx = secidx++;
	}
	/* custom sections, but producers and target_features go last (as LLVM) */
	for (uint32_t k = 0; k < m->ncust; k++)
		if (strcmp(m->cust[k].name, "producers") && strcmp(m->cust[k].name, "target_features")) {
			size_t at = sec_begin(o, SEC_CUSTOM, m->cust[k].name);

			vbuf_put(o, m->cust[k].data.p, m->cust[k].data.n);
			sec_end(o, at);
			secidx++;
		}
	/* linking: the symbol table, then the segments */
	{
		size_t at = sec_begin(o, SEC_CUSTOM, "linking"), sub;
		vbuf b = { 0 };

		uleb(o, 2);
		if (L.nsymtab) {
			put8(o, 8);
			uleb(&b, L.nsymtab);
			for (uint32_t i = 0; i < m->nreg; i++) {
				wsym *y = &m->sym[m->reg[i]], *bs = base(m, y);
				uint32_t fl = 0;
				bool def = defined(m, y);

				if (!y->in_symtab)
					continue;
				if (y->weak)
					fl |= WF_WEAK;
				else if (!y->global && def)
					fl |= WF_LOCAL;
				if (y->hidden)
					fl |= WF_HIDDEN;
				if (!def)
					fl |= WF_UNDEFINED;
				if ((bs->kind == WS_TABLE && L.nelems) || y->set || y->nostrip)
					fl |= WF_EXPORTED | WF_NO_STRIP;
				put8(&b, bs->kind);
				uleb(&b, fl);
				if (bs->kind == WS_DATA) {
					str(&b, y->name);
					if (def) {
						uleb(&b, (uint64_t)bs->seg);
						uleb(&b, bs->off);
						uleb(&b, y->size ? y->size : bs->size);
					}
				} else {
					uleb(&b, bs->index);
					if (def)
						str(&b, y->name);
				}
			}
			sub_size(o, b.n);
			vbuf_put(o, b.p, b.n);
			b.n = 0;
		}
		if (m->nseg) {
			put8(o, 5);
			uleb(&b, m->nseg);
			for (uint32_t k = 0; k < m->nseg; k++) {
				str(&b, m->seg[k].name);
				uleb(&b, m->seg[k].p2align);
				uleb(&b, m->seg[k].flags);
			}
			sub_size(o, b.n);
			vbuf_put(o, b.p, b.n);
			b.n = 0;
		}
		if (m->ninit) {
			/* init functions: grouped by section (priority) in the order the
			   sections first appear, as LLVM; the linker sorts by priority */
			put8(o, 6);
			uleb(&b, m->ninit);
			for (uint32_t k = 0; k < m->ninit; k++) {
				uint32_t j = 0;

				while (j < k && m->init[j].prio != m->init[k].prio)
					j++;
				if (j < k)
					continue;	/* this priority's section came earlier */
				for (j = k; j < m->ninit; j++)
					if (m->init[j].prio == m->init[k].prio) {
						uleb(&b, m->init[j].prio);
						uleb(&b, m->sym[m->init[j].sym].symidx);
					}
			}
			sub_size(o, b.n);
			vbuf_put(o, b.p, b.n);
		}
		free(b.p);
		(void)sub;
		sec_end(o, at);
	}
	if (L.nfuncs)
		reloc_section(o, &L, "reloc.CODE", code_idx, crel, cn, cbase, L.nfuncs);
	if (m->nseg)
		reloc_section(o, &L, "reloc.DATA", data_idx, drel, dn, dbase, m->nseg);
	for (int last = 0; last < 2; last++)
		for (uint32_t k = 0; k < m->ncust; k++)
			if (!strcmp(m->cust[k].name, last ? "target_features" : "producers")) {
				size_t at = sec_begin(o, SEC_CUSTOM, m->cust[k].name);

				vbuf_put(o, m->cust[k].data.p, m->cust[k].data.n);
				sec_end(o, at);
			}
	r = 0;
out:
	free(lb.p);
	free(cbase), free(crel), free(cn), free(dbase), free(drel), free(dn);
	free(L.tmap), free(L.types), free(L.funcs), free(L.imps), free(L.elems);
	return r;
}
