/*
 * pmc: Apple PMU counters (cycles, L1D misses, TLB misses, mispredicts)
 * for the calling thread, through the private kperf frameworks, as in
 * Instruments' CPU Counters.  Needs root (kpc_force_all_ctrs_set).
 */
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pmc.h"

typedef struct kpep_db kpep_db;
typedef struct kpep_config kpep_config;
typedef struct kpep_event kpep_event;

#define KPC_MAX 32

static int (*kpc_force_all_ctrs_set)(int);
static int (*kpc_set_config)(uint32_t, uint64_t *);
static int (*kpc_set_counting)(uint32_t);
static int (*kpc_set_thread_counting)(uint32_t);
static int (*kpc_get_thread_counters)(uint32_t, uint32_t, uint64_t *);
static int (*kpep_db_create)(const char *, kpep_db **);
static int (*kpep_db_event)(kpep_db *, const char *, kpep_event **);
static int (*kpep_config_create)(kpep_db *, kpep_config **);
static int (*kpep_config_force_counters)(kpep_config *);
static int (*kpep_config_add_event)(kpep_config *, kpep_event **, uint32_t, uint32_t *);
static int (*kpep_config_kpc_classes)(kpep_config *, uint32_t *);
static int (*kpep_config_kpc_count)(kpep_config *, size_t *);
static int (*kpep_config_kpc_map)(kpep_config *, size_t *, size_t);
static int (*kpep_config_kpc)(kpep_config *, uint64_t *, size_t);

const char *pmc_names[PMC_N] = {
	"FIXED_CYCLES", "FIXED_INSTRUCTIONS", "L1D_CACHE_MISS_LD_NONSPEC",
	"L1D_CACHE_MISS_ST_NONSPEC", "L1D_TLB_MISS_NONSPEC", "L2_TLB_MISS_DATA",
};
int pmc_n = 6;
static size_t map[KPC_MAX];
static int have[PMC_N];

#define SYM(h, f) do { if (!(*(void **)&f = dlsym(h, #f))) { fprintf(stderr, "pmc: no %s\n", #f); return -1; } } while (0)

int pmc_init(const char *events)
{
	void *k = dlopen("/System/Library/PrivateFrameworks/kperf.framework/kperf", RTLD_LAZY);
	void *d = dlopen("/System/Library/PrivateFrameworks/kperfdata.framework/kperfdata", RTLD_LAZY);
	kpep_db *db;
	kpep_config *cfg;
	uint64_t regs[KPC_MAX] = { 0 };
	uint32_t classes = 0;
	size_t n = 0;

	if (!k || !d)
		return -1;
	SYM(k, kpc_force_all_ctrs_set); SYM(k, kpc_set_config); SYM(k, kpc_set_counting);
	SYM(k, kpc_set_thread_counting); SYM(k, kpc_get_thread_counters);
	SYM(d, kpep_db_create); SYM(d, kpep_db_event); SYM(d, kpep_config_create);
	SYM(d, kpep_config_force_counters); SYM(d, kpep_config_add_event);
	SYM(d, kpep_config_kpc_classes); SYM(d, kpep_config_kpc_count);
	SYM(d, kpep_config_kpc_map); SYM(d, kpep_config_kpc);
	if (kpc_force_all_ctrs_set(1)) {
		fprintf(stderr, "pmc: kpc_force_all_ctrs_set failed (run as root)\n");
		return -1;
	}
	if (kpep_db_create(NULL, &db) || kpep_config_create(db, &cfg) ||
	    kpep_config_force_counters(cfg))
		return -1;
	if (events && strchr(events, '_')) {	/* "EV1,EV2,...": the cycles + instructions first */
		char *l = strdup(events), *t;

		pmc_n = 2;
		while ((t = strsep(&l, ",")) && pmc_n < PMC_N)
			if (*t)
				pmc_names[pmc_n++] = t;
	}
	for (int i = 0; i < pmc_n; i++) {
		kpep_event *ev;

		if (kpep_db_event(db, pmc_names[i], &ev)) {
			fprintf(stderr, "pmc: no event %s\n", pmc_names[i]);
			continue;
		}
		if (kpep_config_add_event(cfg, &ev, 0, NULL)) {
			fprintf(stderr, "pmc: cannot add %s\n", pmc_names[i]);
			continue;
		}
		have[i] = 1;
	}
	if (kpep_config_kpc_classes(cfg, &classes) || kpep_config_kpc_count(cfg, &n) ||
	    kpep_config_kpc_map(cfg, map, sizeof(map)) || kpep_config_kpc(cfg, regs, sizeof(regs)))
		return -1;
	if (kpc_set_config(classes, regs) || kpc_set_counting(classes) ||
	    kpc_set_thread_counting(classes)) {
		fprintf(stderr, "pmc: kpc_set_* failed\n");
		return -1;
	}
	return 0;
}

void pmc_read(uint64_t v[PMC_N])
{
	uint64_t c[KPC_MAX] = { 0 };
	int j = 0;

	kpc_get_thread_counters(0, KPC_MAX, c);
	for (int i = 0; i < pmc_n; i++)
		v[i] = have[i] ? c[map[j++]] : 0;
}
