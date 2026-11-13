/* The linker script's character scanner: skipping, tokens, names and patterns. */

/* ---------------- script parser ---------------- */


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
		while (ctx->script.cursor < ctx->script.end && (*ctx->script.cursor == ' ' || *ctx->script.cursor == '\t' || *ctx->script.cursor == '\n' || *ctx->script.cursor == '\r'))
			ctx->script.cursor++;
		if (ctx->script.cursor + 1 < ctx->script.end && ctx->script.cursor[0] == '/' && ctx->script.cursor[1] == '*') {
			const char *e = strstr(ctx->script.cursor + 2, "*/");
			ctx->script.cursor = e ? e + 2 : ctx->script.end;
			continue;
		}
		break;
	}
}
static bool ld_script_is_at_end(void) { ld_script_whitespace_skip(); return ctx->script.cursor >= ctx->script.end; }
static bool ld_script_character_peek(char c) { ld_script_whitespace_skip(); return ctx->script.cursor < ctx->script.end && *ctx->script.cursor == c; }
static bool ld_script_character_consume(char c)
{
	if (ld_script_character_peek(c)) {
		ctx->script.cursor++;
		return true;
	}
	return false;
}
static void ld_script_error_report(const char *what)
{
	int line = 1;
	for (const char *p = ctx->script.file; p < ctx->script.cursor; p++)
		line += *p == '\n';
	ld_error_report("linker script line %d: %s near '%.20s'", line, what, ctx->script.cursor);
}
static void ld_script_character_expect(char c)
{
	if (!ld_script_character_consume(c)) {
		char m[32];
		snprintf(m, sizeof m, "expected '%c'", c);
		ld_script_error_report(m);
		ctx->script.cursor = ctx->script.end;
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
	const char *b = ctx->script.cursor;
	if (ctx->script.cursor < ctx->script.end && *ctx->script.cursor == '"') {
		const char *e = memchr(ctx->script.cursor + 1, '"', ctx->script.end - ctx->script.cursor - 1);
		if (!e) { ld_script_error_report("unterminated string"); ctx->script.cursor = ctx->script.end; return ld_region_string_duplicate(""); }
		ctx->script.cursor = e + 1;
		return ld_script_text_duplicate(b + 1, e);
	}
	while (ctx->script.cursor < ctx->script.end && !(ld_script_character_classes[(unsigned char)*ctx->script.cursor] & LD_CHARACTER_NAME_END))
		ctx->script.cursor++;
	return ld_script_text_duplicate(b, ctx->script.cursor);
}
/* name inside a pattern list or ADDR(): up to whitespace or parens */
static char *ld_script_pattern_name_parse(void)
{
	ld_script_whitespace_skip();
	const char *b = ctx->script.cursor;
	while (ctx->script.cursor < ctx->script.end && !(ld_script_character_classes[(unsigned char)*ctx->script.cursor] & LD_CHARACTER_PATTERN_END))
		ctx->script.cursor++;
	return ld_script_text_duplicate(b, ctx->script.cursor);
}
static inline __attribute__((always_inline)) bool ld_script_word_consume(const char *w)
{
	ld_script_whitespace_skip();
	size_t n = strlen(w);
	if ((size_t)(ctx->script.end - ctx->script.cursor) >= n && !memcmp(ctx->script.cursor, w, n) &&
	    (ctx->script.cursor + n == ctx->script.end || !(ld_script_character_classes[(unsigned char)ctx->script.cursor[n]] & LD_CHARACTER_WORD))) {
		ctx->script.cursor += n;
		return true;
	}
	return false;
}
