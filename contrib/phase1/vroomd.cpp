// vroomd: zygote-server för clang.
//
// Föräldern laddar libclang-cpp/libLLVM och initierar alla mål EN gång, och forkar
// sedan ett barn per anslutning. Barnet tar emot argv/env/cwd + fd 0,1,2 från
// vroom-cc, kör clangs driver i processen (cc1 via Driver::CC1Main) och skriver
// exitstatus tillbaka. Uppstartskostnaden (~45 ms dyld + statisk init på macOS)
// betalas alltså bara en gång.
//
//   vroomd [--sock /tmp/vroomd.sock] [--clang /opt/homebrew/opt/llvm/bin/clang]

#include "clang/Basic/Diagnostic.h"
#include "clang/Basic/DiagnosticIDs.h"
#include "clang/Basic/DiagnosticOptions.h"
#include "clang/Driver/Compilation.h"
#include "clang/Driver/Driver.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/CompilerInvocation.h"
#include "clang/Frontend/TextDiagnosticBuffer.h"
#include "clang/Frontend/TextDiagnosticPrinter.h"
#include "clang/FrontendTool/Utils.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/TargetParser/Host.h"

#include <cerrno>
#include <ctime>
#include <fcntl.h>
#include <sys/file.h>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <spawn.h>
#include <string>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

extern "C" {
#include "../../src/include/protocol.h"
}

extern char **environ;

using namespace clang;
using namespace clang::driver;

#ifdef VROOM_LLVM_BIN
static std::string g_clang = VROOM_LLVM_BIN "/clang";
#else
static std::string g_clang = "/opt/homebrew/opt/llvm/bin/clang";
#endif

// ---------- cc1 i processen ----------

static int run_cc1(llvm::ArrayRef<const char *> argv, const char *argv0) {
  auto ci = std::make_unique<CompilerInstance>();
  DiagnosticOptions dopts;
  auto *buf = new TextDiagnosticBuffer;
  DiagnosticsEngine diags(DiagnosticIDs::create(), dopts, buf);
  bool ok = CompilerInvocation::CreateFromArgs(ci->getInvocation(), argv, diags, argv0);
  ci->createVirtualFileSystem();
  ci->createDiagnostics();
  if (!ci->hasDiagnostics())
    return 1;
  buf->FlushDiagnostics(ci->getDiagnostics());
  if (!ok) {
    return 1;
  }
  ok = ExecuteCompilerInvocation(ci.get());
  // Barnet avslutas med _exit; inget behöver frigöras.
  llvm::outs().flush();
  llvm::errs().flush();
  (void)ci.release();
  return ok ? 0 : 1;
}

// Allt som inte är -cc1 (t.ex. -cc1as för .S-filer) körs av riktig clang.
static int spawn_real(llvm::SmallVectorImpl<const char *> &argv) {
  std::vector<char *> a;
  a.push_back(const_cast<char *>(g_clang.c_str()));
  for (size_t i = 1; i < argv.size(); i++)
    a.push_back(const_cast<char *>(argv[i]));
  a.push_back(nullptr);
  pid_t pid;
  if (posix_spawn(&pid, g_clang.c_str(), nullptr, nullptr, a.data(), environ) != 0)
    return 1;
  int st;
  while (waitpid(pid, &st, 0) < 0 && errno == EINTR) {
  }
  return WIFEXITED(st) ? WEXITSTATUS(st) : 128 + WTERMSIG(st);
}

static int cc1_hook(llvm::SmallVectorImpl<const char *> &argv) {
  if (argv.size() >= 2 && llvm::StringRef(argv[1]) == "-cc1")
    return run_cc1(llvm::ArrayRef<const char *>(argv).slice(2), argv[0]);
  return spawn_real(argv);
}

// ---------- drivern ----------

static int run_driver(std::vector<const char *> &argv) {
  argv[0] = g_clang.c_str();
  DiagnosticOptions dopts;
  auto *printer = new TextDiagnosticPrinter(llvm::errs(), dopts);
  printer->setPrefix("clang");
  DiagnosticsEngine diags(DiagnosticIDs::create(), dopts, printer);
  Driver drv(g_clang, llvm::sys::getDefaultTargetTriple(), diags);
  drv.CC1Main = cc1_hook;
  std::unique_ptr<Compilation> c(drv.BuildCompilation(argv));
  int res = 1;
  if (c && !c->containsError()) {
    llvm::SmallVector<std::pair<int, const Command *>, 4> failing;
    res = drv.ExecuteCompilation(*c, failing);
    for (auto &f : failing)
      if (!res)
        res = f.first;
  }
  llvm::outs().flush();
  llvm::errs().flush();
  return res;
}

// ---------- kärnspecifikt: delad PCH + lägre optimering ----------
//
// Kärnans C-filer kompileras med nästan identiska flaggor och inkluderar i stort
// sett samma headers. Den första kvalificerade filen bygger en PCH av
// contrib/phase1/pch/vroom-prefix.h med sina (avskalade) flaggor; alla följande använder den.
// Varje kompilering med PCH görs i ett barnbarn med fångad stderr – misslyckas
// den, kompileras filen om utan PCH. Fel blir alltså aldrig fel resultat.

static std::string g_prefix = VROOM_PREFIX_DEFAULT;
static std::vector<std::string> g_optflags; // t.ex. -O1 -fno-inline-functions

static bool has(const std::vector<const char *> &a, llvm::StringRef s) {
  for (auto *x : a)
    if (s == x)
      return true;
  return false;
}

static bool kernel_eligible(const std::vector<const char *> &a) {
  const char *pch = getenv("VROOM_PCH");
  if (pch && !strcmp(pch, "0"))
    return false;
  if (!has(a, "-c") || !has(a, "-D__KERNEL__") || !has(a, "--target=aarch64-linux-gnu"))
    return false;
  bool ctypes = false;
  const char *input = nullptr;
  for (size_t i = 1; i < a.size(); i++) {
    llvm::StringRef x = a[i];
    if (x == "-E" || x == "-S" || x == "-x" || x == "-fsyntax-only" || x == "-include-pch" ||
        x == "-fPIC" || x == "-fpic" || x == "-fPIE" || x == "-fpie" ||
        x == "-D__ASSEMBLY__" || x.starts_with("-DBUILD_VDSO") || x.starts_with("-D__DISABLE_EXPORTS"))
      return false;
    if (x.ends_with("/compiler_types.h"))
      ctypes = true;
    if (x.ends_with(".c") && i > 0 && llvm::StringRef(a[i - 1]) != "-o")
      input = a[i];
  }
  if (!ctypes || !input)
    return false;
  llvm::StringRef in = input;
  for (const char *bad : {"/vdso", "/kernel/pi/", "/boot/", "/efi/", "/purgatory/", "/scripts/"})
    if (in.contains(bad))
      return false;
  return true;
}

// Kör drivern i ett barnbarn. Om errpath != nullptr hamnar stderr där.
static int run_driver_forked(std::vector<const char *> argv, const char *errpath) {
  pid_t pid = fork();
  if (pid == 0) {
    if (errpath) {
      int fd = open(errpath, O_WRONLY | O_CREAT | O_TRUNC, 0644);
      if (fd >= 0) {
        dup2(fd, 2);
        close(fd);
      }
    }
    _exit(run_driver(argv));
  }
  int st;
  while (waitpid(pid, &st, 0) < 0 && errno == EINTR) {
  }
  return WIFEXITED(st) ? WEXITSTATUS(st) : 128 + WTERMSIG(st);
}

static bool exists(const std::string &p) {
  struct stat sb;
  return stat(p.c_str(), &sb) == 0;
}

static std::string ensure_pch(const std::vector<const char *> &argv) {
  std::string dir = ".vroom";
  std::string pch = dir + "/prefix.pch", failed = dir + "/pch.failed";
  if (exists(pch))
    return pch;
  if (exists(failed))
    return "";
  mkdir(dir.c_str(), 0755);
  int lk = open((dir + "/pch.lock").c_str(), O_CREAT | O_RDWR, 0644);
  flock(lk, LOCK_EX);
  if (!exists(pch) && !exists(failed)) {
    std::vector<std::string> keep;
    bool after_include = false;
    for (size_t i = 1; i < argv.size(); i++) {
      llvm::StringRef x = argv[i];
      if (x == "-include")
        after_include = true;
      if (x.starts_with("-Wp,-MMD") || x.starts_with("-DKBUILD_") ||
          x.starts_with("-D__KBUILD_MODNAME") || x == "-c")
        continue;
      if (x == "-o") {
        i++;
        continue;
      }
      if (x.ends_with(".c"))
        continue;
      if (after_include && x.starts_with("-I"))
        continue; // katalogspecifika -I
      keep.push_back(x.str());
    }
    // Samma -O som filerna: annars följer __OPTIMIZE__ med PCH:n in i -O0-filer.
    for (auto &f : g_optflags)
      keep.push_back(f.c_str());
    keep.push_back("-x");
    keep.push_back("c-header");
    keep.push_back(g_prefix);
    keep.push_back("-o");
    keep.push_back(pch + ".tmp");
    std::vector<const char *> a{g_clang.c_str()};
    for (auto &s : keep)
      a.push_back(s.c_str());
    if (run_driver_forked(a, (dir + "/pch.log").c_str()) == 0)
      rename((pch + ".tmp").c_str(), pch.c_str());
    else
      close(open(failed.c_str(), O_CREAT | O_WRONLY, 0644));
  }
  flock(lk, LOCK_UN);
  close(lk);
  return exists(pch) ? pch : "";
}

static std::vector<std::string> g_tryopt; // --try-opt: prova dessa nivåer först (t.ex. -O0)

static void log_tu(const char *file, const std::vector<const char *> &argv, const std::string &what) {
  int lf = open(file, O_WRONLY | O_CREAT | O_APPEND, 0644);
  if (lf < 0)
    return;
  std::string obj = argv.back();
  for (size_t i = 1; i + 1 < argv.size(); i++)
    if (!strcmp(argv[i], "-o")) {
      obj = argv[i + 1];
      break;
    }
  std::string line = what + " " + obj + "\n";
  (void)!write(lf, line.data(), line.size());
  close(lf);
}

// Kör ett försök i ett barn med fångad stderr; vid lyckat försök skrivs stderr ut.
static bool try_quiet(const std::vector<const char *> &a) {
  char errpath[] = "/tmp/vroomd-err.XXXXXX";
  int efd = mkstemp(errpath);
  close(efd);
  int rc = run_driver_forked(a, errpath);
  if (rc == 0) {
    int fd = open(errpath, O_RDONLY);
    char buf[65536];
    ssize_t n;
    while ((n = read(fd, buf, sizeof(buf))) > 0)
      (void)!write(2, buf, n);
    close(fd);
  }
  unlink(errpath);
  return rc == 0;
}

// Ett -O0-objekt med __init-kod kan få icke-inlinade static inline-kopior i .text
// som anropar .init.text (modpost: section mismatch). Sådana byggs om på nästa nivå.
static bool has_init_text(const std::vector<const char *> &argv) {
  const char *obj = nullptr;
  for (size_t i = 1; i + 1 < argv.size(); i++)
    if (!strcmp(argv[i], "-o"))
      obj = argv[i + 1];
  if (!obj)
    return false;
  int fd = open(obj, O_RDONLY);
  if (fd < 0)
    return false;
  std::string data;
  char buf[1 << 16];
  ssize_t n;
  while ((n = read(fd, buf, sizeof(buf))) > 0)
    data.append(buf, n);
  close(fd);
  static const char pat[] = ".init.text";
  return data.find(std::string(pat, sizeof(pat))) != std::string::npos; // inkl. NUL
}

static int run_kernel_tu(std::vector<const char *> argv) {
  std::vector<const char *> base(argv);
  for (auto &f : g_optflags)
    base.push_back(f.c_str()); // sist på raden: vinner över -O2

  std::string pch = ensure_pch(argv);
  std::vector<const char *> pre;
  if (!pch.empty())
    pre = {"-include-pch", pch.c_str()};

  // Lägre optimeringsnivåer först; kärnan kräver ofta DCE (compiletime_assert),
  // så vid fel faller vi tillbaka till nästa nivå och till sist --opt.
  for (auto &lvl : g_tryopt) {
    std::vector<const char *> a{argv[0]};
    a.insert(a.end(), pre.begin(), pre.end());
    a.insert(a.end(), argv.begin() + 1, argv.end());
    a.push_back(lvl.c_str());
    if (try_quiet(a)) {
      if (lvl == "-O0" && has_init_text(argv)) {
        log_tu(".vroom/opt.log", argv, "-O0-init-retry");
        continue;
      }
      log_tu(".vroom/opt.log", argv, lvl);
      return 0;
    }
  }
  if (!pch.empty()) {
    std::vector<const char *> a{argv[0]};
    a.insert(a.end(), pre.begin(), pre.end());
    a.insert(a.end(), base.begin() + 1, base.end());
    if (try_quiet(a)) {
      log_tu(".vroom/opt.log", argv, g_optflags.empty() ? "-O2" : g_optflags.back());
      return 0;
    }
    log_tu(".vroom/fallback.log", argv, "nopch");
  }
  log_tu(".vroom/opt.log", argv, (g_optflags.empty() ? "-O2" : g_optflags.back()) + "-nopch");
  return run_driver(base);
}

// ---------- barn: ta emot förfrågan ----------

static bool read_full(int fd, char *p, size_t n) {
  while (n) {
    ssize_t r = read(fd, p, n);
    if (r < 0 && errno == EINTR)
      continue;
    if (r <= 0)
      return false;
    p += r;
    n -= r;
  }
  return true;
}

[[noreturn]] static void child(int conn) {
  vroom_req_hdr h;
  int fds[3] = {-1, -1, -1};
  char cbuf[CMSG_SPACE(sizeof(fds))];
  iovec iov = {&h, sizeof(h)};
  msghdr m = {};
  m.msg_iov = &iov;
  m.msg_iovlen = 1;
  m.msg_control = cbuf;
  m.msg_controllen = sizeof(cbuf);
  ssize_t n = recvmsg(conn, &m, MSG_WAITALL);
  if (n != (ssize_t)sizeof(h) || h.magic != VROOM_MAGIC || h.payload_len > VROOM_MAX_PAYLOAD)
    _exit(1);
  for (cmsghdr *c = CMSG_FIRSTHDR(&m); c; c = CMSG_NXTHDR(&m, c))
    if (c->cmsg_level == SOL_SOCKET && c->cmsg_type == SCM_RIGHTS)
      memcpy(fds, CMSG_DATA(c), sizeof(fds));
  std::vector<char> payload(h.payload_len);
  if (!read_full(conn, payload.data(), payload.size()))
    _exit(1);

  // Packa upp: cwd, argv, envp
  std::vector<const char *> strs;
  for (size_t off = 0; off < payload.size();) {
    strs.push_back(&payload[off]);
    off += strlen(&payload[off]) + 1;
  }
  if (strs.size() != 1 + h.argc + h.envc)
    _exit(1);
  for (int i = 0; i < 3; i++)
    if (fds[i] >= 0) {
      dup2(fds[i], i);
      close(fds[i]);
    }
  if (chdir(strs[0]) != 0)
    _exit(1);
  std::vector<const char *> argv(strs.begin() + 1, strs.begin() + 1 + h.argc);
  static std::vector<char *> env;
  for (size_t i = 1 + h.argc; i < strs.size(); i++)
    env.push_back(const_cast<char *>(strs[i]));
  env.push_back(nullptr);
  environ = env.data();

  struct timespec t0, t1;
  clock_gettime(CLOCK_MONOTONIC, &t0);
  bool kern = kernel_eligible(argv);
  int32_t status = kern ? run_kernel_tu(argv) : run_driver(argv);
  clock_gettime(CLOCK_MONOTONIC, &t1);
  if (getenv("VROOM_TIMES")) {
    const char *out = "-";
    for (size_t i = 1; i + 1 < argv.size(); i++)
      if (!strcmp(argv[i], "-o"))
        out = argv[i + 1];
    char line[1024];
    double ms = (t1.tv_sec - t0.tv_sec) * 1e3 + (t1.tv_nsec - t0.tv_nsec) / 1e6;
    int n = snprintf(line, sizeof(line), "%.3f %.3f %.1f %s %s\n", t0.tv_sec + t0.tv_nsec / 1e9,
                     t1.tv_sec + t1.tv_nsec / 1e9, ms, kern ? "K" : "-", out);
    int fd = open(getenv("VROOM_TIMES"), O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (fd >= 0) {
      (void)!write(fd, line, n);
      close(fd);
    }
  }
  fflush(stdout);
  fflush(stderr);
  (void)!write(conn, &status, sizeof(status));
  _exit(0);
}

// ---------- förälder ----------

int main(int argc, char **argv) {
  const char *sock = "/tmp/vroomd.sock";
  for (int i = 1; i < argc; i++) {
    if (!strcmp(argv[i], "--sock") && i + 1 < argc)
      sock = argv[++i];
    else if (!strcmp(argv[i], "--clang") && i + 1 < argc)
      g_clang = argv[++i];
    else if (!strcmp(argv[i], "--prefix") && i + 1 < argc)
      g_prefix = argv[++i];
    else if (!strcmp(argv[i], "--opt") && i + 1 < argc)
      g_optflags.push_back(argv[++i]);
    else if (!strcmp(argv[i], "--try-opt") && i + 1 < argc)
      g_tryopt.push_back(argv[++i]);
  }

  llvm::InitializeAllTargets();
  llvm::InitializeAllTargetMCs();
  llvm::InitializeAllAsmPrinters();
  llvm::InitializeAllAsmParsers();

  signal(SIGCHLD, SIG_IGN); // barn städas automatiskt
  signal(SIGPIPE, SIG_IGN);

  int ls = socket(AF_UNIX, SOCK_STREAM, 0);
  sockaddr_un sa = {};
  sa.sun_family = AF_UNIX;
  strncpy(sa.sun_path, sock, sizeof(sa.sun_path) - 1);
  unlink(sock);
  if (bind(ls, (sockaddr *)&sa, sizeof(sa)) < 0 || listen(ls, 256) < 0) {
    perror("vroomd: bind/listen");
    return 1;
  }
  chmod(sock, 0600);
  fprintf(stderr, "vroomd: lyssnar på %s (clang %s)\n", sock, g_clang.c_str());

  for (;;) {
    int conn = accept(ls, nullptr, nullptr);
    if (conn < 0) {
      if (errno == EINTR)
        continue;
      perror("vroomd: accept");
      continue;
    }
    pid_t pid = fork();
    if (pid == 0) {
      close(ls);
      // SIG_IGN would be inherited, and Linux then reaps the driver's own
      // children (cc1, ld) itself: its waitpid() fails with ECHILD
      signal(SIGCHLD, SIG_DFL);
      child(conn);
    }
    close(conn);
  }
}
