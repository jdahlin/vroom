/* SHF_MERGE sections: splitting into pieces and deduplicating them in 32 parallel shards. */

/* ---------------- merge pieces ---------------- */

static void ld_merge_pieces_split(void *arg, size_t i)
{
	struct ld_context *ctx = arg;
	ld_merge_input *m = &ctx->inputs.merge_inputs.v[i];
	ld_input_section *is = &ctx->inputs.sections.v[m->input_section];
	const uint8_t *d = is->data;
	uint64_t n = is->size, es = is->entry_size;
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
		m->input_offsets = ld_region_alloc(cnt * 4);
		m->hash = ld_region_alloc(cnt * 4);
		m->output_offsets = ld_region_alloc(cnt * 4);
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
			m->input_offsets[k] = (uint32_t)p;
			m->hash[k] = (uint32_t)vdigest_xxh3(d + p, len) >> 1;
			k++;
			p = q;
		}
	} else {
		cnt = n / es;
		m->input_offsets = ld_region_alloc(cnt * 4);
		m->hash = ld_region_alloc(cnt * 4);
		m->output_offsets = ld_region_alloc(cnt * 4);
		for (uint32_t k = 0; k < cnt; k++) {
			m->input_offsets[k] = k * es;
			m->hash[k] = (uint32_t)vdigest_xxh3(d + (uint64_t)k * es, es) >> 1;
		}
	}
	m->piece_count = cnt;
}

static inline uint64_t ld_merge_piece_length(struct ld_context *ctx, const ld_merge_input *m, uint32_t p)
{
	uint64_t end = p + 1 < m->piece_count ? m->input_offsets[p + 1] : ctx->inputs.sections.v[m->input_section].size;
	return end - m->input_offsets[p];
}

/* one shard of one merge section: dedup in insertion order */
static void ld_merge_shard_deduplicate(void *arg, size_t job_i)
{
	struct ld_context *ctx = arg;
	uint32_t mi = job_i / 32, shard = job_i % 32;
	ld_merge_section *mg = &ctx->inputs.merge_sections.v[mi];
	uint32_t total = 0;
	for (uint32_t k = 0; k < mg->members.n; k++)
		total += ctx->inputs.merge_inputs.v[mg->members.v[k]].piece_count;
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
		const uint8_t *d = ctx->inputs.sections.v[m->input_section].data;
		for (uint32_t p = 0; p < m->piece_count; p++) {
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
					size = ld_value_align_up(size, mg->alignment);
					m->output_offsets[p] = (uint32_t)size;
					size += len;
					tab[x] = (uint64_t)mid << 32 | p;
					thash[x] = h;
					used++;
					VVEC_PUSH(mg->unique_pieces[shard], tab[x], ld_region_alloc);
					break;
				}
				if (thash[x] == h) {
					ld_merge_input *m2 = &ctx->inputs.merge_inputs.v[tab[x] >> 32];
					uint32_t p2 = (uint32_t)tab[x];
					if (ld_merge_piece_length(ctx, m2, p2) == len &&
					    !memcmp(ctx->inputs.sections.v[m2->input_section].data + m2->input_offsets[p2], d + m->input_offsets[p], len)) {
						m->output_offsets[p] = m2->output_offsets[p2];
						break;
					}
				}
				x = (x + 1) & (cap - 1);
			}
		}
	}
	mg->shard_sizes[shard] = size;
}

static void ld_merge_input_fix(void *arg, size_t i)
{
	struct ld_context *ctx = arg;
	ld_merge_input *m = &ctx->inputs.merge_inputs.v[i];
	ld_merge_section *mg = &ctx->inputs.merge_sections.v[ctx->inputs.sections.v[m->synthetic_section].auxiliary_index];
	for (uint32_t p = 0; p < m->piece_count; p++)
		m->output_offsets[p] += (uint32_t)mg->shard_offsets[m->hash[p] >> 26];
}

static void ld_merges_finalize(struct ld_context *ctx)
{
	ld_parallel_for(ctx->inputs.merge_inputs.n, ld_merge_pieces_split, ctx);
	ld_parallel_for((size_t)ctx->inputs.merge_sections.n * 32, ld_merge_shard_deduplicate, ctx);
	for (uint32_t i = 0; i < ctx->inputs.merge_sections.n; i++) {
		ld_merge_section *mg = &ctx->inputs.merge_sections.v[i];
		uint64_t off = 0;
		for (int s = 0; s < 32; s++) {
			if (mg->shard_sizes[s])
				off = ld_value_align_up(off, mg->alignment);
			mg->shard_offsets[s] = off;
			off += mg->shard_sizes[s];
		}
		ctx->inputs.sections.v[mg->synthetic_section].size = off;
	}
	ld_parallel_for(ctx->inputs.merge_inputs.n, ld_merge_input_fix, ctx);
}
