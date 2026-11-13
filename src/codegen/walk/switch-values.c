/*
 * switch-values.c: the walker's adapter for the result-table switch: emit a switch that only
 * produces constants as a table lookup when opt/switch-values.c has proved it.
 *
 * wk_switch_values returns false, and the caller emits the switch normally, unless -O1 or more, a
 * real (not dry) function body without cleanups and not inside an inlined copy, not Darwin, and at
 * least three free temporaries.  The eligibility itself is the shared proof's, in
 * em_switch_values_plan.
 */
#include "walk.h"

VPRIV bool wk_switch_values(ecx *e, uint32_t n)
{
 cc *c=e->c; wkst *W=e->wk;
 if (!e->E->o->opt || e->E->noopt || e->dry || W->inl || W->nclean ||
     DARWIN(c) || LO(nfree)(e)<3) return false;
 em_switch_values_recipe p;
 if (!em_switch_values_plan(c,n,&p) || !em_switch_values_queue(e,n,&p)) return false;
 qty ct=N(N(n)->a)->ty;
 val v=wk_gen(e,N(n)->a);
 int r=wk_reg(e,&v,ct), w=p.selector_bits/8;
 if (p.lo) v=LO(bin)(e,LEX_TOKEN_MINUS,w,false,v,vconst(p.lo));
 r=v.r;
 uint32_t fallback=wk_newlabel(e), done=wk_newlabel(e);
 LO(cmpbr)(e,LEX_TOKEN_GREATER,w,false,v,vconst(p.slots-1),fallback);
 LO(tkeep)(e,r);
 if (w==4) LO(ext)(e,r,4,false);
 unsigned shift=p.element_bytes==8?3:p.element_bytes==4?2:p.element_bytes==2?1:0;
 if (shift) v=LO(bin)(e,LEX_TOKEN_LESS_LESS,8,false,v,vconst(shift));
 lval table={.k=L_SYM,.ref=REF(RK_TABLE,n),.align=p.element_bytes};
 int base=LO(laddr)(e,&table);
 v=LO(bin)(e,LEX_TOKEN_PLUS,8,false,v,(val){.k=V_REG,.r=(uint8_t)base});
 lval at={.k=L_REG,.r=v.r,.align=p.element_bytes};
 LO(load)(e,v.r,&at,p.element_bytes,wk_signed(c,p.ty));
 r=v.r;
 LO(jump)(e,done);
 wk_label(e,fallback);
 val d=vconst(p.fallback);
 int dr=LO(toreg)(e,&d,p.element_bytes);
 LO(mov)(e,r,dr);
 LO(tfree)(e,dr);
 wk_label(e,done);
 e->kvalid=0;
 if (p.is_return) {
  LO(ret)(e,&v,W->rty,W->retlbl);
  e->reach=false;
 } else {
  lval dest=wk_lval(e,p.destination);
  wk_store(e,&dest,p.ty,&v);
  wk_free(e,&v);
  if (dest.k==L_REG) LO(tfree)(e,dest.r);
 }
 return true;
}
