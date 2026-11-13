/* The standalone HTML page of a TU's diagnostics. */
#include "../frontend/pp/pp-internal.h"
#include "common/buffer.h"
#include <ctype.h>
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

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
VPRIV void html_page(pp *p, vbuf *b)
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
