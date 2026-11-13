/* Writing .eh_frame_hdr (lld's EhFrameHeader::write). */

/* .eh_frame_hdr (lld EhFrameHeader::write): version 1, eh_frame_ptr pcrel
 * sdata4, fde_count udata4, a table datarel sdata4 of (initial location,
 * FDE) sorted by location, duplicate locations dropped; read from the
 * relocated FDEs */
static int cmp_u64pair(const void *a, const void *b)
{
	const uint64_t *x = a, *y = b;
	return x[0] != y[0] ? (x[0] < y[0] ? -1 : 1) : x[1] < y[1] ? -1 : x[1] > y[1];	/* lld stable_sort: output order */
}
static void write_ehhdr(void)
{
	if (ehhdr_isec == NONE)
		return;
	uint32_t ho = isec_osec(ehhdr_isec);

	if (ho == NONE || ho == OS_DISCARD)
		return;
	uint64_t hva = isec_va(ehhdr_isec, 0);
	uint8_t *h = out + osecs.v[ho].offset + isecs.v[ehhdr_isec].off;
	uint64_t (*t)[2] = alloc((ehfdes.n + 1) * sizeof *t);
	uint32_t n = 0;
	uint64_t ehva = 0;

	for (uint32_t k = 0; k < ehfdes.n; k++) {
		uint32_t f = ehfdes.v[k], fo = isec_osec(f);
		const uint8_t *b = out + osecs.v[fo].offset + isecs.v[f].off;
		uint64_t fva = isec_va(f, 0);

		if (!k)
			ehva = osecs.v[fo].va;
		t[n][0] = fva + 8 + (uint64_t)(int64_t)(int32_t)rd32(b + 8);	/* pc_begin: pcrel sdata4 (vcc, clang) */
		t[n][1] = fva;
		n++;
	}
	if (!ehfdes.n && isecs.n)
		for (uint32_t i = 0; i < isecs.n; i++)
			if (!strcmp(isecs.v[i].name, ".eh_frame") && isecs.v[i].kind == K_NORMAL && isec_osec(i) < osecs.n) {
				ehva = osecs.v[isec_osec(i)].va;
				break;
			}
	qsort(t, n, sizeof *t, cmp_u64pair);
	uint32_t m = 0;
	for (uint32_t k = 0; k < n; k++)
		if (!m || t[k][0] != t[m - 1][0]) {
			t[m][0] = t[k][0];
			t[m][1] = t[k][1];
			m++;
		}
	h[0] = 1;
	h[1] = 0x1b;	/* DW_EH_PE_pcrel | sdata4 */
	h[2] = 0x03;	/* udata4 */
	h[3] = 0x3b;	/* DW_EH_PE_datarel | sdata4 */
	wr32(h + 4, (uint32_t)(ehva - (hva + 4)));
	wr32(h + 8, m);
	for (uint32_t k = 0; k < m; k++) {
		wr32(h + 12 + 8 * k, (uint32_t)(t[k][0] - hva));
		wr32(h + 16 + 8 * k, (uint32_t)(t[k][1] - hva));
	}
}
