/* vcc preprocessor: macro expansion: arguments, stringizing, token pasting, substitution */
#include "ppimpl.h"

/* ---------------- arguments ---------------- */

typedef struct margs {
	const ptok *tok;
	uint32_t *st, *ln;	/* per parameter: start, length in tok */
	const ptok **pre;	/* pre-expanded, lazily */
	uint32_t *preln;
	uint8_t *prest;		/* 0: not computed, 1: done */
	size_t base;		/* tok is sv + base */
	bool elided;		/* the variadic argument was left out (clang's isVarargsElidedUse) */
} margs;

/* "macro 'X' defined here": the #define's name, found from the body's file (errors only) */
VPRIV void macro_note_(pp *p, const pmacro *m, uint32_t before, const char *msg)
{
	uint32_t loc = 0, len = 0;
	const char *src = NULL;

	/* the last "#define NAME" seen (a redefinition replaces the earlier one) */
	for (uint32_t i = 0; i < p->nfe; i++) {
		const vlex_file *f = p->fe[i].f;
		const vtok *q;

		if (!f || !f->tok)
			continue;
		q = f->tok;
		for (uint32_t k = 2; k < f->ntok; k++)
			if (q[k].kind == T_IDENT && q[k].id == m->name && q[k - 1].id == KW_define &&
			    q[k - 2].kind == T_HASH && (q[k - 2].flags & TF_SOL) && p->fe[i].base + q[k].off < before)
				loc = p->fe[i].base + q[k].off, len = q[k].len, src = f->src + q[k].off;
	}
	if (src) {
		pp_dinfo d = { "note", loc, NULL, 0, NULL, 0, NULL, 0 };

		pp_diagx(p, &d, msg, (int)len, src);
	}
}

static void macro_note(pp *p, const pmacro *m)
{
	macro_note_(p, m, UINT32_MAX, "macro '%.*s' defined here");
}

static bool collect_(pp *p, const pmacro *m, const ptok *name, margs *a, ptok *rp)
{
	uint32_t np = m->nparam, slots = np ? np : 1, k = 0, depth = 0, end;
	size_t base = p->svn;
	bool va = m->flags & MF_VARIADIC;
	const ptok *tok;
	ptok t;

	a->elided = false;
	/* pre, then st, ln, preln, then prest: one allocation */
	{
		char *b = alloc(&p->tmp, slots * (sizeof(*a->pre) + 3 * sizeof(uint32_t) + 1));

		a->pre = (const ptok **)b;
		a->st = (uint32_t *)(b + slots * sizeof(*a->pre));
		a->ln = a->st + slots;
		a->preln = a->ln + slots;
		a->prest = (uint8_t *)(a->preln + slots);
	}
	raw(p, &t);		/* '(' */
	a->st[0] = 0;
	/*
	 * The whole invocation in the top expansion context (nested macros):
	 * the arguments are slices of its tokens, which outlive the expansion
	 * (tmp) and are not written; nothing is copied.
	 */
	{
		ctx *c = &p->cx[p->depth - 1];

		if (c->type == C_PTOK && !(c->first | p->pend)) {
			const ptok *pt = c->pt + c->i;
			uint32_t e = c->n - c->i;

			for (uint32_t i = 0; i < e; i++) {
				uint8_t kd = pt[i].kind;

				if (kd == T_LPAREN) {
					depth++;
				} else if (kd == T_RPAREN) {
					if (depth) {
						depth--;
						continue;
					}
					c->i += i + 1;
					*rp = pt[i];
					tok = pt;
					end = i;
					goto done;
				} else if (kd == T_COMMA && !depth && !(va && k + 1 >= np)) {
					if (k + 1 >= slots) {
						k++;	/* too many: counted, reported below */
						continue;
					}
					a->ln[k] = i - a->st[k];
					a->st[++k] = i + 1;
				}
			}
			k = depth = 0;	/* not all there: collect into sv */
		}
	}
	for (;;) {
		ctx *c = &p->cx[p->depth - 1];

		/* runs of plain tokens straight into sv */
		if (likely(c->i < c->n && !(c->first | p->pend)) && c->type != C_REPLAY) {
			bool stop_comma = !(va && k + 1 >= np);
			uint32_t i = c->i, e = c->n;

			if (c->type == C_PTOK) {
				const ptok *pt = c->pt;
				ptok *w = p->sv + p->svn;

				/* (no room for the rest of the context: token by token below) */
				if (unlikely(p->svn + (e - i) > SVMAX))
					e = i;
				for (; i < e; i++) {
					uint8_t kd = pt[i].kind;

					if (kd == T_LPAREN) {
						depth++;
					} else if (kd == T_RPAREN) {
						if (!depth)
							break;
						depth--;
					} else if (kd == T_COMMA && !depth && stop_comma) {
						break;
					}
					*w++ = pt[i];
				}
				p->svn = (size_t)(w - p->sv);
				e = c->n;
			} else {
				const vtok *vt = c->vt;
				bool file = c->type == C_FILE;
				uint32_t loc = c->loc, offloc = file || c->offloc;

				for (; i < e; i++) {
					const vtok *v = &vt[i];
					uint8_t kd = v->kind;

					if (kd == T_LPAREN) {
						depth++;
					} else if (kd == T_RPAREN) {
						if (!depth)
							break;
						depth--;
					} else if (kd == T_COMMA && !depth && stop_comma) {
						break;
					} else if (file && (v->flags & TF_SOL) && kd == T_HASH) {
						break;
					}
					if (unlikely(p->svn == SVMAX)) {
						fprintf(stderr, "vcc: expansion too large\n");
						abort();
					}
					vfill(&p->sv[p->svn++], v, c->src, offloc ? loc + v->off : loc);
				}
			}
			c->i = i;
			if (i == e)
				continue;
		}
		raw_fast(p, &t);
		if (unlikely(t.kind == T_EOF)) {
			error(p, name->loc, "unterminated function-like macro invocation");
			p->svn = base;
			return false;
		}
		if (t.kind == T_LPAREN) {
			depth++;
		} else if (t.kind == T_RPAREN) {
			if (!depth)
				break;
			depth--;
		} else if (t.kind == T_COMMA && !depth && !(va && k + 1 >= np)) {
			if (k + 1 >= slots) {
				k++;
				continue;	/* too many: counted, reported below */
			}
			a->ln[k] = (uint32_t)(p->svn - base) - a->st[k];
			a->st[++k] = (uint32_t)(p->svn - base);
			continue;
		} else if (unlikely(t.kind == P_DIRECTIVE)) {
			warn(p, t.loc, "directive in macro arguments ignored");
			continue;
		}
		svpush(p, &t);
	}
	*rp = t;
	/* the arguments stay in sv[base, svn) until expand() is done with them */
	tok = p->sv + base;
	end = (uint32_t)(p->svn - base);
done:
	if (k >= slots) {
		error(p, name->loc, "too many arguments provided to function-like macro invocation");
		p->svn = base;
		return false;
	}
	a->ln[k] = end - a->st[k];
	if (k + 1 < np) {
		if (va && k + 2 == np) {
			/* GNU: the variadic argument left out entirely */
			a->st[k + 1] = end;
			a->ln[k + 1] = 0;
			a->elided = true;
		} else {
			error(p, t.loc, "too few arguments provided to function-like macro invocation");
			macro_note(p, m);
			p->svn = base;
			return false;
		}
	} else if (np == 1 && va && !a->ln[0]) {
		a->elided = true;	/* F() for F(...): no arguments at all */
	} else if (np == 0 && a->ln[0]) {
		error(p, name->loc, "too many arguments provided to function-like macro invocation");
		p->svn = base;
		return false;
	}
	a->tok = tok;
	a->base = base;
	memset(a->prest, 0, slots);
	return true;
}

static bool collect(pp *p, const pmacro *m, const ptok *name, margs *a, ptok *rp)
{
	bool r;

	p->collecting++;
	r = collect_(p, m, name, a, rp);
	p->collecting--;
	return r;
}

/* clang's MacroArgs::ArgNeedsPreexpansion: any identifier with a macro */
static bool needs_pre(pp *p, const ptok *t, uint32_t n)
{
	for (uint32_t i = 0; i < n; i++)
		if (t[i].kind == T_IDENT && mread_tok(p, &t[i], 1))
			return true;
	return false;
}

/*
 * keep: the result stays in sv[base, svn) (it is only read, and only until
 * expand() resets svn); else it is copied to tmp (va_opt: sv is in use above)
 */
static __attribute__((noinline)) void preexpand_run(pp *p, margs *a, uint32_t k, bool keep,
						       const ptok *at, uint32_t n)
{
	uint8_t pend = p->pend;
	int d = p->depth;
	size_t base = p->svn;
	ptok t;

	if (vstack_low())
		fatal(p, at->loc, "macro arguments nested too deeply");
	push_ptok(p, at, n, 0, 0, 1);
	p->pend = 0;
	p->preexp++;
	for (;;) {
		ctx *c = &p->cx[p->depth - 1];

		/* runs of tokens that are not macros: as next_exp() returns them, straight into sv */
		if (c->type == C_PTOK && c->i < c->n && !(c->first | p->pend) && !p->rec.on &&
		    likely(p->svn + (c->n - c->i) <= SVMAX)) {
			const ptok *pt = c->pt;
			ptok *w = p->sv + p->svn;
			uint32_t i = c->i, e = c->n;

			for (; i < e; i++) {
				uintptr_t me;

				*w = pt[i];
				if (pt[i].kind == T_IDENT && !(pt[i].flags & PF_NOEXP) && (me = mget(p, pt[i].id))) {
					if (!(me & 1))
						break;	/* a macro: next_exp() */
					w->flags |= PF_NOEXP;
				}
				w++;
			}
			p->svn = (size_t)(w - p->sv);
			c->i = i;
		}
		next_exp(p, &t);
		if (t.kind == T_EOF)
			break;
		svpush(p, &t);
	}
	p->preexp--;
	pop_to(p, d);
	p->pend = pend;
	if (keep) {
		a->pre[k] = p->sv + base;
		a->preln[k] = (uint32_t)(p->svn - base);
	} else {
		a->pre[k] = svtake(p, base, &a->preln[k]);
	}
}

/* most arguments need no pre-expansion: that check inline, the rest in preexpand_run */
static inline void preexpand(pp *p, margs *a, uint32_t k, bool keep)
{
	const ptok *at = a->tok + a->st[k];
	uint32_t n = a->ln[k];

	a->prest[k] = 1;
	if (!needs_pre(p, at, n)) {
		a->pre[k] = at;
		a->preln[k] = n;
		return;
	}
	preexpand_run(p, a, k, keep, at, n);
}

VPRIV bool is_strlit(int k)
{
	return k >= T_CHAR && k <= T_U32STRING;
}

/* "#x" (clang's MacroArgs::StringifyArgument) */
static void stringify(pp *p, const ptok *t, uint32_t n, ptok *out)
{
	size_t cap = 3;
	char *b, *w;

	for (uint32_t i = 0; i < n; i++)
		cap += 2 * t[i].len + 1;
	/* __COUNTER__ digit runs, in order: (run, its offset in the result) */
	uint16_t runs[64];
	uint32_t roff[64], nr = 0;

	b = w = alloc(&p->tmp, cap + 16);
	*w++ = '"';
	for (uint32_t i = 0; i < n; i++) {
		uint16_t rc = unlikely(t[i].ctr) && p->rec.on ? t[i].ctr : 0;

		if (i && (t[i].flags & (TF_SPACE | TF_SOL)))
			*w++ = ' ';
		if (is_strlit(t[i].kind)) {
			char tmp[t[i].len + 1];
			size_t l;
			const char *s = pp_spell(&t[i], tmp, &l);

			for (size_t j = 0; j < l; j++) {
				/* digits are never escaped: a run starts where its first digit lands */
				while (rc && p->rec.cp[rc].off == j) {
					if (nr < 64) {
						runs[nr] = rc;
						roff[nr++] = (uint32_t)(w - b);
					} else {
						HC_BAD(p);
					}
					rc = p->rec.cp[rc].next;
				}
				if (s[j] == '\\' || s[j] == '"')
					*w++ = '\\';
				if (s[j] == '\n') {
					*w++ = '\\';
					*w++ = 'n';
					continue;
				}
				*w++ = s[j];
			}
			if (rc)
				HC_BAD(p);
		} else {
			for (; rc; rc = p->rec.cp[rc].next) {
				if (nr < 64) {
					runs[nr] = rc;
					roff[nr++] = (uint32_t)(w - b) + p->rec.cp[rc].off;
				} else {
					HC_BAD(p);
				}
			}
			w = spell_to16(w, &t[i]);	/* (cap has 16 to spare) */
		}
	}
	/* a trailing unescaped backslash would escape the quote: drop it */
	if (w - b >= 2 && w[-1] == '\\') {
		size_t bs = 0;

		for (char *q = w - 1; q > b && *q == '\\'; q--)
			bs++;
		if (bs & 1)
			w--;
	}
	*w++ = '"';
	memset(w, 0, 16);
	memset(out, 0, sizeof(*out));
	out->s = b;
	out->len = (uint32_t)(w - b);
	out->kind = T_STRING;
	out->loc = n ? t[0].loc : 0;
	for (uint32_t k = nr; k-- > 0;) {
		const cprov c = p->rec.cp[runs[k]];

		out->ctr = new_cprov(p, c.val, roff[k], c.len, out->ctr);
	}
}

/* ---------------- token pasting ---------------- */

static const struct { const char *s; uint8_t k; } puncts[] = {
	{ "[", T_LSQUARE }, { "]", T_RSQUARE }, { "(", T_LPAREN }, { ")", T_RPAREN },
	{ "{", T_LBRACE }, { "}", T_RBRACE }, { ".", T_PERIOD }, { "...", T_ELLIPSIS },
	{ "&", T_AMP }, { "&&", T_AMPAMP }, { "&=", T_AMPEQ }, { "*", T_STAR },
	{ "*=", T_STAREQ }, { "+", T_PLUS }, { "++", T_PLUSPLUS }, { "+=", T_PLUSEQ },
	{ "-", T_MINUS }, { "->", T_ARROW }, { "--", T_MINUSMINUS }, { "-=", T_MINUSEQ },
	{ "~", T_TILDE }, { "!", T_EXCLAIM }, { "!=", T_EXCLAIMEQ }, { "/", T_SLASH },
	{ "/=", T_SLASHEQ }, { "%", T_PERCENT }, { "%=", T_PERCENTEQ }, { "<", T_LESS },
	{ "<<", T_LESSLESS }, { "<=", T_LESSEQ }, { "<<=", T_LESSLESSEQ }, { ">", T_GREATER },
	{ ">>", T_GREATERGREATER }, { ">=", T_GREATEREQ }, { ">>=", T_GREATERGREATEREQ },
	{ "^", T_CARET }, { "^=", T_CARETEQ }, { "|", T_PIPE }, { "||", T_PIPEPIPE },
	{ "|=", T_PIPEEQ }, { "?", T_QUESTION }, { ":", T_COLON }, { ";", T_SEMI },
	{ "=", T_EQUAL }, { "==", T_EQUALEQUAL }, { ",", T_COMMA }, { "#", T_HASH },
	{ "##", T_HASHHASH }, { "<:", T_LSQUARE }, { ":>", T_RSQUARE }, { "<%", T_LBRACE },
	{ "%>", T_RBRACE }, { "%:", T_HASH }, { "%:%:", T_HASHHASH },
};

#define IDCH_ROW(b) [b] = 1, [b + 1] = 1, [b + 2] = 1, [b + 3] = 1, [b + 4] = 1, [b + 5] = 1, [b + 6] = 1, [b + 7] = 1
const uint8_t idch_tab[256] = {	/* [a-zA-Z0-9_$] and >= 0x80 */
	['0'] = 1, ['1'] = 1, ['2'] = 1, ['3'] = 1, ['4'] = 1, ['5'] = 1, ['6'] = 1, ['7'] = 1, ['8'] = 1, ['9'] = 1,
	IDCH_ROW('A'), IDCH_ROW('I'), IDCH_ROW('Q'), ['Y'] = 1, ['Z'] = 1,
	IDCH_ROW('a'), IDCH_ROW('i'), IDCH_ROW('q'), ['y'] = 1, ['z'] = 1,
	['_'] = 1, ['$'] = 1,
	IDCH_ROW(0x80), IDCH_ROW(0x88), IDCH_ROW(0x90), IDCH_ROW(0x98), IDCH_ROW(0xa0), IDCH_ROW(0xa8),
	IDCH_ROW(0xb0), IDCH_ROW(0xb8), IDCH_ROW(0xc0), IDCH_ROW(0xc8), IDCH_ROW(0xd0), IDCH_ROW(0xd8),
	IDCH_ROW(0xe0), IDCH_ROW(0xe8), IDCH_ROW(0xf0), IDCH_ROW(0xf8),
};
#undef IDCH_ROW

/* the kind of the single token spelled s[0..n), or -1 */
static int classify(const char *s, size_t n, int rkind)
{
	size_t i;

	if (!n)
		return -1;
	if (idch((unsigned char)s[0]) && !(s[0] >= '0' && s[0] <= '9')) {
		for (i = 0; i < n && idch((unsigned char)s[i]); i++)
			;
		if (i == n)
			return T_IDENT;
		/* encoding prefix + literal */
		if (is_strlit(rkind)) {
			size_t pl = i;

			if (s[pl] == '"' || s[pl] == '\'') {
				bool ch = s[pl] == '\'';

				if (pl == 1 && s[0] == 'L') return ch ? T_WCHAR : T_WSTRING;
				if (pl == 1 && s[0] == 'u') return ch ? T_U16CHAR : T_U16STRING;
				if (pl == 1 && s[0] == 'U') return ch ? T_U32CHAR : T_U32STRING;
				if (pl == 2 && s[0] == 'u' && s[1] == '8') return ch ? T_U8CHAR : T_U8STRING;
			}
		}
		return -1;
	}
	if ((s[0] >= '0' && s[0] <= '9') || (s[0] == '.' && n > 1 && s[1] >= '0' && s[1] <= '9')) {
		for (i = 1; i < n; i++) {
			unsigned char c = (unsigned char)s[i];

			if ((c == '+' || c == '-') && strchr("eEpP", s[i - 1]))
				continue;
			if (!idch(c) && c != '.')
				return -1;
		}
		return T_NUMBER;
	}
	for (i = 0; i < sizeof(puncts) / sizeof(puncts[0]); i++)
		if (strlen(puncts[i].s) == n && !memcmp(puncts[i].s, s, n))
			return puncts[i].k;
	return -1;
}

static bool paste_ok(const ptok *l, const ptok *r)
{
	char b[l->len + r->len + 1], *w = spell_to(b, l);

	w = spell_to(w, r);
	return classify(b, (size_t)(w - b), r->kind) >= 0;
}

static ptok paste(pp *p, const ptok *l, const ptok *r)
{
	char *b = alloc(&p->tmp, l->len + r->len + 24), *w;
	ptok t = *l;
	int k;
	uint32_t ll;

	w = spell_to16(b, l);	/* (24 to spare) */
	ll = (uint32_t)(w - b);
	w = spell_to16(w, r);
	memset(w, 0, 17);
	k = classify(b, (size_t)(w - b), r->kind);
	if (k < 0) {
		error(p, l->loc, "pasting formed '%.*s', an invalid preprocessing token", (int)(w - b), b);
		return *l;
	}
	t.s = b;
	t.len = (uint32_t)(w - b);
	t.kind = (uint8_t)k;
	t.flags = l->flags & (TF_SOL | TF_SPACE);
	t.id = k == T_IDENT ? vintern_copy(b, t.len) : 0;
	if (unlikely(r->ctr) && p->rec.on)
		t.ctr = cp_chain(p, l->ctr, 0, cp_chain(p, r->ctr, ll, 0));
	return t;
}

/* ---------------- expansion ---------------- */

static inline bool hashhash(const ptok *t)
{
	return t->kind == T_HASHHASH && !(t->flags & PF_NOPASTE);
}

/* clang's maybeRemoveCommaBeforeVaArgs (gnu mode), only if the argument was elided */
static void remove_va_comma(pp *p, size_t base, bool *ngs)
{
	if (p->svn == base || p->sv[p->svn - 1].kind != T_COMMA)
		return;
	p->svn--;
	if (p->svn > base && hashhash(&p->sv[p->svn - 1]))
		p->svn--;
	*ngs = false;
}

/* append n tokens, turning ## into non-pasting ones */
static size_t append(pp *p, const ptok *t, uint32_t n)
{
	size_t first = p->svn;
	ptok *w = p->sv + first;

	if (unlikely(n > SVMAX - p->svn)) {
		fprintf(stderr, "vcc: expansion too large\n");
		abort();
	}
	for (uint32_t i = 0; i < n; i++) {
		w[i] = t[i];
		if (unlikely(t[i].kind == T_HASHHASH))
			w[i].flags |= PF_NOPASTE;
	}
	p->svn = first + n;
	return first;
}

/* substitute arguments into m's body (clang's ExpandFunctionArguments), paste, push */
static uint32_t va_opt(pp *p, const pmacro *m, margs *a, uint32_t i, uint32_t to, size_t base,
		       uint32_t eloc, bool *ngsp, bool str);

/* substitute body[from, to) into sv (clang's ExpandFunctionArguments) */
static void subst_range(pp *p, const pmacro *m, margs *a, uint32_t from, uint32_t to, size_t base,
			uint32_t eloc, bool *ngsp)
{
	const vtok *body = m->body;
	const uint8_t *pidx = m->pidx;
	uint32_t np = m->nparam;
	bool ngs = *ngsp, va = m->flags & MF_VARIADIC;
	ptok t;

	bool special = m->flags & (MF_VAOPT | MF_HASH);

	for (uint32_t i = from; i < to; i++) {
		const vtok *cur = &body[i];
		uint32_t k = pidx ? pidx[i] : 0;
		bool before_nonempty, before, after;

		if (i && (cur->flags & TF_SPACE))
			ngs = true;
		if (!special)
			goto plain;	/* no __VA_OPT__, no '#' */
		if (unlikely(cur->kind == T_IDENT && cur->id == KW_VA_OPT && va)) {
			i = va_opt(p, m, a, i, to, base, eloc, &ngs, false);
			continue;
		}
		if (unlikely(cur->kind == T_HASH && m->kind == MK_FUNC && i + 1 < to &&
			     body[i + 1].kind == T_IDENT && body[i + 1].id == KW_VA_OPT && va)) {
			i = va_opt(p, m, a, i + 1, to, base, eloc, &ngs, true);
			continue;
		}
		if (cur->kind == T_HASH && m->kind == MK_FUNC && i + 1 < to && pidx && pidx[i + 1]) {
			uint32_t j = pidx[i + 1] - 1;

			stringify(p, a->tok + a->st[j], a->ln[j], &t);
			t.loc = eloc;
			if (ngs)
				t.flags |= TF_SPACE;
			svpush(p, &t);
			i++;
			ngs = false;
			continue;
		}
plain:
		if (!k) {
			ptok *w;

			if (unlikely(p->svn == SVMAX)) {
				fprintf(stderr, "vcc: expansion too large\n");
				abort();
			}
			w = &p->sv[p->svn++];
			vfill(w, cur, m->src, eloc);
			w->flags = (w->flags & ~(TF_SOL | TF_SPACE)) | (ngs ? TF_SPACE : 0) | PF_MACRO;
			ngs = false;
			continue;
		}
		before_nonempty = p->svn > base && hashhash(&p->sv[p->svn - 1]);
		before = i && body[i - 1].kind == T_HASHHASH;
		after = i + 1 < to && body[i + 1].kind == T_HASHHASH;
		k--;
		if (!before && !after) {
			const ptok *at = a->pre[k];
			uint32_t an = a->preln[k];

			if (an) {
				size_t f = append(p, at, an);

				p->sv[f].flags = (p->sv[f].flags & ~(TF_SOL | TF_SPACE)) | (ngs ? TF_SPACE : 0);
				ngs = false;
			}
			continue;
		}
		/* operand of ##: the unexpanded argument */
		if (a->ln[k]) {
			bool pseudo = false;
			size_t f;

			if (before_nonempty && p->svn - base >= 2 && p->sv[p->svn - 2].kind == T_COMMA &&
			    va && k == np - 1) {
				pseudo = true;	/* GNU ", ## __VA_ARGS__" with arguments */
				p->svn--;
			}
			f = append(p, a->tok + a->st[k], a->ln[k]);
			/* clang: unexpanded argument tokens take the expansion's location */
			for (size_t j = f; j < p->svn; j++)
				p->sv[j].loc = eloc, p->sv[j].flags |= PF_MACRO;
			if (!pseudo)
				p->sv[f].flags = (p->sv[f].flags & ~(TF_SOL | TF_SPACE)) | (ngs ? TF_SPACE : 0);
			ngs = false;
			continue;
		}
		/* empty operand: placemarker */
		if (after) {
			i++;
			continue;
		}
		if (before_nonempty)
			p->svn--;
		if (va && k == np - 1 && a->elided)
			remove_va_comma(p, base, &ngs);
	}
	*ngsp = ngs;
}

/*
 * __VA_OPT__(...) at body[i] (C23 6.10.5.1): the content if the variadic
 * argument is non-empty after expansion, else a placemarker.  With str,
 * the result is stringified (#__VA_OPT__).  Returns the index of its ')'.
 */
static uint32_t va_opt(pp *p, const pmacro *m, margs *a, uint32_t i, uint32_t to, size_t base,
		       uint32_t eloc, bool *ngsp, bool str)
{
	const vtok *body = m->body;
	uint32_t k = m->nparam - 1, b = i + 2, e = b, depth = 0;
	size_t start = p->svn;
	bool before = i && body[i - 1].kind == T_HASHHASH && !str;

	for (; e < to; e++) {
		if (body[e].kind == T_LPAREN)
			depth++;
		else if (body[e].kind == T_RPAREN && !depth--)
			break;
	}
	if (i + 1 >= to || body[i + 1].kind != T_LPAREN || e >= to) {
		error(p, eloc, "__VA_OPT__ must be followed by a parenthesized list");
		return to;
	}
	if (!a->prest[k])
		preexpand(p, a, k, false);
	if (a->preln[k]) {
		bool ngs = false;

		subst_range(p, m, a, b, e, start, eloc, &ngs);
	}
	if (str) {
		ptok t;
		uint32_t n;
		const ptok *r = svtake(p, start, &n);

		stringify(p, r, n, &t);
		t.loc = eloc;
		if (*ngsp)
			t.flags |= TF_SPACE;
		svpush(p, &t);
	} else if (p->svn == start) {
		/* placemarker: a ## pm ## d is a ## d; pm ## d is d; a ## pm is a */
		if (e + 1 < to && body[e + 1].kind == T_HASHHASH)
			e++;
		else if (before && p->svn > base && hashhash(&p->sv[p->svn - 1]))
			p->svn--;
	} else {
		p->sv[start].flags = (p->sv[start].flags & ~(TF_SOL | TF_SPACE)) | (*ngsp ? TF_SPACE : 0);
	}
	*ngsp = false;
	return e;
}

/*
 * subst() for a function-like macro without '#', '##' or __VA_OPT__ (most):
 * every parameter is replaced by its pre-expanded argument, so the result's
 * size is known and it is built in place, without sv.
 */
static void subst_simple(pp *p, const pmacro *m, margs *a, uint8_t nf, uint32_t eloc)
{
	const vtok *body = m->body;
	const uint8_t *pidx = m->pidx;
	const char *src = m->src;
	uint32_t nb = m->nbody, n = 0;
	bool ngs = false;
	ptok *r, *w;

	if (pidx) {
		for (uint32_t i = 0; i < nb; i++) {
			uint32_t k = pidx[i];

			if (!k) {
				n++;
				continue;
			}
			if (!a->prest[k - 1])
				preexpand(p, a, k - 1, true);
			n += a->preln[k - 1];
		}
	} else {
		n = nb;
	}
	if (!n) {
		p->pend |= nf;
		return;
	}
	r = w = alloc(&p->tmp, n * sizeof(ptok));
	for (uint32_t i = 0; i < nb; i++) {
		const vtok *cur = &body[i];
		uint32_t k = pidx ? pidx[i] : 0;

		if (i && (cur->flags & TF_SPACE))
			ngs = true;
		if (!k) {
			vfill(w, cur, src, eloc);
			w->flags = (w->flags & ~(TF_SOL | TF_SPACE)) | (w == r ? nf : ngs ? TF_SPACE : 0) | PF_MACRO;
			ngs = false;
			w++;
			continue;
		}
		{
			const ptok *at = a->pre[k - 1];
			uint32_t an = a->preln[k - 1];

			if (!an)
				continue;
			for (uint32_t j = 0; j < an; j++) {
				w[j] = at[j];
				if (unlikely(at[j].kind == T_HASHHASH))
					w[j].flags |= PF_NOPASTE;
			}
			w->flags = (w->flags & ~(TF_SOL | TF_SPACE)) | (w == r ? nf : ngs ? TF_SPACE : 0);
			ngs = false;
			w += an;
		}
	}
	push_ptok(p, r, n, m->name, 0, 0);	/* r[0] took the name's SOL/SPACE (the context's "first") */
	mdisable(p, m->name);
}

static int subst(pp *p, const pmacro *m, const ptok *name, margs *a, uint32_t eloc)
{
	const vtok *body = m->body;
	const uint8_t *pidx = m->pidx;
	uint32_t nb = m->nbody, n;
	uint8_t nf = name->flags & (TF_SOL | TF_SPACE);
	bool ngs = false;
	size_t base;
	ptok *r;

	if (a && !(m->flags & (MF_PASTE | MF_VAOPT | MF_HASH))) {
		subst_simple(p, m, a, nf, eloc);
		return 0;
	}
	/* pre-expand first: it pushes contexts and uses sv */
	if (a && pidx) {
		for (uint32_t i = 0; i < nb; i++) {
			uint32_t k = pidx[i];

			if (!k || a->prest[k - 1])
				continue;
			if (i && body[i - 1].kind == T_HASH)
				continue;
			if ((i && body[i - 1].kind == T_HASHHASH) || (i + 1 < nb && body[i + 1].kind == T_HASHHASH))
				continue;
			preexpand(p, a, k - 1, true);
		}
	}
	base = p->svn;
	subst_range(p, m, a, 0, nb, base, eloc, &ngs);
	/* token pasting, left to right */
	if (m->flags & MF_PASTE) {
		size_t w = base, e = p->svn;

		for (size_t i = base; i < e;) {
			ptok cur;

			if (!(i + 2 < e && hashhash(&p->sv[i + 1]))) {
				/* not pasted: stays (moves down once something was) */
				if (w != i)
					p->sv[w] = p->sv[i];
				w++, i++;
				continue;
			}
			cur = p->sv[i++];

			while (i + 1 < e && hashhash(&p->sv[i])) {
				ptok r;

				if (p->o->asm_mode && !paste_ok(&cur, &p->sv[i + 1])) {
					/* assembler: an invalid paste leaves both tokens */
					p->sv[w++] = cur;
					cur = p->sv[i + 1];
					i += 2;
					continue;
				}
				r = paste(p, &cur, &p->sv[i + 1]);
				cur = r;
				i += 2;
			}
			p->sv[w++] = cur;
		}
		p->svn = w;
	}
	r = svtake(p, base, &n);
	if (!n) {
		p->pend |= nf;
		return 0;
	}
	/* the context's "first" (the name's SOL/SPACE), applied now: r is ours */
	r[0].flags = (r[0].flags & ~(TF_SOL | TF_SPACE)) | nf;
	push_ptok(p, r, n, m->name, 0, 0);
	mdisable(p, m->name);
	return 0;
}

/* clang's isTrivialSingleTokenExpansion */
static bool trivial(pp *p, const pmacro *m)
{
	const vtok *v = &m->body[0];
	uintptr_t e;

	if (v->kind != T_IDENT)
		return true;
	e = mread(p, v->id);
	if (e && !(e & 1) && v->id != m->name)
		return false;
	if (m->kind == MK_OBJ)
		return true;
	return !m->pidx || !m->pidx[0];
}

static void single(pp *p, const pmacro *m, ptok *t, uint8_t nf, uint32_t eloc)
{
	uintptr_t e;

	vfill(t, &m->body[0], m->src, eloc);
	t->flags |= PF_MACRO;
	t->flags = (t->flags & ~(TF_SOL | TF_SPACE)) | nf;
	if (t->kind == T_IDENT && (e = mread(p, t->id)) && ((e & 1) || (const pmacro *)(e & ~(uintptr_t)1) == m))
		t->flags |= PF_NOEXP;
}

/* expand macro m named by t; 1 if t now holds the result token, 2 if the next raw one */
VPRIV int expand(pp *p, const pmacro *m, ptok *t)
{
	uint8_t nf = t->flags & (TF_SOL | TF_SPACE);
	margs a;
	ptok rp;

	if (m->kind == MK_BUILTIN)
		return builtin(p, m, t);
	if (m->kind == MK_OBJ) {
		ctx *c;

		if (!m->nbody) {
			p->pend |= nf;
			return 0;
		}
		if (m->nbody == 1 && trivial(p, m)) {
			single(p, m, t, nf, t->loc);
			return 1;
		}
		if (m->flags & MF_PASTE)
			return subst(p, m, t, NULL, t->loc);
		c = push(p);
		c->type = C_VTOK;
		c->vt = m->body;
		c->n = m->nbody;
		c->src = m->src;
		c->loc = t->loc;
		c->mid = m->name;
		mdisable(p, m->name);
		/* its first token, as raw() would read it next (the context's "first" applied) */
		c->i = 1;
		vfill(t, &m->body[0], m->src, c->loc);
		t->flags = (t->flags & ~(TF_SOL | TF_SPACE)) | nf | p->pend;
		p->pend = 0;
		return 2;
	}
	if (!collect(p, m, t, &a, &rp))
		return 1;
	if (!m->nbody) {
		p->svn = a.base;
		p->pend |= nf;
		return 0;
	}
	if (m->nbody == 1 && trivial(p, m)) {
		p->svn = a.base;
		single(p, m, t, nf, rp.loc);
		return 1;
	}
	subst(p, m, t, &a, rp.loc);
	p->svn = a.base;
	return 0;
}
