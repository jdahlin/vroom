/* SHF_MERGE sections: splitting into pieces and deduplicating them in 32 parallel shards. */

/* ---------------- merge pieces ---------------- */

static void ld_merge_pieces_split(void *arg, size_t i)
{
	struct ld_context *ctx = arg;
	ld_merge_input *m = &ctx->inputs.merge_inputs.v[i];
	ld_input_section *is = &ctx->inputs.sections.v[m->isec];
	const uint8_t *d = is->data;
	uint64_t n = is->size, es = is->entsize;
	uint32_t cnt = 0;
	if (is->flags & SHF_STRINGS) {
		/* count */
		for (uint64_t p = 0; p < n;) {
			uint64_t q = p;
			if (es == 1) {
				const uint8_t *z = memchr(d + p, 0, n - p);
				if (!z) { ld_error_report(ctx, "%s: string is not null terminated", is->name); return; }
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
		m->inoff = ld_region_alloc(cnt * 4);
		m->hash = ld_region_alloc(cnt * 4);
		m->outoff = ld_region_alloc(cnt * 4);
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
		m->inoff = ld_region_alloc(cnt * 4);
		m->hash = ld_region_alloc(cnt * 4);
		m->outoff = ld_region_alloc(cnt * 4);
		for (uint32_t k = 0; k < cnt; k++) {
			m->inoff[k] = k * es;
			m->hash[k] = (uint32_t)vdigest_xxh3(d + (uint64_t)k * es, es) >> 1;
		}
	}
	m->npieces = cnt;
}

static inline uint64_t ld_merge_piece_length(struct ld_context *ctx, const ld_merge_input *m, uint32_t p)
{
	uint64_t end = p + 1 < m->npieces ? m->inoff[p + 1] : ctx->inputs.sections.v[m->isec].size;
	return end - m->inoff[p];
}

/* one shard of one merge section: dedup in insertion order */
static void ld_merge_shard_deduplicate(void *arg, size_t job_i)
{
	struct ld_context *ctx = arg;
	uint32_t mi = job_i / 32, shard = job_i % 32;
	ld_merge_section *mg = &ctx->inputs.merge_sections.v[mi];
	uint32_t total = 0;
	for (uint32_t k = 0; k < mg->members.n; k++)
		total += ctx->inputs.merge_inputs.v[mg->members.v[k]].npieces;
	uint32_t cap = 16;
	while (cap < total / 16 + 16)
		cap *= 2;
	/* table of (mergein, piece) packed in u64; key compare by content */
	uint64_t *tab = ld_region_alloc((uint64_t)cap * 8);
	memset(tab, 0xff, (uint64_t)cap * 8);
	uint32_t *thash = ld_region_alloc((uint64_t)cap * 4);
	uint32_t used = 0;
	uint64_t size = 0;
	for (uint32_t k = 0; k < mg->members.n; k++) {
		uint32_t mid = mg->members.v[k];
		ld_merge_input *m = &ctx->inputs.merge_inputs.v[mid];
		const uint8_t *d = ctx->inputs.sections.v[m->isec].data;
		for (uint32_t p = 0; p < m->npieces; p++) {
			uint32_t h = m->hash[p];
			if ((h >> 26) != shard)
				continue;
			uint64_t len = ld_merge_piece_length(ctx, m, p);
			if ((used + 1) * 2 > cap) {
				/* grow */
				uint32_t nc = cap * 2;
				uint64_t *nt = ld_region_alloc((uint64_t)nc * 8);
				uint32_t *nh = ld_region_alloc((uint64_t)nc * 4);
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
					size = ld_value_align_up(size, mg->align);
					m->outoff[p] = (uint32_t)size;
					size += len;
					tab[x] = (uint64_t)mid << 32 | p;
					thash[x] = h;
					used++;
					VVEC_PUSH(mg->uniq[shard], tab[x], ld_region_alloc);
					break;
				}
				if (thash[x] == h) {
					ld_merge_input *m2 = &ctx->inputs.merge_inputs.v[tab[x] >> 32];
					uint32_t p2 = (uint32_t)tab[x];
					if (ld_merge_piece_length(ctx, m2, p2) == len &&
					    !memcmp(ctx->inputs.sections.v[m2->isec].data + m2->inoff[p2], d + m->inoff[p], len)) {
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

static void ld_merge_input_fix(void *arg, size_t i)
{
	struct ld_context *ctx = arg;
	ld_merge_input *m = &ctx->inputs.merge_inputs.v[i];
	ld_merge_section *mg = &ctx->inputs.merge_sections.v[ctx->inputs.sections.v[m->syn].aux];
	for (uint32_t p = 0; p < m->npieces; p++)
		m->outoff[p] += (uint32_t)mg->shardoff[m->hash[p] >> 26];
}

static void ld_merges_finalize(struct ld_context *ctx)
{
	ld_parallel_for(ctx->inputs.merge_inputs.n, ld_merge_pieces_split, ctx);
	ld_parallel_for((size_t)ctx->inputs.merge_sections.n * 32, ld_merge_shard_deduplicate, ctx);
	for (uint32_t i = 0; i < ctx->inputs.merge_sections.n; i++) {
		ld_merge_section *mg = &ctx->inputs.merge_sections.v[i];
		uint64_t off = 0;
		for (int s = 0; s < 32; s++) {
			if (mg->shardsize[s])
				off = ld_value_align_up(off, mg->align);
			mg->shardoff[s] = off;
			off += mg->shardsize[s];
		}
		ctx->inputs.sections.v[mg->isec].size = off;
	}
	ld_parallel_for(ctx->inputs.merge_inputs.n, ld_merge_input_fix, ctx);
}
