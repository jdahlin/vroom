/* vcc front end: literals: numbers, characters, strings */
#include "cc-internal.h"

/* ---------------- literals ---------------- */

/* the type of an integer literal (C11 6.4.4.1p5) */
static inline ast_qualified_type cc_literal_integer_type(cc *c, uint64_t v, int l, bool u, bool dec)
{
	/* long: 64 bits, or 32 under ILP32 and LLP64 */
	uint64_t lmax = CC_LONG32(c) ? INT32_MAX : INT64_MAX, ulmax = CC_LONG32(c) ? UINT32_MAX : UINT64_MAX;

	if (l == 0 && !u && v <= INT32_MAX)
		return cc_type_basic(AST_TYPE_INT);
	if (l == 0 && (u || !dec) && v <= UINT32_MAX)
		return cc_type_basic(AST_TYPE_UNSIGNED_INT);
	if (l <= 1 && !u && v <= lmax)
		return cc_type_basic(AST_TYPE_LONG);
	if (l <= 1 && (u || !dec) && v <= ulmax)
		return cc_type_basic(AST_TYPE_UNSIGNED_LONG);
	if (!u && v <= INT64_MAX)
		return cc_type_basic(AST_TYPE_LONG_LONG);
	return l <= 1 && !CC_LONG32(c) ? cc_type_basic(AST_TYPE_UNSIGNED_LONG) : cc_type_basic(AST_TYPE_UNSIGNED_LONG_LONG);
}

static uint32_t cc_literal_number_parse_text(cc *c, const ptok *t, const char *s, size_t n);

/* a constant; the GNU suffix i (or j) makes it imaginary: 0 + v i */
static __attribute__((noinline)) uint32_t cc_literal_number_parse_slow(cc *c, const ptok *t)
{
	char tmp[t->len + 1];
	size_t n;
	const char *s = pp_token_spell(t, tmp, &n);

	for (size_t k = n; k > 0 && strchr("fFlLuUiIjJ", s[k - 1]); k--) {
		if (strchr("iIjJ", s[k - 1])) {
			char b[n];
			uint32_t v;

			memcpy(b, s, k - 1);
			memcpy(b + k - 1, s + k, n - k);
			v = cc_literal_number_parse_text(c, t, b, n - 1);
			return cc_complex_node_new(c, cc_type_complex(c, AST_NODE(v)->ty), cc_lowering_zero(c, AST_NODE(v)->ty, t->loc), v, t->loc);
		}
	}
	return cc_literal_number_parse_text(c, t, s, n);
}

/* -Wliteral-range: a constant that rounds to infinity, or to zero although a digit is not 0 */
static __attribute__((noinline, cold)) void cc_literal_range_warn(cc *c, const ptok *t, const char *s, size_t k, ast_qualified_type ty, bool big)
{
	bool hex = k > 1 && s[0] == '0' && (s[1] | 32) == 'x';
	int kd = AST_TYPE_KIND(c, ty);
	bool q = cc_type_is_float128(c, ty);
	const char *lim;
	diag_record *d;

	if (!big) {
		size_t i = hex ? 2 : 0;
		bool nz = false;

		for (; i < k && (hex ? (s[i] | 32) != 'p' : (s[i] | 32) != 'e'); i++)
			nz |= (s[i] >= '1' && s[i] <= '9') || (hex && (s[i] | 32) >= 'a' && (s[i] | 32) <= 'f');
		if (!nz)
			return;
	}
	if (kd == AST_TYPE_FLOAT16)
		lim = big ? "6.5504E+4" : "5.96046E-8";
	else if (kd == AST_TYPE_FLOAT)
		lim = big ? "3.40282347E+38" : "1.40129846E-45";
	else if (q)
		lim = big ? "1.18973149535723176508575932662800702E+4932" : "6.47517511943802511092443895822764655E-4966";
	else
		lim = big ? "1.7976931348623157E+308" : "4.9406564584124654E-324";
	d = cc_report_warning_begin(c, "literal-range", true, NULL, t->loc, "magnitude of floating-point constant too %s for type '%s'; %s is %s",
			  big ? "large" : "small", tyname(c, ty), big ? "maximum" : "minimum", lim);
	if (!d)
		return;
	diag_record_label_add(c->p, d, t->loc, pp_token_end_find(c->p, t->loc), NULL);
	if (kd == AST_TYPE_LONG_DOUBLE && !q) {	/* a constant binary128 holds: written for Arm Linux */
		u128 v = vsoftfloat_parse(s, k);
		unsigned ex = (unsigned)(v >> 112) & 0x7fff;

		if (ex != 0x7fff && v << 1)
			diag_record_help_set(c->p, d, "'long double' is 'double' on Apple; on Arm Linux it has 128 bits, %s",
				     big ? "up to 1.19E+4932" : "down to 6.48E-4966");
	}
	diag_record_emit(c->p, d);
}

static uint32_t cc_literal_number_parse_text(cc *c, const ptok *t, const char *s, size_t n)
{
	size_t i = 0;
	unsigned base = 10;
	u128 v = 0;
	bool over = false, over128 = false, u = false, isf = false, wb = false;
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
		ast_qualified_type ty = cc_type_basic(AST_TYPE_DOUBLE);
		double d;

		if (k >= 3 && (s[k - 3] | 32) == 'f' && s[k - 2] == '1' && s[k - 1] == '6') {
			uint16_t h = vsoftfloat_to_half(vsoftfloat_parse(s, k - 3));

			ty = cc_type_basic(AST_TYPE_FLOAT16);
			if ((h & 0x7fff) == 0x7c00 || !(h & 0x7fff))
				cc_literal_range_warn(c, t, s, k - 3, ty, (h & 0x7fff) == 0x7c00);
			return cc_node_float_new(c, h_to_d(h), ty, t->loc);
		}
		while (k > 0 && (s[k - 1] == 'f' || s[k - 1] == 'F' || s[k - 1] == 'l' || s[k - 1] == 'L' ||
				 s[k - 1] == 'q' || s[k - 1] == 'Q'))
			k--;
		if (k < n && (s[k] == 'f' || s[k] == 'F'))
			ty = cc_type_basic(AST_TYPE_FLOAT);
		else if (k < n && (s[k] == 'q' || s[k] == 'Q'))	/* __float128 (x86-64; binary128 long double elsewhere) */
			ty = cc_type_basic(CC_X86_64(c) ? AST_TYPE_FLOAT128 : AST_TYPE_LONG_DOUBLE);
		else if (k < n)
			ty = cc_type_basic(AST_TYPE_LONG_DOUBLE);
		if (k >= sizeof(buf))
			k = sizeof(buf) - 1;
		if (cc_type_is_float128(c, ty)) {
			u128 q = vsoftfloat_parse_with_precision(s, k, cc_float_precision_bits(c, ty));
			unsigned ex = (unsigned)(q >> 112) & 0x7fff;

			if (unlikely(ex == 0x7fff || !(q << 1)))
				cc_literal_range_warn(c, t, s, k, ty, ex == 0x7fff);
			return cc_node_float128_new(c, q, ty, t->loc);
		}
		memcpy(buf, s, k);
		buf[k] = 0;
		d = AST_TYPE_KIND(c, ty) == AST_TYPE_FLOAT ? (double)strtof(buf, NULL) : strtod(buf, NULL);	/* one rounding */
		if (unlikely(isinf(d) || d == 0))
			cc_literal_range_warn(c, t, s, k, ty, isinf(d));
		return cc_node_float_new(c, d, ty, t->loc);
	}
	if (n > 1 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X'))
		base = 16, i = 2;
	else if (n > 1 && s[0] == '0' && (s[1] == 'b' || s[1] == 'B')) {
		base = 2, i = 2;
		if (c->o->std != 23)
			CC_REPORT_EXTENSION(c, "c23-extensions", cc_warning_group_pedantic, t->loc, "binary integer literals are a C23 extension");
	} else if (s[0] == '0')
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
			cc_report_error(c, t->loc, "invalid digit '%c' in %s constant", ch, base == 8 ? "octal" : "binary");
			break;
		}
		if (v > (UINT64_MAX - d) / base)
			over = true;
		if (v > (~(u128)0 - d) / base)
			over128 = true;
		v = v * base + d;
	}
	size_t suffix = i;
	for (; i < n; i++) {
		char ch = s[i];

		if ((ch == 'w' && i + 1 < n && s[i + 1] == 'b') ||
		    (ch == 'W' && i + 1 < n && s[i + 1] == 'B')) {
			if (wb || l)
				cc_report_error(c, t->loc, "invalid suffix on bit-precise integer constant");
			wb = true;
			i++;
		} else if (ch == 'u' || ch == 'U')
			u = true;
		else if (ch == 'l' || ch == 'L')
			l++;
		else {
			cc_report_error(c, t->loc, "invalid suffix '%.*s' on integer constant", (int)(n - i), s + i);
			break;
		}
	}
	if (wb) {
		if (n - suffix != (u ? 3u : 2u))
			cc_report_error(c, t->loc, "invalid suffix on bit-precise integer constant");
		unsigned bits = v >> 64 ? 128 - (unsigned)__builtin_clzll((uint64_t)(v >> 64)) :
			v ? 64 - (unsigned)__builtin_clzll((uint64_t)v) : 1;

		bits += !u;
		if (bits > 128 || over128)
			return cc_bitint_literal_parse(c, s, suffix, base, u, t->loc);
		return cc_node_integer_new(c, (i128)v, cc_type_bitint(c, bits, u), t->loc);
	}
	if (over)
		cc_report_error(c, t->loc, "integer literal is too large to be represented in any integer type");
	{
		uint64_t magnitude = v > UINT64_MAX ? UINT64_MAX : (uint64_t)v;
		uint32_t r = cc_node_integer_new(c, (i128)v, cc_literal_integer_type(c, magnitude, l, u, base == 10), t->loc);

		AST_NODE(r)->aux = base != 10 ? AST_NUMBER_RADIX : 0;	/* spelled with a leading 0 (-Wconstant-conversion) */
		return r;
	}
}

/* the common case: a clean decimal or hex integer with an optional u/l suffix */
CC_PRIVATE uint32_t cc_literal_number_parse(cc *c, const ptok *t)
{
	const unsigned char *s = (const unsigned char *)t->s, *e = s + t->len;
	uint64_t v = 0;
	bool dec = true, u = false;
	int l = 0;

	if (unlikely(t->flags & LEX_TOKEN_FLAG_SPLICED))
		return cc_literal_number_parse_slow(c, t);
	if (s[0] == '0' && e - s > 1) {
		if ((s[1] | 32) != 'x' || e - s > 18)
			return cc_literal_number_parse_slow(c, t);	/* octal, binary, 0.5, long hex */
		dec = false;
		s += 2;
		if (s == e)
			return cc_literal_number_parse_slow(c, t);
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
			return cc_literal_number_parse_slow(c, t);
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
			return cc_literal_number_parse_slow(c, t);	/* float, separators, odd suffixes */
	}
	{
		ast_qualified_type ty = !l && !u && v <= INT32_MAX ? cc_type_basic(AST_TYPE_INT) : cc_literal_integer_type(c, v, l, u, dec);
		uint32_t r = cc_node_new(c, AST_NODE_NUMBER, ty, t->loc);
		AST_NODE(r)->a = c->nipool;
		c->ipool[c->nipool++] = v;

		AST_NODE(r)->aux = dec ? 0 : AST_NUMBER_RADIX;	/* spelled with a leading 0 (-Wconstant-conversion) */
		return r;
	}
}

/* one (possibly multi-byte UTF-8) escape or character of a literal */
static uint32_t cc_literal_character_parse(cc *c, const char **ps, const char *e, const ptok *t, int kind, bool *wide_ok)
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
	case 'x': {
		uint32_t limit = kind == LEX_TOKEN_UTF16_CHARACTER || kind == LEX_TOKEN_UTF16_STRING ? 0xffffu :
			(kind == LEX_TOKEN_WIDE_CHARACTER || kind == LEX_TOKEN_WIDE_STRING) ? (c->o->short_wchar ? 0xffffu : UINT32_MAX) :
			(kind == LEX_TOKEN_UTF32_CHARACTER || kind == LEX_TOKEN_UTF32_STRING) ? UINT32_MAX : 0xffu;
		bool overflow = false;

		s++;
		v = 0;
		while ((const char *)s < e && ((*s >= '0' && *s <= '9') || ((*s | 32) >= 'a' && (*s | 32) <= 'f'))) {
			uint32_t digit = (uint32_t)(*s <= '9' ? *s - '0' : (*s | 32) - 'a' + 10);

			overflow |= v > (limit - digit) / 16;
			v = v * 16 + digit;
			s++;
		}
		if (overflow) {
			uint32_t loc = t->loc;

			if (!(t->flags & (LEX_TOKEN_FLAG_SPLICED | PP_TOKEN_FLAG_FROM_MACRO)))
				loc += (uint32_t)(*ps - (e + 1 - t->len)) +
				       (t->kind == LEX_TOKEN_UTF8_CHARACTER && t->s[0] == '\'' ? 2 : 0);
			cc_report_error(c, loc, "hex escape sequence out of range");
		}
		break;
	}
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
			uint32_t loc = t->loc;

			/* Spliced or expanded spelling has no direct byte offset in the source. */
			if (!(t->flags & (LEX_TOKEN_FLAG_SPLICED | PP_TOKEN_FLAG_FROM_MACRO)))
				loc += (uint32_t)(*ps - t->s);
			cc_report_warning(c, "unknown-escape-sequence", true, NULL, loc, NULL, 0, NULL, 0,
				 "unknown escape sequence '\\%c'", *s);
			v = *s++;
		}
	}
	*ps = (const char *)s;
	return v;
}

static int cc_literal_element_size(int kind, int target)
{
	switch (kind) {
	case LEX_TOKEN_WIDE_STRING: case LEX_TOKEN_WIDE_CHARACTER: return target ? 2 : 4;
	case LEX_TOKEN_UTF16_STRING: case LEX_TOKEN_UTF16_CHARACTER: return 2;
	case LEX_TOKEN_UTF32_STRING: case LEX_TOKEN_UTF32_CHARACTER: return 4;
	default: (void)target; return 1;
	}
}

static size_t cc_literal_utf8_encode(uint32_t cp, char *o)
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

CC_PRIVATE bool cc_token_kind_is_string(int k) { return k >= LEX_TOKEN_STRING && k <= LEX_TOKEN_UTF32_STRING; }

/* adjacent string literals -> one AST_NODE_STRING of type char[AST_NODE] (or wide) */
CC_PRIVATE uint32_t cc_literal_string_parse(cc *c)
{
	uint32_t loc = tok(c)->loc;
	int kind = LEX_TOKEN_STRING;
	size_t n = 0, count = 0, cap = 8;
	ptok local[8], *tokens = local;

	/* The final prefix applies to every token, even a narrow escape before
	 * the prefixed token. Save the complete run before decoding; pp's
	 * temporary spellings must survive advancing beyond its token window. */
	pp_temporary_hold(c->p, 1);
	while (cc_token_kind_is_string(tok(c)->kind)) {
		ptok *t = tok(c);
		if (count == cap) {
			cap *= 2;
			ptok *more = malloc(cap * sizeof(*more));
			if (!more)
				cc_report_fatal(c, loc, "out of memory");
			memcpy(more, tokens, count * sizeof(*more));
			if (tokens != local)
				free(tokens);
			tokens = more;
		}
		tokens[count++] = *t;
		if (t->kind != LEX_TOKEN_STRING) {
			if (kind != LEX_TOKEN_STRING && kind != t->kind)
				cc_report_error(c, t->loc, "unsupported non-standard concatenation of string literals");
			else
				kind = t->kind;
		}
		next(c);
	}
	int esz = cc_literal_element_size(kind, c->o->short_wchar);
	char *buf = c->hp; /* decoding does not fetch tokens or allocate AST data */
	for (size_t i = 0; i < count; i++) {
		const ptok *t = &tokens[i];
		char *tmp = NULL;
		size_t sl = t->len;
		const char *s = t->s, *e;

		if (unlikely(t->flags & LEX_TOKEN_FLAG_SPLICED))
			s = pp_token_spell(t, tmp = malloc(t->len + 1), &sl);
		e = s + sl - 1;
		/* each source byte gives at most 4 bytes */
		if (unlikely(buf + n + sl * 4 + 16 > c->hend)) {
			cc_report_fatal(c, t->loc, "translation unit too large (front-end heap full)");
		}

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
			v = cc_literal_character_parse(c, &s, e, t, kind, &ok);
			if (esz == 1) {
				if (!ok && (v & 0x80000000u))
					n += cc_literal_utf8_encode(v & 0x7fffffffu, buf + n);
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
				if (!ok)
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
	}
	pp_temporary_hold(c->p, 0);
	if (tokens != local)
		free(tokens);
	memset(buf + n, 0, (size_t)esz);
	n += (size_t)esz;
	{
		char *keep = cc_heap_allocate(c, n);
		uint32_t si = c->nstr++;
		ast_qualified_type et;

		c->strs[si] = (cstr){ keep, (uint32_t)(n / (size_t)esz), (uint8_t)esz, (uint8_t)kind };
		et = esz == 1 ? cc_type_basic(kind == LEX_TOKEN_UTF8_STRING && cc_keyword_map == cc_keyword_map_c23 ? AST_TYPE_UNSIGNED_CHAR : AST_TYPE_CHAR) : kind == LEX_TOKEN_WIDE_STRING ? c->t_wchar : kind == LEX_TOKEN_UTF16_STRING ? c->t_char16 : c->t_char32;
		{
			uint32_t r = cc_node_new(c, AST_NODE_STRING, cc_type_array(c, et, n / (size_t)esz, 0), loc);

			c->nodes[r].a = si;
			c->nodes[r].flags = AST_NODE_FLAG_LVALUE;
			return r;
		}
	}
}

CC_PRIVATE uint32_t cc_literal_character_build(cc *c)
{
	ptok *t = tok(c);
	char tmp[t->len + 1];
	size_t sl;
	const char *s = pp_token_spell(t, tmp, &sl), *e = s + sl - 1;
	int kind = t->kind;
	i128 v = 0;
	int nch = 0;
	ast_qualified_type ty = cc_type_basic(AST_TYPE_INT);
	uint32_t loc;

	while (*s != '\'')
		s++;
	s++;
	while (s < e) {
		bool ok;
		uint32_t x = cc_literal_character_parse(c, &s, e, t, kind, &ok);

		if (kind == LEX_TOKEN_CHARACTER) {
			if (!ok && (x & 0x80000000u)) {
				char u[4];
				size_t k = cc_literal_utf8_encode(x & 0x7fffffffu, u);

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
			v = ok ? x : x & 0x7fffffffu;
			nch++;
		}
	}
	if (unlikely(nch > 1)) {
		if (kind == LEX_TOKEN_CHARACTER) {
			/* clang: 4 characters are -Wfour-char-constants (off); more is too long as well */
			if (nch != 4)
				cc_report_warning(c, "multichar", true, NULL, t->loc, NULL, 0, NULL, 0, "multi-character character constant");
			if (nch > 4)
				report(c, "warning", t->loc, NULL, 0, NULL, 0, "character constant too long for its type");
		} else if (kind == LEX_TOKEN_WIDE_CHARACTER) {
			cc_report_error(c, t->loc, "wide character literals may not contain multiple characters");
		} else {
			cc_report_error(c, t->loc, "Unicode character literals may not contain multiple characters");
		}
	}
	if (kind == LEX_TOKEN_CHARACTER) {
		if (nch == 1)
			v = c->o->unsigned_char ? (uint8_t)v : (int8_t)v;
		else
			v = (int32_t)(uint32_t)v;
	} else if (kind == LEX_TOKEN_WIDE_CHARACTER) {
		ty = c->t_wchar;
		if (c->o->short_wchar)
			v = (uint16_t)v;
	} else if (kind == LEX_TOKEN_UTF16_CHARACTER) {
		ty = c->t_char16;
		v = (uint16_t)v;
	} else if (kind == LEX_TOKEN_UTF32_CHARACTER) {
		ty = c->t_char32;
	} else if (kind == LEX_TOKEN_UTF8_CHARACTER) {
		ty = c->o->unsigned_char ? cc_type_basic(AST_TYPE_UNSIGNED_CHAR) : cc_type_basic(AST_TYPE_CHAR);
		ty = cc_type_basic(AST_TYPE_UNSIGNED_CHAR);
	}
	loc = t->loc;	/* (t is the next token after next()) */
	next(c);
	return cc_node_integer_new(c, v, ty, loc);
}
