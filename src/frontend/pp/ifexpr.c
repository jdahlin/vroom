/* vcc preprocessor: #if expressions */
#include "ppimpl.h"

VPRIV ival parse_number(pp *p, const ptok *t)
{
	char tmp[t->len + 1];
	size_t n, i = 0;
	const char *s = pp_spell(t, tmp, &n);
	unsigned base = 10;
	uint64_t v = 0;
	bool over = false, u = false;
	ival r;

	if (n > 1 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
		base = 16;
		i = 2;
	} else if (n > 1 && s[0] == '0' && (s[1] == 'b' || s[1] == 'B')) {
		base = 2;
		i = 2;
	} else if (s[0] == '0') {
		base = 8;
	}
	for (; i < n; i++) {
		unsigned d;
		char c = s[i];

		if (c == '\'')
			continue;
		if (c >= '0' && c <= '9')
			d = (unsigned)(c - '0');
		else if (base == 16 && c >= 'a' && c <= 'f')
			d = (unsigned)(c - 'a' + 10);
		else if (base == 16 && c >= 'A' && c <= 'F')
			d = (unsigned)(c - 'A' + 10);
		else
			break;
		if (d >= base)
			break;
		if (v > (UINT64_MAX - d) / base)
			over = true;
		v = v * base + d;
	}
	for (; i < n; i++) {
		char c = s[i];

		if (c == 'u' || c == 'U')
			u = true;
		else if (c != 'l' && c != 'L') {
			error(p, t->loc, "invalid integer constant '%.*s' in preprocessor expression", (int)n, s);
			break;
		}
	}
	if (over)
		error(p, t->loc, "integer literal is too large");
	r.v = v;
	r.u = u || v > INT64_MAX;
	return r;
}

static ival parse_char(pp *p, const ptok *t)
{
	char tmp[t->len + 1];
	size_t n;
	const char *s = pp_spell(t, tmp, &n), *e;
	uint64_t v = 0;
	unsigned cnt = 0;
	bool narrow = t->kind == T_CHAR || t->kind == T_U8CHAR;
	ival r;

	while (*s != '\'') {
		s++;
		n--;
	}
	e = s + n - 1;
	for (s++; s < e; cnt++) {
		uint64_t c;

		if (*s == '\\') {
			s++;
			switch (*s) {
			case 'n': c = '\n'; s++; break;
			case 't': c = '\t'; s++; break;
			case 'r': c = '\r'; s++; break;
			case 'a': c = 7; s++; break;
			case 'b': c = 8; s++; break;
			case 'f': c = 12; s++; break;
			case 'v': c = 11; s++; break;
			case 'e': c = 27; s++; break;
			case 'x':
				c = 0;
				for (s++; s < e && strchr("0123456789abcdefABCDEF", *s); s++)
					c = c * 16 + (unsigned)(*s <= '9' ? *s - '0' : (*s | 32) - 'a' + 10);
				break;
			default:
				if (*s >= '0' && *s <= '7') {
					c = 0;
					for (int k = 0; k < 3 && s < e && *s >= '0' && *s <= '7'; k++, s++)
						c = c * 8 + (unsigned)(*s - '0');
				} else {
					c = (unsigned char)*s++;
				}
			}
		} else {
			c = (unsigned char)*s++;
		}
		v = narrow ? (v << 8) | (c & 0xff) : c;
	}
	if (cnt > 1 && t->kind == T_CHAR) {
		pp_span sp;
		pp_dinfo d = { "warning", t->loc, &sp, 0, NULL, 0, NULL, 0 };

		if (cnt != 4)	/* (4: -Wfour-char-constants, off) */
			pp_warnx(p, "multichar", true, NULL, &d, "multi-character character constant");
		if (cnt > 4)
			pp_diag(p, t->loc, "warning", "character constant too long for its type");
	}
	if (narrow && cnt == 1 && t->kind == T_CHAR && !p->o->unsigned_char)
		v = (uint64_t)(int64_t)(int8_t)v;
	else if (narrow && cnt > 1)
		v = (uint64_t)(int64_t)(int32_t)v;
	r.v = v;
	r.u = false;
	return r;
}

typedef struct ev {
	pp *p;
	ptok t;		/* lookahead */
	uint32_t loc;
	uint32_t prev;		/* the last token consumed */
	uint32_t lb, le;	/* the last primary expression: first and last token */
	bool bad;
} ev;

static void eadv(ev *e)
{
	e->prev = e->t.loc;
	next_exp(e->p, &e->t);
}

static ival ecomma(ev *e, bool live);

static ival eprimary_(ev *e, bool live);
static ival eprimary(ev *e, bool live)
{
	uint32_t b = e->t.loc;
	ival r = eprimary_(e, live);

	e->lb = b, e->le = e->prev;
	return r;
}

static ival eprimary_(ev *e, bool live)
{
	ival r = { 0, false };
	pp *p = e->p;

	if (vstack_guard_low())
		fatal(p, e->t.loc, "#if expression nested too deeply");
	switch (e->t.kind) {
	case T_NUMBER:
		r = parse_number(p, &e->t);
		eadv(e);
		return r;
	case T_CHAR: case T_WCHAR: case T_U8CHAR: case T_U16CHAR: case T_U32CHAR:
		r = parse_char(p, &e->t);
		eadv(e);
		return r;
	case T_LPAREN:
		eadv(e);
		r = ecomma(e, live);
		if (e->t.kind != T_RPAREN) {
			if (!e->bad)
				error(p, e->loc, "expected ')' in preprocessor expression");
			e->bad = true;
			return r;
		}
		eadv(e);
		return r;
	case T_PLUS:
		eadv(e);
		return eprimary(e, live);
	case T_MINUS:
		eadv(e);
		r = eprimary(e, live);
		r.v = -r.v;
		return r;
	case T_TILDE:
		eadv(e);
		r = eprimary(e, live);
		r.v = ~r.v;
		return r;
	case T_EXCLAIM:
		eadv(e);
		r = eprimary(e, live);
		r.v = !r.v;
		r.u = false;
		return r;
	case T_IDENT:
		if (e->t.id == KW_defined) {
			ptok n;
			bool paren;

			if (unlikely(obj_macro_at(p, e->t.loc))) {
				pp_dinfo d = { "warning", e->t.loc, NULL, 0, NULL, 0, "defined", 7 };

				pp_warnx(p, "expansion-to-defined", true, NULL, &d,
					 "macro expansion producing 'defined' has undefined behavior");
			}

			raw(p, &n);
			paren = n.kind == T_LPAREN;
			if (paren)
				raw(p, &n);
			if (n.kind != T_IDENT) {
				error(p, e->loc, "macro name must be an identifier");
				e->bad = true;
				return r;
			}
			r.v = mdefined(p, n.id);
			if (paren) {
				raw(p, &n);
				if (n.kind != T_RPAREN) {
					error(p, e->loc, "missing ')' after 'defined'");
					e->bad = true;
				}
			}
			eadv(e);
			return r;
		}
		if (p->o->c23 && e->t.id == KW_true)
			r.v = 1;	/* C23: true is 1 in #if (false is 0 anyway) */
		{
			uint32_t id = e->t.id, loc = e->t.loc;

			eadv(e);	/* an identifier that is not a macro: 0 */
			if (p->o->c23 && e->t.kind == T_CHAR && e->t.loc == loc + 2 && vintern_ents[id].len == 2 &&
			    !memcmp(vintern_ents[id].s, "u8", 2)) {
				/* C23 u8'a' (the lexer's C17 tokens u8 and 'a') */
				e->t.kind = T_U8CHAR;
				r = parse_char(p, &e->t);
				eadv(e);
				return r;
			}
			if (e->t.kind == T_LPAREN && !e->bad) {
				error(p, loc, "function-like macro '%.*s' is not defined",
				      (int)vintern_ents[id].len, vintern_ents[id].s);
				e->bad = true;
			}
		}
		return r;
	default:
		if (!e->bad)
			error(p, e->loc, "invalid token at start of a preprocessor expression");
		e->bad = true;
		return r;
	}
}

static int prec(int k)
{
	switch (k) {
	case T_STAR: case T_SLASH: case T_PERCENT: return 10;
	case T_PLUS: case T_MINUS: return 9;
	case T_LESSLESS: case T_GREATERGREATER: return 8;
	case T_LESS: case T_GREATER: case T_LESSEQ: case T_GREATEREQ: return 7;
	case T_EQUALEQUAL: case T_EXCLAIMEQ: return 6;
	case T_AMP: return 5;
	case T_CARET: return 4;
	case T_PIPE: return 3;
	case T_AMPAMP: return 2;
	case T_PIPEPIPE: return 1;
	}
	return 0;
}

static ival ebinary(ev *e, int minp, bool live)
{
	ival l = eprimary(e, live);

	for (;;) {
		int op = e->t.kind, pr = prec(op);
		ival r;
		bool u;

		if (!pr || pr < minp || e->bad)
			return l;
		eadv(e);
		if (op == T_AMPAMP) {
			r = ebinary(e, pr + 1, live && l.v);
			l.v = l.v && r.v;
			l.u = false;
			continue;
		}
		if (op == T_PIPEPIPE) {
			r = ebinary(e, pr + 1, live && !l.v);
			l.v = l.v || r.v;
			l.u = false;
			continue;
		}
		r = ebinary(e, pr + 1, live);
		u = l.u || r.u;
		switch (op) {
		case T_STAR: l.v = l.v * r.v; break;
		case T_SLASH: case T_PERCENT:
			if (!r.v) {
				if (live) {
					error(e->p, e->loc, "division by zero in preprocessor expression");
					e->bad = true;
				}
				l.v = 0;
				break;
			}
			if (u)
				l.v = op == T_SLASH ? l.v / r.v : l.v % r.v;
			else if ((int64_t)r.v == -1)
				l.v = op == T_SLASH ? -l.v : 0;
			else
				l.v = op == T_SLASH ? (uint64_t)((int64_t)l.v / (int64_t)r.v)
						    : (uint64_t)((int64_t)l.v % (int64_t)r.v);
			break;
		case T_PLUS: l.v = l.v + r.v; break;
		case T_MINUS: l.v = l.v - r.v; break;
		case T_LESSLESS:
			l.v = r.v >= 64 ? 0 : l.v << r.v;
			u = l.u;
			break;
		case T_GREATERGREATER:
			if (r.v >= 64)
				l.v = l.u || (int64_t)l.v >= 0 ? 0 : ~0ull;
			else
				l.v = l.u ? l.v >> r.v : (uint64_t)((int64_t)l.v >> r.v);
			u = l.u;
			break;
		case T_LESS: l.v = u ? l.v < r.v : (int64_t)l.v < (int64_t)r.v; u = false; break;
		case T_GREATER: l.v = u ? l.v > r.v : (int64_t)l.v > (int64_t)r.v; u = false; break;
		case T_LESSEQ: l.v = u ? l.v <= r.v : (int64_t)l.v <= (int64_t)r.v; u = false; break;
		case T_GREATEREQ: l.v = u ? l.v >= r.v : (int64_t)l.v >= (int64_t)r.v; u = false; break;
		case T_EQUALEQUAL: l.v = l.v == r.v; u = false; break;
		case T_EXCLAIMEQ: l.v = l.v != r.v; u = false; break;
		case T_AMP: l.v &= r.v; break;
		case T_CARET: l.v ^= r.v; break;
		case T_PIPE: l.v |= r.v; break;
		}
		l.u = u;
	}
}

static ival econd(ev *e, bool live)
{
	ival c = ebinary(e, 1, live), a, b;

	if (e->t.kind != T_QUESTION || e->bad)
		return c;
	eadv(e);
	a = ecomma(e, live && c.v);
	if (e->t.kind != T_COLON) {
		if (!e->bad)
			error(e->p, e->loc, "expected ':' in preprocessor expression");
		e->bad = true;
		return a;
	}
	eadv(e);
	b = econd(e, live && !c.v);
	a = c.v ? a : b;
	a.u = a.u || b.u;
	return a;
}

static ival ecomma(ev *e, bool live)
{
	ival r = econd(e, live);

	while (e->t.kind == T_COMMA && !e->bad) {
		eadv(e);
		r = econd(e, live);
	}
	return r;
}

/* evaluate tokens [b, e) of fc's current file as an #if expression */
VPRIV bool eval_if(pp *p, const ctx *fc, uint32_t b, uint32_t e)
{
	int d = p->depth;
	ev E = { .p = p, .loc = fc->loc + fc->vt[b > 0 ? b - 1 : 0].off };
	ival r;

	if (b >= e) {
		error(p, E.loc, "#if with no expression");
		return false;
	}
	push_line(p, fc, b, e);
	p->in_dir++;
	eadv(&E);
	r = ecomma(&E, true);
	p->in_dir--;
	if (!E.bad && E.t.kind != T_EOF)
	{
		/* clang: at the token, the operand before it underlined */
		pp_span sp = { E.lb, pp_tok_end(p, E.le) };
		pp_dinfo d = { "error", E.t.loc, &sp, E.lb && E.le >= E.lb, NULL, 0, NULL, 0 };
		char tmp[E.t.len + 1];
		size_t ml;

		if (E.t.flags & PF_MACRO)	/* (the note at the macro's body token) */
			d.ms = pp_spell(&E.t, tmp, &ml), d.mlen = (uint32_t)ml;

		pp_diagx(p, &d, "token is not a valid binary operator in a preprocessor subexpression");
	}
	pop_to(p, d);
	return r.v != 0;
}

/* a constant expression of already expanded tokens (#embed's limit()): false if not one */
VPRIV bool eval_ptoks(pp *p, const ptok *t, uint32_t n, uint64_t *v)
{
	int d = p->depth;
	ev E = { .p = p, .loc = n ? t[0].loc : 0 };
	ival r;

	if (!n)
		return false;
	push_ptok(p, t, n, 0, 0, 1);
	p->in_dir++;
	eadv(&E);
	r = ecomma(&E, true);
	p->in_dir--;
	if (E.t.kind != T_EOF)
		E.bad = true;
	pop_to(p, d);
	*v = (uint64_t)r.v;
	return !E.bad;
}
