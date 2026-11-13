/* The parser's source-text queries: limits, include point, token text and extent, `defined`. */
#include "../pp/pp-internal.h"
#include "common/buffer.h"
#include <ctype.h>
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

int pp_option_error_limit(const pp *p) { return p->o->error_limit; }
int pp_option_spell_limit(const pp *p) { return p->o->spell_limit; }

uint32_t pp_main_file_end(const pp *p)
{
	const pp_file_entry *e = &p->fe[0];
	uint32_t n = e->f->size;

	return e->base + (n && e->f->src[n - 1] == '\n' ? n - 1 : n);
}

void pp_macro_text_keep(pp *p) { p->keep_macro_text = true; }

/* pp_token_spelling_find (pp.h): the file whose text holds the token's bytes, by pointer */
int pp_token_spelling_find(const pp *p, const ptok *t, const lex_file **file, uint32_t *offset)
{
	const pp_file_entry *e;
	const lex_file *f = NULL;

	if (t->flags & LEX_TOKEN_FLAG_SPLICED)
		return PP_SPELLING_DIRTY;
	for (uint32_t i = p->nfe; i-- > 0 && !f;) {	/* (the newest file first: uses cluster) */
		const lex_file *g = p->fe[i].f;

		if (g && t->s >= g->src && t->s < g->src + g->size)
			f = g;
	}
	if (!f)
		return PP_SPELLING_NONE;
	*file = f;
	*offset = (uint32_t)(t->s - f->src);
	return pp_location_is_valid(p, t->loc, &e) && e->f == f && t->loc - e->base == *offset ? PP_SPELLING_FILE : PP_SPELLING_MACRO;
}

/* where an #include for a diagnostic at loc goes: the start of the line after the last
   #include before loc in loc's file, else the file's first line; UINT32_MAX: not in a file */
uint32_t pp_include_point_find(const pp *p, uint32_t loc)
{
	const pp_file_entry *e;
	const char *s;
	uint32_t end, at = 0, i = 0;

	if (!pp_location_is_valid(p, loc, &e))
		return UINT32_MAX;
	s = e->f->src;
	end = loc - e->base;
	while (i < end) {
		uint32_t j = i;

		while (j < end && (s[j] == ' ' || s[j] == '\t'))
			j++;
		if (j < end && s[j] == '#') {
			j++;
			while (j < end && (s[j] == ' ' || s[j] == '\t'))
				j++;
			if (end - j > 7 && !memcmp(s + j, "include", 7)) {
				const char *nl = memchr(s + j, '\n', end - j);

				if (!nl)
					break;
				at = (uint32_t)(nl - s) + 1;
			}
		}
		{
			const char *nl = memchr(s + i, '\n', end - i);

			if (!nl)
				break;
			i = (uint32_t)(nl - s) + 1;
		}
	}
	return e->base + at;
}

const char *pp_source_text(const pp *p, uint32_t b, uint32_t e, size_t *len)
{
	const pp_file_entry *f;

	if (e < b || !pp_location_is_valid(p, b, &f) || e - f->base > f->f->size)
		return NULL;
	*len = e - b;
	return f->f->src + (b - f->base);
}

/* -Winvalid-pp-token: an unknown token that is an unterminated character constant or string */
void pp_token_invalid_report(pp *p, const char *s, uint32_t len, uint32_t loc)
{
	uint32_t i = 0;

	if (i < len && (s[i] == 'L' || s[i] == 'U' || s[i] == 'u'))
		i++;
	if (i < len && s[0] == 'u' && s[i] == '8')
		i++;
	if (i >= len || (s[i] != '\'' && s[i] != '"'))
		return;
	{
		diag_info d = { "warning", loc + i, NULL, 0, NULL, 0, NULL, 0 };

		diag_warning_report(p, "invalid-pp-token", true, NULL, &d, "missing terminating %s character", s[i] == '"' ? "'\"'" : "'");
	}
}

/* the source at loc spells s[0, n) as a whole token (not a macro's expansion of it) */
int pp_location_spells(const pp *p, uint32_t loc, const char *s, uint32_t n)
{
	const pp_file_entry *e;

	if (!pp_location_is_valid(p, loc, &e) || loc - e->base + n > e->f->size || memcmp(e->f->src + (loc - e->base), s, n))
		return 0;
	return pp_token_end_find(p, loc) == loc + n;
}

/* the source character at loc, -1 if none */
int pp_location_char(const pp *p, uint32_t loc)
{
	const pp_file_entry *e;

	if (!pp_location_is_valid(p, loc, &e) || loc - e->base >= e->f->size)
		return -1;
	return (unsigned char)e->f->src[loc - e->base];
}

const char *pp_source_rest(const pp *p, uint32_t b, size_t *len)
{
	const pp_file_entry *f;

	if (!pp_location_is_valid(p, b, &f))
		return NULL;
	*len = f->f->size - (b - f->base);
	return f->f->src + (b - f->base);
}

uint32_t pp_token_end_find(const pp *p, uint32_t loc)
{
	const pp_file_entry *e;
	const char *s, *end;
	uint32_t off;

	if (!pp_location_is_valid(p, loc, &e))
		return loc + 1;
	s = e->f->src, off = loc - e->base, end = s + e->f->size;
	if (s + off >= end)
		return loc + 1;
	{
		const char *q = s + off;
		unsigned char ch = (unsigned char)*q;

		if (ch == '_' || ch == '$' || ch >= 0x80 || (ch | 0x20) - 'a' < 26u || ch - '0' < 10u) {
			/* identifier or pp-number (1e+5, 0x1p-3) */
			bool num = ch - '0' < 10u;

			for (q++; q < end; q++) {
				unsigned char d = (unsigned char)*q;

				if (d == '_' || d == '$' || d >= 0x80 || (d | 0x20) - 'a' < 26u || d - '0' < 10u || (num && d == '.'))
					continue;
				if (num && (d == '+' || d == '-') && strchr("eEpP", q[-1]))
					continue;
				break;
			}
			return loc + (uint32_t)(q - (s + off));
		}
		if (ch == '"' || ch == '\'') {
			for (q++; q < end && *q != (char)ch && *q != '\n'; q++)
				if (*q == '\\')
					q++;
			return loc + (uint32_t)(q + 1 - (s + off));
		}
		{
			static const char *const ops[] = { "<<=", ">>=", "...", "->", "++", "--", "<<", ">>", "<=", ">=", "==", "!=",
							   "&&", "||", "*=", "/=", "%=", "+=", "-=", "&=", "^=", "|=", "##" };

			for (size_t i = 0; i < sizeof(ops) / sizeof(ops[0]); i++) {
				size_t l = strlen(ops[i]);

				if ((size_t)(end - q) >= l && !memcmp(q, ops[i], l))
					return loc + (uint32_t)l;
			}
		}
	}
	return loc + 1;
}

int pp_macro_id_is_defined(pp *p, uint32_t id)
{
	return pp_macro_is_defined(p, id);
}
