/* vcc preprocessor: printing macros: -dM (all defined), -dD (as #define/#undef appear), predefined ones */
#include "ppimpl.h"

/* -dM: every defined macro as "#define NAME[(params)] body\n", sorted by
 * name as clang prints them; builtins (__FILE__, __has_*) are left out.
 * The text is malloc'ed. */
static int mname_cmp(const void *a, const void *b)
{
	const vident *x = &vintern_ents[*(const uint32_t *)a], *y = &vintern_ents[*(const uint32_t *)b];
	int c = memcmp(x->s, y->s, x->len < y->len ? x->len : y->len);

	return c ? c : (x->len > y->len) - (x->len < y->len);
}

static void dm_put(char **b, size_t *n, size_t *cap, const char *s, size_t l)
{
	if (*n + l + 1 > *cap) {
		*cap = (*n + l + 1) * 2;
		*b = realloc(*b, *cap);
	}
	memcpy(*b + *n, s, l);
	*n += l;
}

static void dm_tok(char **b, size_t *n, size_t *cap, const char *src, const vtok *t)
{
	char tmp[256];

	if ((t->flags & TF_DIRTY) && t->len <= sizeof(tmp)) {
		size_t l = vlex_spell(src, t, tmp);

		dm_put(b, n, cap, tmp, l);
	} else {
		dm_put(b, n, cap, src + t->off, t->len);
	}
}


/* one macro as "#define NAME[(params)] body\n" (-dM, -dD) */
static void dm_one(char **bp, size_t *np, size_t *capp, uint32_t id, const pmacro *m)
{
	dm_put(bp, np, capp, "#define ", 8);
	dm_put(bp, np, capp, vintern_ents[id].s, vintern_ents[id].len);
	if (m->kind == MK_FUNC) {
		/* spelled from kinds and ids: a canonical copy's src is its body's text only */
		for (uint32_t j = 0; j < m->nplist; j++) {
			const vtok *q = &m->plist[j];

			if (q->kind == T_IDENT)
				dm_put(bp, np, capp, vintern_ents[q->id].s, vintern_ents[q->id].len);
			else
				dm_put(bp, np, capp, q->kind == T_LPAREN ? "(" : q->kind == T_RPAREN ? ")" :
				       q->kind == T_COMMA ? "," : "...", q->kind == T_ELLIPSIS ? 3 : 1);
		}
	}
	dm_put(bp, np, capp, " ", 1);
	for (uint32_t j = 0; j < m->nbody; j++) {
		if (j && (m->body[j].flags & TF_SPACE))
			dm_put(bp, np, capp, " ", 1);
		dm_tok(bp, np, capp, m->src, &m->body[j]);
	}
	dm_put(bp, np, capp, "\n", 1);
}

/* -dD: the directive in the output, where it is (clang's spelling) */
static void dd_text(pp *p, uint32_t id, const pmacro *m)
{
	char *b = NULL;
	size_t n = 0, cap = 0;

	if (m) {
		dm_one(&b, &n, &cap, id, m);
	} else {
		dm_put(&b, &n, &cap, "#undef ", 7);
		dm_put(&b, &n, &cap, vintern_ents[id].s, vintern_ents[id].len);
		dm_put(&b, &n, &cap, "\n", 1);
	}
	if (p->col)
		out_ch(p, '\n');
	out_put(p, b, n);
	p->col = 0;
	free(b);
}

/* the predefined macros, as clang prints them: those clang predefines in its
 * order (dd_order) with their final <built-in> value, then the rest of
 * <built-in>'s directives (the command line's -D/-U) one by one */
VPRIV void dd_flush(pp *p)
{
	uint8_t *done = calloc(p->ndd + 1, 1);

	p->dd_done = 1;
	for (int k = 0; k < p->o->ndd_order; k++) {
		const char *s = p->o->dd_order[k];
		uint32_t id = vintern_find(s, strlen(s), vintern_hash(s, strlen(s))), any = 0;

		for (uint32_t i = 0; id && i < p->ndd; i++)
			if (p->dd_ev[i].id == id)
				done[i] = 1, any = 1;
		if (any && mget(p, id))
			dd_text(p, id, (const pmacro *)(mget(p, id) & ~(uintptr_t)1));
	}
	for (uint32_t i = 0; i < p->ndd; i++)
		if (!done[i])
			dd_text(p, p->dd_ev[i].id, p->dd_ev[i].m);
	free(done);
	free(p->dd_ev);
	p->dd_ev = NULL;
}

/* -dD: a #define (m) or #undef (NULL) in the text, where it is; <built-in>'s
 * are kept for dd_flush */
VPRIV void dd_out(pp *p, uint32_t id, const pmacro *m, bool builtin)
{
	if (builtin && !p->dd_done) {
		if (p->ndd == p->capdd)
			p->dd_ev = realloc(p->dd_ev, (p->capdd = p->capdd ? 2 * p->capdd : 1024) * sizeof(*p->dd_ev));
		p->dd_ev[p->ndd].id = id;
		p->dd_ev[p->ndd++].m = m;
		return;
	}
	if (!p->dd_done)
		dd_flush(p);
	dd_text(p, id, m);
}

int pp_macros(pp *p, char **out, size_t *len)
{
	uint32_t *ids = NULL, nid = 0, capid = 0;
	char *b = NULL;
	size_t n = 0, cap = 0;

	for (uint32_t d = 0; d < NMDIR; d++) {
		const uintptr_t *pg = p->mdir[d];

		if (!pg)
			continue;
		for (uint32_t k = 0; k < MPAGE; k++) {
			const pmacro *m = (const pmacro *)(pg[k] & ~(uintptr_t)1);

			if (!m || m->kind == MK_BUILTIN)
				continue;
			if (nid == capid)
				ids = realloc(ids, (capid = capid ? capid * 2 : 1024) * sizeof(*ids));
			ids[nid++] = d << MPAGE_BITS | k;
		}
	}
	qsort(ids, nid, sizeof(*ids), mname_cmp);
	for (uint32_t i = 0; i < nid; i++) {
		const pmacro *m = (const pmacro *)(mget(p, ids[i]) & ~(uintptr_t)1);
		dm_one(&b, &n, &cap, ids[i], m);
	}
	free(ids);
	*out = b ? b : calloc(1, 1);
	*len = n;
	return 0;
}
