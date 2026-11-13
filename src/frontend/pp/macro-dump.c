/* vcc preprocessor: printing macros: -dM (all defined), -dD (as #define/#undef appear), predefined ones */
#include "pp-internal.h"

/* -dM: every defined macro as "#define NAME[(params)] body\n", sorted by
 * name as clang prints them; builtins (__FILE__, __has_*) are left out.
 * The text is malloc'ed. */
static int pp_macro_name_compare(const void *a, const void *b)
{
	const vident *x = &vintern_ents[*(const uint32_t *)a], *y = &vintern_ents[*(const uint32_t *)b];
	int c = memcmp(x->s, y->s, x->len < y->len ? x->len : y->len);

	return c ? c : (x->len > y->len) - (x->len < y->len);
}

static void pp_macro_dump_append(char **b, size_t *n, size_t *cap, const char *s, size_t l)
{
	if (*n + l + 1 > *cap) {
		*cap = (*n + l + 1) * 2;
		*b = realloc(*b, *cap);
	}
	memcpy(*b + *n, s, l);
	*n += l;
}

static void pp_macro_dump_token(char **b, size_t *n, size_t *cap, const char *src, const lex_token *t)
{
	char tmp[256];

	if ((t->flags & LEX_TOKEN_FLAG_SPLICED) && t->len <= sizeof(tmp)) {
		size_t l = lex_token_spell(src, t, tmp);

		pp_macro_dump_append(b, n, cap, tmp, l);
	} else {
		pp_macro_dump_append(b, n, cap, src + t->off, t->len);
	}
}


/* one macro as "#define NAME[(params)] body\n" (-dM, -dD) */
static void pp_macro_dump_one(char **bp, size_t *np, size_t *capp, uint32_t id, const pp_macro *m)
{
	pp_macro_dump_append(bp, np, capp, "#define ", 8);
	pp_macro_dump_append(bp, np, capp, vintern_ents[id].s, vintern_ents[id].len);
	if (m->kind == PP_MACRO_FUNCTION_LIKE) {
		/* spelled from kinds and ids: a canonical copy's src is its body's text only */
		for (uint32_t j = 0; j < m->nplist; j++) {
			const lex_token *q = &m->plist[j];

			if (q->kind == LEX_TOKEN_IDENTIFIER)
				pp_macro_dump_append(bp, np, capp, vintern_ents[q->id].s, vintern_ents[q->id].len);
			else
				pp_macro_dump_append(bp, np, capp, q->kind == LEX_TOKEN_LEFT_PAREN ? "(" : q->kind == LEX_TOKEN_RIGHT_PAREN ? ")" :
				       q->kind == LEX_TOKEN_COMMA ? "," : "...", q->kind == LEX_TOKEN_ELLIPSIS ? 3 : 1);
		}
	}
	pp_macro_dump_append(bp, np, capp, " ", 1);
	for (uint32_t j = 0; j < m->nbody; j++) {
		if (j && (m->body[j].flags & LEX_TOKEN_FLAG_SPACE_BEFORE))
			pp_macro_dump_append(bp, np, capp, " ", 1);
		pp_macro_dump_token(bp, np, capp, m->src, &m->body[j]);
	}
	pp_macro_dump_append(bp, np, capp, "\n", 1);
}

/* -dD: the directive in the output, where it is (clang's spelling) */
static void pp_macro_define_event_print(pp *p, uint32_t id, const pp_macro *m)
{
	char *b = NULL;
	size_t n = 0, cap = 0;

	if (m) {
		pp_macro_dump_one(&b, &n, &cap, id, m);
	} else {
		pp_macro_dump_append(&b, &n, &cap, "#undef ", 7);
		pp_macro_dump_append(&b, &n, &cap, vintern_ents[id].s, vintern_ents[id].len);
		pp_macro_dump_append(&b, &n, &cap, "\n", 1);
	}
	if (p->col)
		pp_output_append_character(p, '\n');
	pp_output_append(p, b, n);
	p->col = 0;
	free(b);
}

/* the predefined macros, as clang prints them: those clang predefines in its
 * order (dd_order) with their final <built-in> value, then the rest of
 * <built-in>'s directives (the command line's -D/-U) one by one */
VPRIV void pp_macro_define_events_flush(pp *p)
{
	uint8_t *done = calloc(p->ndd + 1, 1);

	p->define_events_printed = 1;
	for (int k = 0; k < p->o->ndd_order; k++) {
		const char *s = p->o->dd_order[k];
		uint32_t id = vintern_find(s, strlen(s), vintern_hash(s, strlen(s))), any = 0;

		for (uint32_t i = 0; id && i < p->ndd; i++)
			if (p->define_events[i].id == id)
				done[i] = 1, any = 1;
		if (any && pp_macro_table_get(p, id))
			pp_macro_define_event_print(p, id, (const pp_macro *)(pp_macro_table_get(p, id) & ~(uintptr_t)1));
	}
	for (uint32_t i = 0; i < p->ndd; i++)
		if (!done[i])
			pp_macro_define_event_print(p, p->define_events[i].id, p->define_events[i].m);
	free(done);
	free(p->define_events);
	p->define_events = NULL;
}

/* -dD: a #define (m) or #undef (NULL) in the text, where it is; <built-in>'s
 * are kept for pp_macro_define_events_flush */
VPRIV void pp_macro_define_event_record(pp *p, uint32_t id, const pp_macro *m, bool builtin)
{
	if (builtin && !p->define_events_printed) {
		if (p->ndd == p->define_event_capacity)
			p->define_events = realloc(p->define_events, (p->define_event_capacity = p->define_event_capacity ? 2 * p->define_event_capacity : 1024) * sizeof(*p->define_events));
		p->define_events[p->ndd].id = id;
		p->define_events[p->ndd++].m = m;
		return;
	}
	if (!p->define_events_printed)
		pp_macro_define_events_flush(p);
	pp_macro_define_event_print(p, id, m);
}

int pp_macros_dump(pp *p, char **out, size_t *len)
{
	uint32_t *ids = NULL, nid = 0, capid = 0;
	char *b = NULL;
	size_t n = 0, cap = 0;

	for (uint32_t d = 0; d < PP_MACRO_DIRECTORY_SIZE; d++) {
		const uintptr_t *pg = p->macro_directory[d];

		if (!pg)
			continue;
		for (uint32_t k = 0; k < PP_MACRO_PAGE_SIZE; k++) {
			const pp_macro *m = (const pp_macro *)(pg[k] & ~(uintptr_t)1);

			if (!m || m->kind == PP_MACRO_BUILTIN)
				continue;
			if (nid == capid)
				ids = realloc(ids, (capid = capid ? capid * 2 : 1024) * sizeof(*ids));
			ids[nid++] = d << PP_MACRO_PAGE_BITS | k;
		}
	}
	qsort(ids, nid, sizeof(*ids), pp_macro_name_compare);
	for (uint32_t i = 0; i < nid; i++) {
		const pp_macro *m = (const pp_macro *)(pp_macro_table_get(p, ids[i]) & ~(uintptr_t)1);
		pp_macro_dump_one(&b, &n, &cap, ids[i], m);
	}
	free(ids);
	*out = b ? b : calloc(1, 1);
	*len = n;
	return 0;
}
