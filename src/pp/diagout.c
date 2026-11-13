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
#include "ppimpl.h"
#include "../common/buf.h"
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

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
	vbuf_s(b, ",\"properties\":{\"vcc/help\":");
	jcstr(b, d->help);
	if (d->art) {
		vbuf_s(b, ",\"vcc/art\":");
		jcstr(b, d->art);
	}
	vbuf_s(b, ",\"vcc/flag\":");
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

/* vccd: every top-level record, as JSON, to the build's store */
static __attribute__((noinline, cold)) void dsink_records(pp *p)
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
static void html_text(vbuf *b, const char *s, size_t n)
{
	bool open = false, link = false;

	for (size_t i = 0; i < n; i++) {
		char ch = s[i];

		if (ch == '\033' && i + 4 < n && !memcmp(s + i + 1, "]8;;", 4)) {	/* OSC 8: a link */
			size_t j = i + 5, u = j;

			while (j < n && s[j] != '\033' && s[j] != '\007')
				j++;
			if (link)
				vbuf_s(b, "</a>");
			link = j > u;
			if (link) {
				vbuf_s(b, "<a href=\"");
				html_text(b, s + u, j - u);
				vbuf_s(b, "\">");
			}
			i = j < n && s[j] == '\033' ? j + 1 : j;	/* (ESC \\ or BEL ends it) */
			continue;
		}
		if (ch == '\033' && i + 1 < n && s[i + 1] == '[') {
			size_t j = i + 2;
			bool bold = false;
			int col = 0;

			while (j < n && s[j] != 'm') {
				int v = 0;

				while (j < n && s[j] >= '0' && s[j] <= '9')
					v = v * 10 + (s[j++] - '0');
				if (v == 1)
					bold = true;
				else if (v >= 30 && v <= 37)
					col = v;
				if (j < n && s[j] == ';')
					j++;
			}
			if (link)	/* (spans and links nest: a code ends a link) */
				vbuf_s(b, "</a>"), link = false;
			if (open)
				vbuf_s(b, "</span>");
			open = bold || col;
			if (open && col)
				vbuf_f(b, "<span class=\"%sc%d\">", bold ? "b " : "", col);
			else if (open)
				vbuf_s(b, "<span class=\"b\">");
			i = j;
			continue;
		}
		if (ch == '<')
			vbuf_s(b, "&lt;");
		else if (ch == '>')
			vbuf_s(b, "&gt;");
		else if (ch == '&')
			vbuf_s(b, "&amp;");
		else
			vbuf_put(b, &ch, 1);
	}
	if (link)
		vbuf_s(b, "</a>");
	if (open)
		vbuf_s(b, "</span>");
}

/* the TU's records as one standalone page: a summary, then each record as the vcc style
   shows it (source lines, labels, notes, help, diagrams), in colour */
static void html_page(pp *p, vbuf *b)
{
	unsigned ne = 0, nw = 0;
	const char *main = p->o->main ? p->o->main : "";

	for (const vdiag *d = p->dhead; d; d = d->next)
		if (!d->dup)
			ne += d->level >= VDL_ERROR, nw += d->level == VDL_WARNING;
	vbuf_s(b, "<!doctype html>\n<html lang=\"en\"><head><meta charset=\"utf-8\">\n"
		  "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">\n<title>vcc: ");
	html_text(b, main, strlen(main));
	vbuf_s(b, "</title>\n<style>\n"
		  ":root{--bg:#fbfbfa;--fg:#1d1d1b;--mut:#6b6b66;--card:#fff;--line:#e4e3df;"
		  "--err:#b42318;--warn:#a15c00;--note:#1f5fbf;--ok:#1a7f37;--mag:#8e3ba8}\n"
		  "@media (prefers-color-scheme:dark){:root{--bg:#171716;--fg:#e8e7e3;--mut:#9a9993;"
		  "--card:#20201e;--line:#34332f;--err:#ff7b72;--warn:#e3b341;--note:#79b8ff;--ok:#56d364;--mag:#d2a8ff}}\n"
		  "body{margin:0;background:var(--bg);color:var(--fg);font:15px/1.5 system-ui,sans-serif}\n"
		  "main{max-width:72rem;margin:0 auto;padding:24px 16px}\n"
		  "h1{font-size:1.25rem;margin:0 0 4px;word-break:break-all}\n"
		  ".sum{color:var(--mut);margin:0 0 20px}\n"
		  "section{background:var(--card);border:1px solid var(--line);border-left:4px solid var(--note);"
		  "border-radius:6px;margin:0 0 14px;overflow-x:auto}\n"
		  "section.error{border-left-color:var(--err)}section.warning{border-left-color:var(--warn)}\n"
		  "a{color:inherit}\n"
		  "pre{margin:0;padding:12px 14px;font:13px/1.45 ui-monospace,SFMono-Regular,Menlo,monospace}\n"
		  ".b{font-weight:700}.c31{color:var(--err)}.c32{color:var(--ok)}.c33{color:var(--warn)}"
		  ".c34{color:var(--note)}.c35{color:var(--mag)}.c36{color:var(--note)}\n"
		  "</style></head><body><main>\n<h1>");
	html_text(b, main, strlen(main));
	vbuf_f(b, "</h1>\n<p class=\"sum\">%u error%s, %u warning%s</p>\n", ne, ne == 1 ? "" : "s", nw,
	       nw == 1 ? "" : "s");
	for (vdiag *d = p->dhead; d; d = d->next) {
		vbuf r = { 0 };

		if (d->dup)
			continue;
		diag_render_color(p, &r, d);
		vbuf_f(b, "<section class=\"%s\"><pre>", d->level >= VDL_ERROR ? "error" : d->level == VDL_WARNING ? "warning" : "note");
		html_text(b, r.p ? r.p : "", r.n);
		vbuf_s(b, "</pre></section>\n");
		free(r.p);
	}
	if (!p->dhead)
		vbuf_s(b, "<p>No diagnostics.</p>\n");
	vbuf_s(b, "</main></body></html>\n");
}

static void write_file(pp *p, const char *f, const vbuf *b)
{
	int fd = p->o->errfd ? p->o->errfd : 2, out;
	char path[4096];

	if (f[0] != '/' && p->o->cwd)
		snprintf(path, sizeof(path), "%s/%s", p->o->cwd, f), f = path;
	if ((out = open(f, O_WRONLY | O_CREAT | O_TRUNC, 0644)) >= 0) {
		(void)!write(out, b->p, b->n);
		close(out);
	} else {
		dprintf(fd, "vcc: error: cannot write '%s': %s\n", f, strerror(errno));
	}
}

void pp_diag_finish(pp *p)
{
	int fd = p->o->errfd ? p->o->errfd : 2;

	if (p->dfinished)
		return;
	p->dfinished = true;
	diag_text_flush(p);
	diag_json_flush(p);
	if (unlikely(p->o->diag_patch || p->o->fix_apply))
		diag_fixits_out(p);
	if (unlikely(p->o->dsink && p->o->dsink->record && p->dhead))
		dsink_records(p);
	if (p->o->diag_format == PP_DIAG_SARIF || p->o->sarif_file) {
		vbuf b = { 0 };

		sarif_log(p, &b);
		if (p->o->diag_format == PP_DIAG_SARIF)
			(void)!write(fd, b.p, b.n);
		if (p->o->sarif_file)
			write_file(p, p->o->sarif_file, &b);
		free(b.p);
	}
	if (unlikely(p->o->html_file != NULL)) {
		vbuf b = { 0 };

		html_page(p, &b);
		write_file(p, p->o->html_file, &b);
		free(b.p);
	}
}

/* ---------------- fix-its applied: -fdiagnostics-generate-patch, --fix ---------------- */

typedef struct fedit { const vlex_file *f; uint32_t b, e, seq; const char *s; } fedit;

typedef struct fedits { fedit *v; uint32_t n, cap; } fedits;

static void fix_add(const pp *p, fedits *x, const vdiag *d, bool machine)
{
	for (uint32_t i = 0; i < d->nfix; i++) {
		const vdfix *k = &d->fix[i];
		const fent *f;

		if ((machine && k->app != VDA_MACHINE) || !loc_ok(p, k->b, &f) || f->sys || k->e < k->b ||
		    k->e - f->base > f->f->size || !f->f->path)
			continue;
		if (x->n == x->cap)
			x->v = realloc(x->v, (x->cap = x->cap ? x->cap * 2 : 64) * sizeof(*x->v));
		x->v[x->n] = (fedit){ f->f, k->b - f->base, k->e - f->base, x->n, k->s };
		x->n++;
	}
}

static int fedit_cmp(const void *a, const void *b)
{
	const fedit *x = a, *y = b;

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
static void fix_collect(const pp *p, fedits *x, bool machine)
{
	uint32_t k = 0;

	for (const vdiag *d = p->dhead; d; d = d->next) {
		fix_add(p, x, d, machine);
		for (const vdiag *c = d->kids; c; c = c->next)
			if (machine) {
				fix_add(p, x, c, true);
			} else if (!d->nfix && c->nfix) {
				fix_add(p, x, c, false);
				break;
			}
	}
	if (x->n)
		qsort(x->v, x->n, sizeof(*x->v), fedit_cmp);
	for (uint32_t i = 0; i < x->n; i++) {
		const fedit *a = &x->v[i], *l = k ? &x->v[k - 1] : NULL;

		if (l && l->f == a->f && (a->b < l->e || (a->b == l->b && a->e == l->e && (a->b != a->e || !strcmp(a->s, l->s)))))
			continue;
		x->v[k++] = *a;
	}
	x->n = k;
}

/* src[lo, hi) with the edits v[0, n) (inside it) applied */
static void fix_apply(vbuf *o, const char *src, uint32_t lo, uint32_t hi, const fedit *v, uint32_t n)
{
	uint32_t at = lo;

	for (uint32_t i = 0; i < n; i++) {
		vbuf_put(o, src + at, v[i].b - at);
		vbuf_s(o, v[i].s);
		at = v[i].e;
	}
	vbuf_put(o, src + at, hi - at);
}

static uint32_t line_of(const char *s, uint32_t off)	/* 1-based */
{
	uint32_t n = 1;

	for (uint32_t i = 0; i < off; i++)
		n += s[i] == '\n';
	return n;
}

static uint32_t line_start(const char *s, uint32_t size, uint32_t line)	/* offset of line (1-based), or size */
{
	uint32_t i = 0;

	for (uint32_t l = 1; l < line && i < size; i++)
		l += s[i] == '\n';
	return i;
}

/* the text's lines, each prefixed by c ('-', '+', ' ') */
static uint32_t put_lines(vbuf *b, char c, const char *s, uint32_t n)
{
	uint32_t k = 0;

	for (uint32_t i = 0; i < n;) {
		uint32_t j = i;

		while (j < n && s[j] != '\n')
			j++;
		vbuf_put(b, &c, 1);
		vbuf_put(b, s + i, j - i);
		vbuf_s(b, j < n ? "\n" : "\n\\ No newline at end of file\n");
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
static void fix_patch(vbuf *b, const vlex_file *f, const fedit *v, uint32_t n)
{
	const char *s = f->src;
	uint32_t total = nlines(s, f->size);
	int32_t delta = 0;

	vbuf_f(b, "--- %s\n+++ %s\n", f->path, f->path);
	for (uint32_t i = 0; i < n;) {
		uint32_t c1 = line_of(s, v[i].b), c2, prev = 0, no = 0, nn = 0;
		vbuf h = { 0 };

		c1 = c1 > 3 ? c1 - 3 : 1;
		for (;;) {	/* the blocks of one hunk */
			uint32_t j = i + 1, l1 = line_of(s, v[i].b), l2 = line_of(s, v[i].e), mb, me, k;
			vbuf mid = { 0 };

			while (j < n && line_of(s, v[j].b) <= l2 + 1) {
				uint32_t le = line_of(s, v[j].e);

				l2 = le > l2 ? le : l2, j++;
			}
			mb = line_start(s, f->size, prev ? prev + 1 : c1);	/* context since the last block */
			me = line_start(s, f->size, l1);
			no += k = put_lines(&h, ' ', s + mb, me - mb), nn += k;
			mb = me, me = line_start(s, f->size, l2 + 1);
			fix_apply(&mid, s, mb, me, v + i, j - i);
			no += put_lines(&h, '-', s + mb, me - mb);
			nn += put_lines(&h, '+', mid.p ? mid.p : "", (uint32_t)mid.n);
			free(mid.p);
			prev = l2, i = j;
			if (i >= n || line_of(s, v[i].b) > prev + 7)
				break;
		}
		c2 = prev + 3 < total ? prev + 3 : total;
		{
			uint32_t mb = line_start(s, f->size, prev + 1), me = line_start(s, f->size, c2 + 1), k;

			no += k = put_lines(&h, ' ', s + mb, me - mb), nn += k;
		}
		vbuf_f(b, "@@ -%u,%u +%u,%u @@\n", c1, no, (uint32_t)((int32_t)c1 + delta), nn);
		vbuf_put(b, h.p, h.n);
		delta += (int32_t)nn - (int32_t)no;
		free(h.p);
	}
}

/* --fix: each file's machine-applicable edits written back (a new file renamed over it) */
static void fix_write(pp *p, int fd, const vlex_file *f, const fedit *v, uint32_t n)
{
	char path[4096], tmp[4200];
	const char *fp = f->path;
	vbuf o = { 0 };
	struct stat st;
	int out;

	if (fp[0] != '/' && p->o->cwd)
		snprintf(path, sizeof(path), "%s/%s", p->o->cwd, fp), fp = path;
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

VPRIV void diag_fixits_out(pp *p)
{
	int fd = p->o->errfd ? p->o->errfd : 2;

	for (int machine = 0; machine < 2; machine++) {
		fedits x = { 0 };
		vbuf b = { 0 };

		if (!(machine ? p->o->fix_apply : p->o->diag_patch))
			continue;
		fix_collect(p, &x, machine);
		for (uint32_t i = 0; i < x.n;) {
			uint32_t j = i;

			while (j < x.n && x.v[j].f == x.v[i].f)
				j++;
			if (machine)
				fix_write(p, fd, x.v[i].f, x.v + i, j - i);
			else
				fix_patch(&b, x.v[i].f, x.v + i, j - i);
			i = j;
		}
		if (b.n)
			(void)!write(fd, b.p, b.n);
		free(b.p), free(x.v);
	}
}
