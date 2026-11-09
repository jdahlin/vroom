/*
 * ar inside vccd: the archiver Kbuild runs as $(AR), byte-identical to
 * llvm-ar for what Kbuild does with it, so no llvm-ar is needed:
 *
 *   ar cDPrST A FILE...        every built-in.a (thin, no symbol table)
 *   ar cDPrsT A FILE...        lib.a (thin, with a symbol table)
 *   ar cDPrS --thin A FILE...  vmlinux.a
 *   ar mPi POS A MEMBER...     move MEMBERs before POS (head objects first)
 *   ar t A                     list the members
 *
 * Operations r, m, t; modifiers c D P s S T u(ignored) i b a, and --thin.
 * Only thin archives: a thin archive's members are paths, relative to the
 * archive's directory, with no data:
 *
 *   !<thin>\n
 *   "/" member:   symbol table unless S (and only if some member is an
 *                 ELF object): big-endian count, the offset of
 *                 each symbol's member header, NUL-terminated names, padded
 *                 to even length with NUL (global defined ELF symbols)
 *   "//" member:  "PATH/\n" per member, padded to even length with '\n'
 *   per member:   header "/OFFSET-IN-//", date/uid/gid 0 (D), mode, size
 *
 * The semantics follow llvm-ar's: new members are added in command-line
 * order (a thin archive given as a file is flattened into its members); r
 * replaces an old member of the same path in place; m moves the named
 * members, in archive order, before (i, b) or after (a) POS, where POS is
 * compared with the stored names as given.  Other paths are compared and
 * made relative lexically (".", ".." and "//" resolved against
 * the request's cwd).  The archive is written to a temporary file and
 * renamed; on an error nothing is written.
 *
 * Why: llvm-ar is a ~167 M-instruction process start on macOS (dyld binds
 * libLLVM.dylib), and Kbuild runs it once per directory (~420 times).
 */
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include "ar.h"
#include "../lib/buf.h"
#include "../lib/mapfile.h"

/* ---------------- growable buffer ---------------- */

/* ---------------- request state ---------------- */

typedef struct member {
	char *name;		/* as stored: relative to the archive's directory */
	char *abs;		/* absolute, normalized */
	uint32_t mode;
	uint64_t size;
} member;

typedef struct mlist {
	member *m;
	size_t n, cap;
} mlist;

typedef struct arq {
	const char *cwd;
	int outfd, errfd;
	char *dir;		/* the archive's directory, absolute, normalized */
	bool failed;
} arq;

static void err(arq *q, const char *fmt, const char *s, const char *t)
{
	char line[4200];
	int n = snprintf(line, sizeof(line), fmt, s, t);

	q->failed = true;
	if (q->errfd >= 0 && n > 0)
		(void)!write(q->errfd, line, n < (int)sizeof(line) ? n : (int)sizeof(line) - 1);
}

static bool ml_push(mlist *l, member m)
{
	if (l->n == l->cap) {
		size_t cap = l->cap ? l->cap * 2 : 256;
		member *p = realloc(l->m, cap * sizeof(*p));

		if (!p)
			return false;
		l->m = p;
		l->cap = cap;
	}
	l->m[l->n++] = m;
	return true;
}

static void ml_free(mlist *l)
{
	for (size_t i = 0; i < l->n; i++) {
		free(l->m[i].name);
		free(l->m[i].abs);
	}
	free(l->m);
	l->m = NULL;
	l->n = l->cap = 0;
}

/* ---------------- paths ---------------- */

/* base + path, absolute and lexically normalized ("." and ".." resolved) */
static char *norm(const char *base, const char *path)
{
	size_t bl = path[0] == '/' ? 0 : strlen(base), pl = strlen(path), n = 0;
	char *in = malloc(bl + pl + 2), *out = malloc(bl + pl + 2), *s;

	if (!in || !out) {
		free(in);
		free(out);
		return NULL;
	}
	memcpy(in, base, bl);
	in[bl] = '/';
	memcpy(in + bl + 1, path, pl + 1);
	for (s = in; *s;) {
		char *e;
		size_t l;

		while (*s == '/')
			s++;
		for (e = s; *e && *e != '/'; e++)
			;
		l = e - s;
		if (l == 2 && s[0] == '.' && s[1] == '.') {
			while (n > 0 && out[n - 1] != '/')
				n--;
			if (n > 0)
				n--;
		} else if (l && !(l == 1 && s[0] == '.')) {
			out[n++] = '/';
			memcpy(out + n, s, l);
			n += l;
		}
		s = e;
	}
	if (!n)
		out[n++] = '/';
	out[n] = 0;
	free(in);
	return out;
}

/* the directory part of an absolute normalized path */
static char *dirof(const char *abs)
{
	const char *s = strrchr(abs, '/');

	return strndup(abs, s == abs ? 1 : (size_t)(s - abs));
}

/* abs relative to dir (both absolute, normalized) */
static char *relto(const char *dir, const char *abs)
{
	size_t k = 0, last = 0, up = 0, i;
	const char *rest;
	char *r, *p;

	if (!strcmp(dir, "/"))
		return strdup(abs + 1);
	/* common leading components */
	for (;;) {
		if (dir[k] == 0 && (abs[k] == '/' || abs[k] == 0)) {
			last = k;
			break;
		}
		if (dir[k] != abs[k])
			break;
		if (dir[k] == '/')
			last = k;
		k++;
	}
	for (i = last; dir[i]; i++)
		if (dir[i] == '/')
			up++;
	rest = abs + last + (abs[last] == '/');
	if (!(r = malloc(up * 3 + strlen(rest) + 1)))
		return NULL;
	for (p = r; up--; p += 3)
		memcpy(p, "../", 3);
	strcpy(p, rest);
	return r;
}

/* ---------------- reading ---------------- */

static uint64_t num(const unsigned char *s, size_t n, int base)
{
	char b[24];

	if (n >= sizeof(b))
		n = sizeof(b) - 1;
	memcpy(b, s, n);
	b[n] = 0;
	return strtoull(b, NULL, base);
}

/*
 * Append the members of the thin archive at abs (vmapped as d) to l: names
 * are made relative to q->dir, mode and size come from the headers.
 * shown is the path the user gave, for messages.
 */
static bool read_thin(arq *q, const char *abs, const char *shown, const vmapped *d, mlist *l)
{
	char *adir = dirof(abs);
	const unsigned char *names = NULL;
	size_t off = 8, nlen = 0;
	bool ok = adir != NULL;

	if (d->n < 8 || memcmp(d->p, "!<thin>\n", 8)) {
		err(q, "ar: error: %s: %s\n", shown, "only thin archives are supported");
		free(adir);
		return false;
	}
	while (ok && off + 60 <= d->n) {
		const unsigned char *h = d->p + off;
		uint64_t sz = num(h + 48, 10, 10);

		off += 60;
		if (h[0] == '/' && h[1] == ' ') {		/* symbol table */
			off += sz + (sz & 1);
		} else if (h[0] == '/' && h[1] == '/' && h[2] == ' ') {
			names = d->p + off;
			nlen = sz;
			off += sz + (sz & 1);
		} else if (h[0] == '/' && h[1] >= '0' && h[1] <= '9' && names) {
			size_t o = num(h + 1, 15, 10), e;
			member m = { 0 };
			char *nm;

			for (e = o; e < nlen && names[e] != '\n'; e++)
				;
			if (o >= nlen || e < o + 2 || names[e - 1] != '/') {
				err(q, "ar: error: %s: %s\n", shown, "malformed thin archive");
				ok = false;
				break;
			}
			nm = strndup((const char *)names + o, e - o - 1);
			m.abs = nm ? norm(adir, nm) : NULL;
			m.name = m.abs ? relto(q->dir, m.abs) : NULL;
			m.mode = num(h + 40, 8, 8);
			m.size = sz;
			free(nm);
			if (!m.name || !ml_push(l, m)) {
				free(m.name);
				free(m.abs);
				ok = false;
			}
		} else {
			err(q, "ar: error: %s: %s\n", shown, "unsupported archive member");
			ok = false;
		}
	}
	free(adir);
	return ok;
}

/* append the file given as path (a thin archive is flattened) */
static bool add_file(arq *q, const char *path, mlist *l)
{
	char *abs = norm(q->cwd, path);
	struct stat st;
	vmapped d;
	bool ok;

	if (!abs)
		return false;
	if (!vmap_file(abs, &d, &st)) {
		err(q, "ar: error: %s: %s\n", path, strerror(errno));
		free(abs);
		return false;
	}
	if (d.n >= 8 && !memcmp(d.p, "!<thin>\n", 8)) {
		ok = read_thin(q, abs, path, &d, l);
		free(abs);
	} else if (d.n >= 8 && !memcmp(d.p, "!<arch>\n", 8)) {
		err(q, "ar: error: %s: %s\n", path, "a regular archive cannot go into a thin one");
		free(abs);
		ok = false;
	} else {
		member m = { relto(q->dir, abs), abs, st.st_mode & 07777, (uint64_t)st.st_size };

		ok = m.name && ml_push(l, m);
		if (!ok) {
			free(m.name);
			free(abs);
		}
	}
	vunmap_file(&d);
	return ok;
}

/* ---------------- symbol table ---------------- */

static uint16_t r16(const unsigned char *p) { return p[0] | p[1] << 8; }
static uint32_t r32(const unsigned char *p) { return r16(p) | (uint32_t)r16(p + 2) << 16; }
static uint64_t r64(const unsigned char *p) { return r32(p) | (uint64_t)r32(p + 4) << 32; }

/*
 * Global (or weak) defined symbols of an ELF64 little-endian object, in
 * symbol-table order, as llvm's archive writer takes them: no undefined,
 * local, section or file symbols.  Other files have no symbols, and an
 * archive without any object file gets no symbol table (*is_obj).
 */
static bool elf_syms(const vmapped *d, vsbuf *names, uint32_t *count, bool *is_obj)
{
	const unsigned char *p = d->p;
	uint64_t shoff, shnum, i;
	unsigned shentsize;

	if (d->n < 64 || memcmp(p, "\177ELF", 4) || p[4] != 2 || p[5] != 1)
		return true;
	*is_obj = true;
	shoff = r64(p + 0x28);
	shentsize = r16(p + 0x3a);
	shnum = r16(p + 0x3c);
	if (shentsize < 64 || shoff > d->n)
		return true;
	if (!shnum && shoff + 64 <= d->n)
		shnum = r64(p + shoff + 0x20);
	if (shnum > (d->n - shoff) / shentsize)
		return true;
	for (i = 0; i < shnum; i++) {
		const unsigned char *sh = p + shoff + i * shentsize, *str;
		uint64_t off, size, ent, n, j, soff, ssize;
		unsigned link;

		if (r32(sh + 4) != 2)	/* SHT_SYMTAB */
			continue;
		off = r64(sh + 0x18);
		size = r64(sh + 0x20);
		link = r32(sh + 0x28);
		ent = r64(sh + 0x38);
		if (ent < 24 || off > d->n || size > d->n - off || link >= shnum)
			return false;
		sh = p + shoff + (uint64_t)link * shentsize;
		soff = r64(sh + 0x18);
		ssize = r64(sh + 0x20);
		if (soff > d->n || ssize > d->n - soff)
			return false;
		str = p + soff;
		n = size / ent;
		for (j = 1; j < n; j++) {
			const unsigned char *s = p + off + j * ent;
			unsigned bind = s[4] >> 4, type = s[4] & 15;
			uint32_t nm = r32(s);
			size_t len;

			if (bind == 0 || type == 3 || type == 4 || r16(s + 6) == 0)
				continue;
			if (nm >= ssize)
				return false;
			len = strnlen((const char *)str + nm, ssize - nm);
			if (nm + len >= ssize)
				return false;
			vsbuf_put(names, str + nm, len + 1);
			(*count)++;
		}
		return true;	/* one symbol table per object */
	}
	return true;
}

/* ---------------- writing ---------------- */

static void header(vsbuf *b, const char *name, const char *date, const char *uid, const char *gid,
		   const char *mode, uint64_t size)
{
	char h[61];

	snprintf(h, sizeof(h), "%-16s%-12s%-6s%-6s%-8s%-10llu`\n", name, date, uid, gid, mode,
		 (unsigned long long)size);
	vsbuf_put(b, h, 60);
}

static void be32(vsbuf *b, uint32_t v)
{
	unsigned char c[4] = { v >> 24, v >> 16, v >> 8, v };

	vsbuf_put(b, c, 4);
}

static bool write_archive(arq *q, const char *archive_abs, const char *shown, const mlist *l, bool symtab)
{
	vsbuf out = { 0 }, names = { 0 }, syms = { 0 };
	uint32_t *nsym = NULL, total = 0;
	bool any_obj = false;
	uint64_t pos, symsize = 0;
	size_t i;
	char *tmp = NULL;
	int fd;
	bool ok = false;

	for (i = 0; i < l->n; i++) {
		vsbuf_str(&names, l->m[i].name);
		vsbuf_put(&names, "/\n", 2);
	}
	if (names.n & 1)
		vsbuf_put(&names, "\n", 1);
	if (symtab && l->n) {
		if (!(nsym = calloc(l->n, sizeof(*nsym))))
			goto out;
		for (i = 0; i < l->n; i++) {
			struct stat st;
			vmapped d;

			if (!vmap_file(l->m[i].abs, &d, &st)) {
				err(q, "ar: error: %s: %s\n", l->m[i].name, strerror(errno));
				goto out;
			}
			if (!elf_syms(&d, &syms, &nsym[i], &any_obj)) {
				vunmap_file(&d);
				err(q, "ar: error: %s: %s\n", l->m[i].name, "malformed ELF symbol table");
				goto out;
			}
			vunmap_file(&d);
			total += nsym[i];
		}
		symtab = any_obj;
		symsize = 4 + 4 * (uint64_t)total + syms.n;
		symsize += symsize & 1;
		if (!total)
			symsize = 8;	/* llvm-ar: count 0 and four zero bytes */
	}

	vsbuf_put(&out, "!<thin>\n", 8);
	if (l->n) {
		pos = 8 + (symtab ? 60 + symsize : 0) + 60 + names.n;
		if (symtab) {
			header(&out, "/", "0", "0", "0", "0", symsize);
			be32(&out, total);
			for (i = 0; i < l->n; i++)
				for (uint32_t k = 0; k < nsym[i]; k++)
					be32(&out, (uint32_t)(pos + 60 * i));
			vsbuf_put(&out, syms.p, syms.n);
			vsbuf_put(&out, "\0\0\0\0", symsize - (4 + 4 * (uint64_t)total + syms.n));
		}
		header(&out, "//", "", "", "", "", names.n);
		vsbuf_put(&out, names.p, names.n);
		for (i = 0, pos = 0; i < l->n; i++) {
			char nm[24], mode[16];

			snprintf(nm, sizeof(nm), "/%llu", (unsigned long long)pos);
			snprintf(mode, sizeof(mode), "%o", l->m[i].mode);
			header(&out, nm, "0", "0", "0", mode, l->m[i].size);
			pos += strlen(l->m[i].name) + 2;
		}
	}
	if (out.oom || names.oom || syms.oom) {
		err(q, "ar: error: %s%s\n", "out of memory", "");
		goto out;
	}

	if (!(tmp = malloc(strlen(archive_abs) + 20)))
		goto out;
	sprintf(tmp, "%s.tmp-ar-XXXXXX", archive_abs);
	if ((fd = mkstemp(tmp)) < 0) {
		err(q, "ar: error: %s: %s\n", shown, strerror(errno));
		goto out;
	}
	fchmod(fd, 0644);
	{
		const char *p = out.p;
		size_t n = out.n;

		ok = true;
		while (n && ok) {
			ssize_t w = write(fd, p, n);

			if (w < 0 && errno == EINTR)
				continue;
			ok = w > 0;
			p += w > 0 ? w : 0;
			n -= w > 0 ? w : 0;
		}
	}
	if (close(fd))
		ok = false;
	if (!ok || rename(tmp, archive_abs)) {
		err(q, "ar: error: %s: %s\n", shown, strerror(errno));
		unlink(tmp);
		ok = false;
	}
out:
	free(tmp);
	free(nsym);
	free(out.p);
	free(names.p);
	free(syms.p);
	return ok;
}

/* ---------------- operations ---------------- */

int ar_run(const char *cwd, int argc, char **argv, int outfd, int errfd)
{
	arq q = { cwd, outfd, errfd, NULL, false };
	const char *ops = NULL, *relpos = NULL, *archive = NULL;
	bool thin = false, symtab = true, create = false, before = false, after = false;
	char op = 0, *archive_abs = NULL;
	char **files = NULL;
	int nfiles = 0, i, rc = 1;
	mlist old = { 0 }, res = { 0 }, moved = { 0 };
	struct stat st;
	vmapped d = { 0 };
	bool have_old = false;

	if (!(files = calloc(argc, sizeof(*files))))
		return 1;
	for (i = 1; i < argc; i++) {
		const char *a = argv[i];

		if (!strcmp(a, "--thin")) {
			thin = true;
		} else if (ops) {
			if ((before || after) && !relpos)
				relpos = a;
			else if (!archive)
				archive = a;
			else
				files[nfiles++] = argv[i];
		} else {
			ops = a[0] == '-' ? a + 1 : a;
			for (const char *c = ops; *c; c++) {
				switch (*c) {
				case 'r': case 'm': case 't':
					if (op && op != *c) {
						err(&q, "ar: error: %s%s\n", "only one operation may be specified", "");
						goto out;
					}
					op = *c;
					break;
				case 'c': create = true; break;
				case 'D': case 'P': case 'u': break;
				case 's': symtab = true; break;
				case 'S': symtab = false; break;
				case 'T': thin = true; break;
				case 'i': case 'b': before = true; break;
				case 'a': after = true; break;
				default:
					err(&q, "ar: error: unsupported modifier '%c' in '%s'\n", (char[]){ *c, 0 }, ops);
					goto out;
				}
			}
		}
	}
	if (!op || !archive) {
		err(&q, "ar: error: %s%s\n", "expected an operation and an archive", "");
		goto out;
	}
	if (!(archive_abs = norm(cwd, archive)) || !(q.dir = dirof(archive_abs)))
		goto out;

	if (vmap_file(archive_abs, &d, &st)) {
		have_old = true;
		if (!read_thin(&q, archive_abs, archive, &d, &old))
			goto out;
		thin = true;
	} else if (errno != ENOENT || op != 'r') {
		err(&q, "ar: error: %s: %s\n", archive, strerror(errno));
		goto out;
	}

	if (op == 't') {
		/* the archive argument's directory, then the stored name */
		const char *s = strrchr(archive, '/');
		size_t dl = s ? (size_t)(s - archive) : 0;
		vsbuf b = { 0 };

		for (size_t k = 0; k < old.n; k++) {
			if (s) {
				vsbuf_put(&b, archive, dl ? dl : 1);
				if (dl)
					vsbuf_put(&b, "/", 1);
			}
			vsbuf_str(&b, old.m[k].name);
			vsbuf_put(&b, "\n", 1);
		}
		rc = 0;
		for (size_t off = 0; off < b.n;) {
			ssize_t w = write(outfd, b.p + off, b.n - off);

			if (w < 0 && errno == EINTR)
				continue;
			if (w <= 0) {
				rc = 1;
				break;
			}
			off += w;
		}
		if (b.oom)
			rc = 1;
		free(b.p);
		goto out;
	}
	if (!thin) {
		err(&q, "ar: error: %s%s\n", "only thin archives (T) are supported", "");
		goto out;
	}
	(void)create;

	{
		/* llvm-ar's computeNewArchiveMembers for r and m */
		bool *used = calloc(nfiles + 1, sizeof(bool));
		long insert = -1;

		if (!used)
			goto out;
		for (size_t k = 0; k < old.n; k++) {
			member *m = &old.m[k];
			int match = -1;

			if (relpos && !strcmp(m->name, relpos))
				insert = (long)res.n + (after ? 1 : 0);
			for (i = 0; i < nfiles && match < 0; i++) {
				char *fa;

				if (used[i])
					continue;
				fa = norm(cwd, files[i]);
				if (fa && !strcmp(fa, m->abs))
					match = i;
				free(fa);
			}
			if (match < 0) {
				ml_push(&res, *m);
			} else if (op == 'm') {
				ml_push(&moved, *m);
				used[match] = true;
			} else {
				/* r: the new file takes the old member's place */
				mlist nw = { 0 };

				used[match] = true;
				if (!add_file(&q, files[match], &nw)) {
					ml_free(&nw);
					free(used);
					goto out_lists;
				}
				for (size_t j = 0; j < nw.n; j++)
					ml_push(&res, nw.m[j]);
				free(nw.m);
				free(m->name);
				free(m->abs);
			}
			m->name = m->abs = NULL;
		}
		if (op == 'm') {
			if (relpos && insert < 0) {
				err(&q, "ar: error: %s%s\n", "insertion point not found", "");
				free(used);
				goto out_lists;
			}
			for (i = 0; i < nfiles; i++)
				if (!used[i]) {
					err(&q, "ar: error: %s: %s\n", files[i], "no such member in the archive");
					free(used);
					goto out_lists;
				}
			if (insert < 0)
				insert = res.n;
			for (size_t j = 0; j < moved.n; j++)
				ml_push(&res, (member){ 0 });
			memmove(res.m + insert + moved.n, res.m + insert, (res.n - moved.n - insert) * sizeof(member));
			memcpy(res.m + insert, moved.m, moved.n * sizeof(member));
			free(moved.m);
			moved.m = NULL;
			moved.n = 0;
		} else {
			for (i = 0; i < nfiles; i++)
				if (!used[i] && !add_file(&q, files[i], &res)) {
					free(used);
					goto out_lists;
				}
		}
		free(used);
	}
	if (!q.failed && write_archive(&q, archive_abs, archive, &res, symtab))
		rc = 0;
out_lists:
	ml_free(&res);
	ml_free(&moved);
out:
	if (have_old)
		vunmap_file(&d);
	ml_free(&old);
	free(archive_abs);
	free(q.dir);
	free(files);
	return rc;
}
