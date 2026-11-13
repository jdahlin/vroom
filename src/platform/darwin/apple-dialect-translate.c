/* apple-dialect-translate.c: Apple's assembler dialect to GNU's, text to text; uses nothing of the assembler's state. */
#include "as/as.h"
#include <ctype.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

typedef struct { char *p; size_t n, cap; } DBuf;

static void db_put(DBuf *b, const char *s, size_t n)
{
	if (b->n + n + 64 > b->cap) {
		b->cap = (b->n + n + 64) * 2;
		b->p = realloc(b->p, b->cap);
	}
	memcpy(b->p + b->n, s, n);
	b->n += n;
}
static void db_s(DBuf *b, const char *s) { db_put(b, s, strlen(s)); }

static bool dt_id(char c) { return isalnum((unsigned char)c) || c == '_' || c == '.' || c == '$'; }

static bool dt_word(const char *p, const char *e, const char *w)
{
	size_t n = strlen(w);

	return (size_t)(e - p) >= n && !memcmp(p, w, n) && (p + n == e || !dt_id(p[n]));
}

/* __SEG,__sect -> the ELF section the Mach-O writer maps back to it */
static void dt_section(DBuf *b, const char *p, const char *e)
{
	static const struct { const char *m, *elf; } t[] = {
		{ "__TEXT,__text", "\t.text\n" },
		{ "__TEXT,__cstring", "\t.section\t.rodata.str1.1,\"aMS\",@progbits,1\n" },
		{ "__TEXT,__const", "\t.section\t.rodata,\"a\",@progbits\n" },
		{ "__DATA,__const", "\t.section\t.rodata,\"a\",@progbits\n" },
		{ "__DATA,__data", "\t.data\n" },
		{ "__DATA,__bss", "\t.bss\n" },
		{ "__DATA,__mod_init_func", "\t.section\t.init_array,\"aw\",@init_array\n" },
		{ "__DATA,__mod_term_func", "\t.section\t.fini_array,\"aw\",@fini_array\n" },
		{ "__DATA,__thread_vars", "\t.section\t.tlv,\"aw\",@progbits\n" },
		{ "__DATA,__thread_data", "\t.section\t.tdata,\"awT\",@progbits\n" },
		{ "__DATA,__thread_bss", "\t.section\t.tbss,\"awT\",@nobits\n" },
	};
	const char *q = p;
	int commas = 0;

	while (q < e && !(*q == ',' && ++commas == 2) && *q != ' ' && *q != '\t')
		q++;
	for (size_t k = 0; k < sizeof t / sizeof t[0]; k++)
		if ((size_t)(q - p) == strlen(t[k].m) && !memcmp(p, t[k].m, (size_t)(q - p))) {
			db_s(b, t[k].elf);
			return;
		}
	/* section("seg,sect") as the attribute gave it; flags from the segment and type
	 * (the Mach-O writer gives each such name its own section) */
	{
		const char *te = q;
		bool text = q - p > 7 && !memcmp(p, "__TEXT,", 7);

		while (te < e && *te != '\n')
			te++;
		bool code = memmem(q, (size_t)(te - q), "pure_instructions", 17) != NULL;
		bool zero = memmem(q, (size_t)(te - q), "zerofill", 8) != NULL;

		/* the whole spec (seg,sect[,type[,attrs]]): the writer makes it its own section */
		const char *se = te;
		while (se > p && (se[-1] == ' ' || se[-1] == '\t' || se[-1] == '\r'))
			se--;
		db_s(b, "\t.section\t\"");
		db_put(b, p, (size_t)(se - p));
		db_s(b, code ? "\",\"ax\",@progbits\n" : zero ? "\",\"aw\",@nobits\n" :
			text ? "\",\"a\",@progbits\n" : "\",\"aw\",@progbits\n");
	}
}

/* one operand line: Lname -> .Lname, x@SPEC+k -> :mod:x+k */
static void dt_operands(DBuf *b, const char *p, const char *e)
{
	static const struct { const char *spec, *mod; } sp[] = {
		{ "GOTPAGEOFF", ":got_lo12:" }, { "GOTPAGE", ":got:" }, { "TLVPPAGEOFF", ":vcc_tlvp_lo12:" },
		{ "TLVPPAGE", ":vcc_tlvp:" }, { "PAGEOFF", ":lo12:" }, { "PAGE", "" },
	};
	bool quote = false;

	const char *p0 = p;

	while (p < e) {
		if (*p == '"') {
			quote = !quote;
			db_put(b, p++, 1);
			continue;
		}
		if (quote || !dt_id(*p) || *p == '.' || isdigit((unsigned char)*p)) {
			if (*p == '\\' && quote && p + 1 < e)
				db_put(b, p++, 1);
			db_put(b, p++, 1);
			continue;
		}
		const char *s = p;

		while (p < e && dt_id(*p))
			p++;
		const char *mod = NULL;
		const char *after = p;

		if (p < e && *p == '@')
			for (size_t k = 0; k < sizeof sp / sizeof sp[0]; k++)
				if (dt_word(p + 1, e, sp[k].spec)) {
					mod = sp[k].mod;
					after = p + 1 + strlen(sp[k].spec);
					break;
				}
		if (mod)
			db_s(b, mod);
		if (s[0] == 'L' && !((size_t)(p - s) == 2 && s[1] == 'R') && !(s > p0 && s[-1] == '.'))
			db_s(b, ".");	/* Mach-O's assembler-local Lname is ELF's .Lname */
		db_put(b, s, (size_t)(p - s));
		p = after;
	}
}

/* split "a, b, c" (to c) into up to 8 trimmed fields */
static int dt_fields(const char *a, const char *c, const char **f, const char **fe)
{
	int n = 0;

	while (a < c && n < 8) {
		while (a < c && (*a == ' ' || *a == '\t'))
			a++;
		const char *q = a;

		while (q < c && *q != ',')
			q++;
		const char *r = q;

		while (r > a && (r[-1] == ' ' || r[-1] == '\t'))
			r--;
		f[n] = a, fe[n] = r, n++;
		a = q < c ? q + 1 : c;
	}
	return n;
}

static long dt_num(const char *a, const char *e)
{
	long v = 0;

	for (; a < e && *a >= '0' && *a <= '9'; a++)
		v = v * 10 + (*a - '0');
	return v;
}

/* sym size in sec, aligned 1 << al: as a .pushsection block */
static void dt_zero(DBuf *b, const char *sec, const char *y, const char *ye, long size, long al)
{
	char t[96];

	db_s(b, "\t.pushsection\t");
	db_s(b, sec);
	snprintf(t, sizeof t, "\n\t.p2align\t%ld\n", al);
	db_s(b, t);
	if (y) {
		dt_operands(b, y, ye);
		db_s(b, ":");
	}
	snprintf(t, sizeof t, "\n\t.zero\t%ld\n\t.popsection", size ? size : 1);
	db_s(b, t);
}

/* Apple's NEON spelling "op.T vA, vB" -> "op vA.T, vB.T" (and vA[i] -> vA.T[i]) */
static bool dt_neon(DBuf *b, const char *s, const char *c)
{
	const char *m = s, *d;

	while (m < c && *m != ' ' && *m != '\t')
		m++;
	if (!isalpha((unsigned char)*s) || memchr(s, ':', (size_t)(m - s)))
		return false;	/* a label */
	d = memchr(s, '.', (size_t)(m - s));
	if (!d) {
		/* "mov d0, v1[1]": the lane size from the scalar register */
		static const char *const lane[] = { ".b", ".h", ".s", ".d" };
		const char *r = m, *lb;

		while (r < c && (*r == ' ' || *r == '\t'))
			r++;
		lb = memchr(r, '[', (size_t)(c - r));
		if (!lb || r + 1 >= c || !strchr("bhsd", *r) || !isdigit((unsigned char)r[1]) || memchr(r, '.', (size_t)(c - r)))
			return false;
		const char *v = lb;

		while (v > r && isdigit((unsigned char)v[-1]))
			v--;
		if (v == lb || v == r || (v[-1] != 'v' && v[-1] != 'V'))
			return false;
		DBuf t = { 0 };

		db_put(&t, "\t", 1);
		db_put(&t, s, (size_t)(lb - s));
		db_s(&t, lane[strchr("bhsd", *r) - "bhsd"]);
		db_put(&t, lb, (size_t)(c - lb));
		dt_operands(b, t.p, t.p + t.n);
		free(t.p);
		return true;
	}
	if (d == s || (d - s == 1 && *s == 'b') || d + 1 >= m ||
	    (!(d[1] >= '0' && d[1] <= '9') && !(m - d == 2 && strchr("bhsdq", d[1]))))
		return false;
	DBuf t = { 0 };
	/* operands after the first: widening and narrowing ops name the destination's arrangement */
	static const struct { const char *op, *t, *src; } wide[] = {
		{ "sshll", ".2d", ".2s" }, { "sshll", ".4s", ".4h" }, { "sshll", ".8h", ".8b" },
		{ "ushll", ".2d", ".2s" }, { "ushll", ".4s", ".4h" }, { "ushll", ".8h", ".8b" },
		{ "sxtl", ".2d", ".2s" }, { "sxtl", ".4s", ".4h" }, { "sxtl", ".8h", ".8b" },
		{ "uxtl", ".2d", ".2s" }, { "uxtl", ".4s", ".4h" }, { "uxtl", ".8h", ".8b" },
		{ "sshll2", ".2d", ".4s" }, { "sshll2", ".4s", ".8h" }, { "sshll2", ".8h", ".16b" },
		{ "ushll2", ".2d", ".4s" }, { "ushll2", ".4s", ".8h" }, { "ushll2", ".8h", ".16b" },
		{ "sxtl2", ".2d", ".4s" }, { "sxtl2", ".4s", ".8h" }, { "sxtl2", ".8h", ".16b" },
		{ "uxtl2", ".2d", ".4s" }, { "uxtl2", ".4s", ".8h" }, { "uxtl2", ".8h", ".16b" },
		{ "uaddlp", ".8h", ".16b" }, { "uaddlp", ".4s", ".8h" }, { "uaddlp", ".2d", ".4s" },
		{ "uaddlp", ".4h", ".8b" }, { "uaddlp", ".2s", ".4h" }, { "uaddlp", ".1d", ".2s" },
		{ "saddlp", ".8h", ".16b" }, { "saddlp", ".4s", ".8h" }, { "saddlp", ".2d", ".4s" },
		{ "saddlp", ".4h", ".8b" }, { "saddlp", ".2s", ".4h" }, { "saddlp", ".1d", ".2s" },
		{ "uadalp", ".8h", ".16b" }, { "uadalp", ".4s", ".8h" }, { "uadalp", ".2d", ".4s" },
		{ "sadalp", ".8h", ".16b" }, { "sadalp", ".4s", ".8h" }, { "sadalp", ".2d", ".4s" },
		{ "xtn", ".8b", ".8h" }, { "xtn", ".4h", ".4s" }, { "xtn", ".2s", ".2d" },
		{ "xtn2", ".16b", ".8h" }, { "xtn2", ".8h", ".4s" }, { "xtn2", ".4s", ".2d" },
	};
	const char *src = NULL;
	int nv = 0;

	for (size_t k = 0; k < sizeof wide / sizeof wide[0]; k++)
		if (strlen(wide[k].op) == (size_t)(d - s) && !memcmp(wide[k].op, s, (size_t)(d - s)) &&
		    strlen(wide[k].t) == (size_t)(m - d) && !memcmp(wide[k].t, d, (size_t)(m - d)))
			src = wide[k].src;
	db_put(&t, "\t", 1);
	db_put(&t, s, (size_t)(d - s));
	for (const char *q = m; q < c; q++) {
		db_put(&t, q, 1);
		if ((*q == 'v' || *q == 'V') && (q == m || (!isalnum((unsigned char)q[-1]) && q[-1] != '_')) &&
		    q + 1 < c && isdigit((unsigned char)q[1])) {
			const char *r = q + 1;

			while (r < c && isdigit((unsigned char)r[0]))
				r++;
			if (r < c && (isalnum((unsigned char)*r) || *r == '_' || *r == '.'))
				continue;
			db_put(&t, q + 1, (size_t)(r - q - 1));
			if (src && nv++)
				db_s(&t, src);
			else
				db_put(&t, d, (size_t)(m - d));
			q = r - 1;
		}
	}
	dt_operands(b, t.p, t.p + t.n);
	free(t.p);
	return true;
}

char *macho_dialect_translate(const char *t, size_t n, size_t *outn)
{
	DBuf b = { 0 };
	static const char *const drop[] = {
		".build_version", ".macosx_version_min", ".ios_version_min", ".subsections_via_symbols",
		".loh", ".data_region", ".end_data_region", ".no_dead_strip", ".alt_entry",
	};

	for (const char *p = t, *e = t + n; p < e;) {
		const char *nl = memchr(p, '\n', (size_t)(e - p));
		const char *le = nl ? nl : e;
		const char *c = p;
		bool quote = false;

		/* ';' starts a comment (outside strings) */
		for (; c < le; c++) {
			if (*c == '"' && (c == p || c[-1] != '\\'))
				quote = !quote;
			else if (*c == ';' && !quote)
				break;
		}
		const char *s = p;

		while (s < c && (*s == ' ' || *s == '\t'))
			s++;
		bool done = false, dropped = false;

		if (s < c && *s == '.') {
			for (size_t k = 0; k < sizeof drop / sizeof drop[0] && !done; k++)
				if (dt_word(s, c, drop[k]))
					done = dropped = true;
			if (!done && dt_word(s, c, ".section")) {
				const char *a = s + 8;

				while (a < c && (*a == ' ' || *a == '\t'))
					a++;
				if (a + 1 < c && a[0] == '_' && a[1] == '_') {
					dt_section(&b, a, c);
					done = true;
				}
			}
			if (!done && !strncmp(s, ".cfi_", 5)) {
				db_s(&b, "\n");	/* no unwind info in vcc's Mach-O yet */
				done = true;
			}
			if (!done && (dt_word(s, c, ".zerofill") || dt_word(s, c, ".tbss"))) {
				const char *f[8], *fe[8];
				bool tb = s[1] == 't';
				int k = dt_fields(s + (tb ? 5 : 9), c, f, fe);

				if (tb && k >= 2)
					dt_zero(&b, ".tbss,\"awT\",@nobits", f[0], fe[0], dt_num(f[1], fe[1]),
						k > 2 ? dt_num(f[2], fe[2]) : 0);
				else if (!tb && k >= 4)
					dt_zero(&b, fe[1] - f[1] == 12 && !memcmp(f[1], "__thread_bss", 12) ?
						".tbss,\"awT\",@nobits" : ".bss", f[2], fe[2], dt_num(f[3], fe[3]),
						k > 4 ? dt_num(f[4], fe[4]) : 0);
				db_s(&b, "\n");
				done = true;
			}
			if (!done && dt_word(s, c, ".comm")) {
				const char *f[8], *fe[8];
				int k = dt_fields(s + 5, c, f, fe);
				char t[48];

				db_s(&b, "\t.comm\t");
				dt_operands(&b, f[0], fe[0]);
				snprintf(t, sizeof t, ", %ld, %ld\n", k > 1 ? dt_num(f[1], fe[1]) : 0,
					 1L << (k > 2 ? dt_num(f[2], fe[2]) : 0));
				db_s(&b, t);
				done = true;
			}
			if (!done && dt_word(s, c, ".private_extern")) {
				db_s(&b, "\t.hidden");
				dt_operands(&b, s + 15, c);
				db_s(&b, "\n");
				done = true;
			}
			if (!done && (dt_word(s, c, ".weak_definition") || dt_word(s, c, ".weak_reference"))) {
				db_s(&b, "\t.weak");
				dt_operands(&b, s + (s[6] == 'd' ? 16 : 15), c);
				db_s(&b, "\n");
				done = true;
			}
			if (dropped)
				db_s(&b, "\n");	/* a dropped directive: keep the line count */
		}
		if (!done && s < c && *s != '.' && dt_neon(&b, s, c)) {
			db_s(&b, "\n");
			done = true;
		}
		if (!done) {
			dt_operands(&b, p, c);
			db_s(&b, "\n");
		}
		p = le + 1;
	}
	if (!b.p)
		db_put(&b, "", 0);
	*outn = b.n;
	memset(b.p + b.n, 0, 64);
	return b.p;
}
