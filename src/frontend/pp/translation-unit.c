/* vcc preprocessor: opening, closing and describing one translation unit (pp_open, pp_close) */
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

/* the main file from stdin (pp_opts.main_src): lexed here as <built-in> is, its bytes immortal like a cached file's */
static __attribute__((noinline, cold)) const vlex_file *stdin_file(pp *p, const pp_opts *o)
{
	size_t ml = o->main_len;
	char *ms = salloc(ml + 129);
	vlex_file *f = varena_alloc(&p->perm, sizeof(*f));
	vtok *mt = varena_alloc(&p->perm, (ml + 2) * sizeof(vtok));
	void *scratch = vmreg_get(&ppr[7], vlex_scratch_size(ml) + 4096, "pp.lexscratch");

	memcpy(ms, o->main_src, ml);
	memset(ms + ml, 0, 129);
	memset(f, 0, sizeof(*f));
	f->path = o->main;
	f->src = ms;
	f->size = (uint32_t)ml;
	f->ntok = vlex(ms, ml, mt, scratch);
	f->tok = mt;
	vmreg_put(&ppr[7], VMREG_SCRATCH);
	return f;
}

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
	p->wc_o = NULL;	/* (the region may be a previous TU's) */
	{
		const char *t = getenv("VCC_TRACK_DIAG");

		p->dtrack = t && *t && *t != '0';
	}
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

	if (o->main_src) {
		mf = stdin_file(p, o);
		if (o->trigraphs) mf = vlex_trigraphs(mf);
		if (o->c89) mf = vlex_c89(mf);
		if (o->no_digraphs) mf = vlex_nodigraphs(mf);
		if (o->c23) mf = vlex_digitsep(mf);
	} else {
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
	if (o->no_digraphs)
		p->builtin = *vlex_nodigraphs(&p->builtin);
	enter_file(p, &p->builtin, "<built-in>", -1);
	return p;
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
	diag_text_flush(p);	/* a record still waiting for its notes (no summary ran) */
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
