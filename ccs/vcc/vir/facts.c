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

/* Consecutive byte assembly. Keep this proof in VIR: targets choose
 * their unaligned load/reversal and separately check allocation edits. */
static uint32_t be_address(const vir_fn *f,uint32_t v,uint64_t *off)
{
	for(unsigned depth=0;depth<8 && f->ins[v].op==VO_PTRADD;depth++) {
		const vir_ins *i=&f->ins[v],*k=&f->ins[i->b];
		if(k->op!=VO_ICONST)break;
		*off+=(uint64_t)k->c<<32|k->b;v=i->a;
	}
	return v;
}
static bool be_word(const vir_fn *f,uint32_t v,uint32_t limit,unsigned width,uint32_t *last)
{
	uint32_t at=v,acc=v;
	if(limit-v<4*width-3)return false;
	const vir_ins *first=&f->ins[v];
	if(first->op!=VO_LOAD || first->ty!=VT_I32 || (first->fl&(VIF_VOL|3)) ||
	   ((first->fl&VIF_MS) && width!=4))return false;
	/* A signed first byte is equivalent only when shifted to bit 24: its
	 * sign extension is discarded by the i32 shift. A 16-bit assembly would
	 * retain those sign bits and needs a different result extension. */
	uint64_t off=0;uint32_t base=be_address(f,first->a,&off);
	for(unsigned byte=0;byte<width;byte++) {
		uint32_t part=v;
		if(byte) {
			const vir_ins *addr=&f->ins[++at];uint64_t delta=0;
			if(addr->op!=VO_PTRADD || be_address(f,at,&delta)!=base || delta-off!=byte)return false;
			const vir_ins *load=&f->ins[++at];
			if(load->op!=VO_LOAD || load->ty!=VT_I32 || load->a!=at-1 || (load->fl&(VIF_VOL|VIF_MS|3)))return false;
			part=at;
		}
		if(byte+1<width) {
			const vir_ins *shift=&f->ins[++at];
			if(shift->op!=VO_SHL)return false;
			const vir_ins *k=&f->ins[shift->b];
			if(shift->op!=VO_SHL || shift->ty!=VT_I32 || shift->a!=part || k->op!=VO_ICONST || k->b!=8*(width-byte-1) || k->c)return false;
			part=at;
		}
		if(byte) {
			const vir_ins *join=&f->ins[++at];
			if(join->op!=VO_OR || join->ty!=VT_I32 || !((join->a==acc && join->b==part)||(join->b==acc && join->a==part)))return false;
		}
		acc=at;
	}
	*last=at;return true;
}
unsigned vir_read_be(const vir_fn *f,const uint8_t *uses,uint32_t first,uint32_t limit,uint32_t *last)
{
	uint32_t end;
	if(first>=limit || limit>f->nins)return 0;
	unsigned width=be_word(f,first,limit,4,&end)?4:be_word(f,first,limit,2,&end)?2:0;
	if(!width)return 0;
	if(width==4 && limit-end>18) {
		const vir_ins *ext=&f->ins[end+1],*shift=&f->ins[end+2],*addr=&f->ins[end+3];
		const vir_ins *k=shift->op==VO_SHL?&f->ins[shift->b]:&f->ins[0];uint32_t low;
		uint64_t a=0,b=0;
		if(ext->op==VO_EXT && ext->ty==VT_I64 && ext->fl==2 && ext->a==end &&
		   shift->op==VO_SHL && shift->ty==VT_I64 && shift->a==end+1 && k->op==VO_ICONST && k->b==32 && !k->c &&
		   addr->op==VO_PTRADD && be_address(f,end+3,&b)==be_address(f,f->ins[first].a,&a) && b-a==4 &&
		   be_word(f,end+4,limit,4,&low) && f->ins[end+4].a==end+3 && low+2<limit) {
			const vir_ins *le=&f->ins[low+1],*join=&f->ins[low+2];
			if(le->op==VO_EXT && le->ty==VT_I64 && le->fl==2 && le->a==low &&
			   join->op==VO_OR && join->ty==VT_I64 &&
			   ((join->a==end+2 && join->b==low+1)||(join->b==end+2 && join->a==low+1)))width=8,end=low+2;
		}
	}
	/* Every skipped definition is private to this expression. This also
	 * handles the second half's shared pointer without allowing outside uses. */
	uint8_t internal[40]={0};
	if(end-first>=sizeof internal)return 0;
	for(uint32_t v=first;v<=end;v++) {
		vir_ins i=f->ins[v];vir_oit it;uint32_t *p;
		vir_oit_init(&it,(vir_fn *)f,&i,false);
		while((p=vir_oit_next(&it)))if(*p>=first && *p<=end)internal[*p-first]++;
	}
	for(uint32_t v=first;v<end;v++)if(uses[v]!=internal[v-first])return 0;
	*last=end;return width;
}

/* sext(shl.nsw.i32 x,k) == shl.i64(sext(x),k) on every defined input.
 * Allocation, use counts, block boundaries and legal hardware scales belong
 * to the consumer; this rule adds no lifetime or new IR definition. */
bool vir_sext_shift(const vir_fn *f,uint32_t v,unsigned *count)
{
 if(v>=f->nins || f->nins-v<2)return false;
 const vir_ins *shift=&f->ins[v],*ext=&f->ins[v+1];
 if(shift->op!=VO_SHL || shift->ty!=VT_I32 || !(shift->fl&VIF_NSW) ||
    ext->op!=VO_EXT || ext->ty!=VT_I64 || ext->fl!=(2|VIF_MS) || ext->a!=v)return false;
 const vir_ins *k=&f->ins[shift->b];
 if(k->op!=VO_ICONST || k->b>=32 || k->c)return false;
 *count=k->b;return true;
}
