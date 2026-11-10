/* vcc preprocessor: opening and closing a TU, pp_next and the queries on the result */
#include <sys/stat.h>
#include "ppimpl.h"

/* address space, backed as touched: NORESERVE, or Linux's heuristic overcommit
   refuses the 16 GB shared region on a machine with less RAM + swap */
VPRIV void *vm(size_t n)
{
	void *p = mmap(NULL, n, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON | MAP_NORESERVE, -1, 0);

	if (p == MAP_FAILED) {
		perror("vcc: mmap");
		abort();
	}
	return p;
}

/* ---------------- setup ---------------- */

/* the builtin macros: process-wide, so equal in every TU */
static const struct { const char *n; int b; } bi[] = {
	{ "__FILE__", B_FILE }, { "__LINE__", B_LINE }, { "__COUNTER__", B_COUNTER },
	{ "__DATE__", B_DATE }, { "__TIME__", B_TIME }, { "__TIMESTAMP__", B_TIMESTAMP },
	{ "__BASE_FILE__", B_BASE_FILE }, { "__FILE_NAME__", B_FILE_NAME },
	{ "__INCLUDE_LEVEL__", B_INCLUDE_LEVEL }, { "_Pragma", B_PRAGMA },
	{ "__has_attribute", B_HAS_ATTRIBUTE }, { "__has_builtin", B_HAS_BUILTIN },
	{ "__has_feature", B_HAS_FEATURE }, { "__has_extension", B_HAS_EXTENSION },
	{ "__has_c_attribute", B_HAS_C_ATTRIBUTE },	/* __has_cpp_attribute: not defined in C (clang) */
	{ "__has_declspec_attribute", B_HAS_DECLSPEC }, { "__has_warning", B_HAS_WARNING },
	{ "__is_identifier", B_IS_IDENTIFIER }, { "__has_include", B_HAS_INCLUDE },
	{ "__has_include_next", B_HAS_INCLUDE_NEXT }, { "__has_embed", B_HAS_EMBED },
	{ "__has_constexpr_builtin", B_HAS_CONSTEXPR_BUILTIN },
	{ "__is_target_arch", B_IS_TARGET_ARCH }, { "__is_target_os", B_IS_TARGET_OS },
	{ "__is_target_vendor", B_IS_TARGET_VENDOR },
	{ "__is_target_environment", B_IS_TARGET_ENVIRONMENT },
	{ "__is_target_variant_os", B_IS_TARGET_VARIANT_OS },
	{ "__is_target_variant_environment", B_IS_TARGET_VARIANT_ENVIRONMENT },
	{ "__building_module", B_BUILDING_MODULE },
};

static const pmacro *bi_macro[sizeof(bi) / sizeof(bi[0])];

static void bi_init(void)
{
	for (size_t i = 0; i < sizeof(bi) / sizeof(bi[0]); i++) {
		pmacro *m = salloc(sizeof(*m));

		memset(m, 0, sizeof(*m));
		m->kind = MK_BUILTIN;
		m->builtin = (uint8_t)bi[i].b;
		m->name = vintern_cstr(bi[i].n);
		m->body = no_body;
		bi_macro[i] = m;
	}
}

/* a TU's tables, reused from one TU to the next (pp_open takes a set, pp_close gives it back) */
static vmpool pppool = VMPOOL_INIT(8);
static _Thread_local vmreg *ppr;

pp *pp_open(const pp_opts *o)
{
	pp *p = vmreg_get(&(ppr = vmpool_take(&pppool))[0], sizeof(pp), "pp");
	const vlex_file *mf;
	size_t n = strlen(o->predef);
	char *src;
	vtok *tok;
	void *scratch;

	vintern_init();
	p->o = o;
	p->color = o->diag_color > 0 || (o->diag_color == 0 && isatty(o->errfd ? o->errfd : 2));
	p->cx = vmreg_get(&ppr[1], MAXCTX * sizeof(ctx), "pp.cx");
	p->perm.base = p->perm.p = vmreg_get(&ppr[2], 1ull << 30, "pp.perm");
	p->perm.end = p->perm.base + (1ull << 30);
	p->tmp.base = p->tmp.p = vmreg_get(&ppr[3], 1ull << 32, "pp.tmp");
	p->tmp.end = p->tmp.base + (1ull << 32);
	p->psig = varena_alloc(&p->perm, ((size_t)o->ndirs + 1) * sizeof(uint32_t));
	if (!sig_of(o, &p->sig, p->psig))
		p->sig = HNONE;
	p->sv = vmreg_get(&ppr[4], SVMAX * sizeof(ptok), "pp.sv");
	p->fe = vmreg_get(&ppr[5], (1u << 20) * sizeof(fent), "pp.fe");
	p->once = varena_alloc(&p->perm, 256 * sizeof(*p->once));
	p->oncecap = 256;
	p->pushed = varena_alloc(&p->perm, 4096 * sizeof(*p->pushed));
	p->lm = vmreg_get(&ppr[6], 65536 * sizeof(*p->lm), "pp.lm");
	{
		static pthread_once_t once = PTHREAD_ONCE_INIT;

		pthread_once(&once, bi_init);
		for (size_t i = 0; i < sizeof(bi) / sizeof(bi[0]); i++)
			mset(p, bi_macro[i]->name, bi_macro[i]);
	}

	{
		/* the caller's string is unpadded; vlex_get hashes by words (ASan) */
		size_t ml = strlen(o->main);
		char *mp = varena_alloc(&p->perm, ml + 16);

		memcpy(mp, o->main, ml);
		memset(mp + ml, 0, 16);
		mf = try_path(o, mp, ml);
	}
	if (!mf) {
		char m[4200], fp[4200];
		struct stat st;
		bool big;

		/* the lexer takes files below 2 GB (32-bit offsets): say so rather than "cannot read" */
		snprintf(fp, sizeof(fp), "%s%s%s", o->main[0] != '/' && o->cwd ? o->cwd : "",
			 o->main[0] != '/' && o->cwd ? "/" : "", o->main);
		big = !stat(fp, &st) && st.st_size >= (1ll << 31);
		int l = snprintf(m, sizeof(m), big ? "vcc: error: %s: file too large (2 GB or more)\n" :
						    "vcc: error: %s: cannot read\n", o->main);

		(void)!write(o->errfd ? o->errfd : 2, m, (size_t)(l < (int)sizeof(m) ? l : (int)sizeof(m) - 1));
		pp_close(p);
		return NULL;
	}
	{
		/* a fatal error here (unbalanced conditionals) ends the TU, not the process (vccd) */
		jmp_buf jb;

		p->fatal_jb = &jb;
		if (setjmp(jb)) {
			pp_diag_summary(p);	/* ("1 error generated.", as clang) */
			pp_close(p);
			return NULL;
		}
		enter_file(p, mf, o->main, -1);
		p->fatal_jb = NULL;
	}
	p->base_name = p->fe[0].name;
	p->base_namelen = p->fe[0].namelen;

	/* "<built-in>": the predefines, -D/-U and -include, on top of the main file */
	src = salloc(n + 129);	/* interned identifiers point into it: immortal */
	memcpy(src, o->predef, n);
	memset(src + n, 0, 129);
	tok = varena_alloc(&p->perm, (n + 2) * sizeof(vtok));
	scratch = vmreg_get(&ppr[7], vlex_scratch_size(n) + 4096, "pp.lexscratch");
	p->builtin.path = "<built-in>";
	p->builtin.src = src;
	p->builtin.size = (uint32_t)n;
	p->builtin.ntok = vlex(src, n, tok, scratch);
	p->builtin.tok = tok;
	vmreg_put(&ppr[7], VMREG_SCRATCH);
	enter_file(p, &p->builtin, "<built-in>", -1);
	return p;
}

static inline __attribute__((always_inline)) void next_tok(pp *p, ptok *t)
{
	if (p->depth == p->nfile && !p->svn && !p->hold_tmp)
		p->tmp.p = p->tmp.base;	/* nothing above file level refers to tmp */
	next_exp(p, t);
}

/* the header cache's mode: text for pp_text, tokens for pp_next */
static int hc_off;
int pp_drcov;

/* once: a thread that saw the flag but not hc_mem_max stored without a cap (TSan) */
static void hc_env(void)
{
	const char *m = getenv("VPP_HC_MAX_MB");

	hc_mem_max = m ? (size_t)atol(m) << 20 : (size_t)8 << 30;
	hc_off = getenv("VPP_NOHC") != NULL;
	pp_drcov = getenv("VCC_DRCOV") || getenv("VCC_DRREC") || getenv("VCC_DR");
}

static void hc_mode(pp *p, int mode)
{
	static pthread_once_t once = PTHREAD_ONCE_INIT;

	pthread_once(&once, hc_env);
	p->hc = !hc_off && !p->o->show_includes && !p->o->dump_defines && !p->o->comments && !p->o->markers && !p->o->imacros;	/* -H: a replay has no nesting to print */
	p->tokmode = mode;
}

static __attribute__((noinline)) void pp_next_slow(pp *p, ptok *t)
{
	if (unlikely(!p->tokmode))
		hc_mode(p, 2);
	p->rep_norec = 0;
	next_tok(p, t);
	p->ntok_out++;
	if (unlikely(p->rec.on) && !p->rep_norec)
		rec_tok(p, t);
}

/* t names a macro (entry e): pp_next's expansion path (not recording) */
static __attribute__((noinline)) void pp_next_mac(pp *p, ptok *t, uintptr_t e)
{
	next_exp_slow(p, t, e);
	p->ntok_out++;
}

void pp_hold_tmp(pp *p, int on) { p->hold_tmp = on != 0; }

void pp_next(pp *p, ptok *t)
{
	ctx *c = &p->cx[p->depth - 1];

	/* a cached header's tokens: straight out, unless a child or __COUNTER__ digits come next */
	if (c->type == C_REPLAY && c->i < c->n) {
		const hent *e = c->he;

		if ((c->si >= e->nseg || e->seg[c->si].at != c->i) && (c->ci >= e->nctr || e->ctr[c->ci].off != c->i)) {
			p->ntok_out++;
			htok_get(t, &c->ht[c->i++], c->pt, c->src, c->loc);
			if (t->kind == T_IDENT)
				t->flags |= PF_NOEXP;
			if (unlikely(p->rec.on) && !c->norec)
				rec_tok(p, t);
			return;
		}
	}
	/* a plain token with nothing owed and no macro: inline, no frame (the rest are tail calls) */
	if (likely(p->tokmode && !p->rec.on && c->i < c->n && !(c->first | p->pend))) {
		uintptr_t m;

		if (c->type == C_PTOK) {
			*t = c->pt[c->i++];
		} else if (c->type == C_VTOK) {
			const vtok *v = &c->vt[c->i++];

			vfill(t, v, c->src, c->offloc ? c->loc + v->off : c->loc);
		} else if (c->type == C_FILE && !((c->vt[c->i].flags & TF_SOL) && c->vt[c->i].kind == T_HASH)) {
			const vtok *v = &c->vt[c->i++];

			if (p->depth == p->nfile && !p->svn && !p->hold_tmp)
				p->tmp.p = p->tmp.base;	/* nothing above file level refers to tmp */
			vfill(t, v, c->src, c->loc + v->off);
		} else {
			pp_next_slow(p, t);
			return;
		}
		if (t->kind == T_IDENT && !(t->flags & PF_NOEXP) && (m = mget(p, t->id))) {
			pp_next_mac(p, t, m);
			return;
		}
		p->ntok_out++;
		return;
	}
	pp_next_slow(p, t);
}

const pp_topev *pp_top_events(const pp *p, uint32_t *n)
{
	*n = p->ntev;
	return p->tev_full ? NULL : p->tev ? p->tev : (const pp_topev *)"";
}

uint64_t pp_ntok(const pp *p)
{
	return p->ntok_out;
}

uint64_t pp_skipped(const pp *p)
{
	return p->skipped_tok;
}

bool pp_open_hit(const pp *p, uint64_t *b, const void **he, uint32_t *base, unsigned *ctr, uint32_t *span)
{
	if (!p->ndrs)
		return false;
	*b = p->drs[p->ndrs - 1].pos;
	*he = p->drs[p->ndrs - 1].he;
	if (base)
		*base = p->drs[p->ndrs - 1].base, *ctr = p->drs[p->ndrs - 1].ctr,
		*span = ((const hent *)*he)->span;
	return true;
}

uint64_t pp_hit_ntok(const void *he)
{
	return ((const hent *)he)->total;
}

const pp_hitreg *pp_hit_regions(const pp *p, uint32_t *n)
{
	*n = p->ndrh;
	return p->drh;
}

/* VCC_DRCOV: a header-cache hit just applied; its replay (if any) is the context at depth d */
VPRIV void drcov_hit(pp *p, int d0)
{
	if (!p->drh)
		p->drh = varena_alloc(&p->perm, DRCOV_MAX * sizeof(pp_hitreg));
	if (p->depth > d0 && p->cx[d0].type == C_REPLAY && p->cx[d0].he == p->hc_last) {
		if (p->ndrs < 64) {
			p->drs[p->ndrs].pos = p->ntok_out, p->drs[p->ndrs].depth = p->depth;
			p->drs[p->ndrs].base = p->cx[d0].loc, p->drs[p->ndrs].ctr = p->cx[d0].cbase;
			p->drs[p->ndrs++].he = p->hc_last;
		}
	}
}

/* VCC_DRCOV: the replay context at the top of the stack is done */
VPRIV void drcov_end(pp *p)
{
	if (p->ndrs && p->drs[p->ndrs - 1].depth == p->depth) {
		p->ndrs--;
		if (p->ndrh < DRCOV_MAX && p->ntok_out > p->drs[p->ndrs].pos)
			p->drh[p->ndrh++] = (pp_hitreg){ p->drs[p->ndrs].pos, p->ntok_out, (uint32_t)p->nfile,
						    p->drs[p->ndrs].he };
	}
}

int pp_skip_top(pp *p)
{
	const pp_topev *s = p->ntev && !p->tev_full ? &p->tev[p->ntev - 1] : NULL;
	int d0 = -1;

	if (!s || s->end || !s->hit || s->pos + 1 != p->ntok_out)
		return 0;
	for (int d = p->depth - 1; d >= 0; d--)
		if (p->cx[d].type == C_REPLAY && p->cx[d].top == 2) {
			d0 = d;
			break;
		}
	if (d0 < 0 || p->cx[d0].he != s->e)
		return 0;
	for (int d = d0 + 1; d < p->depth; d++)
		if (p->cx[d].type != C_REPLAY)
			return 0;
	p->skipped_tok += p->cx[d0].he->total;
	while (p->ndrs && p->drs[p->ndrs - 1].depth > d0)
		p->ndrs--;	/* (VCC_DRCOV: dropped, not counted as a hit) */
	p->pend |= p->cx[d0].he->pend_out;
	p->depth = d0;
	p->ntok_out = s->pos;
	top_event(p, s->e, 0, 0, 1, 1);
	return 1;
}

/* an identifier's \\u/\\U escapes as UTF-8, in place (never longer), as clang -E */
static __attribute__((noinline, cold)) size_t ucn_utf8(char *s, size_t n)
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

/*
 * Per-region declaration replay (cc/replay.c): drop the rest of the
 * innermost open hit, whose first token was the last one delivered, as
 * pp_skip_top does for a top-level region.  0 if that is not the case.
 */
int pp_skip_hit(pp *p, int dry)
{
	int d0, top;

	if (!p->ndrs || p->drs[p->ndrs - 1].pos + 1 != p->ntok_out)
		return 0;
	d0 = p->drs[p->ndrs - 1].depth - 1;
	if (d0 < 0 || p->cx[d0].type != C_REPLAY || p->cx[d0].he != p->drs[p->ndrs - 1].he)
		return 0;
	for (int d = d0 + 1; d < p->depth; d++)
		if (p->cx[d].type != C_REPLAY)
			return 0;
	if (p->rec.on && !p->cx[d0].norec)
		return 0;	/* an enclosing header is being recorded: it needs the tokens */
	top = p->cx[d0].top;
	if (top == 2) {
		const pp_topev *t = p->ntev && !p->tev_full ? &p->tev[p->ntev - 1] : NULL;

		if (!t || t->end || !t->hit || t->pos + 1 != p->ntok_out || t->e != p->cx[d0].he)
			return 0;
		return dry ? 1 : pp_skip_top(p);
	}
	if (dry)
		return 1;
	p->skipped_tok += p->cx[d0].he->total;
	while (p->ndrs && p->drs[p->ndrs - 1].depth > d0)
		p->ndrs--;
	if (top)
		p->pend |= p->cx[d0].he->pend_out;
	p->depth = d0;
	p->ntok_out--;
	return 1;
}

VPRIV void out_nl(pp *p)
{
	if (unlikely(p->rec.on) && !p->rec.lev[p->rec.n - 1].text_started)
		text_start(p, 1);
	out_ch(p, '\n');
}

/* clang's WriteLineInfo: "# N "file" flags" on a line of its own */
static __attribute__((cold, noinline)) void lm_write(pp *p, uint32_t line, const char *extra)
{
	char b[16];
	int n = snprintf(b, sizeof(b), "# %u \"", line);

	if (p->col || p->edir)
		out_nl(p);
	for (int i = 0; i < n; i++)
		out_ch(p, b[i]);
	for (uint32_t i = 0; i < p->lm_nlen; i++) {	/* llvm's write_escaped */
		unsigned char c = (unsigned char)p->lm_name[i];

		if (c == '\\' || c == '"')
			out_ch(p, '\\'), out_ch(p, (char)c);
		else if (c == '\t')
			out_ch(p, '\\'), out_ch(p, 't');
		else if (c == '\n')
			out_ch(p, '\\'), out_ch(p, 'n');
		else if (c < 32 || c > 126) {
			out_ch(p, '\\');
			out_ch(p, (char)('0' + (c >> 6)));
			out_ch(p, (char)('0' + (c >> 3 & 7)));
			out_ch(p, (char)('0' + (c & 7)));
		} else
			out_ch(p, (char)c);
	}
	out_ch(p, '"');
	while (*extra)
		out_ch(p, *extra++);
	if (p->lm_sys)
		out_ch(p, ' '), out_ch(p, '3');
	if (p->lm_sys == 2)
		out_ch(p, ' '), out_ch(p, '4');
	out_nl(p);
	p->col = 0, p->edir = 0;
}

static void lm_file(pp *p, const char *name, uint8_t sys)
{
	p->lm_name = name;
	p->lm_nlen = (uint32_t)strlen(name);
	p->lm_sys = sys;
}

/* the predefined macros before the command line: clang's <built-in> has a
 * line for each, and the command line's marker is 3 lines after (1 with -undef) */
VPRIV void lm_count(pp *p)
{
	uint32_t n = 0;

	if (p->lm_n)
		return;
	for (uint32_t d = 0; d < NMDIR; d++) {
		const uintptr_t *pg = p->mdir[d];

		for (uint32_t k = 0; pg && k < MPAGE; k++) {
			const pmacro *m = (const pmacro *)(pg[k] & ~(uintptr_t)1);

			n += m && m->kind != MK_BUILTIN;
		}
	}
	p->lm_n = n + 1;
}

/* <built-in>'s command line, after the predefined macros */
static __attribute__((cold, noinline)) void lm_builtin_block(pp *p)
{
	lm_count(p);
	p->lm_blk = 1;
	move_to_line(p, p->lm_n - 1 + (p->o->markers == 2 ? 1 : 3), false);
	lm_file(p, "<command line>", 0);
	lm_write(p, 1, " 1");
	lm_file(p, "<built-in>", 0);
	lm_write(p, 1, " 2");
	p->cur_line = 1;
	p->lm_bline = 1;	/* <built-in>'s line where -imacros/-include start */
	for (unsigned i = 0; i < p->o->predef_incl; i++)
		p->lm_bline += p->o->predef[i] == '\n';
}

/* a file entered: its marker (after the #include's line was moved to) */
VPRIV void lm_enter(pp *p, const fent *e)
{
	lm_file(p, e->spath, e->sys);
	p->cur_line = 1;
	lm_write(p, 1, " 1");
}

/* #line N ["file"] / # N "file": clang's FileChanged(RenameFile) */
VPRIV void lm_rename(pp *p, const char *name, uint32_t nlen, uint32_t line)
{
	if (name)
		p->lm_name = name, p->lm_nlen = nlen;
	p->cur_line = line;
	lm_write(p, line, "");
}

/* clang's PrintPPOutputPPCallbacks::MoveToLine (-P: MinimizeWhitespace off) */
VPRIV bool move_to_line(pp *p, uint32_t line, bool req)
{
	bool nl = false;

	if ((req && p->col) || p->edir) {
		out_nl(p);
		nl = true;
		p->cur_line++;
		p->col = 0, p->edir = 0;
	}
	if (p->cur_line == line)
		;
	else if (unlikely(p->lmark)) {
		if (line - p->cur_line <= 8)
			for (uint32_t i = p->cur_line; i < line; i++)
				out_nl(p);
		else
			lm_write(p, line, "");
		nl = true;
	}
	else if (!nl && line - p->cur_line == 1)
		out_nl(p), nl = true;
	else if (!nl && p->col)
		out_nl(p), nl = true;
	if (nl)
		p->col = 0, p->edir = 0;
	p->cur_line = line;
	return nl;
}

/* FileChanged: entering a file from an #include in `from` moves to its line,
 * leaving it puts the output on the line after (clang's IncludeLoc) */
VPRIV void text_file_change(pp *p, const ctx *from, int enter)
{
	uint32_t l = 1;

	if (!p->out_text)
		return;
	if (unlikely(p->lmark)) {
		const fent *e;

		if (from && from->f == &p->builtin) {
			if (!p->lm_blk)
				lm_builtin_block(p);
			/* clang's <built-in> after the command line: a line per -include, 2 per -imacros */
			l = loc_line(p, from->loc + from->vt[from->i - 1].off) - p->lm_bline + 1 + !enter;
			if (enter)
				move_to_line(p, l, false);
			if (!enter) {
				lm_file(p, "<built-in>", 0);
				p->cur_line = l;
				lm_write(p, l, " 2");
			}
			return;	/* enter: the -include line is the current one */
		}
		if (!enter && !p->lm_blk)
			lm_builtin_block(p);	/* <built-in> ends */
		if (from && from->type == C_FILE && from->i)
			l = loc_line(p, from->loc + from->vt[from->i - 1].off);
		if (enter) {
			move_to_line(p, l, false);
			return;	/* the marker when the file is known (lm_enter) */
		}
		e = &p->fe[from->fe];
		lm_file(p, e->spath, e->sys);
		if (from->i) {
			uint32_t pl;
			const char *pn = presumed_name(p, from->fe, from->loc + from->vt[from->i - 1].off, &pl);

			if (pn != e->name)
				p->lm_name = pn, p->lm_nlen = pl;
			l++;
		}
		p->cur_line = l;
		lm_write(p, l, " 2");
		return;
	}
	if (from && from->type == C_FILE && from->i)
		l = loc_line(p, from->loc + from->vt[from->i - 1].off);
	else if (enter)
		return;
	if (enter) {
		if (from->f != &p->builtin || from->i)
			move_to_line(p, l, false);
		p->cur_line = 1;
	} else {
		p->cur_line = from && from->type == C_FILE && from->i ? l + 1 : 1;
	}
	if (p->col || p->edir) {	/* startNewLineIfNeeded */
		out_nl(p);
		p->col = 0, p->edir = 0;
	}
}

/* -C: the start of the line after s (past a directive's tokens): a block
 * comment that starts on the line continues it */
VPRIV const char *cmt_line_end(const char *s)
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
static void cmt_print(pp *p, const ctx *c, const char *s, const char *e)
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

		if (move_to_line(p, line, p->edir) || (sol && line == 1)) {
			out_reserve(p, col);
			for (; col > 1; col--)
				p->out[p->nout++] = ' ';
		} else if (space) {
			out_ch(p, ' ');
		}
	} else if (space || (p->col && p->pk == T_SLASH && p->pk_end != s)) {
		out_ch(p, ' ');
	}
	out_put(p, s, (size_t)(e - s));
	for (const char *q = s; q < e; q++)
		p->cur_line += *q == '\n';
	p->col = 1, p->sol_on = 0;
	p->pk = T_EOF, p->pk_end = e;
}

/* -C: print the comments of file context c from its position up to upto */
VPRIV void cmt_flush(pp *p, ctx *c, const char *upto)
{
	const char *s = p->fe[c->fe].cpos;

	if (!s || upto <= s) {
		if (s && upto > s)
			p->fe[c->fe].cpos = upto;
		return;
	}
	while (s < upto) {
		if (s[0] == '/' && s[1] == '*') {
			const char *e = s + 2;

			while (e < upto && !(e[0] == '*' && e[1] == '/'))
				e++;
			if (e >= upto)
				break;
			cmt_print(p, c, s, e + 2);
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
			cmt_print(p, c, s, e);
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
	p->fe[c->fe].cpos = upto;
}

/* -C: the file context's comments up to the end of token rp are dropped (macro arguments) */
VPRIV void cmt_skip_to(pp *p, const ptok *rp)
{
	ctx *fc = &p->cx[p->depth - 1];

	if (fc->type == C_FILE && rp->s >= fc->src && rp->s < fc->src + fc->f->size && p->fe[fc->fe].cpos && p->fe[fc->fe].cpos < rp->s)
		p->fe[fc->fe].cpos = rp->s + rp->len;
}

/* clang's TokenConcatenation::AvoidConcat: would prev followed by t, unspaced, lex differently? */
static bool avoid_concat(const pp *p, const ptok *t, char fc)
{
	switch (p->pk) {
	case T_AMP: case T_PLUS: case T_MINUS: case T_SLASH: case T_LESS: case T_GREATER: case T_PIPE:
	case T_PERCENT: case T_STAR: case T_EXCLAIM: case T_LESSLESS: case T_GREATERGREATER: case T_CARET:
	case T_EQUAL:
		if (t->kind == T_EQUAL || t->kind == T_EQUALEQUAL)
			return true;
		break;
	}
	switch (p->pk) {
	case T_IDENT:
		if (t->kind == T_NUMBER)
			return fc != '.';
		if (t->kind == T_IDENT || (t->kind >= T_WCHAR && t->kind <= T_U32CHAR) || (t->kind >= T_WSTRING && t->kind <= T_U32STRING))
			return true;
		return (t->kind == T_CHAR || t->kind == T_STRING) && p->pk_L;
	case T_NUMBER:
		return idch((unsigned char)fc) || fc == '.' || fc == '+' || fc == '-' || fc == '\\';
	case T_PERIOD: return (fc == '.' && p->pk_pp) || (fc >= '0' && fc <= '9');
	case T_AMP: return fc == '&';
	case T_PLUS: return fc == '+';
	case T_MINUS: return fc == '-' || fc == '>';
	case T_SLASH: return fc == '*' || fc == '/';
	case T_LESS: return fc == '<' || fc == ':' || fc == '%';
	case T_GREATER: return fc == '>';
	case T_PIPE: return fc == '|';
	case T_PERCENT: return fc == '>' || fc == ':';
	case T_COLON: return fc == '>';
	case T_HASH: return fc == '#' || fc == '@' || fc == '%';
	default: return false;
	}
}

/* the expansion column of t (1-based, bytes) */
VPRIV __attribute__((noinline)) unsigned tok_col(const pp *p, uint32_t loc)
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
static inline void render(pp *p, const ptok *t)
{
	bool sol = t->kind == P_DIRECTIVE || (t->flags & TF_SOL);
	size_t l;
	const char *sp;
	char fc;

	if (unlikely(p->out_text > 1) && t->kind != P_DIRECTIVE) {
		ctx *fc = &p->cx[p->depth - 1];

		if (fc->type == C_FILE && !(t->flags & PF_MACRO) && p->fe[fc->fe].cpos && t->s >= p->fe[fc->fe].cpos &&
		    t->s < fc->src + fc->f->size) {
			cmt_flush(p, fc, t->s);
			p->fe[fc->fe].cpos = t->s + t->len;
		}
		p->cmt_sol = 0;	/* taken by a comment printed now, else by this token */
	}
	if (unlikely(p->rec.on) && !p->rec.lev[p->rec.n - 1].text_started) {
		if (!sol)
			HC_BAD(p);	/* its separator depends on what was printed before */
		text_start(p, sol);
	}
	if (t->kind == P_DIRECTIVE && t->id) {
		/* an assembler line passed through: tokens, so indented as clang does */
		unsigned c = tok_col(p, t->loc);

		move_to_line(p, loc_line(p, t->loc), p->edir);
		out_reserve(p, t->len + c + 2);
		if (!p->col)
			for (p->out[p->nout++] = ' '; c > 2; c--)
				p->out[p->nout++] = ' ';
		else
			p->out[p->nout++] = ' ';
		p->out[p->nout++] = '#';
		memcpy(p->out + p->nout, t->s, t->len);
		p->nout += t->len;
		p->col = 1, p->sol_on = 0;
		p->pk = T_EOF, p->pk_end = NULL;
		return;
	}
	if (t->kind == P_DIRECTIVE) {
		uint32_t line = loc_line(p, t->loc);

		/* clang prints "assume_nonnull end" at the line of what it printed last */
		if (t->len == 31 && !memcmp(t->s, "pragma clang assume_nonnull end", 31))
			line = p->cur_line;
		move_to_line(p, line, true);
		out_reserve(p, t->len + 2);
		p->out[p->nout++] = '#';
		memcpy(p->out + p->nout, t->s, t->len);
		p->nout += t->len;
		p->col = 1, p->edir = 1, p->sol_on = 0;
		p->pk = T_EOF, p->pk_end = NULL;
		return;
	}
	fc = t->s[0];	/* a token never starts with a splice */
	if (unlikely(p->emb_sp) && !p->edir) {
		/* after #embed's data clang prints the next token after one space, on the same line */
		p->emb_sp = (t->flags & PF_EMBEND) != 0;
		if (p->emb_tail != 1)	/* after the bytes one space, after suffix()/if_empty() none */
			out_ch(p, ' ');
		if (p->emb_tail)
			p->emb_tail--;
		goto spaced;
	}
	p->emb_sp = (t->flags & PF_EMBEND) != 0;
	if (sol || p->edir) {
		/* HandleWhitespaceBeforeTok: indented to its column if it starts a new line */
		uint32_t loc = p->sol_on ? p->sol_loc : p->cx[p->depth - 1].type != C_FILE ? p->exp_loc : t->loc;
		uint32_t line = loc_line(p, loc);
		unsigned c = tok_col(p, loc);
		/* clang's leading space: not the line break before a token in column 1 */
		bool space = (t->flags & TF_SPACE) && (p->sol_on || c > 1) && p->pk_end != t->s;

		if (move_to_line(p, line, p->edir) || (sol && line == 1)) {
			if (c == 1 && space)
				c = 2;	/* after an empty expansion in column 1 */
			out_reserve(p, c + 1);
			if (c <= 1 && t->kind == T_HASH)
				p->out[p->nout++] = ' ';
			for (; c > 1; c--)
				p->out[p->nout++] = ' ';
		} else if (space || (p->col && p->pk_end != t->s && p->pk != T_EOF && avoid_concat(p, t, fc))) {
			out_ch(p, ' ');
		}
	} else if (((t->flags & TF_SPACE) && p->pk_end != t->s) ||
		   (p->col && p->pk_end != t->s && p->pk != T_EOF && avoid_concat(p, t, fc))) {
		out_ch(p, ' ');
	}
spaced:
	out_reserve(p, t->len);
	sp = pp_spell(t, p->out + p->nout, &l);
	if (sp != p->out + p->nout)
		memcpy(p->out + p->nout, sp, l);
	if (t->kind == T_IDENT && !p->o->asm_mode && unlikely(memchr(p->out + p->nout, '\\', l) != NULL))
		l = ucn_utf8(p->out + p->nout, l);
	if (unlikely(t->ctr) && p->rec.on)
		for (uint16_t r = t->ctr; r; r = p->rec.cp[r].next) {
			const cprov *c = &p->rec.cp[r];

			if (p->rec.nctr < HC_MAXCP)
				p->rec.ctr[p->rec.nctr++] = (hcpos){ p->nout + c->off, c->len, c->val };
			else
				HC_BAD(p);
		}
	p->sol_on = 0;
	p->pk_pp = p->pk == T_PERIOD;
	p->pk = t->kind;
	p->pk_L = t->kind == T_IDENT && l == 1 && p->out[p->nout] == 'L';
	p->pk_end = t->flags & TF_DIRTY ? NULL : t->s + t->len;
	p->nout += l;
	p->col++;
}

int pp_text(pp *p, const char **out, size_t *len)
{
	ptok t;

	hc_mode(p, 1);
	p->out_text = p->o->comments ? 2 : 1;	/* 2: -C */
	p->cur_line = 1;
	if (unlikely(p->o->markers)) {
		p->lmark = 1;
		lm_file(p, p->fe[0].spath, p->fe[0].sys);
		lm_write(p, 1, "");
		lm_file(p, "<built-in>", 0);
		lm_write(p, 1, " 1");
		p->lm_sys = 1;
		lm_write(p, 1, "");
	}
	if (p->o->asm_mode)
		p->hc = 0;	/* cached text is C-spaced */
	for (;;) {
		next_tok(p, &t);
		if (unlikely(p->o->dump_defines) && !p->dd_done)
			dd_flush(p);	/* the predefined macros before the first token */
		if (t.kind == T_EOF)
			break;
		render(p, &t);
	}
	out_ch(p, '\n');	/* clang ends with a newline, also after an empty line */
	*out = p->out ? p->out : "";
	*len = p->nout;
	return p->errors;
}

void pp_hc_stats(char *buf, size_t n)
{
	snprintf(buf, n, "header cache: %lu hits, %lu misses (%lu recorded, %lu uncacheable, %lu not stored), "
		 "%.1f MB replayed, %.1f MB recorded, %.1f MB stored",
		 (unsigned long)hstat.hit, (unsigned long)hstat.miss, (unsigned long)hstat.rec,
		 (unsigned long)hstat.uncacheable, (unsigned long)hstat.full, hstat.bytes_replayed / 1e6,
		 hstat.bytes_rec / 1e6, hstat.mem / 1e6);
	if (getenv("VPP_HC_DEBUG")) {
		size_t l = strlen(buf);

		snprintf(buf + l, n - l, "\n  stored: %lu undef reads, %lu other reads, %lu writes, %lu files",
			 (unsigned long)hstat.nru, (unsigned long)hstat.nrv, (unsigned long)hstat.ndl,
			 (unsigned long)hstat.nfe);
		l = strlen(buf);
		snprintf(buf + l, n - l, "\n  bytes: reads %.1f MB, delta %.1f MB, once %.1f MB, files %.1f MB, text %.1f MB, "
			 "segs %.1f MB, entries %.1f MB",
			 hstat.b_rd / 1e6, hstat.b_dl / 1e6, hstat.b_once / 1e6, hstat.b_fe / 1e6, hstat.b_text / 1e6,
			 hstat.b_seg / 1e6, hstat.b_ent / 1e6);
	}
}

int pp_errors(const pp *p)
{
	return p->errors;
}

void pp_set_fatal(pp *p, jmp_buf *jb)
{
	p->fatal_jb = jb;
}

/* the distinct file names in entry order (a hash set: TUs enter ~1000 files) */
size_t pp_files(const pp *p, const char **names, size_t max, int nosys)
{
	uint32_t cap = 64, stk[1024], *tab;
	size_t n = 0;

	while (cap < 2 * p->nfe)
		cap *= 2;
	tab = cap <= 1024 ? stk : malloc(cap * sizeof(*tab));
	if (!tab)
		abort();
	memset(tab, 0, cap * sizeof(*tab));
	for (uint32_t i = 0; i < p->nfe; i++) {
		const char *s = p->fe[i].spath;
		uint64_t h = 14695981039346656037ull;
		size_t l = 0;
		uint32_t k;

		if (p->fe[i].f == &p->builtin || (nosys && p->fe[i].sys))
			continue;
		for (; s[l]; l++)
			h = (h ^ (unsigned char)s[l]) * 1099511628211ull;
		for (k = (uint32_t)(h ^ (h >> 32)) & (cap - 1); tab[k]; k = (k + 1) & (cap - 1))
			if (!strcmp(names[tab[k] - 1], s))
				break;
		if (!tab[k] && n < max) {
			names[n++] = s;
			tab[k] = (uint32_t)n;
		}
	}
	if (tab != stk)
		free(tab);
	return n;
}

void pp_close(pp *p)
{
	/* back to the thread: the stacks and tmp (reused all along the TU) as they are */
	vmreg_put(&ppr[1], VMREG_SCRATCH);
	vmreg_put(&ppr[2], (size_t)(p->perm.p - p->perm.base) + 64);
	vmreg_put(&ppr[3], VMREG_SCRATCH);
	vmreg_put(&ppr[4], VMREG_SCRATCH);
	vmreg_put(&ppr[5], ((size_t)p->nfe + 1) * sizeof(fent));
	vmreg_put(&ppr[6], ((size_t)p->nlm + 1) * sizeof(*p->lm));
	if (p->out) {
		if (out_spare_cap < p->capout) {
			if (out_spare)
				munmap(out_spare, out_spare_cap);
			out_spare = p->out, out_spare_cap = p->capout;
		} else {
			munmap(p->out, p->capout);
		}
	}
	if (p->rec.tk) {
		if (!tk_spare) {
			tk_spare = p->rec.tk, sb_spare = p->rec.sb;
		} else {
			munmap(p->rec.tk, HC_MAXTK * sizeof(ptok));
			munmap(p->rec.sb, HC_MAXSB);
		}
	}
	if (p->rec.t && !rec_spare.t) {
		rec_spare.t = p->rec.t, rec_spare.ord = p->rec.ord, rec_spare.ctr = p->rec.ctr;
		rec_spare.crd = p->rec.crd, rec_spare.seg = p->rec.seg, rec_spare.cp = p->rec.cp;
		rec_spare.kid = p->rec.kid, rec_spare.kex = p->rec.kex, rec_spare.lk = p->rec.lk;
	} else if (p->rec.t) {
		munmap(p->rec.t, HC_MAXT * sizeof(htouch));
		munmap(p->rec.ord, HC_MAXORD * sizeof(honce));
		munmap(p->rec.ctr, HC_MAXCP * sizeof(hcpos));
		munmap(p->rec.crd, HC_MAXCRD * sizeof(hcrd));
		munmap(p->rec.seg, HC_MAXSEG * sizeof(hsegr));
		munmap(p->rec.cp, (HC_MAXCP + 1) * sizeof(cprov));
		munmap(p->rec.lk, HC_MAXLK * sizeof(hlook));
	}
	vmreg_put(&ppr[0], sizeof(pp));
	vmpool_give(&pppool, ppr);
	ppr = NULL;
}
