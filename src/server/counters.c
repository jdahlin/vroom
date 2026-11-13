/* The request counters and their printout. */
#include "vccd-internal.h"

atomic_ulong n_probe, n_probe_cc, n_fwd, n_spawn, n_pp, n_cc, n_fail, n_fallback, n_ar, n_ld, n_host,
	n_objcopy, n_sh, n_sh_decl;
VPRIV void print_counts(void)
{
	fprintf(stderr, "vccd: %lu probes (%lu compiled), %lu -E, %lu compiled by vcc, %lu failed, %lu fell back to clang, %lu host, "
		"%lu forwarded in all, %lu spawned, %lu ar, %lu links, %lu nm/objcopy, %lu recipes run, %lu passed to sh; "
		"%lu builds (cache flushes)\n",
		atomic_load(&n_probe), atomic_load(&n_probe_cc), atomic_load(&n_pp), atomic_load(&n_cc), atomic_load(&n_fail),
		atomic_load(&n_fallback), atomic_load(&n_host), atomic_load(&n_fwd), atomic_load(&n_spawn),
		atomic_load(&n_ar), atomic_load(&n_ld), atomic_load(&n_objcopy),
		atomic_load(&n_sh), atomic_load(&n_sh_decl), atomic_load(&n_flush));
	{	/* the shared caches, as vpp --batch prints them */
		char hs[512];
		struct rusage ru;

		pp_header_cache_statistics_format(hs, sizeof(hs));
		fprintf(stderr, "vccd: %s\n", hs);
		dc_stats(hs, sizeof(hs));
		fprintf(stderr, "vccd: %s\n", hs);
		getrusage(RUSAGE_SELF, &ru);
		fprintf(stderr, "vccd: cpu user %.2f s sys %.2f s, max rss %.0f MB\n",
			ru.ru_utime.tv_sec + ru.ru_utime.tv_usec / 1e6, ru.ru_stime.tv_sec + ru.ru_stime.tv_usec / 1e6,
#ifdef __APPLE__
			ru.ru_maxrss / 1048576.0);
#else
			ru.ru_maxrss / 1024.0);
#endif
	}
}
