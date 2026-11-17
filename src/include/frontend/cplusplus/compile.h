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
};

/* 0 if the translation unit compiled; 1 after an error, a diagnostic on
   errfd (also for a construct lowering has no rule for yet) */
int cplusplus_compile(const struct cplusplus_compile_options *o, pp *p,
                      const cc_options *co);

#ifdef __cplusplus
}
#endif

#endif
