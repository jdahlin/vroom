/* Generated from LLVM 23 X86TargetParser feature facts; no runtime LLVM dependency. */
#ifndef VCC_X86FEATURES_H
#define VCC_X86FEATURES_H
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#define X86_FEATURE_WORDS 3
typedef struct x86_features { uint64_t bits[X86_FEATURE_WORDS]; } x86_features;
#include "x86features-data.inc"
static inline bool x86_feature_has(x86_features f, unsigned bit)
{ return (f.bits[bit / 64] >> (bit % 64)) & 1; }
static inline int x86_feature_find(const char *name)
{ for (unsigned i = 0; i < X86_FEATURE_COUNT; i++) if (!strcmp(name, x86_feature_names[i])) return (int)i; return -1; }
static inline bool x86_cpu_features(const char *name, x86_features *out)
{ for (unsigned i = 0; i < sizeof(x86_cpus)/sizeof(*x86_cpus); i++) if (!strcmp(name, x86_cpus[i].name)) { *out = x86_cpus[i].features; for (unsigned bit = 0; bit < X86_FEATURE_COUNT; bit++) if (x86_feature_has(*out, bit)) for (unsigned j = 0; j < X86_FEATURE_WORDS; j++) out->bits[j] |= x86_feature_deps[bit][1].bits[j]; return true; } return false; }
static inline bool x86_features_subset(x86_features need, x86_features have)
{ for (unsigned i = 0; i < X86_FEATURE_WORDS; i++) if (need.bits[i] & ~have.bits[i]) return false; return true; }
/* Frontend aliases expand as ordered primitive feature changes. */
static inline x86_features x86_feature_change(unsigned bit, bool enable)
{
	x86_features change = x86_feature_deps[bit][enable];
	if (bit == X86_FEATURE_APXF) {
		const unsigned sub[] = {X86_FEATURE_EGPR,X86_FEATURE_PUSH2POP2,X86_FEATURE_PPX,X86_FEATURE_NDD,X86_FEATURE_CCMP,X86_FEATURE_NF,X86_FEATURE_ZU,X86_FEATURE_JMPABS};
		for (unsigned k = 0; k < sizeof(sub)/sizeof(*sub); k++)
			for (unsigned i = 0; i < X86_FEATURE_WORDS; i++) change.bits[i] |= x86_feature_deps[sub[k]][enable].bits[i];
	}
	return change;
}
static inline void x86_feature_apply(x86_features *f, unsigned bit, bool enable)
{
	x86_features change = x86_feature_change(bit, enable);
	for (unsigned i = 0; i < X86_FEATURE_WORDS; i++) {
		if (enable) f->bits[i] |= change.bits[i]; else f->bits[i] &= ~change.bits[i];
	}
}
/* Frontend-only weak implications, applied after explicit options and strong dependencies. */
static inline void x86_features_finish(x86_features *f, x86_features disabled)
{
	static const unsigned weak[][2] = {{X86_FEATURE_SSE42,X86_FEATURE_POPCNT},{X86_FEATURE_SSE42,X86_FEATURE_CRC32},{X86_FEATURE_SSE,X86_FEATURE_MMX},{X86_FEATURE_AVX,X86_FEATURE_XSAVE}};
	for (unsigned i = 0; i < sizeof(weak)/sizeof(*weak); i++)
		if (x86_feature_has(*f,weak[i][0]) && !x86_feature_has(disabled,weak[i][1]))
			f->bits[weak[i][1]/64] |= UINT64_C(1) << (weak[i][1]%64);
}
#endif
