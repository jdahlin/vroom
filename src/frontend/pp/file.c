/* vcc preprocessor: process-wide allocation, the per-file token skeleton, arenas */
#include "pp-internal.h"

/* ---------------- shared (process-wide) allocation ---------------- */

static char *pp_shared_region_base;
_Atomic size_t pp_shared_region_used;
#define PP_SHARED_REGION_SIZE (1ull << 34)	/* address space; the header cache stops storing at pp_header_cache_memory_limit (8 GB) */

VPRIV void *pp_shared_allocate(size_t n)
{
	char *b0 = __atomic_load_n(&pp_shared_region_base, __ATOMIC_ACQUIRE);
	size_t off;

	if (unlikely(!b0)) {
		char *b = pp_address_space_reserve(PP_SHARED_REGION_SIZE), *z = NULL;

		if (__atomic_compare_exchange_n(&pp_shared_region_base, &z, b, 0, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
			b0 = b;
		else
			munmap(b, PP_SHARED_REGION_SIZE), b0 = z;	/* the winner's; never re-read the base plainly */
	}
	off = atomic_fetch_add(&pp_shared_region_used, (n + 15) & ~(size_t)15);
	if (off + n > PP_SHARED_REGION_SIZE)
		vjob_fail_table_full("preprocessor memory full (16 GB shared region)");
	return b0 + off;
}

VPRIV const char *pp_shared_base(void)
{
	return __atomic_load_n(&pp_shared_region_base, __ATOMIC_ACQUIRE);
}

static int pp_token_spelling_equals(const lex_file *f, const lex_token *v, const char *s)
{
	size_t n = strlen(s);

	return v->len == n && !memcmp(f->src + v->off, s, n);
}

static uint32_t pp_directive_kind_find(const lex_file *f, uint32_t i, uint32_t end)
{
	const lex_token *v = &f->tok[i];

	if (i >= end)
		return PP_DIRECTIVE_NULL;
	if (v->kind == LEX_TOKEN_NUMBER)
		return PP_DIRECTIVE_LINE_MARKER;
	if (v->kind != LEX_TOKEN_IDENTIFIER)
		return PP_DIRECTIVE_OTHER;
	switch (v->id) {
	case KW_if: return PP_DIRECTIVE_IF;
	case KW_ifdef: return PP_DIRECTIVE_IFDEF;
	case KW_ifndef: return PP_DIRECTIVE_IFNDEF;
	case KW_elif: return PP_DIRECTIVE_ELIF;
	case KW_elifdef: return PP_DIRECTIVE_ELIFDEF;
	case KW_elifndef: return PP_DIRECTIVE_ELIFNDEF;
	case KW_else: return PP_DIRECTIVE_ELSE;
	case KW_endif: return PP_DIRECTIVE_ENDIF;
	case KW_define: return PP_DIRECTIVE_DEFINE;
	case KW_undef: return PP_DIRECTIVE_UNDEF;
	case KW_include: return PP_DIRECTIVE_INCLUDE;
	case KW_include_next: return PP_DIRECTIVE_INCLUDE_NEXT;
	case KW_pragma: return PP_DIRECTIVE_PRAGMA;
	case KW_error: return PP_DIRECTIVE_ERROR;
	case KW_warning: return PP_DIRECTIVE_WARNING;
	case KW_line: return PP_DIRECTIVE_LINE;
	}
	if (pp_token_spelling_equals(f, v, "import"))
		return PP_DIRECTIVE_IMPORT;
	if (pp_token_spelling_equals(f, v, "embed"))
		return PP_DIRECTIVE_EMBED;
	if (pp_token_spelling_equals(f, v, "ident") || pp_token_spelling_equals(f, v, "sccs"))
		return PP_DIRECTIVE_IDENT;
	if (pp_token_spelling_equals(f, v, "__include_macros"))
		return PP_DIRECTIVE_IMACROS;	/* -imacros, from <built-in> only */
	return PP_DIRECTIVE_OTHER;
}

static pp_skeleton *pp_skeleton_build(const lex_file *f)
{
	const lex_token *t = f->tok;
	uint32_t n = f->ntok, nd = 0, sp = 0, *stk;
	pp_skeleton *fi = pp_shared_allocate(sizeof(*fi));
	pp_directive_span *d;

	for (uint32_t i = 0; i < n; i++)
		nd += (t[i].flags & LEX_TOKEN_FLAG_LINE_START) && t[i].kind == LEX_TOKEN_HASH;
	d = pp_shared_allocate((nd + 1) * sizeof(*d));
	stk = malloc((nd + 1) * sizeof(*stk));
	nd = 0;
	for (uint32_t i = 0; i < n; i++) {
		uint32_t e, k;

		if (!((t[i].flags & LEX_TOKEN_FLAG_LINE_START) && t[i].kind == LEX_TOKEN_HASH))
			continue;
		for (e = i + 1; e < n && !(t[e].flags & LEX_TOKEN_FLAG_LINE_START); e++)
			;
		k = pp_directive_kind_find(f, i + 1, e);
		d[nd] = (pp_directive_span){ i, e, 0, k };
		switch (k) {
		case PP_DIRECTIVE_IF: case PP_DIRECTIVE_IFDEF: case PP_DIRECTIVE_IFNDEF:
			stk[sp++] = nd;
			break;
		case PP_DIRECTIVE_ELIF: case PP_DIRECTIVE_ELIFDEF: case PP_DIRECTIVE_ELIFNDEF: case PP_DIRECTIVE_ELSE:
			if (!sp) { if (!fi->unbalanced) fi->unbalanced = 1, fi->unbalanced_token = i + 1; break; }
			d[stk[sp - 1]].next_in_group = nd;
			stk[sp - 1] = nd;
			break;
		case PP_DIRECTIVE_ENDIF:
			if (!sp) { if (!fi->unbalanced) fi->unbalanced = 1, fi->unbalanced_token = i + 1; break; }
			d[stk[--sp]].next_in_group = nd;
			break;
		}
		nd++;
		i = e - 1;
	}
	if (sp && !fi->unbalanced)
		fi->unbalanced = 2, fi->unbalanced_token = d[stk[sp - 1]].hash_token + 1;
	free(stk);
	fi->directives = d;
	fi->macros = pp_shared_allocate((nd + 1) * sizeof(*fi->macros));	/* fresh: zero */
	fi->include_answers = pp_shared_allocate((nd + 1) * sizeof(*fi->include_answers));
	fi->directive_count = nd;
	/*
	 * Include guard: the file is one #ifndef X / #if !defined(X) group
	 * (no #else/#elif) with nothing before or after it.
	 */
	if (!fi->unbalanced && nd >= 2 && d[0].hash_token == 0) {
		uint32_t g = 0, e = d[0].end_token;

		if (d[0].kind == PP_DIRECTIVE_IFNDEF && e == 3 && t[2].kind == LEX_TOKEN_IDENTIFIER)
			g = t[2].id;
		else if (d[0].kind == PP_DIRECTIVE_IF && t[2].kind == LEX_TOKEN_EXCLAMATION && t[3].kind == LEX_TOKEN_IDENTIFIER &&
			 t[3].id == KW_defined) {
			if (e == 5 && t[4].kind == LEX_TOKEN_IDENTIFIER)
				g = t[4].id;
			else if (e == 7 && t[4].kind == LEX_TOKEN_LEFT_PAREN && t[5].kind == LEX_TOKEN_IDENTIFIER &&
				 t[6].kind == LEX_TOKEN_RIGHT_PAREN)
				g = t[5].id;
		}
		if (g) {
			uint32_t k = d[0].next_in_group;

			if (d[k].kind == PP_DIRECTIVE_ENDIF && d[k].end_token == n)
				fi->guard_macro = g;
		}
	}
	return fi;
}

/* *fresh: built by this call (the file's first entry in the process) */
VPRIV const pp_skeleton *pp_skeleton_get(const lex_file *f, bool *fresh)
{
	lex_file *w = (lex_file *)f;
	void *fi = __atomic_load_n(&w->pp, __ATOMIC_ACQUIRE), *z = NULL;

	*fresh = false;
	if (likely(fi != NULL))
		return fi;
	fi = pp_skeleton_build(f);
	if (!__atomic_compare_exchange_n(&w->pp, &z, fi, 0, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
		return z;	/* lost the race; ours is wasted */
	*fresh = true;
	return fi;
}

/* 1-based line of byte offset off */
VPRIV uint32_t pp_file_line_of_offset(const lex_file *f, const pp_skeleton *cfi, uint32_t off)
{
	pp_skeleton *fi = (pp_skeleton *)cfi;
	uint32_t *l = __atomic_load_n(&fi->line_offsets, __ATOMIC_ACQUIRE), n, lo, hi;

	if (!l) {
		uint32_t *z = NULL, c = 0;

		const char *end = f->src + f->size;
		for (const char *p = f->src; (p = memchr(p, '\n', end - p)); p++)
			c++;
		l = pp_shared_allocate(((size_t)c + 1) * sizeof(*l));
		c = 0;
		for (const char *p = f->src; (p = memchr(p, '\n', end - p)); p++)
			l[c++] = (uint32_t)(p - f->src);
		__atomic_store_n(&fi->line_count, c, __ATOMIC_RELAXED);	/* a loser writes it too */
		if (!__atomic_compare_exchange_n(&fi->line_offsets, &z, l, 0, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
			l = z;
	}
	n = __atomic_load_n(&fi->line_count, __ATOMIC_ACQUIRE);
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
