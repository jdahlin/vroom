/* vcc code generator: emitting records, local labels and reachability */
#include "../../../codegen/emitimpl.h"

/* ---------------- local labels and reachability ---------------- */

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

/* a loop head: the back edges come later */
VPRIV void deflabel_loop(ecx *e, uint32_t l, bool forced)
{
	e->kvalid = 0;
	if (forced)
		deflabel_forced(e, l);
	else
		deflabel(e, l);
}
