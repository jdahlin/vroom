/* vcc arm64 code generator: labels and branches, with the known-value edges they carry */
#include "../../../codegen/codegen-internal.h"

VPRIV uint32_t newlabel(codegen_context *e)
{
	if (e->nlabel == e->caplabel) {
		codegen_fatal_report(e, 0, "function too large (more than %u labels)", e->caplabel);
	}
	e->lrefc[e->nlabel] = 0;
	e->lkvi[e->nlabel] = 0;
	return e->nlabel++;
}

static void back_edge(codegen_context *e, uint32_t l)
{
	codegen_fatal_report(e, e->curloc, "internal: jump back to label %u, %s", l,
	       e->lkvi[l] == CODEGEN_KNOWN_LABEL_DROPPED ? "dropped as unreachable" : "placed with known values");
}

/* a reachable edge to l (call before counting it in lrefc) */
VPRIV void kv_edge(codegen_context *e, uint32_t l)
{
	codegen_known_values *st;

	if (e->lkvi[l] >= CODEGEN_KNOWN_LABEL_PLACED) {
		if (e->lkvi[l] != CODEGEN_KNOWN_LABEL_PLACED && !e->dry)
			back_edge(e, l);
		return;
	}
	if (!e->nkv || !e->reach)
		return;
	if (!e->lkvi[l]) {
		if (e->nkvs == CODEGEN_KNOWN_VALUES_CAPACITY) {
			e->nkv = 0, e->kvalid = 0, e->nael = 0;	/* out of room: stop tracking */
			return;
		}
		st = &e->kvs[e->nkvs++];
		e->lkvi[l] = e->nkvs;
		st->valid = e->lrefc[l] ? 0 : e->kvalid;	/* earlier edges were untracked */
		memcpy(st->v, e->kval, sizeof(st->v));
		return;
	}
	st = &e->kvs[e->lkvi[l] - 1];
	for (uint16_t m = st->valid; m; m &= m - 1) {
		int k = __builtin_ctz(m);

		if (!(e->kvalid >> k & 1) || st->v[k] != e->kval[k])
			st->valid &= (uint16_t)~(1u << k);
	}
}

/* a label that is reachable from elsewhere whatever the counts say */
VPRIV void deflabel_forced(codegen_context *e, uint32_t l)
{
	e->kvalid = 0;
	e->reach = true;
	if (!e->dry)
		e->lkvi[l] = CODEGEN_KNOWN_LABEL_PLACED;
	put(e, I_LABEL)->label = l;
}

VPRIV void jump(codegen_context *e, uint32_t l)
{
	if (!e->reach)
		return;
	kv_edge(e, l);
	put(e, I_B)->label = l;
	e->lrefc[l]++;
	e->reach = false;
}

VPRIV void bcond(codegen_context *e, int cond, uint32_t l)
{
	ins *i;

	if (!e->reach)
		return;
	kv_edge(e, l);
	i = put(e, I_BC);
	i->cond = (uint8_t)cond;
	i->label = l;
	e->lrefc[l]++;
}

VPRIV void cbz(codegen_context *e, bool nz, int x, int r, uint32_t l)
{
	ins *i;

	if (!e->reach)
		return;
	kv_edge(e, l);
	i = put(e, nz ? I_CBNZ : I_CBZ);
	i->sz = (uint8_t)x, i->rd = (uint8_t)r;
	i->label = l;
	e->lrefc[l]++;
}

/* tbz, tbnz: bit of register r (finish_function relaxes one whose label may be beyond 32 KB) */
VPRIV void tbz(codegen_context *e, bool nz, int r, int bit, uint32_t l)
{
	ins *i;

	if (!e->reach)
		return;
	kv_edge(e, l);
	i = put(e, nz ? I_TBNZ : I_TBZ);
	i->sz = bit >= 32, i->rd = (uint8_t)r, i->sh = (uint8_t)bit;
	i->label = l;
	e->lrefc[l]++;
}
