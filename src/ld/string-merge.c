/* SHF_MERGE sections: splitting into pieces and deduplicating them in 32 parallel shards. */

/* ---------------- merge pieces ---------------- */

static void split_pieces(void *arg, size_t i)
{
	(void)arg;
	MergeIn *m = &mergeins.v[i];
	ISec *is = &isecs.v[m->isec];
	const uint8_t *d = is->data;
	uint64_t n = is->size, es = is->entsize;
	uint32_t cnt = 0;
	if (is->flags & SHF_STRINGS) {
		/* count */
		for (uint64_t p = 0; p < n;) {
			uint64_t q = p;
			if (es == 1) {
				const uint8_t *z = memchr(d + p, 0, n - p);
				if (!z) { error("%s: string is not null terminated", is->name); return; }
				q = z - d + 1;
			} else {
				while (q + es <= n) {
					bool zero = true;
					for (uint64_t k = 0; k < es; k++)
						zero &= d[q + k] == 0;
					q += es;
					if (zero)
						break;
				}
			}
			cnt++;
			p = q;
		}
		m->inoff = alloc(cnt * 4);
		m->hash = alloc(cnt * 4);
		m->outoff = alloc(cnt * 4);
		uint32_t k = 0;
		for (uint64_t p = 0; p < n;) {
			uint64_t q, len;
			if (es == 1) {
				len = strlen((const char *)d + p);
				q = p + len + 1;
			} else {
				q = p;
				for (;;) {
					bool zero = true;
					for (uint64_t j = 0; j < es; j++)
						zero &= d[q + j] == 0;
					if (zero)
						break;
					q += es;
				}
				len = q - p;
				q += es;
			}
			m->inoff[k] = (uint32_t)p;
			m->hash[k] = (uint32_t)vdigest_xxh3(d + p, len) >> 1;
			k++;
			p = q;
		}
	} else {
		cnt = n / es;
		m->inoff = alloc(cnt * 4);
		m->hash = alloc(cnt * 4);
		m->outoff = alloc(cnt * 4);
		for (uint32_t k = 0; k < cnt; k++) {
			m->inoff[k] = k * es;
			m->hash[k] = (uint32_t)vdigest_xxh3(d + (uint64_t)k * es, es) >> 1;
		}
	}
	m->npieces = cnt;
}

static inline uint64_t piece_len(const MergeIn *m, uint32_t p)
{
	uint64_t end = p + 1 < m->npieces ? m->inoff[p + 1] : isecs.v[m->isec].size;
	return end - m->inoff[p];
}

/* one shard of one merge section: dedup in insertion order */
static void merge_shard(void *arg, size_t job_i)
{
	(void)arg;
	uint32_t mi = job_i / 32, shard = job_i % 32;
	Merge *mg = &merges.v[mi];
	uint32_t total = 0;
	for (uint32_t k = 0; k < mg->members.n; k++)
		total += mergeins.v[mg->members.v[k]].npieces;
	uint32_t cap = 16;
	while (cap < total / 16 + 16)
		cap *= 2;
	/* table of (mergein, piece) packed in u64; key compare by content */
	uint64_t *tab = alloc((uint64_t)cap * 8);
	memset(tab, 0xff, (uint64_t)cap * 8);
	uint32_t *thash = alloc((uint64_t)cap * 4);
	uint32_t used = 0;
	uint64_t size = 0;
	for (uint32_t k = 0; k < mg->members.n; k++) {
		uint32_t mid = mg->members.v[k];
		MergeIn *m = &mergeins.v[mid];
		const uint8_t *d = isecs.v[m->isec].data;
		for (uint32_t p = 0; p < m->npieces; p++) {
			uint32_t h = m->hash[p];
			if ((h >> 26) != shard)
				continue;
			uint64_t len = piece_len(m, p);
			if ((used + 1) * 2 > cap) {
				/* grow */
				uint32_t nc = cap * 2;
				uint64_t *nt = alloc((uint64_t)nc * 8);
				uint32_t *nh = alloc((uint64_t)nc * 4);
				memset(nt, 0xff, (uint64_t)nc * 8);
				for (uint32_t j = 0; j < cap; j++)
					if (tab[j] != UINT64_MAX) {
						uint32_t x = (thash[j] * 0x9E3779B1u) & (nc - 1);
						while (nt[x] != UINT64_MAX)
							x = (x + 1) & (nc - 1);
						nt[x] = tab[j];
						nh[x] = thash[j];
					}
				tab = nt;
				thash = nh;
				cap = nc;
			}
			uint32_t x = (h * 0x9E3779B1u) & (cap - 1);
			for (;;) {
				if (tab[x] == UINT64_MAX) {
					size = alignup(size, mg->align);
					m->outoff[p] = (uint32_t)size;
					size += len;
					tab[x] = (uint64_t)mid << 32 | p;
					thash[x] = h;
					used++;
					VVEC_PUSH(mg->uniq[shard], tab[x], alloc);
					break;
				}
				if (thash[x] == h) {
					MergeIn *m2 = &mergeins.v[tab[x] >> 32];
					uint32_t p2 = (uint32_t)tab[x];
					if (piece_len(m2, p2) == len &&
					    !memcmp(isecs.v[m2->isec].data + m2->inoff[p2], d + m->inoff[p], len)) {
						m->outoff[p] = m2->outoff[p2];
						break;
					}
				}
				x = (x + 1) & (cap - 1);
			}
		}
	}
	mg->shardsize[shard] = size;
}

static void merge_fix(void *arg, size_t i)
{
	(void)arg;
	MergeIn *m = &mergeins.v[i];
	Merge *mg = &merges.v[isecs.v[m->syn].aux];
	for (uint32_t p = 0; p < m->npieces; p++)
		m->outoff[p] += (uint32_t)mg->shardoff[m->hash[p] >> 26];
}

static void finalize_merges(void)
{
	pfor(mergeins.n, split_pieces, NULL);
	pfor((size_t)merges.n * 32, merge_shard, NULL);
	for (uint32_t i = 0; i < merges.n; i++) {
		Merge *mg = &merges.v[i];
		uint64_t off = 0;
		for (int s = 0; s < 32; s++) {
			if (mg->shardsize[s])
				off = alignup(off, mg->align);
			mg->shardoff[s] = off;
			off += mg->shardsize[s];
		}
		isecs.v[mg->isec].size = off;
	}
	pfor(mergeins.n, merge_fix, NULL);
}
