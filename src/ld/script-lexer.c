/* The linker script's character scanner: skipping, tokens, names and patterns. */

/* ---------------- script parser ---------------- */

static const char *sp, *sp_end, *sp_file;

/* character classes of the script lexer (NUL behaves as strchr() made it) */
enum { CT_NAMEEND = 1, CT_PATEND = 2, CT_IDENT = 4, CT_WORD = 8 };
#define CT_AN (CT_IDENT | CT_WORD)
static const uint8_t ctab[256] = {
	[0] = CT_NAMEEND | CT_PATEND | CT_IDENT,
	[' '] = CT_NAMEEND | CT_PATEND, ['\t'] = CT_NAMEEND | CT_PATEND,
	['\r'] = CT_NAMEEND | CT_PATEND, ['\n'] = CT_NAMEEND | CT_PATEND,
	['('] = CT_NAMEEND | CT_PATEND, [')'] = CT_NAMEEND | CT_PATEND,
	['{'] = CT_NAMEEND, ['}'] = CT_NAMEEND, [':'] = CT_NAMEEND, [';'] = CT_NAMEEND,
	[','] = CT_NAMEEND, ['='] = CT_NAMEEND,
	['0' ... '9'] = CT_AN, ['A' ... 'Z'] = CT_AN, ['a' ... 'z'] = CT_AN,
	['_'] = CT_AN, ['.'] = CT_AN, ['$'] = CT_IDENT,
};

static void sskip(void)
{
	for (;;) {
		while (sp < sp_end && (*sp == ' ' || *sp == '\t' || *sp == '\n' || *sp == '\r'))
			sp++;
		if (sp + 1 < sp_end && sp[0] == '/' && sp[1] == '*') {
			const char *e = strstr(sp + 2, "*/");
			sp = e ? e + 2 : sp_end;
			continue;
		}
		break;
	}
}
static bool satend(void) { sskip(); return sp >= sp_end; }
static bool speek(char c) { sskip(); return sp < sp_end && *sp == c; }
static bool sconsume(char c)
{
	if (speek(c)) {
		sp++;
		return true;
	}
	return false;
}
static void serr(const char *what)
{
	int line = 1;
	for (const char *p = sp_file; p < sp; p++)
		line += *p == '\n';
	error("linker script line %d: %s near '%.20s'", line, what, sp);
}
static void sexpect(char c)
{
	if (!sconsume(c)) {
		char m[32];
		snprintf(m, sizeof m, "expected '%c'", c);
		serr(m);
		sp = sp_end;
	}
}
static char *sdup(const char *b, const char *e)
{
	char *p = alloc(e - b + 1);
	memcpy(p, b, e - b);
	p[e - b] = 0;
	return p;
}
/* statement-level name: up to whitespace or one of (){}:;,= */
static char *sname(void)
{
	sskip();
	const char *b = sp;
	if (sp < sp_end && *sp == '"') {
		const char *e = memchr(sp + 1, '"', sp_end - sp - 1);
		if (!e) { serr("unterminated string"); sp = sp_end; return astrdup(""); }
		sp = e + 1;
		return sdup(b + 1, e);
	}
	while (sp < sp_end && !(ctab[(unsigned char)*sp] & CT_NAMEEND))
		sp++;
	return sdup(b, sp);
}
/* name inside a pattern list or ADDR(): up to whitespace or parens */
static char *spatname(void)
{
	sskip();
	const char *b = sp;
	while (sp < sp_end && !(ctab[(unsigned char)*sp] & CT_PATEND))
		sp++;
	return sdup(b, sp);
}
static inline __attribute__((always_inline)) bool sword(const char *w)
{
	sskip();
	size_t n = strlen(w);
	if ((size_t)(sp_end - sp) >= n && !memcmp(sp, w, n) &&
	    (sp + n == sp_end || !(ctab[(unsigned char)sp[n]] & CT_WORD))) {
		sp += n;
		return true;
	}
	return false;
}
