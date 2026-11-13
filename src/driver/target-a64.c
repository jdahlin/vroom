/*
 * target-a64.c: the arm64 property of the command line that needs a table: does -march or -mcpu name
 * a core with LSE atomics.
 *
 * arch_lse answers as clang's __ARM_FEATURE_ATOMICS would; -1 means "no opinion, keep the previous
 * answer" so that the option loop can take -march and -mcpu in command-line order.  `+lse` and
 * `+nolse` suffixes override the base.  A -mcpu name that is not in the list of old cores (including
 * `native`, which here is an Apple core) has LSE.  The result becomes a predefined macro and the code
 * generator's atomics choice, so a wrong answer changes generated code, not just a macro.
 */
#include "driver-internal.h"

/* does -march=/-mcpu= name a target with LSE atomics (as clang's
   __ARM_FEATURE_ATOMICS)?  -1: no opinion (keep the previous answer) */
VPRIV int arch_lse(const char *v, bool cpu)
{
	const char *plus = strchr(v, '+');
	size_t n = plus ? (size_t)(plus - v) : strlen(v);
	int lse = -1;

	if (!cpu) {
		if (n == 7 && !strncmp(v, "armv8-a", 7))
			lse = 0;
		else if (n >= 5 && (!strncmp(v, "armv8.", 6) || !strncmp(v, "armv9", 5)))
			lse = 1;	/* armv8.1-a and later, armv8-r has none */
		if (n >= 7 && !strncmp(v, "armv8-r", 7))
			lse = 0;
	} else {
		static const char *const old[] = { "generic", "cortex-a35", "cortex-a53", "cortex-a57", "cortex-a72",
						   "cortex-a73", "thunderx", "thunderxt88", "xgene1", "exynos-m3",
						   "cyclone", "apple-a7", "apple-a8", "apple-a9", "apple-a10", "falkor",
						   "kryo", "thunderxt81", "thunderxt83", "cortex-r82", NULL };

		lse = 1;	/* native here is an Apple core */
		for (int i = 0; old[i]; i++)
			if (strlen(old[i]) == n && !strncmp(v, old[i], n))
				lse = 0;
	}
	for (const char *p = plus; p; p = strchr(p + 1, '+')) {
		if (!strncmp(p, "+lse", 4) && (!p[4] || p[4] == '+'))
			lse = 1;
		else if (!strncmp(p, "+nolse", 6) && (!p[6] || p[6] == '+'))
			lse = 0;
	}
	return lse;
}
