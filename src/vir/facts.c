/*
 * facts.c: backward value demand, independent of the target's register encoding: which results only
 * have their low 32 bits observed (vir_demand_low32_compute).
 *
 * low32[i] is 1 when no user reads more than the low half of instruction i's result, so a lowering
 * may use a 32-bit operation or skip a zero extension.  The facts only fall (1 to 0): reverse sweeps
 * settle straight-line uses at once and repeated sweeps carry full-width demand through block
 * parameters of loops, to a fixed point.  The target supplies whether its return ABI leaves the high
 * bits of a 32-bit result unused (ret32), because that is an ABI fact, not an IR one.  The array
 * is the caller's, with f->insn_count entries.  Only virtest calls it today; the lowerings do not use
 * it yet.
 */
#include "vir-internal.h"

void vir_demand_low32_compute(vir_function *f, bool ret32, uint8_t *low32)
{
	memset(low32, 1, f->insn_count);
	for (bool ch = true; ch;) {
		ch = false;
		for (uint32_t b = f->block_count; b-- > 0;)
			for (uint32_t i = f->blocks[b].first + f->blocks[b].insn_count; i-- > f->blocks[b].first;) {
				vir_insn x = f->insns[i];
				vir_operand_iterator it;
				uint32_t *p;

				if (x.op == VIR_OP_TRUNC || (x.op == VIR_OP_RET && ret32))	/* (a 32-bit return's upper half is unspecified) */
					continue;
				if (x.op == VIR_OP_BR) {
					const vir_block *t = &f->blocks[x.a];

					for (uint32_t j = 0; j < t->parameter_count; j++) {
						uint32_t a = f->extra[x.c + j];

						if (!low32[t->first + j] && low32[a])
							low32[a] = 0, ch = true;
					}
					continue;
				}
				if (x.op == VIR_OP_SELECT && low32[i]) {	/* its values are read as it is */
					if (low32[x.a])
						low32[x.a] = 0, ch = true;
					continue;
				}
				if (x.op == VIR_OP_CALL) {	/* an integer argument of 4 bytes or less: its upper half is unspecified */
					uint32_t n = f->extra[x.c], hdr = f->extra[x.b];

					for (uint32_t k = 0; k < n; k++) {
						uint32_t a = f->extra[x.c + 1 + k], w0 = f->extra[x.b + 3 + 2 * k], w1 = f->extra[x.b + 4 + 2 * k];

						if (VIR_SIGNATURE_GROUP_COUNT(hdr) == n && VIR_GROUP_KIND(w0) == VIR_GROUP_REG && VIR_GROUP_PIECE_COUNT(w0) == 1 &&
						    VIR_GROUP_CLASS(w0, 0) == VIR_CLASS_INT && VIR_GROUP_SIZE(w1) <= 4)
							continue;
						if (low32[a])
							low32[a] = 0, ch = true;
					}
					if (low32[x.a])
						low32[x.a] = 0, ch = true;
					continue;
				}
				vir_operand_iterator_init(&it, f, &x, false);
				while ((p = vir_operand_iterator_next(&it)))
					if (low32[*p])
						low32[*p] = 0, ch = true;
			}
	}

}

/* Possible one bits, bounded to a small local expression tree. A clear sign
 * bit proves that a signed i32 extension has the same value as zero extension. */
uint32_t vir_value_possible_bits(const vir_function *f,uint32_t v,unsigned depth)
{
	const vir_insn *i=&f->insns[v];
	if(i->type!=VIR_TYPE_I32 || !depth)return UINT32_MAX;
	switch(i->op) {
	case VIR_OP_ICONST:return i->b;
	case VIR_OP_LOAD: case VIR_OP_EXT:
		if(!(i->flags&VIR_INSN_FLAG_MS) && VIR_INSN_FLAG_WIDTH_LOG2(i->flags)<2)return VIR_INSN_FLAG_WIDTH_LOG2(i->flags)?0xffff:0xff;
		return UINT32_MAX;
	case VIR_OP_ICMP: case VIR_OP_FCMP:return 1;
	case VIR_OP_AND:return vir_value_possible_bits(f,i->a,depth-1)&vir_value_possible_bits(f,i->b,depth-1);
	case VIR_OP_OR: case VIR_OP_XOR:return vir_value_possible_bits(f,i->a,depth-1)|vir_value_possible_bits(f,i->b,depth-1);
	case VIR_OP_SHL: case VIR_OP_LSHR: {
		const vir_insn *shift=&f->insns[i->b];
		if(shift->op==VIR_OP_ICONST && shift->b<32 && (shift->type==VIR_TYPE_I32 || !shift->c)) {
			uint32_t k=shift->b;
			uint32_t mask=vir_value_possible_bits(f,i->a,depth-1);
			return i->op==VIR_OP_SHL?mask<<k:mask>>k;
		}
		return UINT32_MAX;
	}
	default:return UINT32_MAX;
	}
}

/* Consecutive byte assembly. Keep this proof in VIR: targets choose
 * their unaligned load/reversal and separately check allocation edits. */
static uint32_t vir_big_endian_address_match(const vir_function *f,uint32_t v,uint64_t *off)
{
	for(unsigned depth=0;depth<8 && f->insns[v].op==VIR_OP_PTRADD;depth++) {
		const vir_insn *i=&f->insns[v],*k=&f->insns[i->b];
		if(k->op!=VIR_OP_ICONST)break;
		*off+=(uint64_t)k->c<<32|k->b;v=i->a;
	}
	return v;
}
static bool vir_big_endian_word_match(const vir_function *f,uint32_t v,uint32_t limit,unsigned width,uint32_t *last)
{
	uint32_t at=v,acc=v;
	if(limit-v<4*width-3)return false;
	const vir_insn *first=&f->insns[v];
	if(first->op!=VIR_OP_LOAD || first->type!=VIR_TYPE_I32 || (first->flags&(VIR_INSN_FLAG_VOL|3)) ||
	   ((first->flags&VIR_INSN_FLAG_MS) && width!=4))return false;
	/* A signed first byte is equivalent only when shifted to bit 24: its
	 * sign extension is discarded by the i32 shift. A 16-bit assembly would
	 * retain those sign bits and needs a different result extension. */
	uint64_t off=0;uint32_t base=vir_big_endian_address_match(f,first->a,&off);
	for(unsigned byte=0;byte<width;byte++) {
		uint32_t part=v;
		if(byte) {
			const vir_insn *addr=&f->insns[++at];uint64_t delta=0;
			if(addr->op!=VIR_OP_PTRADD || vir_big_endian_address_match(f,at,&delta)!=base || delta-off!=byte)return false;
			const vir_insn *load=&f->insns[++at];
			if(load->op!=VIR_OP_LOAD || load->type!=VIR_TYPE_I32 || load->a!=at-1 || (load->flags&(VIR_INSN_FLAG_VOL|VIR_INSN_FLAG_MS|3)))return false;
			part=at;
		}
		if(byte+1<width) {
			const vir_insn *shift=&f->insns[++at];
			if(shift->op!=VIR_OP_SHL)return false;
			const vir_insn *k=&f->insns[shift->b];
			if(shift->op!=VIR_OP_SHL || shift->type!=VIR_TYPE_I32 || shift->a!=part || k->op!=VIR_OP_ICONST || k->b!=8*(width-byte-1) || k->c)return false;
			part=at;
		}
		if(byte) {
			const vir_insn *join=&f->insns[++at];
			if(join->op!=VIR_OP_OR || join->type!=VIR_TYPE_I32 || !((join->a==acc && join->b==part)||(join->b==acc && join->a==part)))return false;
		}
		acc=at;
	}
	*last=at;return true;
}
unsigned vir_big_endian_read_match(const vir_function *f,const uint8_t *uses,uint32_t first,uint32_t limit,uint32_t *last)
{
	uint32_t end;
	if(first>=limit || limit>f->insn_count)return 0;
	unsigned width=vir_big_endian_word_match(f,first,limit,4,&end)?4:vir_big_endian_word_match(f,first,limit,2,&end)?2:0;
	if(!width)return 0;
	if(width==4 && limit-end>18) {
		const vir_insn *ext=&f->insns[end+1],*shift=&f->insns[end+2],*addr=&f->insns[end+3];
		const vir_insn *k=shift->op==VIR_OP_SHL?&f->insns[shift->b]:&f->insns[0];uint32_t low;
		uint64_t a=0,b=0;
		if(ext->op==VIR_OP_EXT && ext->type==VIR_TYPE_I64 && ext->flags==2 && ext->a==end &&
		   shift->op==VIR_OP_SHL && shift->type==VIR_TYPE_I64 && shift->a==end+1 && k->op==VIR_OP_ICONST && k->b==32 && !k->c &&
		   addr->op==VIR_OP_PTRADD && vir_big_endian_address_match(f,end+3,&b)==vir_big_endian_address_match(f,f->insns[first].a,&a) && b-a==4 &&
		   vir_big_endian_word_match(f,end+4,limit,4,&low) && f->insns[end+4].a==end+3 && low+2<limit) {
			const vir_insn *le=&f->insns[low+1],*join=&f->insns[low+2];
			if(le->op==VIR_OP_EXT && le->type==VIR_TYPE_I64 && le->flags==2 && le->a==low &&
			   join->op==VIR_OP_OR && join->type==VIR_TYPE_I64 &&
			   ((join->a==end+2 && join->b==low+1)||(join->b==end+2 && join->a==low+1)))width=8,end=low+2;
		}
	}
	/* Every skipped definition is private to this expression. This also
	 * handles the second half's shared pointer without allowing outside uses. */
	uint8_t internal[40]={0};
	if(end-first>=sizeof internal)return 0;
	for(uint32_t v=first;v<=end;v++) {
		vir_insn i=f->insns[v];vir_operand_iterator it;uint32_t *p;
		vir_operand_iterator_init(&it,(vir_function *)f,&i,false);
		while((p=vir_operand_iterator_next(&it)))if(*p>=first && *p<=end)internal[*p-first]++;
	}
	for(uint32_t v=first;v<end;v++)if(uses[v]!=internal[v-first])return 0;
	*last=end;return width;
}

/* sext(shl.nsw.i32 x,k) == shl.i64(sext(x),k) on every defined input.
 * Allocation, use counts, block boundaries and legal hardware scales belong
 * to the consumer; this rule adds no lifetime or new IR definition. */
bool vir_sign_extend_shift_match(const vir_function *f,uint32_t v,unsigned *count)
{
 if(v>=f->insn_count || f->insn_count-v<2)return false;
 const vir_insn *shift=&f->insns[v],*ext=&f->insns[v+1];
 if(shift->op!=VIR_OP_SHL || shift->type!=VIR_TYPE_I32 || !(shift->flags&VIR_INSN_FLAG_NSW) ||
    ext->op!=VIR_OP_EXT || ext->type!=VIR_TYPE_I64 || ext->flags!=(2|VIR_INSN_FLAG_MS) || ext->a!=v)return false;
 const vir_insn *k=&f->insns[shift->b];
 if(k->op!=VIR_OP_ICONST || k->b>=32 || k->c)return false;
 *count=k->b;return true;
}
