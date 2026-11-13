/* vcc preprocessor: #embed (C23) and __has_embed: parameters, search, the file's bytes as integer literals */
#include <errno.h>
#include "pp-internal.h"
#include "port/file.h"

/* a #embed / __has_embed parameter list (after the file name) */
typedef struct pp_embed_parameters {
	uint64_t limit, offset;
	bool has_limit, unknown, bad;
	const ptok *prefix, *suffix, *if_empty;	/* prefix(), suffix(), if_empty() token lists */
	uint32_t prefix_count, suffix_count, if_empty_count;
} pp_embed_parameters;

/* the spelling of n (0..255): pp_embed_digit_spellings[4 n] */
static char pp_embed_digit_spellings[256 * 4 + 16];
static pthread_once_t pp_embed_digits_once = PTHREAD_ONCE_INIT;

static void pp_embed_digits_init(void)
{
	for (int i = 0; i < 256; i++)
		snprintf(pp_embed_digit_spellings + 4 * i, 4, "%d", i);
}

/* tokens up to the ')' that matches the '(' just read; the list is copied to tmp */
static bool pp_embed_tokens_read(pp *p, const ptok **out, uint32_t *n, bool exp)
{
	ptok buf[256], t;
	uint32_t k = 0;
	int depth = 0;

	for (;;) {
		if (exp)
			pp_token_expanded_next(p, &t);
		else
			pp_token_raw_next(p, &t);
		if (t.kind == LEX_TOKEN_EOF)
			return false;
		if (t.kind == LEX_TOKEN_LEFT_PAREN)
			depth++;
		else if (t.kind == LEX_TOKEN_RIGHT_PAREN && depth-- == 0)
			break;
		if (k < 256)
			buf[k++] = t;
	}
	{
		ptok *c = varena_alloc(&p->temporary_region, (k ? k : 1) * sizeof(ptok));

		memcpy(c, buf, k * sizeof(ptok));
		*out = c, *n = k;
	}
	return true;
}

/* an integer parameter value: a number (after expansion) up to ')' */
static bool pp_embed_integer_read(pp *p, uint64_t *v)
{
	const ptok *l;
	uint32_t n;
	char s[64];

	if (!pp_embed_tokens_read(p, &l, &n, true))
		return false;
	if (n != 1 || l[0].kind != LEX_TOKEN_NUMBER || l[0].len >= sizeof s)
		return pp_if_tokens_evaluate(p, l, n, v);	/* a constant expression (clang) */
	memcpy(s, l[0].s, l[0].len), s[l[0].len] = 0;
	{
		char *e;
		size_t len = strlen(s);

		while (len && strchr("uUlL", s[len - 1]))
			s[--len] = 0;
		*v = strtoull(s, &e, 0);
		return *e == 0;
	}
}

/* parameters until end (')' for __has_embed, the line's end for #embed); t: the first token after the name */
static void pp_embed_parameters_read(pp *p, pp_embed_parameters *m, bool paren, uint32_t loc)
{
	ptok t;

	for (;;) {
		char nm[64];
		size_t nl = 0;

		pp_token_expanded_next(p, &t);
		if (t.kind == LEX_TOKEN_EOF || (paren && t.kind == LEX_TOKEN_RIGHT_PAREN))
			return;
		if (t.kind != LEX_TOKEN_IDENTIFIER || t.len >= 40) {
			m->bad = true;
			return;
		}
		memcpy(nm, t.s, t.len), nl = t.len;
		pp_token_expanded_next(p, &t);
		if (t.kind == LEX_TOKEN_COLON_COLON || t.kind == LEX_TOKEN_COLON) {	/* vendor::name */
			if (t.kind == LEX_TOKEN_COLON)
				pp_token_expanded_next(p, &t);
			nm[nl++] = ':', nm[nl++] = ':';
			pp_token_expanded_next(p, &t);
			if (t.kind != LEX_TOKEN_IDENTIFIER || nl + t.len >= sizeof nm) {
				m->bad = true;
				return;
			}
			memcpy(nm + nl, t.s, t.len), nl += t.len;
			pp_token_expanded_next(p, &t);
		}
		nm[nl] = 0;
		/* __x__ is x */
		if (nl > 4 && nm[0] == '_' && nm[1] == '_' && nm[nl - 1] == '_' && nm[nl - 2] == '_')
			memmove(nm, nm + 2, nl - 4), nm[nl - 4] = 0;
		if (t.kind != LEX_TOKEN_LEFT_PAREN) {
			m->bad = true;
			return;
		}
		if (!strcmp(nm, "limit")) {
			m->has_limit = pp_embed_integer_read(p, &m->limit) || (m->bad = true, false);
		} else if (!strcmp(nm, "clang::offset")) {
			if (!pp_embed_integer_read(p, &m->offset))
				m->bad = true;
		} else if (!strcmp(nm, "prefix")) {
			if (!pp_embed_tokens_read(p, &m->prefix, &m->prefix_count, false))
				m->bad = true;
		} else if (!strcmp(nm, "suffix")) {
			if (!pp_embed_tokens_read(p, &m->suffix, &m->suffix_count, false))
				m->bad = true;
		} else if (!strcmp(nm, "if_empty")) {
			if (!pp_embed_tokens_read(p, &m->if_empty, &m->if_empty_count, false))
				m->bad = true;
		} else {
			const ptok *l;
			uint32_t n;

			if (!paren)
				PP_ERROR(p, loc, "unknown embed preprocessor parameter '%s'", nm);
			m->unknown = true;
			if (!pp_embed_tokens_read(p, &l, &n, false)) {
				m->bad = true;
				return;
			}
		}
		if (m->bad)
			return;
	}
}

/* clang's #embed search: an absolute name; "x" in the includer's directory,
   then the working directory; finally --embed-dir in order (-I is not searched). */
static const lex_file *pp_embed_file_find(pp *p, const pp_context *from, const char *name, size_t nlen, bool angled, const char **path)
{
	char buf[4096];
	const lex_file *f;
	const char *fp;
	const char *sl;
	size_t dl;

	if (nlen + 3 >= sizeof(buf))
		return NULL;
	if (name[0] == '/') {
		memcpy(buf, name, nlen), buf[nlen] = 0;
		*path = pp_string_copy(p, buf, nlen);
		return pp_path_try(p->options, buf, nlen);
	}
	if (!angled) {
		fp = from && from->lex_file != &p->builtin_file ? from->spelled_path : "./";
		sl = strrchr(fp, '/');
		dl = sl ? (size_t)(sl - fp) + 1 : 0;
		if (!sl && fp[0])
			fp = "./", dl = 2;
		if (dl + nlen + 1 < sizeof(buf)) {
			memcpy(buf, fp, dl), memcpy(buf + dl, name, nlen), buf[dl + nlen] = 0;
			if ((f = pp_path_try(p->options, buf, dl + nlen))) {
				*path = pp_string_copy(p, buf, dl + nlen);
				return f;
			}
		}
		/* In particular, __FILE__ may already contain the relative directory
		 * of the source. Retrying it under that directory would double it. */
		memcpy(buf, name, nlen), buf[nlen] = 0;
		if ((f = pp_path_try(p->options, buf, nlen))) {
			*path = pp_string_copy(p, buf, nlen);
			return f;
		}
	}
	for (int k = 0; k < p->options->nedirs; k++) {	/* --embed-dir, in order */
		const char *d = p->options->edirs[k];
		size_t l = strlen(d);

		if (l + nlen + 2 >= sizeof(buf))
			continue;
		memcpy(buf, d, l);
		if (l && buf[l - 1] != '/')
			buf[l++] = '/';
		memcpy(buf + l, name, nlen), l += nlen, buf[l] = 0;
		if ((f = pp_path_try(p->options, buf, l))) {
			*path = pp_string_copy(p, buf, l);
			return f;
		}
	}
	return NULL;
}

/* path names the main file (clang lists it once in -M) */
static bool pp_embed_path_is_main(const pp *p, const char *path)
{
	const char *m = p->options->main;

	if (!m)
		return false;
	while (path[0] == '.' && path[1] == '/')
		path += 2;
	while (m[0] == '.' && m[1] == '/')
		m += 2;
	return !strcmp(path, m);
}

/* the file's bytes as they are on disk: the lexer's copy (pp_embed_file_find) has a BOM
   blanked and, in C23, the separators of numbers removed */
static bool pp_embed_file_map(const pp *p, const char *path, vfile_mapping *m)
{
	char abs[8192];
	struct stat st;
	size_t cl, l = strlen(path);

	if (p->options->cwd && path[0] != '/') {
		cl = strlen(p->options->cwd);
		if (cl + l + 2 > sizeof(abs))
			return false;
		memcpy(abs, p->options->cwd, cl);
		abs[cl] = '/';
		memcpy(abs + cl + 1, path, l + 1);
		path = abs;
	}
	return vfile_map(path, m, &st);
}

/* the bytes #embed takes from a file of sz bytes: [*b, *b + *n) */
static void pp_embed_range_compute(uint64_t sz, const pp_embed_parameters *m, uint64_t *b, uint64_t *n)
{

	*b = m->offset < sz ? m->offset : sz;
	*n = sz - *b;
	if (m->has_limit && m->limit < *n)
		*n = m->limit;
}

/* #embed: the file's bytes as a comma-separated list of integer literals */
VPRIV void pp_embed_run(pp *p, pp_context *c, uint32_t b, uint32_t e, uint32_t loc, uint32_t nloc)
{
	char buf[4096];
	size_t n;
	bool angled, ok;
	const char *path;
	const lex_file *f;
	int d = p->context_depth;
	ptok t;
	uint32_t floc;
	pp_embed_parameters m = { 0 };
	uint64_t ob, on;
	vfile_mapping raw_;

	pthread_once(&pp_embed_digits_once, pp_embed_digits_init);
	if (!p->options->c23) {
		diag_info di = { "warning", nloc, NULL, 0, NULL, 0, NULL, 0 };

		diag_warning_report(p, "c23-extensions", true, NULL, &di, "#embed is a C23 extension");
	}
	pp_context_directive_line_push(p, c, b, e);
	pp_token_raw_next(p, &t);
	floc = t.loc;
	p->in_directive++;
	ok = (t.kind == LEX_TOKEN_STRING || t.kind == LEX_TOKEN_LESS || t.kind == LEX_TOKEN_IDENTIFIER) && pp_header_name_read(p, buf, sizeof(buf), &n, &angled, &t);
	if (ok)
		pp_embed_parameters_read(p, &m, false, loc);
	p->in_directive--;
	pp_context_pop_to(p, d);
	if (!ok) {
		PP_ERROR(p, loc, "expected \"FILENAME\" or <FILENAME>");
		return;
	}
	if (m.bad || m.unknown)
		return;
	f = pp_embed_file_find(p, c, buf, n, angled, &path);
	if (!f) {
		diag_info di = { "fatal error", floc, NULL, 0, NULL, 0, NULL, 0 };	/* (clang: no range for #embed) */

		diag_report_extended(p, &di, "'%.*s' file not found", (int)n, buf);
		if (p->fatal_jump && !p->options->fatal_continue)
			longjmp(*p->fatal_jump, 1);
		return;
	}
	if (p->options->deps && !pp_embed_path_is_main(p, path))
		pp_dependency_add(p, path, false);
	if (!pp_embed_file_map(p, path, &raw_)) {
		diag_info di = { "fatal error", floc, NULL, 0, NULL, 0, NULL, 0 };

		diag_report_extended(p, &di, "cannot open file '%s': %s", path, strerror(errno));
		if (p->fatal_jump && !p->options->fatal_continue)
			longjmp(*p->fatal_jump, 1);
		return;
	}
	pp_embed_range_compute(raw_.n, &m, &ob, &on);
	if (p->recording.level_count)	/* the header cache does not record #embed's bytes: the open regions are not cached */
		p->recording.uncacheable_levels = p->recording.level_count;
	{
		uint64_t nt = on ? m.prefix_count + 2 * on - 1 + m.suffix_count : m.if_empty_count;
		ptok *o, *w;

		if (!nt) {
			vfile_unmap(&raw_);
			return;
		}
		o = w = varena_alloc(&p->temporary_region, nt * sizeof(ptok));
		if (on) {
			if (m.prefix_count)	/* no prefix: m.pre is NULL, and memcpy's arguments are nonnull */
				memcpy(w, m.prefix, m.prefix_count * sizeof(ptok)), w += m.prefix_count;
			for (uint64_t i = 0; i < on; i++) {
				unsigned char v = raw_.p[ob + i];

				if (i) {
					*w++ = (ptok){ ",", 1, 0, loc, LEX_TOKEN_COMMA, 0, 0 };
				}
				*w++ = (ptok){ pp_embed_digit_spellings + 4 * v, (uint32_t)strlen(pp_embed_digit_spellings + 4 * v), 0, loc, LEX_TOKEN_NUMBER,
					       (uint8_t)(i ? LEX_TOKEN_FLAG_SPACE_BEFORE : 0), 0 };
			}
			w[-1].flags |= PP_TOKEN_FLAG_EMBED_END;
			if (m.suffix_count)
				memcpy(w, m.suffix, m.suffix_count * sizeof(ptok)), w += m.suffix_count;
		} else {
			memcpy(w, m.if_empty, m.if_empty_count * sizeof(ptok)), w += m.if_empty_count;
		}
		w[-1].flags |= PP_TOKEN_FLAG_EMBED_END;
		vfile_unmap(&raw_);
		p->embed_tail = !on ? 1 : m.suffix_count ? 2 : 0;	/* markers left: 2 bytes then suffix, 1 if_empty/suffix */
		pp_context_tokens_push(p, o, (uint32_t)(w - o), 0, 0, 0);
	}
}

/* __has_embed( name params ): 0 not found (or an unknown parameter), 1 found, 2 empty */
VPRIV int pp_embed_has_query(pp *p, const pp_context *fc, uint32_t loc)
{
	char buf[4096];
	size_t n;
	bool angled;
	const char *path;
	const lex_file *f;
	ptok r;
	pp_embed_parameters m = { 0 };
	uint64_t ob, on;

	pp_token_raw_next(p, &r);
	if (r.kind != LEX_TOKEN_LEFT_PAREN) {
		PP_ERROR(p, loc, "missing '(' after __has_embed");
		return 0;
	}
	pp_token_raw_next(p, &r);
	if (!pp_header_name_read(p, buf, sizeof(buf), &n, &angled, &r)) {
		PP_ERROR(p, loc, "invalid __has_embed argument");
		return 0;
	}
	pp_embed_parameters_read(p, &m, true, loc);
	if (m.bad || m.unknown)
		return 0;
	f = pp_embed_file_find(p, fc, buf, n, angled, &path);
	if (!f)
		return 0;
	if (p->options->deps && !pp_embed_path_is_main(p, path))
		pp_dependency_add(p, path, false);
	vfile_mapping raw_;

	if (!pp_embed_file_map(p, path, &raw_))
		return 0;
	pp_embed_range_compute(raw_.n, &m, &ob, &on);
	vfile_unmap(&raw_);
	return on ? 1 : 2;
}
