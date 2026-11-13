/* vcc preprocessor: directives: conditionals, #pragma, #line, the dispatcher */
#include "pp-internal.h"

/* ---------------- directives ---------------- */

/* -Wextra-tokens: tokens after a conditional's (or #undef's) operands */
static void pp_directive_extra_tokens_location(pp *p, uint32_t loc, const char *name, int nl)
{
	diag_span r = { loc, loc };
	diag_fixit fx = { loc, loc, "//", 0 };
	diag_info di = { "warning", loc, &r, 0, &fx, 1, NULL, 0 };

	diag_warning_report(p, "extra-tokens", true, NULL, &di, "extra tokens at end of #%.*s directive", nl, name);
}

VPRIV __attribute__((noinline)) void pp_directive_extra_token_warn(pp *p, const pp_context *c, uint32_t name, uint32_t i)
{
	uint32_t loc = c->loc + c->vt[i].off;
	diag_span r = { loc, loc };
	diag_fixit fx = { loc, loc, "//", 0 };
	diag_info di = { "warning", loc, &r, 0, &fx, 1, NULL, 0 };
	const lex_token *n = &c->vt[name];

	diag_warning_report(p, "extra-tokens", true, NULL, &di, "extra tokens at end of #%.*s directive", (int)n->len, c->src + n->off);
}

static void pp_directive_extra_token_at_warn(pp *p, const pp_context *c, const pp_directive_span *d, uint32_t i)
{
	pp_directive_extra_token_warn(p, c, d->tok + 1, i);
}

static inline void pp_directive_extra_tokens_check(pp *p, const pp_context *c, const pp_directive_span *d)
{
	uint32_t i;

	switch (d->kind) {
	case PP_DIRECTIVE_ELSE: case PP_DIRECTIVE_ENDIF: i = d->tok + 2; break;
	case PP_DIRECTIVE_IFDEF: case PP_DIRECTIVE_IFNDEF: case PP_DIRECTIVE_ELIFDEF: case PP_DIRECTIVE_ELIFNDEF: case PP_DIRECTIVE_UNDEF:
		i = d->tok + 3;
		if (d->tok + 2 >= d->end || c->vt[d->tok + 2].kind != LEX_TOKEN_IDENTIFIER)
			return;
		break;
	default: return;
	}
	if (unlikely(i < d->end))
		pp_directive_extra_token_at_warn(p, c, d, i);
}

static bool pp_conditional_is_true(pp *p, const pp_context *c, const pp_directive_span *d)
{
	uint32_t b = d->tok + 2;

	switch (d->kind) {
	case PP_DIRECTIVE_IFDEF: case PP_DIRECTIVE_IFNDEF: case PP_DIRECTIVE_ELIFDEF: case PP_DIRECTIVE_ELIFNDEF: {
		bool def;

		if (b >= d->end || c->vt[b].kind != LEX_TOKEN_IDENTIFIER) {
			PP_ERROR(p, c->loc + c->vt[d->tok].off, "macro name missing");
			return false;
		}
		def = pp_macro_is_defined(p, c->vt[b].id);
		return (d->kind == PP_DIRECTIVE_IFDEF || d->kind == PP_DIRECTIVE_ELIFDEF) ? def : !def;
	}
	default:
		return pp_if_evaluate(p, c, b, d->end);
	}
}

/* in a false group: run on from directive k to the group that is taken */
/* -C: the comments before directive line [b, e) are printed, those in it dropped */
static __attribute__((noinline, cold)) void pp_directive_comments_flush(pp *p, pp_context *c, uint32_t b, uint32_t e)
{
	if (!p->collecting)	/* in macro arguments they are dropped */
		pp_comments_flush(p, c, c->src + c->vt[b].off);
	p->fe[c->fe].comment_position = pp_comment_line_end(c->src + c->vt[e - 1].off + c->vt[e - 1].len);
}

static void pp_conditional_group_skip(pp *p, pp_context *c, uint32_t k)
{
	const pp_directive_span *dir = c->fi->dir;

	for (;;) {
		const pp_directive_span *d = &dir[k];

		pp_directive_extra_tokens_check(p, c, d);
		if ((d->kind == PP_DIRECTIVE_ELIF || d->kind == PP_DIRECTIVE_ELIFDEF || d->kind == PP_DIRECTIVE_ELIFNDEF) && !pp_conditional_is_true(p, c, d)) {
			k = d->next;
			continue;
		}
		c->i = d->end;
		c->di = k + 1;
		if (unlikely(p->output_text_mode > 1))	/* -C: nothing skipped is printed */
			p->fe[c->fe].comment_position = pp_comment_line_end(c->src + c->vt[c->i - 1].off + c->vt[c->i - 1].len);
		return;
	}
}

/*
 * Parse "#define" tokens vt[b, e) into *m (pidx in memory from mem).
 * Returns 0, or an error message (*eoff: the token it is at).
 */
VPRIV const char *pp_define_parse(const lex_token *vt, uint32_t b, uint32_t e, pp_macro *m,
				void *(*mem)(void *, size_t), void *mc, uint32_t *eoff)
{
	uint32_t params[256], np = 0, i = b + 1;
	uint8_t *pidx = NULL;
	bool va = false;

	memset(m, 0, sizeof(*m));
	m->name = vt[b].id;
	m->kind = PP_MACRO_OBJECT_LIKE;
	*eoff = b;
	if (i < e && vt[i].kind == LEX_TOKEN_LEFT_PAREN && !(vt[i].flags & LEX_TOKEN_FLAG_SPACE_BEFORE)) {
		m->kind = PP_MACRO_FUNCTION_LIKE;
		i++;
		if (i < e && vt[i].kind == LEX_TOKEN_RIGHT_PAREN) {
			i++;
		} else {
			for (;;) {
				if (i >= e)
					return "invalid macro parameter list";
				if (vt[i].kind == LEX_TOKEN_ELLIPSIS) {
					params[np++] = KW_VA_ARGS;
					va = true;
					i++;
				} else if (vt[i].kind == LEX_TOKEN_IDENTIFIER && np < 256) {
					params[np++] = vt[i++].id;
					if (i < e && vt[i].kind == LEX_TOKEN_ELLIPSIS) {
						va = true;	/* GNU named variadic */
						i++;
					}
				} else {
					return "invalid macro parameter list";
				}
				if (i < e && vt[i].kind == LEX_TOKEN_RIGHT_PAREN && (i++, 1))
					break;
				if (va || i >= e || vt[i].kind != LEX_TOKEN_COMMA)
					return "invalid macro parameter list";
				i++;
			}
		}
	}
	m->body = vt + i;
	m->nbody = e - i;
	if (m->kind == PP_MACRO_FUNCTION_LIKE) {
		m->plist = vt + b + 1;
		m->nplist = (uint16_t)(i - b - 1 < 0xffff ? i - b - 1 : 0);
	}
	m->nparam = (uint16_t)np;
	if (va)
		m->flags |= PP_MACRO_FLAG_VARIADIC;
	for (uint32_t j = i; j < e; j++) {
		if (vt[j].kind == LEX_TOKEN_HASH_HASH)
			m->flags |= PP_MACRO_FLAG_PASTE;
		if (vt[j].kind == LEX_TOKEN_IDENTIFIER && vt[j].id == KW_VA_OPT)
			m->flags |= PP_MACRO_FLAG_VA_OPT;
		if (vt[j].kind == LEX_TOKEN_HASH)
			m->flags |= PP_MACRO_FLAG_HAS_HASH;
		if (vt[j].kind == LEX_TOKEN_UNKNOWN)
			m->flags |= PP_MACRO_FLAG_UNKNOWN_TOKEN;
	}
	if (m->nbody && (m->body[0].kind == LEX_TOKEN_HASH_HASH || m->body[m->nbody - 1].kind == LEX_TOKEN_HASH_HASH))
		return "'##' cannot appear at either end of a macro expansion";
	if (np) {
		pidx = mem(mc, m->nbody + 1);
		for (uint32_t j = 0; j < m->nbody; j++) {
			pidx[j] = 0;
			if (m->body[j].kind != LEX_TOKEN_IDENTIFIER)
				continue;
			for (uint32_t k = 0; k < np; k++)
				if (params[k] == m->body[j].id) {
					pidx[j] = (uint8_t)(k + 1);
					break;
				}
		}
		m->pidx = pidx;
	}
	if (m->kind == PP_MACRO_FUNCTION_LIKE)
		for (uint32_t j = 0; j < m->nbody; j++)
			if (m->body[j].kind == LEX_TOKEN_HASH && !(j + 1 < m->nbody && pidx && pidx[j + 1]) &&
			    !(j + 1 < m->nbody && m->body[j + 1].kind == LEX_TOKEN_IDENTIFIER && m->body[j + 1].id == KW_VA_OPT)) {
				m->flags |= PP_MACRO_FLAG_BAD_HASH;
				*eoff = i + j;
				break;
			}
	return NULL;
}

VPRIV void *pp_memory_permanent_allocate(void *a, size_t n) { return varena_alloc(a, n); }
VPRIV void *pp_memory_shared_allocate(void *a, size_t n) { (void)a; return pp_shared_allocate(n); }

/* the macro name in ("NAME") of push_macro/pop_macro, or 0 */
static uint32_t pp_pragma_macro_name_read(const char *s, size_t n)
{
	const char *e = s + n, *q;

	while (s < e && *s != '"')
		s++;
	if (s == e)
		return 0;
	s++;
	q = memchr(s, '"', (size_t)(e - s));
	if (!q || q == s)
		return 0;
	{
		char b[q - s + 17];

		memcpy(b, s, (size_t)(q - s));
		memset(b + (q - s), 0, 17);
		return vintern_copy(b, (size_t)(q - s));
	}
}

static bool pp_pragma_word_match(const char **s, const char *e, const char *w)
{
	const char *q = *s;
	size_t n = strlen(w);

	while (q < e && (*q == ' ' || *q == '\t'))
		q++;
	if ((size_t)(e - q) < n || memcmp(q, w, n) || (q + n < e && pp_character_is_identifier((unsigned char)q[n])))
		return false;
	*s = q + n;
	return true;
}

/* #pragma GCC warning/error: ("msg") or "msg" from q to e, at loc (the word) */
static __attribute__((cold, noinline)) void pp_pragma_message_run(pp *p, bool err, const char *q, const char *e, uint32_t loc)
{
	const char *b, *m;
	bool par = false;
	diag_info d = { err ? "error" : "warning", loc, NULL, 0, NULL, 0, NULL, 0 };

	while (q < e && (*q == ' ' || *q == '\t'))
		q++;
	if (q < e && *q == '(')
		par = true, q++;
	while (q < e && (*q == ' ' || *q == '\t'))
		q++;
	if (q >= e || *q != '"')
		goto bad;
	b = ++q;
	while (q < e && *q != '"')
		q += *q == '\\' ? 2 : 1;
	if (q >= e)
		goto bad;
	m = q++;
	if (par) {
		while (q < e && (*q == ' ' || *q == '\t'))
			q++;
		if (q >= e || *q != ')')
			goto bad;
	}
	diag_next_id = err ? "err_pragma_message" : "warn_pragma_message";
	if (err)
		diag_report_extended(p, &d, "%.*s", (int)(m - b), b);
	else if (!diag_warning_report(p, "#pragma-messages", true, NULL, &d, "%.*s", (int)(m - b), b))
		diag_next_id = NULL;	/* (off: no record took it) */
	return;
bad:
	d.what = "error";
	diag_report_extended(p, &d, "pragma %s requires parenthesized string", err ? "error" : "warning");
}

/*
 * A pragma, text "pragma ..." (from #pragma or _Pragma).  The ones the
 * preprocessor implements are run and dropped, as clang -E does; the rest
 * become a PP_TOKEN_KIND_DIRECTIVE_LINE token.  Returns 1 if *out was set.
 */
VPRIV int pp_pragma_run(pp *p, const char *s, size_t n, uint32_t loc, ptok *out, uint32_t sloc)
{
	const char *q = s + 6, *e = s + n;	/* after "pragma" */

	if (pp_pragma_word_match(&q, e, "once")) {
		const pp_context *fc = pp_file_context_current(p);

		if (fc)
			pp_once_file_add(p, fc->f);
		return 0;
	}
	if (pp_pragma_word_match(&q, e, "push_macro")) {
		uint32_t id = pp_pragma_macro_name_read(q, (size_t)(e - q));

		PP_HEADER_CACHE_REGION_REJECT(p);
		if (id && p->pushed_macro_count < 4096) {
			p->pushed[p->pushed_macro_count].id = id;
			p->pushed[p->pushed_macro_count++].m = pp_macro_read(p, id) & ~(uintptr_t)1;
		}
		return 0;
	}
	if (pp_pragma_word_match(&q, e, "pop_macro")) {
		uint32_t id = pp_pragma_macro_name_read(q, (size_t)(e - q));

		PP_HEADER_CACHE_REGION_REJECT(p);
		for (uint32_t i = p->pushed_macro_count; i-- > 0;)
			if (p->pushed[i].id == id) {
				pp_macro_define_record(p, id, (const pp_macro *)p->pushed[i].m);
				memmove(&p->pushed[i], &p->pushed[i + 1], (p->pushed_macro_count - i - 1) * sizeof(p->pushed[0]));
				p->pushed_macro_count--;
				break;
			}
		return 0;
	}
	{
		const char *r = q;

		if (pp_pragma_word_match(&r, e, "GCC") && (pp_pragma_word_match(&r, e, "system_header") || pp_pragma_word_match(&r, e, "poison") ||
					    pp_pragma_word_match(&r, e, "dependency")))
			return 0;
		r = q;
		if ((pp_pragma_word_match(&r, e, "clang") || pp_pragma_word_match(&r, e, "GCC")) && pp_pragma_word_match(&r, e, "diagnostic")) {
			PP_HEADER_CACHE_REGION_REJECT(p);	/* (a replay would not run it) */
			diag_pragma_handle(p, r, e, loc);	/* (and kept in the output, as clang -E) */
		}
		r = q;
		if (sloc && pp_pragma_word_match(&r, e, "GCC")) {	/* #pragma GCC warning/error "msg" (_Pragma: not modelled) */
			const char *w = r;
			bool err;

			while (w < e && (*w == ' ' || *w == '\t'))
				w++;
			if ((err = pp_pragma_word_match(&r, e, "error")) || pp_pragma_word_match(&r, e, "warning"))
				pp_pragma_message_run(p, err, r, e, sloc + (uint32_t)(w - s));
		}
	}
	memset(out, 0, sizeof(*out));
	out->kind = PP_TOKEN_KIND_DIRECTIVE_LINE;
	out->s = s;
	out->len = (uint32_t)n;
	out->loc = loc;
	out->flags = LEX_TOKEN_FLAG_LINE_START;
	return 1;
}

/* #line N ["file"] (macro-expanded) or # N "file" flags (GNU line marker) */
static void pp_line_directive_run(pp *p, pp_context *c, uint32_t b, uint32_t e, bool expand_line, uint32_t loc)
{
	int d = p->depth;
	const lex_token *last = &c->vt[e - 1];
	struct pp_line_mark *m;
	ptok n, f;
	pp_if_value v;

	pp_context_directive_line_push(p, c, b, e);
	if (expand_line) {
		p->in_directive++;
		pp_token_expanded_next(p, &n);
		pp_token_expanded_next(p, &f);
		p->in_directive--;
	} else {
		pp_token_raw_next(p, &n);
		pp_token_raw_next(p, &f);
	}
	if (n.kind != LEX_TOKEN_NUMBER) {
		pp_context_pop_to(p, d);
		PP_ERROR(p, loc, "#line directive requires a positive integer argument");
		return;
	}
	v = pp_number_parse(p, &n);
	if (p->nlm == 65536) {
		pp_context_pop_to(p, d);
		return;
	}
	m = &p->lm[p->nlm++];
	m->fe = c->fe;
	m->off = last->off + last->len;
	m->pline = pp_file_line_of_offset(c->f, c->fi, m->off) + 1;
	m->line = (uint32_t)v.v;
	m->name = NULL;
	if (f.kind == LEX_TOKEN_STRING) {
		char tmp[f.len + 1], *w;
		size_t l;
		const char *s = pp_token_spell(&f, tmp, &l);

		w = varena_alloc(&p->perm, l + 1);
		m->name = w;
		for (size_t i = 1; i + 1 < l; i++) {
			if (s[i] == '\\' && i + 2 < l)
				i++;
			*w++ = s[i];
		}
		m->namelen = (uint32_t)(w - m->name);
		if (expand_line) {
			ptok x;

			p->in_directive++;
			pp_token_expanded_next(p, &x);
			p->in_directive--;
			if (x.kind != LEX_TOKEN_EOF)
				pp_directive_extra_tokens_location(p, x.loc, "line", 4);
		}
	}
	pp_context_pop_to(p, d);
	if (unlikely(p->output_text_mode)) {
		/* clang's FileChanged(RenameFile): the next line is line v, and a line in progress ends */
		if (p->lmark) {
			pp_line_marker_file_rename(p, m->name, m->namelen, m->line);
			return;
		}
		p->cur_line = m->line;
		if (p->col || p->edir) {
			pp_output_newline(p);
			p->col = 0, p->edir = 0;
		}
	}
}

/* -imacros: the file is read for its macros, its tokens dropped up to the
 * "##" line after the directive in <built-in> (clang's #__include_macros) */
static __attribute__((cold, noinline)) void pp_imacros_run(pp *p, pp_context *c, uint32_t b, uint32_t e, uint32_t loc)
{
	int nfile = p->nfile;
	ptok t;

	pp_include_run(p, c, b + 1, e, PP_DIRECTIVE_INCLUDE, loc);
	do
		pp_token_expanded_next(p, &t);
	while (t.kind != LEX_TOKEN_EOF && !(t.kind == LEX_TOKEN_HASH_HASH && p->nfile == nfile));
}

/* run the directive at c->i; 1 if it produced a token in *out */
VPRIV int pp_directive_run(pp *p, pp_context *c, ptok *out)
{
	const pp_directive_span *d = &c->fi->dir[c->di];
	uint32_t b = d->tok + 1, e = d->end, loc = c->loc + c->vt[d->tok].off;

	c->i = e;
	c->di++;
	if (unlikely(p->output_text_mode > 1))
		pp_directive_comments_flush(p, c, d->tok, e);
	if (unlikely(p->lmark) && c->f == &p->builtin && c->vt[d->tok].off >= p->o->predef_cmdl)
		pp_line_marker_builtin_count(p);	/* the command line's first -D/-U/-include */
	switch (d->kind) {
	case PP_DIRECTIVE_IF: case PP_DIRECTIVE_IFDEF: case PP_DIRECTIVE_IFNDEF:
		pp_directive_extra_tokens_check(p, c, d);
		if (!pp_conditional_is_true(p, c, d))
			pp_conditional_group_skip(p, c, d->next);
		return 0;
	case PP_DIRECTIVE_ELIF: case PP_DIRECTIVE_ELIFDEF: case PP_DIRECTIVE_ELIFNDEF: case PP_DIRECTIVE_ELSE: {
		/* the end of a group that was taken: on to #endif */
		uint32_t k = d->next;

		pp_directive_extra_tokens_check(p, c, d);
		while (pp_directive_extra_tokens_check(p, c, &c->fi->dir[k]), c->fi->dir[k].kind != PP_DIRECTIVE_ENDIF)
			k = c->fi->dir[k].next;
		c->i = c->fi->dir[k].end;
		c->di = k + 1;
		if (unlikely(p->output_text_mode > 1))
			p->fe[c->fe].comment_position = pp_comment_line_end(c->src + c->vt[c->i - 1].off + c->vt[c->i - 1].len);
		return 0;
	}
	case PP_DIRECTIVE_ENDIF:
		pp_directive_extra_tokens_check(p, c, d);
		return 0;
	case PP_DIRECTIVE_NULL:
		return 0;
	case PP_DIRECTIVE_DEFINE:
		pp_macro_define(p, c, b + 1, e);
		return 0;
	case PP_DIRECTIVE_UNDEF:
		pp_directive_extra_tokens_check(p, c, d);
		if (b + 1 < e && c->vt[b + 1].kind == LEX_TOKEN_IDENTIFIER)
{
			pp_macro_define_record(p, c->vt[b + 1].id, NULL);
			if (unlikely(p->o->dump_defines))
				pp_macro_define_event_record(p, c->vt[b + 1].id, NULL, c->f == &p->builtin);
		}
		else
			PP_ERROR(p, loc, "macro name must be an identifier");
		return 0;
	case PP_DIRECTIVE_INCLUDE: case PP_DIRECTIVE_INCLUDE_NEXT: case PP_DIRECTIVE_IMPORT:
		if (unlikely(p->collecting)) {
			diag_info n = { "note", p->coll_loc, NULL, 0, NULL, 0, NULL, 0 };

			PP_ERROR(p, loc + (c->vt[b].off - c->vt[b - 1].off),
			      "embedding a #include directive within macro arguments is not supported");
			diag_report_extended(p, &n, "expansion of macro '%.*s' requested here", (int)vintern_ents[p->collecting_name].len,
				 vintern_ents[p->collecting_name].s);
			return 0;
		}
		pp_include_run(p, c, b + 1, e, (int)d->kind, loc);
		return 0;
	case PP_DIRECTIVE_EMBED:
		if (p->o->asm_mode)
			goto pass;
		pp_embed_run(p, c, b + 1, e, loc, loc + (c->vt[b].off - c->vt[b - 1].off));
		return 0;
	case PP_DIRECTIVE_IMACROS:
		if (c->f == &p->builtin) {
			pp_imacros_run(p, c, b, e, loc);
			return 0;
		}
		if (p->o->asm_mode)
			goto pass;
		PP_ERROR(p, loc, "invalid preprocessing directive");
		return 0;
	case PP_DIRECTIVE_PRAGMA: {
		const lex_token *f = &c->vt[b], *l = &c->vt[e - 1];

		return pp_pragma_run(p, c->src + f->off, l->off + l->len - f->off, loc, out, c->loc + f->off);
	}
	case PP_DIRECTIVE_IDENT: {
		const lex_token *f = &c->vt[b], *l = &c->vt[e - 1];

		memset(out, 0, sizeof(*out));
		out->kind = PP_TOKEN_KIND_DIRECTIVE_LINE;
		out->s = c->src + f->off;
		out->len = l->off + l->len - f->off;
		out->loc = loc;
		out->flags = LEX_TOKEN_FLAG_LINE_START;
		return 1;
	}
	case PP_DIRECTIVE_ERROR: case PP_DIRECTIVE_WARNING: {
		const lex_token *f = &c->vt[b], *l = &c->vt[e - 1];

		/* clang: the text after the directive name, at the name */
		uint32_t nloc = loc + (f->off - c->vt[b - 1].off);

		if (e - b > 1)
			f = &c->vt[b + 1];
		else
			f = l + 1;
		{
			int lv = d->kind == PP_DIRECTIVE_ERROR ? DIAG_LEVEL_ERROR : diag_warning_level(p, "#warnings", true, NULL, UINT32_MAX);
			diag_record *r;

			if (lv < 0)
				return 0;
			r = diag_record_begin(p, lv, nloc, "%.*s", e - b > 1 ? (int)(l->off + l->len - f->off) : 0,
					  e - b > 1 ? c->src + f->off : "");
			r->id = d->kind == PP_DIRECTIVE_ERROR ? "err_pp_hash_error" : "pp_hash_warning";
			if (d->kind == PP_DIRECTIVE_WARNING)
				diag_record_flag_set(r, "#warnings"), r->werror = lv == DIAG_LEVEL_ERROR;
			diag_record_emit(p, r);
		}
		return 0;
	}
	case PP_DIRECTIVE_LINE_MARKER:
		if (p->o->asm_mode)
			goto pass;
		__attribute__((fallthrough));
	case PP_DIRECTIVE_LINE:
		pp_line_directive_run(p, c, d->kind == PP_DIRECTIVE_LINE ? b + 1 : b, e, d->kind == PP_DIRECTIVE_LINE, loc);
		return 0;
	default:
		if (p->o->asm_mode)
			goto pass;
		PP_ERROR(p, loc, "invalid preprocessing directive");
		return 0;
	}
pass: {
	/* assembler: the line as written ("# comment") */
	const lex_token *f = &c->vt[b], *l = &c->vt[e - 1];

	memset(out, 0, sizeof(*out));
	out->kind = PP_TOKEN_KIND_DIRECTIVE_LINE;
	out->s = c->src + f->off;
	out->len = l->off + l->len - f->off;
	out->loc = loc;
	out->flags = LEX_TOKEN_FLAG_LINE_START;
	if (unlikely(p->output_text_mode)) {
		/* clang -E re-lexes the line as tokens: one space where there was
		 * any, and one before the end of the line if a blank or comment is */
		size_t n = 0, cap = (size_t)(l->off + l->len - f->off) + 2;
		char *s = varena_alloc(&p->tmp, cap + 16);
		const char *q = c->src + l->off + l->len;

		for (uint32_t i = b; i < e; i++) {
			if (c->vt[i].flags & (LEX_TOKEN_FLAG_SPACE_BEFORE | LEX_TOKEN_FLAG_LINE_START))
				s[n++] = ' ';
			memcpy(s + n, c->src + c->vt[i].off, c->vt[i].len);
			n += c->vt[i].len;
		}
		if (*q == ' ' || *q == '\t' || *q == '\f' || *q == '\v' || (*q == '/' && (q[1] == '*' || q[1] == '/')))
			s[n++] = ' ';
		out->s = s;
		out->len = (uint32_t)n;
		out->id = 1;	/* tokens, not a directive: indented like a line of them */
	}
	return 1;
	}
}
