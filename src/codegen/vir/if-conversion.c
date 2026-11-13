/* If-conversion of diamonds and triangles into selects. */
#define WK_PREFIX vw_
#define LO_PREFIX vw_
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
static bool vw_exprsame(vwst *V, uint32_t a, uint32_t b, uint32_t *budget)
{
	a=vw_find(V,a), b=vw_find(V,b);
	if (a==b) return true;
	if (!*budget) return false;
	--*budget;
	vir_ins x=V->p[a].in,y=V->p[b].in;
	if (x.op!=y.op || x.ty!=y.ty || x.fl!=y.fl || !(vir_optab[x.op].eff & EF_P) || x.op==VO_BPARAM || x.op==VO_PARAM)
		return false;
	uint32_t u[3]={x.a,x.b,x.c},v[3]={y.a,y.b,y.c};
	for(int k=0;k<3;k++) {
		if (vw_isval(vir_optab[x.op].k[k])) {if(!vw_exprsame(V,u[k],v[k],budget))return false;}
		else if(u[k]!=v[k])return false;
	}
	return true;
}
static void vw_armreuse(vwst *V,uint32_t a,uint32_t b)
{
	if (a==b || V->b[a].npred!=1 || V->pr[V->b[a].pred]!=b) return;
	uint32_t nb=0,na=0;
	for(uint32_t q=V->b[b].head;q;q=V->p[q].next) if(++nb>64)return;
	for(uint32_t q=V->b[a].head;q;q=V->p[q].next) if(++na>16)return;
	bool changed=false;
	for(uint32_t q=V->b[a].head;q;q=V->p[q].next) {
		if(vw_find(V,q)!=q)continue;
		vir_ins in=V->p[q].in;
		bool load=in.op==VO_LOAD && !(in.fl & VIF_VOL);
		if ((vir_optab[in.op].eff & (EF_W|EF_C|EF_A)) || (in.op==VO_LOAD && (in.fl & VIF_VOL))) changed=true;
		if (!(vir_optab[in.op].eff & EF_P) && (!load || changed)) continue;
		uint32_t found=0;
		for(uint32_t p=V->b[b].head;p;p=V->p[p].next) {
			if(vw_find(V,p)!=p)continue;
			vir_ins x=V->p[p].in;
			if(load && ((vir_optab[x.op].eff & (EF_W|EF_C|EF_A)) || (x.op==VO_LOAD && (x.fl & VIF_VOL))))found=0;
			uint32_t budget=128;
			if(load ? x.op==VO_LOAD && x.ty==in.ty && x.fl==in.fl && x.b==in.b && vw_exprsame(V,in.a,x.a,&budget) : vw_exprsame(V,q,p,&budget))found=p;
		}
		if(found)V->fwd[q]=found;
	}
}

VPRIV bool vw_armempty(vwst *V, uint32_t a, uint32_t b, uint32_t cond, uint32_t *n)
{
	const vwb *k = &V->b[a];

	if (a == b || !k->term || V->p[k->term].in.op != VO_BR || k->npred != 1 || V->pr[k->pred] != b)
		return false;
	for (uint32_t p = k->phead; p; p = V->p[p].next)
		if (V->fwd[p] == p)
			return false;
	for (uint32_t q = k->head; q; q = V->p[q].next) {
		uint32_t op = V->p[q].in.op;

		if (vw_find(V, q) != q || vw_promdead(V, q) || (op == VO_MEMSET && (V->p[q].in.fl & VW_LIFETIME)))
			continue;
		/* cheap and without effects: computed whichever way the branch went (at most 2) */
		if ((op != VO_ADD && op != VO_SUB && op != VO_AND && op != VO_OR && op != VO_XOR && op != VO_SHL && op != VO_LSHR &&
		     op != VO_ASHR && op != VO_EXT && op != VO_TRUNC && op != VO_I2P && op != VO_P2I && op != VO_PTRADD) ||
		    V->p[q].in.a == cond || V->p[q].in.b == cond || ++*n > 2)
			return false;
	}
	return true;
}

/* arm a's instructions into b, before b's last instruction when that is the condition
 * (the select stays next to its compare) */
static void vw_hoist(vwst *V, uint32_t a, uint32_t b, uint32_t cond)
{
	vwb *k = &V->b[b];
	uint32_t prev = 0, last = 0, nx;

	if (k->tail == cond) {
		/* a joined condition (vw_condmerge: compare, compare, and/or ...) ends b: before all of it,
		 * when it is b's last instructions and the arm reads none of them */
		uint32_t m[16], nm = 0, v = cond;
		bool ok = true;

		last = cond;
		while (nm + 2 <= 16 && (V->p[v].in.op == VO_AND || V->p[v].in.op == VO_OR) && V->p[v].in.ty == VT_I32 &&
		       V->p[v].blk == b && V->p[vw_find(V, V->p[v].in.b)].in.op == VO_ICMP) {
			m[nm++] = v, m[nm++] = vw_find(V, V->p[v].in.b);
			v = vw_find(V, V->p[v].in.a);
		}
		if (nm && V->p[v].in.op == VO_ICMP && V->p[v].blk == b) {
			uint32_t q, left = nm + 1;

			m[nm++] = v;
			for (q = v; q && left; q = V->p[q].next, left--) {	/* v ... cond: exactly the members, in a row */
				bool in = false;

				for (uint32_t j = 0; j < nm; j++)
					in |= m[j] == q;
				if (!in)
					break;
			}
			ok = !q && !left;
			for (q = V->b[a].head; q && ok; q = V->p[q].next)
				for (uint32_t j = 0; j < nm; j++)
					if (vw_find(V, V->p[q].in.a) == m[j] || vw_find(V, V->p[q].in.b) == m[j])
						ok = false;
			if (ok)
				last = v;
		}
		for (uint32_t q = k->head; q != last; q = V->p[q].next)
			prev = q;
	} else {
		prev = k->tail;
	}
	for (uint32_t q = V->b[a].head; q; q = nx) {
		nx = V->p[q].next;
		if (vw_find(V, q) != q || vw_promdead(V, q) || V->p[q].in.op == VO_MEMSET)
			continue;
		V->p[q].blk = b;
		V->p[q].next = last;
		if (prev)
			V->p[prev].next = q;
		else
			k->head = q;
		prev = q;
	}
	if (!last)
		k->tail = prev;
	V->b[a].head = V->b[a].tail = 0;
}

VPRIV bool vw_ifconv(ecx *e)
{
	vwst *V = e->vw;
	bool any = false;

	for (uint32_t i = 0; i < V->norder; i++) {
		uint32_t b = V->order[i], t = V->b[b].term, T, F, J, it, jf, nsel = 0, cond, nt, nf;
		bool et, ef;
		uint32_t sels[8], tv[8], fv[8];
		bool badsel = false;

		if (!t || V->p[t].in.op != VO_BRIF)
			continue;
		T = V->p[t].in.b, F = V->p[t].in.c;
		if (T == F)
			continue;
		cond = vw_find(V, V->p[t].in.a);
		nt = nf = 0;
		vw_armreuse(V,T,b);
		vw_armreuse(V,F,b);
		et = vw_armempty(V, T, b, cond, &nt), ef = vw_armempty(V, F, b, cond, &nf);
		if (et && ef && V->p[V->b[T].term].in.a == V->p[V->b[F].term].in.a)
			J = V->p[V->b[T].term].in.a;
		else if (et && V->p[V->b[T].term].in.a == F)
			J = F, ef = false;
		else if (ef && V->p[V->b[F].term].in.a == T)
			J = T, et = false;
		else
			continue;
		if (J == b || (et && ef && (J == T || J == F)))
			continue;
		it = vw_predix(V, J, et ? T : b), jf = vw_predix(V, J, ef ? F : b);
		if (it == VW_NOBLK || jf == VW_NOBLK || it == jf)
			continue;
		for (uint32_t p = V->b[J].phead; p; p = V->p[p].next) {
			uint32_t a, c;

			if (V->fwd[p] != p)
				continue;
			a = vw_find(V, V->x[V->p[p].in.b + it]), c = vw_find(V, V->x[V->p[p].in.b + jf]);
			if (a == c)
				continue;
			if (nsel == 8 || V->p[p].in.ty == VT_VOID || V->p[p].in.ty == VT_MULTI || a == p || c == p) {
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
		if (!badsel && nsel && !X86(e->c)) {
			bool all = true;

			for (uint32_t k = 0; k < nsel; k++)
				all &= V->p[tv[k]].in.op == VO_BPARAM || V->p[fv[k]].in.op == VO_BPARAM;
			badsel = all;
		}
		if (badsel)
			continue;
		if (!nsel && (et ? nt : 0) + (ef ? nf : 0))	/* (nothing to select: the arms' work is dead or shared; leave it) */
			continue;
		if (et)
			vw_hoist(V, T, b, cond);
		if (ef)
			vw_hoist(V, F, b, cond);
		for (uint32_t k = 0; k < nsel; k++) {
			uint32_t s = vw_ins_in(e, b, VO_SELECT, V->p[sels[k]].in.ty, 0, cond, tv[k], fv[k]);
			V->p[s].loc = V->p[t].loc;
			V->x[V->p[sels[k]].in.b + it] = s;
		}

		/* J's entry `it` is b's now; entry jf goes */
		V->pr[V->b[J].pred + it] = b;
		for (uint32_t p = V->b[J].phead; p; p = V->p[p].next)
			if (V->p[p].in.b != VW_NOBLK)
				for (uint32_t k = jf; k + 1 < V->b[J].npred; k++)
					V->x[V->p[p].in.b + k] = V->x[V->p[p].in.b + k + 1];
		for (uint32_t k = jf; k + 1 < V->b[J].npred; k++)
			V->pr[V->b[J].pred + k] = V->pr[V->b[J].pred + k + 1];
		V->b[J].npred--;
		V->p[t].in = (vir_ins){ VO_BR, VT_VOID, 0, J, 0, 0 };
		if (et)
			V->b[T].reach = 0;
		if (ef)
			V->b[F].reach = 0;
		any = true;
	}
	if (any) {
		uint32_t m = 0;

		for (uint32_t i = 0; i < V->norder; i++)
			if (V->b[V->order[i]].reach)
				V->order[m++] = V->order[i];
		V->norder = m;
	}
	return any;
}
