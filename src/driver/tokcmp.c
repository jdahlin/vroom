/*
 * tokcmp.c: --cmp, comparing our -E output with clang's token by token, and --vcc-tokhash, a hash of
 * the token stream the parser sees.  Test aids for bench/vcc-diff.py and the parity runs, not part of
 * the product.
 *
 * The comparison lexes both sides with vcc's own lexer and compares kind and spelling, so spacing and
 * line markers do not count; the first difference is shown and the status is 1 (2 when the reference
 * cannot be opened).  The reference's token-stream hash is kept in REF.i.th, keyed by the reference's
 * size and mtime: when ours hashes the same, the reference is not read again (VPP_CMP_NOCACHE=1
 * compares in full).  The cache file is written through a temporary name and renamed, so parallel
 * runs are safe.  Lexing here turns identifier interning off (lex_identifiers_not_interned) because
 * the token copies are freed: do not keep pointers into them.  The token hash covers kind, spacing
 * flags, location, spelling and the file names (VPP_TOKCOUNT=1 only counts).
 */
#include "driver-internal.h"

#ifdef __APPLE__
#define ST_MTIME_NS(st) ((unsigned long long)(st)->st_mtimespec.tv_sec * 1000000000ull + (unsigned long long)(st)->st_mtimespec.tv_nsec)
#else
#define ST_MTIME_NS(st) ((unsigned long long)(st)->st_mtim.tv_sec * 1000000000ull + (unsigned long long)(st)->st_mtim.tv_nsec)
#endif

/* the tokens of a file (whole, zero padded), lexed */
static lex_file driver_tokens_lex_string(const char *s, size_t n)
{
	lex_file f = { 0 };
	char *src = mmap(NULL, n + 4096, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
	lex_token *tok = mmap(NULL, (n + 2) * sizeof(lex_token), PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
	size_t sn = lex_scratch_size(n) + 4096;
	void *scr = mmap(NULL, sn, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);

	memcpy(src, s, n);
	f.src = src;
	f.size = (uint32_t)n;
	f.ntok = lex_tokenize(src, n, tok, scr);
	f.tok = tok;
	munmap(scr, sn);
	return f;
}

static void driver_tokens_lex_free(lex_file *f)
{
	munmap((void *)f->src, f->size + 4096);
	munmap((void *)f->tok, (f->size + 2) * sizeof(lex_token));
}

static size_t driver_tokens_spelling_get(const lex_file *f, const lex_token *t, char *b)
{
	if (t->flags & LEX_TOKEN_FLAG_SPLICED)
		return lex_token_spell(f->src, t, b);
	memcpy(b, f->src + t->off, t->len);
	return t->len;
}

static unsigned driver_tokens_line_number(const lex_file *f, uint32_t off)
{
	unsigned l = 1;

	for (uint32_t i = 0; i < off; i++)
		l += f->src[i] == '\n';
	return l;
}

static void driver_tokens_context_print(const char *what, const lex_file *f, uint32_t i)
{
	uint32_t a = i > 12 ? i - 12 : 0, e = i + 12 < f->ntok ? i + 12 : f->ntok;
	char b[1 << 16];

	fprintf(stderr, "%s (line %u):", what, i < f->ntok ? driver_tokens_line_number(f, f->tok[i].off) : 0);
	for (uint32_t k = a; k < e; k++) {
		size_t n = driver_tokens_spelling_get(f, &f->tok[k], b);

		fprintf(stderr, k == i ? " >>>%.*s<<<" : " %.*s", (int)(n > 200 ? 200 : n), b);
	}
	fputc('\n', stderr);
}

static int driver_tokens_compare_buffers(const lex_file *pa, const lex_file *pb, char *ba, char *bb);

/* XXH3 of a token stream: per token the kind, the clean spelling's length and
 * the spelling (what driver_tokens_compare_buffers compares) */
static uint64_t driver_tokens_hash(const lex_file *f)
{
	size_t cap = (size_t)f->size + (size_t)f->ntok * 5 + 64, n = 0;
	unsigned char *b = mmap(NULL, cap, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
	uint64_t h;

	for (uint32_t i = 0; i < f->ntok; i++) {
		const lex_token *t = &f->tok[i];
		size_t l;

		if (n + 5 + t->len + 64 > cap)	/* a clean spelling is never longer than the raw one */
			break;
		b[n] = (unsigned char)t->kind;
		l = driver_tokens_spelling_get(f, t, (char *)b + n + 5);
		memcpy(b + n + 1, &(uint32_t){ (uint32_t)l }, 4);
		n += 5 + l;
	}
	h = vdigest_xxh3(b, n) ^ f->ntok;
	munmap(b, cap);
	return h;
}

/* REF.i.th: "size mtime_ns ntok hash" of REF.i's token stream, so a run that
 * matches lexes only our side; written on the first compare of a REF.i */
static int driver_tokens_hash_cache_read(const char *refpath, const struct stat *st, uint64_t *h)
{
	char path[4096], b[128];
	unsigned long long sz, mt, nt, hv;
	ssize_t r;

	snprintf(path, sizeof(path), "%s.th", refpath);
	VAUTOCLOSE int fd = open(path, O_RDONLY | O_CLOEXEC);
	if (fd < 0)
		return 0;
	r = read(fd, b, sizeof(b) - 1);
	VCLOSE(fd);
	if (r <= 0)
		return 0;
	b[r] = 0;
	if (sscanf(b, "%llu %llu %llu %llx", &sz, &mt, &nt, &hv) != 4 || sz != (unsigned long long)st->st_size ||
	    mt != ST_MTIME_NS(st))
		return 0;
	*h = hv;
	return 1;
}

static void driver_tokens_hash_cache_write(const char *refpath, const struct stat *st, uint32_t ntok, uint64_t h)
{
	char path[4096], tmp[4200], b[128];
	int fd, n;

	snprintf(path, sizeof(path), "%s.th", refpath);
	snprintf(tmp, sizeof(tmp), "%s.%d.%p", path, (int)getpid(), (void *)&fd);
	n = snprintf(b, sizeof(b), "%llu %llu %u %016llx\n", (unsigned long long)st->st_size,
		     ST_MTIME_NS(st), ntok, (unsigned long long)h);
	fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0666);
	if (fd < 0)
		return;
	if (write(fd, b, (size_t)n) == n && !close(fd))
		rename(tmp, path);
	else
		unlink(tmp);
}

int driver_tokens_compare(const char *ours, size_t on, const char *refpath)
{
	vintern_init();
	VAUTOCLOSE int fd = open(refpath, O_RDONLY);
	struct stat st;
	lex_file A, B;
	static _Thread_local char *ba, *bb;
	uint64_t rh;
	int r, cached;

	if (fd < 0 || fstat(fd, &st)) {
		perror(refpath);
		return 2;
	}
	if (!ba) {
		ba = malloc(1 << 20);
		bb = malloc(1 << 20);
	}
	lex_identifiers_not_interned = 1;	/* the copies are freed: nothing may point into them */
	A = driver_tokens_lex_string(ours, on);
	/* VPP_CMP_NOCACHE=1: always lex and compare the reference */
	cached = !getenv("VPP_CMP_NOCACHE") && driver_tokens_hash_cache_read(refpath, &st, &rh);
	if (cached && driver_tokens_hash(&A) == rh) {
		lex_identifiers_not_interned = 0;
		printf("same: %u tokens\n", A.ntok);
		driver_tokens_lex_free(&A);
		return 0;
	}
	{
		char *m = mmap(NULL, (size_t)st.st_size + 1, PROT_READ, MAP_PRIVATE, fd, 0);

		VCLOSE(fd);
		B = driver_tokens_lex_string(m, (size_t)st.st_size);
		lex_identifiers_not_interned = 0;
		munmap(m, (size_t)st.st_size + 1);
	}
	if (!cached)
		driver_tokens_hash_cache_write(refpath, &st, B.ntok, driver_tokens_hash(&B));
	r = driver_tokens_compare_buffers(&A, &B, ba, bb);
	driver_tokens_lex_free(&A);
	driver_tokens_lex_free(&B);
	return r;
}

static int driver_tokens_compare_buffers(const lex_file *pa, const lex_file *pb, char *ba, char *bb)
{
	const lex_file A = *pa, B = *pb;

	for (uint32_t i = 0;; i++) {
		const lex_token *x = &A.tok[i], *y = &B.tok[i];
		size_t nx, ny;

		if (i == A.ntok || i == B.ntok) {
			if (A.ntok == B.ntok) {
				printf("same: %u tokens\n", A.ntok);
				return 0;
			}
			printf("length differs: ours %u, clang %u tokens\n", A.ntok, B.ntok);
			driver_tokens_context_print("ours ", &A, i);
			driver_tokens_context_print("clang", &B, i);
			return 1;
		}
		nx = driver_tokens_spelling_get(&A, x, ba);
		ny = driver_tokens_spelling_get(&B, y, bb);
		if (x->kind != y->kind || nx != ny || memcmp(ba, bb, nx)) {
			printf("token %u differs\n", i);
			driver_tokens_context_print("ours ", &A, i);
			driver_tokens_context_print("clang", &B, i);
			return 1;
		}
	}
}

/* --vcc-tokhash: a hash of what pp_token_next hands the parser */
VPRIV void driver_tokens_hash_run(struct driver_job *job, pp *p, const driver_environment *env)
{
	{
		/* a hash of what pp_token_next hands the parser: kind, SOL/SPACE, spelling, id, file:line:col, sys */
		uint64_t h = 1469598103934665603ull;
		unsigned long n = 0;
		char tmp[1 << 16], line[64];
		const char *fn[4096];
		size_t nf;
		ptok t;
		const int count_only = getenv("VPP_TOKCOUNT") != NULL;

#define DRIVER_TOKENS_HASH_BYTES(ptr, len) do { const unsigned char *q_ = (const unsigned char *)(ptr); \
		for (size_t k_ = 0; k_ < (size_t)(len); k_++) h = (h ^ q_[k_]) * 1099511628211ull; } while (0)
		for (;;) {
			const char *sp;
			size_t sl;
			unsigned char kf[2];

			pp_token_next(p, &t);
			if (t.kind == LEX_TOKEN_EOF)
				break;
			n++;
			if (count_only)
				continue;
			kf[0] = t.kind, kf[1] = t.flags & (LEX_TOKEN_FLAG_LINE_START | LEX_TOKEN_FLAG_SPACE_BEFORE);
			DRIVER_TOKENS_HASH_BYTES(kf, 2);
			DRIVER_TOKENS_HASH_BYTES(&t.loc, 4);	/* the same in every run: file bases are assigned in order */
			sp = t.len < sizeof(tmp) ? pp_token_spell(&t, tmp, &sl) : (sl = 0, "");
			DRIVER_TOKENS_HASH_BYTES(sp, sl);
			if (t.kind == LEX_TOKEN_IDENTIFIER)
				DRIVER_TOKENS_HASH_BYTES(vintern_ents[t.id].s, vintern_ents[t.id].len);
		}
		nf = pp_file_names(p, fn, 4096, 0);
		for (size_t i = 0; i < nf; i++)
			DRIVER_TOKENS_HASH_BYTES(fn[i], strlen(fn[i]) + 1);
#undef DRIVER_TOKENS_HASH_BYTES
		snprintf(line, sizeof(line), "%016llx %lu\n", (unsigned long long)h, n);
		job->status = pp_error_count(p) ? 1 : driver_output_write(env->cwd, job->output_file, env->outfd, line, strlen(line), job->errfd);
		return;
	}
}
