/* The option loop: clang's command line syntax into the fields of struct driver_job. */
#include "driver-internal.h"

/* an input file by its suffix, as clang's driver: .c .h .i (C), .S .s (assembly), .dts */
static bool src_suffix(const char *a)
{
	static const char *const sfx[] = { ".c", ".h", ".i", ".S", ".s", ".dts" };
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
#define ARG(flag) (!strcmp(a, flag) ? (i + 1 < argc ? argv[++i] : "") : !strncmp(a, flag, strlen(flag)) ? a + strlen(flag) : NULL)
		const char *v;

		if (!strcmp(a, "--cmp") && i + 1 < argc) { job->cmp = argv[++i]; continue; }
		if (!strcmp(a, "-fsyntax-only")) { job->syntax = 1; continue; }
		if (!strcmp(a, "-S")) { job->syntax = 1; job->emit = 1; continue; }
		if (!strcmp(a, "-c")) { job->objmode = 1; continue; }
		if (!strcmp(a, "-dM")) { job->dmacros = 1; continue; }
		if (!strcmp(a, "-H")) { job->showinc = 1; continue; }
		if (!strcmp(a, "-C")) { job->comments = 1; continue; }
		if (!strcmp(a, "-P")) { job->nomarkers = 1; continue; }
		if (!strcmp(a, "-CC") || !strcmp(a, "-traditional-cpp") || !strcmp(a, "-traditional")) {
			dprintf(job->errfd, "vcc: error: %s is not supported\n", a);	/* TODO.md: output modes */
			job->status = 2;
			return 1;
		}
		if (!strcmp(a, "-dD")) { job->ddefs = 1; continue; }
		if (!strcmp(a, "-fms-anonymous-structs") || !strcmp(a, "-fms-extensions")) { job->ms_anon = 1; continue; }
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
		if (!strcmp(a, "-funsigned-char") || !strcmp(a, "-fno-signed-char")) { job->uchar = 1; continue; }
		if (!strcmp(a, "-fsigned-char") || !strcmp(a, "-fno-unsigned-char")) { job->uchar = 0; continue; }
		if (!strncmp(a, "-ffp-contract=", 14)) { job->nocontract = !strcmp(a + 14, "off"); continue; }
		if (!strcmp(a, "-ffast-math")) { job->fast_root4 = 1; job->fmath = (job->fmath | 39) & ~16; continue; }
		if (!strcmp(a, "-fno-fast-math")) { job->fast_root4 = 0; job->fmath &= 8; continue; }
		if (!strcmp(a, "-ffinite-math-only")) { job->fmath |= 1; continue; }
		if (!strcmp(a, "-fno-finite-math-only")) { job->fmath &= ~5; continue; }
		if (!strcmp(a, "-fno-math-errno")) { job->fmath = (job->fmath | 2) & ~16; continue; }
		if (!strcmp(a, "-fmath-errno")) { job->fmath = (job->fmath & ~6) | 16; continue; }	/* no longer fast-math */
		if (!strcmp(a, "-fno-unsafe-math-optimizations") || !strcmp(a, "-frounding-math") ||
		    !strcmp(a, "-fsigned-zeros") || !strcmp(a, "-ftrapping-math") || !strcmp(a, "-fsignaling-nans") ||
		    !strcmp(a, "-fno-approx-func") || !strcmp(a, "-fno-reciprocal-math") || !strcmp(a, "-fno-associative-math") ||
		    !strcmp(a, "-fhonor-infinities") || !strcmp(a, "-fhonor-nans") ||
		    !strcmp(a, "-ffp-model=strict") || !strcmp(a, "-ffp-model=precise")) {
			job->fast_root4 = 0;
			continue;
		}
		if (!strcmp(a, "-pthread")) { job->fmath |= 8; continue; }	/* (-pthread: _REENTRANT) */
		if (!strcmp(a, "-fcolor-diagnostics") || !strcmp(a, "-fdiagnostics-color") || !strcmp(a, "-fdiagnostics-color=always")) { job->dcolor = 1; continue; }
		if (!strcmp(a, "-fno-color-diagnostics") || !strcmp(a, "-fno-diagnostics-color") || !strcmp(a, "-fdiagnostics-color=never")) { job->dcolor = -1; continue; }
		if (!strcmp(a, "-fdiagnostics-color=auto")) { job->dcolor = 0; continue; }
		if (!strcmp(a, "-fno-caret-diagnostics")) { job->nocaret = 1; continue; }
		if (!strcmp(a, "-fcaret-diagnostics")) { job->nocaret = 0; continue; }
		if (!strcmp(a, "-fdiagnostics-show-id")) { job->showid = 1; continue; }
		if (!strncmp(a, "-fdiagnostics-format=", 21)) {
			const char *f = a + 21;

			job->dformat = !strcmp(f, "json") ? PP_DIAG_JSON : !strcmp(f, "sarif") ? PP_DIAG_SARIF : PP_DIAG_TEXT;
			continue;
		}
		if (!strncmp(a, "-fdiagnostics-add-output=sarif:file=", 36)) { job->po.sarif_file = a + 36; continue; }
		if (!strncmp(a, "-fdiagnostics-add-output=html:file=", 35)) { job->po.html_file = a + 35; continue; }
		if (!strncmp(a, "-fdiagnostics-style=", 20)) {	/* vcc (default), clang, narrated */
			const char *f = a + 20;

			job->dstyle = !strcmp(f, "clang") ? PP_DSTYLE_CLANG : !strcmp(f, "narrated") ? PP_DSTYLE_NARRATED : PP_DSTYLE_VCC;
			if (job->dstyle == PP_DSTYLE_VCC && strcmp(f, "vcc"))
				job->badval = a;
			continue;
		}
		if (!strncmp(a, "-fdiagnostics-text-art-charset=", 31)) {	/* GCC's: ascii, unicode, emoji, none */
			const char *f = a + 31;

			job->dunicode = !strcmp(f, "unicode") || !strcmp(f, "emoji");
			if (!job->dunicode && strcmp(f, "ascii") && strcmp(f, "none"))
				job->badval = a;
			continue;
		}
		if (!strcmp(a, "-fno-diagnostics-show-id")) { job->showid = 0; continue; }
		if (!strcmp(a, "-fdiagnostics-parseable-fixits")) { job->po.diag_parseable = 1; continue; }
		if (!strcmp(a, "-fdiagnostics-generate-patch")) { job->po.diag_patch = 1; continue; }
		if (!strcmp(a, "--fix")) { job->po.fix_apply = 1; continue; }
		if (!strcmp(a, "-fno-diagnostics-parseable-fixits")) { job->po.diag_parseable = 0; continue; }
		if (!strncmp(a, "-fspell-checking-limit=", 23)) { job->slimit = atoi(a + 23); job->slimit = job->slimit ? job->slimit : -1; continue; }
		if (!strcmp(a, "-fno-spell-checking")) { job->slimit = -2; continue; }
		if (!strncmp(a, "-ferror-limit=", 14)) { job->elimit = atoi(a + 14); job->elimit = job->elimit ? job->elimit : -1; continue; }
		if (!strncmp(a, "-fmacro-backtrace-limit=", 24)) { job->mblimit = atoi(a + 24); job->mblimit = job->mblimit ? job->mblimit : -1; continue; }
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
			job->std = stds[k].v;
			job->stdver = stds[k].ver;
			job->strict = strncmp(v, "gnu", 3) != 0;
			continue;
		}
		if (!strcmp(a, "-ansi")) { job->std = 89; job->strict = 1; continue; }
		if (!strcmp(a, "-trigraphs") || !strcmp(a, "-ftrigraphs")) { job->tri = 1; continue; }
		if (!strcmp(a, "-fno-trigraphs")) { job->tri = 0; continue; }
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
			/* warning options, in order, for pp_wstate (some are also handled below);
			   -pedantic is -Wpedantic, -pedantic-errors also -Werror=pedantic (two entries) */
			if (!job->wargs)
				job->wargs = calloc((size_t)argc * 2 + 1, sizeof(*job->wargs));
			if (!strcmp(a, "-pedantic") || !strcmp(a, "--pedantic"))
				job->wargs[job->nwargs++] = "-Wpedantic";
			else if (!strcmp(a, "-pedantic-errors") || !strcmp(a, "--pedantic-errors"))
				job->wargs[job->nwargs++] = "-Wpedantic", job->wargs[job->nwargs++] = "-Werror=pedantic";
			else if (a[1] == 'W' || a[1] == 'w')
				job->wargs[job->nwargs++] = a;
		}
		if (!strcmp(a, "-Wno-implicit-int") || !strcmp(a, "-Wno-error=implicit-int")) { job->wimpl_int = 1; continue; }
		if (!strncmp(a, "-Wno-", 5)) {
			/* errors clang lets -Wno-error= make warnings (pp_wstate has the -W flags) */
			const char *f = a + 5;

			if (!strncmp(f, "error=", 6))
				f += 6;
			job->conv_warn |= !strcmp(f, "int-conversion") ? 1 : !strcmp(f, "incompatible-pointer-types") ? 2 | 4	/* (the function pointer group is part of it) */
				     : !strcmp(f, "incompatible-function-pointer-types") ? 4 : 0;
		}
		if (!strcmp(a, "-Wno-implicit-function-declaration") || !strcmp(a, "-Wno-error=implicit-function-declaration")) {
			job->wimpl_fn = 1;
			continue;
		}
		if (!strcmp(a, "--vcc-layout-checks")) { job->syntax = 1; job->layout = 1; continue; }
		if (!strcmp(a, "--vcc-tokhash")) { job->tokhash = 1; continue; }
		if (!strcmp(a, "-o") && i + 1 < argc) { job->out = argv[++i]; continue; }
		if (!strncmp(a, "-Wp,-MMD,", 9) || !strncmp(a, "-Wp,-MD,", 8)) {
			job->md = a[6] == 'M' ? 2 : 1;
			job->depfile = strchr(a + 4, ',') + 1;
			continue;
		}
		if (!strcmp(a, "-MD") || !strcmp(a, "-MMD")) { job->md = a[2] == 'M' ? 2 : 1; continue; }
		if (!strcmp(a, "-M") || !strcmp(a, "-MM")) { job->mdeps = a[2] == 'M' ? 2 : 1; continue; }
		if (!strcmp(a, "-MP")) { job->mphony = 1; continue; }
		if (!strcmp(a, "-MF") && i + 1 < argc) { job->depfile = argv[++i]; continue; }
		if ((!strcmp(a, "-MT") || !strcmp(a, "-MQ")) && i + 1 < argc) {
			/* targets joined by spaces; -MQ quoted for make: $ -> $$, space -> \ space */
			static __thread char q[4096];
			const char *t = argv[++i];
			size_t k = job->deptarget ? strlen(q) : 0;

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
			job->deptarget = q;
			continue;
		}
		if ((v = ARG("-iquote"))) { job->dirs_q[job->nq++] = v; continue; }
		if ((v = ARG("-isystem"))) { job->dirs_s[job->ns++] = v; continue; }
		if ((v = ARG("-idirafter"))) { job->dirs_a[job->na++] = v; continue; }
		if ((v = ARG("--embed-dir="))) { if (job->ne < 64) job->dirs_e[job->ne++] = v; continue; }
		if ((v = ARG("-imacros"))) {
			/* clang's: the file's macros, its tokens dropped up to the ## (before every -include) */
			bstr(&job->imac, "#__include_macros \"");
			bstr(&job->imac, v);
			bstr(&job->imac, "\"\n##\n");
			continue;
		}
		if ((v = ARG("-include"))) {
			bstr(&job->incl, "#include \"");
			bstr(&job->incl, v);
			bstr(&job->incl, "\"\n");
			continue;
		}
		if ((v = ARG("-I"))) { job->dirs_i[job->ni++] = v; continue; }
		if ((v = ARG("-D"))) {
			if (!strncmp(v, "__KERNEL__", 10) && (!v[10] || v[10] == '='))
				job->vcc_kernel = 1;	/* the kernel: its error() calls rely on clang -O2 folding */
			define_arg(&job->cmdl, v);
			continue;
		}
		if ((v = ARG("-U"))) { bstr(&job->cmdl, "#undef "); bstr(&job->cmdl, v); bstr(&job->cmdl, "\n"); continue; }
		if (!strncmp(a, "-fmacro-prefix-map=", 19)) {
			snprintf(job->pmapbuf, sizeof(job->pmapbuf), "%s", a + 19);
			job->pmap = job->pmapbuf;
			continue;
		}
		if (!strncmp(a, "--target=", 9) || !strcmp(a, "-target")) {
			const char *m = strstr(a, "-macos");

			job->wasm = strstr(a, "wasm64") ? 64 : strstr(a, "wasm32") ? 32 : 0;
#ifndef VCC_WASM
			if (job->wasm) {
				dprintf(job->errfd, "vcc: error: this build has no WebAssembly support (make VCC_WASM=1)\n");
				job->status = 1;
				return 1;
			}
#endif
			job->darwin = !job->wasm && (strstr(a, "darwin") || strstr(a, "apple"));
			job->x86 = !job->wasm && !job->darwin && strstr(a, "x86_64");
			job->hosted = 0;
			if (m && m[6] >= '0' && m[6] <= '9')
				job->macos_min = m + 6, job->macos_min_flag = 2;	/* arm64-apple-macos14.2: wins over the flag */
		}
		else if (!strncmp(a, "-mmacosx-version-min=", 21) || !strncmp(a, "-mmacos-version-min=", 20)) {
			if (job->macos_min_flag < 2)
				job->macos_min = strchr(a, '=') + 1, job->macos_min_flag = 1;
		}
		else if (!strcmp(a, "-nostdinc")) job->nostdinc = 1;
		else if (!strcmp(a, "-undef")) job->undef = 1;
		else if (!strcmp(a, "-x") && i + 1 < argc) {
			i++;
			job->asm_mode = !strcmp(argv[i], "assembler-with-cpp");
			job->raw_asm = !strcmp(argv[i], "assembler") ? 1 : 0;
			job->xlang = strcmp(argv[i], "none") != 0;
		}
		else if (!strcmp(a, "-O0")) job->opt = 0, job->os = 0, job->olevel = 0;
		else if (!strcmp(a, "-Os") || !strcmp(a, "-Oz")) { job->opt = 2; job->os = 1; job->olevel = 2; }
		else if (!strncmp(a, "-O", 2)) job->opt = 2, job->os = 0, job->olevel = !strcmp(a, "-O1") || !strcmp(a, "-Og") ? 1 : 2;
		else if (!strcmp(a, "-fpie")) job->pie = 1, job->pic = 0;
		else if (!strcmp(a, "-fPIE")) job->pie = 2, job->pic = 0;
		else if (!strcmp(a, "-fpic")) job->pic = 1, job->pie = 0;
		else if (!strcmp(a, "-fPIC")) job->pic = 2, job->pie = 0;
		else if (!strcmp(a, "-g") || !strcmp(a, "-g1") || !strcmp(a, "-g2") || !strcmp(a, "-g3") ||
			 !strncmp(a, "-ggdb", 5) || !strncmp(a, "-gdwarf", 7) || !strcmp(a, "-gline-tables-only") ||
			 !strcmp(a, "-glldb") || !strcmp(a, "-gfull"))
			job->debug = 1;	/* line tables (the rest of DWARF: TODO.md) */
		else if (!strcmp(a, "-g0")) job->debug = 0;
		else if (!strcmp(a, "-ffunction-sections")) job->sections |= 1;
		else if (!strcmp(a, "-fno-function-sections")) job->sections &= ~1;
		else if (!strcmp(a, "-fdata-sections")) job->sections |= 2;
		else if (!strcmp(a, "-fno-data-sections")) job->sections &= ~2;
		else if (!strncmp(a, "-ftls-model=", 12)) {
			if (!(job->tlsmodel = vcc_tls_model(a + 12))) {
				fprintf(stderr, "vcc: error: invalid value '%s' in '%s'\n", a + 12, a);
				return 2;	/* (Q-drv-07: the one exit that skips the job's cleanup) */
			}
		}
		else if (!strcmp(a, "-fno-PIE") || !strcmp(a, "-fno-pie")) job->pie = 0;
		else if (!strcmp(a, "-fno-PIC") || !strcmp(a, "-fno-pic")) job->pic = 0, job->pie = 0;
		else if (!strcmp(a, "-ffreestanding")) job->freestanding = 1;
		else if (!strcmp(a, "-fno-builtin")) job->nobuiltin = 1;
		else if (!strcmp(a, "-fno-jump-tables")) job->nojt = 1;
		else if (!strcmp(a, "-fjump-tables")) job->nojt = 0;
		else if (!strncmp(a, "-fno-builtin-", 13)) cc_disable_builtin(job->disabled_builtins, a + 13);
		else if (!strcmp(a, "-mllvm") && i + 1 < argc) {
			/* emcc -sSUPPORT_LONGJMP=wasm: setjmp/longjmp through wasm exceptions */
			if (!strcmp(argv[++i], "-wasm-enable-sjlj"))
				job->sjlj = 1;
		}
		else if (!strcmp(a, "-finstrument-functions")) job->instr = 1;
		else if (!strcmp(a, "-fomit-frame-pointer")) job->omit_frame_pointer = 1;
		else if (!strcmp(a, "-fno-omit-frame-pointer")) job->omit_frame_pointer = 0;
		else if (!strcmp(a, "-foptimize-sibling-calls")) job->sibling_calls = 1;
		else if (!strcmp(a, "-fno-optimize-sibling-calls")) job->sibling_calls = -1;
		else if (!strcmp(a, "-fasynchronous-unwind-tables")) job->cfi = 1;
		else if (!strcmp(a, "-fno-asynchronous-unwind-tables")) job->cfi = 0;
		else if (!strcmp(a, "-mstrict-align")) job->strict_align = 1;
		else if (!strcmp(a, "-fwrapv") || !strcmp(a, "-fno-strict-overflow")) job->wrapv = 1;
		else if (!strcmp(a, "-fno-wrapv") || !strcmp(a, "-fstrict-overflow")) job->wrapv = 0;
		else if (!strcmp(a, "-mgeneral-regs-only")) job->gregs = 1;
		else if (!strcmp(a, "-mno-sse")) job->nosse = 1;
		else if (!strcmp(a, "-msse")) job->nosse = 0;
		else if (!strcmp(a, "-mno-sse2")) job->nosse2 = 1;
		else if (!strcmp(a, "-msse2")) job->nosse2 = 0;
		else if (!strcmp(a, "-mno-mmx")) job->nommx = 1;
		else if (!strcmp(a, "-mmmx")) job->nommx = 0;
		else if (!strncmp(a, "-march=", 7) && arch_lse(a + 7, false) >= 0) job->march_lse = arch_lse(a + 7, false);
		else if (!strncmp(a, "-mcpu=", 6) && arch_lse(a + 6, true) >= 0) job->mcpu_lse = arch_lse(a + 6, true);
		else if (!strcmp(a, "-mcmodel=tiny")) job->tiny = 1;
		else if (!strcmp(a, "-fcf-protection=branch") || !strcmp(a, "-fcf-protection=full") || !strcmp(a, "-fcf-protection")) job->xcf |= 1;
		else if (!strcmp(a, "-fcf-protection=none") || !strcmp(a, "-fcf-protection=return")) job->xcf &= ~1;
		else if (!strcmp(a, "-mfunction-return=thunk-extern")) job->xcf |= 2;
		else if (!strcmp(a, "-mfunction-return=keep")) job->xcf &= ~2;
		else if (!strcmp(a, "-mretpoline-external-thunk")) job->xcf |= 4;
		else if (!strcmp(a, "-mindirect-branch-cs-prefix")) job->xcf |= 8;
		else if (!strncmp(a, "-fpatchable-function-entry=", 27)) {
			job->pfe_n = atoi(a + 27), job->pfe_m = strchr(a + 27, ',') ? atoi(strchr(a + 27, ',') + 1) : 0;
		}
		else if (!strcmp(a, "-fstack-protector")) job->sprot = 1;
		else if (!strcmp(a, "-fstack-protector-strong")) job->sprot = 2;
		else if (!strcmp(a, "-fstack-protector-all")) job->sprot = 3;
		else if (!strcmp(a, "-fno-stack-protector")) job->sprot = 0;
		else if (!strcmp(a, "-mstack-protector-guard-reg=gs")) job->sp_gs = 1;
		else if (!strcmp(a, "-mstack-protector-guard-reg=fs")) job->sp_gs = 0;
		else if (!strncmp(a, "-mstack-protector-guard-symbol=", 31)) job->sp_sym = a + 31;
		else if (!strcmp(a, "-ftrivial-auto-var-init=zero")) job->autozero = 1;
		else if (!strcmp(a, "-ftrivial-auto-var-init=uninitialized")) job->autozero = 0;
		else if (!strcmp(a, "-"))
			job->main_file = a;	/* stdin, as clang: "<stdin>" in diagnostics and line markers */
		else if (a[0] != '-' && a[0] && (job->xlang || src_suffix(a))) {
			size_t l = strlen(a);

			job->main_file = a;
			if (l > 2 && !strcmp(a + l - 2, ".S"))
				job->asm_mode = 1;
			if (l > 2 && !strcmp(a + l - 2, ".s") && job->raw_asm < 0)
				job->raw_asm = 1;	/* clang: .s is assembled without the preprocessor */
		}
	}
#undef ARG
	return 0;
}

/* an option with a value clang would reject; no input file.  0 to go on; 1: the job ends */
VPRIV int driver_cmdline_check(struct driver_job *job)
{
	if (job->badval) {
		const char *eq = strchr(job->badval, '=');

		dprintf(job->errfd, "vcc: error: invalid value '%s' in '%.*s'\n", eq + 1, (int)(eq + 1 - job->badval), job->badval);
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
