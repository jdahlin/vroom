/* AArch64 scalar switch-result tables. */
#include "../../../codegen/emitimpl.h"

VPRIV bool a64_switch_values(ecx *e, uint32_t n)
{
 cc *c=e->c;
 if (!e->E->o->opt || e->E->noopt || e->dry || e->f->inl || e->nclean ||
     DARWIN(c) || __builtin_popcount(e->live | e->resv)>14) return false;
 em_switch_values_recipe p;
 if (!em_switch_values_plan(c,n,&p) || !em_switch_values_queue(e,n,&p)) return false;
 val v=gen(e,N(n)->a);
 int r=toreg(e,&v), cx=p.selector_bits==64;
 if (p.lo) opi(e,I_SUBI,cx,r,r,(int64_t)p.lo);
 uint32_t fallback=newlabel(e), done=newlabel(e);
 val a=vreg(r), b=vconst(p.slots-1);
 int cond=emit_cmp(e,LEX_TOKEN_GREATER,cx,false,&a,&b);
 val_free(e,&b);
 bcond(e,cond,fallback);
 if (!cx) extend(e,r,r,4,false);
 unsigned shift=p.element_bytes==8?3:p.element_bytes==4?2:p.element_bytes==2?1:0;
 if (shift) opbf(e,I_UBFM,1,r,r,64-shift,63-shift);
 int base=treg(e);
 addr_of_ref(e,base,REF(RK_TABLE,n),0);
 op3(e,I_ADD,1,r,r,base);
 tfree(e,base);
 lval at={.k=L_REG,.r=(uint8_t)r,.align=p.element_bytes};
 val result=load(e,&at,p.ty);
 /* load owns its result; keep a single result register across both edges. */
 int rr=toreg(e,&result);
 if (rr!=r) {mov(e,1,r,rr);val_free(e,&result);}
 jump(e,done);
 deflabel(e,fallback);
 movi(e,1,r,(int64_t)p.fallback);
 deflabel(e,done);
 v=vreg(r); e->kvalid=0;
 if (p.is_return) {
  move_to(e,0,-1,&v,p.ty);
  val_free(e,&v);
  jump(e,e->f->retlbl);
 } else {
  lval dest=gen_lval(e,p.destination);
  store(e,&dest,p.ty,&v); lfree(e,&dest); val_free(e,&v);
 }
 return true;
}
