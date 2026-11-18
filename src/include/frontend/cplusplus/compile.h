#ifndef VCC_FRONTEND_CPLUSPLUS_COMPILE_H
#define VCC_FRONTEND_CPLUSPLUS_COMPILE_H
/*
 * compile.h: one C++ translation unit compiled for the driver
 * (driver/job.cpp; doc/PLAN-cxx-sema.md S1e): preprocessed, parsed and
 * analysed by frontend/cplusplus, lowered to C's AST and compiled by C's
 * code generator.  The interface is C's, so that the driver needs no C++
 * type; only a C++-hosted build (VCC_LANG=c++) has the front end.
 */
#include "frontend/c/cc.h"
#include "frontend/pp/pp.h"

#ifdef __cplusplus
extern "C" {
#endif

struct cplusplus_compile_options {
    const char *path; /* the main file */
    int edition;      /* the standard's year as -std= gives it: 98 .. 26 */
    int gnu;          /* -std=gnu++NN */
    /* clang's predefined macros and the command line's -D/-U/-include, as
       #define/#undef/#include lines (the driver's predefine text) */
    const char *predefines;
    size_t predefines_length;
    const pp_opts *search; /* the include search path (dirs, nquote, ...) */
    int errfd;             /* where diagnostics go */
    /* the options the front end reads itself, in command-line order:
       -W..., -w, -pedantic, -pedantic-errors, -fms-extensions,
       -fms-compatibility, -f[no-]char8_t, -[f][no-]trigraphs,
       -f[no-]dollars-in-identifiers, -fno-operator-names, and the
       developer outputs -fcxx-dump=WHAT[,WHAT...] (doc/PLAN-cxx-sema.md;
       `vcc -fcxx-dump=help`) */
    const char *const *arguments;
    int argument_count;
    /* --embed-dir */
    const char *const *embed_directories;
    int embed_directory_count;
    int preprocessed_input; /* the main file is a .i or .ii */
};

/* 0 if the translation unit compiled; 1 after an error, a diagnostic on
   errfd (also for a construct lowering has no rule for yet).  With
   -fcxx-dump= the outputs asked for go to co->outfd, the translation unit
   is taken only as far as they need, and code is generated only when -S or
   -c asks for it too */
int cplusplus_compile(const struct cplusplus_compile_options *o, pp *p,
                      const cc_options *co);

#ifdef __cplusplus
}
#endif

#endif
