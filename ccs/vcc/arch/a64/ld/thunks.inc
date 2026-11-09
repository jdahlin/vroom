/* arm64 range-extension thunks: their records and the (isec, reloc) -> thunk map; part of ld/ld.c (#included there) */

/* ---------------- thunks ---------------- */

typedef struct {
	uint32_t g;			/* destination global (or NONE) */
	uint32_t sec;			/* destination ISec for local/section targets */
	uint64_t val;
	int64_t addend;
	uint32_t tsec;			/* thunk ISec */
	uint32_t off;			/* offset in thunk section */
	uint32_t size;			/* 4 (b) or 12 (adrp/add/br) */
	bool lng;			/* long form; sticky once chosen (lld) */
	const char *name;
} Thunk;
static VVEC(Thunk) thunks;
typedef struct { VVEC(uint32_t) list; } ThunkSec;
static VVEC(ThunkSec) thunksecs;

/* thunk redirection: (isec, reloc index) -> thunk; open hash */
static uint64_t *tr_keys;
static uint32_t *tr_vals;
static uint32_t tr_mask, tr_n;

static void tr_put(uint32_t isec, uint32_t ri, uint32_t th)
{
	if (!tr_keys || tr_n * 2 >= tr_mask) {
		uint32_t nm = tr_keys ? tr_mask * 2 + 1 : 1023;
		uint64_t *nk = alloc((uint64_t)(nm + 1) * 8);
		uint32_t *nv = alloc((uint64_t)(nm + 1) * 4);
		memset(nk, 0xff, (uint64_t)(nm + 1) * 8);
		for (uint32_t i = 0; tr_keys && i <= tr_mask; i++)
			if (tr_keys[i] != UINT64_MAX) {
				uint32_t j = (uint32_t)(tr_keys[i] * 0x9E3779B97F4A7C15ULL >> 32) & nm;
				while (nk[j] != UINT64_MAX)
					j = (j + 1) & nm;
				nk[j] = tr_keys[i];
				nv[j] = tr_vals[i];
			}
		tr_keys = nk;
		tr_vals = nv;
		tr_mask = nm;
	}
	isecs.v[isec].thunked = true;
	uint64_t k = (uint64_t)isec << 32 | ri;
	uint32_t j = (uint32_t)(k * 0x9E3779B97F4A7C15ULL >> 32) & tr_mask;
	while (tr_keys[j] != UINT64_MAX && tr_keys[j] != k)
		j = (j + 1) & tr_mask;
	if (tr_keys[j] == UINT64_MAX)
		tr_n++;
	tr_keys[j] = k;
	tr_vals[j] = th;
}
static uint32_t tr_get(uint32_t isec, uint32_t ri)
{
	if (!tr_keys)
		return NONE;
	uint64_t k = (uint64_t)isec << 32 | ri;
	uint32_t j = (uint32_t)(k * 0x9E3779B97F4A7C15ULL >> 32) & tr_mask;
	while (tr_keys[j] != UINT64_MAX) {
		if (tr_keys[j] == k)
			return tr_vals[j];
		j = (j + 1) & tr_mask;
	}
	return NONE;
}

static bool fits_int(int64_t v, int bits);
static uint64_t thunk_va(uint32_t t)
{
	Thunk *th = &thunks.v[t];
	return isec_va(th->tsec, th->off);
}
static uint64_t thunk_dest(uint32_t t)
{
	Thunk *th = &thunks.v[t];
	if (th->g != NONE)
		return gsym_va(th->g) + th->addend;
	return isec_va(th->sec, th->val) + th->addend;
}

static bool in_branch_range(uint64_t src, uint64_t dst)
{
	uint64_t range = 128 * 1024 * 1024;
	if (dst > src)
		return dst - src <= range - 4;
	return src - dst <= range;
}
