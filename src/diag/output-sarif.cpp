/* SARIF 2.1 output. */
#include "../frontend/pp/pp-internal.h"
#include "common/buffer.h"
#include <ctype.h>
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

static void diag_sarif_uri_path_append(vbuffer *b, const char *s, size_t n)
{
	static const char hex[] = "0123456789ABCDEF";

	for (size_t i = 0; i < n; i++) {
		unsigned char c = (unsigned char)s[i];

		if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || strchr("-._~/", c))
			vbuffer_append(b, &s[i], 1);
		else {
			char e[3] = { '%', hex[c >> 4], hex[c & 15] };

			vbuffer_append(b, e, 3);
		}
	}
}

/* {"uri": ...} (absolute: file://; relative: against %SRCROOT%, the working directory) */
static void diag_sarif_artifact_append(vbuffer *b, const char *name, size_t nl)
{
	vbuffer u = { 0 };

	if (nl && name[0] == '/')
		vbuffer_string_append(&u, "file://");
	diag_sarif_uri_path_append(&u, name, nl);
	vbuffer_string_append(b, "{\"uri\":");
	diag_json_string_append(b, u.p, u.n);
	if (!(nl && name[0] == '/'))
		vbuffer_string_append(b, ",\"uriBaseId\":\"%SRCROOT%\"");
	vbuffer_string_append(b, "}");
	free(u.p);
}

/* the presumed line and the code point column (1-based) of loc */
static bool diag_sarif_position_find(pp *p, uint32_t loc, const char **name, uint32_t *nl, uint32_t *line, uint32_t *ucol)
{
	const pp_file_entry *e;
	uint32_t col, off, ls;

	if (loc == UINT32_MAX || !pp_location_is_valid(p, loc, &e) || !diag_location_presumed(p, loc, name, nl, line, &col))
		return false;
	off = loc - e->location_base;
	ls = off - (col - 1);
	*ucol = 1;
	for (uint32_t i = ls; i < off; i++)
		if (((unsigned char)e->lex_file->src[i] & 0xc0) != 0x80)
			(*ucol)++;
	return true;
}

/* "physicalLocation" of [b, e) (e == b: the token at b) */
static bool diag_sarif_physical_location_append(pp *p, vbuffer *b, uint32_t lb, uint32_t le)
{
	const char *name, *n2;
	uint32_t nl, l2n, line, col, l2, c2;

	if (!diag_sarif_position_find(p, lb, &name, &nl, &line, &col))
		return false;
	if (le <= lb)
		le = pp_token_end_find(p, lb);
	if (!diag_sarif_position_find(p, le, &n2, &l2n, &l2, &c2) || l2n != nl || memcmp(n2, name, nl) || l2 < line)
		l2 = line, c2 = col + 1;
	vbuffer_string_append(b, "\"physicalLocation\":{\"artifactLocation\":");
	diag_sarif_artifact_append(b, name, nl);
	vbuffer_format_append(b, ",\"region\":{\"startLine\":%u,\"startColumn\":%u,\"endLine\":%u,\"endColumn\":%u}}", line, col, l2,
	       c2 > col || l2 > line ? c2 : col + 1);
	return true;
}

static void diag_sarif_result_append(pp *p, vbuffer *b, const diag_record *d, const char *const *rules, uint32_t nrules)
{
	uint32_t rule = 0, nrel = 0, id = 1;
	const pp_file_entry *e;
	uint32_t pe = d->loc;

	while (rule < nrules && strcmp(rules[rule], d->id ? d->id : "vcc_unknown"))
		rule++;
	vbuffer_string_append(b, "{\"ruleId\":");
	diag_json_cstring_append(b, d->id ? d->id : "vcc_unknown");
	vbuffer_format_append(b, ",\"ruleIndex\":%u,\"level\":\"%s\",\"message\":{\"text\":", rule,
	       d->level >= DIAG_LEVEL_ERROR ? "error" : d->level == DIAG_LEVEL_WARNING ? "warning" : "note");
	diag_json_cstring_append(b, d->msg);
	vbuffer_string_append(b, "}");
	/* the primary location: the span that holds loc, else the token at loc */
	for (uint32_t i = 0; i < d->nspan; i++)
		if (d->span[i].b <= d->loc && d->loc < d->span[i].e)
			pe = d->span[i].e;
	if (d->loc != UINT32_MAX && pp_location_is_valid(p, d->loc, &e)) {
		vbuffer m = { 0 };
		int nchain = 0;

		if (!diag_sarif_physical_location_append(p, &m, d->loc, pe))
			goto noloc;
		vbuffer_string_append(b, ",\"locations\":[{\"id\":0,");
		vbuffer_append(b, m.p, m.n);
		free(m.p);
		if (d->label) {
			vbuffer_string_append(b, ",\"message\":{\"text\":");
			diag_json_cstring_append(b, d->label);
			vbuffer_string_append(b, "}");
		}
		for (const pp_file_entry *x = e; x && x->included_from && nchain < 64; x = pp_location_file_entry(p, x->included_from))
			nchain++;
		if (nchain) {
			vbuffer_string_append(b, ",\"relationships\":[{\"target\":1,\"kinds\":[\"isIncludedBy\"]}]");
		}
		vbuffer_string_append(b, "}]");
		/* related: the include chain (each included by the next), spans, notes */
		vbuffer_string_append(b, ",\"relatedLocations\":[");
		nchain = 0;
		for (const pp_file_entry *x = e; x && x->included_from && nchain < 64; x = pp_location_file_entry(p, x->included_from), nchain++) {
			const pp_file_entry *up = pp_location_file_entry(p, x->included_from);

			vbuffer_format_append(b, "%s{\"id\":%u,", nrel++ ? "," : "", id);
			diag_sarif_physical_location_append(p, b, x->included_from, x->included_from);
			vbuffer_string_append(b, ",\"message\":{\"text\":\"included from here\"}");
			if (up && up->included_from)
				vbuffer_format_append(b, ",\"relationships\":[{\"target\":%u,\"kinds\":[\"isIncludedBy\"]}]", id + 1);
			vbuffer_string_append(b, "}");
			id++;
		}
		for (uint32_t i = 0; i < d->nspan; i++) {
			vbuffer s = { 0 };

			if (d->span[i].b <= d->loc && d->loc < d->span[i].e && !d->span[i].label)
				continue;	/* the primary location's own range */
			if (!diag_sarif_physical_location_append(p, &s, d->span[i].b, d->span[i].e)) {
				free(s.p);
				continue;
			}
			vbuffer_format_append(b, "%s{\"id\":%u,", nrel++ ? "," : "", id++);
			vbuffer_append(b, s.p, s.n);
			free(s.p);
			if (d->span[i].label) {
				vbuffer_string_append(b, ",\"message\":{\"text\":");
				diag_json_cstring_append(b, d->span[i].label);
				vbuffer_string_append(b, "}");
			}
			vbuffer_string_append(b, "}");
		}
		for (const diag_record *k = d->kids; k; k = k->next) {
			vbuffer s = { 0 };

			vbuffer_format_append(b, "%s{\"id\":%u,", nrel++ ? "," : "", id++);
			if (diag_sarif_physical_location_append(p, &s, k->loc, k->loc)) {
				vbuffer_append(b, s.p, s.n);
				vbuffer_string_append(b, ",");
			}
			free(s.p);
			vbuffer_string_append(b, "\"message\":{\"text\":");
			diag_json_cstring_append(b, k->msg);
			vbuffer_string_append(b, "}}");
		}
		vbuffer_string_append(b, "]");
	}
noloc:
	if (d->nfix) {
		vbuffer_string_append(b, ",\"fixes\":[{\"artifactChanges\":[");
		for (uint32_t i = 0, n = 0; i < d->nfix; i++) {
			const char *name;
			uint32_t nl, line, col, l2, c2;

			if (!diag_sarif_position_find(p, d->fix[i].b, &name, &nl, &line, &col))
				continue;
			if (!diag_sarif_position_find(p, d->fix[i].e, &name, &nl, &l2, &c2))
				l2 = line, c2 = col;
			vbuffer_string_append(b, n++ ? ",{\"artifactLocation\":" : "{\"artifactLocation\":");
			diag_sarif_artifact_append(b, name, nl);
			vbuffer_format_append(b, ",\"replacements\":[{\"deletedRegion\":{\"startLine\":%u,\"startColumn\":%u,"
				  "\"endLine\":%u,\"endColumn\":%u},\"insertedContent\":{\"text\":", line, col, l2, c2);
			diag_json_cstring_append(b, d->fix[i].s);
			vbuffer_string_append(b, "}}]}");
		}
		vbuffer_string_append(b, "]}]");
	}
	vbuffer_string_append(b, ",\"properties\":{\"vcc/help\":");
	diag_json_cstring_append(b, d->help);
	if (d->art) {
		vbuffer_string_append(b, ",\"vcc/art\":");
		diag_json_cstring_append(b, d->art);
	}
	vbuffer_string_append(b, ",\"vcc/flag\":");
	diag_json_cstring_append(b, d->flag);
	vbuffer_format_append(b, ",\"vcc/werror\":%s}}", d->werror ? "true" : "false");
}

VPRIV void diag_sarif_log_write(pp *p, vbuffer *b)
{
	const char *rules[512];
	uint32_t nr = 0;

	for (const diag_record *d = p->diagnostic_head; d; d = d->next) {
		const char *id = d->id ? d->id : "vcc_unknown";
		uint32_t i = 0;

		while (i < nr && strcmp(rules[i], id))
			i++;
		if (i == nr && nr < 512)
			rules[nr++] = id;
	}
	vbuffer_string_append(b, "{\"$schema\":\"https://json.schemastore.org/sarif-2.1.0.json\",\"version\":\"2.1.0\",\"runs\":[{"
		  "\"tool\":{\"driver\":{\"name\":\"vcc\",\"rules\":[");
	for (uint32_t i = 0; i < nr; i++) {
		vbuffer_string_append(b, i ? ",{\"id\":" : "{\"id\":");
		diag_json_cstring_append(b, rules[i]);
		vbuffer_string_append(b, "}");
	}
	vbuffer_string_append(b, "]}},\"columnKind\":\"unicodeCodePoints\",\"originalUriBaseIds\":{\"%SRCROOT%\":{\"uri\":");
	{
		vbuffer u = { 0 };
		const char *cwd = p->options->cwd ? p->options->cwd : ".";

		vbuffer_string_append(&u, "file://");
		diag_sarif_uri_path_append(&u, cwd, strlen(cwd));
		if (!u.n || u.p[u.n - 1] != '/')
			vbuffer_string_append(&u, "/");
		diag_json_string_append(b, u.p, u.n);
		free(u.p);
	}
	vbuffer_format_append(b, "}},\"invocations\":[{\"executionSuccessful\":%s,\"arguments\":[", p->error_count ? "false" : "true");
	for (int i = 0; i < p->options->argc; i++) {
		if (i)
			vbuffer_string_append(b, ",");
		diag_json_cstring_append(b, p->options->argv[i]);
	}
	vbuffer_string_append(b, "]}],\"results\":[");
	for (const diag_record *d = p->diagnostic_head; d; d = d->next) {
		diag_sarif_result_append(p, b, d, rules, nr);
		if (d->next)
			vbuffer_string_append(b, ",");
	}
	vbuffer_string_append(b, "]}]}\n");
}

/* vccd: every top-level record, as JSON, to the build's store */
