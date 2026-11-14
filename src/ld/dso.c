/*
 * Shared objects as inputs (x86-64 dynamic executables): part of ld/ld.c
 * (#included there, before symbol resolution).
 *
 *   - an ELF ET_DYN input is read for its .dynsym (defined globals, their
 *     version from .gnu.version and .gnu.version_d) and DT_SONAME; it adds no
 *     sections;
 *   - a global no object defines is looked up in the shared objects in
 *     command-line order (the first definition wins, a hidden version
 *     never does): it stays GS_UNDEF with dso1/dsym naming that definition,
 *     and becomes an imported symbol (PLT, GOT or a copy; dyn.inc);
 *   - a text input is a GNU ld script: GROUP, INPUT and AS_NEEDED name files
 *     (absolute paths or -lNAME), as glibc's libc.so does.
 */

#include "ld-internal.h"

#define SHT_GNU_VERDEF_ 0x6ffffffdu
#define SHT_GNU_VERSYM_ 0x6fffffffu
#define SHT_DYNSYM_ 11u
#define SHT_DYNAMIC_ 6u

static void ld_dso_tab_put(struct ld_context *ctx, const char *name, uint32_t d, uint32_t idx)
{
	uint32_t len = strlen(name), h = ld_name_hash(name, len);

	if (!ctx->dso.symbols.v || (ctx->dso.symbols.n + 1) * 2 > ctx->dso.symbols.mask) {
		uint32_t m = ctx->dso.symbols.v ? (ctx->dso.symbols.mask + 1) * 2 : 4096;
		DsoSym *nv = ld_region_alloc((size_t)m * sizeof(DsoSym));
		memset(nv, 0, (size_t)m * sizeof(DsoSym));

		for (uint32_t k = 0; ctx->dso.symbols.v && k <= ctx->dso.symbols.mask; k++)
			if (ctx->dso.symbols.v[k].name) {
				uint32_t j = ctx->dso.symbols.v[k].h & (m - 1);

				while (nv[j].name)
					j = (j + 1) & (m - 1);
				nv[j] = ctx->dso.symbols.v[k];
			}
		ctx->dso.symbols.v = nv;
		ctx->dso.symbols.mask = m - 1;
	}
	uint32_t j = h & ctx->dso.symbols.mask;

	for (; ctx->dso.symbols.v[j].name; j = (j + 1) & ctx->dso.symbols.mask)
		if (ctx->dso.symbols.v[j].h == h && ctx->dso.symbols.v[j].len == len && !memcmp(ctx->dso.symbols.v[j].name, name, len))
			return;		/* an earlier object's definition wins */
	ctx->dso.symbols.n++;
	ctx->dso.symbols.v[j] = (DsoSym){ .name = name, .len = len, .h = h, .dso = d, .idx = idx };
}

VPRIV const DsoSym *ld_dso_lookup(struct ld_context *ctx, const char *name)
{
	if (!ctx->dso.symbols.v)
		return NULL;
	uint32_t len = strlen(name), h = ld_name_hash(name, len), j = h & ctx->dso.symbols.mask;

	for (; ctx->dso.symbols.v[j].name; j = (j + 1) & ctx->dso.symbols.mask)
		if (ctx->dso.symbols.v[j].h == h && ctx->dso.symbols.v[j].len == len && !memcmp(ctx->dso.symbols.v[j].name, name, len))
			return &ctx->dso.symbols.v[j];
	return NULL;
}

/* a defined, exported .dynsym entry of its default version */
VPRIV bool ld_dso_sym_def(const Dso *d, uint32_t k)
{
	const elf_symbol *y = &d->dsym[k];
	uint8_t bind = y->st_info >> 4;

	if (y->st_shndx == SHN_UNDEF || (bind != STB_GLOBAL && bind != STB_WEAK && bind != 10 /* GNU_UNIQUE */))
		return false;
	if ((y->st_other & 3) == STV_HIDDEN || (y->st_other & 3) == STV_INTERNAL)
		return false;
	if (d->versym && (d->versym[k] & 0x8000))
		return false;	/* a non-default version (foo@V): only a versioned reference binds it */
	return true;
}

VPRIV const char *ld_dso_vername(const Dso *d, uint32_t k)
{
	if (!d->versym)
		return NULL;
	uint16_t v = d->versym[k] & 0x7fff;

	return v >= 2 && v < d->nver ? d->vername[v] : NULL;
}

/* read an ET_DYN input; false (with an error) when it is not a usable one */
static bool ld_dso_read(struct ld_context *ctx, Dso *d)
{
	const uint8_t *b = d->buf;
	uint64_t n = d->size;
	const elf_file_header *eh = (const elf_file_header *)b;
#define DBAD(...) do { ld_error_report(ctx, __VA_ARGS__); return false; } while (0)

	if (eh->e_machine != 62)
		DBAD("%s: shared objects are supported for x86-64 only", d->path);
	if (eh->e_shoff == 0 || eh->e_shentsize != sizeof(elf_section_header) || eh->e_shoff % 8 || !vchecked_range(eh->e_shoff, (uint64_t)eh->e_shnum * sizeof(elf_section_header), n))
		DBAD("%s: invalid section header table", d->path);
	const elf_section_header *sh = (const elf_section_header *)(b + eh->e_shoff);
	uint32_t nsh = eh->e_shnum;
	const elf_section_header *ds = NULL, *vs = NULL, *vd = NULL, *dy = NULL;

	for (uint32_t s = 0; s < nsh; s++) {
		if (sh[s].sh_type != SHT_NOBITS && sh[s].sh_type != SHT_NULL && !vchecked_range(sh[s].sh_offset, sh[s].sh_size, n))
			DBAD("%s: section %u goes past the end of the file", d->path, s);
		if (sh[s].sh_type == SHT_DYNSYM_)
			ds = &sh[s];
		else if (sh[s].sh_type == SHT_GNU_VERSYM_)
			vs = &sh[s];
		else if (sh[s].sh_type == SHT_GNU_VERDEF_)
			vd = &sh[s];
		else if (sh[s].sh_type == SHT_DYNAMIC_)
			dy = &sh[s];
	}
	if (!ds)
		return true;	/* no symbols: only DT_NEEDED (if any symbol were used) */
	if (ds->sh_offset % 8 || ds->sh_size % sizeof(elf_symbol) || ds->sh_link >= nsh || sh[ds->sh_link].sh_type != SHT_STRTAB)
		DBAD("%s: invalid .dynsym", d->path);
	d->dsym = (const elf_symbol *)(b + ds->sh_offset);
	d->ndsym = ds->sh_size / sizeof(elf_symbol);
	d->dstr = (const char *)b + sh[ds->sh_link].sh_offset;
	d->dstrsz = sh[ds->sh_link].sh_size;
	if (!d->dstrsz || d->dstr[d->dstrsz - 1])
		DBAD("%s: invalid .dynstr", d->path);
	for (uint32_t k = 0; k < d->ndsym; k++)
		if (d->dsym[k].st_name >= d->dstrsz)
			DBAD("%s: invalid symbol name offset", d->path);
	if (vs) {
		if (vs->sh_offset % 2 || vs->sh_size != (uint64_t)d->ndsym * 2)
			DBAD("%s: invalid .gnu.version", d->path);
		d->versym = (const uint16_t *)(b + vs->sh_offset);
	}
	if (vd) {
		/* Verdef: version, flags, ndx, cnt (2 each), hash, aux, next (4 each); Verdaux: name, next */
		uint64_t off = 0;
		uint32_t maxndx = 1;

		for (uint32_t pass = 0; pass < 2; pass++) {
			off = 0;
			for (uint32_t c = 0; c < vd->sh_info && c < 0x8000; c++) {
				if (!vchecked_range(off, 20, vd->sh_size))
					DBAD("%s: invalid .gnu.version_d", d->path);
				const uint8_t *e = b + vd->sh_offset + off;
				uint16_t ndx;
				uint32_t aux = ld_u32_load(e + 12), next = ld_u32_load(e + 16);

				memcpy(&ndx, e + 4, 2);
				ndx &= 0x7fff;
				if (pass == 0) {
					if (ndx > maxndx)
						maxndx = ndx;
				} else {
					if (!vchecked_range(off + aux, 8, vd->sh_size))
						DBAD("%s: invalid .gnu.version_d", d->path);
					uint32_t nm = ld_u32_load(e + aux);

					if (nm >= d->dstrsz)
						DBAD("%s: invalid version name", d->path);
					d->vername[ndx] = d->dstr + nm;
				}
				if (!next)
					break;
				off += next;
			}
			if (pass == 0) {
				d->nver = maxndx + 1;
				d->vername = ld_region_alloc(d->nver * sizeof(char *));
				memset(d->vername, 0, d->nver * sizeof(char *));
			}
		}
	}
	if (dy && dy->sh_size % 16 == 0 && dy->sh_offset % 8 == 0)
		for (uint64_t k = 0; k < dy->sh_size / 16; k++) {
			const uint8_t *e = b + dy->sh_offset + 16 * k;
			uint64_t tag, val;

			memcpy(&tag, e, 8);
			memcpy(&val, e + 8, 8);
			if (tag == 0)
				break;
			if (tag == 14 /* DT_SONAME */ && val < d->dstrsz)
				d->soname = d->dstr + val;
		}
	return true;
#undef DBAD
}

VPRIV void ld_dso_add(struct ld_context *ctx, const char *path, const uint8_t *buf, size_t size, bool as_needed)
{
	Dso *d = ld_region_alloc(sizeof *d);

	memset(d, 0, sizeof *d);
	d->path = path;
	d->buf = buf;
	d->size = size;
	d->as_needed = as_needed;
	if (((uintptr_t)buf & 7)) {
		uint8_t *c = ld_region_alloc(size);

		memcpy(c, buf, size);
		d->buf = c;
	}
	if (!ld_dso_read(ctx, d))
		return;
	if (!d->soname) {
		const char *sl = strrchr(path, '/');

		d->soname = sl ? sl + 1 : path;
	}
	uint32_t di = ctx->dso.dsos.n;

	VVEC_PUSH(ctx->dso.dsos, d, ld_region_alloc);
	for (uint32_t k = 1; k < d->ndsym; k++)
		if (ld_dso_sym_def(d, k))
			ld_dso_tab_put(ctx, d->dstr + d->dsym[k].st_name, di, k);
}

/* ---- GNU ld scripts as inputs (libc.so: GROUP ( libc.so.6 libc_nonshared.a AS_NEEDED ( ld.so ) )) ---- */





static bool ld_is_input_script(const uint8_t *b, size_t n)
{
	if (n >= 4 && !memcmp(b, "\177ELF", 4))
		return false;
	if (n >= 8 && (!memcmp(b, "!<arch>\n", 8) || !memcmp(b, "!<thin>\n", 8)))
		return false;
	for (size_t i = 0; i < n && i < 256; i++)
		if (!b[i] || (b[i] < 32 && !isspace(b[i])))
			return false;
	return true;
}

VPRIV void ld_input_expand(struct ld_context *ctx, ld_input_vector *outv, ld_input_argument in, int depth);

static void ld_expand_script(struct ld_context *ctx, ld_input_vector *outv, const char *path, const char *t, size_t n, bool whole, bool asn, int depth)
{
	const char *p = t, *e = t + n;
	int as_needed_depth = -1, paren = 0;

	while (p < e) {
		if (isspace((unsigned char)*p) || *p == ',') {
			p++;
			continue;
		}
		if (p + 1 < e && p[0] == '/' && p[1] == '*') {
			const char *c = p + 2;

			while (c + 1 < e && !(c[0] == '*' && c[1] == '/'))
				c++;
			p = c + 2 <= e ? c + 2 : e;
			continue;
		}
		if (*p == '(') {
			paren++;
			p++;
			continue;
		}
		if (*p == ')') {
			if (paren == as_needed_depth)
				as_needed_depth = -1;
			paren--;
			p++;
			continue;
		}
		const char *w = p;

		while (p < e && !isspace((unsigned char)*p) && *p != '(' && *p != ')' && *p != ',')
			p++;
		char *word = ld_script_text_duplicate(w, p);

		if (!strcmp(word, "GROUP") || !strcmp(word, "INPUT"))
			continue;
		if (!strcmp(word, "AS_NEEDED")) {
			as_needed_depth = paren + 1;
			continue;
		}
		if (!strcmp(word, "OUTPUT_FORMAT") || !strcmp(word, "OUTPUT_ARCH") || !strcmp(word, "SEARCH_DIR")) {
			/* skip the parenthesised argument */
			while (p < e && *p != '(')
				p++;
			int dpt = 0;

			for (; p < e; p++) {
				if (*p == '(')
					dpt++;
				else if (*p == ')' && --dpt == 0) {
					p++;
					break;
				}
			}
			continue;
		}
		if (!paren) {
			ld_error_report(ctx, "%s: unsupported command in an input linker script: %s", path, word);
			return;
		}
		const char *file = word;

		if (word[0] != '/' && strncmp(word, "-l", 2) && faccessat(ctx->options.directory_fd, word, R_OK, 0)) {
			/* a relative name: the script's directory, then the library path (as lld) */
			const char *sl = strrchr(path, '/');
			char *c = ld_region_alloc(strlen(path) + strlen(word) + 2);

			sprintf(c, "%.*s/%s", sl ? (int)(sl - path) : 1, sl ? path : ".", word);
			file = c;
			for (uint32_t k = 0; faccessat(ctx->options.directory_fd, file, R_OK, 0) && k < ctx->options.library_directories.n; k++) {
				c = ld_region_alloc(strlen(ctx->options.library_directories.v[k]) + strlen(word) + 2);
				sprintf(c, "%s/%s", ctx->options.library_directories.v[k], word);
				file = c;
			}
		}
		if (!strncmp(word, "-l", 2)) {
			file = ld_library_find(ctx, word + 2);
			if (!file) {
				ld_error_report(ctx, "%s: unable to find library %s", path, word);
				continue;
			}
		}
		ld_input_expand(ctx, outv, (ld_input_argument){ .path = file, .whole = whole,
					       .as_needed = as_needed_depth >= 0 || asn }, depth + 1);
	}
}

VPRIV void ld_input_expand(struct ld_context *ctx, ld_input_vector *outv, ld_input_argument in, int depth)
{
	size_t n;
	uint8_t *b = ld_file_map(ctx, in.path, &n);

	if (!b)
		return;
	if (depth > 8) {
		ld_error_report(ctx, "%s: input linker scripts nested too deeply", in.path);
		return;
	}
	if (ld_is_input_script(b, n)) {
		ld_expand_script(ctx, outv, in.path, (const char *)b, n, in.whole, in.as_needed, depth);
		ld_region_unmap(b, n);	/* (its words were copied) */
		return;
	}
	in.buf = b;
	in.size = n;
	VVEC_PUSH(*outv, in, ld_region_alloc);
}
