/* x86-64 lowering of VIR: the switch instruction (dense table and range chain). */

#include "lowering-internal.h"

/* The allocation constraints and dispatch must agree about when the
 * selector is consumed before R10 is overwritten. Sparse range chains
 * need the original selector for every comparison. */
VPRIV bool x64_vir_switch_is_dense(const vir_function *f,const vir_insn *in,uint64_t *low,uint64_t *range)
{
	bool wide=f->insns[in->a].type==VIR_TYPE_I64;
	uint32_t stride=wide?5:3,n=f->extra[in->c];
	const uint32_t *cases=&f->extra[in->c+1];
	uint64_t lo=0,span=0,count=0;
	if(n) {
		lo=wide?(uint64_t)cases[1]<<32|cases[0]:cases[0];
		uint64_t hi=wide?(uint64_t)cases[stride*(n-1)+3]<<32|cases[stride*(n-1)+2]:cases[stride*(n-1)+1];
		span=hi-lo;
		for(uint32_t k=0;k<n;k++) {
			uint64_t a=wide?(uint64_t)cases[stride*k+1]<<32|cases[stride*k]:cases[stride*k];
			uint64_t h=wide?(uint64_t)cases[stride*k+3]<<32|cases[stride*k+2]:cases[stride*k+1];
			count+=h-a>=64?64:h-a+1;
		}
	}
	if(low)*low=lo;
	if(range)*range=span;
	return n>=6 && span<4096 && span+1<=4*count;
}

/* Switch edges retain their allocator edits in separate stubs. Dense tables use
 * signed offsets from a RIP-relative base, so the dispatch stays position independent. */
VPRIV void x64_vir_constant_compare_emit(x64_vir_context *L, int reg, int size, uint64_t value)
{
	int64_t k = size == 4 ? (int64_t)(int32_t)value : (int64_t)value;
	if (k == (int32_t)k)
		x64_op3(L->context, X_CMPI, size, XNOREG, reg, XNOREG)->immediate = k;
	else {
		x64_movi(L->context, R11, k);
		x64_op3(L->context, X_CMP, size, XNOREG, reg, R11);
	}
}

VPRIV void x64_vir_switch_lower(x64_vir_context *L, uint32_t block, const vir_insn *in, int selector)
{
	codegen_context *e = L->context;
	vir_function *f = L->function;
	bool wide = f->insns[in->a].type == VIR_TYPE_I64;
	uint32_t size = wide ? 8 : 4, stride = wide ? 5 : 3, n = f->extra[in->c];
	const uint32_t *cases = &f->extra[in->c+1];
	uint32_t *labels = malloc(((size_t)n+1)*sizeof *labels);
	if (!labels) abort();
	for (uint32_t k=0;k<=n;k++) labels[k]=x64_vir_edge_target(L,block,k,k?cases[stride*(k-1)+stride-1]:in->b);
	uint64_t lo,span;
	if(x64_vir_switch_is_dense(f,in,&lo,&span)) {
		uint32_t table=walk_label_new(e);
		if(selector!=R10 || (size==4 && !x64_last_u32(e,R10)))
			x64_op3(e,X_MOV,size,R10,selector,XNOREG);
		if (lo) {
			x64_movi(e,R11,size==4?(int64_t)(int32_t)lo:(int64_t)lo);
			x64_op3(e,X_SUB,size,R10,R10,R11);
		}
		x64_vir_constant_compare_emit(L,R10,size,span);
		record *r=x64_put(e,X_JCC);r->condition=CC_A;r->label=labels[0];
		x64_op3(e,X_LEAL,8,R11,XNOREG,XNOREG)->label=table;
		r=x64_op3(e,X_LDX,4,R10,R11,XNOREG);r->registers[3]=R10;r->shift=2;r->condition=1;
		x64_op3(e,X_ADD,8,R10,R10,R11);
		x64_op3(e,X_JMPR,8,XNOREG,R10,XNOREG);
		x64_label(e,table);
		uint32_t k=0;
		for (uint64_t offset=0;offset<=span;offset++) {
			uint64_t v=lo+offset,h,a;
			while (k<n && (h=wide?(uint64_t)cases[stride*k+3]<<32|cases[stride*k+2]:cases[stride*k+1])<v) k++;
			a=k<n?(wide?(uint64_t)cases[stride*k+1]<<32|cases[stride*k]:cases[stride*k]):0;
			r=x64_put(e,X_JTE);r->label=k<n && a<=v?labels[k+1]:labels[0];r->reference=table;
		}
	} else {
		for (uint32_t k=0;k<n;k++) {
			uint64_t a=wide?(uint64_t)cases[stride*k+1]<<32|cases[stride*k]:cases[stride*k];
			uint64_t h=wide?(uint64_t)cases[stride*k+3]<<32|cases[stride*k+2]:cases[stride*k+1];
			int reg=selector;
			if (a!=h) {
				x64_movi(e,R11,size==4?(int64_t)(int32_t)a:(int64_t)a);
				x64_op3(e,X_SUB,size,R10,selector,R11);reg=R10;
			}
			x64_vir_constant_compare_emit(L,reg,size,a==h?a:h-a);
			record *r=x64_put(e,X_JCC);r->condition=a==h?CC_E:CC_BE;r->label=labels[k+1];
		}
		x64_jump(e,labels[0]);
	}
	free(labels);
}
