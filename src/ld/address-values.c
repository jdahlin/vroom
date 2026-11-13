/* The address-assignment state and the functions that turn a section or symbol into an address (lld's ExprValue). */

/* ---------------- address-assignment state ---------------- */

static uint64_t osec_addr(uint32_t o) { return o == AETHER ? 0 : osecs.v[o].va; }
static uint32_t osec_shidx(uint32_t o) { return o == AETHER ? 1 : osecs.v[o].shidx; }

/* ---------------- merge pieces ---------------- */

static uint32_t piece_find(const MergeIn *m, uint64_t off)
{
	uint32_t lo = 0, hi = m->npieces;
	while (hi - lo > 1) {
		uint32_t mid = (lo + hi) / 2;
		if (m->inoff[mid] <= off)
			lo = mid;
		else
			hi = mid;
	}
	return lo;
}

/* offset of input offset `off` of section s within its output section */
static uint64_t isec_osoff(uint32_t s, uint64_t off)
{
	ISec *is = &isecs.v[s];
	if (is->kind == K_MERGE_IN) {
		MergeIn *m = &mergeins.v[is->aux];
		uint32_t p = piece_find(m, off);
		return isecs.v[m->syn].off + m->outoff[p] + (off - m->inoff[p]);
	}
	return is->off + off;
}
static uint32_t isec_osec(uint32_t s)
{
	ISec *is = &isecs.v[s];
	if (is->kind == K_MERGE_IN && is->osec != OS_DISCARD)	/* a discarded one has no MergeIn */
		return isecs.v[mergeins.v[is->aux].syn].osec;
	return is->osec;
}
static uint64_t isec_va(uint32_t s, uint64_t off)
{
	uint32_t o = isec_osec(s);
	if (o == NONE || o == OS_DISCARD)
		return 0;
	return osecs.v[o].va + isec_osoff(s, off);
}

static uint64_t ev_secaddr(EV v)
{
	switch (v.kind) {
	case EV_OSEC: return osec_addr(v.idx);
	case EV_ISEC: return isec_va(v.idx, 0);
	default: return 0;
	}
}
static uint64_t ev_value(EV v)
{
	uint64_t x;
	switch (v.kind) {
	case EV_OSEC: x = osec_addr(v.idx) + v.val; break;
	case EV_ISEC: x = isec_va(v.idx, v.val); break;
	default: x = v.val;
	}
	return alignup(x, v.align);
}
static uint64_t ev_secoff(EV v) { return ev_value(v) - ev_secaddr(v); }
static bool ev_isabs(EV v) { return v.fabs || v.kind == EV_ABS; }
static EV evabs(uint64_t x) { return (EV){.kind = EV_ABS, .val = x, .align = 1}; }
static EV evsec(EV a, bool fabs, uint64_t off)
{
	return (EV){.kind = a.kind, .idx = a.idx, .fabs = fabs, .val = off, .align = 1};
}

/* value of a global symbol as an expression value (getSymbolValue) */
static EV gsym_ev(uint32_t g)
{
	G *s = &gsyms[g];
	EV v = {.align = 1, .type = s->type};
	if (s->state == GS_SCRIPT) {
		v.kind = s->ev.kind;
		v.idx = s->ev.idx;
		v.val = s->ev.val;
		return v;
	}
	if (s->state == GS_OBJ && !s->demoted) {
		if (s->sec == NONE) {
			v.kind = EV_ABS;
			v.val = s->val;
		} else {
			v.kind = EV_ISEC;
			v.idx = s->sec;
			v.val = s->val;
		}
		return v;
	}
	if (ctx->layout.in_layout)
		error("symbol not found: %s", gname(g));
	return evabs(0);
}

static uint64_t gsym_va(uint32_t g)
{
	G *s = &gsyms[g];
	if (s->thunk != NONE && 0)
		return 0;
	if (s->state == GS_SCRIPT)
		return s->ev.kind == EV_ABS ? s->ev.val :
		       s->ev.kind == EV_OSEC ? osec_addr(s->ev.idx) + s->ev.val :
		       isec_va(s->ev.idx, s->ev.val);
	if (s->state == GS_OBJ && !s->demoted)
		return s->sec == NONE ? s->val : isec_va(s->sec, s->val);
	return 0;
}
static bool gsym_isabs(uint32_t g)
{
	G *s = &gsyms[g];
	if (s->state == GS_SCRIPT)
		return s->ev.kind == EV_ABS;
	if (s->state == GS_OBJ && !s->demoted)
		return s->sec == NONE;
	return true;
}
