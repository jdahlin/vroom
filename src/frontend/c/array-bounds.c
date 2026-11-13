/* vcc front end: -Warray-bounds and its text art */
#include "ccimpl.h"

/* -Warray-bounds as a picture (GCC's -fdiagnostics-text-art): the array's elements as cells,
   the index's cell outside them, "..." for what lies between:

     +-----+-----+-----+-----+     +-----+
     | [0] | [1] | [2] | [3] | ... | [6] |
     +-----+-----+-----+-----+     +-----+
     '- 'a': 4 x 'int', 16 bytes -'
                                     ^ index 6: bytes 24..27, 8 bytes past the end   */
typedef struct { char *s; size_t n, cap; } artb;

static void art_put(artb *a, const char *s)
{
	size_t n = strlen(s);

	if (a->n + n + 1 > a->cap)
		return;	/* (the buffer is sized for the picture) */
	memcpy(a->s + a->n, s, n);
	a->n += n, a->s[a->n] = 0;
}

static void art_rep(artb *a, const char *s, int k)
{
	for (int i = 0; i < k; i++)
		art_put(a, s);
}

static __attribute__((cold, noinline)) void bounds_art(cc *c, vdiag *d, const char *nm, int nml, long long n,
						       long long i, long long esz, const char *ety)
{
	bool u = pp_diag_art_on(c->p) == 2;
	const char *h = u ? "\xe2\x94\x80" : "-", *v = u ? "\xe2\x94\x82" : "|";
	const char *cn[2][3] = { { u ? "\xe2\x94\x8c" : "+", u ? "\xe2\x94\xac" : "+", u ? "\xe2\x94\x90" : "+" },
				 { u ? "\xe2\x94\x94" : "+", u ? "\xe2\x94\xb4" : "+", u ? "\xe2\x94\x98" : "+" } };
	long long cell[8];
	bool dots[8] = { 0 };
	int nc = 0, w = 5, oob = -1, gap = 0, col = 0, ab = -1, ae = 0, ob = 0, ll;
	char buf[2048], t[160];
	artb a = { buf, 0, sizeof buf };

	if (i < 0)
		gap = i < -1, oob = nc, cell[nc++] = i;
	for (long long k = 0; k < n && k < 4; k++, nc++) {	/* [0] [1] [2] [3], or [0] [1] ... [n-1] */
		dots[nc] = n > 4 && k == 2;
		cell[nc] = n > 4 && k == 3 ? n - 1 : k;
	}
	if (i >= n)
		gap = i > n, oob = nc, cell[nc++] = i;
	for (int k = 0; k < nc; k++) {
		int l = snprintf(t, sizeof t, "[%lld]", cell[k]) + 2;

		w = l > w ? l : w;
	}
	/* top, names, bottom: two groups (the array, the index's cell), "..." between when apart */
	for (int row = 0; row < 3; row++) {
		col = 0;
		for (int k = 0; k < nc; k++) {
			bool first = k == 0 || k == oob || k - 1 == oob, last = k == nc - 1 || k + 1 == oob || k == oob;

			if (first && k) {
				art_put(&a, !gap ? " " : row == 1 ? " ... " : "     ");
				col += gap ? 5 : 1;
			}
			if (k == oob)
				ob = col + 1 + w / 2;
			else if (ab < 0)
				ab = col;
			if (row == 1) {
				int l = dots[k] ? snprintf(t, sizeof t, "...") : snprintf(t, sizeof t, "[%lld]", cell[k]);

				if (first)
					art_put(&a, v);
				art_rep(&a, " ", (w - l) / 2);
				art_put(&a, t);
				art_rep(&a, " ", w - l - (w - l) / 2);
				art_put(&a, v);
			} else {
				if (first)
					art_put(&a, cn[row / 2][0]);
				art_rep(&a, h, w);
				art_put(&a, cn[row / 2][last ? 2 : 1]);
			}
			col += w + 1 + first;
			if (k != oob)
				ae = col;
		}
		art_put(&a, "\n");
	}
	/* the array's extent under its cells, then the index under its cell */
	ll = snprintf(t, sizeof t, " '%.*s': %lld x '%s', %lld bytes ", nml, nm, n, ety, n * esz);
	if (ll >= (int)sizeof t)
		ll = (int)sizeof t - 1, t[ll - 1] = ' ';
	art_rep(&a, " ", ab);
	if (ae - ab >= ll + 4) {
		int l = (ae - ab - ll - 2) / 2;

		art_put(&a, u ? "\xe2\x95\xb0" : "'");
		art_rep(&a, h, l);
		art_put(&a, t);
		art_rep(&a, h, ae - ab - ll - 2 - l);
		art_put(&a, u ? "\xe2\x95\xaf" : "'");
	} else {
		t[ll - 1] = 0;
		art_put(&a, t + 1);
	}
	art_put(&a, "\n");
	art_rep(&a, " ", ob);
	if (i == n)
		snprintf(t, sizeof t, "^ index %lld: bytes %lld..%lld, just past the end", i, i * esz, i * esz + esz - 1);
	else if (i > n)
		snprintf(t, sizeof t, "^ index %lld: bytes %lld..%lld, %lld byte%s past the end", i, i * esz,
			 i * esz + esz - 1, (i - n) * esz, (i - n) * esz == 1 ? "" : "s");
	else
		snprintf(t, sizeof t, "^ index %lld: bytes %lld..%lld, %lld byte%s before the start", i, i * esz,
			 i * esz + esz - 1, -i * esz, -i * esz == 1 ? "" : "s");
	art_put(&a, t);
	pp_diag_art(c->p, d, a.s, a.n);
}

/* -Warray-bounds: a[CONST] outside a's array type (addr: &a[n] may point one past the end) */
VPRIV void bounds_warn(cc *c, uint32_t n, bool addr)
{
	const node *x = N(n);
	uint32_t a, s = 0;
	const ctype *at;
	cval cv;
	i128 i;

	if (x->op != N_INDEX || (x->flags & NF_BOUNDS) || c->dead)
		return;
	N(n)->flags |= NF_BOUNDS;
	a = x->a;
	if (N(a)->op != N_CAST || N(a)->aux != CK_DECAY)
		return;
	a = N(a)->a;
	at = T(c, N(a)->ty);
	if (at->kind != TY_ARRAY || (at->flags & (TF_VLA | TF_INCOMPLETE)) || at->n < 2 ||
	    !cc_eval(c, x->b, &cv) || cv.kind != CV_INT)
		return;
	if (N(a)->op == N_VAR)
		s = N(a)->a;
	else if (N(a)->op != N_MEMBER)
		return;
	i = cv.i;
	if (i >= 0 && (i < (i128)at->n || (addr && i == (i128)at->n)))
		return;
	{
		pp_span r[2] = { rng(c, a), rng(c, x->b) };
		vdiag *d;

		if (i < 0)
			d = cc_warn_begin(c, "array-bounds", true, NULL, r[0].b,
					  "array index %lld is before the beginning of the array", (long long)i);
		else
			d = cc_warn_begin(c, "array-bounds", true, NULL, r[0].b,
					  "array index %lld is past the end of the array (that has type '%s')", (long long)i,
					  arr_tyname(c, N(a)->ty));
		if (!d)
			return;
		pp_diag_label(c->p, d, r[1].b, r[1].e, NULL);
		if (pp_diag_art_on(c->p) && i > -(1LL << 40) && i < (1LL << 40)) {
			size_t len;
			const char *src = pp_src(c->p, r[0].b, r[0].e, &len);
			int m = 0;
			const char *nm = s ? idname(c->syms[s].name, &m) : NULL;

			if (!s && src && len <= 32 && !memchr(src, '\n', len))
				nm = src, m = (int)len;
			if (nm)
				bounds_art(c, d, nm, m, (long long)at->n, (long long)i, (long long)ty_size(c, at->base),
					   tyname(c, at->base));
		}
		if (s) {
			int m;
			const char *nm = idname(c->syms[s].name, &m);

			pp_diag_note(c->p, d, c->syms[s].ple ? c->syms[s].plb : c->syms[s].loc, "array '%.*s' declared here", m, nm);
		} else {	/* a member */
			const field *f = &c->tags[N(a)->b].f[N(a)->c];
			int m;
			const char *nm = idname(f->name, &m);
			uint32_t at0 = f->loc;
			size_t len = 0;
			const char *t = NULL;

			/* clang: at the declaration's start, after the ';', '{' or '}' before it */
			for (uint32_t k = 256; k && !t; k /= 2)
				if (f->loc >= k)
					t = pp_src(c->p, f->loc - k, f->loc, &len);
			if (t) {
				size_t j = len;

				while (j && t[j - 1] != ';' && t[j - 1] != '{' && t[j - 1] != '}')
					j--;
				while (j < len && (t[j] == ' ' || t[j] == '\t' || t[j] == '\n'))
					j++;
				if (j && j < len)
					at0 = f->loc - (uint32_t)(len - j);
			}
			pp_diag_note(c->p, d, at0, "array '%.*s' declared here", m, nm);
		}
		rt_emit(c, d);	/* (clang: DiagRuntimeBehavior) */
	}
}
