/*
 * Exports: -export: directives, MinGW's automatic export of every eligible
 * global of a DLL (lld's AutoExporter), the .edata tables (lld's
 * EdataContents) and --out-implib, an import library in the COFF archive
 * format with short import records (what llvm-dlltool and lld write).
 */

static bool pe_auto_export_excluded_lib(const char *lib)
{
	static const char *const libs[] = {
		"libgcc", "libgcc_s", "libstdc++", "libmingw32", "libmingwex", "libg2c", "libsupc++", "libobjc", "libgcj",
		"libclang_rt.builtins", "libclang_rt.builtins-aarch64", "libclang_rt.builtins-arm",
		"libclang_rt.builtins-i386", "libclang_rt.builtins-x86_64", "libclang_rt.profile",
		"libclang_rt.profile-aarch64", "libclang_rt.profile-arm", "libclang_rt.profile-i386",
		"libclang_rt.profile-x86_64", "libc++", "libc++abi", "libFortranRuntime", "libFortranDecimal",
		"libflang_rt.runtime", "libunwind", "libmsvcrt", "libmsvcrt-os", "libucrt", "libucrtbase", "libucrtapp", NULL,
	};
	for (const char *const *l = libs; *l; l++)
		if (!strcmp(lib, *l))
			return true;
	for (uint32_t i = 0; i < cfg.nexclude_libs; i++) {
		const char *e = cfg.exclude_libs[i];
		size_t n = strlen(e);
		/* --exclude-libs takes names with or without the extension */
		if (!strcmp(lib, e) || (n > 2 && !strncmp(lib, e, strlen(lib)) && e[strlen(lib)] == '.'))
			return true;
	}
	return false;
}

/* lld AutoExporter::shouldExport */
static bool pe_should_export(pe_symbol *s)
{
	if ((s->kind != PE_SYM_REGULAR && s->kind != PE_SYM_COMMON) || !s->chunk || !s->file)
		return false;
	static const char *const syms[] = {
		"__NULL_IMPORT_DESCRIPTOR", "_pei386_runtime_relocator", "do_pseudo_reloc", "_DllMainCRTStartup",
		"_DllEntryPoint", "DllMainCRTStartup", "DllEntryPoint", "DllMain", NULL,
	};
	for (const char *const *e = syms; *e; e++)
		if (!strcmp(s->name, *e))
			return false;
	for (uint32_t i = 0; i < cfg.nexclude_symbols; i++)
		if (!strcmp(s->name, cfg.exclude_symbols[i]))
			return false;
	static const char *const prefixes[] = {
		"__imp_", "__IMPORT_DESCRIPTOR_", "__nm_", "__rtti_", "__builtin_", ".", "__profc_", "__profd_",
		"__profvp_", "_head_", NULL,
	};
	for (const char *const *p = prefixes; *p; p++)
		if (!strncmp(s->name, *p, strlen(*p)))
			return false;
	size_t n = s->name_len;
	if ((n >= 6 && !strcmp(s->name + n - 6, "_iname")) || (n >= 16 && !strcmp(s->name + n - 16, "_NULL_THUNK_DATA")))
		return false;
	if (s->file->parent) {
		if (cfg.exclude_libs_all)
			return false;
		const char *lib = pe_basename(s->file->parent);
		const char *dot = strrchr(lib, '.');
		char *base = pe_strndup(lib, dot ? (size_t)(dot - lib) : strlen(lib));
		/* --whole-archive libraries are not excluded */
		for (uint32_t i = 0; i < pe_input_count; i++)
			if (pe_inputs[i].whole_archive && !strcmp(pe_inputs[i].path, s->file->parent))
				return true;
		return !pe_auto_export_excluded_lib(base);
	}
	static const char *const objs[] = {
		"crt0.o", "crt1.o", "crt1u.o", "crt2.o", "crt2u.o", "dllcrt1.o", "dllcrt2.o", "gcrt0.o", "gcrt1.o", "gcrt2.o",
		"crtbegin.o", "crtend.o", NULL,
	};
	const char *b = pe_basename(s->file->path);
	for (const char *const *o = objs; *o; o++)
		if (!strcmp(b, *o))
			return false;
	return true;
}

static void pe_auto_export(void)
{
	if (cfg.exclude_all)
		return;
	if (!cfg.export_all) {
		if (!cfg.dll || cfg.nexports)
			return;
	}
	for (uint32_t i = 0; i < pe_global_count; i++) {
		pe_symbol *s = pe_globals[i];
		if (!pe_should_export(s))
			continue;
		pe_gc_root_add(s);
		pe_export e = {0};
		e.name = e.symbol_name = s->name;
		e.sym = s;
		if (!(s->chunk->chars & IMAGE_SCN_MEM_EXECUTE))
			e.data = true;
		pe_export_add(&e);
	}
}

static int pe_export_cmp(const void *a, const void *b)
{
	const pe_export *x = a, *y = b;
	int c = strcmp(x->name, y->name);
	return c;
}

/* lld fixupExports + assignExportOrdinals */
static void pe_exports_fixup(void)
{
	/* unique ordinals */
	for (uint32_t i = 0; i < cfg.nexports; i++)
		for (uint32_t k = i + 1; k < cfg.nexports && cfg.exports[i].ordinal; k++)
			if (cfg.exports[k].ordinal == cfg.exports[i].ordinal) {
				pe_error("duplicate export ordinal: %s", cfg.exports[k].name);
				return;
			}
	/* unique names: the first wins, a directive's entry is overridden silently */
	uint32_t n = 0;
	for (uint32_t i = 0; i < cfg.nexports; i++) {
		pe_export *e = &cfg.exports[i];
		bool dup = false;
		for (uint32_t k = 0; k < n && !dup; k++)
			if (!strcmp(cfg.exports[k].name, e->name)) {
				dup = true;
				if (cfg.exports[k].from_directive && !e->from_directive)
					cfg.exports[k] = *e;
			}
		if (!dup)
			cfg.exports[n++] = *e;
	}
	cfg.nexports = n;
	/* (a stable sort: names are unique now) */
	qsort(cfg.exports, cfg.nexports, sizeof *cfg.exports, pe_export_cmp);
	uint32_t max = 0;
	for (uint32_t i = 0; i < cfg.nexports; i++)
		if (cfg.exports[i].ordinal > max)
			max = cfg.exports[i].ordinal;
	for (uint32_t i = 0; i < cfg.nexports; i++)
		if (!cfg.exports[i].ordinal)
			cfg.exports[i].ordinal = (uint16_t)++max;
	if (max > 0xffff)
		pe_error("too many exported symbols (got %u, max 65535)", max);
	/* the symbols: exports of data are not functions; resolve to definitions */
	for (uint32_t i = 0; i < cfg.nexports; i++) {
		pe_export *e = &cfg.exports[i];
		if (!e->sym)
			e->sym = pe_sym_find(e->symbol_name ? e->symbol_name : e->name);
	}
}

/* ---------------- --out-implib ---------------- */

typedef struct { uint8_t *p; size_t n, cap; } pe_bytes;

static void pe_bytes_put(pe_bytes *b, const void *d, size_t n)
{
	if (b->n + n > b->cap) {
		size_t nc = b->cap ? b->cap * 2 : 4096;
		while (nc < b->n + n)
			nc *= 2;
		uint8_t *np = realloc(b->p, nc);
		if (!np)
			pe_oom();
		b->p = np;
		b->cap = nc;
	}
	memcpy(b->p + b->n, d, n);
	b->n += n;
}

static void pe_bytes_u16(pe_bytes *b, uint16_t v) { pe_bytes_put(b, &v, 2); }
static void pe_bytes_u32(pe_bytes *b, uint32_t v) { pe_bytes_put(b, &v, 4); }

/* a small COFF object: sections with data and relocations, symbols */
typedef struct {
	const char *name;
	uint32_t chars;
	const uint8_t *data;
	uint32_t size;
	struct { uint32_t off, sym; uint16_t type; } rel[4];
	uint32_t nrel;
} pe_mini_section;

typedef struct { const char *name; uint32_t value; int16_t sec; uint8_t sclass; } pe_mini_symbol;

static void pe_mini_object(pe_bytes *out, pe_mini_section *secs, uint32_t nsec, pe_mini_symbol *syms, uint32_t nsym)
{
	pe_bytes strtab = {0};
	pe_bytes_u32(&strtab, 0);
	uint32_t off = COFF_FILE_HEADER_SIZE + nsec * COFF_SECTION_HEADER_SIZE;
	uint32_t data_off[8], rel_off[8];
	for (uint32_t i = 0; i < nsec; i++) {
		data_off[i] = off;
		off += secs[i].size;
		rel_off[i] = off;
		off += secs[i].nrel * COFF_RELOCATION_SIZE;
	}
	uint32_t symoff = off;
	size_t start = out->n;
	pe_bytes_u16(out, IMAGE_FILE_MACHINE_AMD64);
	pe_bytes_u16(out, (uint16_t)nsec);
	pe_bytes_u32(out, 0);
	pe_bytes_u32(out, symoff);
	pe_bytes_u32(out, nsym);
	pe_bytes_u16(out, 0);
	pe_bytes_u16(out, 0);
	for (uint32_t i = 0; i < nsec; i++) {
		char nm[8] = {0};
		memcpy(nm, secs[i].name, strlen(secs[i].name) < 8 ? strlen(secs[i].name) : 8);
		pe_bytes_put(out, nm, 8);
		pe_bytes_u32(out, 0);
		pe_bytes_u32(out, 0);
		pe_bytes_u32(out, secs[i].size);
		pe_bytes_u32(out, secs[i].size ? data_off[i] : 0);
		pe_bytes_u32(out, secs[i].nrel ? rel_off[i] : 0);
		pe_bytes_u32(out, 0);
		pe_bytes_u16(out, (uint16_t)secs[i].nrel);
		pe_bytes_u16(out, 0);
		pe_bytes_u32(out, secs[i].chars);
	}
	for (uint32_t i = 0; i < nsec; i++) {
		pe_bytes_put(out, secs[i].data, secs[i].size);
		for (uint32_t r = 0; r < secs[i].nrel; r++) {
			pe_bytes_u32(out, secs[i].rel[r].off);
			pe_bytes_u32(out, secs[i].rel[r].sym);
			pe_bytes_u16(out, secs[i].rel[r].type);
		}
	}
	for (uint32_t i = 0; i < nsym; i++) {
		size_t n = strlen(syms[i].name);
		if (n <= 8) {
			char nm[8] = {0};
			memcpy(nm, syms[i].name, n);
			pe_bytes_put(out, nm, 8);
		} else {
			pe_bytes_u32(out, 0);
			pe_bytes_u32(out, (uint32_t)strtab.n);
			pe_bytes_put(&strtab, syms[i].name, n + 1);
		}
		pe_bytes_u32(out, syms[i].value);
		pe_bytes_u16(out, (uint16_t)syms[i].sec);
		pe_bytes_u16(out, 0);
		uint8_t sc[2] = { syms[i].sclass, 0 };
		pe_bytes_put(out, sc, 2);
	}
	pe_wr32(strtab.p, (uint32_t)strtab.n);
	pe_bytes_put(out, strtab.p, strtab.n);
	free(strtab.p);
	(void)start;
}

typedef struct { const char *name; pe_bytes body; const char **syms; uint32_t nsyms; } pe_ar_member;

static void pe_ar_header(pe_bytes *b, const char *name, size_t size)
{
	char h[61];
	/* (the linker members and the long name table have mode 0, as llvm's archive writer) */
	const char *mode = name[0] == '/' && (name[1] == 0 || name[1] == '/') ? "0" : "644";
	snprintf(h, sizeof h, "%-16s%-12s%-6s%-6s%-8s%-10zu`\n", name, "0", "0", "0", mode, size);
	pe_bytes_put(b, h, 60);
}

static void pe_be32_put(pe_bytes *b, uint32_t v)
{
	uint8_t x[4] = { (uint8_t)(v >> 24), (uint8_t)(v >> 16), (uint8_t)(v >> 8), (uint8_t)v };
	pe_bytes_put(b, x, 4);
}

typedef struct { const char *name; uint32_t member; } pe_ar_sym;
static int pe_ar_sym_cmp(const void *a, const void *b)
{
	return strcmp(((const pe_ar_sym *)a)->name, ((const pe_ar_sym *)b)->name);
}

/* the import library for the DLL being linked: descriptor objects + one short record per export */
static bool pe_implib_write(const char *path)
{
	const char *dll = pe_basename(cfg.output);
	const char *dot = strrchr(dll, '.');
	char *base = pe_strndup(dll, dot ? (size_t)(dot - dll) : strlen(dll));
	for (char *c = base; *c; c++)
		if (!isalnum((unsigned char)*c) && *c != '_')
			*c = '_';
	pe_ar_member *m = NULL;
	uint32_t nm = 0, capm = 0;
	pe_ar_member mem;
	/* __IMPORT_DESCRIPTOR_<base>: .idata$2 (the directory entry) + .idata$6 (the DLL name) */
	{
		memset(&mem, 0, sizeof mem);
		mem.name = dll;
		size_t dn = strlen(dll) + 1;
		uint8_t *name = pe_alloc(dn + (dn & 1));
		memcpy(name, dll, dn);
		static const uint8_t dir[20];
		pe_mini_section secs[2] = {
			{ ".idata$2", IMAGE_SCN_ALIGN_4BYTES | IMAGE_SCN_CNT_INITIALIZED_DATA | IMAGE_SCN_MEM_READ | IMAGE_SCN_MEM_WRITE,
			  dir, 20, { { 12, 2, IMAGE_REL_AMD64_ADDR32NB }, { 0, 3, IMAGE_REL_AMD64_ADDR32NB }, { 16, 4, IMAGE_REL_AMD64_ADDR32NB } }, 3 },
			{ ".idata$6", IMAGE_SCN_ALIGN_2BYTES | IMAGE_SCN_CNT_INITIALIZED_DATA | IMAGE_SCN_MEM_READ | IMAGE_SCN_MEM_WRITE,
			  name, (uint32_t)(dn + (dn & 1)), {{0}}, 0 },
		};
		pe_mini_symbol syms[7] = {
			{ pe_format("__IMPORT_DESCRIPTOR_%s", base), 0, 1, IMAGE_SYM_CLASS_EXTERNAL },
			{ ".idata$2", 0, 1, IMAGE_SYM_CLASS_SECTION },
			{ ".idata$6", 0, 2, IMAGE_SYM_CLASS_STATIC },
			{ ".idata$4", 0, 0, IMAGE_SYM_CLASS_SECTION },
			{ ".idata$5", 0, 0, IMAGE_SYM_CLASS_SECTION },
			{ "__NULL_IMPORT_DESCRIPTOR", 0, 0, IMAGE_SYM_CLASS_EXTERNAL },
			{ pe_format("\x7f%s_NULL_THUNK_DATA", base), 0, 0, IMAGE_SYM_CLASS_EXTERNAL },
		};
		pe_mini_object(&mem.body, secs, 2, syms, 7);
		mem.syms = pe_alloc(sizeof(char *));
		mem.syms[0] = syms[0].name;
		mem.nsyms = 1;
		PE_PUSH(m, nm, capm, mem);
	}
	/* __NULL_IMPORT_DESCRIPTOR: .idata$3, a zero directory entry */
	{
		memset(&mem, 0, sizeof mem);
		mem.name = dll;
		static const uint8_t z[20];
		pe_mini_section secs[1] = { { ".idata$3", IMAGE_SCN_ALIGN_4BYTES | IMAGE_SCN_CNT_INITIALIZED_DATA | IMAGE_SCN_MEM_READ | IMAGE_SCN_MEM_WRITE, z, 20, {{0}}, 0 } };
		pe_mini_symbol syms[1] = { { "__NULL_IMPORT_DESCRIPTOR", 0, 1, IMAGE_SYM_CLASS_EXTERNAL } };
		pe_mini_object(&mem.body, secs, 1, syms, 1);
		mem.syms = pe_alloc(sizeof(char *));
		mem.syms[0] = "__NULL_IMPORT_DESCRIPTOR";
		mem.nsyms = 1;
		PE_PUSH(m, nm, capm, mem);
	}
	/* \x7f<base>_NULL_THUNK_DATA: the terminators of .idata$5 and .idata$4 */
	{
		memset(&mem, 0, sizeof mem);
		mem.name = dll;
		static const uint8_t z[8];
		pe_mini_section secs[2] = {
			{ ".idata$5", IMAGE_SCN_ALIGN_8BYTES | IMAGE_SCN_CNT_INITIALIZED_DATA | IMAGE_SCN_MEM_READ | IMAGE_SCN_MEM_WRITE, z, 8, {{0}}, 0 },
			{ ".idata$4", IMAGE_SCN_ALIGN_8BYTES | IMAGE_SCN_CNT_INITIALIZED_DATA | IMAGE_SCN_MEM_READ | IMAGE_SCN_MEM_WRITE, z, 8, {{0}}, 0 },
		};
		pe_mini_symbol syms[1] = { { pe_format("\x7f%s_NULL_THUNK_DATA", base), 0, 1, IMAGE_SYM_CLASS_EXTERNAL } };
		pe_mini_object(&mem.body, secs, 2, syms, 1);
		mem.syms = pe_alloc(sizeof(char *));
		mem.syms[0] = syms[0].name;
		mem.nsyms = 1;
		PE_PUSH(m, nm, capm, mem);
	}
	/* one short import record per export */
	for (uint32_t i = 0; i < cfg.nexports; i++) {
		pe_export *e = &cfg.exports[i];
		if (e->private_)
			continue;
		memset(&mem, 0, sizeof mem);
		mem.name = dll;
		size_t ln = strlen(e->name), dn = strlen(dll);
		pe_bytes_u16(&mem.body, 0);
		pe_bytes_u16(&mem.body, 0xffff);
		pe_bytes_u16(&mem.body, 0);
		pe_bytes_u16(&mem.body, IMAGE_FILE_MACHINE_AMD64);
		pe_bytes_u32(&mem.body, 0);
		pe_bytes_u32(&mem.body, (uint32_t)(ln + 1 + dn + 1));
		pe_bytes_u16(&mem.body, e->no_name ? e->ordinal : 0);
		uint16_t type = e->data ? IMPORT_DATA : e->constant ? IMPORT_CONST : IMPORT_CODE;
		uint16_t name_type = e->no_name ? IMPORT_ORDINAL : IMPORT_NAME;
		pe_bytes_u16(&mem.body, (uint16_t)(type | name_type << 2));
		pe_bytes_put(&mem.body, e->name, ln + 1);
		pe_bytes_put(&mem.body, dll, dn + 1);
		mem.syms = pe_alloc(2 * sizeof(char *));
		mem.syms[0] = pe_format("__imp_%s", e->name);
		mem.nsyms = 1;
		if (type == IMPORT_CODE)
			mem.syms[mem.nsyms++] = e->name;
		PE_PUSH(m, nm, capm, mem);
	}
	/* the archive: first linker member (big endian), second (sorted), long names, members */
	uint32_t nsym = 0;
	for (uint32_t i = 0; i < nm; i++)
		nsym += m[i].nsyms;
	pe_ar_sym *as = calloc(nsym ? nsym : 1, sizeof *as);
	if (!as)
		pe_oom();
	uint32_t k = 0;
	size_t names_size = 0;
	for (uint32_t i = 0; i < nm; i++)
		for (uint32_t j = 0; j < m[i].nsyms; j++) {
			as[k].name = m[i].syms[j];
			as[k++].member = i;
			names_size += strlen(m[i].syms[j]) + 1;
		}
	pe_bytes longnames = {0};
	const char **hdrname = pe_alloc(nm * sizeof *hdrname);
	if (strlen(dll) + 1 > 15) {
		pe_bytes_put(&longnames, dll, strlen(dll));
		pe_bytes_put(&longnames, "/\n", 2);
	}
	for (uint32_t i = 0; i < nm; i++)
		hdrname[i] = strlen(dll) + 1 > 15 ? "/0" : pe_format("%s/", dll);
	size_t first_size = 4 + 4 * (size_t)nsym + names_size;
	size_t second_size = 4 + 4 * (size_t)nm + 4 + 2 * (size_t)nsym + names_size;
	size_t off = 8 + 60 + first_size + (first_size & 1) + 60 + second_size + (second_size & 1);
	if (longnames.n)
		off += 60 + longnames.n + (longnames.n & 1);
	uint32_t *member_off = pe_alloc(nm * sizeof *member_off);
	for (uint32_t i = 0; i < nm; i++) {
		member_off[i] = (uint32_t)off;
		off += 60 + m[i].body.n + (m[i].body.n & 1);
	}
	pe_bytes out = {0};
	pe_bytes_put(&out, "!<arch>\n", 8);
	pe_ar_header(&out, "/", first_size);
	pe_be32_put(&out, nsym);
	for (uint32_t i = 0; i < nsym; i++)
		pe_be32_put(&out, member_off[as[i].member]);
	for (uint32_t i = 0; i < nsym; i++)
		pe_bytes_put(&out, as[i].name, strlen(as[i].name) + 1);
	if (first_size & 1)
		pe_bytes_put(&out, "\n", 1);
	qsort(as, nsym, sizeof *as, pe_ar_sym_cmp);
	pe_ar_header(&out, "/", second_size);
	pe_bytes_u32(&out, nm);
	for (uint32_t i = 0; i < nm; i++)
		pe_bytes_u32(&out, member_off[i]);
	pe_bytes_u32(&out, nsym);
	for (uint32_t i = 0; i < nsym; i++)
		pe_bytes_u16(&out, (uint16_t)(as[i].member + 1));
	for (uint32_t i = 0; i < nsym; i++)
		pe_bytes_put(&out, as[i].name, strlen(as[i].name) + 1);
	if (second_size & 1)
		pe_bytes_put(&out, "\n", 1);
	if (longnames.n) {
		pe_ar_header(&out, "//", longnames.n);
		pe_bytes_put(&out, longnames.p, longnames.n);
		if (longnames.n & 1)
			pe_bytes_put(&out, "\n", 1);
	}
	for (uint32_t i = 0; i < nm; i++) {
		pe_ar_header(&out, hdrname[i], m[i].body.n);
		pe_bytes_put(&out, m[i].body.p, m[i].body.n);
		if (m[i].body.n & 1)
			pe_bytes_put(&out, "\n", 1);
		free(m[i].body.p);
	}
	free(m);
	free(as);
	free(longnames.p);
	bool ok = true;
	int fd = openat(pe_dirfd, path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
	if (fd < 0 || write(fd, out.p, out.n) != (ssize_t)out.n) {
		pe_error("cannot write %s: %s", path, strerror(errno));
		ok = false;
	}
	if (fd >= 0)
		close(fd);
	free(out.p);
	return ok;
}
