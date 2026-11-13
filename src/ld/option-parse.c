/* Command-line parsing (ld_arguments_parse) and library search. */

/* ---------------- main link ---------------- */

static void ld_usage_error_report(const char *a)
{
	ld_error_report("unknown argument: %s", a);
}

/* -lNAME: libNAME.a in the -L directories in order (only static archives: no
 * dynamic linking yet, so as lld under -Bstatic); -l:FILE takes FILE as is */
static const char *ld_library_find(const char *name)
{
	for (uint32_t k = 0; k < opt.libdirs.n; k++) {
		const char *d = opt.libdirs.v[k];
		size_t n = strlen(d) + strlen(name) + 8;
		char *p = ld_region_alloc(n);

		if (name[0] == ':')
			snprintf(p, n, "%s/%s", d, name + 1);
		else
			snprintf(p, n, "%s/lib%s.a", d, name);
		if (!faccessat(dirfd_base, p, R_OK, 0))
			return p;
	}
	return NULL;
}

static bool ld_arguments_parse(int argc, char **argv)
{
	bool whole = false;
	opt.max_page = 0;	/* (default by machine once the inputs are read: arm64 64 KB, x86-64 4 KB, as lld) */
	opt.relax = true;
	opt.comment = "Linker: vld (vroom)";
	for (int i = 1; i < argc; i++) {
		const char *a = argv[i];
#define LD_ARGUMENT_IS(name) (!strcmp(a, name))
#define LD_ARGUMENT_VALUE(name) (!strncmp(a, name "=", strlen(name) + 1) ? (a + strlen(name) + 1) : \
		   (!strcmp(a, name) && i + 1 < argc) ? argv[++i] : NULL)
		const char *v;
		if (a[0] != '-' || !a[1]) {
			VVEC_PUSH(opt.inputs, ((ld_input_argument){.path = a, .whole = whole}), ld_region_alloc);
			continue;
		}
		if (LD_ARGUMENT_IS("-o") && i + 1 < argc) { opt.out = argv[++i]; continue; }
		if (!strncmp(a, "-o", 2) && a[2]) { opt.out = a + 2; continue; }
		if (LD_ARGUMENT_IS("-r") || LD_ARGUMENT_IS("--relocatable") || LD_ARGUMENT_IS("-i")) { opt.relocatable = true; continue; }
		if (LD_ARGUMENT_IS("-shared") || LD_ARGUMENT_IS("--shared") || LD_ARGUMENT_IS("-Bshareable")) { opt.shared = true; continue; }
		if (LD_ARGUMENT_IS("-Bsymbolic") || LD_ARGUMENT_IS("--Bsymbolic")) { opt.bsymbolic = true; continue; }
		if (LD_ARGUMENT_IS("--whole-archive") || LD_ARGUMENT_IS("-whole-archive")) { whole = true; continue; }
		if (LD_ARGUMENT_IS("--no-whole-archive") || LD_ARGUMENT_IS("-no-whole-archive")) { whole = false; continue; }
		if (LD_ARGUMENT_IS("--start-group") || LD_ARGUMENT_IS("--end-group") || LD_ARGUMENT_IS("-(") || LD_ARGUMENT_IS("-)")) continue;
		if (LD_ARGUMENT_IS("--pic-veneer")) { opt.pic_veneer = true; continue; }
		if (LD_ARGUMENT_IS("--eh-frame-hdr")) { opt.eh_frame_hdr = true; continue; }
		if (LD_ARGUMENT_IS("--no-eh-frame-hdr")) { opt.eh_frame_hdr = false; continue; }
		if (LD_ARGUMENT_IS("-EL") || LD_ARGUMENT_IS("--no-undefined-version")) continue;
		if (LD_ARGUMENT_IS("--no-undefined") || LD_ARGUMENT_IS("-zdefs")) { opt.no_undefined = true; continue; }
		if (LD_ARGUMENT_IS("-X") || LD_ARGUMENT_IS("--discard-locals")) { opt.discard_locals = true; continue; }
		if (LD_ARGUMENT_IS("-x") || LD_ARGUMENT_IS("--discard-all")) { opt.discard_all = true; continue; }
		if (LD_ARGUMENT_IS("-S") || LD_ARGUMENT_IS("--strip-debug")) { opt.strip_debug = true; continue; }
		if (LD_ARGUMENT_IS("-n") || LD_ARGUMENT_IS("--nmagic")) { opt.nmagic = true; continue; }
		if (LD_ARGUMENT_IS("--no-apply-dynamic-relocs")) { opt.no_apply_dyn = true; continue; }
		if (LD_ARGUMENT_IS("--apply-dynamic-relocs")) { opt.no_apply_dyn = false; continue; }
		if (LD_ARGUMENT_IS("--no-relax")) { opt.relax = false; continue; }
		if (LD_ARGUMENT_IS("--gc-sections")) { opt.gc = true; continue; }
		if (LD_ARGUMENT_IS("--no-gc-sections")) { opt.gc = false; continue; }
		if (LD_ARGUMENT_IS("--print-gc-sections")) { opt.print_gc = true; continue; }
		if (LD_ARGUMENT_IS("--no-print-gc-sections")) { opt.print_gc = false; continue; }
		if (LD_ARGUMENT_IS("--vroom-stubs")) { opt.stubs = true; continue; }
		if (LD_ARGUMENT_IS("--emit-relocs") || LD_ARGUMENT_IS("-q")) { ld_error_report("--emit-relocs is not supported"); continue; }
		if ((v = LD_ARGUMENT_VALUE("-m")) || (!strncmp(a, "-m", 2) && a[2] && (v = a + 2))) {
			/* lld's AArch64 little-endian emulations */
			if (strcmp(v, "aarch64elf") && strcmp(v, "aarch64linux") && strcmp(v, "aarch64_elf64_le_vec"))
				ld_error_report("unknown emulation: %s", v);
			continue;
		}
		if (LD_ARGUMENT_IS("--version") || LD_ARGUMENT_IS("-v") || LD_ARGUMENT_IS("-V")) {
			/* scripts/ld-version.sh takes the word after "LLD" */
			printf("vld (vroom) LLD 23.1.4 (compatible with GNU linkers)\n");
			if (argc == 2)
				exit(0);
			continue;
		}
		if (LD_ARGUMENT_IS("-static") || LD_ARGUMENT_IS("-Bstatic") || LD_ARGUMENT_IS("--Bstatic") || LD_ARGUMENT_IS("-dn") || LD_ARGUMENT_IS("-non_shared")) {
			opt.bstatic = true;
			continue;
		}
		if (LD_ARGUMENT_IS("-Bdynamic") || LD_ARGUMENT_IS("--Bdynamic") || LD_ARGUMENT_IS("-dy") || LD_ARGUMENT_IS("-call_shared")) { opt.bstatic = false; continue; }
		if (LD_ARGUMENT_IS("-no-pie") || LD_ARGUMENT_IS("--no-pie")) continue;
		if (LD_ARGUMENT_IS("-pie") || LD_ARGUMENT_IS("--pie")) { ld_error_report("-pie is not supported (no dynamic linking yet)"); continue; }
		if ((v = LD_ARGUMENT_VALUE("--dynamic-linker")) || (v = LD_ARGUMENT_VALUE("-dynamic-linker")) || (v = LD_ARGUMENT_VALUE("-I"))) {
			ld_error_report("--dynamic-linker is not supported (no dynamic linking yet)");
			continue;
		}
		if ((v = LD_ARGUMENT_VALUE("--library-path")) || (v = LD_ARGUMENT_VALUE("-L")) || (!strncmp(a, "-L", 2) && a[2] && (v = a + 2))) {
			VVEC_PUSH(opt.libdirs, v, ld_region_alloc);
			continue;
		}
		if ((v = LD_ARGUMENT_VALUE("--library")) || (v = LD_ARGUMENT_VALUE("-l")) || (!strncmp(a, "-l", 2) && a[2] && (v = a + 2))) {
			const char *f = ld_library_find(v);

			if (f)
				VVEC_PUSH(opt.inputs, ((ld_input_argument){.path = f, .whole = whole}), ld_region_alloc);
			else
				ld_error_report("unable to find library -l%s", v);
			continue;
		}
		if ((v = LD_ARGUMENT_VALUE("--script")) || (v = LD_ARGUMENT_VALUE("-T"))) { opt.script = v; continue; }
		if (!strncmp(a, "-T", 2) && a[2]) { opt.script = a + 2; continue; }
		if ((v = LD_ARGUMENT_VALUE("--entry")) || (v = LD_ARGUMENT_VALUE("-e"))) { opt.entry = v; continue; }
		if (!strncmp(a, "-e", 2) && a[2]) { opt.entry = a + 2; continue; }
		if ((v = LD_ARGUMENT_VALUE("-soname")) || (v = LD_ARGUMENT_VALUE("--soname")) || (v = LD_ARGUMENT_VALUE("-h"))) { opt.soname = v; continue; }
		if ((v = LD_ARGUMENT_VALUE("--build-id"))) { opt.build_id = strcmp(v, "none") ? v : NULL; continue; }
		if (LD_ARGUMENT_IS("--build-id")) { opt.build_id = "fast"; continue; }
		if ((v = LD_ARGUMENT_VALUE("--orphan-handling"))) {
			if (!strcmp(v, "place")) opt.orphan = 0;
			else if (!strcmp(v, "warn")) opt.orphan = 1;
			else if (!strcmp(v, "error")) opt.orphan = 2;
			else ld_error_report("unknown --orphan-handling mode: %s", v);
			continue;
		}
		if ((v = LD_ARGUMENT_VALUE("--error-limit"))) { ctx->diagnostics.error_limit = atoi(v); continue; }
		if ((v = LD_ARGUMENT_VALUE("--threads"))) { ld_thread_count = atoi(v) > 0 ? atoi(v) : 1; continue; }
		if ((v = LD_ARGUMENT_VALUE("-Map")) || (v = LD_ARGUMENT_VALUE("--Map"))) { opt.map = v; continue; }
		if ((v = LD_ARGUMENT_VALUE("--vroom-comment"))) { opt.comment = v; continue; }
		if ((v = LD_ARGUMENT_VALUE("-z")) || (!strncmp(a, "-z", 2) && a[2] && (v = a + 2))) {
			if (!strcmp(v, "notext")) opt.notext = true;
			else if (!strcmp(v, "norelro")) opt.norelro = true;
			else if (!strcmp(v, "relro")) opt.norelro = false;
			else if (!strcmp(v, "max-page-size") || !strncmp(v, "max-page-size=", 14))
				opt.max_page = strtoull(v + 14, NULL, 0);
			else if (strcmp(v, "norelro") && strcmp(v, "noexecstack") && strcmp(v, "relro") &&
				 strcmp(v, "text") && strcmp(v, "now") && strcmp(v, "lazy") && strcmp(v, "nodefaultlib"))
				ld_warning_report("ignoring -z %s", v);
			continue;
		}
		ld_usage_error_report(a);
	}
	if (!opt.out)
		opt.out = "a.out";
	if (opt.nmagic)		/* lld: -n ignores -z max-page-size */
		opt.max_page = 1;
	return true;
}
