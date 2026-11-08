/* vcc front end: the parser's token window over pp_next */
#include "ccimpl.h"

uint8_t kwclass[KW_END];

VPRIV void init_keywords(void)
{
	static const uint16_t types[] = {
		KW_void, KW_char, KW_short, KW_int, KW_long, KW_float, KW_double, KW_signed, KW_unsigned,
		KW__Bool, KW__Complex, KW___complex__, KW___complex, KW_struct, KW_union, KW_enum, KW_const, KW_volatile, KW_restrict,
		KW__Atomic, KW_typeof, KW___typeof__, KW___typeof, KW___typeof_unqual__, KW___const__,
		KW___const, KW___signed__, KW___signed, KW___volatile__, KW___volatile, KW___restrict__,
		KW___restrict, KW___int128, KW__Float16, KW___fp16, KW__Alignas, KW___attribute__,
		KW___attribute, KW___auto_type,
	};
	static const uint16_t decls[] = {
		KW_typedef, KW_extern, KW_static, KW_auto, KW_register, KW_inline, KW___inline__, KW___inline,
		KW__Noreturn, KW__Thread_local, KW___thread, KW__Static_assert,
	};

	for (int i = KW_auto; i <= KW_thread_local; i++)
		kwclass[i] = KC_KW;
	/* C23 keywords that are plain identifiers in gnu11 */
	kwclass[KW_bool] = kwclass[KW_true] = kwclass[KW_false] = 0;
	kwclass[KW_nullptr] = kwclass[KW_alignof] = kwclass[KW_alignas] = 0;
	kwclass[KW_thread_local] = kwclass[KW_static_assert] = 0;
	kwclass[KW_typeof_unqual] = 0;
	for (size_t i = 0; i < sizeof(types) / sizeof(types[0]); i++)
		kwclass[types[i]] |= KC_TYPE | KC_DECL;
	for (size_t i = 0; i < sizeof(decls) / sizeof(decls[0]); i++)
		kwclass[decls[i]] |= KC_DECL;
}

static void pragma(cc *c, const ptok *t);

VPRIV __attribute__((noinline)) void fetch_directive(cc *c, ptok *t)
{
	do {
		if (unlikely(dc_stale(c)))
			dc_materialize(c);	/* (the pragma acts on the real state) */
		pragma(c, t);
		if (unlikely(c->lz_on))
			lz_fetch(c, t);
		else
			pp_next(c->p, t);
	} while (t->kind == P_DIRECTIVE);
}

/* a lazy body's next token (lazy.c), T_EOF after its '}' */
VPRIV __attribute__((noinline)) void lz_fetch(cc *c, ptok *t)
{
	const lztok *z;

	if (c->lz_hc && c->lzz.k <= c->lzz.close) {
		pp_lazy_tok(c->p, &c->lzz, t);
		c->lze = t->loc;	/* (the T_EOF's location) */
		return;
	}
	if (c->lz_hc) {
		memset(t, 0, sizeof(*t));
		t->kind = T_EOF;
		t->s = "";
		t->loc = c->lze;
		return;
	}
	if (c->lzc >= c->lze) {
		memset(t, 0, sizeof(*t));
		t->kind = T_EOF;
		t->s = "";
		t->loc = c->lze ? c->lzt[c->lze - 1].loc : 0;
		return;
	}
	z = &c->lzt[c->lzc++];
	t->kind = z->kind;
	t->flags = z->flags;
	t->loc = z->loc;
	t->ctr = 0;
	t->len = z->len;
	if (z->kind == T_IDENT) {
		t->id = z->v;
		t->s = vintern_ents[z->v].s;
	} else {
		t->id = 0;
		t->s = c->lzs + z->v;
	}
}

void cc_fetch(cc *c, ptok *t) { fetch(c, t); }

VPRIV ptok *peek(cc *c, int k)
{
	while (c->ntk <= k)
		fetch(c, &c->tk[c->ntk++]);
	return &c->tk[k];
}

/* drop tk[0] with lookahead queued: fixed-size copies (a libc memmove costs more) */
VPRIV __attribute__((noinline)) void next_shift(cc *c)
{
	c->tk[0] = c->tk[1];
	if (c->ntk > 2) {
		c->tk[1] = c->tk[2];
		if (c->ntk > 3)
			c->tk[2] = c->tk[3];
	}
	c->ntk--;
}

VPRIV const char *tokspell(const ptok *t, char *buf, size_t cap)
{
	size_t n;
	char tmp[t->len + 1];
	const char *s;

	if (t->kind == T_EOF)
		return "end of file";
	s = pp_spell(t, tmp, &n);
	if (n >= cap)
		n = cap - 1;
	memcpy(buf, s, n);
	buf[n] = 0;
	return buf;
}

/* the failure path out of line: tokspell's buffer (a VLA) made every call set up a frame */
VPRIV __attribute__((noinline, cold, noreturn)) void expect_fail(cc *c, const char *what)
{
	fatal(c, c->tk[0].loc, "expected %s", what);
	abort();
}

/* "expected ';' after X": reported at the end of the previous token with a
   fix-it, like clang; parsing goes on as if the ';' were there */
VPRIV void expect_semi(cc *c, const char *after)
{
	uint32_t e;
	pp_fixit fx;

	if (likely(c->tk[0].kind == T_SEMI)) {
		next(c);
		return;
	}
	e = pp_tok_end(c->p, c->prevloc);
	fx = (pp_fixit){ e, e, ";" };
	report(c, "error", e, NULL, 0, &fx, 1, "expected ';' %s", after);
}

/* #pragma: only pack changes semantics; the rest is ignored here */
static void pragma(cc *c, const ptok *t)
{
	const char *s = t->s, *e = t->s + t->len;
	int v = 0;

	if ((size_t)(e - s) < 11 || memcmp(s, "pragma", 6))
		return;
	s += 6;
	while (s < e && (*s == ' ' || *s == '\t'))
		s++;
	if ((size_t)(e - s) < 4 || memcmp(s, "pack", 4))
		return;
	s += 4;
	while (s < e && *s != '(')
		s++;
	if (s == e)
		return;
	s++;
	while (s < e && *s == ' ')
		s++;
	if (*s == ')') {
		c->pack[c->npack ? c->npack - 1 : 0] = 0;
		if (!c->npack)
			c->npack = 1;
		return;
	}
	if (!memcmp(s, "push", 4)) {
		s += 4;
		while (s < e && (*s == ' ' || *s == ','))
			s++;
		v = c->npack ? (int)c->pack[c->npack - 1] : 0;
		if (*s >= '0' && *s <= '9')
			v = atoi(s);
		if (c->npack < 63)
			c->pack[c->npack++] = (uint32_t)v;
		return;
	}
	if (!memcmp(s, "pop", 3)) {
		if (c->npack)
			c->npack--;
		return;
	}
	if (*s >= '0' && *s <= '9') {
		if (!c->npack)
			c->npack = 1;
		c->pack[c->npack - 1] = (uint32_t)atoi(s);
	}
}
