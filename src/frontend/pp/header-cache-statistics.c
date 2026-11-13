/* vcc preprocessor: the header cache's counters and their summary line */
#include "ppimpl.h"

hstat_t hstat;

void pp_hc_stats(char *buf, size_t n)
{
	snprintf(buf, n, "header cache: %lu hits, %lu misses (%lu recorded, %lu uncacheable, %lu not stored), "
		 "%.1f MB replayed, %.1f MB recorded, %.1f MB stored",
		 (unsigned long)hstat.hit, (unsigned long)hstat.miss, (unsigned long)hstat.rec,
		 (unsigned long)hstat.uncacheable, (unsigned long)hstat.full, hstat.bytes_replayed / 1e6,
		 hstat.bytes_rec / 1e6, hstat.mem / 1e6);
	if (getenv("VPP_HC_DEBUG")) {
		size_t l = strlen(buf);

		snprintf(buf + l, n - l, "\n  stored: %lu undef reads, %lu other reads, %lu writes, %lu files",
			 (unsigned long)hstat.nru, (unsigned long)hstat.nrv, (unsigned long)hstat.ndl,
			 (unsigned long)hstat.nfe);
		l = strlen(buf);
		snprintf(buf + l, n - l, "\n  bytes: reads %.1f MB, delta %.1f MB, once %.1f MB, files %.1f MB, text %.1f MB, "
			 "segs %.1f MB, entries %.1f MB",
			 hstat.b_rd / 1e6, hstat.b_dl / 1e6, hstat.b_once / 1e6, hstat.b_fe / 1e6, hstat.b_text / 1e6,
			 hstat.b_seg / 1e6, hstat.b_ent / 1e6);
	}
}
