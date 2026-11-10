/* vcc front end: literals: numbers, characters, strings */
#include "ccimpl.h"

/* ---------------- literals ---------------- */

/* the type of an integer literal (C11 6.4.4.1p5) */
static inline qty int_lit_type(cc *c, u128 v, int l, bool u, bool dec)
{
	/* long: 64 bits, or 32 under ILP32 */
	u128 lmax = CC_ILP32(c) ? INT32_MAX : INT64_MAX, ulmax = CC_ILP32(c) ? UINT32_MAX : UINT64_MAX;

	if (l == 0 && !u && v <= INT32_MAX)
		return prim(TY_INT);
	if (l == 0 && (u || !dec) && v <= UINT32_MAX)
		return prim(TY_UINT);
	if (l <= 1 && !u && v <= lmax)
		return prim(TY_LONG);
	if (l <= 1 && (u || !dec) && v <= ulmax)
		return prim(TY_ULONG);
	if (!u && v <= INT64_MAX)
		return prim(TY_LLONG);
	return l <= 1 && !CC_ILP32(c) ? prim(TY_ULONG) : prim(TY_ULLONG);
}

static uint32_t number_s(cc *c, const ptok *t, const char *s, size_t n);

/* a constant; the GNU suffix i (or j) makes it imaginary: 0 + v i */
static __attribute__((noinline)) uint32_t number_slow(cc *c, const ptok *t)
{
	char tmp[t->len + 1];
	size_t n;
	const char *s = pp_spell(t, tmp, &n);

	for (size_t k = n; k > 0 && strchr("fFlLuUiIjJ", s[k - 1]); k--) {
		if (strchr("iIjJ", s[k - 1])) {
			char b[n];
			uint32_t v;

			memcpy(b, s, k - 1);
			memcpy(b + k - 1, s + k, n - k);
			v = number_s(c, t, b, n - 1);
			return mk_cplx(c, ty_complex(c, N(v)->ty), zero_of(c, N(v)->ty, t->loc), v, t->loc);
		}
	}
	return number_s(c, t, s, n);
}

static uint32_t number_s(cc *c, const ptok *t, const char *s, size_t n)
{
	size_t i = 0;
	unsigned base = 10;
	u128 v = 0;
	bool over = false, u = false, isf = false;
	int l = 0;
	char buf[256];

	/* floating? */
	if (n > 1 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
		for (size_t k = 2; k < n; k++)
			if (s[k] == '.' || s[k] == 'p' || s[k] == 'P')
				isf = true;
	} else {
		for (size_t k = 0; k < n; k++)
			if (s[k] == '.' || s[k] == 'e' || s[k] == 'E')
				isf = true;
	}
	if (isf) {
		size_t k = n;
		qty ty = prim(TY_DOUBLE);
		double d;

		while (k > 0 && (s[k - 1] == 'f' || s[k - 1] == 'F' || s[k - 1] == 'l' || s[k - 1] == 'L'))
			k--;
		if (k < n && (s[k] == 'f' || s[k] == 'F'))
			ty = prim(TY_FLOAT);
		else if (k < n)
			ty = prim(TY_LDOUBLE);
		if (k >= sizeof(buf))
			k = sizeof(buf) - 1;
		if (is_q(c, ty))
			return mk_fltq(c, q_parse(s, k), ty, t->loc);
		memcpy(buf, s, k);
		buf[k] = 0;
		d = KIND(c, ty) == TY_FLOAT ? (double)strtof(buf, NULL) : strtod(buf, NULL);	/* one rounding */
		return mk_flt(c, d, ty, t->loc);
	}
	if (n > 1 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X'))
		base = 16, i = 2;
	else if (n > 1 && s[0] == '0' && (s[1] == 'b' || s[1] == 'B'))
		base = 2, i = 2;
	else if (s[0] == '0')
		base = 8;
	for (; i < n; i++) {
		unsigned d;
		char ch = s[i];

		if (ch == '\'')
			continue;
		if (ch >= '0' && ch <= '9')
			d = (unsigned)(ch - '0');
		else if (base == 16 && ch >= 'a' && ch <= 'f')
			d = (unsigned)(ch - 'a' + 10);
		else if (base == 16 && ch >= 'A' && ch <= 'F')
			d = (unsigned)(ch - 'A' + 10);
		else
			break;
		if (d >= base) {
			cc_error(c, t->loc, "invalid digit '%c' in %s constant", ch, base == 8 ? "octal" : "binary");
			break;
		}
		if (v > (UINT64_MAX - d) / base)
			over = true;
		v = v * base + d;
	}
	for (; i < n; i++) {
		char ch = s[i];

		if (ch == 'u' || ch == 'U')
			u = true;
		else if (ch == 'l' || ch == 'L')
			l++;
		else {
			cc_error(c, t->loc, "invalid suffix '%.*s' on integer constant", (int)(n - i), s + i);
			break;
		}
	}
	if (over)
		cc_error(c, t->loc, "integer literal is too large to be represented in any integer type");
	return mk_int(c, (i128)v, int_lit_type(c, v, l, u, base == 10), t->loc);
}

/* the common case: a clean decimal or hex integer with an optional u/l suffix */
VPRIV uint32_t number(cc *c, const ptok *t)
{
	const unsigned char *s = (const unsigned char *)t->s, *e = s + t->len;
	uint64_t v = 0;
	bool dec = true, u = false;
	int l = 0;

	if (unlikely(t->flags & TF_DIRTY))
		return number_slow(c, t);
	if (s[0] == '0' && e - s > 1) {
		if ((s[1] | 32) != 'x' || e - s > 18)
			return number_slow(c, t);	/* octal, binary, 0.5, long hex */
		dec = false;
		s += 2;
		if (s == e)
			return number_slow(c, t);
		for (; s < e; s++) {
			unsigned d = *s - '0', h = (*s | 32) - 'a';

			if (d < 10)
				v = v << 4 | d;
			else if (h < 6)
				v = v << 4 | (h + 10);
			else
				break;
		}
	} else {
		if (e - s > 19)
			return number_slow(c, t);
		for (; s < e; s++) {
			unsigned d = *s - '0';

			if (d >= 10)
				break;
			v = v * 10 + d;
		}
	}
	for (; s < e; s++) {
		if ((*s | 32) == 'u' && !u)
			u = true;
		else if ((*s | 32) == 'l' && l < 2)
			l++;
		else
			return number_slow(c, t);	/* float, separators, odd suffixes */
	}
	return mk_int(c, (i128)v, int_lit_type(c, v, l, u, dec), t->loc);
}

/* one (possibly multi-byte UTF-8) escape or character of a literal */
static uint32_t lit_char(cc *c, const char **ps, const char *e, uint32_t loc, bool *wide_ok)
{
	const unsigned char *s = (const unsigned char *)*ps;
	uint32_t v;

	*wide_ok = true;
	if (*s != '\\') {
		if (*s < 0x80) {
			*ps = (const char *)s + 1;
			return *s;
		}
		/* UTF-8: decode (narrow literals store the bytes as they are) */
		*wide_ok = false;
		*ps = (const char *)s + 1;
		return *s;
	}
	s++;
	switch (*s) {
	case 'n': v = '\n'; s++; break;
	case 't': v = '\t'; s++; break;
	case 'r': v = '\r'; s++; break;
	case 'a': v = 7; s++; break;
	case 'b': v = 8; s++; break;
	case 'f': v = 12; s++; break;
	case 'v': v = 11; s++; break;
	case 'e': case 'E': v = 27; s++; break;
	case '\\': case '\'': case '"': case '?': v = *s++; break;
	case 'x':
		s++;
		v = 0;
		while ((const char *)s < e && ((*s >= '0' && *s <= '9') || ((*s | 32) >= 'a' && (*s | 32) <= 'f'))) {
			v = v * 16 + (uint32_t)(*s <= '9' ? *s - '0' : (*s | 32) - 'a' + 10);
			s++;
		}
		break;
	case 'u': case 'U': {
		int nd = *s == 'u' ? 4 : 8;

		s++;
		v = 0;
		for (int k = 0; k < nd && (const char *)s < e; k++, s++)
			v = v * 16 + (uint32_t)(*s <= '9' ? *s - '0' : (*s | 32) - 'a' + 10);
		*wide_ok = false;	/* caller encodes */
		*ps = (const char *)s;
		return v | 0x80000000u;	/* marks a code point */
	}
	default:
		if (*s >= '0' && *s <= '7') {
			v = 0;
			for (int k = 0; k < 3 && *s >= '0' && *s <= '7'; k++, s++)
				v = v * 8 + (uint32_t)(*s - '0');
		} else {
			cc_error(c, loc, "unknown escape sequence '\\%c'", *s);
			v = *s++;
		}
	}
	*ps = (const char *)s;
	return v;
}

static int lit_esz(int kind, int target)
{
	switch (kind) {
	case T_WSTRING: case T_WCHAR: return target ? 2 : 4;
	case T_U16STRING: case T_U16CHAR: return 2;
	case T_U32STRING: case T_U32CHAR: return 4;
	default: (void)target; return 1;
	}
}

static size_t utf8_enc(uint32_t cp, char *o)
{
	if (cp < 0x80) { o[0] = (char)cp; return 1; }
	if (cp < 0x800) { o[0] = (char)(0xC0 | cp >> 6); o[1] = (char)(0x80 | (cp & 63)); return 2; }
	if (cp < 0x10000) {
		o[0] = (char)(0xE0 | cp >> 12); o[1] = (char)(0x80 | ((cp >> 6) & 63)); o[2] = (char)(0x80 | (cp & 63));
		return 3;
	}
	o[0] = (char)(0xF0 | cp >> 18); o[1] = (char)(0x80 | ((cp >> 12) & 63));
	o[2] = (char)(0x80 | ((cp >> 6) & 63)); o[3] = (char)(0x80 | (cp & 63));
	return 4;
}

VPRIV bool is_strtok(int k) { return k >= T_STRING && k <= T_U32STRING; }

/* adjacent string literals -> one N_STR of type char[N] (or wide) */
VPRIV uint32_t string_lit(cc *c)
{
	uint32_t loc = tok(c)->loc;
	int kind = T_STRING;
	size_t n = 0;
	char *buf = c->hp;	/* built in place at the top of the heap (nothing else allocates meanwhile) */
	int esz;

	/* first pass: the literal kind (any prefixed one wins) */
	for (int k = 0;; k++) {
		ptok *t = peek(c, k);

		if (!is_strtok(t->kind))
			break;
		if (t->kind != T_STRING && t->kind != T_U8STRING)
			kind = t->kind;
		if (k >= 2) {	/* the lookahead buffer is small: just stop counting */
			break;
		}
	}
	esz = lit_esz(kind, c->o->short_wchar);
	while (is_strtok(tok(c)->kind)) {
		ptok *t = tok(c);
		char *tmp = NULL;
		size_t sl = t->len, used;
		const char *s = t->s, *e;

		if (unlikely(t->flags & TF_DIRTY))
			s = pp_spell(t, tmp = malloc(t->len + 1), &sl);
		e = s + sl - 1;
		/* each source byte gives at most 4 bytes */
		if (unlikely(buf + n + sl * 4 + 16 > c->hend)) {
			fatal(c, t->loc, "translation unit too large (front-end heap full)");
		}

		if (t->kind != T_STRING && t->kind != T_U8STRING && t->kind != kind && esz != lit_esz(t->kind, c->o->short_wchar))
			cc_error(c, t->loc, "unsupported non-standard concatenation of string literals");
		if (t->kind != T_STRING && t->kind != T_U8STRING && kind == T_STRING)
			kind = t->kind, esz = lit_esz(kind, c->o->short_wchar);
		while (*s != '"')
			s++;
		s++;
		while (s < e) {
			bool ok;
			uint32_t v;

			if (esz == 1) {
				const char *q = memchr(s, '\\', (size_t)(e - s));

				if (!q)
					q = e;
				memcpy(buf + n, s, (size_t)(q - s));
				n += (size_t)(q - s);
				s = q;
				if (s == e)
					break;
			}
			v = lit_char(c, &s, e, t->loc, &ok);
			if (esz == 1) {
				if (v & 0x80000000u)
					n += utf8_enc(v & 0x7fffffffu, buf + n);
				else
					buf[n++] = (char)v;
			} else {
				if (!ok && !(v & 0x80000000u)) {
					/* UTF-8 sequence in a wide literal: decode */
					const unsigned char *u = (const unsigned char *)s - 1;
					uint32_t cp = *u;
					int more = cp >= 0xF0 ? 3 : cp >= 0xE0 ? 2 : cp >= 0xC0 ? 1 : 0;

					cp &= more == 3 ? 7 : more == 2 ? 15 : more == 1 ? 31 : 0xff;
					for (int k = 0; k < more && s < e; k++, s++)
						cp = cp << 6 | ((unsigned char)*s & 63);
					v = cp;
				}
				v &= 0x7fffffffu;
				if (esz == 2 && v >= 0x10000) {
					uint16_t hi = (uint16_t)(0xD800 + ((v - 0x10000) >> 10)), lo = (uint16_t)(0xDC00 + ((v - 0x10000) & 0x3ff));

					memcpy(buf + n, &hi, 2), n += 2;
					memcpy(buf + n, &lo, 2), n += 2;
				} else if (esz == 2) {
					uint16_t w = (uint16_t)v;

					memcpy(buf + n, &w, 2), n += 2;
				} else {
					memcpy(buf + n, &v, 4), n += 4;
				}
			}
		}
		free(tmp);
		/* own the bytes while the next token is fetched */
		used = (n + 15) & ~(size_t)15;
		c->hp = buf + used;
		next(c);
		if (unlikely(c->hp != buf + used)) {
			char *nb = halloc(c, used);

			memcpy(nb, buf, n);
			buf = nb;
		}
		c->hp = buf;
	}
	memset(buf + n, 0, (size_t)esz);
	n += (size_t)esz;
	{
		char *keep = halloc(c, n);
		uint32_t si = c->nstr++;
		qty et;

		c->strs[si] = (cstr){ keep, (uint32_t)(n / (size_t)esz), (uint8_t)esz, (uint8_t)kind };
		et = esz == 1 ? prim(TY_CHAR) : kind == T_WSTRING ? c->t_wchar : kind == T_U16STRING ? c->t_char16 : c->t_char32;
		{
			uint32_t r = mk(c, N_STR, ty_array(c, et, n / (size_t)esz, 0), loc);

			c->nodes[r].a = si;
			c->nodes[r].flags = NF_LVALUE;
			return r;
		}
	}
}

VPRIV uint32_t char_lit(cc *c)
{
	ptok *t = tok(c);
	char tmp[t->len + 1];
	size_t sl;
	const char *s = pp_spell(t, tmp, &sl), *e = s + sl - 1;
	int kind = t->kind;
	i128 v = 0;
	int nch = 0;
	qty ty = prim(TY_INT);

	while (*s != '\'')
		s++;
	s++;
	while (s < e) {
		bool ok;
		uint32_t x = lit_char(c, &s, e, t->loc, &ok);

		if (kind == T_CHAR) {
			if (x & 0x80000000u) {
				char u[4];
				size_t k = utf8_enc(x & 0x7fffffffu, u);

				for (size_t j = 0; j < k; j++)
					v = (v << 8) | (unsigned char)u[j], nch++;
				continue;
			}
			v = (v << 8) | (x & 0xff);
			nch++;
		} else {
			if (!ok && !(x & 0x80000000u)) {
				const unsigned char *u = (const unsigned char *)s - 1;
				uint32_t cp = *u;
				int more = cp >= 0xF0 ? 3 : cp >= 0xE0 ? 2 : cp >= 0xC0 ? 1 : 0;

				cp &= more == 3 ? 7 : more == 2 ? 15 : more == 1 ? 31 : 0xff;
				for (int k = 0; k < more && s < e; k++, s++)
					cp = cp << 6 | ((unsigned char)*s & 63);
				x = cp;
			}
			v = x & 0x7fffffffu;
			nch++;
		}
	}
	if (unlikely(nch > 1)) {
		if (kind == T_CHAR) {
			/* clang: 4 characters are -Wfour-char-constants (off); more is too long as well */
			if (nch != 4)
				cc_warnx(c, "multichar", true, NULL, t->loc, NULL, 0, NULL, 0, "multi-character character constant");
			if (nch > 4)
				report(c, "warning", t->loc, NULL, 0, NULL, 0, "character constant too long for its type");
		} else if (kind == T_WCHAR) {
			cc_error(c, t->loc, "wide character literals may not contain multiple characters");
		} else {
			cc_error(c, t->loc, "Unicode character literals may not contain multiple characters");
		}
	}
	if (kind == T_CHAR) {
		if (nch == 1)
			v = c->o->unsigned_char ? (uint8_t)v : (int8_t)v;
		else
			v = (int32_t)(uint32_t)v;
	} else if (kind == T_WCHAR) {
		ty = c->t_wchar;
		if (c->o->short_wchar)
			v = (uint16_t)v;
	} else if (kind == T_U16CHAR) {
		ty = c->t_char16;
		v = (uint16_t)v;
	} else if (kind == T_U32CHAR) {
		ty = c->t_char32;
	} else if (kind == T_U8CHAR) {
		ty = c->o->unsigned_char ? prim(TY_UCHAR) : prim(TY_CHAR);
		ty = prim(TY_UCHAR);
	}
	next(c);
	return mk_int(c, v, ty, t->loc);
}
