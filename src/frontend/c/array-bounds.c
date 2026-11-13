/* vcc front end: -Warray-bounds and its text art */
#include "cc-internal.h"

/* -Warray-bounds as a picture (GCC's -fdiagnostics-text-art): the array's elements as cells,
   the index's cell outside them, "..." for what lies between:

     +-----+-----+-----+-----+     +-----+
     | [0] | [1] | [2] | [3] | ... | [6] |
     +-----+-----+-----+-----+     +-----+
     '- 'a': 4 x 'int', 16 bytes -'
                                     ^ index 6: bytes 24..27, 8 bytes past the end   */
typedef struct { char *s; size_t n, cap; } cc_array_bounds_art;

static void cc_array_bounds_art_append(cc_array_bounds_art *a, const char *s)
{
	size_t n = strlen(s);

	if (a->n + n + 1 > a->cap)
		return;	/* (the buffer is sized for the picture) */
	memcpy(a->s + a->n, s, n);
	a->n += n, a->s[a->n] = 0;
}

static void cc_array_bounds_art_repeat(cc_array_bounds_art *a, const char *s, int k)
{
	for (int i = 0; i < k; i++)
		cc_array_bounds_art_append(a, s);
}

static __attribute__((cold, noinline)) void cc_array_bounds_art_draw(cc *c, diag_record *d, const char *nm, int nml, long long n,
						       long long i, long long esz, const char *ety)
{
	bool u = diag_art_style(c->p) == 2;
	const char *h = u ? "\xe2\x94\x80" : "-", *v = u ? "\xe2\x94\x82" : "|";
	const char *cn[2][3] = { { u ? "\xe2\x94\x8c" : "+", u ? "\xe2\x94\xac" : "+", u ? "\xe2\x94\x90" : "+" },
				 { u ? "\xe2\x94\x94" : "+", u ? "\xe2\x94\xb4" : "+", u ? "\xe2\x94\x98" : "+" } };
	long long cell[8];
	bool dots[8] = { 0 };
	int nc = 0, w = 5, oob = -1, gap = 0, col = 0, ab = -1, ae = 0, ob = 0, ll;
	char buf[2048], t[160];
	cc_array_bounds_art a = { buf, 0, sizeof buf };

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
				cc_array_bounds_art_append(&a, !gap ? " " : row == 1 ? " ... " : "     ");
				col += gap ? 5 : 1;
			}
			if (k == oob)
				ob = col + 1 + w / 2;
			else if (ab < 0)
				ab = col;
			if (row == 1) {
				int l = dots[k] ? snprintf(t, sizeof t, "...") : snprintf(t, sizeof t, "[%lld]", cell[k]);

				if (first)
					cc_array_bounds_art_append(&a, v);
				cc_array_bounds_art_repeat(&a, " ", (w - l) / 2);
				cc_array_bounds_art_append(&a, t);
				cc_array_bounds_art_repeat(&a, " ", w - l - (w - l) / 2);
				cc_array_bounds_art_append(&a, v);
			} else {
				if (first)
					cc_array_bounds_art_append(&a, cn[row / 2][0]);
				cc_array_bounds_art_repeat(&a, h, w);
				cc_array_bounds_art_append(&a, cn[row / 2][last ? 2 : 1]);
			}
			col += w + 1 + first;
			if (k != oob)
				ae = col;
		}
		cc_array_bounds_art_append(&a, "\n");
	}
	/* the array's extent under its cells, then the index under its cell */
	ll = snprintf(t, sizeof t, " '%.*s': %lld x '%s', %lld bytes ", nml, nm, n, ety, n * esz);
	if (ll >= (int)sizeof t)
		ll = (int)sizeof t - 1, t[ll - 1] = ' ';
	cc_array_bounds_art_repeat(&a, " ", ab);
	if (ae - ab >= ll + 4) {
		int l = (ae - ab - ll - 2) / 2;

		cc_array_bounds_art_append(&a, u ? "\xe2\x95\xb0" : "'");
		cc_array_bounds_art_repeat(&a, h, l);
		cc_array_bounds_art_append(&a, t);
		cc_array_bounds_art_repeat(&a, h, ae - ab - ll - 2 - l);
		cc_array_bounds_art_append(&a, u ? "\xe2\x95\xaf" : "'");
	} else {
		t[ll - 1] = 0;
		cc_array_bounds_art_append(&a, t + 1);
	}
	cc_array_bounds_art_append(&a, "\n");
	cc_array_bounds_art_repeat(&a, " ", ob);
	if (i == n)
		snprintf(t, sizeof t, "^ index %lld: bytes %lld..%lld, just past the end", i, i * esz, i * esz + esz - 1);
	else if (i > n)
		snprintf(t, sizeof t, "^ index %lld: bytes %lld..%lld, %lld byte%s past the end", i, i * esz,
			 i * esz + esz - 1, (i - n) * esz, (i - n) * esz == 1 ? "" : "s");
	else
		snprintf(t, sizeof t, "^ index %lld: bytes %lld..%lld, %lld byte%s before the start", i, i * esz,
			 i * esz + esz - 1, -i * esz, -i * esz == 1 ? "" : "s");
	cc_array_bounds_art_append(&a, t);
	diag_record_art_set(c->p, d, a.s, a.n);
}

/* -Warray-bounds: a[CONST] outside a's array type (addr: &a[n] may point one past the end) */
CC_PRIVATE void cc_array_bounds_warn(cc *c, uint32_t n, bool addr)
{
	const node *x = AST_NODE(n);
	uint32_t a, s = 0;
	const ctype *at;
	cc_value cv;
	i128 i;

	if (x->op != AST_NODE_INDEX || (x->flags & AST_NODE_FLAG_BOUNDS_CHECKED) || c->dead)
		return;
	AST_NODE(n)->flags |= AST_NODE_FLAG_BOUNDS_CHECKED;
	a = x->a;
	if (AST_NODE(a)->op != AST_NODE_CAST || AST_NODE(a)->aux != AST_CAST_ARRAY_DECAY)
		return;
	a = AST_NODE(a)->a;
	at = AST_TYPE(c, AST_NODE(a)->ty);
	if (at->kind != AST_TYPE_ARRAY || (at->flags & (AST_TYPE_FLAG_VLA | AST_TYPE_FLAG_INCOMPLETE)) || at->n < 2 ||
	    !cc_eval(c, x->b, &cv) || cv.kind != CC_VALUE_INTEGER)
		return;
	if (AST_NODE(a)->op == AST_NODE_VARIABLE)
		s = AST_NODE(a)->a;
	else if (AST_NODE(a)->op != AST_NODE_MEMBER)
		return;
	i = cv.i;
	if (i >= 0 && (i < (i128)at->n || (addr && i == (i128)at->n)))
		return;
	{
		diag_span r[2] = { cc_node_range(c, a), cc_node_range(c, x->b) };
		diag_record *d;

		if (i < 0)
			d = cc_report_warning_begin(c, "array-bounds", true, NULL, r[0].b,
					  "array index %lld is before the beginning of the array", (long long)i);
		else
			d = cc_report_warning_begin(c, "array-bounds", true, NULL, r[0].b,
					  "array index %lld is past the end of the array (that has type '%s')", (long long)i,
					  cc_type_array_name(c, AST_NODE(a)->ty));
		if (!d)
			return;
		diag_record_label_add(c->p, d, r[1].b, r[1].e, NULL);
		if (diag_art_style(c->p) && i > -(1LL << 40) && i < (1LL << 40)) {
			size_t len;
			const char *src = pp_source_text(c->p, r[0].b, r[0].e, &len);
			int m = 0;
			const char *nm = s ? cc_identifier_name(c->syms[s].name, &m) : NULL;

			if (!s && src && len <= 32 && !memchr(src, '\n', len))
				nm = src, m = (int)len;
			if (nm)
				cc_array_bounds_art_draw(c, d, nm, m, (long long)at->n, (long long)i, (long long)cc_type_size(c, at->base),
					   tyname(c, at->base));
		}
		if (s) {
			int m;
			const char *nm = cc_identifier_name(c->syms[s].name, &m);

			diag_record_note_add(c->p, d, c->syms[s].ple ? c->syms[s].plb : c->syms[s].loc, "array '%.*s' declared here", m, nm);
		} else {	/* a member */
			const field *f = &c->tags[AST_NODE(a)->b].f[AST_NODE(a)->c];
			int m;
			const char *nm = cc_identifier_name(f->name, &m);
			uint32_t at0 = f->loc;
			size_t len = 0;
			const char *t = NULL;

			/* clang: at the declaration's start, after the ';', '{' or '}' before it */
			for (uint32_t k = 256; k && !t; k /= 2)
				if (f->loc >= k)
					t = pp_source_text(c->p, f->loc - k, f->loc, &len);
			if (t) {
				size_t j = len;

				while (j && t[j - 1] != ';' && t[j - 1] != '{' && t[j - 1] != '}')
					j--;
				while (j < len && (t[j] == ' ' || t[j] == '\t' || t[j] == '\n'))
					j++;
				if (j && j < len)
					at0 = f->loc - (uint32_t)(len - j);
			}
			diag_record_note_add(c->p, d, at0, "array '%.*s' declared here", m, nm);
		}
		cc_report_runtime(c, d);	/* (clang: DiagRuntimeBehavior) */
	}
}
