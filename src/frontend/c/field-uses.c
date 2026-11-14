/* vcc front end: the VCC_FIELD_USES test aid.  The parser calls the hooks below at every
   place it resolves a name to a struct or union field (declaration, `.`, `->`, designator,
   offsetof); at the end of the TU the uses of the selected fields are printed, one line
   each, with the bytes a rename has to edit (doc/quality/DESIGN-field-uses.md). */
#include "cc-internal.h"
#include <errno.h>

typedef struct fu_sel {
	char *owner, *field, *neu;
	uint32_t fid;
	bool star;
} fu_sel;

typedef struct fu_use {
	ptok t;
	uint32_t tag, idx;
	uint8_t role;
} fu_use;

struct cc_field_uses {
	fu_sel *sel;
	uint32_t nsel;
	uint32_t *fids;		/* the selected field-name ids, sorted */
	uint32_t nfids;
	bool any_star;
	ptok pend;		/* the name token of the member access being parsed */
	ptok *names;		/* declarator names seen inside struct bodies */
	size_t nnames, capnames;
	fu_use *use;
	size_t nuse, capuse;
	struct { uint32_t tag, loc; } *defs;	/* where each record's '{' is */
	size_t ndefs, capdefs;
	uint32_t *defloc;	/* at the end: by tag */
	uint32_t *cont_tag, *cont_fld;	/* at the end: the record an untagged record is a member of */
	char *out;
	size_t nout, capout;
};

typedef struct cc_field_uses fu;

#define FU_NONE UINT32_MAX

/* room for element n (doubles *cap) */
static void *fu_grow(void *p, size_t n, size_t *cap, size_t sz)
{
	while (n >= *cap) {
		*cap = *cap ? *cap * 2 : 256;
		p = realloc(p, *cap * sz);
		if (!p) {
			fputs("vcc: out of memory\n", stderr);
			abort();
		}
	}
	return p;
}

static __attribute__((format(printf, 2, 3))) void fu_printf(fu *u, const char *f, ...)
{
	va_list ap;
	int n;

	for (;;) {
		va_start(ap, f);
		n = vsnprintf(u->out + u->nout, u->capout - u->nout, f, ap);
		va_end(ap);
		if (n >= 0 && u->nout + (size_t)n < u->capout) {
			u->nout += (size_t)n;
			return;
		}
		u->out = fu_grow(u->out, u->capout, &u->capout, 1);
	}
}

static int fu_cmp_u32(const void *a, const void *b)
{
	uint32_t x = *(const uint32_t *)a, y = *(const uint32_t *)b;

	return x < y ? -1 : x > y;
}

CC_PRIVATE void cc_field_uses_begin(cc *c, const char *path)
{
	FILE *f = fopen(path, "r");
	char *line = NULL;
	size_t cap = 0;
	fu *u = calloc(1, sizeof *u);
	size_t scap = 0;

	c->field_uses = u;
	pp_macro_text_keep(c->p);
	if (!f) {
		fprintf(stderr, "vcc: error: VCC_FIELD_USES: cannot read %s: %s\n", path, strerror(errno));
		c->errors++;
		return;
	}
	while (getline(&line, &cap, f) > 0) {
		char *s = line, *e, *spec, *neu = NULL, *dot;
		fu_sel *x;

		if ((e = strchr(s, '#')))
			*e = 0;
		while (*s == ' ' || *s == '\t')
			s++;
		spec = s;
		while (*s && *s != ' ' && *s != '\t' && *s != '\n')
			s++;
		if (spec == s)
			continue;
		if (*s) {
			*s++ = 0;
			while (*s == ' ' || *s == '\t')
				s++;
			neu = s;
			while (*s && *s != ' ' && *s != '\t' && *s != '\n')
				s++;
			*s = 0;
			if (neu == s)
				neu = NULL;
		}
		dot = strrchr(spec, '.');
		if (!dot || dot == spec || !dot[1]) {
			fprintf(stderr, "vcc: error: VCC_FIELD_USES: bad line '%s' (want OWNER.FIELD [NEW])\n", spec);
			c->errors++;
			continue;
		}
		u->sel = fu_grow(u->sel, u->nsel, &scap, sizeof *u->sel);
		x = &u->sel[u->nsel++];
		x->owner = strndup(spec, (size_t)(dot - spec));
		x->field = strdup(dot + 1);
		x->neu = neu ? strdup(neu) : NULL;
		x->star = !strcmp(x->field, "*");
		if (x->star)
			u->any_star = true;
		else
			x->fid = vintern_cstr(x->field);
	}
	free(line);
	fclose(f);
	u->fids = malloc((u->nsel + 1) * sizeof(uint32_t));
	for (uint32_t i = 0; i < u->nsel; i++)
		if (!u->sel[i].star)
			u->fids[u->nfids++] = u->sel[i].fid;
	qsort(u->fids, u->nfids, sizeof(uint32_t), fu_cmp_u32);
}

static bool fu_wanted(const fu *u, uint32_t id)
{
	return u->any_star || bsearch(&id, u->fids, u->nfids, sizeof(uint32_t), fu_cmp_u32);
}

/* a record's body starts: tok(c) is its '{' */
CC_PRIVATE void cc_field_uses_def(cc *c, uint32_t ti)
{
	fu *u = c->field_uses;

	u->defs = fu_grow(u->defs, u->ndefs, &u->capdefs, sizeof *u->defs);
	u->defs[u->ndefs].tag = ti;
	u->defs[u->ndefs++].loc = tok(c)->loc;
}

CC_PRIVATE void cc_field_uses_name_keep(cc *c)
{
	fu *u = c->field_uses;

	u->pend = *tok(c);
	if (c->in_struct) {
		u->names = fu_grow(u->names, u->nnames, &u->capnames, sizeof *u->names);
		u->names[u->nnames++] = *tok(c);
	}
}

static void fu_add(cc *c, int role, uint32_t tag, uint32_t idx, const ptok *t)
{
	fu *u = c->field_uses;

	u->use = fu_grow(u->use, u->nuse, &u->capuse, sizeof *u->use);
	u->use[u->nuse++] = (fu_use){ .t = *t, .tag = tag, .idx = idx, .role = (uint8_t)role };
}

/* a member declaration: the declarator's name token is the one kept at loc */
CC_PRIVATE void cc_field_uses_decl(cc *c, uint32_t tag, uint32_t idx, uint32_t name, uint32_t loc)
{
	fu *u = c->field_uses;

	if (!fu_wanted(u, name))
		return;
	for (uint32_t i = u->nnames; i-- > 0;)
		if (u->names[i].loc == loc && u->names[i].id == name) {
			fu_add(c, FU_DECL, tag, idx, &u->names[i]);
			return;
		}
}

/* n: the node member() returned for the name kept by cc_field_uses_name_keep */
CC_PRIVATE void cc_field_uses_member(cc *c, uint32_t n, uint32_t id)
{
	fu *u = c->field_uses;

	if (AST_NODE(n)->op != AST_NODE_MEMBER || !fu_wanted(u, id))
		return;
	if (AST_NODE(n)->b < c->ntag && AST_NODE(n)->c < c->tags[AST_NODE(n)->b].nfield && c->tags[AST_NODE(n)->b].f[AST_NODE(n)->c].name == id)
		fu_add(c, FU_MEMBER, AST_NODE(n)->b, AST_NODE(n)->c, &u->pend);
}

/* cc_member_find's result for the name at tok(c): the last step of the path is the named field */
CC_PRIVATE void cc_field_uses_path_record(cc *c, int role, uint32_t tag, const uint32_t *path, int depth)
{
	fu *u = c->field_uses;
	uint32_t ti = tag;

	for (int i = 0; i + 1 < depth; i++)
		ti = AST_TYPE(c, c->tags[ti].f[path[i]].ty)->aux;
	if (fu_wanted(u, tok(c)->id))
		fu_add(c, role, ti, path[depth - 1], tok(c));
}

/* ---- the end of the TU ---- */

static bool fu_complete(const cc *c, uint32_t ti)
{
	return ti < c->ntag && (c->tags[ti].flags & AST_RECORD_FLAG_COMPLETE) && !(c->tags[ti].flags & AST_RECORD_FLAG_ENUM);
}

static void fu_where(cc *c, uint32_t loc, char *b, size_t n, bool with_col)
{
	const char *name;
	uint32_t nl, line, col;

	pp_location_describe(c->p, loc, &name, &nl, &line, &col);
	if (with_col)
		snprintf(b, n, "%.*s:%u:%u", (int)nl, name, line, col);
	else
		snprintf(b, n, "%.*s:%u", (int)nl, name, line);
}

static void fu_owner(cc *c, uint32_t ti, char *b, size_t n, int depth)
{
	fu *u = c->field_uses;
	const tag *r = &c->tags[ti];

	if (r->name)
		snprintf(b, n, "%.*s", CC_IDENTIFIER(r->name));
	else if (r->typedef_name)
		snprintf(b, n, "%.*s", CC_IDENTIFIER(r->typedef_name));
	else if (u->cont_tag[ti] != FU_NONE && depth < 64) {
		uint32_t ct = u->cont_tag[ti];
		const field *f = &c->tags[ct].f[u->cont_fld[ti]];

		fu_owner(c, ct, b, n, depth + 1);
		if (f->name) {
			size_t l = strlen(b);

			snprintf(b + l, n - l, ".%.*s", CC_IDENTIFIER(f->name));
		}
	} else {
		char w[600];

		fu_where(c, u->defloc[ti] ? u->defloc[ti] : r->loc, w, sizeof w, true);
		snprintf(b, n, "(anonymous at %s)", w);
	}
}

/* the names cc_member_find finds from a record: its fields and its anonymous members' */
static void fu_names(cc *c, uint32_t ti, char *b, size_t n, int depth)
{
	const tag *r = &c->tags[ti];

	for (uint32_t i = 0; i < r->nfield; i++) {
		const field *f = &r->f[i];
		size_t l = strlen(b);

		if (f->name)
			snprintf(b + l, n - l, "%s%.*s", l ? "," : "", CC_IDENTIFIER(f->name));
		else if (!f->bitfield && cc_type_is_record(c, f->ty) && depth < 64)
			fu_names(c, AST_TYPE(c, f->ty)->aux, b, n, depth + 1);
	}
}

static void fu_line(cc *c, const char *kind, const fu_use *x, const char *owner, const char *def, const char *field)
{
	fu *u = c->field_uses;
	static const char *const roles[] = { "decl", "member", "designator", "offsetof" };
	static const char *const wheres[] = { "file", "macro", "none", "dirty" };
	const lex_file *f = NULL;
	const char *name;
	uint32_t off = 0, line = 1, col, b = 0, nl, el, ec;
	char ex[600];
	int rc = pp_token_spelling_find(c->p, &x->t, &f, &off);

	pp_location_describe(c->p, x->t.loc, &name, &nl, &el, &ec);
	snprintf(ex, sizeof ex, "%.*s:%u:%u", (int)nl, name, el, ec);
	if (rc == PP_SPELLING_FILE || rc == PP_SPELLING_MACRO) {
		for (uint32_t i = 0; i < off; i++)
			if (f->src[i] == '\n')
				line++, b = i + 1;
		col = off - b + 1;
		fu_printf(u, "%s\t%s\t%s\t%s\t%s\t%s\t%s\t%u\t%u\t%u\t%d\t%s\n", kind, roles[x->role], owner, field, def, wheres[rc],
			  f->path, off, line, col, cc_identifier_length(c->tags[x->tag].f[x->idx].name), rc == PP_SPELLING_FILE ? "-" : ex);
	} else {
		fu_printf(u, "%s\t%s\t%s\t%s\t%s\t%s\t%.*s\t-1\t%u\t%u\t%d\t%s\n", kind, roles[x->role], owner, field, def, wheres[rc],
			  (int)nl, name, el, ec, cc_identifier_length(c->tags[x->tag].f[x->idx].name), ex);
	}
}

CC_PRIVATE void cc_field_uses_finish(cc *c)
{
	fu *u = c->field_uses;
	uint32_t n = c->ntag;
	char main_name[600];

	u->defloc = calloc(n + 1, sizeof(uint32_t));
	u->cont_tag = malloc((n + 1) * sizeof(uint32_t));
	u->cont_fld = calloc(n + 1, sizeof(uint32_t));
	for (uint32_t i = 0; i <= n; i++)
		u->cont_tag[i] = FU_NONE;
	snprintf(main_name, sizeof main_name, "%s", c->o->dbg_name ? c->o->dbg_name : "?");
	if (c->errors || pp_error_count(c->p)) {
		fu_printf(u, "tu\t%s\terrors\n", main_name);
		goto out;
	}
	for (uint32_t i = 0; i < u->ndefs; i++)
		if (u->defs[i].tag < n && !u->defloc[u->defs[i].tag])
			u->defloc[u->defs[i].tag] = u->defs[i].loc;
	/* the container of an untagged record: the first record with a member of that type */
	for (uint32_t ti = 1; ti < n; ti++) {
		if (!fu_complete(c, ti))
			continue;
		for (uint32_t i = 0; i < c->tags[ti].nfield; i++) {
			ast_qualified_type t = c->tags[ti].f[i].ty;

			while (cc_type_is_array(c, t))
				t = AST_TYPE(c, t)->base;
			if (cc_type_is_record(c, t)) {
				uint32_t t2 = AST_TYPE(c, t)->aux;

				if (!c->tags[t2].name && !c->tags[t2].typedef_name && t2 != ti && u->cont_tag[t2] == FU_NONE)
					u->cont_tag[t2] = ti, u->cont_fld[t2] = i;
			}
		}
	}
	fu_printf(u, "tu\t%s\tok\n", main_name);
	for (uint32_t k = 0; k < u->nuse; k++) {
		const fu_use *x = &u->use[k];
		uint32_t name = c->tags[x->tag].f[x->idx].name;
		char owner[600], def[600];
		bool sel = false;

		fu_owner(c, x->tag, owner, sizeof owner, 0);
		fu_where(c, u->defloc[x->tag] ? u->defloc[x->tag] : c->tags[x->tag].loc, def, sizeof def, false);
		for (uint32_t i = 0; i < u->nsel; i++)
			if (!strcmp(u->sel[i].owner, owner) && (u->sel[i].star || u->sel[i].fid == name))
				sel = true;
		if (sel || bsearch(&name, u->fids, u->nfids, sizeof(uint32_t), fu_cmp_u32)) {
			char fld[256];

			snprintf(fld, sizeof fld, "%.*s", CC_IDENTIFIER(name));
			fu_line(c, sel ? "use" : "other", x, owner, def, fld);
		}
	}
	for (uint32_t ti = 1; ti < n; ti++) {
		char owner[600];
		bool sel = false;

		if (!fu_complete(c, ti))
			continue;
		if (!c->tags[ti].name && !c->tags[ti].typedef_name && u->cont_tag[ti] != FU_NONE &&
		    !c->tags[u->cont_tag[ti]].f[u->cont_fld[ti]].name)
			continue;	/* an anonymous member: its container's members include its names */
		fu_owner(c, ti, owner, sizeof owner, 0);
		for (uint32_t i = 0; i < u->nsel; i++)
			sel |= !strcmp(u->sel[i].owner, owner);
		if (sel) {
			char def[600], *nm = calloc(1, 1 << 16);

			fu_where(c, u->defloc[ti] ? u->defloc[ti] : c->tags[ti].loc, def, sizeof def, false);
			fu_names(c, ti, nm, 1 << 16, 0);
			fu_printf(u, "members\t%s\t%s\t%s\n", owner, def, nm);
			free(nm);
		}
	}
	for (uint32_t i = 0; i < u->nsel; i++) {
		const char *names[2] = { u->sel[i].star ? NULL : u->sel[i].field, u->sel[i].neu };

		for (int k = 0; k < 2; k++) {
			bool dup = false;

			if (!names[k])
				continue;
			for (uint32_t j = 0; j < i; j++)
				dup |= (!u->sel[j].star && !strcmp(u->sel[j].field, names[k])) || (u->sel[j].neu && !strcmp(u->sel[j].neu, names[k]));
			if (!dup && pp_macro_id_is_defined(c->p, vintern_cstr(names[k])))
				fu_printf(u, "macro\t%s\n", names[k]);
		}
	}
out:
	{
		int fd = c->o->outfd ? c->o->outfd : 1;
		size_t done = 0;

		while (done < u->nout) {
			ssize_t w = write(fd, u->out + done, u->nout - done);

			if (w <= 0)
				break;
			done += (size_t)w;
		}
	}
	for (uint32_t i = 0; i < u->nsel; i++)
		free(u->sel[i].owner), free(u->sel[i].field), free(u->sel[i].neu);
	free(u->sel), free(u->fids), free(u->names), free(u->use), free(u->defs);
	free(u->defloc), free(u->cont_tag), free(u->cont_fld), free(u->out);
	free(u);
	c->field_uses = NULL;
}
