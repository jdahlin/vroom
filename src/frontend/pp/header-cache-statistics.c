/* vcc preprocessor: the header cache's counters and their summary line */
#include "pp-internal.h"

pp_header_cache_statistics_counters pp_header_cache_statistics;

void pp_header_cache_statistics_format(char *buf, size_t n)
{
	snprintf(buf, n, "header cache: %lu hits, %lu misses (%lu recorded, %lu uncacheable, %lu not stored), "
		 "%.1f MB replayed, %.1f MB recorded, %.1f MB stored",
		 (unsigned long)pp_header_cache_statistics.hit, (unsigned long)pp_header_cache_statistics.miss, (unsigned long)pp_header_cache_statistics.rec,
		 (unsigned long)pp_header_cache_statistics.uncacheable, (unsigned long)pp_header_cache_statistics.full, pp_header_cache_statistics.bytes_replayed / 1e6,
		 pp_header_cache_statistics.bytes_rec / 1e6, pp_header_cache_statistics.mem / 1e6);
	if (getenv("VPP_HC_DEBUG")) {
		size_t l = strlen(buf);

		snprintf(buf + l, n - l, "\n  stored: %lu undef reads, %lu other reads, %lu writes, %lu files",
			 (unsigned long)pp_header_cache_statistics.nru, (unsigned long)pp_header_cache_statistics.nrv, (unsigned long)pp_header_cache_statistics.ndl,
			 (unsigned long)pp_header_cache_statistics.nfe);
		l = strlen(buf);
		snprintf(buf + l, n - l, "\n  bytes: reads %.1f MB, delta %.1f MB, once %.1f MB, files %.1f MB, text %.1f MB, "
			 "segs %.1f MB, entries %.1f MB",
			 pp_header_cache_statistics.b_rd / 1e6, pp_header_cache_statistics.b_dl / 1e6, pp_header_cache_statistics.b_once / 1e6, pp_header_cache_statistics.b_fe / 1e6, pp_header_cache_statistics.b_text / 1e6,
			 pp_header_cache_statistics.b_seg / 1e6, pp_header_cache_statistics.b_ent / 1e6);
	}
}
