/* vcc preprocessor: #embed (C23) and __has_embed: parameters, search, the file's bytes as integer literals */
#include <errno.h>
#include "ppimpl.h"
#include "port/mapfile.h"

/* a #embed / __has_embed parameter list (after the file name) */
typedef struct embprm {
	uint64_t limit, offset;
	bool has_limit, unknown, bad;
	const ptok *pre, *suf, *emp;	/* prefix(), suffix(), if_empty() token lists */
	uint32_t npre, nsuf, nemp;
} embprm;

/* the spelling of n (0..255): emb_digits[4 n] */
static char emb_digits[256 * 4 + 16];
static pthread_once_t emb_once = PTHREAD_ONCE_INIT;

static void emb_init(void)
{
	for (int i = 0; i < 256; i++)
		snprintf(emb_digits + 4 * i, 4, "%d", i);
}

/* tokens up to the ')' that matches the '(' just read; the list is copied to tmp */
static bool emb_list(pp *p, const ptok **out, uint32_t *n, bool exp)
{
	ptok buf[256], t;
	uint32_t k = 0;
	int depth = 0;

	for (;;) {
		if (exp)
			next_exp(p, &t);
		else
			raw(p, &t);
		if (t.kind == T_EOF)
			return false;
		if (t.kind == T_LPAREN)
			depth++;
		else if (t.kind == T_RPAREN && depth-- == 0)
			break;
		if (k < 256)
			buf[k++] = t;
	}
	{
		ptok *c = varena_alloc(&p->tmp, (k ? k : 1) * sizeof(ptok));

		memcpy(c, buf, k * sizeof(ptok));
		*out = c, *n = k;
	}
	return true;
}

/* an integer parameter value: a number (after expansion) up to ')' */
static bool emb_int(pp *p, uint64_t *v)
{
	const ptok *l;
	uint32_t n;
	char s[64];

	if (!emb_list(p, &l, &n, true))
		return false;
	if (n != 1 || l[0].kind != T_NUMBER || l[0].len >= sizeof s)
		return eval_ptoks(p, l, n, v);	/* a constant expression (clang) */
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
static void emb_params(pp *p, embprm *m, bool paren, uint32_t loc)
{
	ptok t;

	for (;;) {
		char nm[64];
		size_t nl = 0;

		next_exp(p, &t);
		if (t.kind == T_EOF || (paren && t.kind == T_RPAREN))
			return;
		if (t.kind != T_IDENT || t.len >= 40) {
			m->bad = true;
			return;
		}
		memcpy(nm, t.s, t.len), nl = t.len;
		next_exp(p, &t);
		if (t.kind == T_COLONCOLON || t.kind == T_COLON) {	/* vendor::name */
			if (t.kind == T_COLON)
				next_exp(p, &t);
			nm[nl++] = ':', nm[nl++] = ':';
			next_exp(p, &t);
			if (t.kind != T_IDENT || nl + t.len >= sizeof nm) {
				m->bad = true;
				return;
			}
			memcpy(nm + nl, t.s, t.len), nl += t.len;
			next_exp(p, &t);
		}
		nm[nl] = 0;
		/* __x__ is x */
		if (nl > 4 && nm[0] == '_' && nm[1] == '_' && nm[nl - 1] == '_' && nm[nl - 2] == '_')
			memmove(nm, nm + 2, nl - 4), nm[nl - 4] = 0;
		if (t.kind != T_LPAREN) {
			m->bad = true;
			return;
		}
		if (!strcmp(nm, "limit")) {
			m->has_limit = emb_int(p, &m->limit) || (m->bad = true, false);
		} else if (!strcmp(nm, "clang::offset")) {
			if (!emb_int(p, &m->offset))
				m->bad = true;
		} else if (!strcmp(nm, "prefix")) {
			if (!emb_list(p, &m->pre, &m->npre, false))
				m->bad = true;
		} else if (!strcmp(nm, "suffix")) {
			if (!emb_list(p, &m->suf, &m->nsuf, false))
				m->bad = true;
		} else if (!strcmp(nm, "if_empty")) {
			if (!emb_list(p, &m->emp, &m->nemp, false))
				m->bad = true;
		} else {
			const ptok *l;
			uint32_t n;

			if (!paren)
				error(p, loc, "unknown embed preprocessor parameter '%s'", nm);
			m->unknown = true;
			if (!emb_list(p, &l, &n, false)) {
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
static const vlex_file *find_embed(pp *p, const ctx *from, const char *name, size_t nlen, bool angled, const char **path)
{
	char buf[4096];
	const vlex_file *f;
	const char *fp;
	const char *sl;
	size_t dl;

	if (nlen + 3 >= sizeof(buf))
		return NULL;
	if (name[0] == '/') {
		memcpy(buf, name, nlen), buf[nlen] = 0;
		*path = pstrdup(p, buf, nlen);
		return try_path(p->o, buf, nlen);
	}
	if (!angled) {
		fp = from && from->f != &p->builtin ? from->spath : "./";
		sl = strrchr(fp, '/');
		dl = sl ? (size_t)(sl - fp) + 1 : 0;
		if (!sl && fp[0])
			fp = "./", dl = 2;
		if (dl + nlen + 1 < sizeof(buf)) {
			memcpy(buf, fp, dl), memcpy(buf + dl, name, nlen), buf[dl + nlen] = 0;
			if ((f = try_path(p->o, buf, dl + nlen))) {
				*path = pstrdup(p, buf, dl + nlen);
				return f;
			}
		}
		/* In particular, __FILE__ may already contain the relative directory
		 * of the source. Retrying it under that directory would double it. */
		memcpy(buf, name, nlen), buf[nlen] = 0;
		if ((f = try_path(p->o, buf, nlen))) {
			*path = pstrdup(p, buf, nlen);
			return f;
		}
	}
	for (int k = 0; k < p->o->nedirs; k++) {	/* --embed-dir, in order */
		const char *d = p->o->edirs[k];
		size_t l = strlen(d);

		if (l + nlen + 2 >= sizeof(buf))
			continue;
		memcpy(buf, d, l);
		if (l && buf[l - 1] != '/')
			buf[l++] = '/';
		memcpy(buf + l, name, nlen), l += nlen, buf[l] = 0;
		if ((f = try_path(p->o, buf, l))) {
			*path = pstrdup(p, buf, l);
			return f;
		}
	}
	return NULL;
}

/* path names the main file (clang lists it once in -M) */
static bool emb_is_main(const pp *p, const char *path)
{
	const char *m = p->o->main;

	if (!m)
		return false;
	while (path[0] == '.' && path[1] == '/')
		path += 2;
	while (m[0] == '.' && m[1] == '/')
		m += 2;
	return !strcmp(path, m);
}

/* the file's bytes as they are on disk: the lexer's copy (find_embed) has a BOM
   blanked and, in C23, the separators of numbers removed */
static bool emb_map(const pp *p, const char *path, vmapped *m)
{
	char abs[8192];
	struct stat st;
	size_t cl, l = strlen(path);

	if (p->o->cwd && path[0] != '/') {
		cl = strlen(p->o->cwd);
		if (cl + l + 2 > sizeof(abs))
			return false;
		memcpy(abs, p->o->cwd, cl);
		abs[cl] = '/';
		memcpy(abs + cl + 1, path, l + 1);
		path = abs;
	}
	return vmap_file(path, m, &st);
}

/* the bytes #embed takes from a file of sz bytes: [*b, *b + *n) */
static void emb_range(uint64_t sz, const embprm *m, uint64_t *b, uint64_t *n)
{

	*b = m->offset < sz ? m->offset : sz;
	*n = sz - *b;
	if (m->has_limit && m->limit < *n)
		*n = m->limit;
}

/* #embed: the file's bytes as a comma-separated list of integer literals */
VPRIV void embed(pp *p, ctx *c, uint32_t b, uint32_t e, uint32_t loc, uint32_t nloc)
{
	char buf[4096];
	size_t n;
	bool angled, ok;
	const char *path;
	const vlex_file *f;
	int d = p->depth;
	ptok t;
	uint32_t floc;
	embprm m = { 0 };
	uint64_t ob, on;
	vmapped raw_;

	pthread_once(&emb_once, emb_init);
	if (!p->o->c23) {
		pp_dinfo di = { "warning", nloc, NULL, 0, NULL, 0, NULL, 0 };

		pp_warnx(p, "c23-extensions", true, NULL, &di, "#embed is a C23 extension");
	}
	push_line(p, c, b, e);
	raw(p, &t);
	floc = t.loc;
	p->in_dir++;
	ok = (t.kind == T_STRING || t.kind == T_LESS || t.kind == T_IDENT) && header_name(p, buf, sizeof(buf), &n, &angled, &t);
	if (ok)
		emb_params(p, &m, false, loc);
	p->in_dir--;
	pop_to(p, d);
	if (!ok) {
		error(p, loc, "expected \"FILENAME\" or <FILENAME>");
		return;
	}
	if (m.bad || m.unknown)
		return;
	f = find_embed(p, c, buf, n, angled, &path);
	if (!f) {
		pp_dinfo di = { "fatal error", floc, NULL, 0, NULL, 0, NULL, 0 };	/* (clang: no range for #embed) */

		pp_diagx(p, &di, "'%.*s' file not found", (int)n, buf);
		if (p->fatal_jb && !p->o->fatal_continue)
			longjmp(*p->fatal_jb, 1);
		return;
	}
	if (p->o->deps && !emb_is_main(p, path))
		dep_only(p, path, false);
	if (!emb_map(p, path, &raw_)) {
		pp_dinfo di = { "fatal error", floc, NULL, 0, NULL, 0, NULL, 0 };

		pp_diagx(p, &di, "cannot open file '%s': %s", path, strerror(errno));
		if (p->fatal_jb && !p->o->fatal_continue)
			longjmp(*p->fatal_jb, 1);
		return;
	}
	emb_range(raw_.n, &m, &ob, &on);
	if (p->rec.n)	/* the header cache does not record #embed's bytes: the open regions are not cached */
		p->rec.nbad = p->rec.n;
	{
		uint64_t nt = on ? m.npre + 2 * on - 1 + m.nsuf : m.nemp;
		ptok *o, *w;

		if (!nt) {
			vunmap_file(&raw_);
			return;
		}
		o = w = varena_alloc(&p->tmp, nt * sizeof(ptok));
		if (on) {
			if (m.npre)	/* no prefix: m.pre is NULL, and memcpy's arguments are nonnull */
				memcpy(w, m.pre, m.npre * sizeof(ptok)), w += m.npre;
			for (uint64_t i = 0; i < on; i++) {
				unsigned char v = raw_.p[ob + i];

				if (i) {
					*w++ = (ptok){ ",", 1, 0, loc, T_COMMA, 0, 0 };
				}
				*w++ = (ptok){ emb_digits + 4 * v, (uint32_t)strlen(emb_digits + 4 * v), 0, loc, T_NUMBER,
					       (uint8_t)(i ? TF_SPACE : 0), 0 };
			}
			w[-1].flags |= PF_EMBEND;
			if (m.nsuf)
				memcpy(w, m.suf, m.nsuf * sizeof(ptok)), w += m.nsuf;
		} else {
			memcpy(w, m.emp, m.nemp * sizeof(ptok)), w += m.nemp;
		}
		w[-1].flags |= PF_EMBEND;
		vunmap_file(&raw_);
		p->emb_tail = !on ? 1 : m.nsuf ? 2 : 0;	/* markers left: 2 bytes then suffix, 1 if_empty/suffix */
		push_ptok(p, o, (uint32_t)(w - o), 0, 0, 0);
	}
}

/* __has_embed( name params ): 0 not found (or an unknown parameter), 1 found, 2 empty */
VPRIV int has_embed(pp *p, const ctx *fc, uint32_t loc)
{
	char buf[4096];
	size_t n;
	bool angled;
	const char *path;
	const vlex_file *f;
	ptok r;
	embprm m = { 0 };
	uint64_t ob, on;

	raw(p, &r);
	if (r.kind != T_LPAREN) {
		error(p, loc, "missing '(' after __has_embed");
		return 0;
	}
	raw(p, &r);
	if (!header_name(p, buf, sizeof(buf), &n, &angled, &r)) {
		error(p, loc, "invalid __has_embed argument");
		return 0;
	}
	emb_params(p, &m, true, loc);
	if (m.bad || m.unknown)
		return 0;
	f = find_embed(p, fc, buf, n, angled, &path);
	if (!f)
		return 0;
	if (p->o->deps && !emb_is_main(p, path))
		dep_only(p, path, false);
	vmapped raw_;

	if (!emb_map(p, path, &raw_))
		return 0;
	emb_range(raw_.n, &m, &ob, &on);
	vunmap_file(&raw_);
	return on ? 1 : 2;
}
