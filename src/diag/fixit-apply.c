/* Applying fix-its to files (--fix) and the unified diff of the change. */
#include "../frontend/pp/pp-internal.h"
#include "common/buffer.h"
#include <ctype.h>
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

/* ---------------- fix-its applied: -fdiagnostics-generate-patch, --fix ---------------- */

typedef struct diag_fixit_edit { const lex_file *f; uint32_t b, e, seq; const char *s; } diag_fixit_edit;

typedef struct diag_fixit_edit_list { diag_fixit_edit *v; uint32_t n, cap; } diag_fixit_edit_list;

static void diag_fixit_edit_add(const pp *p, diag_fixit_edit_list *x, const diag_record *d, bool machine)
{
	for (uint32_t i = 0; i < d->nfix; i++) {
		const diag_record_fixit *k = &d->fix[i];
		const pp_file_entry *f;

		if ((machine && k->app != DIAG_APPLICABILITY_MACHINE) || !pp_location_is_valid(p, k->b, &f) || f->is_system || k->e < k->b ||
		    k->e - f->location_base > f->lex_file->size || !f->lex_file->path)
			continue;
		if (x->n == x->cap)
			x->v = realloc(x->v, (x->cap = x->cap ? x->cap * 2 : 64) * sizeof(*x->v));
		x->v[x->n] = (diag_fixit_edit){ f->lex_file, k->b - f->location_base, k->e - f->location_base, x->n, k->s };
		x->n++;
	}
}

static int diag_fixit_edit_compare(const void *a, const void *b)
{
	const diag_fixit_edit *x = a, *y = b;

	if (x->f != y->f)
		return x->f < y->f ? -1 : 1;
	if (x->b != y->b)
		return x->b < y->b ? -1 : 1;
	if (x->e != y->e)
		return x->e < y->e ? -1 : 1;
	return x->seq < y->seq ? -1 : x->seq > y->seq;
}

/* every record's fix-its (machine: only machine-applicable ones, notes' too; else a
   record's own, or when it has none its first note's, the safest first: D6), sorted by
   file and offset; an edit that overlaps one before it, or repeats it, is left out */
static void diag_fixit_edits_collect(const pp *p, diag_fixit_edit_list *x, bool machine)
{
	uint32_t k = 0;

	for (const diag_record *d = p->diagnostic_head; d; d = d->next) {
		diag_fixit_edit_add(p, x, d, machine);
		for (const diag_record *c = d->kids; c; c = c->next)
			if (machine) {
				diag_fixit_edit_add(p, x, c, true);
			} else if (!d->nfix && c->nfix) {
				diag_fixit_edit_add(p, x, c, false);
				break;
			}
	}
	if (x->n)
		qsort(x->v, x->n, sizeof(*x->v), diag_fixit_edit_compare);
	for (uint32_t i = 0; i < x->n; i++) {
		const diag_fixit_edit *a = &x->v[i], *l = k ? &x->v[k - 1] : NULL;

		if (l && l->f == a->f && (a->b < l->e || (a->b == l->b && a->e == l->e && (a->b != a->e || !strcmp(a->s, l->s)))))
			continue;
		x->v[k++] = *a;
	}
	x->n = k;
}

/* src[lo, hi) with the edits v[0, n) (inside it) applied */
static void fix_apply(vbuffer *o, const char *src, uint32_t lo, uint32_t hi, const diag_fixit_edit *v, uint32_t n)
{
	uint32_t at = lo;

	for (uint32_t i = 0; i < n; i++) {
		vbuffer_append(o, src + at, v[i].b - at);
		vbuffer_string_append(o, v[i].s);
		at = v[i].e;
	}
	vbuffer_append(o, src + at, hi - at);
}

static uint32_t line_of(const char *s, uint32_t off)	/* 1-based */
{
	uint32_t n = 1;

	for (uint32_t i = 0; i < off; i++)
		n += s[i] == '\n';
	return n;
}

static uint32_t diag_fixit_line_offset(const char *s, uint32_t size, uint32_t line)	/* offset of line (1-based), or size */
{
	uint32_t i = 0;

	for (uint32_t l = 1; l < line && i < size; i++)
		l += s[i] == '\n';
	return i;
}

/* the text's lines, each prefixed by c ('-', '+', ' ') */
static uint32_t diag_fixit_patch_lines_append(vbuffer *b, char c, const char *s, uint32_t n)
{
	uint32_t k = 0;

	for (uint32_t i = 0; i < n;) {
		uint32_t j = i;

		while (j < n && s[j] != '\n')
			j++;
		vbuffer_append(b, &c, 1);
		vbuffer_append(b, s + i, j - i);
		vbuffer_string_append(b, j < n ? "\n" : "\n\\ No newline at end of file\n");
		i = j + 1, k++;
	}
	return k;
}

static uint32_t nlines(const char *s, uint32_t n)
{
	uint32_t k = 0;

	for (uint32_t i = 0; i < n; i++)
		k += s[i] == '\n';
	return k + (n && s[n - 1] != '\n');
}

/* a unified diff of one file's edits v[0, n), 3 lines of context (GCC's -fdiagnostics-generate-patch):
   edits on the same or adjacent lines are one block of - and + lines, blocks less than 7 lines
   apart one hunk */
static void diag_fixit_patch_format(vbuffer *b, const lex_file *f, const diag_fixit_edit *v, uint32_t n)
{
	const char *s = f->src;
	uint32_t total = nlines(s, f->size);
	int32_t delta = 0;

	vbuffer_format_append(b, "--- %s\n+++ %s\n", f->path, f->path);
	for (uint32_t i = 0; i < n;) {
		uint32_t c1 = line_of(s, v[i].b), c2, prev = 0, no = 0, nn = 0;
		vbuffer h = { 0 };

		c1 = c1 > 3 ? c1 - 3 : 1;
		for (;;) {	/* the blocks of one hunk */
			uint32_t j = i + 1, l1 = line_of(s, v[i].b), l2 = line_of(s, v[i].e), mb, me, k;
			vbuffer mid = { 0 };

			while (j < n && line_of(s, v[j].b) <= l2 + 1) {
				uint32_t le = line_of(s, v[j].e);

				l2 = le > l2 ? le : l2, j++;
			}
			mb = diag_fixit_line_offset(s, f->size, prev ? prev + 1 : c1);	/* context since the last block */
			me = diag_fixit_line_offset(s, f->size, l1);
			no += k = diag_fixit_patch_lines_append(&h, ' ', s + mb, me - mb), nn += k;
			mb = me, me = diag_fixit_line_offset(s, f->size, l2 + 1);
			fix_apply(&mid, s, mb, me, v + i, j - i);
			no += diag_fixit_patch_lines_append(&h, '-', s + mb, me - mb);
			nn += diag_fixit_patch_lines_append(&h, '+', mid.p ? mid.p : "", (uint32_t)mid.n);
			free(mid.p);
			prev = l2, i = j;
			if (i >= n || line_of(s, v[i].b) > prev + 7)
				break;
		}
		c2 = prev + 3 < total ? prev + 3 : total;
		{
			uint32_t mb = diag_fixit_line_offset(s, f->size, prev + 1), me = diag_fixit_line_offset(s, f->size, c2 + 1), k;

			no += k = diag_fixit_patch_lines_append(&h, ' ', s + mb, me - mb), nn += k;
		}
		vbuffer_format_append(b, "@@ -%u,%u +%u,%u @@\n", c1, no, (uint32_t)((int32_t)c1 + delta), nn);
		vbuffer_append(b, h.p, h.n);
		delta += (int32_t)nn - (int32_t)no;
		free(h.p);
	}
}

/* --fix: each file's machine-applicable edits written back (a new file renamed over it) */
static void diag_fixit_file_write(pp *p, int fd, const lex_file *f, const diag_fixit_edit *v, uint32_t n)
{
	char path[4096], tmp[4200];
	const char *fp = f->path;
	vbuffer o = { 0 };
	struct stat st;
	int out;

	if (fp[0] != '/' && p->options->cwd)
		snprintf(path, sizeof(path), "%s/%s", p->options->cwd, fp), fp = path;
	fix_apply(&o, f->src, 0, f->size, v, n);
	snprintf(tmp, sizeof(tmp), "%s.vcc-fix.%d", fp, (int)getpid());
	if ((out = open(tmp, O_WRONLY | O_CREAT | O_EXCL, 0644)) < 0) {
		dprintf(fd, "vcc: error: cannot write '%s': %s\n", tmp, strerror(errno));
		free(o.p);
		return;
	}
	if (!stat(fp, &st))
		(void)fchmod(out, st.st_mode & 07777);
	if (write(out, o.p ? o.p : "", o.n) != (ssize_t)o.n || close(out) || rename(tmp, fp)) {
		dprintf(fd, "vcc: error: cannot write '%s': %s\n", fp, strerror(errno));
		unlink(tmp);
	} else {
		dprintf(fd, "%s: note: applied %u fix-it%s\n", f->path, n, n > 1 ? "s" : "");
	}
	free(o.p);
}

VPRIV void diag_fixit_output(pp *p)
{
	int fd = p->options->errfd ? p->options->errfd : 2;

	for (int machine = 0; machine < 2; machine++) {
		diag_fixit_edit_list x = { 0 };
		vbuffer b = { 0 };

		if (!(machine ? p->options->fix_apply : p->options->diag_patch))
			continue;
		diag_fixit_edits_collect(p, &x, machine);
		for (uint32_t i = 0; i < x.n;) {
			uint32_t j = i;

			while (j < x.n && x.v[j].f == x.v[i].f)
				j++;
			if (machine)
				diag_fixit_file_write(p, fd, x.v[i].f, x.v + i, j - i);
			else
				diag_fixit_patch_format(&b, x.v[i].f, x.v + i, j - i);
			i = j;
		}
		if (b.n)
			(void)!write(fd, b.p, b.n);
		free(b.p), free(x.v);
	}
}
