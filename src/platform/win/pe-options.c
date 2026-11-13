/*
 * The command line: GNU ld's options as lld's MinGW driver (lld/MinGW)
 * accepts them, with its defaults for x86-64 (-m i386pep).  Long options take
 * one or two dashes, values after '=' or as the next argument.  Inputs and
 * -l libraries keep their order; the library search is the driver's
 * (lib<name>.dll.a, <name>.dll.a, lib<name>.a, <name>.lib, ... per -L
 * directory, the .dll.a forms only under -Bdynamic).
 */

typedef struct {
	const char *path;
	bool whole_archive;
} pe_input_arg;

static pe_input_arg *pe_inputs;
static uint32_t pe_input_count, pe_input_capacity;
static const char **pe_search_dirs;
static uint32_t pe_search_dir_count, pe_search_dir_capacity;
static const char *pe_sysroot;

/* matches -NAME / --NAME exactly */
static bool pe_flag(const char *a, const char *name)
{
	if (a[0] != '-')
		return false;
	a += a[1] == '-' ? 2 : 1;
	return !strcmp(a, name);
}

/* -NAME=V, --NAME=V, -NAME V, --NAME V: the value, or NULL */
static const char *pe_value(int argc, char **argv, int *i, const char *name)
{
	const char *a = argv[*i];
	if (a[0] != '-')
		return NULL;
	a += a[1] == '-' ? 2 : 1;
	size_t n = strlen(name);
	if (strncmp(a, name, n))
		return NULL;
	if (a[n] == '=')
		return a + n + 1;
	if (a[n])
		return NULL;
	if (*i + 1 >= argc) {
		pe_error("-%s: missing argument", name);
		return "";
	}
	return argv[++*i];
}

/* single-letter options: -oX or -o X */
static const char *pe_short(int argc, char **argv, int *i, char c)
{
	const char *a = argv[*i];
	if (a[0] != '-' || a[1] != c)
		return NULL;
	if (a[2])
		return a + 2;
	if (*i + 1 >= argc) {
		pe_error("-%c: missing argument", c);
		return "";
	}
	return argv[++*i];
}

static bool pe_parse_u64(const char *s, uint64_t *v)
{
	if (!*s)
		return false;
	char *end;
	errno = 0;
	unsigned long long x = strtoull(s, &end, 0);
	if (errno || *end)
		return false;
	*v = x;
	return true;
}

/* "R[,C]" */
static void pe_parse_reserve_commit(const char *opt, const char *s, uint64_t *reserve, uint64_t *commit)
{
	const char *comma = strchr(s, ',');
	char *a = pe_strndup(s, comma ? (size_t)(comma - s) : strlen(s));
	if (!pe_parse_u64(a, reserve))
		pe_error("%s: invalid number: %s", opt, a);
	if (comma && !pe_parse_u64(comma + 1, commit))
		pe_error("%s: invalid number: %s", opt, comma + 1);
}

/* "MAJOR[.MINOR]" */
static void pe_parse_version(const char *opt, const char *s, uint16_t *major, uint16_t *minor)
{
	const char *dot = strchr(s, '.');
	char *a = pe_strndup(s, dot ? (size_t)(dot - s) : strlen(s));
	uint64_t v;
	if (!pe_parse_u64(a, &v) || v > 0xffff)
		pe_error("%s: invalid number: %s", opt, a);
	else
		*major = (uint16_t)v;
	if (dot) {
		if (!pe_parse_u64(dot + 1, &v) || v > 0xffff)
			pe_error("%s: invalid number: %s", opt, dot + 1);
		else
			*minor = (uint16_t)v;
	} else
		*minor = 0;
}

static bool pe_file_exists(const char *p)
{
	struct stat st;
	return fstatat(pe_dirfd, p, &st, 0) == 0 && S_ISREG(st.st_mode);
}

static const char *pe_path_join(const char *dir, const char *name)
{
	size_t n = strlen(dir);
	if (n && dir[n - 1] == '/')
		return pe_format("%s%s", dir, name);
	return pe_format("%s/%s", dir, name);
}

/* lld/MinGW searchLibrary */
static const char *pe_library_search(const char *name, bool bstatic)
{
	if (name[0] == ':') {
		for (uint32_t d = 0; d < pe_search_dir_count; d++) {
			const char *p = pe_path_join(pe_search_dirs[d], name + 1);
			if (pe_file_exists(p))
				return p;
		}
		pe_error("unable to find library -l%s", name);
		return NULL;
	}
	for (uint32_t d = 0; d < pe_search_dir_count; d++) {
		const char *dir = pe_search_dirs[d], *p;
		if (!bstatic) {
			if (pe_file_exists(p = pe_path_join(dir, pe_format("lib%s.dll.a", name))))
				return p;
			if (pe_file_exists(p = pe_path_join(dir, pe_format("%s.dll.a", name))))
				return p;
		}
		if (pe_file_exists(p = pe_path_join(dir, pe_format("lib%s.a", name))))
			return p;
		if (pe_file_exists(p = pe_path_join(dir, pe_format("%s.lib", name))))
			return p;
		if (!bstatic) {
			if (pe_file_exists(p = pe_path_join(dir, pe_format("lib%s.lib", name))))
				return p;
			if (pe_file_exists(p = pe_path_join(dir, pe_format("%s.a", name)))) {
				pe_warn("found library file %s for -l%s, which doesn't follow the naming convention", p, name);
				return p;
			}
		}
	}
	pe_error("unable to find library -l%s", name);
	return NULL;
}

static void pe_input_add(const char *path, bool whole)
{
	pe_input_arg a = { path, whole };
	PE_PUSH(pe_inputs, pe_input_count, pe_input_capacity, a);
}

static void pe_export_add(const pe_export *e)
{
	PE_PUSH(cfg.exports, cfg.nexports, cfg.export_capacity, *e);
}

/* "NAME[,data]" or "NAME=OTHER[,data]" as in a .def file or -export: (lld parseExport) */
static void pe_export_parse(const char *spec, bool from_directive)
{
	pe_export e = {0};
	e.from_directive = from_directive;
	const char *comma = strchr(spec, ',');
	char *name = pe_strndup(spec, comma ? (size_t)(comma - spec) : strlen(spec));
	char *eq = strchr(name, '=');
	if (eq) {
		*eq = 0;
		e.name = name;
		e.symbol_name = eq + 1;
	} else
		e.name = e.symbol_name = name;
	while (comma) {
		const char *next = strchr(comma + 1, ',');
		char *tok = pe_strndup(comma + 1, next ? (size_t)(next - comma - 1) : strlen(comma + 1));
		for (char *c = tok; *c; c++)
			*c = (char)tolower((unsigned char)*c);
		if (!strcmp(tok, "data"))
			e.data = true;
		else if (!strcmp(tok, "constant"))
			e.constant = true;
		else if (!strcmp(tok, "private"))
			e.private_ = true;
		else if (!strcmp(tok, "noname"))
			e.no_name = true;
		else if (tok[0] == '@') {
			uint64_t v;
			if (pe_parse_u64(tok + 1, &v) && v && v < 0x10000)
				e.ordinal = (uint16_t)v;
			else
				pe_error("invalid /export: %s", spec);
		} else
			pe_error("invalid /export: %s", spec);
		comma = next;
	}
	pe_export_add(&e);
}

static const char *pe_with_sysroot(const char *p)
{
	if (p[0] == '=' && pe_sysroot)
		return pe_format("%s%s", pe_sysroot, p + 1);
	return p;
}

/* options lld's MinGW driver accepts and ignores */
static const char *const pe_ignored_flags[] = {
	"start-group", "end-group", "(", ")", "no-undefined", "enable-auto-image-base", "disable-auto-image-base",
	"color-diagnostics", "no-color-diagnostics", "nxcompat", "no-keep-memory", "as-needed", "no-as-needed",
	"sort-common", "no-seh-disable", "Bsymbolic", "no-copy-dt-needed-entries", "eh-frame-hdr", "g",
	"enable-stdcall-fixup", "disable-stdcall-fixup", "no-relax", "relax", "warn-common", "no-warn-mismatch",
	"enable-long-section-names", "disable-long-section-names", "full-shutdown", "nostdlib", "no-allow-shlib-undefined",
	NULL,
};
static const char *const pe_ignored_values[] = {
	"plugin", "plugin-opt", "threads", "error-limit", "O", "mllvm", "lto-O", "lto-CGO", "thinlto-cache-dir",
	"exclude-modules-for-implib", "sort-section", "hash-style", "dependent-load-flag", "color-diagnostics",
	NULL,
};

static bool pe_subsystem_parse(const char *s)
{
	const char *colon = strchr(s, ':');
	char *n = pe_strndup(s, colon ? (size_t)(colon - s) : strlen(s));
	static const struct { const char *name; uint16_t v; } tab[] = {
		{ "windows", IMAGE_SUBSYSTEM_WINDOWS_GUI }, { "console", IMAGE_SUBSYSTEM_WINDOWS_CUI },
		{ "native", IMAGE_SUBSYSTEM_NATIVE }, { "posix", IMAGE_SUBSYSTEM_POSIX_CUI },
		{ "wince", IMAGE_SUBSYSTEM_WINDOWS_CE_GUI }, { "efi_application", IMAGE_SUBSYSTEM_EFI_APPLICATION },
		{ "efi_boot_service_driver", IMAGE_SUBSYSTEM_EFI_BOOT_SERVICE_DRIVER },
		{ "efi_rom", IMAGE_SUBSYSTEM_EFI_ROM }, { "efi_runtime_driver", IMAGE_SUBSYSTEM_EFI_RUNTIME_DRIVER },
		{ "boot_application", IMAGE_SUBSYSTEM_WINDOWS_BOOT_APPLICATION }, { "xbox", IMAGE_SUBSYSTEM_XBOX },
	};
	for (char *c = n; *c; c++)
		*c = (char)tolower((unsigned char)*c);
	for (size_t i = 0; i < sizeof tab / sizeof tab[0]; i++)
		if (!strcmp(n, tab[i].name)) {
			cfg.subsystem = tab[i].v;
			if (colon) {
				pe_parse_version("--subsystem", colon + 1, &cfg.major_subsystem, &cfg.minor_subsystem);
				cfg.subsystem_version_set = true;
			}
			return true;
		}
	pe_error("unknown subsystem: %s", n);
	return false;
}

#define PE_PUSH_STR(v, n, x) do { uint32_t pe_cap_ = (n) ? ((n) < 16 ? 16 : (n) * 2) : 0; (void)pe_cap_; \
	const char **pe_nv_ = realloc((void *)(v), ((size_t)(n) + 1) * sizeof(*(v))); if (!pe_nv_) pe_oom(); \
	(v) = pe_nv_; (v)[(n)++] = (x); } while (0)

/* false: stop without linking (--version) */
static bool pe_options_parse(int argc, char **argv)
{
	cfg.output = NULL;
	cfg.subsystem = IMAGE_SUBSYSTEM_UNKNOWN;
	cfg.major_subsystem = 6;
	cfg.major_os = 6;
	cfg.stack_reserve = 1 << 20;
	cfg.stack_commit = 4096;
	cfg.heap_reserve = 1 << 20;
	cfg.heap_commit = 4096;
	cfg.file_align = 512;
	cfg.section_align = 4096;
	cfg.dynamic_base = cfg.high_entropy_va = cfg.nx_compat = cfg.ts_aware = cfg.large_address_aware = true;
	cfg.relocatable = true;
	cfg.debug = 2;
	cfg.build_id = true;
	cfg.insert_timestamp = true;
	cfg.auto_import = cfg.pseudo_relocs = true;
	cfg.demangle = true;
	cfg.allow_duplicate_weak = true;
	bool bstatic = false, whole = false, have_m = false, build_id_none = false;
	const char *m = NULL;

	for (int i = 1; i < argc; i++) {
		const char *a = argv[i], *v;
		if (a[0] != '-' || !a[1]) {
			pe_input_add(pe_with_sysroot(a), whole);
			continue;
		}
		/* static/dynamic library search */
		if (pe_flag(a, "Bstatic") || pe_flag(a, "static") || pe_flag(a, "dn") || pe_flag(a, "non_shared")) { bstatic = true; continue; }
		if (pe_flag(a, "Bdynamic") || pe_flag(a, "dy") || pe_flag(a, "call_shared")) { bstatic = false; continue; }
		if (pe_flag(a, "whole-archive")) { whole = true; continue; }
		if (pe_flag(a, "no-whole-archive")) { whole = false; continue; }
		if (pe_flag(a, "shared") || pe_flag(a, "dll")) { cfg.dll = true; continue; }
		if (pe_flag(a, "gc-sections")) { cfg.gc = true; continue; }
		if (pe_flag(a, "no-gc-sections")) { cfg.gc = false; continue; }
		if (pe_flag(a, "strip-all")) { cfg.debug = 0; continue; }
		if (pe_flag(a, "strip-debug")) { if (cfg.debug) cfg.debug = 1; continue; }
		if (pe_flag(a, "verbose")) { cfg.verbose = true; continue; }
		if (pe_flag(a, "version")) {
			fprintf(stdout, "vlink (compatible with GNU linkers)\n");
			return false;
		}
		if (pe_flag(a, "dynamicbase")) { cfg.dynamic_base = true; continue; }
		if (pe_flag(a, "disable-dynamicbase") || pe_flag(a, "no-dynamicbase")) { cfg.dynamic_base = false; continue; }
		if (pe_flag(a, "high-entropy-va")) { cfg.high_entropy_va = true; continue; }
		if (pe_flag(a, "disable-high-entropy-va")) { cfg.high_entropy_va = false; continue; }
		if (pe_flag(a, "disable-nxcompat")) { cfg.nx_compat = false; continue; }
		if (pe_flag(a, "tsaware")) { cfg.ts_aware = true; continue; }
		if (pe_flag(a, "disable-tsaware")) { cfg.ts_aware = false; continue; }
		if (pe_flag(a, "large-address-aware")) { cfg.large_address_aware = true; continue; }
		if (pe_flag(a, "disable-large-address-aware")) { cfg.large_address_aware = false; continue; }
		if (pe_flag(a, "no-seh")) { cfg.no_seh = true; continue; }
		if (pe_flag(a, "disable-no-seh")) { cfg.no_seh = false; continue; }
		if (pe_flag(a, "no-isolation")) { cfg.no_isolation = true; continue; }
		if (pe_flag(a, "disable-no-isolation")) { cfg.no_isolation = false; continue; }
		if (pe_flag(a, "no-bind")) { cfg.no_bind = true; continue; }
		if (pe_flag(a, "disable-no-bind")) { cfg.no_bind = false; continue; }
		if (pe_flag(a, "appcontainer")) { cfg.appcontainer = true; continue; }
		if (pe_flag(a, "disable-appcontainer")) { cfg.appcontainer = false; continue; }
		if (pe_flag(a, "forceinteg")) { cfg.integrity_check = true; continue; }
		if (pe_flag(a, "disable-forceinteg")) { cfg.integrity_check = false; continue; }
		if (pe_flag(a, "disable-reloc-section")) { cfg.relocatable = false; continue; }
		if (pe_flag(a, "enable-reloc-section")) { cfg.relocatable = true; continue; }
		if (pe_flag(a, "insert-timestamp")) { cfg.insert_timestamp = true; continue; }
		if (pe_flag(a, "no-insert-timestamp")) { cfg.insert_timestamp = false; continue; }
		if (pe_flag(a, "enable-auto-import")) { cfg.auto_import = true; continue; }
		if (pe_flag(a, "disable-auto-import")) { cfg.auto_import = false; continue; }
		if (pe_flag(a, "enable-runtime-pseudo-reloc")) { cfg.pseudo_relocs = true; continue; }
		if (pe_flag(a, "disable-runtime-pseudo-reloc")) { cfg.pseudo_relocs = false; continue; }
		if (pe_flag(a, "export-all-symbols")) { cfg.export_all = cfg.export_all_set = true; continue; }
		if (pe_flag(a, "no-export-all-symbols")) { cfg.export_all = false; cfg.export_all_set = true; continue; }
		if (pe_flag(a, "exclude-all-symbols")) { cfg.exclude_all = true; continue; }
		if (pe_flag(a, "allow-multiple-definition")) { cfg.allow_multiple = true; continue; }
		if (pe_flag(a, "no-allow-multiple-definition")) { cfg.allow_multiple = false; continue; }
		if (pe_flag(a, "kill-at")) { cfg.kill_at = true; continue; }
		if (pe_flag(a, "demangle")) { cfg.demangle = true; continue; }
		if (pe_flag(a, "no-demangle")) { cfg.demangle = false; continue; }
		if (pe_flag(a, "fatal-warnings")) { cfg.fatal_warnings = true; continue; }
		if (pe_flag(a, "no-fatal-warnings")) { cfg.fatal_warnings = false; continue; }
		if (pe_flag(a, "build-id")) { cfg.build_id = true; build_id_none = false; continue; }
		if (!strcmp(a, "-z")) {
			if (i + 1 < argc && !strcmp(argv[i + 1], "muldefs")) { cfg.allow_multiple = true; i++; continue; }
			pe_error("unknown argument: -z %s", i + 1 < argc ? argv[i + 1] : "");
			i++;
			continue;
		}
		if ((v = pe_value(argc, argv, &i, "build-id"))) {
			if (!strcmp(v, "none")) { cfg.build_id = false; build_id_none = true; }
			else {
				if (*v && strcmp(v, "fast") && strcmp(v, "md5") && strcmp(v, "sha1") && strcmp(v, "tree") && strcmp(v, "uuid"))
					pe_warn("unsupported build id hashing: %s, using default hashing.", v);
				cfg.build_id = true;
			}
			continue;
		}
		if ((v = pe_value(argc, argv, &i, "entry"))) { cfg.entry = v; continue; }
		if ((v = pe_value(argc, argv, &i, "subsystem"))) { pe_subsystem_parse(v); continue; }
		if ((v = pe_value(argc, argv, &i, "image-base"))) {
			if (!pe_parse_u64(v, &cfg.image_base))
				pe_error("--image-base: invalid number: %s", v);
			cfg.image_base_set = true;
			continue;
		}
		if ((v = pe_value(argc, argv, &i, "stack"))) { pe_parse_reserve_commit("--stack", v, &cfg.stack_reserve, &cfg.stack_commit); continue; }
		if ((v = pe_value(argc, argv, &i, "heap"))) { pe_parse_reserve_commit("--heap", v, &cfg.heap_reserve, &cfg.heap_commit); continue; }
		if ((v = pe_value(argc, argv, &i, "file-alignment"))) {
			uint64_t x;
			if (!pe_parse_u64(v, &x) || !x || (x & (x - 1)))
				pe_error("--file-alignment: invalid number: %s", v);
			else
				cfg.file_align = (uint32_t)x;
			continue;
		}
		if ((v = pe_value(argc, argv, &i, "section-alignment"))) {
			uint64_t x;
			if (!pe_parse_u64(v, &x) || !x || (x & (x - 1)))
				pe_error("--section-alignment: invalid number: %s", v);
			else
				cfg.section_align = (uint32_t)x;
			continue;
		}
		uint16_t *vmaj = NULL, *vmin = NULL;
		bool is_major = false;
		if ((v = pe_value(argc, argv, &i, "major-os-version"))) { vmaj = &cfg.major_os; is_major = true; }
		else if ((v = pe_value(argc, argv, &i, "minor-os-version"))) vmin = &cfg.minor_os;
		else if ((v = pe_value(argc, argv, &i, "major-image-version"))) { vmaj = &cfg.major_image; is_major = true; }
		else if ((v = pe_value(argc, argv, &i, "minor-image-version"))) vmin = &cfg.minor_image;
		else if ((v = pe_value(argc, argv, &i, "major-subsystem-version"))) { vmaj = &cfg.major_subsystem; is_major = true; cfg.subsystem_version_set = true; }
		else if ((v = pe_value(argc, argv, &i, "minor-subsystem-version"))) { vmin = &cfg.minor_subsystem; cfg.subsystem_version_set = true; }
		if (v) {
			uint64_t x;
			if (!pe_parse_u64(v, &x) || x > 0xffff)
				pe_error("invalid number: %s", v);
			else if (is_major)
				*vmaj = (uint16_t)x;
			else
				*vmin = (uint16_t)x;
			continue;
		}
		if ((v = pe_value(argc, argv, &i, "out-implib"))) { cfg.implib = v; continue; }
		if ((v = pe_value(argc, argv, &i, "output-def"))) { cfg.output_def = v; continue; }
		if ((v = pe_value(argc, argv, &i, "Map"))) { cfg.map_file = v; continue; }
		if ((v = pe_value(argc, argv, &i, "require-defined"))) { PE_PUSH_STR(cfg.includes, cfg.nincludes, v); continue; }
		if ((v = pe_value(argc, argv, &i, "undefined"))) { PE_PUSH_STR(cfg.optional_includes, cfg.noptional_includes, v); continue; }
		if ((v = pe_value(argc, argv, &i, "sysroot"))) { pe_sysroot = v; continue; }
		if ((v = pe_value(argc, argv, &i, "output"))) { cfg.output = v; continue; }
		if ((v = pe_value(argc, argv, &i, "library-path"))) { PE_PUSH_STR(pe_search_dirs, pe_search_dir_count, pe_with_sysroot(v)); continue; }
		if ((v = pe_value(argc, argv, &i, "library"))) {
			const char *p = pe_library_search(v, bstatic);
			if (p)
				pe_input_add(p, whole);
			continue;
		}
		if ((v = pe_value(argc, argv, &i, "exclude-libs"))) {
			for (const char *s = v; *s;) {
				size_t n = strcspn(s, ",:");
				char *lib = pe_strndup(s, n);
				if (!strcmp(lib, "ALL"))
					cfg.exclude_libs_all = true;
				else if (*lib)
					PE_PUSH_STR(cfg.exclude_libs, cfg.nexclude_libs, lib);
				s += n;
				if (*s)
					s++;
			}
			continue;
		}
		if ((v = pe_value(argc, argv, &i, "exclude-symbols"))) {
			for (const char *s = v; *s;) {
				size_t n = strcspn(s, ",:");
				if (n)
					PE_PUSH_STR(cfg.exclude_symbols, cfg.nexclude_symbols, pe_strndup(s, n));
				s += n;
				if (*s)
					s++;
			}
			continue;
		}
		if ((v = pe_value(argc, argv, &i, "icf"))) {
			if (strcmp(v, "none"))
				pe_error("--icf=%s is not supported by vlink", v);
			continue;
		}
		/* things lld implements that this linker does not (yet): a clear error */
		{
			static const char *const unsupported_values[] = {
				"wrap", "defsym", "delayload", "pdb", "reproduce", "Xlink", "guard-cf", "guard-longjmp",
				"version-script", "retain-symbols-file", "dependency-file", "lto-emit-asm", "use-temp-file", NULL,
			};
			bool done = false;
			for (const char *const *u = unsupported_values; *u && !done; u++)
				if ((v = pe_value(argc, argv, &i, *u))) {
					pe_error("-%s%s is not supported by vlink", a[1] == '-' ? "-" : "", *u);
					done = true;
				}
			if (done)
				continue;
			if (pe_flag(a, "guard-cf") || pe_flag(a, "guard-longjmp") || pe_flag(a, "pdb") || pe_flag(a, "Xlink")) {
				pe_error("%s is not supported by vlink", a);
				continue;
			}
			if (pe_flag(a, "no-guard-cf") || pe_flag(a, "no-guard-longjmp"))
				continue;
		}
		bool ignored = false;
		for (const char *const *f = pe_ignored_flags; *f && !ignored; f++)
			ignored = pe_flag(a, *f);
		for (const char *const *f = pe_ignored_values; *f && !ignored; f++)
			ignored = pe_value(argc, argv, &i, *f) != NULL;
		if (ignored)
			continue;
		if (a[1] == '-') {
			pe_error("unknown argument: %s", a);
			continue;
		}
		/* single-letter options (-o -l -L -e -u -m -s -S -O -v), after the long ones */
		switch (a[1]) {
		case 'o': cfg.output = pe_short(argc, argv, &i, 'o'); continue;
		case 'l': {
			v = pe_short(argc, argv, &i, 'l');
			const char *p = pe_library_search(v, bstatic);
			if (p)
				pe_input_add(p, whole);
			continue;
		}
		case 'L': v = pe_short(argc, argv, &i, 'L'); PE_PUSH_STR(pe_search_dirs, pe_search_dir_count, pe_with_sysroot(v)); continue;
		case 'e': cfg.entry = pe_short(argc, argv, &i, 'e'); continue;
		case 'u': v = pe_short(argc, argv, &i, 'u'); PE_PUSH_STR(cfg.optional_includes, cfg.noptional_includes, v); continue;
		case 'm': m = pe_short(argc, argv, &i, 'm'); have_m = true; continue;
		case 's': if (!a[2]) { cfg.debug = 0; continue; } break;
		case 'S': if (!a[2]) { if (cfg.debug) cfg.debug = 1; continue; } break;
		case 'O': if (!a[2] || isdigit((unsigned char)a[2]) || a[2] == 's' || a[2] == 'z') { if (!a[2]) i++; continue; } break;
		case 'v': if (!a[2]) { fprintf(stdout, "vlink (compatible with GNU linkers)\n"); continue; } break;
		case '(': case ')': if (!a[2]) continue; break;
		}
		pe_error("unknown argument: %s", a);
	}
	(void)build_id_none;

	if (!have_m) {
		/* lld's MinGW driver defaults to i386pep... it needs -m to know the machine; x86-64 here */
	} else if (strcmp(m, "i386pep")) {
		if (!strcmp(m, "i386pe") || !strcmp(m, "thumb2pe") || !strcmp(m, "arm64pe") || !strcmp(m, "arm64ecpe"))
			pe_error("vlink: emulation %s is not supported (only i386pep, x86-64)", m);
		else
			pe_error("unknown parameter: -m%s", m);
	}
	if (!cfg.output)
		cfg.output = cfg.dll ? "a.dll" : "a.exe";
	if (!cfg.image_base_set)
		cfg.image_base = cfg.dll ? 0x180000000ull : 0x140000000ull;
	if (cfg.dll)
		cfg.ts_aware = false;
	if (!cfg.dynamic_base)
		cfg.high_entropy_va = cfg.high_entropy_va && cfg.dynamic_base;
	if (!pe_input_count && !pe_error_count)
		pe_error("no input files");
	return true;
}
