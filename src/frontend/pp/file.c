/* vcc preprocessor: process-wide allocation, the per-file token skeleton, arenas */
#include "ppimpl.h"

/* ---------------- shared (process-wide) allocation ---------------- */

static char *shared_base;
_Atomic size_t shared_used;
#define SHARED_MAX (1ull << 34)	/* address space; the header cache stops storing at hc_mem_max (8 GB) */

VPRIV void *salloc(size_t n)
{
	char *b0 = __atomic_load_n(&shared_base, __ATOMIC_ACQUIRE);
	size_t off;

	if (unlikely(!b0)) {
		char *b = vm(SHARED_MAX), *z = NULL;

		if (__atomic_compare_exchange_n(&shared_base, &z, b, 0, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
			b0 = b;
		else
			munmap(b, SHARED_MAX), b0 = z;	/* the winner's; never re-read the base plainly */
	}
	off = atomic_fetch_add(&shared_used, (n + 15) & ~(size_t)15);
	if (off + n > SHARED_MAX)
		vjob_fail_table_full("preprocessor memory full (16 GB shared region)");
	return b0 + off;
}

VPRIV const char *shared_base0(void)
{
	return __atomic_load_n(&shared_base, __ATOMIC_ACQUIRE);
}

static int spelled(const vlex_file *f, const vtok *v, const char *s)
{
	size_t n = strlen(s);

	return v->len == n && !memcmp(f->src + v->off, s, n);
}

static uint32_t dir_kind(const vlex_file *f, uint32_t i, uint32_t end)
{
	const vtok *v = &f->tok[i];

	if (i >= end)
		return D_NULL;
	if (v->kind == T_NUMBER)
		return D_LINEMARK;
	if (v->kind != T_IDENT)
		return D_OTHER;
	switch (v->id) {
	case KW_if: return D_IF;
	case KW_ifdef: return D_IFDEF;
	case KW_ifndef: return D_IFNDEF;
	case KW_elif: return D_ELIF;
	case KW_elifdef: return D_ELIFDEF;
	case KW_elifndef: return D_ELIFNDEF;
	case KW_else: return D_ELSE;
	case KW_endif: return D_ENDIF;
	case KW_define: return D_DEFINE;
	case KW_undef: return D_UNDEF;
	case KW_include: return D_INCLUDE;
	case KW_include_next: return D_INCLUDE_NEXT;
	case KW_pragma: return D_PRAGMA;
	case KW_error: return D_ERROR;
	case KW_warning: return D_WARNING;
	case KW_line: return D_LINE;
	}
	if (spelled(f, v, "import"))
		return D_IMPORT;
	if (spelled(f, v, "embed"))
		return D_EMBED;
	if (spelled(f, v, "ident") || spelled(f, v, "sccs"))
		return D_IDENT;
	if (spelled(f, v, "__include_macros"))
		return D_IMACROS;	/* -imacros, from <built-in> only */
	return D_OTHER;
}

static pfile *pfile_build(const vlex_file *f)
{
	const vtok *t = f->tok;
	uint32_t n = f->ntok, nd = 0, sp = 0, *stk;
	pfile *fi = salloc(sizeof(*fi));
	pdir *d;

	for (uint32_t i = 0; i < n; i++)
		nd += (t[i].flags & TF_SOL) && t[i].kind == T_HASH;
	d = salloc((nd + 1) * sizeof(*d));
	stk = malloc((nd + 1) * sizeof(*stk));
	nd = 0;
	for (uint32_t i = 0; i < n; i++) {
		uint32_t e, k;

		if (!((t[i].flags & TF_SOL) && t[i].kind == T_HASH))
			continue;
		for (e = i + 1; e < n && !(t[e].flags & TF_SOL); e++)
			;
		k = dir_kind(f, i + 1, e);
		d[nd] = (pdir){ i, e, 0, k };
		switch (k) {
		case D_IF: case D_IFDEF: case D_IFNDEF:
			stk[sp++] = nd;
			break;
		case D_ELIF: case D_ELIFDEF: case D_ELIFNDEF: case D_ELSE:
			if (!sp) { if (!fi->bad) fi->bad = 1, fi->badtok = i + 1; break; }
			d[stk[sp - 1]].next = nd;
			stk[sp - 1] = nd;
			break;
		case D_ENDIF:
			if (!sp) { if (!fi->bad) fi->bad = 1, fi->badtok = i + 1; break; }
			d[stk[--sp]].next = nd;
			break;
		}
		nd++;
		i = e - 1;
	}
	if (sp && !fi->bad)
		fi->bad = 2, fi->badtok = d[stk[sp - 1]].tok + 1;
	free(stk);
	fi->dir = d;
	fi->mac = salloc((nd + 1) * sizeof(*fi->mac));	/* fresh: zero */
	fi->inc = salloc((nd + 1) * sizeof(*fi->inc));
	fi->ndir = nd;
	/*
	 * Include guard: the file is one #ifndef X / #if !defined(X) group
	 * (no #else/#elif) with nothing before or after it.
	 */
	if (!fi->bad && nd >= 2 && d[0].tok == 0) {
		uint32_t g = 0, e = d[0].end;

		if (d[0].kind == D_IFNDEF && e == 3 && t[2].kind == T_IDENT)
			g = t[2].id;
		else if (d[0].kind == D_IF && t[2].kind == T_EXCLAIM && t[3].kind == T_IDENT &&
			 t[3].id == KW_defined) {
			if (e == 5 && t[4].kind == T_IDENT)
				g = t[4].id;
			else if (e == 7 && t[4].kind == T_LPAREN && t[5].kind == T_IDENT &&
				 t[6].kind == T_RPAREN)
				g = t[5].id;
		}
		if (g) {
			uint32_t k = d[0].next;

			if (d[k].kind == D_ENDIF && d[k].end == n)
				fi->guard = g;
		}
	}
	return fi;
}

/* *fresh: built by this call (the file's first entry in the process) */
VPRIV const pfile *pfile_get(const vlex_file *f, bool *fresh)
{
	vlex_file *w = (vlex_file *)f;
	void *fi = __atomic_load_n(&w->pp, __ATOMIC_ACQUIRE), *z = NULL;

	*fresh = false;
	if (likely(fi != NULL))
		return fi;
	fi = pfile_build(f);
	if (!__atomic_compare_exchange_n(&w->pp, &z, fi, 0, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
		return z;	/* lost the race; ours is wasted */
	*fresh = true;
	return fi;
}

/* 1-based line of byte offset off */
VPRIV uint32_t file_line(const vlex_file *f, const pfile *cfi, uint32_t off)
{
	pfile *fi = (pfile *)cfi;
	uint32_t *l = __atomic_load_n(&fi->lines, __ATOMIC_ACQUIRE), n, lo, hi;

	if (!l) {
		uint32_t *z = NULL, c = 0;

		for (uint32_t i = 0; i < f->size; i++)
			c += f->src[i] == '\n';
		l = salloc((c + 1) * sizeof(*l));
		c = 0;
		for (uint32_t i = 0; i < f->size; i++)
			if (f->src[i] == '\n')
				l[c++] = i;
		__atomic_store_n(&fi->nlines, c, __ATOMIC_RELAXED);	/* a loser writes it too */
		if (!__atomic_compare_exchange_n(&fi->lines, &z, l, 0, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
			l = z;
	}
	n = __atomic_load_n(&fi->nlines, __ATOMIC_ACQUIRE);
	/* number of newlines before off */
	lo = 0, hi = n;
	while (lo < hi) {
		uint32_t mid = (lo + hi) / 2;

		if (l[mid] < off)
			lo = mid + 1;
		else
			hi = mid;
	}
	return lo + 1;
}
