/* Finding out whether any relocation needs a GOT slot. */

/* ---------------- the final link ---------------- */

static void reset_state(void);

static _Atomic bool got_seen;
static void find_got(void *arg, size_t i)
{
	(void)arg;
	const ISec *is = &isecs.v[i];
	if (is->kind == K_NONE || atomic_load_explicit(&got_seen, memory_order_relaxed))
		return;
	for (uint32_t r = 0; r < is->nrela; r++) {
		uint32_t t = (uint32_t)is->rela[r].r_info;
		if (t == R_AARCH64_ADR_GOT_PAGE || t == R_AARCH64_LD64_GOT_LO12_NC) {
			atomic_store(&got_seen, true);
			return;
		}
	}
}
static bool any_got(void)
{
	atomic_store(&got_seen, false);
	if (!opt.relocatable)
		pfor(isecs.n, x86_ld ? x64_find_got : find_got, NULL);
	return atomic_load(&got_seen);
}
