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
 * fixes.  Only the error path runs any of this.
 */
#include "ppimpl.h"
#include "../lib/buf.h"
#include <errno.h>
#include <fcntl.h>

static void jstr(vbuf *b, const char *s, size_t n)
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

static void jcstr(vbuf *b, const char *s)
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

static void json_rec(pp *p, vbuf *b, const vdiag *d, bool top)
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
		vbuf_s(b, "],\"rendered\":");
		/* the text as the text output prints it, with its include stack */
		p->diag_fe = 0;
		bool color = p->color;
		p->color = 0;
		render_text(p, &r, d);
		for (const vdiag *k = d->kids; k; k = k->next)
			render_text(p, &r, k);
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

static void juri_path(vbuf *b, const char *s, size_t n)
{
	static const char hex[] = "0123456789ABCDEF";

	for (size_t i = 0; i < n; i++) {
		unsigned char c = (unsigned char)s[i];

		if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || strchr("-._~/", c))
			vbuf_put(b, &s[i], 1);
		else {
			char e[3] = { '%', hex[c >> 4], hex[c & 15] };

			vbuf_put(b, e, 3);
		}
	}
}

/* {"uri": ...} (absolute: file://; relative: against %SRCROOT%, the working directory) */
static void jartifact(vbuf *b, const char *name, size_t nl)
{
	vbuf u = { 0 };

	if (nl && name[0] == '/')
		vbuf_s(&u, "file://");
	juri_path(&u, name, nl);
	vbuf_s(b, "{\"uri\":");
	jstr(b, u.p, u.n);
	if (!(nl && name[0] == '/'))
		vbuf_s(b, ",\"uriBaseId\":\"%SRCROOT%\"");
	vbuf_s(b, "}");
	free(u.p);
}

/* the presumed line and the code point column (1-based) of loc */
static bool sarif_pos(pp *p, uint32_t loc, const char **name, uint32_t *nl, uint32_t *line, uint32_t *ucol)
{
	const fent *e;
	uint32_t col, off, ls;

	if (loc == UINT32_MAX || !loc_ok(p, loc, &e) || !diag_where(p, loc, name, nl, line, &col))
		return false;
	off = loc - e->base;
	ls = off - (col - 1);
	*ucol = 1;
	for (uint32_t i = ls; i < off; i++)
		if (((unsigned char)e->f->src[i] & 0xc0) != 0x80)
			(*ucol)++;
	return true;
}

/* "physicalLocation" of [b, e) (e == b: the token at b) */
static bool jphys(pp *p, vbuf *b, uint32_t lb, uint32_t le)
{
	const char *name, *n2;
	uint32_t nl, l2n, line, col, l2, c2;

	if (!sarif_pos(p, lb, &name, &nl, &line, &col))
		return false;
	if (le <= lb)
		le = pp_tok_end(p, lb);
	if (!sarif_pos(p, le, &n2, &l2n, &l2, &c2) || l2n != nl || memcmp(n2, name, nl) || l2 < line)
		l2 = line, c2 = col + 1;
	vbuf_s(b, "\"physicalLocation\":{\"artifactLocation\":");
	jartifact(b, name, nl);
	vbuf_f(b, ",\"region\":{\"startLine\":%u,\"startColumn\":%u,\"endLine\":%u,\"endColumn\":%u}}", line, col, l2,
	       c2 > col || l2 > line ? c2 : col + 1);
	return true;
}

static void sarif_result(pp *p, vbuf *b, const vdiag *d, const char *const *rules, uint32_t nrules)
{
	uint32_t rule = 0, nrel = 0, id = 1;
	const fent *e;
	uint32_t pe = d->loc;

	while (rule < nrules && strcmp(rules[rule], d->id ? d->id : "vcc_unknown"))
		rule++;
	vbuf_s(b, "{\"ruleId\":");
	jcstr(b, d->id ? d->id : "vcc_unknown");
	vbuf_f(b, ",\"ruleIndex\":%u,\"level\":\"%s\",\"message\":{\"text\":", rule,
	       d->level >= VDL_ERROR ? "error" : d->level == VDL_WARNING ? "warning" : "note");
	jcstr(b, d->msg);
	vbuf_s(b, "}");
	/* the primary location: the span that holds loc, else the token at loc */
	for (uint32_t i = 0; i < d->nspan; i++)
		if (d->span[i].b <= d->loc && d->loc < d->span[i].e)
			pe = d->span[i].e;
	if (d->loc != UINT32_MAX && loc_ok(p, d->loc, &e)) {
		vbuf m = { 0 };
		int nchain = 0;

		if (!jphys(p, &m, d->loc, pe))
			goto noloc;
		vbuf_s(b, ",\"locations\":[{\"id\":0,");
		vbuf_put(b, m.p, m.n);
		free(m.p);
		if (d->label) {
			vbuf_s(b, ",\"message\":{\"text\":");
			jcstr(b, d->label);
			vbuf_s(b, "}");
		}
		for (const fent *x = e; x && x->from && nchain < 64; x = loc_fent(p, x->from))
			nchain++;
		if (nchain) {
			vbuf_s(b, ",\"relationships\":[{\"target\":1,\"kinds\":[\"isIncludedBy\"]}]");
		}
		vbuf_s(b, "}]");
		/* related: the include chain (each included by the next), spans, notes */
		vbuf_s(b, ",\"relatedLocations\":[");
		nchain = 0;
		for (const fent *x = e; x && x->from && nchain < 64; x = loc_fent(p, x->from), nchain++) {
			const fent *up = loc_fent(p, x->from);

			vbuf_f(b, "%s{\"id\":%u,", nrel++ ? "," : "", id);
			jphys(p, b, x->from, x->from);
			vbuf_s(b, ",\"message\":{\"text\":\"included from here\"}");
			if (up && up->from)
				vbuf_f(b, ",\"relationships\":[{\"target\":%u,\"kinds\":[\"isIncludedBy\"]}]", id + 1);
			vbuf_s(b, "}");
			id++;
		}
		for (uint32_t i = 0; i < d->nspan; i++) {
			vbuf s = { 0 };

			if (d->span[i].b <= d->loc && d->loc < d->span[i].e && !d->span[i].label)
				continue;	/* the primary location's own range */
			if (!jphys(p, &s, d->span[i].b, d->span[i].e)) {
				free(s.p);
				continue;
			}
			vbuf_f(b, "%s{\"id\":%u,", nrel++ ? "," : "", id++);
			vbuf_put(b, s.p, s.n);
			free(s.p);
			if (d->span[i].label) {
				vbuf_s(b, ",\"message\":{\"text\":");
				jcstr(b, d->span[i].label);
				vbuf_s(b, "}");
			}
			vbuf_s(b, "}");
		}
		for (const vdiag *k = d->kids; k; k = k->next) {
			vbuf s = { 0 };

			vbuf_f(b, "%s{\"id\":%u,", nrel++ ? "," : "", id++);
			if (jphys(p, &s, k->loc, k->loc)) {
				vbuf_put(b, s.p, s.n);
				vbuf_s(b, ",");
			}
			free(s.p);
			vbuf_s(b, "\"message\":{\"text\":");
			jcstr(b, k->msg);
			vbuf_s(b, "}}");
		}
		vbuf_s(b, "]");
	}
noloc:
	if (d->nfix) {
		vbuf_s(b, ",\"fixes\":[{\"artifactChanges\":[");
		for (uint32_t i = 0, n = 0; i < d->nfix; i++) {
			const char *name;
			uint32_t nl, line, col, l2, c2;

			if (!sarif_pos(p, d->fix[i].b, &name, &nl, &line, &col))
				continue;
			if (!sarif_pos(p, d->fix[i].e, &name, &nl, &l2, &c2))
				l2 = line, c2 = col;
			vbuf_s(b, n++ ? ",{\"artifactLocation\":" : "{\"artifactLocation\":");
			jartifact(b, name, nl);
			vbuf_f(b, ",\"replacements\":[{\"deletedRegion\":{\"startLine\":%u,\"startColumn\":%u,"
				  "\"endLine\":%u,\"endColumn\":%u},\"insertedContent\":{\"text\":", line, col, l2, c2);
			jcstr(b, d->fix[i].s);
			vbuf_s(b, "}}]}");
		}
		vbuf_s(b, "]}]");
	}
	vbuf_s(b, ",\"properties\":{\"vcc/flag\":");
	jcstr(b, d->flag);
	vbuf_f(b, ",\"vcc/werror\":%s}}", d->werror ? "true" : "false");
}

static void sarif_log(pp *p, vbuf *b)
{
	const char *rules[512];
	uint32_t nr = 0;

	for (const vdiag *d = p->dhead; d; d = d->next) {
		const char *id = d->id ? d->id : "vcc_unknown";
		uint32_t i = 0;

		while (i < nr && strcmp(rules[i], id))
			i++;
		if (i == nr && nr < 512)
			rules[nr++] = id;
	}
	vbuf_s(b, "{\"$schema\":\"https://json.schemastore.org/sarif-2.1.0.json\",\"version\":\"2.1.0\",\"runs\":[{"
		  "\"tool\":{\"driver\":{\"name\":\"vcc\",\"rules\":[");
	for (uint32_t i = 0; i < nr; i++) {
		vbuf_s(b, i ? ",{\"id\":" : "{\"id\":");
		jcstr(b, rules[i]);
		vbuf_s(b, "}");
	}
	vbuf_s(b, "]}},\"columnKind\":\"unicodeCodePoints\",\"originalUriBaseIds\":{\"%SRCROOT%\":{\"uri\":");
	{
		vbuf u = { 0 };
		const char *cwd = p->o->cwd ? p->o->cwd : ".";

		vbuf_s(&u, "file://");
		juri_path(&u, cwd, strlen(cwd));
		if (!u.n || u.p[u.n - 1] != '/')
			vbuf_s(&u, "/");
		jstr(b, u.p, u.n);
		free(u.p);
	}
	vbuf_f(b, "}},\"invocations\":[{\"executionSuccessful\":%s,\"arguments\":[", p->errors ? "false" : "true");
	for (int i = 0; i < p->o->argc; i++) {
		if (i)
			vbuf_s(b, ",");
		jcstr(b, p->o->argv[i]);
	}
	vbuf_s(b, "]}],\"results\":[");
	for (const vdiag *d = p->dhead; d; d = d->next) {
		sarif_result(p, b, d, rules, nr);
		if (d->next)
			vbuf_s(b, ",");
	}
	vbuf_s(b, "]}]}\n");
}

void pp_diag_finish(pp *p)
{
	int fd = p->o->errfd ? p->o->errfd : 2;

	if (p->dfinished)
		return;
	p->dfinished = true;
	diag_json_flush(p);
	if (p->o->diag_format == PP_DIAG_SARIF || p->o->sarif_file) {
		vbuf b = { 0 };

		sarif_log(p, &b);
		if (p->o->diag_format == PP_DIAG_SARIF)
			(void)!write(fd, b.p, b.n);
		if (p->o->sarif_file) {
			char path[4096];
			const char *f = p->o->sarif_file;
			int out;

			if (f[0] != '/' && p->o->cwd)
				snprintf(path, sizeof(path), "%s/%s", p->o->cwd, f), f = path;
			if ((out = open(f, O_WRONLY | O_CREAT | O_TRUNC, 0644)) >= 0) {
				(void)!write(out, b.p, b.n);
				close(out);
			} else {
				dprintf(fd, "vcc: error: cannot write '%s': %s\n", f, strerror(errno));
			}
		}
		free(b.p);
	}
}
