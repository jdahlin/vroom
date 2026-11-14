/* vcc front end: #pragma pack, weak, visibility and ms_struct */
#include "cc-internal.h"

/* #pragma pack as clang: pack(AST_NODE), pack(), push[, id][, AST_NODE], pop[, id][, AST_NODE],
   show.  pack[npack-1] is the current value (bytes, 0: default), the entries
   below are saved ones, packid[i] the label of pack[i] (0: none).  A
   malformed pragma or a value other than 0/1/2/4/8/16 is ignored whole. */
static int cc_pragma_pack_word_parse(const char **ps, const char *e, const char **w)
{
	const char *s = *ps;
	int n = 0;

	while (s < e && (*s == ' ' || *s == '\t'))
		s++;
	*w = s;
	while (s < e && (((*s | 32) >= 'a' && (*s | 32) <= 'z') || (*s >= '0' && *s <= '9') || *s == '_'))
		s++, n++;
	*ps = s;
	return n;
}

static void cc_pragma_pack_set(cc *c, uint32_t v)
{
	if (!c->npack)
		c->pack[0] = 0, c->packid[0] = 0, c->npack = 1;
	c->pack[c->npack - 1] = v;
}

static void cc_pragma_pack_parse(cc *c, const char *s, const char *e)
{
	const char *w, *it[3];
	int n, len[3], ni = 0, push = 0, pop = 0;
	uint64_t id = 0;
	long v = -1;

	while (s < e && (*s == ' ' || *s == '\t'))
		s++;
	if (s == e || *s++ != '(')
		return;
	for (;;) {
		n = cc_pragma_pack_word_parse(&s, e, &w);
		while (s < e && (*s == ' ' || *s == '\t'))
			s++;
		if (n) {
			if (ni == 3)
				return;
			it[ni] = w, len[ni++] = n;
		}
		if (s < e && *s == ')')
			break;
		if (s == e || *s != ',' || !n)
			return;
		s++;
	}
	for (int i = 0; i < ni; i++) {
		if (it[i][0] >= '0' && it[i][0] <= '9') {
			char *x;
			if (v >= 0)
				return;
			v = strtol(it[i], &x, 0);
			if (x != it[i] + len[i] || (v && (v > 16 || (v & (v - 1)))))
				return;
		} else if (i == 0 && len[i] == 4 && !memcmp(it[i], "push", 4))
			push = 1;
		else if (i == 0 && len[i] == 3 && !memcmp(it[i], "pop", 3))
			pop = 1;
		else if (i == 0 && len[i] == 4 && !memcmp(it[i], "show", 4)) {
			if (ni != 1)
				return;
			return;
		} else if ((push || pop) && !id && v < 0) {
			id = 1469598103934665603ull;
			for (int k = 0; k < len[i]; k++)
				id = (id ^ (unsigned char)it[i][k]) * 1099511628211ull;
			id |= 1;
		} else
			return;
	}
	if (!push && !pop) {
		if (ni > 1)
			return;
		cc_pragma_pack_set(c, v < 0 ? 0 : (uint32_t)v);
		return;
	}
	if (push) {
		uint32_t cur = c->npack ? c->pack[c->npack - 1] : 0;
		if (!c->npack)
			c->pack[0] = 0, c->npack = 1;
		if (c->npack == 64)
			return;
		c->packid[c->npack - 1] = id;
		c->pack[c->npack] = cur, c->packid[c->npack] = 0;
		c->npack++;
	} else if (c->npack > 1) {
		if (!id)
			c->npack--;
		else
			for (int i = c->npack - 2; i >= 0; i--)
				if (c->packid[i] == id) {
					c->npack = i + 1;
					break;
				}
		c->packid[c->npack - 1] = 0;
	}
	if (v >= 0)
		cc_pragma_pack_set(c, (uint32_t)v);
}

/* #pragma: pack and weak change semantics; the rest is ignored here */
CC_PRIVATE void cc_pragma_parse(cc *c, const ptok *t)
{
	const char *s = t->s, *e = t->s + t->len;

	if ((size_t)(e - s) < 11 || memcmp(s, "pragma", 6))
		return;
	s += 6;
	while (s < e && (*s == ' ' || *s == '\t'))
		s++;
	if (e - s >= 4 && !memcmp(s, "weak", 4) && (e - s == 4 || s[4] == ' ' || s[4] == '\t')) {
		if (unlikely(c->dr_on))
			cc_replay_pragma_log(c, 1);
		cc_pragma_weak_parse(c, s + 4, e, t->s, t->loc);
		return;
	}
	if (e - s > 14 && !memcmp(s, "GCC visibility", 14)) {
		/* #pragma GCC visibility push(hidden) / pop: the default for later declarations */
		const char *q = s + 14;

		while (q < e && (*q == ' ' || *q == '\t'))
			q++;
		if (e - q >= 3 && !memcmp(q, "pop", 3)) {
			if (c->nvis)
				c->nvis--;
		} else if (e - q >= 4 && !memcmp(q, "push", 4)) {
			const char *a = memchr(q, '(', (size_t)(e - q)), *b = a ? memchr(a, ')', (size_t)(e - a)) : NULL;

			if (a && b && c->nvis < 16) {
				size_t n;
				char *keep;

				for (a++; a < b && (*a == ' ' || *a == '\t'); a++)
					;
				while (b > a && (b[-1] == ' ' || b[-1] == '\t'))
					b--;
				n = (size_t)(b - a);
				keep = cc_heap_allocate(c, n + 1);
				memcpy(keep, a, n);
				keep[n] = 0;
				c->strs[c->nstr] = (cstr){ keep, (uint32_t)(n + 1), 1, LEX_TOKEN_STRING };
				c->visstk[c->nvis++] = ++c->nstr;	/* (string index + 1, as attrs.visibility) */
			}
		}
		return;
	}
	if (e - s >= 9 && !memcmp(s, "ms_struct", 9) && (e - s == 9 || s[9] == ' ' || s[9] == '\t')) {
		if (unlikely(c->dr_on))
			cc_replay_pragma_log(c, 2);
		s += 9;
		while (s < e && (*s == ' ' || *s == '\t')) s++;
		c->ms_struct = e - s >= 2 && !memcmp(s, "on", 2);
		return;
	}
	if ((size_t)(e - s) < 4 || memcmp(s, "pack", 4))
		return;
	if (unlikely(c->dr_on))
		cc_replay_pragma_log(c, 2);
	cc_pragma_pack_parse(c, s + 4, e);
}
