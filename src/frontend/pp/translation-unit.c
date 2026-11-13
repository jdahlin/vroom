/* vcc preprocessor: opening, closing and describing one translation unit (pp_open, pp_close) */
#include <sys/stat.h>
#include "pp-internal.h"

/* address space, backed as touched: NORESERVE, or Linux's heuristic overcommit
   refuses the 16 GB shared region on a machine with less RAM + swap */
VPRIV void *pp_address_space_reserve(size_t n)
{
	void *p = mmap(NULL, n, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON | MAP_NORESERVE, -1, 0);

	if (p == MAP_FAILED) {
		perror("vcc: mmap");
		abort();
	}
	return p;
}

/* the builtin macros: process-wide, so equal in every TU */
static const struct { const char *n; int b; } pp_builtin_macro_names[] = {
	{ "__FILE__", PP_BUILTIN_FILE }, { "__LINE__", PP_BUILTIN_LINE }, { "__COUNTER__", PP_BUILTIN_COUNTER },
	{ "__DATE__", PP_BUILTIN_DATE }, { "__TIME__", PP_BUILTIN_TIME }, { "__TIMESTAMP__", PP_BUILTIN_TIMESTAMP },
	{ "__BASE_FILE__", PP_BUILTIN_BASE_FILE }, { "__FILE_NAME__", PP_BUILTIN_FILE_NAME },
	{ "__INCLUDE_LEVEL__", PP_BUILTIN_INCLUDE_LEVEL }, { "_Pragma", PP_BUILTIN_PRAGMA },
	{ "__has_attribute", PP_BUILTIN_HAS_ATTRIBUTE }, { "__has_builtin", PP_BUILTIN_HAS_BUILTIN },
	{ "__has_feature", PP_BUILTIN_HAS_FEATURE }, { "__has_extension", PP_BUILTIN_HAS_EXTENSION },
	{ "__has_c_attribute", PP_BUILTIN_HAS_C_ATTRIBUTE },	/* __has_cpp_attribute: not defined in C (clang) */
	{ "__has_declspec_attribute", PP_BUILTIN_HAS_DECLSPEC_ATTRIBUTE }, { "__has_warning", PP_BUILTIN_HAS_WARNING },
	{ "__is_identifier", PP_BUILTIN_IS_IDENTIFIER }, { "__has_include", PP_BUILTIN_HAS_INCLUDE },
	{ "__has_include_next", PP_BUILTIN_HAS_INCLUDE_NEXT }, { "__has_embed", PP_BUILTIN_HAS_EMBED },
	{ "__has_constexpr_builtin", PP_BUILTIN_HAS_CONSTEXPR_BUILTIN },
	{ "__is_target_arch", PP_BUILTIN_IS_TARGET_ARCHITECTURE }, { "__is_target_os", PP_BUILTIN_IS_TARGET_OS },
	{ "__is_target_vendor", PP_BUILTIN_IS_TARGET_VENDOR },
	{ "__is_target_environment", PP_BUILTIN_IS_TARGET_ENVIRONMENT },
	{ "__is_target_variant_os", PP_BUILTIN_IS_TARGET_VARIANT_OS },
	{ "__is_target_variant_environment", PP_BUILTIN_IS_TARGET_VARIANT_ENVIRONMENT },
	{ "__building_module", PP_BUILTIN_BUILDING_MODULE },
};

static const pp_macro *pp_builtin_macros[sizeof(pp_builtin_macro_names) / sizeof(pp_builtin_macro_names[0])];

static void pp_builtin_macros_init(void)
{
	for (size_t i = 0; i < sizeof(pp_builtin_macro_names) / sizeof(pp_builtin_macro_names[0]); i++) {
		pp_macro *m = pp_shared_allocate(sizeof(*m));

		memset(m, 0, sizeof(*m));
		m->kind = PP_MACRO_BUILTIN;
		m->builtin = (uint8_t)pp_builtin_macro_names[i].b;
		m->name = vintern_cstr(pp_builtin_macro_names[i].n);
		m->body = pp_macro_empty_body;
		pp_builtin_macros[i] = m;
	}
}

/* a TU's tables, reused from one TU to the next (pp_open takes a set, pp_close gives it back) */
static vjob_region_pool pp_translation_unit_pool = VJOB_REGION_POOL_INIT(8);
static _Thread_local vjob_region *pp_translation_unit_regions;

/* the main file from stdin (pp_opts.main_src): lexed here as <built-in> is, its bytes immortal like a cached file's */
static __attribute__((noinline, cold)) const lex_file *pp_stdin_file_lex(pp *p, const pp_opts *o)
{
	size_t ml = o->main_len;
	char *ms = pp_shared_allocate(ml + 129);
	lex_file *f = varena_alloc(&p->perm, sizeof(*f));
	lex_token *mt = varena_alloc(&p->perm, (ml + 2) * sizeof(lex_token));
	void *lex_scratch = vjob_region_acquire(&pp_translation_unit_regions[7], lex_scratch_size(ml) + 4096, "pp.lexscratch");

	memcpy(ms, o->main_src, ml);
	memset(ms + ml, 0, 129);
	memset(f, 0, sizeof(*f));
	f->path = o->main;
	f->src = ms;
	f->size = (uint32_t)ml;
	f->ntok = lex_tokenize(ms, ml, mt, lex_scratch);
	f->tok = mt;
	vjob_region_release(&pp_translation_unit_regions[7], VJOB_REGION_SCRATCH);
	return f;
}

pp *pp_open(const pp_opts *o)
{
	pp *p = vjob_region_acquire(&(pp_translation_unit_regions = vjob_region_pool_take(&pp_translation_unit_pool))[0], sizeof(pp), "pp");
	const lex_file *mf;
	size_t n = strlen(o->predef);
	char *src;
	lex_token *tok;
	void *lex_scratch;

	vintern_init();
	p->o = o;
	p->warning_cache_options = NULL;	/* (the region may be a previous TU's) */
	{
		const char *t = getenv("VCC_TRACK_DIAG");

		p->diagnostic_track = t && *t && *t != '0';
	}
	p->color = o->diag_color > 0 || (o->diag_color == 0 && isatty(o->errfd ? o->errfd : 2));
	p->cx = vjob_region_acquire(&pp_translation_unit_regions[1], PP_CONTEXT_MAX * sizeof(pp_context), "pp.cx");
	p->perm.base = p->perm.p = vjob_region_acquire(&pp_translation_unit_regions[2], 1ull << 30, "pp.perm");
	p->perm.end = p->perm.base + (1ull << 30);
	p->tmp.base = p->tmp.p = vjob_region_acquire(&pp_translation_unit_regions[3], 1ull << 32, "pp.tmp");
	p->tmp.end = p->tmp.base + (1ull << 32);
	p->psig = varena_alloc(&p->perm, ((size_t)o->ndirs + 1) * sizeof(uint32_t));
	if (!pp_header_cache_signatures_get(o, &p->sig, p->psig))
		p->sig = PP_HEADER_CACHE_NONE;
	p->sv = vjob_region_acquire(&pp_translation_unit_regions[4], PP_SCRATCH_TOKENS_MAX * sizeof(ptok), "pp.sv");
	p->fe = vjob_region_acquire(&pp_translation_unit_regions[5], (1u << 20) * sizeof(pp_file_entry), "pp.fe");
	p->once = varena_alloc(&p->perm, 256 * sizeof(*p->once));
	p->once_file_capacity = 256;
	p->pushed = varena_alloc(&p->perm, 4096 * sizeof(*p->pushed));
	p->lm = vjob_region_acquire(&pp_translation_unit_regions[6], 65536 * sizeof(*p->lm), "pp.lm");
	{
		static pthread_once_t once = PTHREAD_ONCE_INIT;

		pthread_once(&once, pp_builtin_macros_init);
		for (size_t i = 0; i < sizeof(pp_builtin_macro_names) / sizeof(pp_builtin_macro_names[0]); i++)
			pp_macro_table_set(p, pp_builtin_macros[i]->name, pp_builtin_macros[i]);
	}

	if (o->main_src) {
		mf = pp_stdin_file_lex(p, o);
		if (o->trigraphs) mf = lex_file_trigraphs_replace(mf);
		if (o->c89) mf = lex_file_c89_convert(mf);
		if (o->no_digraphs) mf = lex_file_without_digraphs(mf);
		if (o->c23) mf = lex_file_digit_separators_remove(mf);
	} else {
		/* the caller's string is unpadded; lex_file_get hashes by words (ASan) */
		size_t ml = strlen(o->main);
		char *mp = varena_alloc(&p->perm, ml + 16);

		memcpy(mp, o->main, ml);
		memset(mp + ml, 0, 16);
		mf = pp_path_try(o, mp, ml);
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

		p->fatal_jump = &jb;
		if (setjmp(jb)) {
			pp_diag_summary(p);	/* ("1 error generated.", as clang) */
			pp_close(p);
			return NULL;
		}
		pp_file_enter(p, mf, o->main, -1);
		p->fatal_jump = NULL;
	}
	p->base_file_name = p->fe[0].name;
	p->base_file_name_length = p->fe[0].namelen;

	/* "<built-in>": the predefines, -D/-U and -include, on top of the main file */
	src = pp_shared_allocate(n + 129);	/* interned identifiers point into it: immortal */
	memcpy(src, o->predef, n);
	memset(src + n, 0, 129);
	tok = varena_alloc(&p->perm, (n + 2) * sizeof(lex_token));
	lex_scratch = vjob_region_acquire(&pp_translation_unit_regions[7], lex_scratch_size(n) + 4096, "pp.lexscratch");
	p->builtin.path = "<built-in>";
	p->builtin.src = src;
	p->builtin.size = (uint32_t)n;
	p->builtin.ntok = lex_tokenize(src, n, tok, lex_scratch);
	p->builtin.tok = tok;
	vjob_region_release(&pp_translation_unit_regions[7], VJOB_REGION_SCRATCH);
	if (o->no_digraphs)
		p->builtin = *lex_file_without_digraphs(&p->builtin);
	pp_file_enter(p, &p->builtin, "<built-in>", -1);
	return p;
}
int pp_error_count(const pp *p)
{
	return p->errors;
}

void pp_fatal_jump_set(pp *p, jmp_buf *jb)
{
	p->fatal_jump = jb;
}

/* the distinct file names in entry order (a hash set: TUs enter ~1000 files) */
size_t pp_file_names(const pp *p, const char **lex_token_kind_names, size_t max, int nosys)
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
			if (!strcmp(lex_token_kind_names[tab[k] - 1], s))
				break;
		if (!tab[k] && n < max) {
			lex_token_kind_names[n++] = s;
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
	vjob_region_release(&pp_translation_unit_regions[1], VJOB_REGION_SCRATCH);
	vjob_region_release(&pp_translation_unit_regions[2], (size_t)(p->perm.p - p->perm.base) + 64);
	vjob_region_release(&pp_translation_unit_regions[3], VJOB_REGION_SCRATCH);
	vjob_region_release(&pp_translation_unit_regions[4], VJOB_REGION_SCRATCH);
	vjob_region_release(&pp_translation_unit_regions[5], ((size_t)p->nfe + 1) * sizeof(pp_file_entry));
	vjob_region_release(&pp_translation_unit_regions[6], ((size_t)p->nlm + 1) * sizeof(*p->lm));
	if (p->out) {
		if (pp_output_spare_capacity < p->output_capacity) {
			if (pp_output_spare)
				munmap(pp_output_spare, pp_output_spare_capacity);
			pp_output_spare = p->out, pp_output_spare_capacity = p->output_capacity;
		} else {
			munmap(p->out, p->output_capacity);
		}
	}
	if (p->rec.tk) {
		if (!pp_recording_spare_tokens) {
			pp_recording_spare_tokens = p->rec.tk, pp_recording_spare_spellings = p->rec.sb;
		} else {
			munmap(p->rec.tk, PP_HEADER_CACHE_TOKEN_MAX * sizeof(ptok));
			munmap(p->rec.sb, PP_HEADER_CACHE_SPELLING_BYTES_MAX);
		}
	}
	if (p->rec.t && !pp_recording_spare_buffers.t) {
		pp_recording_spare_buffers.t = p->rec.t, pp_recording_spare_buffers.ord = p->rec.ord, pp_recording_spare_buffers.ctr = p->rec.ctr;
		pp_recording_spare_buffers.crd = p->rec.crd, pp_recording_spare_buffers.seg = p->rec.seg, pp_recording_spare_buffers.cp = p->rec.cp;
		pp_recording_spare_buffers.kid = p->rec.kid, pp_recording_spare_buffers.kex = p->rec.kex, pp_recording_spare_buffers.lk = p->rec.lk;
	} else if (p->rec.t) {
		munmap(p->rec.t, PP_HEADER_CACHE_TOUCH_MAX * sizeof(pp_header_cache_touch_record));
		munmap(p->rec.ord, PP_HEADER_CACHE_ONCE_MARK_MAX * sizeof(pp_header_cache_once_mark));
		munmap(p->rec.ctr, PP_HEADER_CACHE_COUNTER_PROVENANCE_MAX * sizeof(pp_header_cache_counter_position));
		munmap(p->rec.crd, PP_HEADER_CACHE_COUNTER_READ_MAX * sizeof(pp_header_cache_counter_read));
		munmap(p->rec.seg, PP_HEADER_CACHE_SEGMENT_MAX * sizeof(pp_header_cache_recorded_segment));
		munmap(p->rec.cp, (PP_HEADER_CACHE_COUNTER_PROVENANCE_MAX + 1) * sizeof(pp_header_cache_counter_provenance));
		munmap(p->rec.lk, PP_HEADER_CACHE_LOOKUP_MAX * sizeof(pp_header_cache_lookup));
	}
	vjob_region_release(&pp_translation_unit_regions[0], sizeof(pp));
	vjob_region_pool_give(&pp_translation_unit_pool, pp_translation_unit_regions);
	pp_translation_unit_regions = NULL;
}
