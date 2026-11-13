/* JSON lines, and the record sink vccd reads. */
#include "../frontend/pp/pp-internal.h"
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

VPRIV void jstr(vbuffer *b, const char *s, size_t n)
{
	static const char hex[] = "0123456789abcdef";

	vbuffer_string_append(b, "\"");
	for (size_t i = 0; i < n; i++) {
		unsigned char c = (unsigned char)s[i];

		if (c == '"' || c == '\\') {
			char e[2] = { '\\', (char)c };

			vbuffer_append(b, e, 2);
		} else if (c == '\n')
			vbuffer_string_append(b, "\\n");
		else if (c == '\t')
			vbuffer_string_append(b, "\\t");
		else if (c < 0x20 || c == 0x7f) {
			char e[6] = { '\\', 'u', '0', '0', hex[c >> 4], hex[c & 15] };

			vbuffer_append(b, e, 6);
		} else
			vbuffer_append(b, &s[i], 1);
	}
	vbuffer_string_append(b, "\"");
}

VPRIV void jcstr(vbuffer *b, const char *s)
{
	if (s)
		jstr(b, s, strlen(s));
	else
		vbuffer_string_append(b, "null");
}

static const char *const lvjson[] = { "note", "remark", "warning", "error", "fatal error" };
static const char *const appname[] = { "unspecified", "machine", "maybe", "placeholder" };

/* {"file", "line", "column", "path", "offset"}, {"file": null} outside any file, null for none */
static void jloc(pp *p, vbuffer *b, uint32_t loc)
{
	const pp_file_entry *e;
	const char *name;
	uint32_t nl, line, col;

	if (loc == UINT32_MAX) {
		vbuffer_string_append(b, "null");
		return;
	}
	if (!loc_ok(p, loc, &e) || !diag_where(p, loc, &name, &nl, &line, &col)) {
		vbuffer_string_append(b, "{\"file\":null}");
		return;
	}
	vbuffer_string_append(b, "{\"file\":");
	jstr(b, name, nl);
	vbuffer_format_append(b, ",\"line\":%u,\"column\":%u,\"path\":", line, col);
	jcstr(b, e->f->path);
	vbuffer_format_append(b, ",\"offset\":%u}", loc - e->base);
}

/* the include stack of loc, outermost first: [{"file", "line"}] */
static void jincludes(pp *p, vbuffer *b, uint32_t loc)
{
	const pp_file_entry *chain[64], *e;
	int n = 0;

	vbuffer_string_append(b, "[");
	if (loc != UINT32_MAX && loc_ok(p, loc, &e))
		for (const pp_file_entry *x = e; x && x->from && n < 64; x = loc_fent(p, x->from))
			chain[n++] = x;
	for (int i = n, first = 1; i-- > 0;) {
		const char *name;
		uint32_t nl, line, col;

		if (!diag_where(p, chain[i]->from, &name, &nl, &line, &col))
			continue;
		vbuffer_string_append(b, first ? "{\"file\":" : ",{\"file\":");
		jstr(b, name, nl);
		vbuffer_format_append(b, ",\"line\":%u}", line);
		first = 0;
	}
	vbuffer_string_append(b, "]");
}

/* top: 0 a child, 1 a record, 2 a record without "rendered" (vccd's store) */
static void json_rec(pp *p, vbuffer *b, const vdiag *d, int top)
{
	vbuffer_string_append(b, "{\"id\":");
	jcstr(b, d->id);
	vbuffer_format_append(b, ",\"level\":\"%s\",\"flag\":", lvjson[d->level]);
	jcstr(b, d->flag);
	vbuffer_format_append(b, ",\"werror\":%s,\"message\":", d->werror ? "true" : "false");
	jcstr(b, d->msg);
	vbuffer_string_append(b, ",\"location\":");
	jloc(p, b, d->loc);
	vbuffer_string_append(b, ",\"label\":");
	jcstr(b, d->label);
	vbuffer_string_append(b, ",\"help\":");
	jcstr(b, d->help);
	if (d->art) {
		vbuffer_string_append(b, ",\"art\":");
		jcstr(b, d->art);
	}
	vbuffer_string_append(b, ",\"spans\":[");
	for (uint32_t i = 0; i < d->nspan; i++) {
		vbuffer_string_append(b, i ? ",{\"start\":" : "{\"start\":");
		jloc(p, b, d->span[i].b);
		vbuffer_string_append(b, ",\"end\":");
		jloc(p, b, d->span[i].e);
		vbuffer_string_append(b, ",\"label\":");
		jcstr(b, d->span[i].label);
		vbuffer_string_append(b, "}");
	}
	vbuffer_string_append(b, "],\"fixits\":[");
	for (uint32_t i = 0; i < d->nfix; i++) {
		vbuffer_string_append(b, i ? ",{\"start\":" : "{\"start\":");
		jloc(p, b, d->fix[i].b);
		vbuffer_string_append(b, ",\"end\":");
		jloc(p, b, d->fix[i].e);
		vbuffer_string_append(b, ",\"text\":");
		jcstr(b, d->fix[i].s);
		vbuffer_format_append(b, ",\"applicability\":\"%s\"}", appname[d->fix[i].app & 3]);
	}
	vbuffer_string_append(b, "],\"include_stack\":");
	jincludes(p, b, d->loc);
	if (top) {
		vbuffer r = { 0 };
		uint32_t fe = p->diagnostic_last_file;

		vbuffer_string_append(b, ",\"children\":[");
		for (const vdiag *k = d->kids; k; k = k->next) {
			json_rec(p, b, k, false);
			if (k->next)
				vbuffer_string_append(b, ",");
		}
		if (top == 2) {
			vbuffer_string_append(b, "]}");
			return;
		}
		vbuffer_string_append(b, "],\"rendered\":");
		/* the text as the text output prints it (in its style), with its include stack */
		p->diagnostic_last_file = 0;
		bool color = p->color;
		p->color = 0;
		diag_render_rec(p, &r, (vdiag *)d);
		p->color = color;
		p->diagnostic_last_file = fe;
		jstr(b, r.p ? r.p : "", r.n);
		free(r.p);
	}
	vbuffer_string_append(b, "}");
}

/* write the pending JSON record */
VPRIV void diag_json_flush(pp *p)
{
	int fd = p->o->errfd ? p->o->errfd : 2;
	vbuffer b = { 0 };

	if (!p->diagnostic_pending_structured)
		return;
	json_rec(p, &b, p->diagnostic_pending_structured, true);
	vbuffer_string_append(&b, "\n");
	(void)!write(fd, b.p, b.n);
	free(b.p);
	p->diagnostic_pending_structured = NULL;
}

/* ---------------- SARIF ---------------- */

VPRIV __attribute__((noinline, cold)) void dsink_records(pp *p)
{
	for (const vdiag *d = p->dhead; d; d = d->next) {
		pp_drec r = { .level = d->level, .flag = d->flag };
		const pp_file_entry *e;
		const char *name;
		uint32_t nl;
		vbuffer b = { 0 };

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
