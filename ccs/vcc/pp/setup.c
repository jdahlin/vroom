/* vcc preprocessor: opening and closing a TU, pp_next and the queries on the result */
#include <sys/stat.h>
#include "ppimpl.h"

VPRIV void *vm(size_t n)
{
	void *p = mmap(NULL, n, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);

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
	{ "__has_include_next", B_HAS_INCLUDE_NEXT },
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

/* once: a thread that saw the flag but not hc_mem_max stored without a cap (TSan) */
static void hc_env(void)
{
	const char *m = getenv("VPP_HC_MAX_MB");

	hc_mem_max = m ? (size_t)atol(m) << 20 : (size_t)8 << 30;
	hc_off = getenv("VPP_NOHC") != NULL;
}

static void hc_mode(pp *p, int mode)
{
	static pthread_once_t once = PTHREAD_ONCE_INIT;

	pthread_once(&once, hc_env);
	p->hc = !hc_off && !p->o->show_includes && !p->o->dump_defines;	/* -H: a replay has no nesting to print */
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

/* the token as clang -E -P prints it */
static inline void render(pp *p, const ptok *t)
{
	bool sol = t->kind == P_DIRECTIVE || (t->flags & TF_SOL);
	size_t l;
	const char *sp;

	if (p->col) {
		/* assembler-with-cpp: keep the source spacing like clang does, as
		 * gas syntax is space-sensitive ("\\arg", "1:", ".L x") */
		if (sol || !p->o->asm_mode || (t->flags & TF_SPACE))
			out_ch(p, sol ? '\n' : ' ');
		if (sol)
			p->col = 0;
	}
	if (unlikely(p->rec.on) && !p->rec.lev[p->rec.n - 1].text_started)
		text_start(p, sol);
	if (t->kind == P_DIRECTIVE) {
		out_reserve(p, t->len + 2);
		p->out[p->nout++] = '#';
		memcpy(p->out + p->nout, t->s, t->len);
		p->nout += t->len;
		p->out[p->nout++] = '\n';
		p->col = 0;
		return;
	}
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
	p->nout += l;
	p->col++;
}

int pp_text(pp *p, const char **out, size_t *len)
{
	ptok t;

	hc_mode(p, 1);
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
	if (p->col)
		out_ch(p, '\n');
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
		rec_spare.kid = p->rec.kid, rec_spare.kex = p->rec.kex;
	} else if (p->rec.t) {
		munmap(p->rec.t, HC_MAXT * sizeof(htouch));
		munmap(p->rec.ord, HC_MAXORD * sizeof(honce));
		munmap(p->rec.ctr, HC_MAXCP * sizeof(hcpos));
		munmap(p->rec.crd, HC_MAXCRD * sizeof(hcrd));
		munmap(p->rec.seg, HC_MAXSEG * sizeof(hsegr));
		munmap(p->rec.cp, (HC_MAXCP + 1) * sizeof(cprov));
	}
	vmreg_put(&ppr[0], sizeof(pp));
	vmpool_give(&pppool, ppr);
	ppr = NULL;
}
