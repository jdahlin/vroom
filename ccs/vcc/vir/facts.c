/* Backward value demand, independent of the target's register encoding.
 * Facts only fall. Reverse sweeps settle straight-line uses immediately;
 * repeated sweeps carry full-width demand through loop block parameters.
 * The target supplies whether its return ABI leaves integer high bits unused.
 */
#include "virimpl.h"

void vir_low32(vir_fn *f, bool ret32, uint8_t *low32)
{
	memset(low32, 1, f->nins);
	for (bool ch = true; ch;) {
		ch = false;
		for (uint32_t b = f->nblk; b-- > 0;)
			for (uint32_t i = f->blk[b].first + f->blk[b].n; i-- > f->blk[b].first;) {
				vir_ins x = f->ins[i];
				vir_oit it;
				uint32_t *p;

				if (x.op == VO_TRUNC || (x.op == VO_RET && ret32))	/* (a 32-bit return's upper half is unspecified) */
					continue;
				if (x.op == VO_BR) {
					const vir_blk *t = &f->blk[x.a];

					for (uint32_t j = 0; j < t->nparam; j++) {
						uint32_t a = f->extra[x.c + j];

						if (!low32[t->first + j] && low32[a])
							low32[a] = 0, ch = true;
					}
					continue;
				}
				if (x.op == VO_SELECT && low32[i]) {	/* its values are read as it is */
					if (low32[x.a])
						low32[x.a] = 0, ch = true;
					continue;
				}
				if (x.op == VO_CALL) {	/* an integer argument of 4 bytes or less: its upper half is unspecified */
					uint32_t n = f->extra[x.c], hdr = f->extra[x.b];

					for (uint32_t k = 0; k < n; k++) {
						uint32_t a = f->extra[x.c + 1 + k], w0 = f->extra[x.b + 3 + 2 * k], w1 = f->extra[x.b + 4 + 2 * k];

						if (VSIG_NGROUPS(hdr) == n && VG_KIND(w0) == VG_REG && VG_NPIECE(w0) == 1 &&
						    VG_CLASS(w0, 0) == VC_INT && VG_SIZE(w1) <= 4)
							continue;
						if (low32[a])
							low32[a] = 0, ch = true;
					}
					if (low32[x.a])
						low32[x.a] = 0, ch = true;
					continue;
				}
				vir_oit_init(&it, f, &x, false);
				while ((p = vir_oit_next(&it)))
					if (low32[*p])
						low32[*p] = 0, ch = true;
			}
	}

}

/* Possible one bits, bounded to a small local expression tree. A clear sign
 * bit proves that a signed i32 extension has the same value as zero extension. */
uint32_t vir_mask32(const vir_fn *f,uint32_t v,unsigned depth)
{
	const vir_ins *i=&f->ins[v];
	if(i->ty!=VT_I32 || !depth)return UINT32_MAX;
	switch(i->op) {
	case VO_ICONST:return i->b;
	case VO_LOAD: case VO_EXT:
		if(!(i->fl&VIF_MS) && VIF_W(i->fl)<2)return VIF_W(i->fl)?0xffff:0xff;
		return UINT32_MAX;
	case VO_ICMP: case VO_FCMP:return 1;
	case VO_AND:return vir_mask32(f,i->a,depth-1)&vir_mask32(f,i->b,depth-1);
	case VO_OR: case VO_XOR:return vir_mask32(f,i->a,depth-1)|vir_mask32(f,i->b,depth-1);
	case VO_SHL: case VO_LSHR: {
		const vir_ins *shift=&f->ins[i->b];
		if(shift->op==VO_ICONST && shift->b<32 && (shift->ty==VT_I32 || !shift->c)) {
			uint32_t k=shift->b;
			uint32_t mask=vir_mask32(f,i->a,depth-1);
			return i->op==VO_SHL?mask<<k:mask>>k;
		}
		return UINT32_MAX;
	}
	default:return UINT32_MAX;
	}
}
