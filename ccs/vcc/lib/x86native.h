/* Native x86 CPU discovery. LLVM-derived model/CPUID policy is in the generated
 * data include; instruction execution is confined to the matching host build. */
#ifndef VCC_X86NATIVE_H
#define VCC_X86NATIVE_H
#include "x86features.h"
/* Injectable reads let tests exercise missing leaves and OS-save masks without
 * executing unsupported instructions on the test machine. Return true on error. */
typedef struct x86_native_io {
	void *context;
	bool (*cpuid)(void *, unsigned, unsigned, unsigned *, unsigned *, unsigned *, unsigned *);
	bool (*xcr0)(void *, unsigned *, unsigned *);
} x86_native_io;
static inline bool x86_native_cpuid(const x86_native_io *io, unsigned leaf, unsigned sub,
	unsigned *a, unsigned *b, unsigned *c, unsigned *d)
{ return io->cpuid(io->context, leaf, sub, a, b, c, d); }
static inline bool x86_native_cpuid0(const x86_native_io *io, unsigned leaf,
	unsigned *a, unsigned *b, unsigned *c, unsigned *d)
{ return x86_native_cpuid(io, leaf, 0, a, b, c, d); }
static inline bool x86_native_xcr0(const x86_native_io *io, unsigned *a, unsigned *d)
{ return io->xcr0(io->context, a, d); }
#include "x86native-data.inc"
static inline bool x86_native_detect(const x86_native_io *io, x86_features *features,
	x86_features *disabled, const char **cpu)
{
	unsigned max, a, b, c, d;
	if (x86_native_cpuid0(io, 0, &max, &b, &c, &d) || max < 1) return false;
	char vendor[13];
	memcpy(vendor, &b, 4); memcpy(vendor + 4, &d, 4); memcpy(vendor + 8, &c, 4); vendor[12] = 0;
	if (x86_native_cpuid0(io, 1, &a, &b, &c, &d)) return false;
	unsigned family = (a >> 8) & 15, model = (a >> 4) & 15;
	if (family == 6 || family == 15) model |= ((a >> 16) & 15) << 4;
	if (family == 15) family += (a >> 20) & 255;
	x86_features name_features = x86_native_name_features(io, c, d, max);
	const char *name = NULL;
	if (!strcmp(vendor, "GenuineIntel")) name = x86_native_intel(family, model, name_features);
	else if (!strcmp(vendor, "AuthenticAMD")) name = x86_native_amd(family, model, name_features);
	else if (!strcmp(vendor, "HygonGenuine")) name = x86_native_hygon(family, model, name_features);
	bool values[X86_FEATURE_COUNT] = {0};
	if (!x86_native_values(io, values) || !values[X86_FEATURE_64BIT]) return false;
	/* LLVM's generic CPU has no identity predefines. Retain the mandatory
	 * x86-64 baseline while allowing every detected negative to clear it. */
	if (!name || !x86_cpu_features(name, features)) {
		name = "";
		x86_cpu_features("x86-64", features);
	}
	*disabled = (x86_features){{0}};
	for (unsigned i = 0; i < sizeof(x86_native_known)/sizeof(*x86_native_known); i++) {
		unsigned bit = x86_native_known[i];
		if (values[bit]) x86_feature_apply(features, bit, true);
	}
	for (unsigned i = 0; i < sizeof(x86_native_known)/sizeof(*x86_native_known); i++) {
		unsigned bit = x86_native_known[i];
		if (!values[bit]) {
			x86_feature_apply(features, bit, false);
			disabled->bits[bit / 64] |= UINT64_C(1) << (bit % 64);
		}
	}
	*cpu = name;
	return true;
}
#if defined(__x86_64__) || defined(__i386__)
static inline bool x86_native_host_cpuid(void *context, unsigned leaf, unsigned sub,
	unsigned *a, unsigned *b, unsigned *c, unsigned *d)
{
	(void)context;
	__asm__ volatile("cpuid" : "=a"(*a), "=b"(*b), "=c"(*c), "=d"(*d) : "a"(leaf), "c"(sub));
	return false;
}
static inline bool x86_native_host_xcr0(void *context, unsigned *a, unsigned *d)
{
	(void)context;
	__asm__ volatile("xgetbv" : "=a"(*a), "=d"(*d) : "c"(0));
	return false;
}
#endif
static inline bool x86_native_features(x86_features *features, x86_features *disabled, const char **cpu)
{
#if defined(__x86_64__) || defined(__i386__)
	const x86_native_io io = {NULL, x86_native_host_cpuid, x86_native_host_xcr0};
	return x86_native_detect(&io, features, disabled, cpu);
#else
	(void)features; (void)disabled; (void)cpu;
	return false;
#endif
}
#endif
