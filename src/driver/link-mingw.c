/*
 * link-mingw.c: `vcc x.c -o x.exe` for x86_64-w64-mingw32 (PLAN-win64.md WIN5/WIN8): compile each
 * source with driver_job_run to a temporary object next to the output, then link with vlink and the
 * line clang's MinGW toolchain gives ld.lld (`clang -###`), with vcc's run-time library libvccrt.a where
 * clang has compiler-rt's builtins, and never libgcc or libunwind.
 *
 *   vlink --sysroot=S -m i386pep [--shared] [--subsystem windows|console] -Bstatic|-Bdynamic
 *         [-e DllMainCRTStartup --enable-auto-image-base] -o OUT
 *         S/x86_64-w64-mingw32/lib/{crt2.o,crt2u.o,dllcrt2.o} S/.../crtbegin.o
 *         -L(user) -LS/x86_64-w64-mingw32/lib -LS/x86_64-w64-mingw32/mingw/lib -LS/lib
 *         OBJECTS, -l, -Wl, and -Xlinker in command-line order
 *         [-lmingwthrd] -lmingw32 libvccrt.a -lmoldname -lmingwex -lmsvcrt [-lpthread] [-lgdi32 -lcomdlg32]
 *         -ladvapi32 -lshell32 -luser32 -lkernel32 (static: in --start-group/--end-group; dynamic:
 *         then [-lmingwthrd] -lmingw32 libvccrt.a -lmoldname -lmingwex -lmsvcrt -lkernel32 again)
 *         S/x86_64-w64-mingw32/lib/crtend.o
 *
 * -nostdlib drops both the start files and the libraries, -nodefaultlibs the libraries,
 * -nostartfiles the start files; -v prints the line.  libvccrt.a is VCC_VCCRT, else vccrt/libvccrt.a beside the running
 * program (make -C src vccrt puts it there).  The linker itself is the caller's: vpp spawns the vlink
 * beside it, vccd calls ld_main in its own process.
 */
#include "driver-internal.h"

#include <limits.h>

/* the triple says Windows x64 through MinGW-w64 */
static bool driver_mingw_triple(const char *t)
{
	return !strncmp(t, "x86_64-", 7) && (strstr(t, "-mingw32") || strstr(t, "-windows-gnu"));
}

/* compile options whose value is the next argument */
static bool driver_mingw_option_takes_value(const char *a)
{
	static const char *const v[] = {
		"-o", "-I", "-D", "-U", "-include", "-imacros", "-isystem", "-idirafter", "-iquote", "-iprefix",
		"-iwithprefix", "-isysroot", "-MF", "-MT", "-MQ", "-x", "-Xclang", "-Xassembler", "-Xpreprocessor",
		"-mllvm", "--param", "-arch", "-target", "-include-pch", "--sysroot", "-L", "-l", "-Xlinker", "-e",
		"-u",
	};

	for (size_t k = 0; k < sizeof v / sizeof *v; k++)
		if (!strcmp(a, v[k]))
			return true;
	return false;
}

static bool driver_mingw_is_source(const char *a)
{
	size_t n = strlen(a);

	return !strcmp(a, "-") || (n > 2 && a[n - 2] == '.' && (a[n - 1] == 'c' || a[n - 1] == 'S' || a[n - 1] == 's' || a[n - 1] == 'i'));
}

bool driver_mingw_link_wanted(int argc, char **argv)
{
	bool mingw = false, inputs = false;

	for (int i = 1; i < argc; i++) {
		const char *a = argv[i];

		if (!strncmp(a, "--target=", 9))
			mingw = driver_mingw_triple(a + 9);
		else if ((!strcmp(a, "-target") || !strcmp(a, "--target")) && i + 1 < argc)
			mingw = driver_mingw_triple(argv[++i]);
		else if (!strcmp(a, "-c") || !strcmp(a, "-S") || !strcmp(a, "-E") || !strcmp(a, "-fsyntax-only") ||
			 !strcmp(a, "-M") || !strcmp(a, "-MM") || !strcmp(a, "--cmp") || !strncmp(a, "--vcc-", 6) ||
			 !strncmp(a, "--explain", 9) || !strcmp(a, "--fix") || !strncmp(a, "-print-", 7) ||
			 !strcmp(a, "-dumpmachine") || !strcmp(a, "-###"))
			return false;
		else if (a[0] == '-' && a[1]) {
			if (driver_mingw_option_takes_value(a))
				i++;
		} else
			inputs = true;
	}
	return mingw && inputs;
}

/* libvccrt.a: VCC_VCCRT, else vccrt/libvccrt.a beside the running program */
static bool driver_mingw_runtime_find(const driver_environment *env, char *path, size_t cap)
{
	const char *v = NULL;

	for (char **e = env->envp; e && *e; e++)
		if (!strncmp(*e, "VCC_VCCRT=", 10))
			v = *e + 10;
	if (!env->envp)
		v = getenv("VCC_VCCRT");
	if (v && *v) {
		snprintf(path, cap, "%s", v);
		return true;
	}
	char self[PATH_MAX];
	ssize_t n = readlink("/proc/self/exe", self, sizeof self - 1);

	if (n <= 0)
		return false;
	self[n] = 0;
	char *slash = strrchr(self, '/');

	if (!slash)
		return false;
	*slash = 0;
	snprintf(path, cap, "%s/vccrt/libvccrt.a", self);
	return access(path, R_OK) == 0;
}

int driver_mingw_link_run(int argc, char **argv, const driver_environment *env, driver_linker_function *link, void *ctx)
{
	const char *out = "a.exe", *sysroot = NULL;
	bool shared = false, static_ = false, nostdlib = false, nodefaultlibs = false, nostartfiles = false;
	bool unicode = false, threads = false, pthread = false, strip = false, verbose = false;
	const char *subsystem = NULL;
	int nsrc = 0, errfd = env->errfd >= 0 ? env->errfd : 2;

	for (int i = 1; i < argc; i++) {
		const char *a = argv[i];

		if (!strcmp(a, "-o") && i + 1 < argc)
			out = argv[++i];
		else if (!strncmp(a, "--sysroot=", 10))
			sysroot = a + 10;
		else if (!strcmp(a, "--sysroot") && i + 1 < argc)
			sysroot = argv[++i];
		else if (!strcmp(a, "-shared") || !strcmp(a, "-mdll"))
			shared = true;
		else if (!strcmp(a, "-static"))
			static_ = true;
		else if (!strcmp(a, "-nostdlib"))
			nostdlib = true;
		else if (!strcmp(a, "-nodefaultlibs"))
			nodefaultlibs = true;
		else if (!strcmp(a, "-nostartfiles"))
			nostartfiles = true;
		else if (!strcmp(a, "-municode"))
			unicode = true;
		else if (!strcmp(a, "-mthreads"))
			threads = true;
		else if (!strcmp(a, "-pthread"))
			pthread = true;
		else if (!strcmp(a, "-s"))
			strip = true;
		else if (!strcmp(a, "-v"))
			verbose = true;
		else if (!strcmp(a, "-mwindows"))
			subsystem = "windows";
		else if (!strcmp(a, "-mconsole"))
			subsystem = "console";
		else if (a[0] == '-' && a[1]) {
			if (driver_mingw_option_takes_value(a))
				i++;
		} else if (driver_mingw_is_source(a))
			nsrc++;
	}
	if (!sysroot || !*sysroot) {
		dprintf(errfd, "vcc: error: linking for x86_64-w64-mingw32 needs --sysroot (the MinGW-w64 headers, CRT and import libraries)\n");
		return 1;
	}
	char rt[PATH_MAX];

	if (!nostdlib && !nodefaultlibs && !driver_mingw_runtime_find(env, rt, sizeof rt)) {
		dprintf(errfd, "vcc: error: libvccrt.a not found (make -C src vccrt, or VCC_VCCRT=PATH)\n");
		return 1;
	}

	size_t cap = (size_t)argc * 2 + 96;
	char **cav = malloc(((size_t)argc + 8) * sizeof(char *)), **lav = malloc(cap * sizeof(char *));
	char (*tmp)[PATH_MAX + 32] = nsrc ? malloc((size_t)nsrc * sizeof(*tmp)) : NULL;
	char (*own)[PATH_MAX + 64] = malloc(((size_t)argc + 16) * sizeof(*own));	/* paths and -lX made here */
	char **wl = malloc(((size_t)argc + 1) * sizeof(char *));	/* the -Wl, copies */
	int nc = 1, nl = 0, ntmp = 0, nown = 0, nwl = 0, status = 0;

	if (!cav || !lav || (nsrc && !tmp) || !own || !wl) {
		dprintf(errfd, "vcc: error: out of memory\n");
		status = 1;
		goto done;
	}
#define OWN(...) (snprintf(own[nown], sizeof(own[nown]), __VA_ARGS__), own[nown++])
	/* the compile options: everything but the link's own */
	cav[0] = argv[0];
	for (int i = 1; i < argc; i++) {
		const char *a = argv[i];

		if ((!strcmp(a, "-o") || !strcmp(a, "-L") || !strcmp(a, "-l") || !strcmp(a, "-Xlinker") || !strcmp(a, "-e") ||
		     !strcmp(a, "-u")) && i + 1 < argc) {
			i++;
			continue;
		}
		if (!strncmp(a, "-l", 2) || !strncmp(a, "-L", 2) || !strncmp(a, "-Wl,", 4) || !strcmp(a, "-shared") ||
		    !strcmp(a, "-mdll") || !strcmp(a, "-static") || !strcmp(a, "-nostdlib") || !strcmp(a, "-nodefaultlibs") ||
		    !strcmp(a, "-nostartfiles") || !strcmp(a, "-s") || !strcmp(a, "-mwindows") || !strcmp(a, "-mconsole") ||
		    !strncmp(a, "-fuse-ld=", 9) || !strncmp(a, "-rtlib=", 7) || !strncmp(a, "--unwindlib=", 12) ||
		    !strcmp(a, "-static-libgcc") || !strcmp(a, "-rdynamic") || !strcmp(a, "-v"))
			continue;
		if (a[0] == '-' && a[1]) {
			cav[nc++] = (char *)a;
			if (driver_mingw_option_takes_value(a) && i + 1 < argc)
				cav[nc++] = argv[++i];
		}
	}

	/* the link line, as clang -### shows it */
	lav[nl++] = "vlink";
	lav[nl++] = OWN("--sysroot=%s", sysroot);
	if (strip)
		lav[nl++] = "-s";
	lav[nl++] = "-m";
	lav[nl++] = "i386pep";
	if (shared)
		lav[nl++] = "--shared";
	if (subsystem) {
		lav[nl++] = "--subsystem";
		lav[nl++] = (char *)subsystem;
	}
	lav[nl++] = static_ ? "-Bstatic" : "-Bdynamic";
	if (shared) {
		lav[nl++] = "-e";
		lav[nl++] = "DllMainCRTStartup";
		lav[nl++] = "--enable-auto-image-base";
	}
	lav[nl++] = "-o";
	lav[nl++] = (char *)out;
	if (strip)
		lav[nl++] = "-s";
	if (!nostdlib && !nostartfiles) {
		lav[nl++] = OWN("%s/x86_64-w64-mingw32/lib/%s", sysroot, shared ? "dllcrt2.o" : unicode ? "crt2u.o" : "crt2.o");
		lav[nl++] = OWN("%s/x86_64-w64-mingw32/lib/crtbegin.o", sysroot);
	}
	for (int i = 1; i < argc; i++) {	/* -L first, the user's before the sysroot's */
		const char *a = argv[i];

		if (!strcmp(a, "-L") && i + 1 < argc)
			lav[nl++] = (char *)a, lav[nl++] = argv[++i];
		else if (!strncmp(a, "-L", 2))
			lav[nl++] = (char *)a;
		else if (a[0] == '-' && a[1] && driver_mingw_option_takes_value(a))
			i++;
	}
	lav[nl++] = OWN("-L%s/x86_64-w64-mingw32/lib", sysroot);
	lav[nl++] = OWN("-L%s/x86_64-w64-mingw32/mingw/lib", sysroot);
	lav[nl++] = OWN("-L%s/lib", sysroot);
	for (int i = 1; i < argc && !status; i++) {	/* inputs, -l, -Wl, and -Xlinker in order; sources compiled */
		const char *a = argv[i];

		if (!strcmp(a, "-l") && i + 1 < argc) {
			lav[nl++] = OWN("-l%s", argv[++i]);
			continue;
		}
		if ((!strcmp(a, "-Xlinker") || !strcmp(a, "-e") || !strcmp(a, "-u")) && i + 1 < argc) {
			if (a[1] != 'X')
				lav[nl++] = (char *)a;
			lav[nl++] = argv[++i];
			continue;
		}
		if (!strncmp(a, "-l", 2)) {
			lav[nl++] = (char *)a;
			continue;
		}
		if (!strncmp(a, "-Wl,", 4)) {	/* split at the commas, in place in a copy */
			char *w = wl[nwl++] = strdup(a + 4);

			if (!w)
				continue;
			for (char *p = w, *q; p; p = q) {
				q = strchr(p, ',');
				if (q)
					*q++ = 0;
				if (nl + 8 < (int)cap)
					lav[nl++] = p;
			}
			continue;
		}
		if (a[0] == '-' && a[1]) {
			if (driver_mingw_option_takes_value(a))
				i++;
			continue;
		}
		if (!driver_mingw_is_source(a)) {
			lav[nl++] = (char *)a;	/* an object, archive, import library or .def */
			continue;
		}
		snprintf(tmp[ntmp], sizeof(tmp[ntmp]), "%s.vcc-%d.o", out, ntmp);
		int k = nc;

		cav[k++] = "-c";
		cav[k++] = (char *)a;
		cav[k++] = "-o";
		cav[k++] = tmp[ntmp];
		cav[k] = NULL;
		lav[nl++] = tmp[ntmp++];
		status = driver_job_run(k, cav, env);
	}
	if (!nostdlib && !nodefaultlibs) {
		static const char *const tail[] = { "-ladvapi32", "-lshell32", "-luser32", "-lkernel32" };

		if (static_)
			lav[nl++] = "--start-group";
		for (int pass = 0; pass < (static_ ? 1 : 2); pass++) {
			if (threads)
				lav[nl++] = "-lmingwthrd";
			lav[nl++] = "-lmingw32";
			lav[nl++] = rt;
			lav[nl++] = "-lmoldname";
			lav[nl++] = "-lmingwex";
			lav[nl++] = "-lmsvcrt";
			if (pass) {
				lav[nl++] = "-lkernel32";
				break;
			}
			if (pthread)
				lav[nl++] = "-lpthread";
			if (subsystem && !strcmp(subsystem, "windows"))
				lav[nl++] = "-lgdi32", lav[nl++] = "-lcomdlg32";
			for (size_t t = 0; t < sizeof tail / sizeof *tail; t++)
				lav[nl++] = (char *)tail[t];
		}
		if (static_)
			lav[nl++] = "--end-group";
	}
	if (!nostdlib && !nostartfiles)
		lav[nl++] = OWN("%s/x86_64-w64-mingw32/lib/crtend.o", sysroot);
	lav[nl] = NULL;
#undef OWN
	if (verbose && !status)	/* -v: the link line, as clang's driver prints its commands */
		for (int i = 0; i < nl; i++)
			dprintf(errfd, "%s%s", lav[i], i + 1 < nl ? " " : "\n");
	if (!status)
		status = link(nl, lav, ctx);
	for (int t = 0; t < ntmp; t++) {
		char path[PATH_MAX + 64];

		unlink(driver_path_resolve(env->cwd, tmp[t], path, sizeof path));
	}
done:
	for (int k = 0; k < nwl; k++)
		free(wl[k]);
	free(wl);
	free(cav);
	free(lav);
	free(tmp);
	free(own);
	return status;
}
