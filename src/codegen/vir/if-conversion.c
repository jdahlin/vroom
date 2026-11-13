/* If-conversion of diamonds and triangles into selects. */
#define WALK_PREFIX virbuild_
#define WALK_LOWER_PREFIX virbuild_
#include "../walk/walk.h"
#include "../walk/proto.h"
#include "preir.h"

/*
 * If-conversion, as LLVM's SimplifyCFG folds a two-entry phi: b ends in brif c, T, F
 * where the arms hold nothing (or at most two cheap instructions without effects
 * each, which move up into b) and meet in J (a diamond: both arms empty blocks of b's
 * alone; a triangle: one arm, the other edge goes to J itself) and J's parameters
 * differ between the two entries in at most eight values. Each becomes select c, t, f
 * at the end of b (its operands are defined in b or above it now), b jumps to
 * J and the arms go. The selects share the original condition.
 */
/* A branch arm often reloads the value its condition just read. Reuse only exact
 * expressions and loads with no intervening write/call; no speculative memory access. */
static bool virbuild_expression_is_same(virbuild_context *V, uint32_t a, uint32_t b, uint32_t *budget)
{
	a=virbuild_value_resolve(V,a), b=virbuild_value_resolve(V,b);
	if (a==b) return true;
	if (!*budget) return false;
	--*budget;
	vir_insn x=V->insns[a].insn,y=V->insns[b].insn;
	if (x.op!=y.op || x.type!=y.type || x.flags!=y.flags || !(vir_opcode_table[x.op].effects & VIR_EFFECT_P) || x.op==VIR_OP_BPARAM || x.op==VIR_OP_PARAM)
		return false;
	uint32_t u[3]={x.a,x.b,x.c},v[3]={y.a,y.b,y.c};
	for(int k=0;k<3;k++) {
		if (virbuild_operand_kind_is_value(vir_opcode_table[x.op].operand_kinds[k])) {if(!virbuild_expression_is_same(V,u[k],v[k],budget))return false;}
		else if(u[k]!=v[k])return false;
	}
	return true;
}
static void virbuild_arm_values_reuse(virbuild_context *V,uint32_t a,uint32_t b)
{
	if (a==b || V->blocks[a].predecessor_count!=1 || V->predecessors[V->blocks[a].predecessor_first]!=b) return;
	uint32_t nb=0,na=0;
	for(uint32_t q=V->blocks[b].first_insn;q;q=V->insns[q].next) if(++nb>64)return;
	for(uint32_t q=V->blocks[a].first_insn;q;q=V->insns[q].next) if(++na>16)return;
	bool changed=false;
	for(uint32_t q=V->blocks[a].first_insn;q;q=V->insns[q].next) {
		if(virbuild_value_resolve(V,q)!=q)continue;
		vir_insn in=V->insns[q].insn;
		bool load=in.op==VIR_OP_LOAD && !(in.flags & VIR_INSN_FLAG_VOL);
		if ((vir_opcode_table[in.op].effects & (VIR_EFFECT_W|VIR_EFFECT_C|VIR_EFFECT_A)) || (in.op==VIR_OP_LOAD && (in.flags & VIR_INSN_FLAG_VOL))) changed=true;
		if (!(vir_opcode_table[in.op].effects & VIR_EFFECT_P) && (!load || changed)) continue;
		uint32_t found=0;
		for(uint32_t p=V->blocks[b].first_insn;p;p=V->insns[p].next) {
			if(virbuild_value_resolve(V,p)!=p)continue;
			vir_insn x=V->insns[p].insn;
			if(load && ((vir_opcode_table[x.op].effects & (VIR_EFFECT_W|VIR_EFFECT_C|VIR_EFFECT_A)) || (x.op==VIR_OP_LOAD && (x.flags & VIR_INSN_FLAG_VOL))))found=0;
			uint32_t budget=128;
			if(load ? x.op==VIR_OP_LOAD && x.type==in.type && x.flags==in.flags && x.b==in.b && virbuild_expression_is_same(V,in.a,x.a,&budget) : virbuild_expression_is_same(V,q,p,&budget))found=p;
		}
		if(found)V->forward[q]=found;
	}
}

VPRIV bool virbuild_arm_is_hoistable(virbuild_context *V, uint32_t a, uint32_t b, uint32_t cond, uint32_t *n)
{
	const virbuild_preir_block *k = &V->blocks[a];

	if (a == b || !k->terminator || V->insns[k->terminator].insn.op != VIR_OP_BR || k->predecessor_count != 1 || V->predecessors[k->predecessor_first] != b)
		return false;
	for (uint32_t p = k->first_parameter; p; p = V->insns[p].next)
		if (V->forward[p] == p)
			return false;
	for (uint32_t q = k->first_insn; q; q = V->insns[q].next) {
		uint32_t op = V->insns[q].insn.op;

		if (virbuild_value_resolve(V, q) != q || virbuild_promote_store_is_dead(V, q) || (op == VIR_OP_MEMSET && (V->insns[q].insn.flags & VIRBUILD_MEMSET_LIFETIME_FLAG)))
			continue;
		/* cheap and without effects: computed whichever way the branch went (at most 2) */
		if ((op != VIR_OP_ADD && op != VIR_OP_SUB && op != VIR_OP_AND && op != VIR_OP_OR && op != VIR_OP_XOR && op != VIR_OP_SHL && op != VIR_OP_LSHR &&
		     op != VIR_OP_ASHR && op != VIR_OP_EXT && op != VIR_OP_TRUNC && op != VIR_OP_I2P && op != VIR_OP_P2I && op != VIR_OP_PTRADD) ||
		    V->insns[q].insn.a == cond || V->insns[q].insn.b == cond || ++*n > 2)
			return false;
	}
	return true;
}

/* arm a's instructions into b, before b's last instruction when that is the condition
 * (the select stays next to its compare) */
static void virbuild_arm_hoist(virbuild_context *V, uint32_t a, uint32_t b, uint32_t cond)
{
	virbuild_preir_block *k = &V->blocks[b];
	uint32_t prev = 0, last = 0, nx;

	if (k->last_insn == cond) {
		/* a joined condition (virbuild_conditions_merge: compare, compare, and/or ...) ends b: before all of it,
		 * when it is b's last instructions and the arm reads none of them */
		uint32_t m[16], nm = 0, v = cond;
		bool ok = true;

		last = cond;
		while (nm + 2 <= 16 && (V->insns[v].insn.op == VIR_OP_AND || V->insns[v].insn.op == VIR_OP_OR) && V->insns[v].insn.type == VIR_TYPE_I32 &&
		       V->insns[v].block == b && V->insns[virbuild_value_resolve(V, V->insns[v].insn.b)].insn.op == VIR_OP_ICMP) {
			m[nm++] = v, m[nm++] = virbuild_value_resolve(V, V->insns[v].insn.b);
			v = virbuild_value_resolve(V, V->insns[v].insn.a);
		}
		if (nm && V->insns[v].insn.op == VIR_OP_ICMP && V->insns[v].block == b) {
			uint32_t q, left = nm + 1;

			m[nm++] = v;
			for (q = v; q && left; q = V->insns[q].next, left--) {	/* v ... cond: exactly the members, in a row */
				bool in = false;

				for (uint32_t j = 0; j < nm; j++)
					in |= m[j] == q;
				if (!in)
					break;
			}
			ok = !q && !left;
			for (q = V->blocks[a].first_insn; q && ok; q = V->insns[q].next)
				for (uint32_t j = 0; j < nm; j++)
					if (virbuild_value_resolve(V, V->insns[q].insn.a) == m[j] || virbuild_value_resolve(V, V->insns[q].insn.b) == m[j])
						ok = false;
			if (ok)
				last = v;
		}
		for (uint32_t q = k->first_insn; q != last; q = V->insns[q].next)
			prev = q;
	} else {
		prev = k->last_insn;
	}
	for (uint32_t q = V->blocks[a].first_insn; q; q = nx) {
		nx = V->insns[q].next;
		if (virbuild_value_resolve(V, q) != q || virbuild_promote_store_is_dead(V, q) || V->insns[q].insn.op == VIR_OP_MEMSET)
			continue;
		V->insns[q].block = b;
		V->insns[q].next = last;
		if (prev)
			V->insns[prev].next = q;
		else
			k->first_insn = q;
		prev = q;
	}
	if (!last)
		k->last_insn = prev;
	V->blocks[a].first_insn = V->blocks[a].last_insn = 0;
}

VPRIV bool virbuild_if_convert(codegen_context *e)
{
	virbuild_context *V = e->vir_builder;
	bool any = false;

	for (uint32_t i = 0; i < V->block_order_count; i++) {
		uint32_t b = V->block_order[i], t = V->blocks[b].terminator, T, F, J, it, jf, nsel = 0, cond, nt, nf;
		bool et, ef;
		uint32_t sels[8], tv[8], fv[8];
		bool badsel = false;

		if (!t || V->insns[t].insn.op != VIR_OP_BRIF)
			continue;
		T = V->insns[t].insn.b, F = V->insns[t].insn.c;
		if (T == F)
			continue;
		cond = virbuild_value_resolve(V, V->insns[t].insn.a);
		nt = nf = 0;
		virbuild_arm_values_reuse(V,T,b);
		virbuild_arm_values_reuse(V,F,b);
		et = virbuild_arm_is_hoistable(V, T, b, cond, &nt), ef = virbuild_arm_is_hoistable(V, F, b, cond, &nf);
		if (et && ef && V->insns[V->blocks[T].terminator].insn.a == V->insns[V->blocks[F].terminator].insn.a)
			J = V->insns[V->blocks[T].terminator].insn.a;
		else if (et && V->insns[V->blocks[T].terminator].insn.a == F)
			J = F, ef = false;
		else if (ef && V->insns[V->blocks[F].terminator].insn.a == T)
			J = T, et = false;
		else
			continue;
		if (J == b || (et && ef && (J == T || J == F)))
			continue;
		it = virbuild_block_predecessor_index(V, J, et ? T : b), jf = virbuild_block_predecessor_index(V, J, ef ? F : b);
		if (it == VIRBUILD_BLOCK_NONE || jf == VIRBUILD_BLOCK_NONE || it == jf)
			continue;
		for (uint32_t p = V->blocks[J].first_parameter; p; p = V->insns[p].next) {
			uint32_t a, c;

			if (V->forward[p] != p)
				continue;
			a = virbuild_value_resolve(V, V->extra[V->insns[p].insn.b + it]), c = virbuild_value_resolve(V, V->extra[V->insns[p].insn.b + jf]);
			if (a == c)
				continue;
			if (nsel == 8 || V->insns[p].insn.type == VIR_TYPE_VOID || V->insns[p].insn.type == VIR_TYPE_MULTI || a == p || c == p) {
				badsel = true;
				break;
			}
			sels[nsel] = p, tv[nsel] = a, fv[nsel++] = c;
		}
		/* Values that each either keep a block parameter or replace it (a maximum, and its
		 * index, in a search loop) stay a branch: as selects every round waits for the one
		 * before, and such a branch is mostly not taken.  TSVC s314-s316, s3110: three to
		 * nine times slower as selects; sqlite's speedtest1 is the same either way.  Measured on
		 * arm64 only: x86-64 keeps the selects, which its vectorizer takes as ordered
		 * reductions (bench/vcc-vir-x86.py requires that of maxval, maxindex, lastnegative). */
		if (!badsel && nsel && !CODEGEN_IS_X64(e->compile_context)) {
			bool all = true;

			for (uint32_t k = 0; k < nsel; k++)
				all &= V->insns[tv[k]].insn.op == VIR_OP_BPARAM || V->insns[fv[k]].insn.op == VIR_OP_BPARAM;
			badsel = all;
		}
		if (badsel)
			continue;
		if (!nsel && (et ? nt : 0) + (ef ? nf : 0))	/* (nothing to select: the arms' work is dead or shared; leave it) */
			continue;
		if (et)
			virbuild_arm_hoist(V, T, b, cond);
		if (ef)
			virbuild_arm_hoist(V, F, b, cond);
		for (uint32_t k = 0; k < nsel; k++) {
			uint32_t s = virbuild_block_insn_append(e, b, VIR_OP_SELECT, V->insns[sels[k]].insn.type, 0, cond, tv[k], fv[k]);
			V->insns[s].location = V->insns[t].location;
			V->extra[V->insns[sels[k]].insn.b + it] = s;
		}

		/* J's entry `it` is b's now; entry jf goes */
		V->predecessors[V->blocks[J].predecessor_first + it] = b;
		for (uint32_t p = V->blocks[J].first_parameter; p; p = V->insns[p].next)
			if (V->insns[p].insn.b != VIRBUILD_BLOCK_NONE)
				for (uint32_t k = jf; k + 1 < V->blocks[J].predecessor_count; k++)
					V->extra[V->insns[p].insn.b + k] = V->extra[V->insns[p].insn.b + k + 1];
		for (uint32_t k = jf; k + 1 < V->blocks[J].predecessor_count; k++)
			V->predecessors[V->blocks[J].predecessor_first + k] = V->predecessors[V->blocks[J].predecessor_first + k + 1];
		V->blocks[J].predecessor_count--;
		V->insns[t].insn = (vir_insn){ VIR_OP_BR, VIR_TYPE_VOID, 0, J, 0, 0 };
		if (et)
			V->blocks[T].is_reachable = 0;
		if (ef)
			V->blocks[F].is_reachable = 0;
		any = true;
	}
	if (any) {
		uint32_t m = 0;

		for (uint32_t i = 0; i < V->block_order_count; i++)
			if (V->blocks[V->block_order[i]].is_reachable)
				V->block_order[m++] = V->block_order[i];
		V->block_order_count = m;
	}
	return any;
}
