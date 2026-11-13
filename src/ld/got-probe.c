/* Finding out whether any relocation needs a GOT slot. */

/* ---------------- the final link ---------------- */

static void ld_state_reset(void);

static _Atomic bool got_seen;
static void ld_got_find(void *arg, size_t i)
{
	(void)arg;
	const ld_input_section *is = &isecs.v[i];
	if (is->kind == LD_SECTION_KIND_NONE || atomic_load_explicit(&got_seen, memory_order_relaxed))
		return;
	for (uint32_t r = 0; r < is->nrela; r++) {
		uint32_t t = (uint32_t)is->rela[r].r_info;
		if (t == R_AARCH64_ADR_GOT_PAGE || t == R_AARCH64_LD64_GOT_LO12_NC) {
			atomic_store(&got_seen, true);
			return;
		}
	}
}
static bool ld_got_is_needed(void)
{
	atomic_store(&got_seen, false);
	if (!opt.relocatable)
		ld_parallel_for(isecs.n, x86_ld ? x64_ld_got_find : ld_got_find, NULL);
	return atomic_load(&got_seen);
}
