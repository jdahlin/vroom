/*
 * x86predefines.h: the x86-64 feature and CPU macros (__AVX2__, __znver3__ ...) for predef.c.
 *
 * Included by predef.c after the buffer type, so it needs `buf` and `bstr`.  The tables
 * (x86macros-data.inc) are generated from clang by src/tools/gen-x86features.py; the functions here
 * emit `#undef` then `#define` for every feature macro whose required features are all in the job's
 * set, and the chosen CPU's own macros, so no LLVM library is needed at run time.
 */
#ifndef VCC_X86PREDEFINES_H
#define VCC_X86PREDEFINES_H
#include "arch/x64/x86features.h"
#include "x86macros-data.inc"
static inline void x86_macro_line(buf *pre, const char *name, const char *value)
{
	bstr(pre, value ? "#define " : "#undef ");
	bstr(pre, name);
	if (value) { bstr(pre, " "); bstr(pre, value); }
	bstr(pre, "\n");
}
static inline void x86_feature_predefines(buf *pre, x86_features features, const char *cpu)
{
	for (unsigned i = 0; i < sizeof(x86_feature_macros)/sizeof(*x86_feature_macros); i++) {
		x86_macro_line(pre, x86_feature_macros[i].name, NULL);
		if (x86_features_subset(x86_feature_macros[i].required, features))
			x86_macro_line(pre, x86_feature_macros[i].name, x86_feature_macros[i].value);
	}
	for (unsigned i = 0; i < sizeof(x86_cpu_macro_names)/sizeof(*x86_cpu_macro_names); i++)
		x86_macro_line(pre, x86_cpu_macro_names[i], NULL);
	for (unsigned i = 0; i < sizeof(x86_cpu_macros)/sizeof(*x86_cpu_macros); i++)
		if (!strcmp(cpu, x86_cpu_macros[i].cpu))
			x86_macro_line(pre, x86_cpu_macros[i].name, x86_cpu_macros[i].value);
}
#endif
