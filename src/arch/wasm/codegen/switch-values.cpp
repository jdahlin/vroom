/* Wasm32/64 use the same bounded result table and unsigned index proof. */
#include "wimpl.h"

VPRIV bool wswitch_values(ecx *e, uint32_t n)
{
 cc *c=e->c; wst *W=e->w;
 if (!e->E->o->opt || e->E->noopt || e->dry || W->ncl || DARWIN(c)) return false;
 em_switch_values_recipe p;
 if (!em_switch_values_plan(c,n,&p) || !em_switch_values_queue(e,n,&p)) return false;
 wflow_reject(e);
 int k=p.selector_bits==64?WT_I64:WT_I32, result=wty(c,p.ty), ptr=WPT(c);
 uint32_t index=wtmp(e,k), value=wtmp(e,result);
 wexpr(e,N(n)->a,true);
 if (p.lo) {wconst(e,k,(int64_t)p.lo);wop(e,k,"sub");}
 wset(e,index);
 wget(e,index);wconst(e,k,p.slots-1);wop(e,k,"gt_u");
 wblock(e,"if",WF_IF);
 wconst(e,result,(int64_t)p.fallback);wset(e,value);
 wboundary(e,"else",WF_ELSE);
 waddr_ref(e,REF(RK_TABLE,n),0);
 wget(e,index);
 if (k==WT_I32 && ptr==WT_I64) wi(e,"i64.extend_i32_u");
 if (k==WT_I64 && ptr==WT_I32) wi(e,"i32.wrap_i64");
 unsigned shift=p.element_bytes==8?3:p.element_bytes==4?2:p.element_bytes==2?1:0;
 if (shift) {wconst(e,ptr,shift);wop(e,ptr,"shl");}
 wop(e,ptr,"add");
 wlv at={.k=WL_MEM};wload(e,&at,p.ty);wset(e,value);
 wend(e,"end_if");
 if (p.is_return) {
  wget(e,value);wset(e,W->rv);wbr(e,"br",0,false);
 } else {
  wlv dest;
  wlval(e,p.destination,&dest,p.ty);wget(e,value);wstore(e,&dest,p.ty);
 }
 wfree(e,index);wfree(e,value);
 return true;
}
