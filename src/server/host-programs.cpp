/* Build native host programs with the compiler and linker in this process. */
#include "common/attributes.h"
#include "vccd-internal.h"

/* options that take the next argv entry */
static bool vccd_host_option_takes_value(const char *a)
{
    static const char *const w[] = {
        "-o",      "-include",  "-isystem", "-iquote", "-idirafter", "-x",
        "-MF",     "-MT",       "-MQ",      "-I",      "-D",         "-U",
        "-L",      "-l",        "-imacros", "-mllvm",  "--param",    "-arch",
        "-target", "-isysroot", NULL};
    for (const char *const *p = w; *p; p++)
        if (!strcmp(a, *p))
            return true;
    return false;
}

static bool vccd_host_is_source(const char *a)
{
    size_t n = strlen(a);
    return n > 2 && a[n - 2] == '.' &&
           (a[n - 1] == 'c' || a[n - 1] == 'S' || a[n - 1] == 's');
}

#if defined(__linux__) && defined(__GLIBC__)
#if defined(__x86_64__)
#define HOST_LIBRARY_DIR "/usr/lib/x86_64-linux-gnu"
#define HOST_INTERPRETER "/lib64/ld-linux-x86-64.so.2"
#elif defined(__aarch64__)
#define HOST_LIBRARY_DIR "/usr/lib/aarch64-linux-gnu"
#define HOST_INTERPRETER "/lib/ld-linux-aarch64.so.1"
#endif
#endif

#ifdef HOST_INTERPRETER
static int vccd_host_linux_startup(const struct vccd_request *r, char **lav,
                                   int nl, bool pie)
{
    /* glibc supplies _start and the init/fini fragments. Resolve user -L
     * directories before the distribution's multiarch or lib64 directory. */
    for (int i = 1; i < r->argc; i++) {
        const char *a = r->argv[i];
        if (!strncmp(a, "-L", 2)) {
            lav[nl++] = VUNCONST(char *, a);
            if (!a[2] && i + 1 < r->argc)
                lav[nl++] = r->argv[++i];
        } else if (vccd_host_option_takes_value(a))
            i++;
    }
    lav[nl++] = VUNCONST(char *, "-L" HOST_LIBRARY_DIR);
    lav[nl++] = VUNCONST(char *, "-L/usr/lib64");
    lav[nl++] = VUNCONST(char *, "-L/usr/lib");
    lav[nl++] = VUNCONST(char *, "--dynamic-linker");
    lav[nl++] = VUNCONST(char *, HOST_INTERPRETER);
    if (pie)
        lav[nl++] = VUNCONST(char *, "-pie");
    lav[nl++] = VUNCONST(char *, (pie ? "-l:Scrt1.o" : "-l:crt1.o"));
    lav[nl++] = VUNCONST(char *, "-l:crti.o");
    return nl;
}
#endif

static int vccd_host_link(const struct vccd_request *r, int argc, char **argv)
{
#if defined(HOST_INTERPRETER) && defined(__aarch64__)
    /* vlink currently reads shared libraries only for x86-64 ELF. */
    struct vccd_request link = *r;
    link.argc = argc;
    link.argv = argv;
    return vccd_program_spawn(&link, "/usr/bin/ld");
#else
    return vccd_linker_run_in_process(r, argc, argv);
#endif
}

VPRIV int vccd_host_program_compile(const struct vccd_request *r)
{
    bool compile_only = false;
    const char *out = "a.out";
    int nsrc = 0;
#ifdef HOST_INTERPRETER
    bool pie = false;
#endif

#if !defined(__APPLE__) && !defined(HOST_INTERPRETER)
    vccd_request_message_write(
        r, "%s: error: %s\n",
        "host programs need --backend-sock on this system");
    return 1;
#endif
    for (int i = 1; i < r->argc; i++) {
        const char *a = r->argv[i];
        if (!strncmp(a, "--target=", 9) || !strcmp(a, "-target")) {
            /* a target job vcc does not take (vccd_request_is_compile, vccd_request_is_preprocess): no fallback */
            vccd_request_message_write(
                r, "%s: error: unsupported job for target '%s'\n",
                a[1] == '-' ? a + 9 : "?");
            return 1;
        }
#ifdef HOST_INTERPRETER
        if (!strcmp(a, "-pie") || !strcmp(a, "-no-pie"))
            pie = !strcmp(a, "-pie");
#endif
        if (!strcmp(a, "-c") || !strcmp(a, "-S") || !strcmp(a, "-E") ||
            !strcmp(a, "-fsyntax-only") || !strcmp(a, "-M") ||
            !strcmp(a, "-MM"))
            compile_only = true;
        else if (!strcmp(a, "-o") && i + 1 < r->argc)
            out = r->argv[i + 1];
        else if (!strncmp(a, "-o", 2) && a[2])
            out = a + 2;
        else if (!strcmp(a, "-") || (a[0] != '-' && vccd_host_is_source(a)))
            nsrc++;
        if (a[0] == '-' && vccd_host_option_takes_value(a))
            i++;
    }
    if (compile_only) {
        driver_environment e = {.cwd = r->cwd,
                                .outfd = r->fds[1],
                                .errfd = r->fds[2],
                                .envp = r->envp,
                                .infd = r->fds[0]};
        return driver_job_run(r->argc, r->argv, &e);
    }

    /* compile + link: the compile options, then per source "-c SRC -o TMP" */
    char **cav = (char **)(malloc(((size_t)r->argc + 8) * sizeof(char *)));
    char **lav = (char **)(malloc(((size_t)r->argc + 20) * sizeof(char *)));
    typedef char temp_path[sizeof("/tmp/vcc-host-XXXXXX")];
    temp_path *tmp = (temp_path *)calloc((size_t)nsrc, sizeof(*tmp));
    int status = 0, nc = 1, nl = 3, ntmp = 0, source_slot = 0;

    if (!cav || !lav || (nsrc && !tmp)) {
        free(cav);
        free(lav);
        free(tmp);
        return 1;
    }
    cav[0] = (char *)r->argv[0];
    lav[0] = "vld";
    lav[1] = "-o";
    lav[2] = VUNCONST(char *, out);
#ifdef HOST_INTERPRETER
    nl = vccd_host_linux_startup(r, lav, nl, pie);
#endif
    for (int i = 1; i < r->argc; i++) {
        const char *a = r->argv[i];
        if (!strcmp(a, "-o") && i + 1 < r->argc) {
            i++;
            continue;
        }
        if (!strncmp(a, "-o", 2) && a[2])
            continue;
        if (!strncmp(a, "-l", 2) || !strncmp(a, "-L", 2)) {
#ifdef HOST_INTERPRETER
            if (a[1] == 'L') {
                if (!a[2] && i + 1 < r->argc)
                    i++;
                continue;
            }
#endif
            lav[nl++] = VUNCONST(char *, a);
            if (!a[2] && i + 1 < r->argc)
                lav[nl++] = (char *)r->argv[++i];
            continue;
        }
        if (!strncmp(a, "-fuse-ld=", 9) || !strcmp(a, "-pie") ||
            !strcmp(a, "-no-pie"))
            continue;
        if (!strncmp(a, "-Wl,", 4)
#ifdef HOST_INTERPRETER
            || !strcmp(a, "-static") || !strcmp(a, "-shared") ||
            !strcmp(a, "-nostdlib") || !strcmp(a, "-nostartfiles") ||
            !strcmp(a, "-nodefaultlibs") || !strcmp(a, "-Xlinker") ||
            !strncmp(a, "--sysroot", 9)
#endif
        ) {
            vccd_request_message_write(
                r, "%s: error: host link option '%s' is not supported\n", a);
            status = 1;
            goto done;
        }
        if (a[0] == '-' && a[1]) {
            cav[nc++] = VUNCONST(char *, a);
            if (vccd_host_option_takes_value(a) && i + 1 < r->argc)
                cav[nc++] = (char *)r->argv[++i];
            continue;
        }
        if (!strcmp(a, "-") || vccd_host_is_source(a)) {
            lav[nl++] = tmp[source_slot++];
            continue;
        }
        lav[nl++] = VUNCONST(char *, a); /* an object */
    }
    for (int i = 1; i < r->argc && !status; i++) {
        const char *a = r->argv[i];
        if (a[0] == '-' && a[1]) {
            if (vccd_host_option_takes_value(a))
                i++;
            continue;
        }
        if (strcmp(a, "-") && !vccd_host_is_source(a))
            continue;
        strcpy(tmp[ntmp], "/tmp/vcc-host-XXXXXX");
        int fd = mkstemp(tmp[ntmp]);
        if (fd < 0) {
            vccd_request_message_write(
                r, "%s: error: cannot create temporary object: %s\n",
                strerror(errno));
            status = 1;
            break;
        }
        close(fd);
        int k = nc;
        cav[k++] = "-c";
        cav[k++] = VUNCONST(char *, a);
        cav[k++] = "-o";
        cav[k++] = tmp[ntmp];
        cav[k] = NULL;
        ntmp++;
        driver_environment e = {.cwd = r->cwd,
                                .outfd = r->fds[1],
                                .errfd = r->fds[2],
                                .envp = r->envp,
                                .infd = r->fds[0]};
        status = driver_job_run(k, cav, &e);
    }
#ifdef HOST_INTERPRETER
    lav[nl++] = VUNCONST(char *, "-lc");
    lav[nl++] = VUNCONST(char *, "-l:crtn.o");
#endif
    lav[nl] = NULL;
    if (!status)
        status = vccd_host_link(r, nl, lav);
done:
    for (int t = 0; t < ntmp; t++)
        unlink(tmp[t]);
    free(cav);
    free(lav);
    free(tmp);
    return status;
}
