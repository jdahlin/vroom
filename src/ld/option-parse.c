/* Command-line parsing (ld_arguments_parse) and library search. */

#include "ld-internal.h"

/* ---------------- main link ---------------- */

static void ld_usage_error_report(struct ld_context *ctx, const char *a)
{
	ld_error_report(ctx, "unknown argument: %s", a);
}

/* -lNAME: libNAME.a in the -L directories in order (only static archives: no
 * dynamic linking yet, so as lld under -Bstatic); -l:FILE takes FILE as is */
static const char *ld_library_find(struct ld_context *ctx, const char *name)
{
	for (uint32_t k = 0; k < ctx->options.library_directories.n; k++) {
		const char *d = ctx->options.library_directories.v[k];
		size_t n = strlen(d) + strlen(name) + 8;
		char *p = ld_region_alloc(n);

		if (name[0] == ':')
			snprintf(p, n, "%s/%s", d, name + 1);
		else
			snprintf(p, n, "%s/lib%s.a", d, name);
		if (!faccessat(ctx->options.directory_fd, p, R_OK, 0))
			return p;
	}
	return NULL;
}

VPRIV bool ld_arguments_parse(struct ld_context *ctx, int argc, char **argv)
{
	bool whole = false;
	ctx->options.max_page_size = 0;	/* (default by machine once the inputs are read: arm64 64 KB, x86-64 4 KB, as lld) */
	ctx->options.relax = true;
	ctx->options.comment = "Linker: vld (vroom)";
	for (int i = 1; i < argc; i++) {
		const char *a = argv[i];
#define LD_ARGUMENT_IS(name) (!strcmp(a, name))
#define LD_ARGUMENT_VALUE(name) (!strncmp(a, name "=", strlen(name) + 1) ? (a + strlen(name) + 1) : \
		   (!strcmp(a, name) && i + 1 < argc) ? argv[++i] : NULL)
		const char *v;
		if (a[0] != '-' || !a[1]) {
			VVEC_PUSH(ctx->options.inputs, ((ld_input_argument){.path = a, .whole = whole}), ld_region_alloc);
			continue;
		}
		if (LD_ARGUMENT_IS("-o") && i + 1 < argc) { ctx->options.output_path = argv[++i]; continue; }
		if (!strncmp(a, "-o", 2) && a[2]) { ctx->options.output_path = a + 2; continue; }
		if (LD_ARGUMENT_IS("-r") || LD_ARGUMENT_IS("--relocatable") || LD_ARGUMENT_IS("-i")) { ctx->options.relocatable = true; continue; }
		if (LD_ARGUMENT_IS("-shared") || LD_ARGUMENT_IS("--shared") || LD_ARGUMENT_IS("-Bshareable")) { ctx->options.shared = true; continue; }
		if (LD_ARGUMENT_IS("-Bsymbolic") || LD_ARGUMENT_IS("--Bsymbolic")) { ctx->options.bsymbolic = true; continue; }
		if (LD_ARGUMENT_IS("--whole-archive") || LD_ARGUMENT_IS("-whole-archive")) { whole = true; continue; }
		if (LD_ARGUMENT_IS("--no-whole-archive") || LD_ARGUMENT_IS("-no-whole-archive")) { whole = false; continue; }
		if (LD_ARGUMENT_IS("--start-group") || LD_ARGUMENT_IS("--end-group") || LD_ARGUMENT_IS("-(") || LD_ARGUMENT_IS("-)")) continue;
		if (LD_ARGUMENT_IS("--pic-veneer")) { ctx->options.pic_veneers = true; continue; }
		if (LD_ARGUMENT_IS("--eh-frame-hdr")) { ctx->options.eh_frame_header = true; continue; }
		if (LD_ARGUMENT_IS("--no-eh-frame-hdr")) { ctx->options.eh_frame_header = false; continue; }
		if (LD_ARGUMENT_IS("-EL") || LD_ARGUMENT_IS("--no-undefined-version")) continue;
		if (LD_ARGUMENT_IS("--no-undefined") || LD_ARGUMENT_IS("-zdefs")) { ctx->options.no_undefined = true; continue; }
		if (LD_ARGUMENT_IS("-X") || LD_ARGUMENT_IS("--discard-locals")) { ctx->options.discard_locals = true; continue; }
		if (LD_ARGUMENT_IS("-x") || LD_ARGUMENT_IS("--discard-all")) { ctx->options.discard_all = true; continue; }
		if (LD_ARGUMENT_IS("-S") || LD_ARGUMENT_IS("--strip-debug")) { ctx->options.strip_debug = true; continue; }
		if (LD_ARGUMENT_IS("-n") || LD_ARGUMENT_IS("--nmagic")) { ctx->options.nmagic = true; continue; }
		if (LD_ARGUMENT_IS("--no-apply-dynamic-relocs")) { ctx->options.no_apply_dynamic_relocations = true; continue; }
		if (LD_ARGUMENT_IS("--apply-dynamic-relocs")) { ctx->options.no_apply_dynamic_relocations = false; continue; }
		if (LD_ARGUMENT_IS("--no-relax")) { ctx->options.relax = false; continue; }
		if (LD_ARGUMENT_IS("--gc-sections")) { ctx->options.garbage_collect_sections = true; continue; }
		if (LD_ARGUMENT_IS("--no-gc-sections")) { ctx->options.garbage_collect_sections = false; continue; }
		if (LD_ARGUMENT_IS("--print-gc-sections")) { ctx->options.print_garbage_collected_sections = true; continue; }
		if (LD_ARGUMENT_IS("--no-print-gc-sections")) { ctx->options.print_garbage_collected_sections = false; continue; }
		if (LD_ARGUMENT_IS("--vroom-stubs")) { ctx->options.stubs = true; continue; }
		if (LD_ARGUMENT_IS("--emit-relocs") || LD_ARGUMENT_IS("-q")) { ld_error_report(ctx, "--emit-relocs is not supported"); continue; }
		if ((v = LD_ARGUMENT_VALUE("-m")) || (!strncmp(a, "-m", 2) && a[2] && (v = a + 2))) {
			/* lld's AArch64 little-endian emulations */
			if (strcmp(v, "aarch64elf") && strcmp(v, "aarch64linux") && strcmp(v, "aarch64_elf64_le_vec"))
				ld_error_report(ctx, "unknown emulation: %s", v);
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
			ctx->options.static_libraries_only = true;
			continue;
		}
		if (LD_ARGUMENT_IS("-Bdynamic") || LD_ARGUMENT_IS("--Bdynamic") || LD_ARGUMENT_IS("-dy") || LD_ARGUMENT_IS("-call_shared")) { ctx->options.static_libraries_only = false; continue; }
		if (LD_ARGUMENT_IS("-no-pie") || LD_ARGUMENT_IS("--no-pie")) continue;
		if (LD_ARGUMENT_IS("-pie") || LD_ARGUMENT_IS("--pie")) { ld_error_report(ctx, "-pie is not supported (no dynamic linking yet)"); continue; }
		if ((v = LD_ARGUMENT_VALUE("--dynamic-linker")) || (v = LD_ARGUMENT_VALUE("-dynamic-linker")) || (v = LD_ARGUMENT_VALUE("-I"))) {
			ld_error_report(ctx, "--dynamic-linker is not supported (no dynamic linking yet)");
			continue;
		}
		if ((v = LD_ARGUMENT_VALUE("--library-path")) || (v = LD_ARGUMENT_VALUE("-L")) || (!strncmp(a, "-L", 2) && a[2] && (v = a + 2))) {
			VVEC_PUSH(ctx->options.library_directories, v, ld_region_alloc);
			continue;
		}
		if ((v = LD_ARGUMENT_VALUE("--library")) || (v = LD_ARGUMENT_VALUE("-l")) || (!strncmp(a, "-l", 2) && a[2] && (v = a + 2))) {
			const char *f = ld_library_find(ctx, v);

			if (f)
				VVEC_PUSH(ctx->options.inputs, ((ld_input_argument){.path = f, .whole = whole}), ld_region_alloc);
			else
				ld_error_report(ctx, "unable to find library -l%s", v);
			continue;
		}
		if ((v = LD_ARGUMENT_VALUE("--script")) || (v = LD_ARGUMENT_VALUE("-T"))) { ctx->options.script = v; continue; }
		if (!strncmp(a, "-T", 2) && a[2]) { ctx->options.script = a + 2; continue; }
		if ((v = LD_ARGUMENT_VALUE("--entry")) || (v = LD_ARGUMENT_VALUE("-e"))) { ctx->options.entry = v; continue; }
		if (!strncmp(a, "-e", 2) && a[2]) { ctx->options.entry = a + 2; continue; }
		if ((v = LD_ARGUMENT_VALUE("-soname")) || (v = LD_ARGUMENT_VALUE("--soname")) || (v = LD_ARGUMENT_VALUE("-h"))) { ctx->options.soname = v; continue; }
		if ((v = LD_ARGUMENT_VALUE("--build-id"))) { ctx->options.build_id = strcmp(v, "none") ? v : NULL; continue; }
		if (LD_ARGUMENT_IS("--build-id")) { ctx->options.build_id = "fast"; continue; }
		if ((v = LD_ARGUMENT_VALUE("--orphan-handling"))) {
			if (!strcmp(v, "place")) ctx->options.orphan_handling = 0;
			else if (!strcmp(v, "warn")) ctx->options.orphan_handling = 1;
			else if (!strcmp(v, "error")) ctx->options.orphan_handling = 2;
			else ld_error_report(ctx, "unknown --orphan-handling mode: %s", v);
			continue;
		}
		if ((v = LD_ARGUMENT_VALUE("--error-limit"))) { ctx->diagnostics.error_limit = atoi(v); continue; }
		if ((v = LD_ARGUMENT_VALUE("--threads"))) { ld_thread_count = atoi(v) > 0 ? atoi(v) : 1; continue; }
		if ((v = LD_ARGUMENT_VALUE("-Map")) || (v = LD_ARGUMENT_VALUE("--Map"))) { ctx->options.map = v; continue; }
		if ((v = LD_ARGUMENT_VALUE("--vroom-comment"))) { ctx->options.comment = v; continue; }
		if ((v = LD_ARGUMENT_VALUE("-z")) || (!strncmp(a, "-z", 2) && a[2] && (v = a + 2))) {
			if (!strcmp(v, "notext")) ctx->options.notext = true;
			else if (!strcmp(v, "norelro")) ctx->options.no_relro = true;
			else if (!strcmp(v, "relro")) ctx->options.no_relro = false;
			else if (!strcmp(v, "max-page-size") || !strncmp(v, "max-page-size=", 14))
				ctx->options.max_page_size = strtoull(v + 14, NULL, 0);
			else if (strcmp(v, "norelro") && strcmp(v, "noexecstack") && strcmp(v, "relro") &&
				 strcmp(v, "text") && strcmp(v, "now") && strcmp(v, "lazy") && strcmp(v, "nodefaultlib"))
				ld_warning_report(ctx, "ignoring -z %s", v);
			continue;
		}
		ld_usage_error_report(ctx, a);
	}
	if (!ctx->options.output_path)
		ctx->options.output_path = "a.out";
	if (ctx->options.nmagic)		/* lld: -n ignores -z max-page-size */
		ctx->options.max_page_size = 1;
	return true;
}
