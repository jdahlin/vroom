/* The linker script's character scanner: skipping, tokens, names and patterns. */

#include "ld-internal.h"

/* ---------------- script parser ---------------- */


VPRIV const uint8_t ld_script_character_classes[256] = {
	[0] = LD_CHARACTER_NAME_END | LD_CHARACTER_PATTERN_END | LD_CHARACTER_IDENTIFIER,
	[' '] = LD_CHARACTER_NAME_END | LD_CHARACTER_PATTERN_END, ['\t'] = LD_CHARACTER_NAME_END | LD_CHARACTER_PATTERN_END,
	['\r'] = LD_CHARACTER_NAME_END | LD_CHARACTER_PATTERN_END, ['\n'] = LD_CHARACTER_NAME_END | LD_CHARACTER_PATTERN_END,
	['('] = LD_CHARACTER_NAME_END | LD_CHARACTER_PATTERN_END, [')'] = LD_CHARACTER_NAME_END | LD_CHARACTER_PATTERN_END,
	['{'] = LD_CHARACTER_NAME_END, ['}'] = LD_CHARACTER_NAME_END, [':'] = LD_CHARACTER_NAME_END, [';'] = LD_CHARACTER_NAME_END,
	[','] = LD_CHARACTER_NAME_END, ['='] = LD_CHARACTER_NAME_END,
	['0' ... '9'] = LD_CHARACTER_ALPHANUMERIC, ['A' ... 'Z'] = LD_CHARACTER_ALPHANUMERIC, ['a' ... 'z'] = LD_CHARACTER_ALPHANUMERIC,
	['_'] = LD_CHARACTER_ALPHANUMERIC, ['.'] = LD_CHARACTER_ALPHANUMERIC, ['$'] = LD_CHARACTER_IDENTIFIER,
};

VPRIV void ld_script_whitespace_skip(struct ld_context *ctx)
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
VPRIV bool ld_script_is_at_end(struct ld_context *ctx) { ld_script_whitespace_skip(ctx); return ctx->script.cursor >= ctx->script.end; }
VPRIV bool ld_script_character_peek(struct ld_context *ctx, char c) { ld_script_whitespace_skip(ctx); return ctx->script.cursor < ctx->script.end && *ctx->script.cursor == c; }
VPRIV bool ld_script_character_consume(struct ld_context *ctx, char c)
{
	if (ld_script_character_peek(ctx, c)) {
		ctx->script.cursor++;
		return true;
	}
	return false;
}
VPRIV void ld_script_error_report(struct ld_context *ctx, const char *what)
{
	int line = 1;
	for (const char *p = ctx->script.file; p < ctx->script.cursor; p++)
		line += *p == '\n';
	ld_error_report(ctx, "linker script line %d: %s near '%.20s'", line, what, ctx->script.cursor);
}
VPRIV void ld_script_character_expect(struct ld_context *ctx, char c)
{
	if (!ld_script_character_consume(ctx, c)) {
		char m[32];
		snprintf(m, sizeof m, "expected '%c'", c);
		ld_script_error_report(ctx, m);
		ctx->script.cursor = ctx->script.end;
	}
}
VPRIV char *ld_script_text_duplicate(const char *b, const char *e)
{
	char *p = ld_region_alloc(e - b + 1);
	memcpy(p, b, e - b);
	p[e - b] = 0;
	return p;
}
/* statement-level name: up to whitespace or one of (){}:;,= */
VPRIV char *ld_script_name_parse(struct ld_context *ctx)
{
	ld_script_whitespace_skip(ctx);
	const char *b = ctx->script.cursor;
	if (ctx->script.cursor < ctx->script.end && *ctx->script.cursor == '"') {
		const char *e = memchr(ctx->script.cursor + 1, '"', ctx->script.end - ctx->script.cursor - 1);
		if (!e) { ld_script_error_report(ctx, "unterminated string"); ctx->script.cursor = ctx->script.end; return ld_region_string_duplicate(""); }
		ctx->script.cursor = e + 1;
		return ld_script_text_duplicate(b + 1, e);
	}
	while (ctx->script.cursor < ctx->script.end && !(ld_script_character_classes[(unsigned char)*ctx->script.cursor] & LD_CHARACTER_NAME_END))
		ctx->script.cursor++;
	return ld_script_text_duplicate(b, ctx->script.cursor);
}
/* name inside a pattern list or ADDR(): up to whitespace or parens */
VPRIV char *ld_script_pattern_name_parse(struct ld_context *ctx)
{
	ld_script_whitespace_skip(ctx);
	const char *b = ctx->script.cursor;
	while (ctx->script.cursor < ctx->script.end && !(ld_script_character_classes[(unsigned char)*ctx->script.cursor] & LD_CHARACTER_PATTERN_END))
		ctx->script.cursor++;
	return ld_script_text_duplicate(b, ctx->script.cursor);
}
