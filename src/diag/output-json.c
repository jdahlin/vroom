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

VPRIV void diag_json_string_append(vbuffer *b, const char *s, size_t n)
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

VPRIV void diag_json_cstring_append(vbuffer *b, const char *s)
{
	if (s)
		diag_json_string_append(b, s, strlen(s));
	else
		vbuffer_string_append(b, "null");
}

static const char *const diag_json_level_names[] = { "note", "remark", "warning", "error", "fatal error" };
static const char *const diag_json_applicability_names[] = { "unspecified", "machine", "maybe", "placeholder" };

/* {"file", "line", "column", "path", "offset"}, {"file": null} outside any file, null for none */
static void diag_json_location_append(pp *p, vbuffer *b, uint32_t loc)
{
	const pp_file_entry *e;
	const char *name;
	uint32_t nl, line, col;

	if (loc == UINT32_MAX) {
		vbuffer_string_append(b, "null");
		return;
	}
	if (!pp_location_is_valid(p, loc, &e) || !diag_location_presumed(p, loc, &name, &nl, &line, &col)) {
		vbuffer_string_append(b, "{\"file\":null}");
		return;
	}
	vbuffer_string_append(b, "{\"file\":");
	diag_json_string_append(b, name, nl);
	vbuffer_format_append(b, ",\"line\":%u,\"column\":%u,\"path\":", line, col);
	diag_json_cstring_append(b, e->lex_file->path);
	vbuffer_format_append(b, ",\"offset\":%u}", loc - e->location_base);
}

/* the include stack of loc, outermost first: [{"file", "line"}] */
static void diag_json_include_stack_append(pp *p, vbuffer *b, uint32_t loc)
{
	const pp_file_entry *chain[64], *e;
	int n = 0;

	vbuffer_string_append(b, "[");
	if (loc != UINT32_MAX && pp_location_is_valid(p, loc, &e))
		for (const pp_file_entry *x = e; x && x->included_from && n < 64; x = pp_location_file_entry(p, x->included_from))
			chain[n++] = x;
	for (int i = n, first = 1; i-- > 0;) {
		const char *name;
		uint32_t nl, line, col;

		if (!diag_location_presumed(p, chain[i]->included_from, &name, &nl, &line, &col))
			continue;
		vbuffer_string_append(b, first ? "{\"file\":" : ",{\"file\":");
		diag_json_string_append(b, name, nl);
		vbuffer_format_append(b, ",\"line\":%u}", line);
		first = 0;
	}
	vbuffer_string_append(b, "]");
}

/* top: 0 a child, 1 a record, 2 a record without "rendered" (vccd's store) */
static void diag_json_record_append(pp *p, vbuffer *b, const diag_record *d, int top)
{
	vbuffer_string_append(b, "{\"id\":");
	diag_json_cstring_append(b, d->id);
	vbuffer_format_append(b, ",\"level\":\"%s\",\"flag\":", diag_json_level_names[d->level]);
	diag_json_cstring_append(b, d->flag);
	vbuffer_format_append(b, ",\"werror\":%s,\"message\":", d->werror ? "true" : "false");
	diag_json_cstring_append(b, d->msg);
	vbuffer_string_append(b, ",\"location\":");
	diag_json_location_append(p, b, d->loc);
	vbuffer_string_append(b, ",\"label\":");
	diag_json_cstring_append(b, d->label);
	vbuffer_string_append(b, ",\"help\":");
	diag_json_cstring_append(b, d->help);
	if (d->art) {
		vbuffer_string_append(b, ",\"art\":");
		diag_json_cstring_append(b, d->art);
	}
	vbuffer_string_append(b, ",\"spans\":[");
	for (uint32_t i = 0; i < d->nspan; i++) {
		vbuffer_string_append(b, i ? ",{\"start\":" : "{\"start\":");
		diag_json_location_append(p, b, d->span[i].b);
		vbuffer_string_append(b, ",\"end\":");
		diag_json_location_append(p, b, d->span[i].e);
		vbuffer_string_append(b, ",\"label\":");
		diag_json_cstring_append(b, d->span[i].label);
		vbuffer_string_append(b, "}");
	}
	vbuffer_string_append(b, "],\"fixits\":[");
	for (uint32_t i = 0; i < d->nfix; i++) {
		vbuffer_string_append(b, i ? ",{\"start\":" : "{\"start\":");
		diag_json_location_append(p, b, d->fix[i].b);
		vbuffer_string_append(b, ",\"end\":");
		diag_json_location_append(p, b, d->fix[i].e);
		vbuffer_string_append(b, ",\"text\":");
		diag_json_cstring_append(b, d->fix[i].s);
		vbuffer_format_append(b, ",\"applicability\":\"%s\"}", diag_json_applicability_names[d->fix[i].app & 3]);
	}
	vbuffer_string_append(b, "],\"include_stack\":");
	diag_json_include_stack_append(p, b, d->loc);
	if (top) {
		vbuffer r = { 0 };
		uint32_t fe = p->diagnostic_last_file;

		vbuffer_string_append(b, ",\"children\":[");
		for (const diag_record *k = d->kids; k; k = k->next) {
			diag_json_record_append(p, b, k, false);
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
		bool color = p->diagnostic_color;
		p->diagnostic_color = 0;
		diag_record_render_plain(p, &r, (diag_record *)d);
		p->diagnostic_color = color;
		p->diagnostic_last_file = fe;
		diag_json_string_append(b, r.p ? r.p : "", r.n);
		free(r.p);
	}
	vbuffer_string_append(b, "}");
}

/* write the pending JSON record */
VPRIV void diag_json_record_flush(pp *p)
{
	int fd = p->options->errfd ? p->options->errfd : 2;
	vbuffer b = { 0 };

	if (!p->diagnostic_pending_structured)
		return;
	diag_json_record_append(p, &b, p->diagnostic_pending_structured, true);
	vbuffer_string_append(&b, "\n");
	(void)!write(fd, b.p, b.n);
	free(b.p);
	p->diagnostic_pending_structured = NULL;
}

/* ---------------- SARIF ---------------- */

VPRIV __attribute__((noinline, cold)) void diag_sink_records_send(pp *p)
{
	for (const diag_record *d = p->diagnostic_head; d; d = d->next) {
		diag_sink_record r = { .level = d->level, .flag = d->flag };
		const pp_file_entry *e;
		const char *name;
		uint32_t nl;
		vbuffer b = { 0 };

		if (d->loc != UINT32_MAX && pp_location_is_valid(p, d->loc, &e) && diag_location_presumed(p, d->loc, &name, &nl, &r.line, &r.col))
			r.path = e->lex_file->path;
		if (!p->options->dsink->nojson) {
			diag_json_record_append(p, &b, d, 2);
			r.json = b.p ? b.p : "";
			r.jlen = b.n;
		}
		p->options->dsink->record(p->options->dsink, &r);
		free(b.p);
	}
}

/* ---------------- -fdiagnostics-add-output=html:file=F ---------------- */

/* text with the colour codes the vcc style writes, as HTML: escaped, each code a span */
