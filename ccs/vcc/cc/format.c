/* vcc front end: printf and scanf format strings, clang's -Wformat family.
 *
 * A call to a function with __attribute__((format(printf|scanf, i, j))), or to one of the
 * C library's (clang's Builtins.inc marks them; not under -fno-builtin), has its format
 * argument checked when it is a string literal (also a const variable initialized with one,
 * both arms of a ?: whose condition is not constant):
 *   - each conversion specification parsed as clang's PrintfFormatString/ScanfFormatString;
 *   - the data arguments' types against what each conversion reads (clang's matchesType:
 *     signedness and integer promotion are forgiven, long vs long long is not), with a
 *     fix-it to the specification that fits the argument;
 *   - too few or too many data arguments, '*' widths and precisions, positional arguments;
 *   - undefined flags ('#', '0', '+', ' ' where the conversion does not take them).
 * A format that is not a literal, with no data arguments, is -Wformat-security.
 * Each warning points into the literal: its bytes are mapped back to their source columns
 * by decoding the literal's tokens again (escapes included); when that fails (a macro, an
 * odd spelling) every byte maps to the literal's location.
 */
#include "ccimpl.h"

enum { FK_PRINTF = 1, FK_SCANF = 2 };

/* the C library's format functions: format argument, first data argument (0: a va_list) */
static const struct libfmt {
	const char *name;
	uint8_t kind, idx, first;
} libfmts[] = {
	{ "printf", FK_PRINTF, 1, 2 },	 { "fprintf", FK_PRINTF, 2, 3 },  { "sprintf", FK_PRINTF, 2, 3 },
	{ "snprintf", FK_PRINTF, 3, 4 }, { "vprintf", FK_PRINTF, 1, 0 },  { "vfprintf", FK_PRINTF, 2, 0 },
	{ "vsprintf", FK_PRINTF, 2, 0 }, { "vsnprintf", FK_PRINTF, 3, 0 }, { "scanf", FK_SCANF, 1, 2 },
	{ "fscanf", FK_SCANF, 2, 3 },	 { "sscanf", FK_SCANF, 2, 3 },	   { "vscanf", FK_SCANF, 1, 0 },
	{ "vfscanf", FK_SCANF, 2, 0 },	 { "vsscanf", FK_SCANF, 2, 0 },
};

VPRIV uint16_t fmt_pack(int kind, unsigned idx, unsigned first)
{
	if (idx > 127 || first > 127)
		return 0;
	return (uint16_t)(kind << 14 | idx << 7 | first);
}

/* the format info of a call to sym s: an attribute on any of its declarations, else the library's */
static uint16_t fmt_of(cc *c, uint32_t s)
{
	const sym *y = &c->syms[s];
	int nl;
	const char *nm;

	for (uint32_t t = s; t; t = c->syms[t].next)
		if (c->syms[t].fmt)
			return c->syms[t].fmt;
	if (c->o->no_builtin || !(y->flags & SF_GLOBAL))
		return 0;
	nm = idname(y->name, &nl);
	if (nl > 10 && !memcmp(nm, "__builtin_", 10))
		nm += 10, nl -= 10;
	if (nl < 5 || nl > 9 || (nm[0] != 'p' && nm[0] != 'f' && nm[0] != 's' && nm[0] != 'v'))
		return 0;
	for (size_t i = 0; i < sizeof libfmts / sizeof *libfmts; i++)
		if ((int)strlen(libfmts[i].name) == nl && !memcmp(libfmts[i].name, nm, (size_t)nl))
			return fmt_pack(libfmts[i].kind, libfmts[i].idx, libfmts[i].first);
	return 0;
}

static uint32_t fstrip(cc *c, uint32_t n)
{
	while (N(n)->op == N_CAST && !c->span[n].e)
		n = N(n)->a;
	return n;
}

/* ---------------- the literal's bytes and their source locations ---------------- */

typedef struct fstr {
	const char *s;	/* the bytes */
	uint32_t n;	/* without the terminator */
	uint32_t *loc;	/* loc[i]: byte i's location, loc[n]: the closing quote's; NULL: all at at */
	uint32_t at;	/* the literal's location */
	pp_span r;	/* the literal's range */
} fstr;

static uint32_t floc(const fstr *f, uint32_t i)
{
	return f->loc ? f->loc[i <= f->n ? i : f->n] : f->at;
}

static int hexv(int ch)
{
	return ch >= '0' && ch <= '9' ? ch - '0' : ch >= 'a' && ch <= 'f' ? ch - 'a' + 10 : ch >= 'A' && ch <= 'F' ? ch - 'A' + 10 : -1;
}

/* decode the literal's tokens from its source again; false if they do not spell f->s */
static bool fmap(cc *c, fstr *f)
{
	size_t len;
	const char *src = pp_src_rest(c->p, f->at, &len);
	uint32_t i = 0, k = 0;

	if (!src)
		return false;
	f->loc = malloc(((size_t)f->n + 1) * sizeof(uint32_t));
	while (i < f->n) {
		while (k < len && (src[k] == ' ' || src[k] == '\t' || src[k] == '\n' || src[k] == '\r'))
			k++;
		if (k >= len || src[k] != '"')
			goto fail;
		k++;
		while (i < f->n) {
			uint32_t at = k;
			int v;

			if (k >= len || src[k] == '\n')
				goto fail;
			if (src[k] == '"')
				break;
			if (src[k] != '\\') {
				v = (unsigned char)src[k++];
			} else {
				k++;
				if (k >= len)
					goto fail;
				switch (src[k]) {
				case 'n': v = '\n', k++; break;
				case 't': v = '\t', k++; break;
				case 'r': v = '\r', k++; break;
				case 'a': v = '\a', k++; break;
				case 'b': v = '\b', k++; break;
				case 'f': v = '\f', k++; break;
				case 'v': v = '\v', k++; break;
				case 'e': v = 27, k++; break;
				case 'x':
					k++, v = 0;
					while (k < len && hexv(src[k]) >= 0)
						v = (v * 16 + hexv(src[k++])) & 0xff;
					break;
				default:
					if (src[k] >= '0' && src[k] <= '7') {
						v = 0;
						for (int d = 0; d < 3 && k < len && src[k] >= '0' && src[k] <= '7'; d++)
							v = v * 8 + (src[k++] - '0');
						v &= 0xff;
					} else {
						v = (unsigned char)src[k++];	/* \\ \" \' \? */
					}
				}
			}
			if ((unsigned char)f->s[i] != v)
				goto fail;
			f->loc[i++] = f->at + at;
		}
		if (i < f->n) {
			if (k >= len || src[k] != '"')
				goto fail;
			k++;	/* the next literal of a concatenation */
		}
	}
	while (k < len && src[k] != '"')	/* the closing quote */
		k++;
	f->loc[f->n] = f->at + (uint32_t)k;
	return true;
fail:
	free(f->loc);
	f->loc = NULL;
	return false;
}

/* ---------------- what a conversion reads ---------------- */

enum { W_NONE, W_INT, W_FLT, W_CSTR, W_VPTR, W_WSTR, W_PINT, W_PFLT, W_PVPTR };

typedef struct want {
	uint8_t cls;	/* W_* */
	uint8_t kind;	/* TY_* of the integer or floating type */
	const char *name;	/* clang's spelling, quotes included */
} want;

static int norm_int(int k)
{
	switch (k) {
	case TY_CHAR: case TY_UCHAR: return TY_SCHAR;
	case TY_USHORT: return TY_SHORT;
	case TY_UINT: return TY_INT;
	case TY_ULONG: return TY_LONG;
	case TY_ULLONG: return TY_LLONG;
	case TY_UINT128: return TY_INT128;
	default: return k;
	}
}

static int int_kind(cc *c, qty t)
{
	int k = KIND(c, t);

	if (k == TY_ENUM)
		k = KIND(c, T(c, t)->base);
	return k;
}

/* the integer type a length modifier and a conversion read (len: "", "hh", "h", "l", "ll", "j", "z", "t", "L", "q") */
static void int_want(const char *len, bool sgn, bool ptr, want *w, bool linux)
{
	static const char *const sn[] = { "char", "short", "int", "long", "long long" };
	static const char *const un[] = { "unsigned char", "unsigned short", "unsigned int", "unsigned long", "unsigned long long" };
	static const int sk[] = { TY_SCHAR, TY_SHORT, TY_INT, TY_LONG, TY_LLONG };
	static char buf[8][48];
	static int bi;
	int r;
	const char *nm;

	(void)linux;
	if (!strcmp(len, "hh"))
		r = 0;
	else if (!strcmp(len, "h"))
		r = 1;
	else if (!*len)
		r = 2;
	else if (!strcmp(len, "l"))
		r = 3;
	else if (!strcmp(len, "ll") || !strcmp(len, "q") || !strcmp(len, "L"))
		r = 4;
	else {	/* j z t: typedefs of long */
		w->cls = ptr ? W_PINT : W_INT, w->kind = sgn ? TY_LONG : TY_ULONG;
		nm = len[0] == 'j' ? (sgn ? "'intmax_t' (aka 'long')" : "'uintmax_t' (aka 'unsigned long')")
		     : len[0] == 'z' ? (sgn ? "'ssize_t' (aka 'long')" : "'size_t' (aka 'unsigned long')")
				     : "'ptrdiff_t' (aka 'long')";
		if (ptr) {
			char *b = buf[bi++ & 7];

			nm = len[0] == 'j' ? (sgn ? "'intmax_t *' (aka 'long *')" : "'uintmax_t *' (aka 'unsigned long *')")
			     : len[0] == 'z' ? (sgn ? "'ssize_t *' (aka 'long *')" : "'size_t *' (aka 'unsigned long *')")
					     : "'ptrdiff_t *' (aka 'long *')";
			snprintf(b, 48, "%s", nm);
			nm = b;
		}
		w->name = nm;
		return;
	}
	w->cls = ptr ? W_PINT : W_INT;
	w->kind = (uint8_t)(sgn ? sk[r] : norm_int(sk[r]) + 1);
	if (!sgn && r == 0)
		w->kind = TY_UCHAR;
	{
		char *b = buf[bi++ & 7];

		snprintf(b, 48, "'%s%s'", sgn ? sn[r] : un[r], ptr ? " *" : "");
		w->name = b;
	}
}

static bool int_match(cc *c, int want_k, qty a)
{
	int ak;

	if (!is_integer(c, a) && KIND(c, a) != TY_ENUM)
		return false;
	ak = int_kind(c, a);
	if (ak == TY_BOOL)
		ak = TY_SCHAR;
	if (norm_int(ak) == norm_int(want_k))
		return true;
	if (norm_int(want_k) == TY_INT && (norm_int(ak) == TY_SCHAR || norm_int(ak) == TY_SHORT))
		return true;	/* promoted to int */
	if ((norm_int(want_k) == TY_SCHAR || norm_int(want_k) == TY_SHORT) &&
	    (norm_int(ak) == TY_INT || norm_int(ak) == TY_SHORT || norm_int(ak) == TY_SCHAR))
		return true;	/* clang: %hd and %hhd forgive an int */
	return false;
}

static bool is_charlike(cc *c, qty t)
{
	int k = KIND(c, t);

	return k == TY_CHAR || k == TY_SCHAR || k == TY_UCHAR;
}

/* does argument a (node) fit w */
static bool matches(cc *c, const want *w, uint32_t a, bool scanf_)
{
	qty t = N(a)->ty, u = N(fstrip(c, a))->ty, pt;

	switch (w->cls) {
	case W_INT:
		return int_match(c, w->kind, u);
	case W_FLT:
		if (!is_float(c, t))
			return false;
		return w->kind == TY_LDOUBLE ? KIND(c, t) == TY_LDOUBLE : KIND(c, t) != TY_LDOUBLE;
	case W_CSTR:
		return is_ptr(c, t) && is_charlike(c, T(c, t)->base);
	case W_VPTR:
		return is_ptr(c, t);
	case W_WSTR:
		return is_ptr(c, t) && is_integer(c, T(c, t)->base) && norm_int(int_kind(c, T(c, t)->base)) == TY_INT;
	case W_PINT:
		if (!is_ptr(c, t))
			return false;
		pt = T(c, t)->base;
		return (is_integer(c, pt) || KIND(c, pt) == TY_ENUM) && norm_int(int_kind(c, pt)) == norm_int(w->kind);
	case W_PFLT:
		return is_ptr(c, t) && KIND(c, T(c, t)->base) == w->kind;
	case W_PVPTR:
		return is_ptr(c, t) && is_ptr(c, T(c, t)->base);
	default:
		(void)scanf_;
		return true;
	}
}

/* ---------------- one conversion specification ---------------- */

typedef struct spec {
	uint32_t b, e;		/* bytes: '%' .. after the conversion */
	uint32_t conv;		/* the conversion's byte */
	uint32_t lenb, lene;	/* the length modifier's bytes */
	char len[4];
	char cv;
	uint8_t flags[8];	/* the flags in order */
	uint32_t flagat[8];
	int nflags;
	int pos;		/* n$: n, else 0 */
	int wstar, pstar;	/* '*' width / precision: 1, or n for *n$ */
	uint32_t wat, pat;	/* their bytes */
	bool suppress;		/* scanf '*' */
} spec;

typedef struct fctx {
	cc *c;
	fstr *f;
	int kind;
	uint32_t *args;		/* the data arguments */
	uint32_t nargs;
	uint32_t used;		/* data arguments consumed */
	bool positional, plain, stop, va;
} fctx;

static void spec_range(const fctx *x, const spec *s, pp_span *r)
{
	r->b = floc(x->f, s->b);
	r->e = s->cv == '[' ? (s->e < x->f->n ? floc(x->f, s->e) : floc(x->f, x->f->n)) : floc(x->f, s->conv) + 1;
	if (r->e <= r->b)
		r->e = r->b + 1;
}

/* clang's fixType: the specification that reads a's type, or false */
static bool suggest(cc *c, const fstr *f, const spec *s, uint32_t a, int kind, char *out, size_t sz)
{
	qty t = N(fstrip(c, a))->ty, d = N(a)->ty;
	const char *len = NULL;
	char cv = 0;
	size_t k = 0;

	if (kind == FK_SCANF) {	/* a pointer to what a conversion stores */
		qty pt;

		if (!is_ptr(c, d))
			return false;
		pt = T(c, d)->base;
		if ((is_integer(c, pt) || KIND(c, pt) == TY_ENUM) && !is_charlike(c, pt)) {
			int ik = norm_int(int_kind(c, pt));

			len = ik == TY_SHORT ? "h" : ik == TY_LONG ? "l" : ik == TY_LLONG ? "ll" : ik == TY_INT ? "" : NULL;
			cv = strchr("diouxX", s->cv) ? s->cv : is_signed(c, pt) ? 'd' : 'u';
		} else if (is_float(c, pt)) {
			len = KIND(c, pt) == TY_LDOUBLE ? "L" : KIND(c, pt) == TY_DOUBLE ? "l" : "";
			cv = strchr("fFeEgGaA", s->cv) ? s->cv : 'f';
		}
		if (!len)
			return false;
	} else if (is_integer(c, t) || KIND(c, t) == TY_ENUM) {
		int ik = int_kind(c, t);
		bool keep = strchr("diouxX", s->cv) != NULL;

		switch (norm_int(ik)) {
		case TY_SCHAR: len = keep ? "hh" : ""; break;
		case TY_SHORT: len = "h"; break;
		case TY_LONG: len = "l"; break;
		case TY_LLONG: len = "ll"; break;
		case TY_INT128: return false;
		default: len = ""; break;
		}
		if (ik == TY_BOOL)
			len = "";
		cv = keep ? s->cv : ik == TY_CHAR && !keep && s->cv == 's' ? 'c' : is_signed(c, t) || ik == TY_BOOL ? 'd' : 'u';
		if ((cv == 'd' || cv == 'i') && !is_signed(c, t) && ik != TY_BOOL)
			cv = 'u';
		if (cv == 'c')
			len = "";
	} else if (is_float(c, d)) {
		len = KIND(c, d) == TY_LDOUBLE ? "L" : "";
		cv = strchr("fFeEgGaA", s->cv) ? s->cv : 'f';
	} else if (is_ptr(c, d)) {
		len = "";
		cv = is_charlike(c, T(c, d)->base) ? 's' : 'p';
		if (cv == 'p' && s->cv == 's')	/* clang: no fix for a non-char pointer to %s */
			return false;
	} else {
		return false;
	}
	/* '%', flags, width and precision as written, then the new length and conversion */
	for (uint32_t i = s->b; i < s->lenb && k + 1 < sz; i++)
		out[k++] = f->s[i];
	for (const char *p = len; *p && k + 1 < sz; p++)
		out[k++] = *p;
	if (k + 1 < sz)
		out[k++] = cv;
	out[k] = 0;
	return true;
}

static uint32_t argloc(cc *c, uint32_t a)
{
	uint32_t s = fstrip(c, a);

	return N(s)->loc != UINT32_MAX ? N(s)->loc : rng(c, s).b;
}

/* the data argument for position p (1-based), or 0 */
static uint32_t data_arg(fctx *x, int p)
{
	return p >= 1 && (uint32_t)p <= x->nargs ? x->args[p - 1] : 0;
}

static void mismatch(fctx *x, const spec *s, const want *w, uint32_t a)
{
	cc *c = x->c;
	pp_span sr, ar = rng(c, fstrip(c, a));
	char fix[48];
	qty t = N(fstrip(c, a))->ty;
	vdiag *d;

	if (KIND(c, N(a)->ty) == TY_DOUBLE && KIND(c, t) == TY_FLOAT && w->cls != W_FLT)
		t = N(fstrip(c, a))->ty;
	spec_range(x, s, &sr);
	d = cc_warn_begin(c, "format", true, NULL, argloc(c, a), "format specifies type %s but the argument has type '%s'", w->name,
			  tyname(c, is_ptr(c, N(a)->ty) ? N(a)->ty : t));
	if (!d)
		return;
	pp_diag_label(c->p, d, sr.b, sr.e, NULL);
	pp_diag_label(c->p, d, ar.b, ar.e, NULL);
	if (x->f->loc && suggest(c, x->f, s, a, x->kind, fix, sizeof fix))
		pp_diag_fix(c->p, d, sr.b, sr.e, fix, VDA_MAYBE);
	pp_diag_emit(c->p, d);
}

static void warn_at(fctx *x, const char *flag, const spec *s, uint32_t byte, const char *fmt, const char *a1, const char *a2)
{
	pp_span sr;
	vdiag *d;

	spec_range(x, s, &sr);
	d = cc_warn_begin(x->c, flag, true, NULL, floc(x->f, byte), fmt, a1, a2);
	if (!d)
		return;
	pp_diag_label(x->c->p, d, sr.b, sr.e, NULL);
	pp_diag_emit(x->c->p, d);
}

/* a '*' width or precision: its argument must be an int */
static void amount(fctx *x, const spec *s, int star, uint32_t at, bool prec)
{
	cc *c = x->c;
	uint32_t a = x->positional ? data_arg(x, star) : data_arg(x, (int)++x->used);
	pp_span sr, ar;
	vdiag *d;

	if (!a) {
		if (!x->positional)
			warn_at(x, "format-insufficient-args", s, s->conv, "more '%%' conversions than data arguments", NULL, NULL);
		x->stop = true;
		return;
	}
	if (int_match(c, TY_INT, N(fstrip(c, a))->ty))
		return;
	spec_range(x, s, &sr);
	ar = rng(c, fstrip(c, a));
	d = cc_warn_begin(c, "format", true, NULL, floc(x->f, at + 1), "field %s should have type 'int', but argument has type '%s'",
			  prec ? "precision" : "width", tyname(c, N(fstrip(c, a))->ty));
	if (!d)
		return;
	pp_diag_label(c->p, d, sr.b, sr.e, NULL);
	pp_diag_label(c->p, d, ar.b, ar.e, NULL);
	pp_diag_emit(c->p, d);
}

/* what s's conversion reads (W_NONE: no argument) */
static bool conv_want(fctx *x, const spec *s, want *w, bool *bad)
{
	bool sc = x->kind == FK_SCANF, lnx = x->c->o->target != PP_DARWIN;
	const char *wch = lnx ? "'wchar_t *' (aka 'unsigned int *')" : "'wchar_t *' (aka 'int *')";
	const char *wint = lnx ? "'wint_t' (aka 'unsigned int')" : "'wint_t' (aka 'int')";

	*bad = false;
	memset(w, 0, sizeof *w);
	switch (s->cv) {
	case 'd': case 'i':
		int_want(s->len, true, sc, w, lnx);
		return true;
	case 'o': case 'u': case 'x': case 'X':
		int_want(s->len, false, sc, w, lnx);
		return true;
	case 'f': case 'F': case 'e': case 'E': case 'g': case 'G': case 'a': case 'A':
		if (sc) {
			w->cls = W_PFLT;
			w->kind = !strcmp(s->len, "L") ? TY_LDOUBLE : !strcmp(s->len, "l") ? TY_DOUBLE : TY_FLOAT;
			w->name = w->kind == TY_LDOUBLE ? "'long double *'" : w->kind == TY_DOUBLE ? "'double *'" : "'float *'";
		} else {
			w->cls = W_FLT;
			w->kind = !strcmp(s->len, "L") ? TY_LDOUBLE : TY_DOUBLE;
			w->name = w->kind == TY_LDOUBLE ? "'long double'" : "'double'";
		}
		return true;
	case 'c':
		if (sc || !strcmp(s->len, "l")) {
			if (sc)
				w->cls = !strcmp(s->len, "l") ? W_WSTR : W_CSTR, w->name = !strcmp(s->len, "l") ? wch : "'char *'";
			else
				w->cls = W_INT, w->kind = TY_UINT, w->name = wint;
		} else {
			w->cls = W_INT, w->kind = TY_INT, w->name = "'int'";
		}
		return true;
	case 'C':
		if (sc)
			w->cls = W_WSTR, w->name = wch;
		else
			w->cls = W_INT, w->kind = TY_UINT, w->name = wint;
		return true;
	case 's':
		if (!strcmp(s->len, "l"))
			w->cls = W_WSTR, w->name = wch;
		else
			w->cls = W_CSTR, w->name = "'char *'";
		return true;
	case 'S':
		w->cls = W_WSTR, w->name = wch;
		return true;
	case '[':
		if (!sc)
			break;
		w->cls = !strcmp(s->len, "l") ? W_WSTR : W_CSTR, w->name = !strcmp(s->len, "l") ? wch : "'char *'";
		return true;
	case 'p':
		if (sc)
			w->cls = W_PVPTR, w->name = "'void **'";
		else
			w->cls = W_VPTR, w->name = "'void *'";
		return true;
	case 'n':
		int_want(s->len, true, true, w, lnx);
		return true;
	case 'm':
		if (sc)
			break;
		return true;
	case '%':
		return true;
	}
	*bad = true;
	return false;
}

/* clang's flag rules: '#' with o x X a A e E f F g G; '0' with those and d i u; '+' and ' ' with signed conversions */
static void check_flags(fctx *x, const spec *s)
{
	char cv[2] = { s->cv, 0 };

	for (int i = 0; i < s->nflags; i++) {
		char fl[2] = { (char)s->flags[i], 0 };
		const char *ok = fl[0] == '#' ? "oxXaAeEfFgG" : fl[0] == '0' ? "diouxXaAeEfFgG" : fl[0] == '+' || fl[0] == ' ' ? "diaAeEfFgG" : NULL;

		if (ok && !strchr(ok, s->cv) && s->cv != '%')
			warn_at(x, "format", s, s->flagat[i], "flag '%s' results in undefined behavior with '%s' conversion specifier", fl, cv);
	}
}

/* parse the specification at byte i ('%'); false at the end (incomplete) */
static bool parse_spec(fctx *x, uint32_t i, spec *s)
{
	const char *p = x->f->s;
	uint32_t n = x->f->n, k = i + 1;
	bool sc = x->kind == FK_SCANF;

	memset(s, 0, sizeof *s);
	s->b = i;
	/* n$ */
	{
		uint32_t j = k;
		int v = 0;

		while (j < n && p[j] >= '0' && p[j] <= '9')
			v = v * 10 + (p[j++] - '0');
		if (j > k && j < n && p[j] == '$')
			s->pos = v, k = j + 1;
	}
	if (sc) {
		if (k < n && p[k] == '*')
			s->suppress = true, k++;
	} else {
		while (k < n && strchr("-+ #0'", p[k]) && s->nflags < 8)
			s->flagat[s->nflags] = k, s->flags[s->nflags++] = (uint8_t)p[k++];
		while (k < n && strchr("-+ #0'", p[k]))
			k++;
	}
	/* width */
	if (!sc && k < n && p[k] == '*') {
		uint32_t j = k + 1;
		int v = 0;

		s->wat = k, s->wstar = 1;
		while (j < n && p[j] >= '0' && p[j] <= '9')
			v = v * 10 + (p[j++] - '0');
		if (j > k + 1 && j < n && p[j] == '$')
			s->wstar = v, j++;
		else
			j = k + 1;
		k = j;
	} else {
		while (k < n && p[k] >= '0' && p[k] <= '9')
			k++;
	}
	/* precision */
	if (!sc && k < n && p[k] == '.') {
		k++;
		if (k < n && p[k] == '*') {
			uint32_t j = k + 1;
			int v = 0;

			s->pat = k - 1, s->pstar = 1;
			while (j < n && p[j] >= '0' && p[j] <= '9')
				v = v * 10 + (p[j++] - '0');
			if (j > k + 1 && j < n && p[j] == '$')
				s->pstar = v, j++;
			else
				j = k + 1;
			k = j;
		} else {
			while (k < n && p[k] >= '0' && p[k] <= '9')
				k++;
		}
	}
	/* length */
	s->lenb = k;
	if (k < n) {
		char a = p[k], b = k + 1 < n ? p[k + 1] : 0;

		if ((a == 'h' && b == 'h') || (a == 'l' && b == 'l'))
			s->len[0] = a, s->len[1] = b, k += 2;
		else if (strchr("hljztLq", a))
			s->len[0] = a, k++;
	}
	s->lene = k;
	if (k >= n)
		return false;
	s->conv = k, s->cv = p[k++];
	if (sc && s->cv == '[') {	/* a scanset: up to its ']' (a leading ']' or '^]' is part of it) */
		if (k < n && p[k] == '^')
			k++;
		if (k < n && p[k] == ']')
			k++;
		while (k < n && p[k] != ']')
			k++;
		if (k >= n)
			return false;
		k++;
	}
	s->e = k;
	return true;
}

static void check_spec(fctx *x, spec *s)
{
	cc *c = x->c;
	want w;
	bool bad;
	uint32_t a;

	if (s->cv == '%' && !s->pos)
		return;
	if (s->pos)
		x->positional = true;
	else if (s->cv != '%')
		x->plain = true;
	if (x->positional && x->plain && !x->va) {
		warn_at(x, "format", s, s->conv, "cannot mix positional and non-positional arguments in format string", NULL, NULL);
		x->stop = true;
		return;
	}
	if (!conv_want(x, s, &w, &bad)) {
		char ch[8];
		unsigned char u = (unsigned char)s->cv;

		if (u >= 0x20 && u < 0x7f)
			snprintf(ch, sizeof ch, "%c", u);
		else
			snprintf(ch, sizeof ch, "\\x%02x", u);
		warn_at(x, "format-invalid-specifier", s, s->conv, "invalid conversion specifier '%s'", ch, NULL);
		x->stop = true;
		return;
	}
	if (x->kind == FK_PRINTF)
		check_flags(x, s);
	if (x->va)
		return;
	if (s->wstar)
		amount(x, s, s->wstar, s->wat, false);
	if (!x->stop && s->pstar)
		amount(x, s, s->pstar, s->pat, true);
	if (x->stop || w.cls == W_NONE || s->suppress || s->cv == '%')
		return;
	if (s->pos) {
		a = data_arg(x, s->pos);
		if (!a) {
			char p[16], nn[16];

			snprintf(p, sizeof p, "%d", s->pos), snprintf(nn, sizeof nn, "%u", x->nargs);
			warn_at(x, "format", s, s->conv, "data argument position '%s' exceeds the number of data arguments (%s)", p, nn);
			x->stop = true;
			return;
		}
		if ((uint32_t)s->pos > x->used)
			x->used = (uint32_t)s->pos;
	} else {
		a = data_arg(x, (int)++x->used);
		if (!a) {
			warn_at(x, "format-insufficient-args", s, s->conv, "more '%%' conversions than data arguments", NULL, NULL);
			x->stop = true;
			return;
		}
	}
	if (!(N(a)->flags & NF_ERR) && !matches(c, &w, a, x->kind == FK_SCANF))
		mismatch(x, s, &w, a);
}

/* what the format literals of one call left: the most data arguments one consumed (clang's
   UncoveredArg: an argument is extra only if no literal reads it) */
typedef struct fuse {
	uint32_t used;
	bool any, skip;		/* a literal seen; one stopped or was positional (no extra-args check) */
	pp_span r;		/* the first literal */
} fuse;

static void check_literal(cc *c, int kind, uint32_t lit, uint32_t *args, uint32_t nargs, bool va, fuse *u)
{
	const cstr *cs = &c->strs[N(lit)->a];
	fstr f = { cs->s, cs->n ? cs->n - 1 : 0, NULL, N(lit)->loc, rng(c, lit) };
	fctx x = { c, &f, kind, args, nargs, 0, false, false, false, va };

	if (cs->esz != 1)
		return;
	if (f.r.e <= f.r.b)
		f.r.e = f.r.b + 1;
	fmap(c, &f);
	for (uint32_t i = 0; i < f.n && !x.stop; i++) {
		spec s;

		if (f.s[i] != '%')
			continue;
		if (!parse_spec(&x, i, &s)) {
			vdiag *d = cc_warn_begin(c, "format", true, NULL, floc(&f, i), "incomplete format specifier");

			if (d)
				pp_diag_emit(c->p, d);
			x.stop = true;
			break;
		}
		check_spec(&x, &s);
		i = s.e - 1;
	}
	if (!u->any)
		u->r = f.r;
	u->any = true;
	u->skip |= x.stop || x.positional;
	if (x.used > u->used)
		u->used = x.used;
	free(f.loc);
}

/* the literal a format argument stands for (0: none) */
static uint32_t literal_of(cc *c, uint32_t n)
{
	n = fstrip(c, n);
	if (N(n)->op == N_STR)
		return n;
	if (N(n)->op == N_VAR) {	/* a const variable initialized with a literal */
		const sym *y = &c->syms[N(n)->a];
		qty t = y->ty;
		bool cst = (QQ(t) & Q_CONST) || (is_array(c, t) && (QQ(T(c, t)->base) & Q_CONST));

		if (y->kind == S_VAR && cst && y->body && N(y->body)->op != N_INIT)
			return N(fstrip(c, y->body))->op == N_STR ? fstrip(c, y->body) : 0;
	}
	return 0;
}

static void check_arg(cc *c, int kind, uint32_t fa, uint32_t *args, uint32_t nargs, bool va, fuse *u)
{
	uint32_t n = fstrip(c, fa), lit;

	if (N(n)->op == N_COND && N(n)->b) {
		cval k;

		if (cc_eval(c, N(n)->a, &k) && k.kind == CV_INT) {
			check_arg(c, kind, k.i ? N(n)->b : N(n)->c, args, nargs, va, u);
		} else {
			check_arg(c, kind, N(n)->b, args, nargs, va, u);
			check_arg(c, kind, N(n)->c, args, nargs, va, u);
		}
		return;
	}
	lit = literal_of(c, n);
	if (lit) {
		check_literal(c, kind, lit, args, nargs, va, u);
		return;
	}
	if (!va && !nargs && !(N(n)->flags & NF_ERR) && !is_npc(c, n)) {	/* printf(s): clang's -Wformat-security */
		pp_span r = rng(c, n);
		vdiag *d = cc_warn_begin(c, "format-security", true, NULL, r.b, "format string is not a string literal (potentially insecure)");

		if (d) {
			vdiag *k;

			pp_diag_label(c->p, d, r.b, r.e, NULL);
			k = pp_diag_note(c->p, d, r.b, "treat the string as an argument to avoid this");
			if (k)
				pp_diag_fix(c->p, k, r.b, r.b, "\"%s\", ", VDA_MAYBE);
			pp_diag_emit(c->p, d);
		}
	}
	u->skip = true;	/* not a literal: what it reads is unknown */
}

/* a call to sym s with its arguments (converted) */
VPRIV void format_call(cc *c, uint32_t s, uint32_t *args, uint32_t n)
{
	uint16_t fm = fmt_of(c, s);
	unsigned kind = fm >> 14, idx = (fm >> 7) & 127, first = fm & 127;

	if (!fm || !kind || idx < 1 || idx > n)
		return;
	if (first && first <= idx)
		return;
	if (!first) {
		fuse u = { 0 };

		check_arg(c, (int)kind, args[idx - 1], NULL, 0, true, &u);
	} else {
		uint32_t *da = args + (first - 1 < n ? first - 1 : n), nd = first - 1 < n ? n - (first - 1) : 0;
		fuse u = { 0 };

		check_arg(c, (int)kind, args[idx - 1], da, nd, false, &u);
		if (u.any && !u.skip && u.used < nd) {	/* the first data argument no literal reads */
			uint32_t a = da[u.used];
			pp_span ar = rng(c, fstrip(c, a));
			vdiag *d = cc_warn_begin(c, "format-extra-args", true, NULL, argloc(c, a), "data argument not used by format string");

			if (d) {
				pp_diag_label(c->p, d, u.r.b, u.r.e, NULL);
				pp_diag_label(c->p, d, ar.b, ar.e, NULL);
				pp_diag_emit(c->p, d);
			}
		}
	}
}
