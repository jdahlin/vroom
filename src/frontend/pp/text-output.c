/* vcc preprocessor: the -E printer: spacing, line markers, comments and the output buffer */
#include "pp-internal.h"

/* ---------------- -E text ---------------- */

/* the -E buffer of this thread's last pp, kept for the next one (no unmap, no page faults) */
_Thread_local char *pp_output_spare;
_Thread_local size_t pp_output_spare_capacity;

VPRIV void pp_output_reserve(pp *p, size_t n)
{
	size_t nc;
	char *np;

	if (likely(p->nout + n <= p->output_capacity))
		return;
	if (!p->out && pp_output_spare && pp_output_spare_capacity >= n) {
		p->out = pp_output_spare, p->output_capacity = pp_output_spare_capacity;
		pp_output_spare = NULL, pp_output_spare_capacity = 0;
		return;
	}
	nc = p->output_capacity ? p->output_capacity * 2 : 1u << 24;
	while (nc < p->nout + n)
		nc *= 2;
	np = pp_address_space_reserve(nc);
	if (p->out) {
		memcpy(np, p->out, p->nout);
		munmap(p->out, p->output_capacity);
	}
	p->out = np;
	p->output_capacity = nc;
}
/* an identifier's \\u/\\U escapes as UTF-8, in place (never longer), as clang -E */
static __attribute__((noinline, cold)) size_t pp_identifier_ucn_to_utf8(char *s, size_t n)
{
	size_t o = 0;

	for (size_t i = 0; i < n;) {
		unsigned d = s[i] == '\\' && i + 1 < n ? (s[i + 1] == 'u' ? 4 : s[i + 1] == 'U' ? 8 : 0) : 0;
		uint32_t c = 0;

		if (!d || i + 2 + d > n) {
			s[o++] = s[i++];
			continue;
		}
		for (unsigned k = 0; k < d; k++) {
			char h = s[i + 2 + k];

			c = c << 4 | (uint32_t)(h <= '9' ? h - '0' : (h | 32) - 'a' + 10);
		}
		i += 2 + d;
		if (c < 0x80) {
			s[o++] = (char)c;
		} else if (c < 0x800) {
			s[o++] = (char)(0xc0 | c >> 6);
			s[o++] = (char)(0x80 | (c & 63));
		} else if (c < 0x10000) {
			s[o++] = (char)(0xe0 | c >> 12);
			s[o++] = (char)(0x80 | (c >> 6 & 63));
			s[o++] = (char)(0x80 | (c & 63));
		} else {
			s[o++] = (char)(0xf0 | c >> 18);
			s[o++] = (char)(0x80 | (c >> 12 & 63));
			s[o++] = (char)(0x80 | (c >> 6 & 63));
			s[o++] = (char)(0x80 | (c & 63));
		}
	}
	return o;
}
VPRIV void pp_output_newline(pp *p)
{
	if (unlikely(p->rec.on) && !p->rec.levels[p->rec.n - 1].text_started)
		pp_header_cache_text_start(p, 1);
	pp_output_append_character(p, '\n');
}

/* clang's WriteLineInfo: "# N "file" flags" on a line of its own */
static __attribute__((cold, noinline)) void pp_line_marker_write(pp *p, uint32_t line, const char *extra)
{
	char b[16];
	int n = snprintf(b, sizeof(b), "# %u \"", line);

	if (p->col || p->edir)
		pp_output_newline(p);
	for (int i = 0; i < n; i++)
		pp_output_append_character(p, b[i]);
	for (uint32_t i = 0; i < p->marker_file_name_length; i++) {	/* llvm's write_escaped */
		unsigned char c = (unsigned char)p->marker_file_name[i];

		if (c == '\\' || c == '"')
			pp_output_append_character(p, '\\'), pp_output_append_character(p, (char)c);
		else if (c == '\t')
			pp_output_append_character(p, '\\'), pp_output_append_character(p, 't');
		else if (c == '\n')
			pp_output_append_character(p, '\\'), pp_output_append_character(p, 'n');
		else if (c < 32 || c > 126) {
			pp_output_append_character(p, '\\');
			pp_output_append_character(p, (char)('0' + (c >> 6)));
			pp_output_append_character(p, (char)('0' + (c >> 3 & 7)));
			pp_output_append_character(p, (char)('0' + (c & 7)));
		} else
			pp_output_append_character(p, (char)c);
	}
	pp_output_append_character(p, '"');
	while (*extra)
		pp_output_append_character(p, *extra++);
	if (p->marker_is_system)
		pp_output_append_character(p, ' '), pp_output_append_character(p, '3');
	if (p->marker_is_system == 2)
		pp_output_append_character(p, ' '), pp_output_append_character(p, '4');
	pp_output_newline(p);
	p->col = 0, p->edir = 0;
}

static void pp_line_marker_file_set(pp *p, const char *name, uint8_t sys)
{
	p->marker_file_name = name;
	p->marker_file_name_length = (uint32_t)strlen(name);
	p->marker_is_system = sys;
}

/* the predefined macros before the command line: clang's <built-in> has a
 * line for each, and the command line's marker is 3 lines after (1 with -undef) */
VPRIV void pp_line_marker_builtin_count(pp *p)
{
	uint32_t n = 0;

	if (p->lm_n)
		return;
	for (uint32_t d = 0; d < PP_MACRO_DIRECTORY_SIZE; d++) {
		const uintptr_t *pg = p->macro_directory[d];

		for (uint32_t k = 0; pg && k < PP_MACRO_PAGE_SIZE; k++) {
			const pp_macro *m = (const pp_macro *)(pg[k] & ~(uintptr_t)1);

			n += m && m->kind != PP_MACRO_BUILTIN;
		}
	}
	p->lm_n = n + 1;
}

/* <built-in>'s command line, after the predefined macros */
static __attribute__((cold, noinline)) void pp_line_marker_builtin_block_write(pp *p)
{
	pp_line_marker_builtin_count(p);
	p->lm_blk = 1;
	pp_output_line_move(p, p->lm_n - 1 + (p->o->markers == 2 ? 1 : 3), false);
	pp_line_marker_file_set(p, "<command line>", 0);
	pp_line_marker_write(p, 1, " 1");
	pp_line_marker_file_set(p, "<built-in>", 0);
	pp_line_marker_write(p, 1, " 2");
	p->cur_line = 1;
	p->marker_builtin_line = 1;	/* <built-in>'s line where -imacros/-include start */
	for (unsigned i = 0; i < p->o->predef_incl; i++)
		p->marker_builtin_line += p->o->predef[i] == '\n';
}

/* a file entered: its marker (after the #include's line was moved to) */
VPRIV void pp_line_marker_file_enter(pp *p, const pp_file_entry *e)
{
	pp_line_marker_file_set(p, e->spath, e->sys);
	p->cur_line = 1;
	pp_line_marker_write(p, 1, " 1");
}

/* #line N ["file"] / # N "file": clang's FileChanged(RenameFile) */
VPRIV void pp_line_marker_file_rename(pp *p, const char *name, uint32_t nlen, uint32_t line)
{
	if (name)
		p->marker_file_name = name, p->marker_file_name_length = nlen;
	p->cur_line = line;
	pp_line_marker_write(p, line, "");
}

/* clang's PrintPPOutputPPCallbacks::MoveToLine (-P: MinimizeWhitespace off) */
VPRIV bool pp_output_line_move(pp *p, uint32_t line, bool req)
{
	bool nl = false;

	if ((req && p->col) || p->edir) {
		pp_output_newline(p);
		nl = true;
		p->cur_line++;
		p->col = 0, p->edir = 0;
	}
	if (p->cur_line == line)
		;
	else if (unlikely(p->lmark)) {
		if (line - p->cur_line <= 8)
			for (uint32_t i = p->cur_line; i < line; i++)
				pp_output_newline(p);
		else
			pp_line_marker_write(p, line, "");
		nl = true;
	}
	else if (!nl && line - p->cur_line == 1)
		pp_output_newline(p), nl = true;
	else if (!nl && p->col)
		pp_output_newline(p), nl = true;
	if (nl)
		p->col = 0, p->edir = 0;
	p->cur_line = line;
	return nl;
}

/* FileChanged: entering a file from an #include in `from` moves to its line,
 * leaving it puts the output on the line after (clang's IncludeLoc) */
VPRIV void pp_output_file_change(pp *p, const pp_context *from, int enter)
{
	uint32_t l = 1;

	if (!p->output_text_mode)
		return;
	if (unlikely(p->lmark)) {
		const pp_file_entry *e;

		if (from && from->f == &p->builtin) {
			if (!p->lm_blk)
				pp_line_marker_builtin_block_write(p);
			/* clang's <built-in> after the command line: a line per -include, 2 per -imacros */
			l = loc_line(p, from->loc + from->vt[from->i - 1].off) - p->marker_builtin_line + 1 + !enter;
			if (enter)
				pp_output_line_move(p, l, false);
			if (!enter) {
				pp_line_marker_file_set(p, "<built-in>", 0);
				p->cur_line = l;
				pp_line_marker_write(p, l, " 2");
			}
			return;	/* enter: the -include line is the current one */
		}
		if (!enter && !p->lm_blk)
			pp_line_marker_builtin_block_write(p);	/* <built-in> ends */
		if (from && from->type == PP_CONTEXT_FILE && from->i)
			l = loc_line(p, from->loc + from->vt[from->i - 1].off);
		if (enter) {
			pp_output_line_move(p, l, false);
			return;	/* the marker when the file is known (pp_line_marker_file_enter) */
		}
		e = &p->fe[from->fe];
		pp_line_marker_file_set(p, e->spath, e->sys);
		if (from->i) {
			uint32_t pl;
			const char *pn = presumed_name(p, from->fe, from->loc + from->vt[from->i - 1].off, &pl);

			if (pn != e->name)
				p->marker_file_name = pn, p->marker_file_name_length = pl;
			l++;
		}
		p->cur_line = l;
		pp_line_marker_write(p, l, " 2");
		return;
	}
	if (from && from->type == PP_CONTEXT_FILE && from->i)
		l = loc_line(p, from->loc + from->vt[from->i - 1].off);
	else if (enter)
		return;
	if (enter) {
		if (from->f != &p->builtin || from->i)
			pp_output_line_move(p, l, false);
		p->cur_line = 1;
	} else {
		p->cur_line = from && from->type == PP_CONTEXT_FILE && from->i ? l + 1 : 1;
	}
	if (p->col || p->edir) {	/* startNewLineIfNeeded */
		pp_output_newline(p);
		p->col = 0, p->edir = 0;
	}
}

/* -C: the start of the line after s (past a directive's tokens): a block
 * comment that starts on the line continues it */
VPRIV const char *pp_comment_line_end(const char *s)
{
	for (;;) {
		if (*s == '\n')
			return s + 1;
		if (!*s)
			return s;	/* the end: files are zero padded */
		if (s[0] == '/' && s[1] == '*') {
			for (s += 2; *s && !(s[0] == '*' && s[1] == '/'); s++)
				;
			s += *s ? 2 : 0;
		} else if (s[0] == '\\' && s[1] == '\n') {
			s += 2;
		} else if (s[0] == '\\' && s[1] == '\r' && s[2] == '\n') {
			s += 3;
		} else if (s[0] == '/' && s[1] == '/') {
			for (; *s && *s != '\n'; s++)
				if (s[0] == '\\' && s[1] == '\n')
					s++;
		} else {
			s++;
		}
	}
}

/* -C: a comment token [s, e) of file context c, printed as clang does */
static void pp_comment_print(pp *p, const pp_context *c, const char *s, const char *e)
{
	const char *b = s;
	bool sol, space;
	unsigned col;

	while (b > c->src && (b[-1] == ' ' || b[-1] == '\t' || b[-1] == '\f' || b[-1] == '\v'))
		b--;
	sol = b == c->src || b[-1] == '\n' || p->cmt_sol;
	p->cmt_sol = 0;
	space = b != s;
	for (col = 1; s - col >= c->src && s[-(ptrdiff_t)col] != '\n'; col++)
		;
	if (sol || p->edir) {
		uint32_t line = loc_line(p, c->loc + (uint32_t)(s - c->src));

		if (pp_output_line_move(p, line, p->edir) || (sol && line == 1)) {
			pp_output_reserve(p, col);
			for (; col > 1; col--)
				p->out[p->nout++] = ' ';
		} else if (space) {
			pp_output_append_character(p, ' ');
		}
	} else if (space || (p->col && p->pk == LEX_TOKEN_SLASH && p->previous_end != s)) {
		pp_output_append_character(p, ' ');
	}
	pp_output_append(p, s, (size_t)(e - s));
	for (const char *q = s; q < e; q++)
		p->cur_line += *q == '\n';
	p->col = 1, p->sol_on = 0;
	p->pk = LEX_TOKEN_EOF, p->previous_end = e;
}

/* -C: print the comments of file context c from its position up to upto */
VPRIV void pp_comments_flush(pp *p, pp_context *c, const char *upto)
{
	const char *s = p->fe[c->fe].comment_position;

	if (!s || upto <= s) {
		if (s && upto > s)
			p->fe[c->fe].comment_position = upto;
		return;
	}
	while (s < upto) {
		if (s[0] == '/' && s[1] == '*') {
			const char *e = s + 2;

			while (e < upto && !(e[0] == '*' && e[1] == '/'))
				e++;
			if (e >= upto)
				break;
			pp_comment_print(p, c, s, e + 2);
			s = e + 2;
		} else if (s[0] == '/' && s[1] == '/') {
			const char *e = s;

			for (; e < upto && *e != '\n'; e++)
				if (e[0] == '\\' && e[1] == '\n')
					e++;
			if (e > upto)
				e = upto;
			while (e > s && (e[-1] == '\r'))
				e--;
			pp_comment_print(p, c, s, e);
			s = e;
		} else if (*s == '"' || *s == '\'') {
			char q = *s++;

			while (s < upto && *s != q && *s != '\n')
				s += *s == '\\' ? 2 : 1;
			s++;
		} else {
			s++;
		}
	}
	p->fe[c->fe].comment_position = upto;
}

/* -C: the file context's comments up to the end of token rp are dropped (macro arguments) */
VPRIV void pp_comments_skip_to(pp *p, const ptok *rp)
{
	pp_context *fc = &p->cx[p->depth - 1];

	if (fc->type == PP_CONTEXT_FILE && rp->s >= fc->src && rp->s < fc->src + fc->f->size && p->fe[fc->fe].comment_position && p->fe[fc->fe].comment_position < rp->s)
		p->fe[fc->fe].comment_position = rp->s + rp->len;
}

/* clang's TokenConcatenation::AvoidConcat: would prev followed by t, unspaced, lex differently? */
static bool pp_output_avoids_concatenation(const pp *p, const ptok *t, char fc)
{
	switch (p->pk) {
	case LEX_TOKEN_AMPERSAND: case LEX_TOKEN_PLUS: case LEX_TOKEN_MINUS: case LEX_TOKEN_SLASH: case LEX_TOKEN_LESS: case LEX_TOKEN_GREATER: case LEX_TOKEN_PIPE:
	case LEX_TOKEN_PERCENT: case LEX_TOKEN_STAR: case LEX_TOKEN_EXCLAMATION: case LEX_TOKEN_LESS_LESS: case LEX_TOKEN_GREATER_GREATER: case LEX_TOKEN_CARET:
	case LEX_TOKEN_EQUAL:
		if (t->kind == LEX_TOKEN_EQUAL || t->kind == LEX_TOKEN_EQUAL_EQUAL)
			return true;
		break;
	}
	switch (p->pk) {
	case LEX_TOKEN_IDENTIFIER:
		if (t->kind == LEX_TOKEN_NUMBER)
			return fc != '.';
		if (t->kind == LEX_TOKEN_IDENTIFIER || (t->kind >= LEX_TOKEN_WIDE_CHARACTER && t->kind <= LEX_TOKEN_UTF32_CHARACTER) || (t->kind >= LEX_TOKEN_WIDE_STRING && t->kind <= LEX_TOKEN_UTF32_STRING))
			return true;
		return (t->kind == LEX_TOKEN_CHARACTER || t->kind == LEX_TOKEN_STRING) && p->pk_L;
	case LEX_TOKEN_NUMBER:
		return pp_character_is_identifier((unsigned char)fc) || fc == '.' || fc == '+' || fc == '-' || fc == '\\';
	case LEX_TOKEN_PERIOD: return (fc == '.' && p->pk_pp) || (fc >= '0' && fc <= '9');
	case LEX_TOKEN_AMPERSAND: return fc == '&';
	case LEX_TOKEN_PLUS: return fc == '+';
	case LEX_TOKEN_MINUS: return fc == '-' || fc == '>';
	case LEX_TOKEN_SLASH: return fc == '*' || fc == '/';
	case LEX_TOKEN_LESS: return fc == '<' || fc == ':' || fc == '%';
	case LEX_TOKEN_GREATER: return fc == '>';
	case LEX_TOKEN_PIPE: return fc == '|';
	case LEX_TOKEN_PERCENT: return fc == '>' || fc == ':';
	case LEX_TOKEN_COLON: return fc == '>';
	case LEX_TOKEN_HASH: return fc == '#' || fc == '@' || fc == '%';
	default: return false;
	}
}

/* the expansion column of t (1-based, bytes) */
VPRIV __attribute__((noinline)) unsigned pp_token_column(const pp *p, uint32_t loc)
{
	const char *s, *b;

	{
		uint32_t lo = 0, hi = p->nfe;

		while (hi - lo > 1) {
			uint32_t mid = (lo + hi) / 2;

			if (p->fe[mid].base <= loc)
				lo = mid;
			else
				hi = mid;
		}
		while (lo > 0 && !p->fe[lo].f)
			lo--;
		if (!p->nfe || !p->fe[lo].f || loc - p->fe[lo].base > p->fe[lo].f->size)
			return 1;
		b = p->fe[lo].f->src;
		s = b + (loc - p->fe[lo].base);
	}
	for (unsigned c = 1;; c++)
		if (s == b || s[-1] == '\n' || s[-1] == 0)
			return c;
		else
			s--;
}

/* the token as clang -E -P prints it */
static inline void pp_token_render(pp *p, const ptok *t)
{
	bool sol = t->kind == PP_TOKEN_KIND_DIRECTIVE_LINE || (t->flags & LEX_TOKEN_FLAG_LINE_START);
	size_t l;
	const char *sp;
	char fc;

	if (unlikely(p->output_text_mode > 1) && t->kind != PP_TOKEN_KIND_DIRECTIVE_LINE) {
		pp_context *fc = &p->cx[p->depth - 1];

		if (fc->type == PP_CONTEXT_FILE && !(t->flags & PP_TOKEN_FLAG_FROM_MACRO) && p->fe[fc->fe].comment_position && t->s >= p->fe[fc->fe].comment_position &&
		    t->s < fc->src + fc->f->size) {
			pp_comments_flush(p, fc, t->s);
			p->fe[fc->fe].comment_position = t->s + t->len;
		}
		p->cmt_sol = 0;	/* taken by a comment printed now, else by this token */
	}
	if (unlikely(p->rec.on) && !p->rec.levels[p->rec.n - 1].text_started) {
		if (!sol)
			PP_HEADER_CACHE_REGION_REJECT(p);	/* its separator depends on what was printed before */
		pp_header_cache_text_start(p, sol);
	}
	if (t->kind == PP_TOKEN_KIND_DIRECTIVE_LINE && t->id) {
		/* an assembler line passed through: tokens, so indented as clang does */
		unsigned c = pp_token_column(p, t->loc);

		pp_output_line_move(p, loc_line(p, t->loc), p->edir);
		pp_output_reserve(p, t->len + c + 2);
		if (!p->col)
			for (p->out[p->nout++] = ' '; c > 2; c--)
				p->out[p->nout++] = ' ';
		else
			p->out[p->nout++] = ' ';
		p->out[p->nout++] = '#';
		memcpy(p->out + p->nout, t->s, t->len);
		p->nout += t->len;
		p->col = 1, p->sol_on = 0;
		p->pk = LEX_TOKEN_EOF, p->previous_end = NULL;
		return;
	}
	if (t->kind == PP_TOKEN_KIND_DIRECTIVE_LINE) {
		uint32_t line = loc_line(p, t->loc);

		/* clang prints "assume_nonnull end" at the line of what it printed last */
		if (t->len == 31 && !memcmp(t->s, "pragma clang assume_nonnull end", 31))
			line = p->cur_line;
		pp_output_line_move(p, line, true);
		pp_output_reserve(p, t->len + 2);
		p->out[p->nout++] = '#';
		memcpy(p->out + p->nout, t->s, t->len);
		p->nout += t->len;
		p->col = 1, p->edir = 1, p->sol_on = 0;
		p->pk = LEX_TOKEN_EOF, p->previous_end = NULL;
		return;
	}
	fc = t->s[0];	/* a token never starts with a splice */
	if (unlikely(p->emb_sp) && !p->edir) {
		/* after #embed's data clang prints the next token after one space, on the same line */
		p->emb_sp = (t->flags & PP_TOKEN_FLAG_EMBED_END) != 0;
		if (p->embed_tail != 1)	/* after the bytes one space, after suffix()/if_empty() none */
			pp_output_append_character(p, ' ');
		if (p->embed_tail)
			p->embed_tail--;
		goto spaced;
	}
	p->emb_sp = (t->flags & PP_TOKEN_FLAG_EMBED_END) != 0;
	if (sol || p->edir) {
		/* HandleWhitespaceBeforeTok: indented to its column if it starts a new line */
		uint32_t loc = p->sol_on ? p->line_start_location : p->cx[p->depth - 1].type != PP_CONTEXT_FILE ? p->expansion_location : t->loc;
		uint32_t line = loc_line(p, loc);
		unsigned c = pp_token_column(p, loc);
		/* clang's leading space: not the line break before a token in column 1 */
		bool space = (t->flags & LEX_TOKEN_FLAG_SPACE_BEFORE) && (p->sol_on || c > 1) && p->previous_end != t->s;

		if (pp_output_line_move(p, line, p->edir) || (sol && line == 1)) {
			if (c == 1 && space)
				c = 2;	/* after an empty expansion in column 1 */
			pp_output_reserve(p, c + 1);
			if (c <= 1 && t->kind == LEX_TOKEN_HASH)
				p->out[p->nout++] = ' ';
			for (; c > 1; c--)
				p->out[p->nout++] = ' ';
		} else if (space || (p->col && p->previous_end != t->s && p->pk != LEX_TOKEN_EOF && pp_output_avoids_concatenation(p, t, fc))) {
			pp_output_append_character(p, ' ');
		}
	} else if (((t->flags & LEX_TOKEN_FLAG_SPACE_BEFORE) && p->previous_end != t->s) ||
		   (p->col && p->previous_end != t->s && p->pk != LEX_TOKEN_EOF && pp_output_avoids_concatenation(p, t, fc))) {
		pp_output_append_character(p, ' ');
	}
spaced:
	pp_output_reserve(p, t->len);
	sp = pp_token_spell(t, p->out + p->nout, &l);
	if (sp != p->out + p->nout)
		memcpy(p->out + p->nout, sp, l);
	if (t->kind == LEX_TOKEN_IDENTIFIER && !p->o->asm_mode && unlikely(memchr(p->out + p->nout, '\\', l) != NULL))
		l = pp_identifier_ucn_to_utf8(p->out + p->nout, l);
	if (unlikely(t->ctr) && p->rec.on)
		for (uint16_t r = t->ctr; r; r = p->rec.cp[r].next) {
			const pp_header_cache_counter_provenance *c = &p->rec.cp[r];

			if (p->rec.nctr < PP_HEADER_CACHE_COUNTER_PROVENANCE_MAX)
				p->rec.ctr[p->rec.nctr++] = (pp_header_cache_counter_position){ p->nout + c->off, c->len, c->val };
			else
				PP_HEADER_CACHE_REGION_REJECT(p);
		}
	p->sol_on = 0;
	p->pk_pp = p->pk == LEX_TOKEN_PERIOD;
	p->pk = t->kind;
	p->pk_L = t->kind == LEX_TOKEN_IDENTIFIER && l == 1 && p->out[p->nout] == 'L';
	p->previous_end = t->flags & LEX_TOKEN_FLAG_SPLICED ? NULL : t->s + t->len;
	p->nout += l;
	p->col++;
}

int pp_text_render(pp *p, const char **out, size_t *len)
{
	ptok t;

	pp_header_cache_mode_set(p, 1);
	p->output_text_mode = p->o->comments ? 2 : 1;	/* 2: -C */
	p->cur_line = 1;
	if (unlikely(p->o->markers)) {
		p->lmark = 1;
		pp_line_marker_file_set(p, p->fe[0].spath, p->fe[0].sys);
		pp_line_marker_write(p, 1, "");
		pp_line_marker_file_set(p, "<built-in>", 0);
		pp_line_marker_write(p, 1, " 1");
		p->marker_is_system = 1;
		pp_line_marker_write(p, 1, "");
	}
	if (p->o->asm_mode)
		p->hc = 0;	/* cached text is C-spaced */
	for (;;) {
		pp_token_next_resetting_temporary(p, &t);
		if (unlikely(p->o->dump_defines) && !p->define_events_printed)
			pp_macro_define_events_flush(p);	/* the predefined macros before the first token */
		if (t.kind == LEX_TOKEN_EOF)
			break;
		pp_token_render(p, &t);
	}
	pp_output_append_character(p, '\n');	/* clang ends with a newline, also after an empty line */
	*out = p->out ? p->out : "";
	*len = p->nout;
	return p->errors;
}
