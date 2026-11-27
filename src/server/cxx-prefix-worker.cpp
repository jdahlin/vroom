/*
 * cxx-prefix-worker.cpp: C++ compiles with -fcxx-prefix=FILE share the
 * analysed prefix (doc/PLAN-cxx-snapshot.md CS5, CS6).  Linux.
 *
 * vccd's side (vccd_cxx_prefix_compile): per key (the arguments but for the
 * job's files, the directory, the environment variables the driver reads) a
 * worker, `vccd --cxx-prefix-worker` on one end of a SOCK_SEQPACKET pair.
 * vccd is threaded and never forks itself.  A job is one message: its files,
 * and the descriptors for stdout, the diagnostics and the answer.
 *
 * The worker (vccd_cxx_prefix_worker_main) runs the driver on the first job's
 * command line up to the stop after the prefix, then forks a child per
 * message; the child takes the job's files and finishes as any compile.  The
 * parent stays single-threaded and never compiles past the stop.
 *
 * Validation: every path the prefix asked the filesystem about, found or not,
 * is compared before each fork (a header edited, replaced, or one that now
 * shadows another); a difference answers `stale` and ends the worker, and
 * vccd starts a new one.
 *
 * A worker writes one byte on the control socket when it reaches the stop
 * (never read, only looked at), so vccd tells a prefix that does not work
 * from a worker that was killed.  A stale one stops reading, answers what is
 * queued and exits.
 *
 * Anything that goes wrong (the prefix has errors, the child fails or dies,
 * the job is not eligible) leaves the job to the caller, which compiles it
 * without the worker: every error a user sees is that path's.
 */
#include "vccd-internal.h"

#ifdef __linux__
#include "common/attributes.h"

enum { CXX_PREFIX_OK = 'o', CXX_PREFIX_FAILED = 'f', CXX_PREFIX_STALE = 's' };
enum {
    CXX_PREFIX_RETRY_SECONDS = 2, /* a prefix that did not work */
    CXX_PREFIX_WORKERS = 4,
    CXX_PREFIX_MESSAGE = 1 << 17,
};

/* strings, NUL after each */
struct cxx_prefix_message {
    char data[CXX_PREFIX_MESSAGE];
    size_t length;
    bool full;
};

static void cxx_prefix_put(struct cxx_prefix_message *m, const char *s)
{
    size_t n = strlen(s) + 1;

    if (m->length + n > sizeof m->data) {
        m->full = true;
        return;
    }
    memcpy(m->data + m->length, s, n);
    m->length += n;
}

static const char *cxx_prefix_get(const struct cxx_prefix_message *m,
                                  size_t *at)
{
    const char *s = m->data + *at;

    if (*at >= m->length)
        return "";
    *at += strlen(s) + 1;
    return s;
}

/* ---- the worker process ------------------------------------------------ */

struct cxx_prefix_file {
    char *path;
    bool found;
    dev_t device;
    ino_t inode;
    off_t size;
    struct timespec modified;
};

struct cxx_prefix_server {
    int control;
    struct cxx_prefix_file *files;
    size_t count, capacity;
    int reply; /* the child's */
    struct cxx_prefix_message message;
};

static void cxx_prefix_stat(struct cxx_prefix_file *f)
{
    struct stat st;

    f->found = stat(f->path, &st) == 0;
    if (!f->found)
        return;
    f->device = st.st_dev;
    f->inode = st.st_ino;
    f->size = st.st_size;
    f->modified = st.st_mtim;
}

static void cxx_prefix_probe(void *context, const char *path)
{
    struct cxx_prefix_server *s = (struct cxx_prefix_server *)context;

    if (s->count == s->capacity) {
        s->capacity = s->capacity ? 2 * s->capacity : 1024;
        s->files = (struct cxx_prefix_file *)(realloc(
            s->files, s->capacity * sizeof *s->files));
        if (!s->files)
            _exit(2);
    }
    struct cxx_prefix_file *f = &s->files[s->count++];

    memset(f, 0, sizeof *f);
    f->path = strdup(path);
    cxx_prefix_stat(f);
}

static bool cxx_prefix_unchanged(const struct cxx_prefix_server *s,
                                 size_t count)
{
    for (size_t i = 0; i < count; i++) {
        const struct cxx_prefix_file *was = &s->files[i];
        struct cxx_prefix_file is = *was;

        cxx_prefix_stat(&is);
        if (is.found != was->found)
            return false;
        if (is.found && (is.device != was->device || is.inode != was->inode ||
                         is.size != was->size ||
                         is.modified.tv_sec != was->modified.tv_sec ||
                         is.modified.tv_nsec != was->modified.tv_nsec))
            return false;
    }
    return true;
}

/* one message and its descriptors; false at the socket's end */
static bool cxx_prefix_receive(int fd, struct cxx_prefix_message *m,
                               int *descriptors, int want)
{
    char control[CMSG_SPACE(3 * sizeof(int))];
    struct iovec v = {m->data, sizeof m->data};
    struct msghdr h = {};
    ssize_t n;

    h.msg_iov = &v;
    h.msg_iovlen = 1;
    h.msg_control = control;
    h.msg_controllen = sizeof control;
    do
        n = recvmsg(fd, &h, MSG_CMSG_CLOEXEC);
    while (n < 0 && errno == EINTR);
    if (n <= 0)
        return false;
    m->length = (size_t)n;
    if (!want)
        return true;
    struct cmsghdr *c = CMSG_FIRSTHDR(&h);

    if (!c || c->cmsg_len != CMSG_LEN((size_t)want * sizeof(int)))
        return false;
    memcpy(descriptors, CMSG_DATA(c), (size_t)want * sizeof(int));
    return true;
}

static void cxx_prefix_answer(int fd, char status)
{
    while (write(fd, &status, 1) < 0 && errno == EINTR)
        ;
}

/* at the stop: a child per job; returns in the child only */
static void cxx_prefix_serve(void *context, struct driver_files *files)
{
    struct cxx_prefix_server *s = (struct cxx_prefix_server *)context;
    /* (the child's own probes come after these) */
    size_t prefix_files = s->count;
    char ready = 'r';

    while (send(s->control, &ready, 1, MSG_NOSIGNAL) < 0 && errno == EINTR)
        ;
    for (;;) {
        int fd[3];

        if (!cxx_prefix_receive(s->control, &s->message, fd, 3))
            _exit(0);
        if (!cxx_prefix_unchanged(s, prefix_files)) {
            /* no more sends; every job queued hears `stale` */
            shutdown(s->control, SHUT_RD);
            do {
                cxx_prefix_answer(fd[2], CXX_PREFIX_STALE);
                for (int i = 0; i < 3; i++)
                    close(fd[i]);
            } while (cxx_prefix_receive(s->control, &s->message, fd, 3));
            _exit(0);
        }
        pid_t child = fork();

        if (child == 0) {
            size_t at = 0;
            const char *mask;

            close(s->control);
            dup2(fd[0], 1);
            dup2(fd[1], 2);
            s->reply = fd[2];
            files->main_file = cxx_prefix_get(&s->message, &at);
            files->output_file = cxx_prefix_get(&s->message, &at);
            files->depfile = cxx_prefix_get(&s->message, &at);
            files->depfile_target = cxx_prefix_get(&s->message, &at);
            mask = cxx_prefix_get(&s->message, &at);
            driver_client_umask = atoi(mask);
            if (!*files->depfile)
                files->depfile = NULL;
            return;
        }
        if (child < 0)
            cxx_prefix_answer(fd[2], CXX_PREFIX_FAILED);
        for (int i = 0; i < 3; i++)
            close(fd[i]);
    }
}

VPRIV int vccd_cxx_prefix_worker_main(int control)
{
    static struct cxx_prefix_server s;
    static char *strings[8192];
    size_t at = 0, n = 0;

    s.control = control;
    s.reply = -1;
    signal(SIGCHLD, SIG_IGN);
    signal(SIGPIPE, SIG_IGN);
    if (!cxx_prefix_receive(control, &s.message, NULL, 0))
        return 1;
    /* cwd, the environment up to an empty string, the arguments */
    static struct cxx_prefix_message start;

    start = s.message;
    const char *cwd = cxx_prefix_get(&start, &at);

    while (at < start.length && n < 8190) {
        const char *e = cxx_prefix_get(&start, &at);

        if (!*e)
            break;
        strings[n++] = VUNCONST(char *, e);
    }
    strings[n++] = NULL;
    char **argv = strings + n;
    int argc = 0;

    while (at < start.length && n < 8190) {
        strings[n++] = VUNCONST(char *, cxx_prefix_get(&start, &at));
        argc++;
    }
    strings[n] = NULL;
    driver_environment e = {};

    e.cwd = cwd;
    e.outfd = 1;
    e.errfd = 2;
    e.envp = strings;
    e.cxx_boundary = cxx_prefix_serve;
    e.cxx_boundary_context = &s;
    e.cxx_probe = cxx_prefix_probe;
    e.cxx_probe_context = &s;
    if (chdir(cwd))
        return 1;
    int status = driver_job_run(argc, argv, &e);

    if (s.reply < 0) /* the parent: the stop was not reached */
        return 1;
    cxx_prefix_answer(s.reply, status ? CXX_PREFIX_FAILED : CXX_PREFIX_OK);
    _exit(0);
}

/* ---- vccd's side ------------------------------------------------------- */

struct cxx_prefix_worker {
    char *key;
    size_t key_length;
    int control;   /* -1: no process; with a key: its prefix does not work */
    pid_t process; /* 0: reaped */
    int busy;
    uint64_t used, generation;
    time_t broken; /* when its prefix was found not to work */
};

static struct cxx_prefix_worker cxx_prefix_workers[CXX_PREFIX_WORKERS];
static pthread_mutex_t cxx_prefix_mutex = PTHREAD_MUTEX_INITIALIZER;

struct cxx_prefix_job {
    const char *main_file, *output_file, *dependency_file, *dependency_target;
    int main_index;
    char dependency_name[4096];
};

static bool cxx_prefix_suffix(const char *s, const char *suffix)
{
    size_t n = strlen(s), k = strlen(suffix);

    return n > k && !strcmp(s + n - k, suffix);
}

/* a -c of one C++ file with -fcxx-prefix=, its files named as the worker can
   take them */
static bool cxx_prefix_job_read(int argc, char **argv, struct cxx_prefix_job *j)
{
    bool prefix = false, compile = false, dependencies = false;

    memset(j, 0, sizeof *j);
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];

        if (!strncmp(a, "-fcxx-prefix=", 13))
            prefix = true;
        else if (!strcmp(a, "-c"))
            compile = true;
        else if (!strncmp(a, "-g", 2) || !strcmp(a, "-E") || !strcmp(a, "-S") ||
                 !strcmp(a, "-M") || !strcmp(a, "-MM") ||
                 !strcmp(a, "-fsyntax-only") || !strncmp(a, "-Wp,", 4) ||
                 !strcmp(a, "-MQ") || !strcmp(a, "-") ||
                 !strncmp(a, "-fcxx-dump", 10))
            return false;
        else if (!strcmp(a, "-MD") || !strcmp(a, "-MMD"))
            dependencies = true;
        else if (vccd_option_takes_value(a)) {
            if (i + 1 >= argc)
                return false;
            if (!strcmp(a, "-o"))
                j->output_file = argv[i + 1];
            if (!strcmp(a, "-MF"))
                j->dependency_file = argv[i + 1];
            if (!strcmp(a, "-MT")) {
                if (j->dependency_target)
                    return false;
                j->dependency_target = argv[i + 1];
            }
            i++;
        } else if (a[0] != '-') {
            if (j->main_file ||
                !(cxx_prefix_suffix(a, ".cpp") || cxx_prefix_suffix(a, ".cc") ||
                  cxx_prefix_suffix(a, ".cxx")))
                return false;
            j->main_file = a;
            j->main_index = i;
        }
    }
    if (!prefix || !compile || !j->main_file || !j->output_file)
        return false;
    if (!dependencies) {
        j->dependency_file = NULL;
        return !j->dependency_target;
    }
    if (!j->dependency_target)
        j->dependency_target = j->output_file;
    if (!j->dependency_file) {
        const char *slash = strrchr(j->output_file, '/');
        const char *dot = strrchr(j->output_file, '.');
        int stem = (int)strlen(j->output_file);

        if (dot && (!slash || dot > slash))
            stem = (int)(dot - j->output_file);
        snprintf(j->dependency_name, sizeof j->dependency_name, "%.*s.d", stem,
                 j->output_file);
        j->dependency_file = j->dependency_name;
    }
    return true;
}

static bool cxx_prefix_environment_counts(const char *e)
{
    static const char *const names[] = {
        "CPATH=", "CPLUS_INCLUDE_PATH=", "C_INCLUDE_PATH=",
        "SOURCE_DATE_EPOCH=", "VCC_"};

    if (!strncmp(e, "VCC_FIXDEP_", 11)) /* each job's own */
        return false;
    for (size_t i = 0; i < sizeof names / sizeof *names; i++)
        if (!strncmp(e, names[i], strlen(names[i])))
            return true;
    return false;
}

/* the key: the directory, the environment, the arguments with the job's files
   left out; and the worker's first message, the same with them in */
static void cxx_prefix_describe(int argc, char **argv,
                                const driver_environment *env,
                                const struct cxx_prefix_job *j,
                                struct cxx_prefix_message *key,
                                struct cxx_prefix_message *start)
{
    char here[4096];
    const char *cwd = env->cwd;

    if (!cwd)
        cwd = getcwd(here, sizeof here) ? here : ".";
    cxx_prefix_put(key, cwd);
    cxx_prefix_put(start, cwd);
    for (char **e = env->envp; e && *e; e++)
        if (cxx_prefix_environment_counts(*e)) {
            cxx_prefix_put(key, *e);
            cxx_prefix_put(start, *e);
        }
    cxx_prefix_put(key, "");
    cxx_prefix_put(start, "");
    for (int i = 0; i < argc; i++) {
        const char *a = argv[i];

        cxx_prefix_put(start, a);
        if (i == j->main_index)
            continue;
        cxx_prefix_put(key, a);
        if (!strcmp(a, "-o") || !strcmp(a, "-MF") || !strcmp(a, "-MT")) {
            i++;
            cxx_prefix_put(start, argv[i]);
        }
    }
}

static uint64_t cxx_prefix_clock;

static void cxx_prefix_drop(struct cxx_prefix_worker *w)
{
    if (w->control >= 0) {
        /* the worker ends at its next read, also while jobs still hold
           their copies of the socket */
        shutdown(w->control, SHUT_RDWR);
        close(w->control);
        if (w->process)
            waitpid(w->process, NULL, 0);
        w->control = -1;
        /* the jobs still holding it were not answered by what is there now */
        w->generation = ++cxx_prefix_clock;
    }
}

static bool cxx_prefix_start(struct cxx_prefix_worker *w,
                             const struct cxx_prefix_message *start)
{
    int pair[2], error;
    posix_spawn_file_actions_t actions;
    posix_spawnattr_t attributes;
    sigset_t none;
    char name[] = "vccd", option[] = "--cxx-prefix-worker";
    char *argv[] = {name, option, NULL};
    extern char **environ;

    if (socketpair(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0, pair))
        return false;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_adddup2(&actions, pair[1], 3);
    posix_spawn_file_actions_addopen(&actions, 0, "/dev/null", O_RDONLY, 0);
    posix_spawn_file_actions_addopen(&actions, 1, "/dev/null", O_WRONLY, 0);
    posix_spawn_file_actions_addopen(&actions, 2, "/dev/null", O_WRONLY, 0);
    /* other requests' descriptors are open: none of them */
    posix_spawn_file_actions_addclosefrom_np(&actions, 4);
    posix_spawnattr_init(&attributes);
    sigemptyset(&none);
    posix_spawnattr_setsigmask(&attributes, &none);
    posix_spawnattr_setflags(&attributes, POSIX_SPAWN_SETSIGMASK);
    error = posix_spawn(&w->process, "/proc/self/exe", &actions, &attributes,
                        argv, environ);
    posix_spawn_file_actions_destroy(&actions);
    posix_spawnattr_destroy(&attributes);
    close(pair[1]);
    if (error || send(pair[0], start->data, start->length, MSG_NOSIGNAL) < 0) {
        close(pair[0]);
        return false;
    }
    w->control = pair[0];
    return true;
}

/* a job's hold on a worker: its own descriptor of the control socket, which
   stays valid whatever happens to the worker meanwhile */
struct cxx_prefix_hold {
    struct cxx_prefix_worker *worker;
    int control;
    uint64_t generation;
};

/* the worker of key, started if need be; false: none */
static bool cxx_prefix_acquire(const struct cxx_prefix_message *key,
                               const struct cxx_prefix_message *start,
                               struct cxx_prefix_hold *hold)
{
    struct cxx_prefix_worker *w = NULL, *oldest = NULL;
    bool start_it = false;

    pthread_mutex_lock(&cxx_prefix_mutex);
    for (int i = 0; i < CXX_PREFIX_WORKERS; i++) {
        struct cxx_prefix_worker *x = &cxx_prefix_workers[i];

        if (x->key && x->key_length == key->length &&
            !memcmp(x->key, key->data, key->length)) {
            w = x;
            break;
        }
        if (!x->busy && (!oldest || !x->key || x->used < oldest->used))
            oldest = x;
    }
    if (w) {
        start_it = w->control < 0 &&
                   time(NULL) - w->broken >= CXX_PREFIX_RETRY_SECONDS;
    } else if (oldest) {
        if (oldest->key)
            cxx_prefix_drop(oldest);
        free(oldest->key);
        oldest->key = (char *)(malloc(key->length));
        if (oldest->key) {
            memcpy(oldest->key, key->data, key->length);
            oldest->key_length = key->length;
            oldest->control = -1;
            w = oldest;
            start_it = true;
        }
    }
    if (w && start_it) {
        w->generation = ++cxx_prefix_clock;
        if (!cxx_prefix_start(w, start)) {
            free(w->key);
            w->key = NULL;
            w = NULL;
        } else if (vccd_log_requests)
            fprintf(stderr, "vccd cxx-prefix: worker %d started\n",
                    (int)w->process);
    }
    hold->control = -1;
    if (w && w->control >= 0)
        hold->control = fcntl(w->control, F_DUPFD_CLOEXEC, 3);
    if (hold->control >= 0) {
        hold->worker = w;
        hold->generation = w->generation;
        w->busy++;
        w->used = ++cxx_prefix_clock;
    }
    pthread_mutex_unlock(&cxx_prefix_mutex);
    return hold->control >= 0;
}

/* the job to w; the answer, or 0 when the child or the worker died */
static char cxx_prefix_run(int control_socket, const struct cxx_prefix_job *j,
                           int outfd, int errfd)
{
    struct cxx_prefix_message *m =
        (struct cxx_prefix_message *)(calloc(1, sizeof *m));
    int reply[2];
    char status = 0, mask[16];

    if (!m || pipe2(reply, O_CLOEXEC)) {
        free(m);
        return 0;
    }
    int umask_ = driver_client_umask;

    if (umask_ < 0) {
        mode_t process = umask(0);

        umask(process);
        umask_ = (int)process;
    }
    snprintf(mask, sizeof mask, "%d", umask_);
    cxx_prefix_put(m, j->main_file);
    cxx_prefix_put(m, j->output_file);
    cxx_prefix_put(m, j->dependency_file ? j->dependency_file : "");
    cxx_prefix_put(m, j->dependency_target ? j->dependency_target : "");
    cxx_prefix_put(m, mask);

    int descriptors[3] = {outfd, errfd, reply[1]};
    char control[CMSG_SPACE(sizeof descriptors)] = {};
    struct iovec v = {m->data, m->length};
    struct msghdr h = {};

    h.msg_iov = &v;
    h.msg_iovlen = 1;
    h.msg_control = control;
    h.msg_controllen = sizeof control;
    struct cmsghdr *c = CMSG_FIRSTHDR(&h);

    c->cmsg_level = SOL_SOCKET;
    c->cmsg_type = SCM_RIGHTS;
    c->cmsg_len = CMSG_LEN(sizeof descriptors);
    memcpy(CMSG_DATA(c), descriptors, sizeof descriptors);
    bool sent = !m->full && sendmsg(control_socket, &h, MSG_NOSIGNAL) >= 0;

    if (!sent && errno == EPIPE) /* it stopped reading: stale, or gone */
        status = CXX_PREFIX_STALE;

    close(reply[1]);
    if (sent) {
        ssize_t n;

        do
            n = read(reply[0], &status, 1);
        while (n < 0 && errno == EINTR);
        if (n != 1)
            status = 0;
    }
    close(reply[0]);
    free(m);
    return status;
}

/* after a job; whether another worker is worth a try */
static bool cxx_prefix_release(struct cxx_prefix_hold *hold, char status)
{
    struct cxx_prefix_worker *w = hold->worker;
    bool again = status == CXX_PREFIX_STALE;

    pthread_mutex_lock(&cxx_prefix_mutex);
    w->busy--;
    if (w->generation != hold->generation) {
        again = again || !status; /* replaced meanwhile */
    } else if (w->control >= 0 && status == CXX_PREFIX_STALE) {
        cxx_prefix_drop(w);
        free(w->key);
        w->key = NULL;
    } else if (w->control >= 0 && !status) {
        char byte;
        ssize_t n = recv(w->control, &byte, 1, MSG_DONTWAIT | MSG_PEEK);

        if (n == 0) {
            /* ended without reaching the stop: the key stays, broken */
            cxx_prefix_drop(w);
            w->broken = time(NULL);
        } else {
            /* the child died, or the worker was killed (it may still be
               dying): a new worker either way */
            cxx_prefix_drop(w);
            free(w->key);
            w->key = NULL;
            again = true;
        }
    }
    pthread_mutex_unlock(&cxx_prefix_mutex);
    close(hold->control);
    return again;
}

VPRIV int vccd_cxx_prefix_compile(int argc, char **argv,
                                  const driver_environment *env)
{
    struct cxx_prefix_job j;

    if (env->errfd < 0 || !cxx_prefix_job_read(argc, argv, &j))
        return -1;
    struct cxx_prefix_message *key =
        (struct cxx_prefix_message *)(calloc(2, sizeof *key));

    if (!key)
        return -1;
    struct cxx_prefix_message *start = key + 1;
    int result = -1;

    cxx_prefix_describe(argc, argv, env, &j, key, start);
    /* further tries with a new worker, after `stale` */
    for (int attempt = 0; attempt < 3 && !key->full && !start->full;
         attempt++) {
        struct cxx_prefix_hold hold;

        if (!cxx_prefix_acquire(key, start, &hold))
            break;
        FILE *diagnostics = tmpfile();
        char status = 0;

        if (diagnostics)
            status = cxx_prefix_run(hold.control, &j, env->outfd,
                                    fileno(diagnostics));
        bool again = cxx_prefix_release(&hold, status);

        if (status == CXX_PREFIX_OK) {
            char buffer[1 << 14];
            ssize_t k;
            off_t at = 0;

            while ((k = pread(fileno(diagnostics), buffer, sizeof buffer, at)) >
                   0) {
                if (!vccd_fd_write_full(env->errfd, buffer, (size_t)k))
                    break;
                at += k;
            }
            result = 0;
        }
        if (diagnostics)
            fclose(diagnostics);
        if (!again)
            break;
    }
    if (vccd_log_requests)
        fprintf(stderr, "vccd cxx-prefix: %s %s\n", j.main_file,
                result ? "compiled without a worker" : "compiled by a worker");
    free(key);
    return result;
}

#else

VPRIV int vccd_cxx_prefix_worker_main(int control)
{
    (void)control;
    return 1;
}

VPRIV int vccd_cxx_prefix_compile(int argc, char **argv,
                                  const driver_environment *env)
{
    (void)argc, (void)argv, (void)env;
    return -1;
}

#endif
