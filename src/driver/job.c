/*
 * job.c: one compile job from a clang command line (driver_job_run): parse the options, then run one of
 * four modes (--cmp token comparison, -S/-c compile, assembler source, plain -E) and write the output.
 *
 * Callers: vpp (parity tests) and vccd (server/dispatch.c, compile.c, probe.c, host-programs.c), one
 * job per call and per thread.  The job is thread safe and touches no process state: the working
 * directory, environment and file descriptors come in through driver_environment (driver.h), so under vccd
 * they are the client's.  The only per-thread state is the buffer regions (job-buffer.c) and the
 * object state (output.c: one assembler, one ElfObj, one output buffer per thread).
 *
 * The order in driver_job_run_one matters: the option loop (cmdline.c) and its checks come first, defaults
 * that depend on the target (PIE, CFI) are fixed after them, and the search paths and predefined
 * macros are built before pp_open lexes the main file.  A job ends by setting job->status
 * (0 ok, 1 failed, 2 usage error) and jumping to `out`; a full table or an overflow in the
 * preprocessor, assembler or ELF writer longjmps to the job's jmp_buf, so a failed job returns
 * status 1 and the thread goes on with the next.
 *
 *   VPP_TIME=1   report the preprocessing time on the error fd
 */
#include "driver-internal.h"

/* -g's compilation directory: the client's (vccd) or the process' */
static const char *driver_debug_directory(const driver_environment *env)
{
	static _Thread_local char cwd[4096];

	return env->cwd ? env->cwd : getcwd(cwd, sizeof cwd);
}

static double driver_clock_seconds(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec + ts.tv_nsec * 1e-9;
}

/* a variable of the job's environment: the client's under vccd */
VPRIV const char *driver_environment_get(const driver_environment *env, const char *name)
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
static void driver_source_assemble(struct driver_job *job, const driver_environment *env)
{
		/* a .s file (or -x assembler): straight to the assembler, as clang */
		char path[4096];
		const char *msg;

		driver_object_state_acquire();
		if (!job->output_file)
			job->output_file = driver_object_name(job->main_file, job->object_name_buffer, sizeof(job->object_name_buffer));
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
		as_include_directory_set(driver_assembler, env->cwd);
		as_dwarf_version_set(driver_assembler, job->dwarf_version);
		as_debug_directory_set(driver_assembler, job->debug ? driver_debug_directory(env) : NULL);
		elf_set_machine(driver_object, job->target_x64 ? EM_X86_64 : EM_AARCH64);
		obj_set_format(driver_object, job->target_win64 ? OBJ_COFF : OBJ_ELF);
		size_t tn = (size_t)got;

		if (job->darwin) {
			/* Apple's dialect, then the Mach-O writer */
			char *d = macho_dialect_translate(t, tn, &tn);

			free(t);
			t = d;
		}
		if (as_run(driver_assembler, driver_object, t, tn, job->main_file, &msg)) {
			dprintf(job->errfd, "vcc: error: %s\n", msg);
			job->status = 1;
		} else
			job->status = driver_object_write(env->cwd, job->output_file, env->outfd, job->errfd, job->darwin);
		return;
}

/* the first half of pp_opts: the directories' lists, the language switches, the epoch */
static void driver_job_pp_options_prepare(struct driver_job *job, const driver_environment *env)
{
	job->pp_options.c23 = job->standard == 23;
	job->pp_options.c89 = job->standard == 89 && job->strict_standard;
	job->pp_options.no_digraphs = job->pp_options.c89 && !job->standard_version;
	job->pp_options.deps = job->dependency_print_mode || job->depfile;
	job->pp_options.show_includes = job->show_includes;
	job->pp_options.comments = job->comments;
	job->pp_options.dump_defines = job->dump_defines;
	job->target_linux = !job->darwin && !job->wasm_bits && !job->target_x64;
	job->pp_options.fatal_continue = !job->emit_assembly && !job->object_mode && !job->syntax_only && !job->layout_checks && !job->token_hash;
	job->pp_options.trigraphs = job->trigraphs >= 0 ? job->trigraphs : job->strict_standard && job->standard != 23;	/* clang: strict modes before C23 */
	{
		const char *ep = driver_environment_get(env, "SOURCE_DATE_EPOCH");
		char *end;

		if (ep && *ep) {
			job->pp_options.epoch = strtoll(ep, &end, 10);
			job->pp_options.has_epoch = !*end;
		}
	}
}

/* the second half: what follows the predefine text (markers, the main file, diagnostics, -fmacro-prefix-map) */
static void driver_job_pp_options_finish(struct driver_job *job, const driver_environment *env, int argc, char **argv)
{
	job->pp_options.markers = job->no_line_markers || job->compare_file || !job->pp_options.fatal_continue || job->dump_macros ? 0 : job->undefine_predefined ? 2 : 1;
	job->pp_options.predef_cmdl = (unsigned)job->predefines.length;
	if (job->target_win64 && job->unicode)	/* clang's driver: -municode is -DUNICODE, before the user's -D */
		driver_buffer_append_string(&job->predefines, "#define UNICODE 1\n");
	if (job->command_line_defines.length) driver_buffer_append(&job->predefines, job->command_line_defines.data, job->command_line_defines.length);
	job->pp_options.predef_incl = (unsigned)job->predefines.length;
	if (job->include_macros_text.length) driver_buffer_append(&job->predefines, job->include_macros_text.data, job->include_macros_text.length);
	if (job->include_text.length) driver_buffer_append(&job->predefines, job->include_text.data, job->include_text.length);
	job->pp_options.imacros = job->include_macros_text.length != 0;
	driver_buffer_append(&job->predefines, "", 1);
	job->pp_options.predef = job->predefines.data;
	job->pp_options.main = job->stdin_src ? "<stdin>" : job->main_file;
	job->pp_options.main_src = job->stdin_src, job->pp_options.main_len = job->stdin_len;
	job->pp_options.unsigned_char = job->unsigned_char >= 0 ? job->unsigned_char : job->target_linux;
	job->pp_options.cwd = env->cwd;
	job->pp_options.errfd = job->errfd;
	job->pp_options.dsink = env->diagnostic_sink;
	job->pp_options.diag_color = job->diagnostic_color ? job->diagnostic_color : env->error_is_terminal ? 1 : 0;
	job->pp_options.diag_nocaret = job->no_caret;
	job->pp_options.diag_show_id = job->show_diagnostic_id;
	job->pp_options.diag_style = job->diagnostic_style;
	job->pp_options.diag_unicode = job->diagnostic_unicode;
	job->pp_options.diag_format = job->diagnostic_format;
	job->pp_options.argv = argv;
	job->pp_options.argc = argc;
	job->pp_options.error_limit = job->error_limit;
	job->pp_options.macro_bt_limit = job->macro_backtrace_limit;
	job->pp_options.spell_limit = job->spell_checking_limit;
	job->pp_options.wargs = job->warning_arguments, job->pp_options.nwargs = job->warning_argument_count;
	job->pp_options.asm_mode = job->asm_mode;
	if (job->prefix_map) {
		char *eq = strchr(job->prefix_map, '=');

		if (eq) {
			*eq = 0;
			job->pp_options.prefix_from = job->prefix_map;
			job->pp_options.prefix_to = eq + 1;
		}
	}
}

/* -fsyntax-only, -S, -c of C: the front end and code generator, then the output files */
static void driver_job_compile(struct driver_job *job, pp *p, const driver_environment *env)
{
		const char *field_uses = driver_environment_get(env, "VCC_FIELD_USES");	/* test aid: frontend/c/field-uses.c */

		if (__builtin_expect(field_uses != NULL, 0)) {
			if (!job->syntax_only || job->emit_assembly || job->layout_checks) {
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
		cc_options co = { .target = job->pp_options.target, .unsigned_char = job->pp_options.unsigned_char, .dump_layouts = job->layout_checks, .lint = job->lint, .ms_anon = job->ms_anonymous_structs, .short_wchar = job->short_wchar || job->target_win64,
				.ms_bitfields = job->ms_bitfields >= 0 ? job->ms_bitfields : job->target_win64, .strict_flex_arrays = job->strict_flex_arrays,
				.std = job->standard, .c2y = job->standard == 23 && job->standard_version && !strcmp(job->standard_version, "202400L"), .implicit_int = job->standard == 89 || job->allow_implicit_int, .implicit_fn = job->standard == 89 || job->allow_implicit_function, .conv_warn = job->conversion_warning_flags,
				.strict_std = job->strict_standard,
				.fast_math = job->fast_math && (job->math_flags & 7) == 7, .no_fp_contract = job->no_fp_contract, .gnu89_inline = job->gnu89_inline, .no_builtin = job->freestanding || job->no_builtin, .no_jump_tables = job->no_jump_tables, .instrument = job->instrument_functions, .wasm_sjlj = job->sjlj,
				.x86_features = job->x64_features, .x86_disabled = job->x64_features_disabled, .x86_features_set = job->target_x64, .x86_raw = job->x64_features_raw, .x86_override_on = job->x64_features_forced_on, .x86_override_off = job->x64_features_forced_off,
				.general_regs_only = job->general_regs_only || (job->target_x64 && (job->no_sse || job->no_sse2)), .vcc_kernel = job->vcc_kernel, .lse = job->lse, .opt = job->optimization_level, .wrapv = job->wrapv, .pic = job->target_win64 ? 0 : job->pic ? 2 : job->pie ? 1 : 0,	/* (Windows: no GOT; PE images are relocated) */ .tlsmodel = job->tls_model, .sections = job->function_data_sections, .debug = job->debug, .dbg_version = job->dwarf_version, .cfi = job->cfi > 0, .no_seh = job->no_seh, .dbg_name = job->main_file,
				.omit_frame_pointer = job->target_x64 && (job->omit_frame_pointer < 0 ? job->optimization_level > 0 : job->omit_frame_pointer),
				.sibling_calls = job->sibling_calls,
				.xcf = job->target_x64 ? job->x64_hardening_flags : 0, .pfe_n = job->target_x64 ? job->patchable_entry_count : 0, .pfe_m = job->patchable_entry_before, .sprot = job->target_x64 ? job->stack_protector : 0, .sp_gs = job->stack_protector_guard_gs, .sp_sym = job->stack_protector_guard_symbol, .autozero = job->target_x64 ? job->auto_variables_zero : 0,
			       .outfd = env->outfd };
		int fd = -1;
		char outpath[4096], outtmp[4200];
		bool outrename = false;

		memcpy(co.disabled_builtins, job->disabled_builtins, sizeof job->disabled_builtins);
		co.emit = job->emit_assembly;
		co.field_uses = field_uses;
		char dbgdir[4096];
		if (job->debug)
			co.dbg_dir = env->cwd ? env->cwd : getcwd(dbgdir, sizeof dbgdir);
		co.nthreads = getenv("VCC_THREADS") ? atoi(getenv("VCC_THREADS")) : 1;
		co.obj = driver_object;
		co.as = driver_assembler;
		co.wasm_obj = &driver_wasm_object;
		if ((job->layout_checks || job->emit_assembly == 1) && job->output_file && strcmp(job->output_file, "-")) {
			char path[4096];

			if (job->output_file[0] != '/' && env->cwd)
				snprintf(path, sizeof(path), "%s/%s", env->cwd, job->output_file);
			else
				snprintf(path, sizeof(path), "%s", job->output_file);
			snprintf(outpath, sizeof(outpath), "%s", path);
			outrename = driver_output_temporary_name(outtmp, sizeof(outtmp), path);
			fd = open(outtmp, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0666);
			if (fd >= 0 && outrename)
				driver_output_mode_set(fd);
			if (fd < 0) {
				dprintf(job->errfd, "vcc: error: cannot open %s\n", job->output_file);
				job->status = 1;
				return;
			}
			co.outfd = fd;
		}
		{
			unsigned long long t1 = driver_clock_nanoseconds();
			vperf v1 = vperformance_now();

			job->status = cc_translation_unit_run(p, &co) || pp_error_count(p) ? 1 : 0;
			driver_compile_nanoseconds += driver_clock_nanoseconds() - t1;
			driver_phase_add(DRIVER_PHASE_COMPILE, v1);
			driver_tokens_taken += pp_token_count(p);
			driver_tokens_skipped += pp_skipped_token_count(p);
		}
		if (fd >= 0) {
			if (!job->status && driver_client_is_gone())
				job->status = 1;	/* (the temporary is removed below) */
			if (close(fd) || (!job->status && outrename && rename(outtmp, outpath))) {
				dprintf(job->errfd, "vcc: error: cannot write %s: %s\n", job->output_file, strerror(errno));
				job->status = 1;
			}
			if (job->status && outrename)
				unlink(outtmp);
		}
		if (!job->status && job->emit_assembly == 2 && PP_IS_WASM(job->pp_options.target))
			job->status = driver_output_write(env->cwd, job->output_file, env->outfd, driver_wasm_object.p, driver_wasm_object.n, job->errfd);
		else if (!job->status && job->emit_assembly == 2)
			job->status = driver_object_write(env->cwd, job->output_file, env->outfd, job->errfd, job->darwin);
		if (!job->status && job->emit_assembly && job->depfile)	/* -S and -c */
			job->status = driver_depfile_write(p, env->cwd, job->depfile, job->depfile_target, job->depfile_omit_system, job->dependency_phony, job->depfile_first_input, NULL, -1, job->errfd);
		if (getenv("VPP_TIME"))
			dprintf(job->errfd, "vcc: %.1f ms\n", (driver_clock_seconds() - job->start_time) * 1e3);
		return;
}

/* -E, -dM, -M, and -c of preprocessed assembler */
static void driver_job_preprocess(struct driver_job *job, pp *p, const driver_environment *env)
{
	driver_buffer o = { 0 };

	{
		const char *s;
		size_t l;

		pp_text_render(p, &s, &l);
		o.data = (char *)s;
		o.length = l;
		if (job->dump_macros) {
			char *m;

			pp_macros_dump(p, &m, &l);
			o.data = m;	/* freed with the process or the next job: small */
			o.length = l;
		}
	}
	if (getenv("VPP_TIME")) {
		extern _Atomic unsigned long pp_include_lookup_count, pp_include_guard_skip_count, pp_directory_probe_count;

		dprintf(job->errfd, "vpp: %.1f ms, %zu bytes out; include candidates %lu, %lu ruled out by the directory cache (%lu dirs probed)\n",
			(driver_clock_seconds() - job->start_time) * 1e3, o.length, atomic_load(&pp_include_lookup_count), atomic_load(&pp_include_guard_skip_count),
			atomic_load(&pp_directory_probe_count));
	}
	if (pp_error_count(p)) {
		/* clang: -E to stdout still prints the text; with -o it writes no file */
		if (!job->compare_file && !job->object_mode && !job->dependency_print_mode && (!job->output_file || !strcmp(job->output_file, "-")))	/* -M: nothing */
			driver_output_write(env->cwd, job->output_file, env->outfd, o.data, o.length, job->errfd);
		job->status = 1;
	} else if (job->compare_file) {
		job->status = driver_tokens_compare(o.data, o.length, job->compare_file);
	} else if (job->object_mode) {
		const char *msg;

		as_include_directory_set(driver_assembler, env->cwd);
		as_dwarf_version_set(driver_assembler, job->dwarf_version);
		as_debug_directory_set(driver_assembler, job->debug ? driver_debug_directory(env) : NULL);
		elf_set_machine(driver_object, job->target_x64 ? EM_X86_64 : EM_AARCH64);	/* (a .S file: preprocessed, then assembled) */
		obj_set_format(driver_object, job->target_win64 ? OBJ_COFF : OBJ_ELF);
		char *dt = NULL;
		size_t dn = o.length;

		if (job->darwin)
			dt = macho_dialect_translate(o.data, o.length, &dn);
		if (as_run(driver_assembler, driver_object, dt ? dt : o.data, dn, job->main_file, &msg)) {
			dprintf(job->errfd, "vcc: error: %s\n", msg);
			job->status = 1;
		} else {
			job->status = driver_object_write(env->cwd, job->output_file, env->outfd, job->errfd, job->darwin);
		}
		free(dt);
		if (!job->status && job->depfile)
			job->status = driver_depfile_write(p, env->cwd, job->depfile, job->depfile_target, job->depfile_omit_system, job->dependency_phony, job->depfile_first_input, NULL, -1, job->errfd);
	} else {
		if (job->dependency_print_mode)
			job->status = driver_depfile_write(p, env->cwd, job->depfile, job->depfile_target, job->depfile_omit_system, job->dependency_phony, job->depfile_first_input, job->output_file, env->outfd, job->errfd);
		else
			job->status = driver_output_write(env->cwd, job->output_file, env->outfd, o.data, o.length, job->errfd);
		if (!job->status && job->depfile && !job->dependency_print_mode)
			job->status = driver_depfile_write(p, env->cwd, job->depfile, job->depfile_target, job->depfile_omit_system, job->dependency_phony, job->depfile_first_input, NULL, -1, job->errfd);
	}
}

static __attribute__((noinline)) int driver_job_run_one(int argc, char **argv, const driver_environment *env);

/* (--explain apart: a call at the top of driver_job_run_one costs its token loop registers) */
int driver_job_run(int argc, char **argv, const driver_environment *env)
{
	int x = driver_explain_run(argc, argv, env);

	return x >= 0 ? x : driver_job_run_one(argc, argv, env);
}

static __attribute__((noinline)) int driver_job_run_one(int argc, char **argv, const driver_environment *env)
{
	vjob_region *br = driver_buffer_region = vjob_region_pool_take(&driver_buffer_pool);
	struct driver_job jobstore = {
		.errfd = env->errfd, .hosted = 1, .ms_bitfields = -1, .raw_assembler = -1, .march_has_lse = -1, .mcpu_has_lse = -1, .pie = -1, .cfi = -1, .unwind_tables = -1,
		.x64_cpu = "x86-64", .omit_frame_pointer = -1, .unsigned_char = -1, .trigraphs = -1, .gnu89_inline = -1,
		.predefines = { .region = &br[0] }, .command_line_defines = { .region = &br[1] }, .include_text = { .region = &br[2] }, .include_macros_text = { .region = &br[3] },
	/* no --target: the host's own userland (Darwin on a Mac, glibc aarch64 or x86-64 on Linux) */
#ifdef __APPLE__
		.darwin = 1,
#endif
#if defined(__x86_64__) && !defined(__APPLE__)
		.target_x64 = 1,	/* x86_64-linux-gnu (PLAN-x86.md) */
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
	if (job->emit_assembly == 1 && !job->object_mode && !job->output_file && !job->layout_checks) {
		/* clang: -S writes the source's basename with .s */
		job->output_file = driver_object_name(job->main_file, job->object_name_buffer, sizeof(job->object_name_buffer));
		job->object_name_buffer[strlen(job->object_name_buffer) - 1] = 's';
	}
	if (job->raw_assembler == 1 && !job->object_mode) {
		dprintf(job->errfd, "vcc: error: %s: assembler input needs -c\n", job->main_file);
		job->status = 2;
		goto out;
	}
	if (job->raw_assembler == 1) {
		driver_source_assemble(job, env);
		goto out;
	}
	if (job->object_mode) {
		driver_object_state_acquire();
		if (!job->output_file)
			job->output_file = driver_object_name(job->main_file, job->object_name_buffer, sizeof(job->object_name_buffer));
		if (!job->asm_mode) {
			job->syntax_only = 1;
			job->emit_assembly = 2;
		}
	}
	driver_depfile_names(job);
	/* Linux defaults as clang's: PIE, asynchronous unwind tables (CFI); Windows has neither (its
	   unwind tables are SEH's, PLAN-win64.md WIN3) */
	{
		int hl = !job->darwin && !job->target_win64;

		/* Windows: SEH unless -fno-asynchronous-unwind-tables without -funwind-tables (clang) */
		job->no_seh = job->target_win64 && job->cfi == 0 && job->unwind_tables != 1;
		if (job->pie < 0)
			job->pie = hl && !job->pic ? 2 : 0;
		if (job->cfi < 0)
			job->cfi = hl && !job->freestanding;
	}
	driver_searchpath_build(job, env);
	driver_job_pp_options_prepare(job, env);
	driver_predef_build(job);
	driver_job_pp_options_finish(job, env, argc, argv);

	job->start_time = driver_clock_seconds();
	{	/* pp_open lexes the main file: a full identifier table there ends the job too
		   (the pp it was opening is lost: a leak, as rare as the table filling) */
		static _Thread_local jmp_buf jo;

		if (setjmp(jo)) {
			vjob_jb = NULL;
			job->status = 1;
			goto out;
		}
		vjob_jb = &jo;
		p = pp_open(&job->pp_options);
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
	if (job->token_hash) {
		driver_tokens_hash_run(job, p, env);
		goto done;
	}
	if (job->syntax_only) {
		driver_job_compile(job, p, env);
		goto done;
	}
	driver_job_preprocess(job, p, env);
done:
	vjob_jb = NULL;
	diag_summary_print(p);
	pp_close(p);	/* frees the text in o */
out:
	vjob_fail_fd = 2;
	driver_buffer_free(&job->predefines);
	driver_buffer_free(&job->command_line_defines);
	driver_buffer_free(&job->include_text);
	driver_buffer_free(&job->include_macros_text);
	vjob_region_pool_give(&driver_buffer_pool, br);
	driver_buffer_region = NULL;
	driver_object_state_release();
	return job->status;
}
