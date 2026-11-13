/*
 * counters.c: the request counters (one per kind of request) and what vccd prints about itself.
 *
 * The counters are atomics bumped by every worker and printed by vccd_counters_print on SIGUSR1, at a
 * restart and at exit: what the benchmarks read to see how many TUs vcc compiled
 * itself and how many fell back to clang.
 * Also prints the shared caches' statistics and the process's CPU time and peak RSS.  Adding a
 * counter means adding it to the format and to the extern list in vccd-internal.h.
 */
#include "vccd-internal.h"

atomic_ulong vccd_probe_count, vccd_probe_compile_count, vccd_forward_count, vccd_spawn_count, vccd_preprocess_count, n_cc, vccd_failure_count, vccd_fallback_count, vccd_archive_count, vccd_link_count, vccd_host_count,
	vccd_objcopy_count, vccd_recipe_count, vccd_recipe_declined_count;
VPRIV void vccd_counters_print(void)
{
	fprintf(stderr, "vccd: %lu probes (%lu compiled), %lu -E, %lu compiled by vcc, %lu failed, %lu fell back to clang, %lu host, "
		"%lu forwarded in all, %lu spawned, %lu ar, %lu links, %lu nm/objcopy, %lu recipes run, %lu passed to sh; "
		"%lu builds (cache flushes)\n",
		atomic_load(&vccd_probe_count), atomic_load(&vccd_probe_compile_count), atomic_load(&vccd_preprocess_count), atomic_load(&n_cc), atomic_load(&vccd_failure_count),
		atomic_load(&vccd_fallback_count), atomic_load(&vccd_host_count), atomic_load(&vccd_forward_count), atomic_load(&vccd_spawn_count),
		atomic_load(&vccd_archive_count), atomic_load(&vccd_link_count), atomic_load(&vccd_objcopy_count),
		atomic_load(&vccd_recipe_count), atomic_load(&vccd_recipe_declined_count), atomic_load(&vccd_flush_count));
	{	/* the shared caches, as vpp --batch prints them */
		char hs[512];
		struct rusage ru;

		pp_header_cache_statistics_format(hs, sizeof(hs));
		fprintf(stderr, "vccd: %s\n", hs);
		driver_declaration_cache_statistics_format(hs, sizeof(hs));
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
