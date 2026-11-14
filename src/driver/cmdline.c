/*
 * cmdline.c: clang's command line syntax into the fields of struct driver_job, one option at a time.
 *
 * driver_cmdline_parse is one loop over argv.  An option it does not know is ignored (kbuild passes
 * many that only matter to clang); one clang rejects (a bad -fstrict-flex-arrays=, -CC) ends the job
 * with a message on the job's error fd.  The return value is 0 to go on, 1 to end the job
 * (job->status is set) and 2 to return 1 at once without the job's cleanup (an invalid -ftls-model=).
 * Nothing here opens a file: options only select what later stages do, so their effect on predefined
 * macros is in predef.c, on search paths in searchpath.c, on the target's features in target-*.c.
 * -march/-mcpu are kept separately (march_lse, mcpu_lse) and resolved later because their order
 * matters.  driver_cmdline_check runs after the target check and refuses a missing input and a bad
 * option value (job->bad_value_option).
 */
#include "driver-internal.h"

/* an input file by its suffix, as clang's driver: .c .h .i (C), .S .s (assembly), .dts, C++'s */
static bool driver_input_is_source(const char *a)
{
	static const char *const sfx[] = { ".c", ".h", ".i", ".S", ".s", ".dts", ".cpp", ".cc", ".cxx", ".c++", ".C", ".hpp", ".hh", ".hxx", ".ii" };
	size_t l = strlen(a);

	for (size_t i = 0; i < sizeof sfx / sizeof *sfx; i++) {
		size_t n = strlen(sfx[i]);

		if (l > n && !strcmp(a + l - n, sfx[i]))
			return true;
	}
	return false;
}

/* 0 to go on; 1: the job ends (status and message set); 2: return 1 at once, as the loop did on
   an invalid -ftls-model= (without the job's cleanup) */
VPRIV __attribute__((noinline)) int driver_cmdline_parse(struct driver_job *job, int argc, char **argv)
{
	for (int i = 1; i < argc; i++) {
		const char *a = argv[i];
#define DRIVER_OPTION_VALUE(flag) (!strcmp(a, flag) ? (i + 1 < argc ? argv[++i] : "") : !strncmp(a, flag, strlen(flag)) ? a + strlen(flag) : NULL)
		const char *v;

		if (!strcmp(a, "--cmp") && i + 1 < argc) { job->compare_file = argv[++i]; continue; }
		if (!strcmp(a, "-fsyntax-only")) { job->syntax_only = 1; continue; }
		if (!strcmp(a, "-S")) { job->syntax_only = 1; job->emit_assembly = 1; continue; }
		if (!strcmp(a, "-c")) { job->object_mode = 1; continue; }
		if (!strcmp(a, "-dM")) { job->dump_macros = 1; continue; }
		if (!strcmp(a, "-H")) { job->show_includes = 1; continue; }
		if (!strcmp(a, "-C")) { job->comments = 1; continue; }
		if (!strcmp(a, "-P")) { job->no_line_markers = 1; continue; }
		if (!strcmp(a, "-CC") || !strcmp(a, "-traditional-cpp") || !strcmp(a, "-traditional")) {
			dprintf(job->errfd, "vcc: error: %s is not supported\n", a);	/* TODO.md: output modes */
			job->status = 2;
			return 1;
		}
		if (!strcmp(a, "-dD")) { job->dump_defines = 1; continue; }
		if (!strcmp(a, "-fms-anonymous-structs") || !strcmp(a, "-fms-extensions")) { job->ms_anonymous_structs = 1; continue; }
		if (!strncmp(a, "-fstrict-flex-arrays=", 21)) {
			if (a[21] < '0' || a[21] > '3' || a[22]) {
				dprintf(job->errfd, "vcc: error: invalid argument '%s' to -fstrict-flex-arrays=\n", a + 21);
				job->status = 2;
				return 1;
			}
			job->strict_flex_arrays = a[21] - '0';
			continue;
		}
		if (!strcmp(a, "-fshort-wchar")) { job->short_wchar = 1; continue; }
		if (!strcmp(a, "-funsigned-char") || !strcmp(a, "-fno-signed-char")) { job->unsigned_char = 1; continue; }
		if (!strcmp(a, "-fsigned-char") || !strcmp(a, "-fno-unsigned-char")) { job->unsigned_char = 0; continue; }
		if (!strncmp(a, "-ffp-contract=", 14)) { job->no_fp_contract = !strcmp(a + 14, "off"); continue; }
		if (!strcmp(a, "-ffast-math")) { job->fast_math = 1; job->math_flags = (job->math_flags | 39) & ~16; continue; }
		if (!strcmp(a, "-fno-fast-math")) { job->fast_math = 0; job->math_flags &= 8; continue; }
		if (!strcmp(a, "-ffinite-math-only")) { job->math_flags |= 1; continue; }
		if (!strcmp(a, "-fno-finite-math-only")) { job->math_flags &= ~5; continue; }
		if (!strcmp(a, "-fno-math-errno")) { job->math_flags = (job->math_flags | 2) & ~16; continue; }
		if (!strcmp(a, "-fmath-errno")) { job->math_flags = (job->math_flags & ~6) | 16; continue; }	/* no longer fast-math */
		if (!strcmp(a, "-fno-unsafe-math-optimizations") || !strcmp(a, "-frounding-math") ||
		    !strcmp(a, "-fsigned-zeros") || !strcmp(a, "-ftrapping-math") || !strcmp(a, "-fsignaling-nans") ||
		    !strcmp(a, "-fno-approx-func") || !strcmp(a, "-fno-reciprocal-math") || !strcmp(a, "-fno-associative-math") ||
		    !strcmp(a, "-fhonor-infinities") || !strcmp(a, "-fhonor-nans") ||
		    !strcmp(a, "-ffp-model=strict") || !strcmp(a, "-ffp-model=precise")) {
			job->fast_math = 0;
			continue;
		}
		if (!strcmp(a, "-pthread")) { job->math_flags |= 8; continue; }	/* (-pthread: _REENTRANT) */
		if (!strcmp(a, "-fcolor-diagnostics") || !strcmp(a, "-fdiagnostics-color") || !strcmp(a, "-fdiagnostics-color=always")) { job->diagnostic_color = 1; continue; }
		if (!strcmp(a, "-fno-color-diagnostics") || !strcmp(a, "-fno-diagnostics-color") || !strcmp(a, "-fdiagnostics-color=never")) { job->diagnostic_color = -1; continue; }
		if (!strcmp(a, "-fdiagnostics-color=auto")) { job->diagnostic_color = 0; continue; }
		if (!strcmp(a, "-fno-caret-diagnostics")) { job->no_caret = 1; continue; }
		if (!strcmp(a, "-fcaret-diagnostics")) { job->no_caret = 0; continue; }
		if (!strcmp(a, "-fdiagnostics-show-id")) { job->show_diagnostic_id = 1; continue; }
		if (!strncmp(a, "-fdiagnostics-format=", 21)) {
			const char *f = a + 21;

			job->diagnostic_format = !strcmp(f, "json") ? DIAG_FORMAT_JSON : !strcmp(f, "sarif") ? DIAG_FORMAT_SARIF : DIAG_FORMAT_TEXT;
			continue;
		}
		if (!strncmp(a, "-fdiagnostics-add-output=sarif:file=", 36)) { job->pp_options.sarif_file = a + 36; continue; }
		if (!strncmp(a, "-fdiagnostics-add-output=html:file=", 35)) { job->pp_options.html_file = a + 35; continue; }
		if (!strncmp(a, "-fdiagnostics-style=", 20)) {	/* vcc (default), clang, narrated */
			const char *f = a + 20;

			job->diagnostic_style = !strcmp(f, "clang") ? DIAG_STYLE_CLANG : !strcmp(f, "narrated") ? DIAG_STYLE_NARRATED : DIAG_STYLE_VCC;
			if (job->diagnostic_style == DIAG_STYLE_VCC && strcmp(f, "vcc"))
				job->bad_value_option = a;
			continue;
		}
		if (!strncmp(a, "-fdiagnostics-text-art-charset=", 31)) {	/* GCC's: ascii, unicode, emoji, none */
			const char *f = a + 31;

			job->diagnostic_unicode = !strcmp(f, "unicode") || !strcmp(f, "emoji");
			if (!job->diagnostic_unicode && strcmp(f, "ascii") && strcmp(f, "none"))
				job->bad_value_option = a;
			continue;
		}
		if (!strcmp(a, "-fno-diagnostics-show-id")) { job->show_diagnostic_id = 0; continue; }
		if (!strcmp(a, "-fdiagnostics-parseable-fixits")) { job->pp_options.diag_parseable = 1; continue; }
		if (!strcmp(a, "-fdiagnostics-generate-patch")) { job->pp_options.diag_patch = 1; continue; }
		if (!strcmp(a, "--fix")) { job->pp_options.fix_apply = 1; continue; }
		if (!strcmp(a, "-fno-diagnostics-parseable-fixits")) { job->pp_options.diag_parseable = 0; continue; }
		if (!strncmp(a, "-fspell-checking-limit=", 23)) { job->spell_checking_limit = atoi(a + 23); job->spell_checking_limit = job->spell_checking_limit ? job->spell_checking_limit : -1; continue; }
		if (!strcmp(a, "-fno-spell-checking")) { job->spell_checking_limit = -2; continue; }
		if (!strncmp(a, "-ferror-limit=", 14)) { job->error_limit = atoi(a + 14); job->error_limit = job->error_limit ? job->error_limit : -1; continue; }
		if (!strncmp(a, "-fmacro-backtrace-limit=", 24)) { job->macro_backtrace_limit = atoi(a + 24); job->macro_backtrace_limit = job->macro_backtrace_limit ? job->macro_backtrace_limit : -1; continue; }
		if (!strncmp(a, "-std=", 5) && (!strncmp(a + 5, "c++", 3) || !strncmp(a + 5, "gnu++", 5))) {
			/* clang's C++ spellings (LangStandards.def) */
			static const struct { const char *s; int v; } stds[] = {
				{ "98", 98 }, { "03", 98 }, { "11", 11 }, { "0x", 11 }, { "14", 14 }, { "1y", 14 }, { "17", 17 }, { "1z", 17 },
				{ "20", 20 }, { "2a", 20 }, { "23", 23 }, { "2b", 23 }, { "26", 26 }, { "2c", 26 },
			};
			const char *v = a + 5 + (a[5] == 'g' ? 5 : 3);
			size_t k;

			for (k = 0; k < sizeof(stds) / sizeof(stds[0]) && strcmp(v, stds[k].s); k++)
				;
			if (k == sizeof(stds) / sizeof(stds[0])) {
				dprintf(job->errfd, "error: invalid value '%s' in '%s'\n", a + 5, a);
				job->status = 1;
				return 1;
			}
			job->cxx = stds[k].v;
			job->cxx_given = 1;
			job->strict_standard = a[5] != 'g';
			continue;
		}
		if (!strcmp(a, "-fno-exceptions") || !strcmp(a, "-fno-cxx-exceptions")) { job->no_exceptions = 1; continue; }
		if (!strcmp(a, "-fexceptions") || !strcmp(a, "-fcxx-exceptions")) { job->no_exceptions = 0; continue; }
		if (!strcmp(a, "-fno-rtti")) { job->no_rtti = 1; continue; }
		if (!strcmp(a, "-frtti")) { job->no_rtti = 0; continue; }
		if (!strcmp(a, "-stdlib=libc++")) { job->cxx_libcxx = 1; continue; }
		if (!strcmp(a, "-nostdinc++")) { job->no_standard_cxx_includes = 1; continue; }
		if (!strcmp(a, "-stdlib=libstdc++") || !strcmp(a, "-stdlib=platform")) { job->cxx_libcxx = 0; continue; }
		if (!strncmp(a, "-std=", 5)) {
			/* clang's C spellings (LangStandards.def); ver: a __STDC_VERSION__ of its own */
			static const struct { const char *s; int v; const char *ver; } stds[] = {
				{ "c89", 89, NULL }, { "c90", 89, NULL }, { "iso9899:1990", 89, NULL }, { "iso9899:199409", 89, "199409L" },
				{ "gnu89", 89, NULL }, { "gnu90", 89, NULL }, { "c99", 99, NULL }, { "iso9899:1999", 99, NULL }, { "c9x", 99, NULL },
				{ "iso9899:199x", 99, NULL }, { "gnu99", 99, NULL }, { "gnu9x", 99, NULL }, { "c11", 11, NULL }, { "iso9899:2011", 11, NULL },
				{ "c1x", 11, NULL }, { "iso9899:201x", 11, NULL }, { "gnu11", 11, NULL }, { "gnu1x", 11, NULL }, { "c17", 17, NULL },
				{ "iso9899:2017", 17, NULL }, { "c18", 17, NULL }, { "iso9899:2018", 17, NULL }, { "gnu17", 17, NULL }, { "gnu18", 17, NULL },
				{ "c23", 23, NULL }, { "iso9899:2024", 23, NULL }, { "c2x", 23, NULL }, { "gnu23", 23, NULL }, { "gnu2x", 23, NULL },
				{ "c2y", 23, "202400L" }, { "gnu2y", 23, "202400L" },
			};
			const char *v = a + 5;
			size_t k;

			for (k = 0; k < sizeof(stds) / sizeof(stds[0]) && strcmp(v, stds[k].s); k++)
				;
			if (k == sizeof(stds) / sizeof(stds[0])) {
				dprintf(job->errfd, "error: invalid value '%s' in '%s'\n", v, a);	/* clang, without its notes */
				job->status = 1;
				return 1;
			}
			job->standard = stds[k].v;
			job->standard_version = stds[k].ver;
			job->strict_standard = strncmp(v, "gnu", 3) != 0;
			continue;
		}
		if (!strcmp(a, "-ansi")) { job->standard = 89; job->strict_standard = 1; continue; }
		if (!strcmp(a, "-trigraphs") || !strcmp(a, "-ftrigraphs")) { job->trigraphs = 1; continue; }
		if (!strcmp(a, "-fno-trigraphs")) { job->trigraphs = 0; continue; }
		if (!strcmp(a, "-fgnu89-inline")) { job->gnu89_inline = 1; continue; }
		{
			/* gcc-only options clang 23 rejects: rejected too, so configure and kbuild probes
			   (cc-option) choose what they choose with clang */
			static const char *const gcconly[] = {
				"--param", "-fdiagnostics-show-context", "-falign-jumps", "-finline-stringops",
				"-fno-allow-store-data-races", "-fallow-store-data-races", "-fzero-init-padding-bits",
				"-fmin-function-alignment", "-mnop-mcount", "-mrecord-mcount", "-fconserve-stack",
				"-fno-var-tracking-assignments", "-mpreferred-stack-boundary",
				"-mindirect-branch=", "-mindirect-branch-register",
			};

			for (size_t k = 0; k < sizeof gcconly / sizeof *gcconly; k++) {
				size_t n = strlen(gcconly[k]);

				if (!strncmp(a, gcconly[k], n) && (!a[n] || a[n] == '=' || gcconly[k][n - 1] == '=')) {
					dprintf(job->errfd, "clang: error: unknown argument: '%s'\n", a);
					job->status = 1;
					return 1;
				}
			}
		}
		if (!strcmp(a, "-fvcc-kernel")) { job->vcc_kernel = 1; continue; }
		if (!strcmp(a, "-fno-gnu89-inline")) { job->gnu89_inline = 0; continue; }
		if ((a[1] == 'W' && a[2] && a[3] != ',') || !strcmp(a, "-w") || strstr(a, "-pedantic") == a + (a[1] == '-')) {
			/* warning options, in order, for diag_warning_state (some are also handled below);
			   -pedantic is -Wpedantic, -pedantic-errors also -Werror=pedantic (two entries) */
			if (!job->warning_arguments)
				job->warning_arguments = calloc((size_t)argc * 2 + 1, sizeof(*job->warning_arguments));
			if (!strcmp(a, "-pedantic") || !strcmp(a, "--pedantic"))
				job->warning_arguments[job->warning_argument_count++] = "-Wpedantic";
			else if (!strcmp(a, "-pedantic-errors") || !strcmp(a, "--pedantic-errors"))
				job->warning_arguments[job->warning_argument_count++] = "-Wpedantic", job->warning_arguments[job->warning_argument_count++] = "-Werror=pedantic";
			else if (a[1] == 'W' || a[1] == 'w')
				job->warning_arguments[job->warning_argument_count++] = a;
		}
		if (!strcmp(a, "-Wno-implicit-int") || !strcmp(a, "-Wno-error=implicit-int")) { job->allow_implicit_int = 1; continue; }
		if (!strncmp(a, "-Wno-", 5)) {
			/* errors clang lets -Wno-error= make warnings (diag_warning_state has the -W flags) */
			const char *f = a + 5;

			if (!strncmp(f, "error=", 6))
				f += 6;
			job->conversion_warning_flags |= !strcmp(f, "int-conversion") ? 1 : !strcmp(f, "incompatible-pointer-types") ? 2 | 4	/* (the function pointer group is part of it) */
				     : !strcmp(f, "incompatible-function-pointer-types") ? 4 : 0;
		}
		if (!strcmp(a, "-Wno-implicit-function-declaration") || !strcmp(a, "-Wno-error=implicit-function-declaration")) {
			job->allow_implicit_function = 1;
			continue;
		}
		if (!strcmp(a, "--vcc-layout-checks")) { job->syntax_only = 1; job->layout_checks = 1; continue; }
		if (!strcmp(a, "--lint")) { job->syntax_only = 1; job->lint = 1; continue; }	/* vpp only: the style checks, front end alone */
		if (!strcmp(a, "--vcc-tokhash")) { job->token_hash = 1; continue; }
		if (!strcmp(a, "-o") && i + 1 < argc) { job->output_file = argv[++i]; continue; }
		if (!strncmp(a, "-Wp,-MMD,", 9) || !strncmp(a, "-Wp,-MD,", 8)) {
			job->dependency_file_mode = a[6] == 'M' ? 2 : 1;
			job->depfile = strchr(a + 4, ',') + 1;
			continue;
		}
		if (!strcmp(a, "-MD") || !strcmp(a, "-MMD")) { job->dependency_file_mode = a[2] == 'M' ? 2 : 1; continue; }
		if (!strcmp(a, "-M") || !strcmp(a, "-MM")) { job->dependency_print_mode = a[2] == 'M' ? 2 : 1; continue; }
		if (!strcmp(a, "-MP")) { job->dependency_phony = 1; continue; }
		if (!strcmp(a, "-MF") && i + 1 < argc) { job->depfile = argv[++i]; continue; }
		if ((!strcmp(a, "-MT") || !strcmp(a, "-MQ")) && i + 1 < argc) {
			/* targets joined by spaces; -MQ quoted for make: $ -> $$, space -> \ space */
			static __thread char q[4096];
			const char *t = argv[++i];
			size_t k = job->dependency_target ? strlen(q) : 0;

			if (k && k + 1 < sizeof(q))
				q[k++] = ' ';
			for (; *t && k + 3 < sizeof(q); t++) {
				if (a[2] == 'Q' && *t == '$')
					q[k++] = '$';
				else if (a[2] == 'Q' && (*t == ' ' || *t == '\t' || *t == '#'))
					q[k++] = '\\';
				q[k++] = *t;
			}
			q[k] = 0;
			job->dependency_target = q;
			continue;
		}
		if ((v = DRIVER_OPTION_VALUE("-iquote"))) { job->quote_directories[job->quote_count++] = v; continue; }
		if ((v = DRIVER_OPTION_VALUE("-isystem"))) { job->system_directories[job->system_count++] = v; continue; }
		if ((v = DRIVER_OPTION_VALUE("-idirafter"))) { job->after_directories[job->after_count++] = v; continue; }
		if ((v = DRIVER_OPTION_VALUE("--embed-dir="))) { if (job->embed_count < 64) job->embed_directories[job->embed_count++] = v; continue; }
		if ((v = DRIVER_OPTION_VALUE("-imacros"))) {
			/* clang's: the file's macros, its tokens dropped up to the ## (before every -include) */
			driver_buffer_append_string(&job->include_macros_text, "#__include_macros \"");
			driver_buffer_append_string(&job->include_macros_text, v);
			driver_buffer_append_string(&job->include_macros_text, "\"\n##\n");
			continue;
		}
		if ((v = DRIVER_OPTION_VALUE("-include"))) {
			driver_buffer_append_string(&job->include_text, "#include \"");
			driver_buffer_append_string(&job->include_text, v);
			driver_buffer_append_string(&job->include_text, "\"\n");
			continue;
		}
		if ((v = DRIVER_OPTION_VALUE("-I"))) { job->include_directories[job->include_count++] = v; continue; }
		if ((v = DRIVER_OPTION_VALUE("-D"))) {
			if (!strncmp(v, "__KERNEL__", 10) && (!v[10] || v[10] == '='))
				job->vcc_kernel = 1;	/* the kernel: its error() calls rely on clang -O2 folding */
			driver_buffer_append_define(&job->command_line_defines, v);
			continue;
		}
		if ((v = DRIVER_OPTION_VALUE("-U"))) { driver_buffer_append_string(&job->command_line_defines, "#undef "); driver_buffer_append_string(&job->command_line_defines, v); driver_buffer_append_string(&job->command_line_defines, "\n"); continue; }
		if (!strncmp(a, "-fmacro-prefix-map=", 19)) {
			snprintf(job->prefix_map_buffer, sizeof(job->prefix_map_buffer), "%s", a + 19);
			job->prefix_map = job->prefix_map_buffer;
			continue;
		}
		if (!strncmp(a, "--target=", 9) || !strcmp(a, "-target")) {
			const char *m = strstr(a, "-macos");

			job->wasm_bits = strstr(a, "wasm64") ? 64 : strstr(a, "wasm32") ? 32 : 0;
#ifndef VCC_WASM
			if (job->wasm_bits) {
				dprintf(job->errfd, "vcc: error: this build has no WebAssembly support (make VCC_WASM=1)\n");
				job->status = 1;
				return 1;
			}
#endif
			job->darwin = !job->wasm_bits && (strstr(a, "darwin") || strstr(a, "apple"));
			job->target_x64 = !job->wasm_bits && !job->darwin && strstr(a, "x86_64");
			job->hosted = 0;
			if (m && m[6] >= '0' && m[6] <= '9')
				job->macos_minimum_version = m + 6, job->macos_minimum_source = 2;	/* arm64-apple-macos14.2: wins over the flag */
		}
		else if (!strncmp(a, "-mmacosx-version-min=", 21) || !strncmp(a, "-mmacos-version-min=", 20)) {
			if (job->macos_minimum_source < 2)
				job->macos_minimum_version = strchr(a, '=') + 1, job->macos_minimum_source = 1;
		}
		else if (!strcmp(a, "-nostdinc")) job->no_standard_includes = 1;
		else if (!strcmp(a, "-undef")) job->undefine_predefined = 1;
		else if (!strcmp(a, "-x") && i + 1 < argc) {
			i++;
			job->asm_mode = !strcmp(argv[i], "assembler-with-cpp");
			if (!strcmp(argv[i], "c++") || !strcmp(argv[i], "c++-header") || !strcmp(argv[i], "c++-cpp-output"))
				job->cxx = job->cxx ? job->cxx : -1;	/* the standard: -std= or clang++'s default (gnu++17), after the loop */
			else if (strcmp(argv[i], "none"))
				job->cxx = job->cxx_given ? job->cxx : 0;
			job->raw_assembler = !strcmp(argv[i], "assembler") ? 1 : 0;
			job->language_given = strcmp(argv[i], "none") != 0;
		}
		else if (!strcmp(a, "-O0")) job->optimize = 0, job->optimize_size = 0, job->optimization_level = 0;
		else if (!strcmp(a, "-Os") || !strcmp(a, "-Oz")) { job->optimize = 2; job->optimize_size = 1; job->optimization_level = 2; }
		else if (!strncmp(a, "-O", 2)) job->optimize = 2, job->optimize_size = 0, job->optimization_level = !strcmp(a, "-O1") || !strcmp(a, "-Og") ? 1 : 2;
		else if (!strcmp(a, "-fpie")) job->pie = 1, job->pic = 0;
		else if (!strcmp(a, "-fPIE")) job->pie = 2, job->pic = 0;
		else if (!strcmp(a, "-fpic")) job->pic = 1, job->pie = 0;
		else if (!strcmp(a, "-fPIC")) job->pic = 2, job->pie = 0;
		else if (!strcmp(a, "-g") || !strcmp(a, "-g1") || !strcmp(a, "-g2") || !strcmp(a, "-g3") ||
			 !strncmp(a, "-ggdb", 5) || !strncmp(a, "-gdwarf", 7) || !strcmp(a, "-gline-tables-only") ||
			 !strcmp(a, "-glldb") || !strcmp(a, "-gfull"))
			job->debug = 1;	/* line tables (the rest of DWARF: TODO.md) */
		else if (!strcmp(a, "-g0")) job->debug = 0;
		else if (!strcmp(a, "-ffunction-sections")) job->function_data_sections |= 1;
		else if (!strcmp(a, "-fno-function-sections")) job->function_data_sections &= ~1;
		else if (!strcmp(a, "-fdata-sections")) job->function_data_sections |= 2;
		else if (!strcmp(a, "-fno-data-sections")) job->function_data_sections &= ~2;
		else if (!strncmp(a, "-ftls-model=", 12)) {
			if (!(job->tls_model = vcc_tls_model(a + 12))) {
				fprintf(stderr, "vcc: error: invalid value '%s' in '%s'\n", a + 12, a);
				return 2;	/* (Q-drv-07: the one exit that skips the job's cleanup) */
			}
		}
		else if (!strcmp(a, "-fno-PIE") || !strcmp(a, "-fno-pie")) job->pie = 0;
		else if (!strcmp(a, "-fno-PIC") || !strcmp(a, "-fno-pic")) job->pic = 0, job->pie = 0;
		else if (!strcmp(a, "-ffreestanding")) job->freestanding = 1;
		else if (!strcmp(a, "-fno-builtin")) job->no_builtin = 1;
		else if (!strcmp(a, "-fno-jump-tables")) job->no_jump_tables = 1;
		else if (!strcmp(a, "-fjump-tables")) job->no_jump_tables = 0;
		else if (!strncmp(a, "-fno-builtin-", 13)) cc_disable_builtin(job->disabled_builtins, a + 13);
		else if (!strcmp(a, "-mllvm") && i + 1 < argc) {
			/* emcc -sSUPPORT_LONGJMP=wasm: setjmp/longjmp through wasm exceptions */
			if (!strcmp(argv[++i], "-wasm-enable-sjlj"))
				job->sjlj = 1;
		}
		else if (!strcmp(a, "-finstrument-functions")) job->instrument_functions = 1;
		else if (!strcmp(a, "-fomit-frame-pointer")) job->omit_frame_pointer = 1;
		else if (!strcmp(a, "-fno-omit-frame-pointer")) job->omit_frame_pointer = 0;
		else if (!strcmp(a, "-foptimize-sibling-calls")) job->sibling_calls = 1;
		else if (!strcmp(a, "-fno-optimize-sibling-calls")) job->sibling_calls = -1;
		else if (!strcmp(a, "-fasynchronous-unwind-tables")) job->cfi = 1;
		else if (!strcmp(a, "-fno-asynchronous-unwind-tables")) job->cfi = 0;
		else if (!strcmp(a, "-mstrict-align")) job->strict_align = 1;
		else if (!strcmp(a, "-fwrapv") || !strcmp(a, "-fno-strict-overflow")) job->wrapv = 1;
		else if (!strcmp(a, "-fno-wrapv") || !strcmp(a, "-fstrict-overflow")) job->wrapv = 0;
		else if (!strcmp(a, "-mgeneral-regs-only")) job->general_regs_only = 1;
		else if (!strcmp(a, "-mno-sse")) job->no_sse = 1;
		else if (!strcmp(a, "-msse")) job->no_sse = 0;
		else if (!strcmp(a, "-mno-sse2")) job->no_sse2 = 1;
		else if (!strcmp(a, "-msse2")) job->no_sse2 = 0;
		else if (!strcmp(a, "-mno-mmx")) job->no_mmx = 1;
		else if (!strcmp(a, "-mmmx")) job->no_mmx = 0;
		else if (!strncmp(a, "-march=", 7) && driver_target_lse_detect(a + 7, false) >= 0) job->march_has_lse = driver_target_lse_detect(a + 7, false);
		else if (!strncmp(a, "-mcpu=", 6) && driver_target_lse_detect(a + 6, true) >= 0) job->mcpu_has_lse = driver_target_lse_detect(a + 6, true);
		else if (!strcmp(a, "-mcmodel=tiny")) job->code_model_tiny = 1;
		else if (!strcmp(a, "-fcf-protection=branch") || !strcmp(a, "-fcf-protection=full") || !strcmp(a, "-fcf-protection")) job->x64_hardening_flags |= 1;
		else if (!strcmp(a, "-fcf-protection=none") || !strcmp(a, "-fcf-protection=return")) job->x64_hardening_flags &= ~1;
		else if (!strcmp(a, "-mfunction-return=thunk-extern")) job->x64_hardening_flags |= 2;
		else if (!strcmp(a, "-mfunction-return=keep")) job->x64_hardening_flags &= ~2;
		else if (!strcmp(a, "-mretpoline-external-thunk")) job->x64_hardening_flags |= 4;
		else if (!strcmp(a, "-mindirect-branch-cs-prefix")) job->x64_hardening_flags |= 8;
		else if (!strncmp(a, "-fpatchable-function-entry=", 27)) {
			job->patchable_entry_count = atoi(a + 27), job->patchable_entry_before = strchr(a + 27, ',') ? atoi(strchr(a + 27, ',') + 1) : 0;
		}
		else if (!strcmp(a, "-fstack-protector")) job->stack_protector = 1;
		else if (!strcmp(a, "-fstack-protector-strong")) job->stack_protector = 2;
		else if (!strcmp(a, "-fstack-protector-all")) job->stack_protector = 3;
		else if (!strcmp(a, "-fno-stack-protector")) job->stack_protector = 0;
		else if (!strcmp(a, "-mstack-protector-guard-reg=gs")) job->stack_protector_guard_gs = 1;
		else if (!strcmp(a, "-mstack-protector-guard-reg=fs")) job->stack_protector_guard_gs = 0;
		else if (!strncmp(a, "-mstack-protector-guard-symbol=", 31)) job->stack_protector_guard_symbol = a + 31;
		else if (!strcmp(a, "-ftrivial-auto-var-init=zero")) job->auto_variables_zero = 1;
		else if (!strcmp(a, "-ftrivial-auto-var-init=uninitialized")) job->auto_variables_zero = 0;
		else if (!strcmp(a, "-"))
			job->main_file = a;	/* stdin, as clang: "<stdin>" in diagnostics and line markers */
		else if (a[0] != '-' && a[0] && (job->language_given || driver_input_is_source(a))) {
			size_t l = strlen(a);

			job->main_file = a;
			if (l > 2 && !strcmp(a + l - 2, ".S"))
				job->asm_mode = 1;
			if (l > 2 && !strcmp(a + l - 2, ".s") && job->raw_assembler < 0)
				job->raw_assembler = 1;	/* clang: .s is assembled without the preprocessor */
			if (!job->language_given) {
				static const char *const cxx_sfx[] = { ".cpp", ".cc", ".cxx", ".c++", ".C", ".hpp", ".hh", ".hxx", ".ii" };

				for (size_t k = 0; k < sizeof cxx_sfx / sizeof *cxx_sfx; k++)
					if (l > strlen(cxx_sfx[k]) && !strcmp(a + l - strlen(cxx_sfx[k]), cxx_sfx[k]))
						job->cxx = job->cxx > 0 ? job->cxx : -1;
			}
		}
	}
#undef DRIVER_OPTION_VALUE
	/* C++ without -std=: clang++'s default, gnu++17; -std=c++NN for a C input: C after all */
	if (job->cxx < 0 || (job->cxx > 0 && !job->cxx_given))
		job->cxx = 17, job->strict_standard = 0;
	if (job->cxx_given && job->cxx > 0 && !job->language_given && job->main_file && job->main_file[0] != '-') {
		size_t l = strlen(job->main_file);
		static const char *const cxx_sfx[] = { ".cpp", ".cc", ".cxx", ".c++", ".C", ".hpp", ".hh", ".hxx", ".ii" };
		int is_cxx = 0;

		for (size_t k = 0; k < sizeof cxx_sfx / sizeof *cxx_sfx; k++)
			is_cxx |= l > strlen(cxx_sfx[k]) && !strcmp(job->main_file + l - strlen(cxx_sfx[k]), cxx_sfx[k]);
		if (!is_cxx)
			job->cxx = 0;	/* clang: -std=c++20 on a .c file is an error; vcc lexes C */
	}
	job->pp_options.cxx = job->cxx;
	return 0;
}

/* an option with a value clang would reject; no input file.  0 to go on; 1: the job ends */
VPRIV int driver_cmdline_check(struct driver_job *job)
{
	if (job->bad_value_option) {
		const char *eq = strchr(job->bad_value_option, '=');

		dprintf(job->errfd, "vcc: error: invalid value '%s' in '%.*s'\n", eq + 1, (int)(eq + 1 - job->bad_value_option), job->bad_value_option);
		job->status = 1;
		return 1;
	}
	if (!job->main_file) {
		dprintf(job->errfd, "vcc: error: no input file\n");
		job->status = 2;
		return 1;
	}
	return 0;
}
