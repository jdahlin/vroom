/*
 * vasm: assembler driver.  vasm [-o out.o] in.s  (input: preprocessed text)
 *       vasm --target=x86_64-linux-gnu [-o out.o] in.s  (x86-64 AT&T syntax, an ELF object; default is arm64 ELF)
 *       vasm --target=x86_64-w64-mingw32 [-o out.o] in.s  (x86-64 for Windows: a COFF object, .seh_* unwind tables)
 *       vasm --target=wasm64-unknown-emscripten [-o out.o] in.s  (LLVM's wasm syntax; only with make VCC_WASM=1)
 *       vasm --target=arm64-apple-macos [-o out.o] in.s  (Apple's syntax, a Mach-O object)
 *       vasm --bench N in.s...  (assemble each file N times, report MB/s)
 */
#include "as/as.h"
#include "common/cli-options.h"
#ifdef VCC_WASM
#include "arch/wasm/as/wasmas.h"
#endif
#include "obj/obj.h"
#include "port/performance-counters.h"
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

static const char *vasm_file_map(const char *path, size_t *n)
{
    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        perror(path);
        exit(1);
    }
    struct stat st;
    fstat(fd, &st);
    *n = (size_t)st.st_size;
    const char *p =
        (const char *)(*n ? mmap(NULL, *n, PROT_READ, MAP_PRIVATE, fd, 0) : "");
    close(fd);
    return p;
}

static double vasm_clock_seconds(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec + t.tv_nsec * 1e-9;
}

enum vasm_option {
    OPT_HELP,
    OPT_BENCH,
    OPT_TRACE,
    OPT_TRACE_ALL,
    OPT_OUTPUT,
    OPT_DEBUG,
    OPT_PRODUCER,
    OPT_DWARF,
    OPT_TARGET
};

static const vcli_option vasm_options[] = {
    [OPT_HELP] = {"--help", NULL, "Show available options."},
    [OPT_BENCH] = {"--bench", "N",
                   "As the first option, assemble the remaining files N times "
                   "and report throughput and a hash."},
    [OPT_TRACE] = {"--trace", NULL,
                   "As the first option, enable assembler tracing."},
    [OPT_TRACE_ALL] = {"--trace-all", NULL,
                       "As the first option, enable full assembler tracing."},
    [OPT_OUTPUT] = {"-o", "FILE", "Write the object to FILE (default: a.out)."},
    [OPT_DEBUG] = {"-g", NULL,
                   "Record the compilation directory for debug information."},
    [OPT_PRODUCER] = {"-dwarf-debug-producer", "TEXT",
                      "Set the DWARF producer string."},
    [OPT_DWARF] = {"-gdwarf-", "N", "Select DWARF version 2, 3, 4, or 5.",
                   true},
    [OPT_TARGET] = {"--target=", "TRIPLE",
                    "Select arm64 ELF (default), x86-64 ELF, x86-64 Windows "
                    "COFF, Apple arm64 Mach-O, or WebAssembly (when enabled).",
                    true},
    {NULL}};

int main(int argc, char **argv)
{
    const char *out = "a.out", *in = 0, *value;
    static char debug_directory[4096];
    const vcli_option *mode =
        vcli_match(vasm_options, argc - 1, argv + 1, &value);
    if (mode == &vasm_options[OPT_BENCH]) {
        int repetitions = atoi(value);
        int file_count = argc - 3;
        char **files = argv + 3;
        FILE *errors = stderr;
        if (repetitions) {
            as_context *a = as_new();
            ElfObj *o = elf_new();
            /* inputs mapped once and touched, so this measures the assembler */
            const char **ps =
                (const char **)(calloc((size_t)file_count, sizeof *ps));
            size_t *ns = (size_t *)(calloc((size_t)file_count, sizeof *ns)),
                   tin = 0, tout = 0;
            static uint8_t *buf;
            static size_t cap;
            volatile unsigned sink = 0;
            for (int i = 0; i < file_count; i++) {
                ps[i] = vasm_file_map(files[i], &ns[i]);
                for (size_t j = 0; j < ns[i]; j += 4096)
                    sink += (unsigned char)ps[i][j];
            }
            double best = 1e9;
            vperf vbest = {~0ull, ~0ull};
            uint64_t h = 1469598103934665603ull;
            for (int r = 0; r < repetitions; r++) {
                double t0 = vasm_clock_seconds();
                vperf v0 = vperformance_now();
                for (int i = 0; i < file_count; i++) {
                    const char *msg;
                    if (as_run(a, o, ps[i], ns[i], files[i], &msg)) {
                        fprintf(errors, "%s\n", msg);
                        return 1;
                    }
                    size_t k = elf_finish(o);
                    if (k > cap) {
                        cap = k * 2;
                        buf = (uint8_t *)realloc(buf, cap);
                    }
                    elf_emit(o, buf);
                    if (!r) {
                        tin += ns[i];
                        tout += k;
                        for (size_t b = 0; b < k; b++)
                            h = (h ^ buf[b]) * 1099511628211ull;
                    }
                }
                v0 = vperformance_subtract(vperformance_now(), v0);
                if (r) { /* the first pass also hashes the objects */
                    vbest.ins = v0.ins < vbest.ins ? v0.ins : vbest.ins;
                    vbest.cyc = v0.cyc < vbest.cyc ? v0.cyc : vbest.cyc;
                }
                double t = vasm_clock_seconds() - t0;
                if (t < best)
                    best = t;
            }
            uint64_t st, sb;
            as_statistics_get(a, &st, &sb);
            st /= (uint64_t)repetitions;
            sb /= (uint64_t)repetitions;
            printf(
                "%.3f ms/pass (best of %d): in %zu B %.1f MB/s, out %zu B %.1f "
                "MB/s, %llu statements %.1f M/s, %.1f MB/s expanded\n",
                best * 1e3, repetitions, tin, tin / best / 1e6, tout,
                tout / best / 1e6, (unsigned long long)st, st / best / 1e6,
                sb / best / 1e6);
            printf(
                "perf: %llu instructions, %llu cycles (min of %d passes after "
                "the first)\nhash: %016llx\n",
                (unsigned long long)vbest.ins, (unsigned long long)vbest.cyc,
                repetitions - 1, (unsigned long long)h);
            return 0;
        }
    }
    if (mode == &vasm_options[OPT_TRACE] ||
        mode == &vasm_options[OPT_TRACE_ALL]) {
        as_trace_level = mode == &vasm_options[OPT_TRACE] ? 1 : 2;
        argv++;
        argc--;
    }
    as_context *a = as_new();
    ElfObj *o = elf_new();
    int wasm = 0, macho = 0;
    for (int i = 1; i < argc; i++) {
        const vcli_option *option =
            vcli_match(vasm_options, argc - i, argv + i, &value);
        if (option == &vasm_options[OPT_HELP]) {
            puts("Usage: vasm [OPTIONS] INPUT\n       vasm --bench N FILE...");
            vcli_print_options(stdout, vasm_options);
            return 0;
        } else if (option == &vasm_options[OPT_OUTPUT])
            out = argv[++i];
        else if (option == &vasm_options[OPT_DEBUG])
            as_debug_directory_set(
                a, getcwd(debug_directory, sizeof debug_directory));
        else if (option == &vasm_options[OPT_PRODUCER])
            as_dwarf_producer_set(a, argv[++i]);
        else if (option == &vasm_options[OPT_DWARF] && value[0] >= '2' &&
                 value[0] <= '5' && !value[1])
            as_dwarf_version_set(a, value[0] - '0');
        else if (option == &vasm_options[OPT_TARGET] &&
                 !strncmp(value, "wasm", 4))
            wasm = strncmp(value + 4, "32", 2) ? 64 : 32;
        else if (option == &vasm_options[OPT_TARGET] &&
                 !strncmp(value, "x86_64", 6)) {
            elf_set_machine(o, EM_X86_64);
            if (strstr(argv[i], "mingw") ||
                strstr(argv[i],
                       "windows")) /* x86_64-w64-mingw32: a COFF object */
                obj_set_format(o, OBJ_COFF);
        } else if (option == &vasm_options[OPT_TARGET] &&
                   (strstr(argv[i], "darwin") || strstr(argv[i], "apple") ||
                    strstr(argv[i], "macos")))
            macho = 1;
        else
            in = argv[i];
    }
    if (!in) {
        fprintf(stderr, "usage: vasm "
                        "[--target=wasm64-...|arm64-apple-macos|x86_64-linux-"
                        "gnu|x86_64-w64-mingw32] [-g [-dwarf-debug-producer "
                        "S]] [-gdwarf-N] [-o out.o] in.s\n");
        return 2;
    }
    size_t n;
    const char *p = vasm_file_map(in, &n);
    const char *msg;
#ifndef VCC_WASM
    if (wasm) {
        fprintf(stderr, "vcc: error: this build has no WebAssembly support "
                        "(make VCC_WASM=1)\n");
        return 1;
    }
#else
    if (wasm) {
        /* LLVM's wasm .s to a wasm object (arch/wasm/as, platform/wasm) */
        uint8_t *ob;
        size_t on;
        char err[512];
        if (wasm_assemble(p, n, wasm == 32, &ob, &on, err, sizeof err)) {
            fprintf(stderr, "%s: %s\n", in, err);
            return 1;
        }
        FILE *f = fopen(out, "wb");
        if (!f || fwrite(ob, 1, on, f) != on || fclose(f)) {
            perror(out);
            return 1;
        }
        free(ob);
        return 0;
    }
#endif
    if (macho) {
        /* Apple's dialect, as vpp -c for a Darwin target: back to ours, then the Mach-O writer */
        size_t dn;
        char *d = macho_dialect_translate(p, n, &dn);
        if (as_run(a, o, d, dn, in, &msg)) {
            fprintf(stderr, "%s\n", msg);
            return 1;
        }
        size_t on = macho_finish(o);
        if (!on || o->has_err) {
            fprintf(stderr, "%s: %s\n", in,
                    o->err[0] ? o->err : "cannot lay out the object");
            return 1;
        }
        uint8_t *ob = (uint8_t *)malloc(on);
        FILE *f = fopen(out, "wb");
        if (!ob)
            return 1;
        macho_emit(o, ob);
        if (!f || fwrite(ob, 1, on, f) != on || fclose(f)) {
            perror(out);
            return 1;
        }
        free(ob);
        free(d);
        return 0;
    }
    if (as_run(a, o, p, n, in, &msg)) {
        fprintf(stderr, "%s\n", msg);
        return 1;
    }
    if (elf_write_file(o, out)) {
        fprintf(stderr, "%s: %s\n", in, o->err);
        return 1;
    }
    return 0;
}
