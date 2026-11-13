/* JSON lines, and the record sink vccd reads. */
#include "../frontend/pp/ppimpl.h"
#include "common/buffer.h"
#include <ctype.h>
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

/*
 * vcc preprocessor: diagnostic records as JSON lines and SARIF 2.1
 * (PLAN-diag.md D3.1, D3.2).
 *
 * -fdiagnostics-format=json writes one record per line on stderr when it is
 * complete (its notes are children): id, level, flag, message, location,
 * label, spans, fix-its with their applicability, the include stack, and
 * "rendered", the record's text exactly as the text output would print it
 * (with its include stack), so a tool never renders.  A location carries the
 * presumed file, line and byte column as the text shows them, and the
 * physical path and byte offset, so bench/diag-render.py rebuilds the text
 * from the record and the source alone (test layer 5a).
 *
 * -fdiagnostics-format=sarif writes the TU's SARIF log on stderr at the end
 * instead of text; -fdiagnostics-add-output=sarif:file=F writes it to F as
 * well as the text.  Columns are Unicode code points (columnKind), regions
 * end exclusive, notes and secondary spans are relatedLocations, the include
 * chain is relatedLocations with an "isIncludedBy" relationship, fix-its are
 * fixes.  -fdiagnostics-add-output=html:file=F writes a standalone page of the
 * records as the vcc style shows them, in colour (PLAN-diag.md D8.4).  Only the
 * error path runs any of this.
 */

VPRIV void jstr(vbuf *b, const char *s, size_t n)
{
	static const char hex[] = "0123456789abcdef";

	vbuf_s(b, "\"");
	for (size_t i = 0; i < n; i++) {
		unsigned char c = (unsigned char)s[i];

		if (c == '"' || c == '\\') {
			char e[2] = { '\\', (char)c };

			vbuf_put(b, e, 2);
		} else if (c == '\n')
			vbuf_s(b, "\\n");
		else if (c == '\t')
			vbuf_s(b, "\\t");
		else if (c < 0x20 || c == 0x7f) {
			char e[6] = { '\\', 'u', '0', '0', hex[c >> 4], hex[c & 15] };

			vbuf_put(b, e, 6);
		} else
			vbuf_put(b, &s[i], 1);
	}
	vbuf_s(b, "\"");
}

VPRIV void jcstr(vbuf *b, const char *s)
{
	if (s)
		jstr(b, s, strlen(s));
	else
		vbuf_s(b, "null");
}

static const char *const lvjson[] = { "note", "remark", "warning", "error", "fatal error" };
static const char *const appname[] = { "unspecified", "machine", "maybe", "placeholder" };

/* {"file", "line", "column", "path", "offset"}, {"file": null} outside any file, null for none */
static void jloc(pp *p, vbuf *b, uint32_t loc)
{
	const fent *e;
	const char *name;
	uint32_t nl, line, col;

	if (loc == UINT32_MAX) {
		vbuf_s(b, "null");
		return;
	}
	if (!loc_ok(p, loc, &e) || !diag_where(p, loc, &name, &nl, &line, &col)) {
		vbuf_s(b, "{\"file\":null}");
		return;
	}
	vbuf_s(b, "{\"file\":");
	jstr(b, name, nl);
	vbuf_f(b, ",\"line\":%u,\"column\":%u,\"path\":", line, col);
	jcstr(b, e->f->path);
	vbuf_f(b, ",\"offset\":%u}", loc - e->base);
}

/* the include stack of loc, outermost first: [{"file", "line"}] */
static void jincludes(pp *p, vbuf *b, uint32_t loc)
{
	const fent *chain[64], *e;
	int n = 0;

	vbuf_s(b, "[");
	if (loc != UINT32_MAX && loc_ok(p, loc, &e))
		for (const fent *x = e; x && x->from && n < 64; x = loc_fent(p, x->from))
			chain[n++] = x;
	for (int i = n, first = 1; i-- > 0;) {
		const char *name;
		uint32_t nl, line, col;

		if (!diag_where(p, chain[i]->from, &name, &nl, &line, &col))
			continue;
		vbuf_s(b, first ? "{\"file\":" : ",{\"file\":");
		jstr(b, name, nl);
		vbuf_f(b, ",\"line\":%u}", line);
		first = 0;
	}
	vbuf_s(b, "]");
}

/* top: 0 a child, 1 a record, 2 a record without "rendered" (vccd's store) */
static void json_rec(pp *p, vbuf *b, const vdiag *d, int top)
{
	vbuf_s(b, "{\"id\":");
	jcstr(b, d->id);
	vbuf_f(b, ",\"level\":\"%s\",\"flag\":", lvjson[d->level]);
	jcstr(b, d->flag);
	vbuf_f(b, ",\"werror\":%s,\"message\":", d->werror ? "true" : "false");
	jcstr(b, d->msg);
	vbuf_s(b, ",\"location\":");
	jloc(p, b, d->loc);
	vbuf_s(b, ",\"label\":");
	jcstr(b, d->label);
	vbuf_s(b, ",\"help\":");
	jcstr(b, d->help);
	if (d->art) {
		vbuf_s(b, ",\"art\":");
		jcstr(b, d->art);
	}
	vbuf_s(b, ",\"spans\":[");
	for (uint32_t i = 0; i < d->nspan; i++) {
		vbuf_s(b, i ? ",{\"start\":" : "{\"start\":");
		jloc(p, b, d->span[i].b);
		vbuf_s(b, ",\"end\":");
		jloc(p, b, d->span[i].e);
		vbuf_s(b, ",\"label\":");
		jcstr(b, d->span[i].label);
		vbuf_s(b, "}");
	}
	vbuf_s(b, "],\"fixits\":[");
	for (uint32_t i = 0; i < d->nfix; i++) {
		vbuf_s(b, i ? ",{\"start\":" : "{\"start\":");
		jloc(p, b, d->fix[i].b);
		vbuf_s(b, ",\"end\":");
		jloc(p, b, d->fix[i].e);
		vbuf_s(b, ",\"text\":");
		jcstr(b, d->fix[i].s);
		vbuf_f(b, ",\"applicability\":\"%s\"}", appname[d->fix[i].app & 3]);
	}
	vbuf_s(b, "],\"include_stack\":");
	jincludes(p, b, d->loc);
	if (top) {
		vbuf r = { 0 };
		uint32_t fe = p->diag_fe;

		vbuf_s(b, ",\"children\":[");
		for (const vdiag *k = d->kids; k; k = k->next) {
			json_rec(p, b, k, false);
			if (k->next)
				vbuf_s(b, ",");
		}
		if (top == 2) {
			vbuf_s(b, "]}");
			return;
		}
		vbuf_s(b, "],\"rendered\":");
		/* the text as the text output prints it (in its style), with its include stack */
		p->diag_fe = 0;
		bool color = p->color;
		p->color = 0;
		diag_render_rec(p, &r, (vdiag *)d);
		p->color = color;
		p->diag_fe = fe;
		jstr(b, r.p ? r.p : "", r.n);
		free(r.p);
	}
	vbuf_s(b, "}");
}

/* write the pending JSON record */
VPRIV void diag_json_flush(pp *p)
{
	int fd = p->o->errfd ? p->o->errfd : 2;
	vbuf b = { 0 };

	if (!p->dpend)
		return;
	json_rec(p, &b, p->dpend, true);
	vbuf_s(&b, "\n");
	(void)!write(fd, b.p, b.n);
	free(b.p);
	p->dpend = NULL;
}

/* ---------------- SARIF ---------------- */

VPRIV __attribute__((noinline, cold)) void dsink_records(pp *p)
{
	for (const vdiag *d = p->dhead; d; d = d->next) {
		pp_drec r = { .level = d->level, .flag = d->flag };
		const fent *e;
		const char *name;
		uint32_t nl;
		vbuf b = { 0 };

		if (d->loc != UINT32_MAX && loc_ok(p, d->loc, &e) && diag_where(p, d->loc, &name, &nl, &r.line, &r.col))
			r.path = e->f->path;
		if (!p->o->dsink->nojson) {
			json_rec(p, &b, d, 2);
			r.json = b.p ? b.p : "";
			r.jlen = b.n;
		}
		p->o->dsink->record(p->o->dsink, &r);
		free(b.p);
	}
}

/* ---------------- -fdiagnostics-add-output=html:file=F ---------------- */

/* text with the colour codes the vcc style writes, as HTML: escaped, each code a span */
