/* Macro backtraces: a location inside an expansion, its notes and the fix-it mapped out of a macro. */
#include "../pp/pp-internal.h"
#include "common/buffer.h"
#include <ctype.h>
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

/* the token of file entry e at offset off, or NULL */
static const lex_token *pp_expansion_token_at(const pp_file_entry *e, uint32_t off, uint32_t *ix)
{
	const lex_file *f = e->f;
	uint32_t n = f && f->tok ? f->ntok : 0, lo = 0, hi = n;

	while (lo < hi) {
		uint32_t mid = (lo + hi) / 2;

		if (f->tok[mid].off < off)
			lo = mid + 1;
		else
			hi = mid;
	}
	if (lo < n && f->tok[lo].off == off) {
		*ix = lo;
		return &f->tok[lo];
	}
	return NULL;
}

/*
 * A token from a macro expansion carries the expansion's location: the
 * macro's name (object-like) or the invocation's ')' (function-like).  Find
 * the macro and the invocation's range [*b, *ee).
 */
static const pp_macro *pp_expansion_find(pp *p, uint32_t loc, uint32_t *b, uint32_t *ee)
{
	const pp_file_entry *e;
	const lex_token *t;
	uint32_t i;
	uintptr_t m;

	if (!pp_location_is_valid(p, loc, &e) || !(t = pp_expansion_token_at(e, loc - e->base, &i)))
		return NULL;
	*ee = loc + t->len;
	if (t->kind == LEX_TOKEN_RIGHT_PAREN) {
		int depth = 0;

		for (;; i--) {
			if (e->f->tok[i].kind == LEX_TOKEN_RIGHT_PAREN)
				depth++;
			else if (e->f->tok[i].kind == LEX_TOKEN_LEFT_PAREN && !--depth)
				break;
			if (!i)
				return NULL;
		}
		if (!i)
			return NULL;
		t = &e->f->tok[i - 1];
	}
	if (t->kind != LEX_TOKEN_IDENTIFIER || !(m = pp_macro_table_get(p, t->id) & ~(uintptr_t)1))
		return NULL;
	*b = e->base + t->off;
	return (const pp_macro *)m;
}

/* loc is in the expansion of an object-like macro (-Wexpansion-to-defined) */
VPRIV bool pp_location_is_object_macro(pp *p, uint32_t loc)
{
	uint32_t b, ee;
	const pp_file_entry *e;
	const pp_macro *m;

	if (!pp_location_is_valid(p, loc, &e) || (loc - e->base + 7 <= e->f->size && !memcmp(e->f->src + (loc - e->base), "defined", 7)))
		return false;	/* spelled here */
	m = pp_expansion_find(p, loc, &b, &ee);
	return m && m->kind == PP_MACRO_OBJECT_LIKE;
}

/* "expanded from macro 'M'" at the body token spelled like the diagnosed one (the last #define before) */

static void pp_expansion_note_scan(pp *p, diag_record *top, diag_record *after, const pp_macro *m, uint32_t before, const char *ms, uint32_t mlen,
				bool rng)
{
	uint32_t loc = 0, len = 0;

	for (uint32_t i = 0; i < p->nfe; i++) {
		const lex_file *f = p->fe[i].f;
		const lex_token *q;

		if (!f || !f->tok)
			continue;
		q = f->tok;
		for (uint32_t k = 2; k < f->ntok; k++) {
			uint32_t b = k + 1, pb = 0, pe = 0, hit = 0, first = 0;

			if (!(q[k].kind == LEX_TOKEN_IDENTIFIER && q[k].id == m->name && q[k - 1].id == KW_define &&
			      q[k - 2].kind == LEX_TOKEN_HASH && (q[k - 2].flags & LEX_TOKEN_FLAG_LINE_START) &&
			      (p->fe[i].base + q[k].off < before || f == &p->builtin)))	/* <built-in> comes first */
				continue;
			if (b < f->ntok && q[b].kind == LEX_TOKEN_LEFT_PAREN && !(q[b].flags & LEX_TOKEN_FLAG_SPACE_BEFORE) && !(q[b].flags & LEX_TOKEN_FLAG_LINE_START)) {
				pb = b + 1;
				while (b < f->ntok && q[b].kind != LEX_TOKEN_RIGHT_PAREN && !(q[b].flags & LEX_TOKEN_FLAG_LINE_START))
					b++;
				pe = b++;
			}
			for (uint32_t t = b; t < f->ntok && !(q[t].flags & LEX_TOKEN_FLAG_LINE_START); t++) {
				bool param = false;

				if (!first)
					first = t + 1;
				for (uint32_t a = pb; a < pe && q[t].kind == LEX_TOKEN_IDENTIFIER; a++)
					param |= q[a].id == q[t].id;
				if (!param && q[t].len == mlen && !memcmp(f->src + q[t].off, ms, mlen)) {
					hit = t + 1;
					break;
				}
			}
			if (hit || first) {
				uint32_t t = (hit ? hit : first) - 1;

				loc = p->fe[i].base + q[t].off, len = q[t].len;
			}
		}
	}
	if (loc) {
		diag_record *n = diag_record_begin(p, DIAG_LEVEL_NOTE, loc, "expanded from macro '%.*s'",
					 (int)vintern_ents[m->name].len, vintern_ents[m->name].s);

		if (len > 1 && rng)	/* (clang: the diagnostic's own ranges mapped into the macro; none, none) */
			diag_record_label_add(p, n, loc, loc + len, NULL);
		n->id = "note_macro_expansion_here";
		kid_after(top, after, n);
	}
}

/*
 * The macro chain to the diagnosed token (clang's "expanded from macro" backtrace): in
 * m's body, a token spelled like it (not a parameter), else a macro invoked there whose
 * own body leads to it, depth first.  pp keeps no per-token provenance (the hot path
 * stays as it is): the chain is rebuilt from the definitions when a diagnostic needs it.
 */
typedef struct pp_expansion_frame { const pp_macro *m; const lex_token *ft; uint32_t base, bi, loc, len; } pp_expansion_frame;

/* a #define's site: the name's token in file entry fe */
struct pp_macro_define_record { uint32_t name, loc, fe, k; };

static int pp_macro_definition_compare(const void *a, const void *b)
{
	const struct pp_macro_define_record *x = a, *y = b;

	return x->name != y->name ? (x->name < y->name ? -1 : 1) : x->loc < y->loc ? -1 : x->loc > y->loc;
}

/* index the #defines of the file entries not indexed yet (when a note needs it) */
static void pp_macro_definition_index_update(pp *p)
{
	if (p->macro_definition_site_files == p->nfe)
		return;
	for (uint32_t i = p->macro_definition_site_files; i < p->nfe; i++) {
		const lex_file *f = p->fe[i].f;

		if (!f || !f->tok)
			continue;
		for (uint32_t k = 2; k < f->ntok; k++) {
			const lex_token *q = f->tok;

			if (!(q[k].kind == LEX_TOKEN_IDENTIFIER && q[k - 1].id == KW_define && q[k - 1].kind == LEX_TOKEN_IDENTIFIER &&
			      q[k - 2].kind == LEX_TOKEN_HASH && (q[k - 2].flags & LEX_TOKEN_FLAG_LINE_START)))
				continue;
			if (p->nmdx == p->cmdx) {
				uint32_t c = p->cmdx ? p->cmdx * 2 : 1024;
				struct pp_macro_define_record *n = varena_alloc(&p->perm, c * sizeof(*n));

				if (p->nmdx)
					memcpy(n, p->macro_definition_sites, p->nmdx * sizeof(*n));
				p->macro_definition_sites = n, p->cmdx = c;
			}
			p->macro_definition_sites[p->nmdx++] = (struct pp_macro_define_record){ q[k].id, p->fe[i].base + q[k].off, i, k };
		}
	}
	p->macro_definition_site_files = p->nfe;
	qsort(p->macro_definition_sites, p->nmdx, sizeof(*p->macro_definition_sites), pp_macro_definition_compare);
}

/* m's body in the source: the file tokens of the last #define of its name whose tokens are
   m's (m->body is a shared copy; locations do not order files, so not "the last before") */
static const lex_token *pp_macro_definition_body(pp *p, const pp_macro *m, const pp_file_entry **ep)
{
	size_t lo = 0, hi;

	pp_macro_definition_index_update(p);
	hi = p->nmdx;
	while (lo < hi) {
		size_t mid = (lo + hi) / 2;

		if (p->macro_definition_sites[mid].name <= m->name)
			lo = mid + 1;
		else
			hi = mid;
	}
	for (size_t x = lo; x-- > 0 && p->macro_definition_sites[x].name == m->name;) {
		const pp_file_entry *e = &p->fe[p->macro_definition_sites[x].fe];
		const lex_token *q = e->f->tok;
		uint32_t b = p->macro_definition_sites[x].k + 1, n = e->f->ntok, i;

		if (b < n && q[b].kind == LEX_TOKEN_LEFT_PAREN && !(q[b].flags & (LEX_TOKEN_FLAG_SPACE_BEFORE | LEX_TOKEN_FLAG_LINE_START))) {
			while (b < n && q[b].kind != LEX_TOKEN_RIGHT_PAREN && !(q[b].flags & LEX_TOKEN_FLAG_LINE_START))
				b++;
			b++;
		}
		if (b + m->nbody > n || (m->nbody && (q[b].flags & LEX_TOKEN_FLAG_LINE_START)) ||
		    (b + m->nbody < n && !(q[b + m->nbody].flags & LEX_TOKEN_FLAG_LINE_START)))
			continue;
		for (i = 0; i < m->nbody; i++)
			if (q[b + i].kind != m->body[i].kind || q[b + i].len != m->body[i].len ||
			    memcmp(e->f->src + q[b + i].off, m->src + m->body[i].off, q[b + i].len))
				break;
		if (i == m->nbody) {
			*ep = e;
			return q + b;
		}
	}
	return NULL;
}

static int pp_expansion_chain_build(pp *p, const pp_macro *m, const char *ms, uint32_t mlen, uint32_t before, pp_expansion_frame *out, int n,
		  int cap, int *budget)
{
	const pp_file_entry *e;
	const lex_token *ft;

	if (n >= cap || !m->nbody || --*budget < 0 || !(ft = pp_macro_definition_body(p, m, &e)))
		return -1;
	for (uint32_t i = m->nbody; i-- > 0;) {	/* the last one: clang agrees more often (kernel) */
		const lex_token *t = &m->body[i];

		if ((!m->pidx || !m->pidx[i]) && t->len == mlen && !memcmp(m->src + t->off, ms, mlen)) {
			out[n] = (pp_expansion_frame){ m, ft, e->base, i, e->base + ft[i].off, t->len };
			return n + 1;
		}
	}
	for (uint32_t i = 0; i < m->nbody; i++) {
		const lex_token *t = &m->body[i];
		uintptr_t v;
		const pp_macro *x;
		int r;

		if (t->kind != LEX_TOKEN_IDENTIFIER || (m->pidx && m->pidx[i]) || !(v = pp_macro_table_get(p, t->id) & ~(uintptr_t)1))
			continue;
		x = (const pp_macro *)v;
		if (x == m || x->kind == PP_MACRO_BUILTIN || (x->kind == PP_MACRO_FUNCTION_LIKE && (i + 1 >= m->nbody || m->body[i + 1].kind != LEX_TOKEN_LEFT_PAREN)))
			continue;
		if ((r = pp_expansion_chain_build(p, x, ms, mlen, before, out, n + 1, cap, budget)) > 0) {
			uint32_t len = t->len;

			if (x->kind == PP_MACRO_FUNCTION_LIKE) {	/* clang: the range is the whole invocation */
				int depth = 0;

				for (uint32_t k = i + 1; k < m->nbody; k++) {
					depth += ft[k].kind == LEX_TOKEN_LEFT_PAREN;
					if (ft[k].kind == LEX_TOKEN_RIGHT_PAREN && !--depth) {
						len = ft[k].off + ft[k].len - ft[i].off;
						break;
					}
				}
			}
			out[n] = (pp_expansion_frame){ m, ft, e->base, i, e->base + ft[i].off, len };
			return r;
		}
	}
	return -1;
}

/* clang: an operand range in an argument of the invocation at b is shown in the note on the
   parameter's use in the body (with its parentheses), the use nearest the diagnosed token */
static void pp_expansion_argument_spans(pp *p, diag_record *k, const pp_expansion_frame *f, uint32_t b, const diag_record_span *os, int nos)
{
	const pp_file_entry *e;
	const lex_token *t;
	uint32_t ix, ab[64], ae[64], na = 0;
	int depth = 0;

	if (!f->m->pidx || !pp_location_is_valid(p, b, &e) || !(t = pp_expansion_token_at(e, b - e->base, &ix)) || ix + 1 >= e->f->ntok ||
	    e->f->tok[ix + 1].kind != LEX_TOKEN_LEFT_PAREN)
		return;
	for (uint32_t i = ix + 1; i < e->f->ntok && na < 64; i++) {
		const lex_token *q = &e->f->tok[i];

		if (q->kind == LEX_TOKEN_LEFT_PAREN && !depth++) {
			ab[na] = UINT32_MAX;
			continue;
		}
		if ((q->kind == LEX_TOKEN_RIGHT_PAREN && !--depth) || (q->kind == LEX_TOKEN_COMMA && depth == 1)) {
			na++;
			if (!depth)
				break;
			ab[na] = UINT32_MAX;
			continue;
		}
		if (ab[na] == UINT32_MAX)
			ab[na] = e->base + q->off;
		ae[na] = e->base + q->off + q->len;
	}
	for (int s = 0; s < nos; s++) {
		uint32_t a = 0, best = UINT32_MAX, bd = UINT32_MAX;

		while (a < na && !(ab[a] != UINT32_MAX && os[s].b >= ab[a] && os[s].b < ae[a]))
			a++;
		if (a == na)
			continue;
		for (uint32_t i = 0; i < f->m->nbody; i++) {
			uint32_t d = i > f->bi ? i - f->bi : f->bi - i;

			if (f->m->pidx[i] == a + 1 && d < bd)
				best = i, bd = d;
		}
		if (best == UINT32_MAX)
			continue;
		{
			uint32_t lo = best, hi = best;

			if (lo > 0 && hi + 1 < f->m->nbody && f->ft[lo - 1].kind == LEX_TOKEN_LEFT_PAREN && f->ft[hi + 1].kind == LEX_TOKEN_RIGHT_PAREN)
				lo--, hi++;
			diag_record_label_add(p, k, f->base + f->ft[lo].off, f->base + f->ft[hi].off + f->ft[hi].len, NULL);
		}
	}
}

/* ---- the expansion as text (vcc's help line): the definitions with the arguments put in ---- */

enum { PP_EXPANSION_ARGUMENTS_MAX = 16, XS = 240 };
typedef struct xstr { char s[XS]; uint32_t n; } xstr;

static void pp_expansion_text_append(xstr *x, const char *s, uint32_t n)
{
	if (x->n + n >= XS)
		n = x->n < XS - 1 ? XS - 1 - x->n : 0, x->n = XS;	/* (too long: dropped) */
	memcpy(x->s + (x->n < XS ? x->n : XS - 1 - n), s, n);
	if (x->n < XS)
		x->n += n;
}

/* body tokens [lo, hi) of f with parameters replaced by a[0..na) ('#' and '##' as the
   preprocessor does them; spaces as in the definition) */
static void pp_expansion_text_build(const pp_expansion_frame *f, uint32_t lo, uint32_t hi, const xstr *a, uint32_t na, xstr *out)
{
	const pp_macro *m = f->m;
	bool pp_paste_tokens = false;

	for (uint32_t i = lo; i < hi; i++) {
		const lex_token *t = &f->ft[i];
		uint32_t pi = m->pidx ? m->pidx[i] : 0;

		if (t->kind == LEX_TOKEN_HASH_HASH && m->kind == PP_MACRO_FUNCTION_LIKE) {
			pp_paste_tokens = true;
			continue;
		}
		if (i > lo && (t->flags & LEX_TOKEN_FLAG_SPACE_BEFORE) && !pp_paste_tokens && out->n)
			pp_expansion_text_append(out, " ", 1);
		pp_paste_tokens = false;
		if (t->kind == LEX_TOKEN_HASH && m->kind == PP_MACRO_FUNCTION_LIKE && i + 1 < hi && m->pidx && m->pidx[i + 1]) {
			uint32_t k = m->pidx[i + 1] - 1;

			pp_expansion_text_append(out, "\"", 1);
			for (uint32_t j = 0; k < na && j < a[k].n; j++) {
				if (a[k].s[j] == '"' || a[k].s[j] == '\\')
					pp_expansion_text_append(out, "\\", 1);
				pp_expansion_text_append(out, &a[k].s[j], 1);
			}
			pp_expansion_text_append(out, "\"", 1);
			i++;
			continue;
		}
		if (pi && pi - 1 < na)
			pp_expansion_text_append(out, a[pi - 1].s, a[pi - 1].n);
		else if (!pi)
			pp_expansion_text_append(out, m->src + m->body[i].off, t->len);
	}
}

/* the arguments of the invocation in tokens [b, e) of q (b: the '('), each as text: from the
   source (f NULL, spaces collapsed) or from f's body with f's arguments in */
static uint32_t xargs(const lex_token *q, const char *src, uint32_t b, uint32_t e, const pp_expansion_frame *f, const xstr *fa,
		      uint32_t fna, const pp_macro *callee, xstr *out)
{
	uint32_t na = 0, s0 = b + 1;
	int depth = 0;

	for (uint32_t i = b; i < e; i++) {
		if (q[i].kind == LEX_TOKEN_LEFT_PAREN)
			depth++;
		if ((q[i].kind == LEX_TOKEN_RIGHT_PAREN && !--depth) || (q[i].kind == LEX_TOKEN_COMMA && depth == 1 &&
		     !((callee->flags & PP_MACRO_FLAG_VARIADIC) && na + 1 >= callee->nparam))) {
			if (na == PP_EXPANSION_ARGUMENTS_MAX)
				return na;
			out[na].n = 0;
			if (f)
				pp_expansion_text_build(f, s0, i, fa, fna, &out[na]);
			else
				for (uint32_t k = s0; k < i; k++) {
					if (k > s0 && (q[k].flags & (LEX_TOKEN_FLAG_SPACE_BEFORE | LEX_TOKEN_FLAG_LINE_START)))
						pp_expansion_text_append(&out[na], " ", 1);
					pp_expansion_text_append(&out[na], src + q[k].off, q[k].len);
				}
			na++, s0 = i + 1;
			if (!depth)
				return na;
		}
	}
	return na;
}

/* "the expansion of 'M' here: `...`" on the innermost note; false: nothing to add */
static bool pp_expansion_help_build(pp *p, diag_record *k, const pp_expansion_frame *fr, int n, uint32_t b)
{
	xstr a[2][PP_EXPANSION_ARGUMENTS_MAX], out = { .n = 0 };
	uint32_t na = 0, ix;
	const pp_file_entry *e;
	int cur = 0;

	if (!fr[n - 1].m->nparam && n == 1)
		return false;	/* (an object-like macro: its body is shown already) */
	if (!pp_location_is_valid(p, b, &e) || !pp_expansion_token_at(e, b - e->base, &ix))
		return false;
	if (fr[0].m->kind == PP_MACRO_FUNCTION_LIKE) {
		if (ix + 1 >= e->f->ntok || e->f->tok[ix + 1].kind != LEX_TOKEN_LEFT_PAREN)
			return false;
		na = xargs(e->f->tok, e->f->src, ix + 1, e->f->ntok, NULL, NULL, 0, fr[0].m, a[cur]);
	}
	for (int i = 0; i + 1 < n; i++) {	/* the next level's arguments, from this level's body */
		uint32_t nb = fr[i].bi, nn = 0;

		if (fr[i + 1].m->kind == PP_MACRO_FUNCTION_LIKE)
			nn = xargs(fr[i].ft, NULL, nb + 1, fr[i].m->nbody, &fr[i], a[cur], na, fr[i + 1].m, a[!cur]);
		cur = !cur, na = nn;
	}
	pp_expansion_text_build(&fr[n - 1], 0, fr[n - 1].m->nbody, a[cur], na, &out);
	if (out.n >= XS || !out.n)
		return false;
	diag_record_help_set(p, k, "the expansion of '%.*s' here: `%.*s`", (int)vintern_ents[fr[n - 1].m->name].len,
		     vintern_ents[fr[n - 1].m->name].s, (int)out.n, out.s);
	return true;
}

static void pp_expansion_note_add(pp *p, diag_record *top, diag_record *after, const pp_macro *m, uint32_t before, const char *ms, uint32_t mlen,
			   const diag_record_span *os, int nos, bool rng)
{
	pp_expansion_frame fr[64];
	int budget = 4096, n = pp_expansion_chain_build(p, m, ms, mlen, before, fr, 0, 64, &budget);
	int lim = p->o->macro_bt_limit ? p->o->macro_bt_limit : 6, head = n, tail = 0;

	if (n <= 0) {
		pp_expansion_note_scan(p, top, after, m, before, ms, mlen, rng);
		return;
	}
	/* clang's records (the text styles decide what to show: vcc's folds the middle frames) */
	if (lim > 0 && n > lim)	/* clang: the first half and the last, "skipping N" between */
		head = (lim + 1) / 2, tail = lim / 2;
	for (int i = 0; i < n; i++) {
		diag_record *k;

		if (i == head && tail) {
			k = diag_record_begin(p, DIAG_LEVEL_NOTE, UINT32_MAX,
					  "(skipping %d expansions in backtrace; use -fmacro-backtrace-limit=0 to see all)",
					  n - head - tail);
			kid_after(top, after, k), after = k;
			i = n - tail;
		}
		k = diag_record_begin(p, DIAG_LEVEL_NOTE, fr[i].loc, "expanded from macro '%.*s'",
				  (int)vintern_ents[fr[i].m->name].len, vintern_ents[fr[i].m->name].s);
		if (rng)	/* (clang: the diagnostic's own ranges mapped into the macro) */
			diag_record_label_add(p, k, fr[i].loc, fr[i].loc + fr[i].len, NULL);
		k->id = "note_macro_expansion_here";
		kid_after(top, after, k), after = k;
		if (n == 1 && nos)
			pp_expansion_argument_spans(p, k, &fr[0], before, os, nos);
		if (i == n - 1)
			pp_expansion_help_build(p, k, fr, n, before);	/* (vcc's text and JSON; clang's text has no help) */
	}
}

VPRIV bool pp_fixit_is_in_macro(const pp *p, uint32_t b, uint32_t e)
{
	const pp_file_entry *f;
	const char *s;
	uint32_t i, j, n, at;

	if (e <= b || !pp_location_is_valid(p, b, &f) || e - f->base > f->f->size)
		return false;
	s = f->f->src, n = f->f->size, at = b - f->base;
	for (i = at; i > 0 && (isalnum((unsigned char)s[i - 1]) || s[i - 1] == '_' || s[i - 1] == '$'); i--)
		;
	for (j = at; j < n && (isalnum((unsigned char)s[j]) || s[j] == '_' || s[j] == '$'); j++)
		;
	if (j <= i || isdigit((unsigned char)s[i]) || e - f->base > j)
		return false;
	{
		uint32_t id = vintern_find(s + i, j - i, vintern_hash(s + i, j - i));

		return id && (pp_macro_table_get(p, id) & ~(uintptr_t)1);
	}
}

int pp_location_is_in_macro(const pp *p, uint32_t loc) { return pp_fixit_is_in_macro(p, loc, loc + 1); }

int pp_macro_name_is_defined(const pp *p, const char *name)
{
	size_t n = strlen(name);
	uint32_t id = vintern_find(name, n, vintern_hash(name, n));

	return id && (pp_macro_table_get(p, id) & ~(uintptr_t)1);
}

VPRIV void pp_expansion_notes_adjust(pp *p, diag_record *top, diag_record *d)
{
	const pp_macro *xm;
	uint32_t b, ee;
	bool any = false;

	diag_record_span os[8];
	int nos = 0;
	bool had = d->nspan > 0;	/* (ranges of its own, before the invocation's) */

	if (!d->ms || d->loc == UINT32_MAX || !(xm = pp_expansion_find(p, d->loc, &b, &ee)))
		return;
	d->loc = b;
	if (d->nspan > 8)
		d->nspan = 8;
	for (uint32_t i = 0; i < d->nspan; i++)
		if (d->span[i].b > b && d->span[i].b < ee)	/* (in an argument) */
			os[nos++] = d->span[i];
	for (uint32_t i = 0; i < d->nspan; i++) {
		if (d->span[i].b >= b && d->span[i].b <= ee)
			d->span[i].b = b, d->span[i].e = ee;
		any = true;
	}
	if (!any && (isalnum((unsigned char)d->ms[0]) || d->ms[0] == '_'))	/* (clang: a name has a range, '{' not) */
		diag_record_label_add(p, d, b, ee, NULL);
	pp_expansion_note_add(p, top, d, xm, b, d->ms, d->mlen, os, nos, had);
}
