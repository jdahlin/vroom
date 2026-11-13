/* The linker script's character scanner: skipping, tokens, names and patterns. */

/* ---------------- script parser ---------------- */

static const char *sp, *sp_end, *sp_file;

/* character classes of the script lexer (NUL behaves as strchr() made it) */
enum { LD_CHARACTER_NAME_END = 1, LD_CHARACTER_PATTERN_END = 2, LD_CHARACTER_IDENTIFIER = 4, LD_CHARACTER_WORD = 8 };
#define LD_CHARACTER_ALPHANUMERIC (LD_CHARACTER_IDENTIFIER | LD_CHARACTER_WORD)
static const uint8_t ld_script_character_classes[256] = {
	[0] = LD_CHARACTER_NAME_END | LD_CHARACTER_PATTERN_END | LD_CHARACTER_IDENTIFIER,
	[' '] = LD_CHARACTER_NAME_END | LD_CHARACTER_PATTERN_END, ['\t'] = LD_CHARACTER_NAME_END | LD_CHARACTER_PATTERN_END,
	['\r'] = LD_CHARACTER_NAME_END | LD_CHARACTER_PATTERN_END, ['\n'] = LD_CHARACTER_NAME_END | LD_CHARACTER_PATTERN_END,
	['('] = LD_CHARACTER_NAME_END | LD_CHARACTER_PATTERN_END, [')'] = LD_CHARACTER_NAME_END | LD_CHARACTER_PATTERN_END,
	['{'] = LD_CHARACTER_NAME_END, ['}'] = LD_CHARACTER_NAME_END, [':'] = LD_CHARACTER_NAME_END, [';'] = LD_CHARACTER_NAME_END,
	[','] = LD_CHARACTER_NAME_END, ['='] = LD_CHARACTER_NAME_END,
	['0' ... '9'] = LD_CHARACTER_ALPHANUMERIC, ['A' ... 'Z'] = LD_CHARACTER_ALPHANUMERIC, ['a' ... 'z'] = LD_CHARACTER_ALPHANUMERIC,
	['_'] = LD_CHARACTER_ALPHANUMERIC, ['.'] = LD_CHARACTER_ALPHANUMERIC, ['$'] = LD_CHARACTER_IDENTIFIER,
};

static void ld_script_whitespace_skip(void)
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
static bool ld_script_is_at_end(void) { ld_script_whitespace_skip(); return sp >= sp_end; }
static bool ld_script_character_peek(char c) { ld_script_whitespace_skip(); return sp < sp_end && *sp == c; }
static bool ld_script_character_consume(char c)
{
	if (ld_script_character_peek(c)) {
		sp++;
		return true;
	}
	return false;
}
static void ld_script_error_report(const char *what)
{
	int line = 1;
	for (const char *p = sp_file; p < sp; p++)
		line += *p == '\n';
	ld_error_report("linker script line %d: %s near '%.20s'", line, what, sp);
}
static void ld_script_character_expect(char c)
{
	if (!ld_script_character_consume(c)) {
		char m[32];
		snprintf(m, sizeof m, "expected '%c'", c);
		ld_script_error_report(m);
		sp = sp_end;
	}
}
static char *ld_script_text_duplicate(const char *b, const char *e)
{
	char *p = ld_region_alloc(e - b + 1);
	memcpy(p, b, e - b);
	p[e - b] = 0;
	return p;
}
/* statement-level name: up to whitespace or one of (){}:;,= */
static char *ld_script_name_parse(void)
{
	ld_script_whitespace_skip();
	const char *b = sp;
	if (sp < sp_end && *sp == '"') {
		const char *e = memchr(sp + 1, '"', sp_end - sp - 1);
		if (!e) { ld_script_error_report("unterminated string"); sp = sp_end; return ld_region_string_duplicate(""); }
		sp = e + 1;
		return ld_script_text_duplicate(b + 1, e);
	}
	while (sp < sp_end && !(ld_script_character_classes[(unsigned char)*sp] & LD_CHARACTER_NAME_END))
		sp++;
	return ld_script_text_duplicate(b, sp);
}
/* name inside a pattern list or ADDR(): up to whitespace or parens */
static char *ld_script_pattern_name_parse(void)
{
	ld_script_whitespace_skip();
	const char *b = sp;
	while (sp < sp_end && !(ld_script_character_classes[(unsigned char)*sp] & LD_CHARACTER_PATTERN_END))
		sp++;
	return ld_script_text_duplicate(b, sp);
}
static inline __attribute__((always_inline)) bool ld_script_word_consume(const char *w)
{
	ld_script_whitespace_skip();
	size_t n = strlen(w);
	if ((size_t)(sp_end - sp) >= n && !memcmp(sp, w, n) &&
	    (sp + n == sp_end || !(ld_script_character_classes[(unsigned char)sp[n]] & LD_CHARACTER_WORD))) {
		sp += n;
		return true;
	}
	return false;
}
