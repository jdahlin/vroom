/* vcc code generator: emitting records, local labels and reachability */
#include "../../../emit/emitimpl.h"

/* ---------------- local labels and reachability ---------------- */

VPRIV uint32_t newlabel(ecx *e)
{
	if (e->nlabel == e->caplabel) {
		efatal(e, 0, "function too large (more than %u labels)", e->caplabel);
	}
	e->lrefc[e->nlabel] = 0;
	e->lkvi[e->nlabel] = 0;
	return e->nlabel++;
}

static void back_edge(ecx *e, uint32_t l)
{
	efatal(e, e->curloc, "internal: jump back to label %u, %s", l,
	       e->lkvi[l] == LKV_DROPPED ? "dropped as unreachable" : "placed with known values");
}

/* a reachable edge to l (call before counting it in lrefc) */
VPRIV void kv_edge(ecx *e, uint32_t l)
{
	kvst *st;

	if (e->lkvi[l] >= LKV_PLACED) {
		if (e->lkvi[l] != LKV_PLACED && !e->dry)
			back_edge(e, l);
		return;
	}
	if (!e->nkv || !e->reach)
		return;
	if (!e->lkvi[l]) {
		if (e->nkvs == CAPKVS) {
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

/* label l is placed: what is known is what every way in agrees on */
static void kv_join(ecx *e, uint32_t l)
{
	const kvst *st;

	if (!e->nkv || !e->lrefc[l])
		return;
	if (!e->lkvi[l] || e->lkvi[l] >= LKV_PLACED) {
		e->kvalid = 0;
		return;
	}
	st = &e->kvs[e->lkvi[l] - 1];
	if (!e->reach) {
		e->kvalid = st->valid;
		memcpy(e->kval, st->v, sizeof(e->kval));
		return;
	}
	for (uint16_t m = e->kvalid; m; m &= m - 1) {
		int k = __builtin_ctz(m);

		if (!(st->valid >> k & 1) || st->v[k] != e->kval[k])
			e->kvalid &= (uint16_t)~(1u << k);
	}
}

VPRIV void deflabel(ecx *e, uint32_t l)
{
	kv_join(e, l);
	if (e->lrefc[l])
		e->reach = true;
	if (!e->dry)
		e->lkvi[l] = !e->reach ? LKV_DROPPED : e->kvalid ? LKV_KNOWN : LKV_PLACED;
	if (!e->reach)
		return;
	put(e, I_LABEL)->label = l;
}

/* a label that is reachable from elsewhere whatever the counts say */
VPRIV void deflabel_forced(ecx *e, uint32_t l)
{
	e->kvalid = 0;
	e->reach = true;
	if (!e->dry)
		e->lkvi[l] = LKV_PLACED;
	put(e, I_LABEL)->label = l;
}

/* a loop head: the back edges come later */
VPRIV void deflabel_loop(ecx *e, uint32_t l, bool forced)
{
	e->kvalid = 0;
	if (forced)
		deflabel_forced(e, l);
	else
		deflabel(e, l);
}

VPRIV void jump(ecx *e, uint32_t l)
{
	if (!e->reach)
		return;
	kv_edge(e, l);
	put(e, I_B)->label = l;
	e->lrefc[l]++;
	e->reach = false;
}

VPRIV void bcond(ecx *e, int cond, uint32_t l)
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

VPRIV void cbz(ecx *e, bool nz, int x, int r, uint32_t l)
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
VPRIV void tbz(ecx *e, bool nz, int r, int bit, uint32_t l)
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
