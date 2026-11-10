/* vcc front end: -Wformat, printf format strings against their arguments (clang's CheckFormatHandler) */
#include "ccimpl.h"
#include <ctype.h>

typedef struct fmtspec {
	char spec[4];		/* length modifier and conversion, e.g. "lld" */
	uint8_t badlen;		/* the length modifier is invalid with the conversion */
	const char *exp;	/* the expected type as clang prints it */
	uint8_t fix[32];	/* per argument class: 0 matches, 1 mismatch, 255 bool with %c, k: fix-it fmt_fix[k - 1] */
} fmtspec;

#include "fmttab.inc"

/* argument classes, the columns of the table (TYPES in gen-fmttab.py) */
enum {
	FT_CHAR, FT_SCHAR, FT_UCHAR, FT_SHORT, FT_USHORT, FT_INT, FT_UINT, FT_LONG, FT_ULONG, FT_LLONG, FT_ULLONG,
	FT_FLOAT, FT_DOUBLE, FT_LDOUBLE, FT_PCHAR, FT_PCCHAR, FT_PSCHAR, FT_PUCHAR, FT_PVOID, FT_PCVOID,
	FT_PINT, FT_PLONG, FT_BOOL, FT_I128, FT_PSHORT, FT_PUINT, FT_POBJ, FT_ENUMU, FT_ENUMI, FT_U128,
	FT_PLLONG, FT_PPTR
};
_Static_assert(FMT_NTY == 32, "fmttab.inc columns");

/* t's class, -1 if the table has none for it */
static int fmt_class(cc *c, qty t)
{
	int k = KIND(c, t);

	switch (k) {
	case TY_BOOL: return FT_BOOL;
	case TY_CHAR: return FT_CHAR;
	case TY_SCHAR: return FT_SCHAR;
	case TY_UCHAR: return FT_UCHAR;
	case TY_SHORT: return FT_SHORT;
	case TY_USHORT: return FT_USHORT;
	case TY_INT: return FT_INT;
	case TY_UINT: return FT_UINT;
	case TY_LONG: return FT_LONG;
	case TY_ULONG: return FT_ULONG;
	case TY_LLONG: return FT_LLONG;
	case TY_ULLONG: return FT_ULLONG;
	case TY_INT128: return FT_I128;
	case TY_UINT128: return FT_U128;
	case TY_FLOAT: return FT_FLOAT;
	case TY_DOUBLE: return FT_DOUBLE;
	case TY_LDOUBLE: return FT_LDOUBLE;
	case TY_ENUM: {
		int b = KIND(c, T(c, t)->base);

		return b == TY_UINT ? FT_ENUMU : b == TY_INT ? FT_ENUMI : -1;
	}
	case TY_PTR: {
		qty p = T(c, t)->base;
		int pk = KIND(c, p), q = QQ(p);

		if (T(c, t)->flags & TF_BLOCK)
			return -1;
		if (pk == TY_PTR)
			return FT_PPTR;
		if (pk == TY_STRUCT || pk == TY_UNION || pk == TY_FUNC || pk == TY_FLOAT || pk == TY_DOUBLE)
			return FT_POBJ;
		if (q == Q_CONST)
			return pk == TY_CHAR ? FT_PCCHAR : pk == TY_VOID ? FT_PCVOID : -1;
		if (q)
			return -1;
		switch (pk) {
		case TY_CHAR: return FT_PCHAR;
		case TY_SCHAR: return FT_PSCHAR;
		case TY_UCHAR: return FT_PUCHAR;
		case TY_VOID: return FT_PVOID;
		case TY_INT: return FT_PINT;
		case TY_LONG: return FT_PLONG;
		case TY_LLONG: return FT_PLLONG;
		case TY_SHORT: return FT_PSHORT;
		case TY_UINT: return FT_PUINT;
		}
		return -1;
	}
	}
	return -1;
}

typedef struct fmtx {
	cc *c;
	uint32_t node;		/* the N_STR */
	pp_span all;		/* the format string expression */
	const char *s;
	uint32_t n;
} fmtx;

/* the source location of byte i of the format string, 0 if it cannot be mapped */
static uint32_t fmt_loc2(fmtx *x, uint32_t i, uint32_t *endp);
static uint32_t fmt_loc(fmtx *x, uint32_t i)
{
	return fmt_loc2(x, i, NULL);
}

static uint32_t fmt_loc2(fmtx *x, uint32_t i, uint32_t *endp)
{
	uint32_t b = x->all.b, e = x->all.e;
	size_t len;
	const char *src = pp_src(x->c->p, b, e, &len), *p, *end;
	uint32_t k = 0;

	if (!src)
		return 0;
	p = src, end = src + len;
	while (p < end) {
		while (p < end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r'))
			p++;
		if (p >= end)
			break;
		if (p + 2 < end && p[0] == 'u' && p[1] == '8')
			p += 2;
		if (*p != '"')
			return 0;
		for (p++; p < end && *p != '"';) {
			const char *q = p;

			if (*p == '\n')
				return 0;
			if (*p == '\\') {
				p++;
				if (p >= end)
					return 0;
				if (*p == 'x') {
					for (p++; p < end && isxdigit((unsigned char)*p); p++)
						;
				} else if (*p >= '0' && *p <= '7') {
					for (int d = 0; d < 3 && p < end && *p >= '0' && *p <= '7'; d++, p++)
						;
				} else if (*p == 'u' || *p == 'U') {
					return 0;	/* (several bytes for one escape) */
				} else {
					p++;
				}
			} else {
				if ((unsigned char)*p >= 0x80)
					return 0;
				p++;
			}
			if (k == i) {
				if (endp)
					*endp = b + (uint32_t)(p - src);
				return b + (uint32_t)(q - src);
			}
			k++;
		}
		if (p >= end)
			return k == i ? 0 : 0;
		p++;
	}
	return 0;
}

/* one conversion specification */
typedef struct fspec {
	uint32_t start, end;	/* bytes [start, end) of the string */
	int32_t pos;		/* n$: n - 1, -1 none */
	uint32_t fminus, fplus, fspace, fhash, fzero, fquote;	/* flag byte + 1 */
	uint32_t wb, we;	/* width bytes [wb, we), wb == we: none */
	int wstar;		/* width '*' at byte wstar, -1 */
	uint32_t pb, pe;	/* precision: '.' at pb .. pe, pb == pe: none */
	int pstar;
	uint32_t lb, le;	/* length modifier */
	uint32_t conv;		/* the conversion byte */
} fspec;

static bool fmt_report(fmtx *x, const char *flag, uint32_t loc, const pp_span *r, int nr, const pp_fixit *fx, int nfx,
		       const char *fmt, ...) __attribute__((format(printf, 8, 9)));
static bool fmt_report(fmtx *x, const char *flag, uint32_t loc, const pp_span *r, int nr, const pp_fixit *fx, int nfx,
		       const char *fmt, ...)
{
	char buf[512];
	va_list ap;

	va_start(ap, fmt);
	vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	pp_diag_next_id = pp_diag_fmt_id(fmt);	/* (the message is formatted here) */
	{
		bool on = cc_warnx(x->c, flag, true, NULL, loc, r, nr, fx, nfx, "%s", buf);

		pp_diag_next_id = NULL;
		return on;
	}
}

/* the range of bytes [b, e) */
static bool fmt_span(fmtx *x, uint32_t b, uint32_t e, pp_span *r)
{
	uint32_t end = 0, lb = fmt_loc(x, b), le = e ? fmt_loc2(x, e - 1, &end) : 0;

	if (!lb || !le)
		return false;
	(void)end;
	r->b = lb;
	r->e = le + 1;	/* (clang: the last byte's first character) */
	return true;
}

/*
 * The fix-it's text, clang's fixType then toString: the flags in C99 order
 * (never '\''), width, precision, the new length modifier and conversion.
 * Only the length modifier changed (the conversion is the same or its
 * signed/unsigned twin): every flag stays; else c drops the precision, '#',
 * '0' and '+', s drops '#' and '0', d drops '#', u drops '#' and '+'.
 */
static void fmt_fixtext(fmtx *x, const fspec *f, const char *lc, bool charp, char *out, size_t cap)
{
	char oc = x->s[f->conv], nc = lc[strlen(lc) - 1];
	bool same = oc == nc || (strchr("diu", oc) && strchr("du", nc));
	bool minus = f->fminus, plus = f->fplus, space = f->fspace, hash = f->fhash, zero = f->fzero, prec = true;
	size_t n = 0;
	char nlc[4];

	if (plus && (oc == 'd' || oc == 'i') && nc == 'u') {
		/* clang: d/i turns u for an unsigned argument only without '+' */
		snprintf(nlc, sizeof(nlc), "%s", lc);
		nlc[strlen(nlc) - 1] = oc;
		lc = nlc, nc = oc;
	}
	if (charp)
		hash = zero = false;	/* (a char pointer: %s before anything else) */
	else if (!same) {
		if (nc == 'c')
			prec = hash = zero = plus = false;
		else if (nc == 's')
			hash = zero = false;
		else if (nc == 'd')
			hash = false;
		else if (nc == 'u')
			hash = plus = false;
	}
	n = (size_t)snprintf(out, cap, "%%");
	if (f->pos >= 0)
		n += (size_t)snprintf(out + n, cap - n, "%d$", f->pos + 1);
	n += (size_t)snprintf(out + n, cap - n, "%s%s%s%s%s", minus ? "-" : "", plus ? "+" : "", space ? " " : "",
			      hash ? "#" : "", zero ? "0" : "");
	if (f->wstar >= 0)
		n += (size_t)snprintf(out + n, cap - n, "*");
	else if (f->we > f->wb)
		n += (size_t)snprintf(out + n, cap - n, "%u", (unsigned)strtoul(x->s + f->wb, NULL, 10));
	if (prec && f->pstar >= 0)
		n += (size_t)snprintf(out + n, cap - n, ".*");
	else if (prec && f->pe > f->pb + 1)
		n += (size_t)snprintf(out + n, cap - n, ".%u", (unsigned)strtoul(x->s + f->pb + 1, NULL, 10));
	if (n < cap)
		snprintf(out + n, cap - n, "%s", lc);
}

static const char *const fmt_valid_flags[6] = {
	/* '-' '+' ' ' '#' '0' '\'' */
	"diouxXcspfFeEgGaA", "difFeEgGaA", "difFeEgGaA", "oxXaAeEfFgG", "diouxXaAeEfFgG", "diufFgG",
};

/* the C library's printf functions (clang's builtins with a format attribute): format
   argument, first data argument (0: a va_list) */
static const struct libfmt {
	const char *name;
	uint8_t idx, first;
} libfmts[] = {
	{ "printf", 1, 2 },   { "fprintf", 2, 3 },  { "sprintf", 2, 3 },   { "snprintf", 3, 4 },
	{ "vprintf", 1, 0 },  { "vfprintf", 2, 0 }, { "vsprintf", 2, 0 },  { "vsnprintf", 3, 0 },
};

/* the format attribute of a call to fs (arch << 16 | fmt << 8 | first), else the library's
   for a global printf (clang: implicitly declared, or declared without the attribute) */
VPRIV uint32_t fmt_attr(cc *c, uint32_t fs)
{
	const sym *y = &c->syms[fs];
	int nl;
	const char *nm;

	if (y->attrs && c->attr[y->attrs].fmt)
		return c->attr[y->attrs].fmt;
	if (c->o->no_builtin || !(y->flags & SF_GLOBAL) || y->sc == SC_STATIC)
		return 0;
	nm = idname(y->name, &nl);
	if (nl > 10 && !memcmp(nm, "__builtin_", 10))
		nm += 10, nl -= 10;
	if (nl < 6 || nl > 9 || (nm[0] != 'p' && nm[0] != 'f' && nm[0] != 's' && nm[0] != 'v'))
		return 0;
	for (size_t i = 0; i < sizeof libfmts / sizeof *libfmts; i++)
		if ((int)strlen(libfmts[i].name) == nl && !memcmp(libfmts[i].name, nm, (size_t)nl))
			return 1u << 16 | (uint32_t)libfmts[i].idx << 8 | libfmts[i].first;
	return 0;
}

/*
 * The checks of one N_STR format string, in clang's order: per specification
 * its flags, width and precision, length modifier, then (first != 0) the
 * argument; at the end the first data argument no specification used.
 */
VPRIV void format_check(cc *c, uint32_t fs, const uint32_t *args, const qty *aty, const uint32_t *atd, uint32_t nargs)
{
	uint32_t fa = fmt_attr(c, fs), fi, first, ndata, n;
	const fmtspec *tab;
	fmtx x;
	uint32_t s;
	uint64_t covered[4] = { 0 };
	int positional = -1, argi = 0;
	bool stopped = false;

	if (!fa || fa >> 16 != 1 || c->o->short_wchar)
		return;
	if (c->o->target == PP_DARWIN)
		tab = fmt_darwin;
	else if (c->o->target == PP_LINUX_AARCH64)
		tab = fmt_linux;
	else
		return;
	fi = (fa >> 8 & 255) - 1, first = fa & 255;
	if (fi >= nargs)
		return;
	if (first) {
		if (first - 1 > nargs)
			return;
		ndata = nargs - (first - 1);
	} else {
		ndata = 0;
	}
	if (ndata > 256)
		return;
	s = args[fi];
	while (N(s)->op == N_CAST)
		s = N(s)->a;
	if (first && first - 1 == nargs && (N(s)->op == N_VAR || N(s)->op == N_MEMBER)) {
		/* -Wformat-security: a format that is no literal and no data arguments */
		qty t = N(s)->ty;
		bool cst = is_array(c, t) ? (QQ(T(c, t)->base) & Q_CONST) : (QQ(t) & Q_CONST);
		uint32_t v = N(s)->op == N_VAR ? N(s)->a : 0;

		if (v && (c->syms[v].flags & SF_PARAM) && c->fn && c->syms[c->fn].attrs && c->attr[c->syms[c->fn].attrs].fmt)
			return;	/* the format parameter of a format function */
		if (!cst && (is_ptr(c, t) || is_array(c, t))) {
			pp_span r = rng(c, args[fi]);
			pp_fixit fx = { r.b, r.b, "\"%s\", ", 0 };

			if (cc_warnx(c, "format-security", true, NULL, r.b, &r, 1, NULL, 0,
				     "format string is not a string literal (potentially insecure)"))
				report(c, "note", r.b, &r, 1, &fx, 1, "treat the string as an argument to avoid this");
		}
		return;
	}
	if (N(s)->op != N_STR || (N(s)->flags & NF_PAREN) || c->strs[N(s)->a].esz != 1)
		return;
	x.c = c, x.node = s;
	x.all = rng(c, s);
	x.s = c->strs[N(s)->a].s;
	x.n = c->strs[N(s)->a].n - 1;
	n = x.n;
	for (uint32_t i = 0; i < n && !stopped;) {
		fspec f;
		uint32_t p;
		char lc[4];
		const fmtspec *row = NULL;
		pp_span sr;
		bool havesr;
		uint32_t cv;

		if (x.s[i] != '%') {
			i++;
			continue;
		}
		memset(&f, 0, sizeof(f));
		f.start = i, f.pos = -1, f.wstar = f.pstar = -1;
		p = i + 1;
		if (p >= n) {
			pp_span r;

			if (fmt_span(&x, i, n, &r))
				fmt_report(&x, "format", r.b, &r, 1, NULL, 0, "incomplete format specifier");
			return;
		}
		{	/* n$ */
			uint32_t q = p, v = 0;

			while (q < n && x.s[q] >= '0' && x.s[q] <= '9' && v < 100000)
				v = v * 10 + (uint32_t)(x.s[q++] - '0');
			if (q > p && q < n && x.s[q] == '$') {
				if (v == 0)
					return;
				f.pos = (int32_t)v - 1;
				p = q + 1;
			}
		}
		for (; p < n; p++) {
			char ch = x.s[p];

			if (ch == '-') f.fminus = p + 1;
			else if (ch == '+') f.fplus = p + 1;
			else if (ch == ' ') f.fspace = p + 1;
			else if (ch == '#') f.fhash = p + 1;
			else if (ch == '0') f.fzero = p + 1;
			else if (ch == '\'') f.fquote = p + 1;
			else break;
		}
		f.wb = f.we = p;
		if (p < n && x.s[p] == '*') {
			f.wstar = (int)p++;
			if (p < n && x.s[p] >= '0' && x.s[p] <= '9')
				return;	/* *n$ */
		} else {
			while (p < n && x.s[p] >= '0' && x.s[p] <= '9')
				p++;
		}
		f.we = p;
		f.pb = f.pe = p;
		if (p < n && x.s[p] == '.') {
			p++;
			if (p < n && x.s[p] == '*') {
				f.pstar = (int)p++;
				if (p < n && x.s[p] >= '0' && x.s[p] <= '9')
					return;
			} else {
				while (p < n && x.s[p] >= '0' && x.s[p] <= '9')
					p++;
			}
			f.pe = p;
		}
		f.lb = p;
		if (p < n && x.s[p] == 'q') {
			p++;	/* BSD quad: only as an incomplete specifier */
		} else if (p < n && (x.s[p] == 'h' || x.s[p] == 'l')) {
			p++;
			if (p < n && x.s[p] == x.s[p - 1])
				p++;
		} else if (p < n && strchr("jztL", x.s[p]) && x.s[p]) {
			p++;
		}
		f.le = p;
		if (p >= n) {
			pp_span r;

			if (fmt_span(&x, i, n, &r))
				fmt_report(&x, "format", r.b, &r, 1, NULL, 0, "incomplete format specifier");
			return;
		}
		f.conv = p;
		f.end = p + 1;
		i = f.end;
		cv = (unsigned char)x.s[p];
		/* what this checker does not model leaves the rest of the call alone */
		if (strchr("IwqbB", (int)cv) || (c->o->target == PP_DARWIN && strchr("DOU", (int)cv)) || cv == 0)
			return;
		havesr = fmt_span(&x, f.start, f.end, &sr);
		if (!havesr)
			return;
		if (cv == '%' || cv == 'm')
			continue;	/* (consume no argument) */
		if (!strchr("diouxXcspfFeEgGaAnCS", (int)cv)) {
			/* an invalid conversion: covers its argument (none left: stop) */
			uint32_t l = fmt_loc(&x, f.conv);
			uint32_t ai;

			char sp[8];

			if (cv >= 0x7f || f.wstar >= 0 || f.pstar >= 0)
				return;
			if (cv < 0x20)
				snprintf(sp, sizeof(sp), "\\x%02x", cv);
			else
				sp[0] = (char)cv, sp[1] = 0;
			ai = f.pos >= 0 ? (uint32_t)f.pos : 0;	/* (clang: a fresh specifier's index, 0) */
			if (l)
				fmt_report(&x, "format-invalid-specifier", l, &sr, 1, NULL, 0, "invalid conversion specifier '%s'", sp);
			if (first && ai < ndata)
				covered[ai >> 6] |= 1ull << (ai & 63);
			else
				return;
			continue;
		}
		/* positional and not: they may not mix */
		if (positional < 0) {
			positional = f.pos >= 0;
		} else if (positional != (f.pos >= 0)) {
			uint32_t l = fmt_loc(&x, f.conv);

			if (l)
				fmt_report(&x, "format", l, &sr, 1, NULL, 0,
					   "cannot mix positional and non-positional arguments in format string");
			return;
		}
		/* the amounts: '*' takes an int argument */
		for (int k = 0; k < 2; k++) {
			int star = k ? f.pstar : f.wstar;
			uint32_t ai, l;
			qty t;
			int cl;

			if (star < 0)
				continue;
			if (positional)
				return;
			if (!first)
				continue;
			ai = (uint32_t)argi++;
			l = fmt_loc(&x, (uint32_t)star + (k ? 0 : 1));
			if (ai >= ndata) {
				if (l)
					fmt_report(&x, "format", l, &sr, 1, NULL, 0,
						   "'%s' specified field %s is missing a matching 'int' argument",
						   k ? ".*" : "*", k ? "precision" : "width");
				return;
			}
			covered[ai >> 6] |= 1ull << (ai & 63);
			t = aty[first - 1 + ai];
			if (!t || (cl = fmt_class(c, t)) < 0)
				return;
			if (tab[0].fix[cl] != 0 && tab[0].fix[cl] != 255) {	/* tab[0]: "d", int */
				pp_span r[2] = { sr, rng(c, args[first - 1 + ai]) };

				uint32_t td = atd[first - 1 + ai];
				char tn[256];

				if (td && KIND(c, t) != TY_FLOAT)
					snprintf(tn, sizeof(tn), "'%.*s' (aka '%s')", IDF(c->syms[td].name), tyname(c, t));
				else
					snprintf(tn, sizeof(tn), "'%s'", KIND(c, t) == TY_FLOAT ? "double" : tyname(c, t));
				if (l)
					fmt_report(&x, "format", l, r, 2, NULL, 0, "field %s should have type 'int', but argument has type %s",
						   k ? "precision" : "width", tn);
				return;
			}
		}
		/* the conversion */
		{
			uint32_t ll = f.le - f.lb;
			char conv = (char)cv;

			memcpy(lc, x.s + f.lb, ll);
			lc[ll] = conv, lc[ll + 1] = 0;
			for (size_t k = 0; k < sizeof(fmt_linux) / sizeof(fmt_linux[0]); k++)
				if (!strcmp(tab[k].spec, lc)) {
					row = &tab[k];
					break;
				}
			if (!row)
				return;
			/* flags valid with the conversion, in clang's order: ' 0 + space # - */
			{
				static const int order[6] = { 5, 4, 1, 2, 3, 0 };
				static const char fl[6] = { '-', '+', ' ', '#', '0', '\'' };
				char cc0 = conv == 'C' ? 'c' : conv == 'S' ? 's' : conv;
				bool prec = f.pb != f.pe && (f.pstar >= 0 || f.pe > f.pb + 1);	/* ('.' alone: not specified) */

				if (f.wb != f.we && cc0 == 'n') {
					uint32_t l = fmt_loc(&x, f.wstar >= 0 ? f.wb + 1 : f.wb);

					if (l)
						fmt_report(&x, "format", l, &sr, 1, NULL, 0,
							   "field width used with 'n' conversion specifier, resulting in undefined behavior");
				}
				if (prec && !strchr("diouxXaAeEfFgGs", conv)) {
					uint32_t l = fmt_loc(&x, f.pstar >= 0 ? (uint32_t)f.pstar : f.pb);

					if (l)
						fmt_report(&x, "format", l, &sr, 1, NULL, 0,
							   "precision used with '%c' conversion specifier, resulting in undefined behavior", conv);
				}
				for (int o = 0; o < 6; o++) {
					int k = order[o];
					uint32_t at = k == 0 ? f.fminus : k == 1 ? f.fplus : k == 2 ? f.fspace : k == 3 ? f.fhash :
						      k == 4 ? f.fzero : f.fquote;

					if (at && !strchr(fmt_valid_flags[k], cc0)) {
						uint32_t l = fmt_loc(&x, at - 1);

						if (l)
							fmt_report(&x, "format", l, &sr, 1, NULL, 0,
								   "flag '%c' results in undefined behavior with '%c' conversion specifier", fl[k], conv);
					}
				}
				if (f.fspace && f.fplus) {
					uint32_t l = fmt_loc(&x, f.fspace - 1);

					if (l)
						fmt_report(&x, "format", l, &sr, 1, NULL, 0, "flag ' ' is ignored when flag '+' is present");
				}
				if (f.fzero && f.fminus) {
					uint32_t l = fmt_loc(&x, f.fzero - 1);

					if (l)
						fmt_report(&x, "format", l, &sr, 1, NULL, 0, "flag '0' is ignored when flag '-' is present");
				}
			}
			if (row->badlen) {
				uint32_t l = fmt_loc(&x, f.lb);
				char lm[3] = { 0 };

				memcpy(lm, x.s + f.lb, ll);
				if (l && fmt_report(&x, "format", l, &sr, 1, NULL, 0,
						    "length modifier '%s' results in undefined behavior or no effect with '%c' conversion specifier",
						    lm, conv) &&
				    (lm[0] == 'L' || lm[0] == 'q') && strchr("diouxXn", conv)) {
					pp_fixit fx = { l, l + 1, "ll", 0 };

					report(c, "note", l, NULL, 0, &fx, 1, "did you mean to use 'll'?");
				}
			}
		}
		if (!first)
			continue;
		{
			uint32_t ai = positional ? (uint32_t)f.pos : (uint32_t)argi++;
			uint32_t an, code;
			qty t;
			int cl;
			pp_span r[2];

			if (ai >= ndata) {
				uint32_t l = fmt_loc(&x, f.conv);

				if (!positional && l)
					fmt_report(&x, "format-insufficient-args", l, &sr, 1, NULL, 0,
						   "more '%%' conversions than data arguments");
				return;
			}
			covered[ai >> 6] |= 1ull << (ai & 63);
			an = args[first - 1 + ai];
			t = aty[first - 1 + ai];
			if (!t || (cl = fmt_class(c, t)) < 0)
				continue;
			code = row->fix[cl];
			if (!code)
				continue;
			r[0] = sr, r[1] = rng(c, an);
			if (code == 255) {
				fmt_report(&x, "format", r[1].b, r, 1, NULL, 0, "using '%.*s' format specifier, but argument has boolean value",
					   (int)(f.end - f.start), x.s + f.start);
				continue;
			}
			{
				char ft[64];
				pp_fixit fx;
				bool en = KIND(c, t) == TY_ENUM;

				char lcb[8], tn[256];
				const char *fl = code > 1 ? fmt_fix[code - 1] : NULL;
				uint32_t td = atd[first - 1 + ai];

				if (td && fl) {
					/* clang: a typedef's arguments: size_t, ssize_t -> z, [u]intmax_t -> j, ptrdiff_t -> t;
					 * one to a char is an integer (%hhd, not %c) */
					char nc = fl[strlen(fl) - 1], lm = 0;

					for (uint32_t q = td; q && !lm; q = c->syms[q].tdef) {
						size_t l = idlen_(c->syms[q].name);
						const char *nm = idstr_(c->syms[q].name);

						if ((l == 6 && !memcmp(nm, "size_t", 6)) || (l == 7 && !memcmp(nm, "ssize_t", 7)))
							lm = 'z';
						else if ((l == 8 && !memcmp(nm, "intmax_t", 8)) || (l == 9 && !memcmp(nm, "uintmax_t", 9)))
							lm = 'j';
						else if (l == 9 && !memcmp(nm, "ptrdiff_t", 9))
							lm = 't';
					}
					if (nc == 'c' && cl <= FT_UCHAR) {
						snprintf(lcb, sizeof(lcb), "hh%c", is_signed(c, t) ? 'd' : 'u');
						fl = lcb;
					} else if (lm && strchr("diouxX", nc)) {
						snprintf(lcb, sizeof(lcb), "%c%c", lm, nc);
						fl = lcb;
					}
				}
				if (td)
					snprintf(tn, sizeof(tn), "'%.*s' (aka '%s')", IDF(c->syms[td].name), tyname(c, t));
				else
					snprintf(tn, sizeof(tn), "'%s'", en ? tyname(c, T(c, t)->base) : tyname(c, t));
				if (code > 1) {
					fmt_fixtext(&x, &f, fl, cl >= FT_PCHAR && cl <= FT_PUCHAR, ft, sizeof(ft));
					fx = (pp_fixit){ sr.b, sr.e, ft, 0 };
				}
				fmt_report(&x, "format", r[1].b, r, 2, code > 1 ? &fx : NULL, code > 1,
					   "format specifies type %s but the argument has %stype %s", row->exp,
					   en && !td ? "underlying " : "", tn);
			}
		}
	}
	if (stopped || !first)
		return;
	for (uint32_t ai = 0; ai < ndata; ai++)
		if (!(covered[ai >> 6] >> (ai & 63) & 1)) {
			pp_span r = x.all;

			fmt_report(&x, "format-extra-args", rng(c, args[first - 1 + ai]).b, &r, 1, NULL, 0,
				   "data argument not used by format string");
			return;
		}
}
