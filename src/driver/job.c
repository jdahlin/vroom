/*
 * job.c: one compile job from a clang command line (ppd_run): parse the options, then run one of
 * four modes (--cmp token comparison, -S/-c compile, assembler source, plain -E) and write the output.
 *
 * Callers: vpp (parity tests) and vccd (server/dispatch.c, compile.c, probe.c, host-programs.c), one
 * job per call and per thread.  The job is thread safe and touches no process state: the working
 * directory, environment and file descriptors come in through ppd_env (ppdriver.h), so under vccd
 * they are the client's.  The only per-thread state is the buffer regions (job-buffer.c) and the
 * object state (output.c: one assembler, one ElfObj, one output buffer per thread).
 *
 * The order in ppd_run1 matters: the option loop (cmdline.c) and its checks come first, defaults
 * that depend on the target (PIE, CFI) are fixed after them, and the search paths and predefined
 * macros are built before pp_open lexes the main file.  A job ends by setting job->status
 * (0 ok, 1 failed, 2 usage error) and jumping to `out`; a full table or an overflow in the
 * preprocessor, assembler or ELF writer longjmps to the job's jmp_buf, so a failed job returns
 * status 1 and the thread goes on with the next.
 *
 *   VPP_TIME=1   report the preprocessing time on the error fd
 */
#include "driver-internal.h"

static double now(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec + ts.tv_nsec * 1e-9;
}

/* a variable of the job's environment: the client's under vccd */
VPRIV const char *ppd_getenv(const ppd_env *env, const char *name)
{
	size_t l = strlen(name);

	if (!env->envp)
		return getenv(name);
	for (char **e = env->envp; *e; e++)
		if (!strncmp(*e, name, l) && (*e)[l] == '=')
			return *e + l + 1;
	return NULL;
}

/* a .s file (or -x assembler): straight to the assembler, as clang */
static void driver_assemble_source(struct driver_job *job, const ppd_env *env)
{
		/* a .s file (or -x assembler): straight to the assembler, as clang */
		char path[4096];
		const char *msg;

		obj_state();
		if (!job->out)
			job->out = obj_name(job->main_file, job->objbuf, sizeof(job->objbuf));
		snprintf(path, sizeof(path), "%s%s%s", job->main_file[0] == '/' || !env->cwd ? "" : env->cwd,
			 job->main_file[0] == '/' || !env->cwd ? "" : "/", job->main_file);
		VAUTOCLOSE int fd = open(path, O_RDONLY);
		struct stat st;

		if (fd < 0 || fstat(fd, &st)) {
			dprintf(job->errfd, "vcc: error: no such file or directory: '%s'\n", job->main_file);
			job->status = 1;
			return;
		}
		/* padded: the lexer reads 16 bytes past the end */
		VAUTOFREE char *t = calloc(1, (size_t)st.st_size + 64);
		ssize_t got = t ? read(fd, t, (size_t)st.st_size) : -1;

		VCLOSE(fd);
		if (got != st.st_size) {
			dprintf(job->errfd, "vcc: error: cannot read '%s'\n", job->main_file);
			job->status = 1;
			return;
		}
		as_include_directory_set(tl_as, env->cwd);
		elf_set_machine(tl_obj, job->x86 ? EM_X86_64 : EM_AARCH64);
		size_t tn = (size_t)got;

		if (job->darwin) {
			/* Apple's dialect, then the Mach-O writer */
			char *d = macho_dialect_translate(t, tn, &tn);

			free(t);
			t = d;
		}
		if (as_run(tl_as, tl_obj, t, tn, job->main_file, &msg)) {
			dprintf(job->errfd, "vcc: error: %s\n", msg);
			job->status = 1;
		} else
			job->status = write_obj(env->cwd, job->out, env->outfd, job->errfd, job->darwin);
		return;
}

/* the first half of pp_opts: the directories' lists, the language switches, the epoch */
static void job_pp_opts_first(struct driver_job *job, const ppd_env *env)
{
	job->po.c23 = job->std == 23;
	job->po.c89 = job->std == 89 && job->strict;
	job->po.no_digraphs = job->po.c89 && !job->stdver;
	job->po.deps = job->mdeps || job->depfile;
	job->po.show_includes = job->showinc;
	job->po.comments = job->comments;
	job->po.dump_defines = job->ddefs;
	job->lnx = !job->darwin && !job->wasm && !job->x86;
	job->po.fatal_continue = !job->emit && !job->objmode && !job->syntax && !job->layout && !job->tokhash;
	job->po.trigraphs = job->tri >= 0 ? job->tri : job->strict && job->std != 23;	/* clang: strict modes before C23 */
	{
		const char *ep = ppd_getenv(env, "SOURCE_DATE_EPOCH");
		char *end;

		if (ep && *ep) {
			job->po.epoch = strtoll(ep, &end, 10);
			job->po.has_epoch = !*end;
		}
	}
}

/* the second half: what follows the predefine text (markers, the main file, diagnostics, -fmacro-prefix-map) */
static void job_pp_opts_second(struct driver_job *job, const ppd_env *env, int argc, char **argv)
{
	job->po.markers = job->nomarkers || job->cmp || !job->po.fatal_continue || job->dmacros ? 0 : job->undef ? 2 : 1;
	job->po.predef_cmdl = (unsigned)job->pre.n;
	if (job->cmdl.n) bput(&job->pre, job->cmdl.p, job->cmdl.n);
	job->po.predef_incl = (unsigned)job->pre.n;
	if (job->imac.n) bput(&job->pre, job->imac.p, job->imac.n);
	if (job->incl.n) bput(&job->pre, job->incl.p, job->incl.n);
	job->po.imacros = job->imac.n != 0;
	bput(&job->pre, "", 1);
	job->po.predef = job->pre.p;
	job->po.main = job->stdin_src ? "<stdin>" : job->main_file;
	job->po.main_src = job->stdin_src, job->po.main_len = job->stdin_len;
	job->po.unsigned_char = job->uchar >= 0 ? job->uchar : job->lnx;
	job->po.cwd = env->cwd;
	job->po.errfd = job->errfd;
	job->po.dsink = env->dsink;
	job->po.diag_color = job->dcolor ? job->dcolor : env->errtty ? 1 : 0;
	job->po.diag_nocaret = job->nocaret;
	job->po.diag_show_id = job->showid;
	job->po.diag_style = job->dstyle;
	job->po.diag_unicode = job->dunicode;
	job->po.diag_format = job->dformat;
	job->po.argv = argv;
	job->po.argc = argc;
	job->po.error_limit = job->elimit;
	job->po.macro_bt_limit = job->mblimit;
	job->po.spell_limit = job->slimit;
	job->po.wargs = job->wargs, job->po.nwargs = job->nwargs;
	job->po.asm_mode = job->asm_mode;
	if (job->pmap) {
		char *eq = strchr(job->pmap, '=');

		if (eq) {
			*eq = 0;
			job->po.prefix_from = job->pmap;
			job->po.prefix_to = eq + 1;
		}
	}
}

/* -fsyntax-only, -S, -c of C: the front end and code generator, then the output files */
static void job_compile(struct driver_job *job, pp *p, const ppd_env *env)
{
		const char *field_uses = ppd_getenv(env, "VCC_FIELD_USES");	/* test aid: frontend/c/field-uses.c */

		if (__builtin_expect(field_uses != NULL, 0)) {
			if (!job->syntax || job->emit || job->layout) {
				dprintf(job->errfd, "vcc: error: VCC_FIELD_USES needs -fsyntax-only\n");
				job->status = 1;
				return;
			}
			if (!getenv("VCC_EAGER") || !getenv("VCC_NODC") || !getenv("VPP_NOHC")) {
				dprintf(job->errfd, "vcc: error: VCC_FIELD_USES needs VCC_EAGER=1 VCC_NODC=1 VPP_NOHC=1\n");
				job->status = 1;
				return;
			}
		}
		cc_opts co = { .target = job->po.target, .unsigned_char = job->po.unsigned_char, .dump_layouts = job->layout, .lint = job->lint, .ms_anon = job->ms_anon, .short_wchar = job->short_wchar, .strict_flex_arrays = job->strict_flex_arrays,
				.std = job->std, .c2y = job->std == 23 && job->stdver && !strcmp(job->stdver, "202400L"), .implicit_int = job->std == 89 || job->wimpl_int, .implicit_fn = job->std == 89 || job->wimpl_fn, .conv_warn = job->conv_warn,
				.strict_std = job->strict,
				.fast_math = job->fast_root4 && (job->fmath & 7) == 7, .no_fp_contract = job->nocontract, .gnu89_inline = job->gnu89_inline, .no_builtin = job->freestanding || job->nobuiltin, .no_jump_tables = job->nojt, .instrument = job->instr, .wasm_sjlj = job->sjlj,
				.x86_features = job->x86_features_, .x86_disabled = job->x86_disabled, .x86_features_set = job->x86, .x86_raw = job->x86_raw, .x86_override_on = job->x86_on, .x86_override_off = job->x86_off,
				.general_regs_only = job->gregs || (job->x86 && (job->nosse || job->nosse2)), .vcc_kernel = job->vcc_kernel, .lse = job->lse, .opt = job->olevel, .wrapv = job->wrapv, .pic = job->pic ? 2 : job->pie ? 1 : 0, .tlsmodel = job->tlsmodel, .sections = job->sections, .debug = job->debug, .cfi = job->cfi > 0, .dbg_name = job->main_file,
				.omit_frame_pointer = job->x86 && (job->omit_frame_pointer < 0 ? job->olevel > 0 : job->omit_frame_pointer),
				.sibling_calls = job->sibling_calls,
				.xcf = job->x86 ? job->xcf : 0, .pfe_n = job->x86 ? job->pfe_n : 0, .pfe_m = job->pfe_m, .sprot = job->x86 ? job->sprot : 0, .sp_gs = job->sp_gs, .sp_sym = job->sp_sym, .autozero = job->x86 ? job->autozero : 0,
			       .outfd = env->outfd };
		int fd = -1;
		char outpath[4096], outtmp[4200];
		bool outrename = false;

		memcpy(co.disabled_builtins, job->disabled_builtins, sizeof job->disabled_builtins);
		co.emit = job->emit;
		co.field_uses = field_uses;
		char dbgdir[4096];
		if (job->debug)
			co.dbg_dir = env->cwd ? env->cwd : getcwd(dbgdir, sizeof dbgdir);
		co.nthreads = getenv("VCC_THREADS") ? atoi(getenv("VCC_THREADS")) : 1;
		co.obj = tl_obj;
		co.as = tl_as;
		co.wasm_obj = &tl_wasm_obj;
		if ((job->layout || job->emit == 1) && job->out && strcmp(job->out, "-")) {
			char path[4096];

			if (job->out[0] != '/' && env->cwd)
				snprintf(path, sizeof(path), "%s/%s", env->cwd, job->out);
			else
				snprintf(path, sizeof(path), "%s", job->out);
			snprintf(outpath, sizeof(outpath), "%s", path);
			outrename = tmp_name(outtmp, sizeof(outtmp), path);
			fd = open(outtmp, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0666);
			if (fd >= 0 && outrename)
				out_mode(fd);
			if (fd < 0) {
				dprintf(job->errfd, "vcc: error: cannot open %s\n", job->out);
				job->status = 1;
				return;
			}
			co.outfd = fd;
		}
		{
			unsigned long long t1 = nsnow();
			vperf v1 = vperformance_now();

			job->status = cc_run(p, &co) || pp_error_count(p) ? 1 : 0;
			ppd_ns_cc += nsnow() - t1;
			ppd_ph_add(PPD_PH_CC, v1);
			ppd_tok_out += pp_token_count(p);
			ppd_tok_skip += pp_skipped_token_count(p);
		}
		if (fd >= 0) {
			if (!job->status && client_gone())
				job->status = 1;	/* (the temporary is removed below) */
			if (close(fd) || (!job->status && outrename && rename(outtmp, outpath))) {
				dprintf(job->errfd, "vcc: error: cannot write %s: %s\n", job->out, strerror(errno));
				job->status = 1;
			}
			if (job->status && outrename)
				unlink(outtmp);
		}
		if (!job->status && job->emit == 2 && PP_IS_WASM(job->po.target))
			job->status = write_out(env->cwd, job->out, env->outfd, tl_wasm_obj.p, tl_wasm_obj.n, job->errfd);
		else if (!job->status && job->emit == 2)
			job->status = write_obj(env->cwd, job->out, env->outfd, job->errfd, job->darwin);
		if (!job->status && job->emit && job->depfile)	/* -S and -c */
			job->status = write_deps(p, env->cwd, job->depfile, job->dtarget, job->dnosys, job->mphony, job->dfirst, NULL, -1, job->errfd);
		if (getenv("VPP_TIME"))
			dprintf(job->errfd, "vcc: %.1f ms\n", (now() - job->t0) * 1e3);
		return;
}

/* -E, -dM, -M, and -c of preprocessed assembler */
static void job_preprocess(struct driver_job *job, pp *p, const ppd_env *env)
{
	buf o = { 0 };

	{
		const char *s;
		size_t l;

		pp_text_render(p, &s, &l);
		o.p = (char *)s;
		o.n = l;
		if (job->dmacros) {
			char *m;

			pp_macros_dump(p, &m, &l);
			o.p = m;	/* freed with the process or the next job: small */
			o.n = l;
		}
	}
	if (getenv("VPP_TIME")) {
		extern _Atomic unsigned long pp_include_lookup_count, pp_include_guard_skip_count, pp_directory_probe_count;

		dprintf(job->errfd, "vpp: %.1f ms, %zu bytes out; include candidates %lu, %lu ruled out by the directory cache (%lu dirs probed)\n",
			(now() - job->t0) * 1e3, o.n, atomic_load(&pp_include_lookup_count), atomic_load(&pp_include_guard_skip_count),
			atomic_load(&pp_directory_probe_count));
	}
	if (pp_error_count(p)) {
		/* clang: -E to stdout still prints the text; with -o it writes no file */
		if (!job->cmp && !job->objmode && !job->mdeps && (!job->out || !strcmp(job->out, "-")))	/* -M: nothing */
			write_out(env->cwd, job->out, env->outfd, o.p, o.n, job->errfd);
		job->status = 1;
	} else if (job->cmp) {
		job->status = ppd_compare(o.p, o.n, job->cmp);
	} else if (job->objmode) {
		const char *msg;

		as_include_directory_set(tl_as, env->cwd);
		elf_set_machine(tl_obj, job->x86 ? EM_X86_64 : EM_AARCH64);	/* (a .S file: preprocessed, then assembled) */
		char *dt = NULL;
		size_t dn = o.n;

		if (job->darwin)
			dt = macho_dialect_translate(o.p, o.n, &dn);
		if (as_run(tl_as, tl_obj, dt ? dt : o.p, dn, job->main_file, &msg)) {
			dprintf(job->errfd, "vcc: error: %s\n", msg);
			job->status = 1;
		} else {
			job->status = write_obj(env->cwd, job->out, env->outfd, job->errfd, job->darwin);
		}
		free(dt);
		if (!job->status && job->depfile)
			job->status = write_deps(p, env->cwd, job->depfile, job->dtarget, job->dnosys, job->mphony, job->dfirst, NULL, -1, job->errfd);
	} else {
		if (job->mdeps)
			job->status = write_deps(p, env->cwd, job->depfile, job->dtarget, job->dnosys, job->mphony, job->dfirst, job->out, env->outfd, job->errfd);
		else
			job->status = write_out(env->cwd, job->out, env->outfd, o.p, o.n, job->errfd);
		if (!job->status && job->depfile && !job->mdeps)
			job->status = write_deps(p, env->cwd, job->depfile, job->dtarget, job->dnosys, job->mphony, job->dfirst, NULL, -1, job->errfd);
	}
}

static __attribute__((noinline)) int ppd_run1(int argc, char **argv, const ppd_env *env);

/* (--explain apart: a call at the top of ppd_run1 costs its token loop registers) */
int ppd_run(int argc, char **argv, const ppd_env *env)
{
	int x = explain(argc, argv, env);

	return x >= 0 ? x : ppd_run1(argc, argv, env);
}

static __attribute__((noinline)) int ppd_run1(int argc, char **argv, const ppd_env *env)
{
	vjob_region *br = bufr = vjob_region_pool_take(&bufpool);
	struct driver_job jobstore = {
		.errfd = env->errfd, .hosted = 1, .raw_asm = -1, .march_lse = -1, .mcpu_lse = -1, .pie = -1, .cfi = -1,
		.x86_cpu = "x86-64", .omit_frame_pointer = -1, .uchar = -1, .tri = -1, .gnu89_inline = -1,
		.pre = { .r = &br[0] }, .cmdl = { .r = &br[1] }, .incl = { .r = &br[2] }, .imac = { .r = &br[3] },
	/* no --target: the host's own userland (Darwin on a Mac, glibc aarch64 or x86-64 on Linux) */
#ifdef __APPLE__
		.darwin = 1,
#endif
#if defined(__x86_64__) && !defined(__APPLE__)
		.x86 = 1,	/* x86_64-linux-gnu (PLAN-x86.md) */
#endif
	}, *job = &jobstore;
	jmp_buf jb;
	pp *p;
	int r;

	driver_searchpath_alloc(job, argc, env);
	r = driver_cmdline_parse(job, argc, argv);
	if (r == 2)
		return 1;
	if (r)
		goto out;
	if (driver_target_features_check(job, argc, argv))
		goto out;
	vjob_fail_fd = job->errfd;	/* (vccd: the client's stderr, not the server's) */
	if (driver_cmdline_check(job))
		goto out;
	if (driver_warning_groups_check(job))
		goto out;
	if (driver_input_read(job, env))
		goto out;
	if (job->emit == 1 && !job->objmode && !job->out && !job->layout) {
		/* clang: -S writes the source's basename with .s */
		job->out = obj_name(job->main_file, job->objbuf, sizeof(job->objbuf));
		job->objbuf[strlen(job->objbuf) - 1] = 's';
	}
	if (job->raw_asm == 1 && !job->objmode) {
		dprintf(job->errfd, "vcc: error: %s: assembler input needs -c\n", job->main_file);
		job->status = 2;
		goto out;
	}
	if (job->raw_asm == 1) {
		driver_assemble_source(job, env);
		goto out;
	}
	if (job->objmode) {
		obj_state();
		if (!job->out)
			job->out = obj_name(job->main_file, job->objbuf, sizeof(job->objbuf));
		if (!job->asm_mode) {
			job->syntax = 1;
			job->emit = 2;
		}
	}
	driver_depfile_names(job);
	/* Linux defaults as clang's: PIE, asynchronous unwind tables (CFI) */
	{
		int hl = !job->darwin;

		if (job->pie < 0)
			job->pie = hl && !job->pic ? 2 : 0;
		if (job->cfi < 0)
			job->cfi = hl && !job->freestanding;
	}
	driver_searchpath_build(job, env);
	job_pp_opts_first(job, env);
	driver_predef_build(job);
	job_pp_opts_second(job, env, argc, argv);

	job->t0 = now();
	{	/* pp_open lexes the main file: a full identifier table there ends the job too
		   (the pp it was opening is lost: a leak, as rare as the table filling) */
		static _Thread_local jmp_buf jo;

		if (setjmp(jo)) {
			vjob_jb = NULL;
			job->status = 1;
			goto out;
		}
		vjob_jb = &jo;
		p = pp_open(&job->po);
		vjob_jb = NULL;
	}
	if (!p) {
		job->status = 1;
		goto out;
	}
	pp_fatal_jump_set(p, &jb);
	if (setjmp(jb)) {
		vjob_jb = NULL;
		job->status = 1;
		goto done;
	}
	vjob_jb = &jb;	/* as/elf overflow ends this TU too */
	if (job->tokhash) {
		driver_tokhash_run(job, p, env);
		goto done;
	}
	if (job->syntax) {
		job_compile(job, p, env);
		goto done;
	}
	job_preprocess(job, p, env);
done:
	vjob_jb = NULL;
	diag_summary_print(p);
	pp_close(p);	/* frees the text in o */
out:
	vjob_fail_fd = 2;
	bfree(&job->pre);
	bfree(&job->cmdl);
	bfree(&job->incl);
	bfree(&job->imac);
	vjob_region_pool_give(&bufpool, br);
	bufr = NULL;
	obj_release();
	return job->status;
}
