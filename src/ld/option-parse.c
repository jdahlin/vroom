/* Command-line parsing (parse_args) and library search. */

/* ---------------- main link ---------------- */

static void usage_error(const char *a)
{
	error("unknown argument: %s", a);
}

/* -lNAME: libNAME.a in the -L directories in order (only static archives: no
 * dynamic linking yet, so as lld under -Bstatic); -l:FILE takes FILE as is */
static const char *find_lib(const char *name)
{
	for (uint32_t k = 0; k < opt.libdirs.n; k++) {
		const char *d = opt.libdirs.v[k];
		size_t n = strlen(d) + strlen(name) + 8;
		char *p = alloc(n);

		if (name[0] == ':')
			snprintf(p, n, "%s/%s", d, name + 1);
		else
			snprintf(p, n, "%s/lib%s.a", d, name);
		if (!faccessat(dirfd_base, p, R_OK, 0))
			return p;
	}
	return NULL;
}

static bool parse_args(int argc, char **argv)
{
	bool whole = false;
	opt.max_page = 0;	/* (default by machine once the inputs are read: arm64 64 KB, x86-64 4 KB, as lld) */
	opt.relax = true;
	opt.comment = "Linker: vld (vroom)";
	for (int i = 1; i < argc; i++) {
		const char *a = argv[i];
#define ARG(name) (!strcmp(a, name))
#define VAL(name) (!strncmp(a, name "=", strlen(name) + 1) ? (a + strlen(name) + 1) : \
		   (!strcmp(a, name) && i + 1 < argc) ? argv[++i] : NULL)
		const char *v;
		if (a[0] != '-' || !a[1]) {
			VVEC_PUSH(opt.inputs, ((InputArg){.path = a, .whole = whole}), alloc);
			continue;
		}
		if (ARG("-o") && i + 1 < argc) { opt.out = argv[++i]; continue; }
		if (!strncmp(a, "-o", 2) && a[2]) { opt.out = a + 2; continue; }
		if (ARG("-r") || ARG("--relocatable") || ARG("-i")) { opt.relocatable = true; continue; }
		if (ARG("-shared") || ARG("--shared") || ARG("-Bshareable")) { opt.shared = true; continue; }
		if (ARG("-Bsymbolic") || ARG("--Bsymbolic")) { opt.bsymbolic = true; continue; }
		if (ARG("--whole-archive") || ARG("-whole-archive")) { whole = true; continue; }
		if (ARG("--no-whole-archive") || ARG("-no-whole-archive")) { whole = false; continue; }
		if (ARG("--start-group") || ARG("--end-group") || ARG("-(") || ARG("-)")) continue;
		if (ARG("--pic-veneer")) { opt.pic_veneer = true; continue; }
		if (ARG("--eh-frame-hdr")) { opt.eh_frame_hdr = true; continue; }
		if (ARG("--no-eh-frame-hdr")) { opt.eh_frame_hdr = false; continue; }
		if (ARG("-EL") || ARG("--no-undefined-version")) continue;
		if (ARG("--no-undefined") || ARG("-zdefs")) { opt.no_undefined = true; continue; }
		if (ARG("-X") || ARG("--discard-locals")) { opt.discard_locals = true; continue; }
		if (ARG("-x") || ARG("--discard-all")) { opt.discard_all = true; continue; }
		if (ARG("-S") || ARG("--strip-debug")) { opt.strip_debug = true; continue; }
		if (ARG("-n") || ARG("--nmagic")) { opt.nmagic = true; continue; }
		if (ARG("--no-apply-dynamic-relocs")) { opt.no_apply_dyn = true; continue; }
		if (ARG("--apply-dynamic-relocs")) { opt.no_apply_dyn = false; continue; }
		if (ARG("--no-relax")) { opt.relax = false; continue; }
		if (ARG("--gc-sections")) { opt.gc = true; continue; }
		if (ARG("--no-gc-sections")) { opt.gc = false; continue; }
		if (ARG("--print-gc-sections")) { opt.print_gc = true; continue; }
		if (ARG("--no-print-gc-sections")) { opt.print_gc = false; continue; }
		if (ARG("--vroom-stubs")) { opt.stubs = true; continue; }
		if (ARG("--emit-relocs") || ARG("-q")) { error("--emit-relocs is not supported"); continue; }
		if ((v = VAL("-m")) || (!strncmp(a, "-m", 2) && a[2] && (v = a + 2))) {
			/* lld's AArch64 little-endian emulations */
			if (strcmp(v, "aarch64elf") && strcmp(v, "aarch64linux") && strcmp(v, "aarch64_elf64_le_vec"))
				error("unknown emulation: %s", v);
			continue;
		}
		if (ARG("--version") || ARG("-v") || ARG("-V")) {
			/* scripts/ld-version.sh takes the word after "LLD" */
			printf("vld (vroom) LLD 23.1.4 (compatible with GNU linkers)\n");
			if (argc == 2)
				exit(0);
			continue;
		}
		if (ARG("-static") || ARG("-Bstatic") || ARG("--Bstatic") || ARG("-dn") || ARG("-non_shared")) {
			opt.bstatic = true;
			continue;
		}
		if (ARG("-Bdynamic") || ARG("--Bdynamic") || ARG("-dy") || ARG("-call_shared")) { opt.bstatic = false; continue; }
		if (ARG("-no-pie") || ARG("--no-pie")) continue;
		if (ARG("-pie") || ARG("--pie")) { error("-pie is not supported (no dynamic linking yet)"); continue; }
		if ((v = VAL("--dynamic-linker")) || (v = VAL("-dynamic-linker")) || (v = VAL("-I"))) {
			error("--dynamic-linker is not supported (no dynamic linking yet)");
			continue;
		}
		if ((v = VAL("--library-path")) || (v = VAL("-L")) || (!strncmp(a, "-L", 2) && a[2] && (v = a + 2))) {
			VVEC_PUSH(opt.libdirs, v, alloc);
			continue;
		}
		if ((v = VAL("--library")) || (v = VAL("-l")) || (!strncmp(a, "-l", 2) && a[2] && (v = a + 2))) {
			const char *f = find_lib(v);

			if (f)
				VVEC_PUSH(opt.inputs, ((InputArg){.path = f, .whole = whole}), alloc);
			else
				error("unable to find library -l%s", v);
			continue;
		}
		if ((v = VAL("--script")) || (v = VAL("-T"))) { opt.script = v; continue; }
		if (!strncmp(a, "-T", 2) && a[2]) { opt.script = a + 2; continue; }
		if ((v = VAL("--entry")) || (v = VAL("-e"))) { opt.entry = v; continue; }
		if (!strncmp(a, "-e", 2) && a[2]) { opt.entry = a + 2; continue; }
		if ((v = VAL("-soname")) || (v = VAL("--soname")) || (v = VAL("-h"))) { opt.soname = v; continue; }
		if ((v = VAL("--build-id"))) { opt.build_id = strcmp(v, "none") ? v : NULL; continue; }
		if (ARG("--build-id")) { opt.build_id = "fast"; continue; }
		if ((v = VAL("--orphan-handling"))) {
			if (!strcmp(v, "place")) opt.orphan = 0;
			else if (!strcmp(v, "warn")) opt.orphan = 1;
			else if (!strcmp(v, "error")) opt.orphan = 2;
			else error("unknown --orphan-handling mode: %s", v);
			continue;
		}
		if ((v = VAL("--error-limit"))) { ctx->diagnostics.error_limit = atoi(v); continue; }
		if ((v = VAL("--threads"))) { nthreads = atoi(v) > 0 ? atoi(v) : 1; continue; }
		if ((v = VAL("-Map")) || (v = VAL("--Map"))) { opt.map = v; continue; }
		if ((v = VAL("--vroom-comment"))) { opt.comment = v; continue; }
		if ((v = VAL("-z")) || (!strncmp(a, "-z", 2) && a[2] && (v = a + 2))) {
			if (!strcmp(v, "notext")) opt.notext = true;
			else if (!strcmp(v, "norelro")) opt.norelro = true;
			else if (!strcmp(v, "relro")) opt.norelro = false;
			else if (!strcmp(v, "max-page-size") || !strncmp(v, "max-page-size=", 14))
				opt.max_page = strtoull(v + 14, NULL, 0);
			else if (strcmp(v, "norelro") && strcmp(v, "noexecstack") && strcmp(v, "relro") &&
				 strcmp(v, "text") && strcmp(v, "now") && strcmp(v, "lazy") && strcmp(v, "nodefaultlib"))
				warn("ignoring -z %s", v);
			continue;
		}
		usage_error(a);
	}
	if (!opt.out)
		opt.out = "a.out";
	if (opt.nmagic)		/* lld: -n ignores -z max-page-size */
		opt.max_page = 1;
	return true;
}
