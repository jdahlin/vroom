/*
 * switch-values.c: the walker's adapter for the result-table switch: emit a switch that only
 * produces constants as a table lookup when opt/switch-values.c has proved it.
 *
 * walk_switch_values_emit returns false, and the caller emits the switch normally, unless -O1 or more, a
 * real (not dry) function body without cleanups and not inside an inlined copy, not Darwin, and at
 * least three free temporaries.  The eligibility itself is the shared proof's, in
 * em_switch_values_plan.
 */
#include "walk.h"

VPRIV bool walk_switch_values_emit(codegen_context *e, uint32_t n)
{
 cc *c=e->c; walk_state *W=e->wk;
 if (!e->E->o->opt || e->E->noopt || e->dry || W->inl || W->nclean ||
     CODEGEN_IS_DARWIN(c) || WALK_LOWER(nfree)(e)<3) return false;
 em_switch_values_recipe p;
 if (!em_switch_values_plan(c,n,&p) || !em_switch_values_queue(e,n,&p)) return false;
 ast_qualified_type ct=N(N(n)->a)->ty;
 codegen_value v=walk_value_emit(e,N(n)->a);
 int r=walk_value_to_register(e,&v,ct), w=p.selector_bits/8;
 if (p.lo) v=WALK_LOWER(bin)(e,LEX_TOKEN_MINUS,w,false,v,codegen_value_constant(p.lo));
 r=v.r;
 uint32_t fallback=walk_label_new(e), done=walk_label_new(e);
 WALK_LOWER(cmpbr)(e,LEX_TOKEN_GREATER,w,false,v,codegen_value_constant(p.slots-1),fallback);
 WALK_LOWER(tkeep)(e,r);
 if (w==4) WALK_LOWER(ext)(e,r,4,false);
 unsigned shift=p.element_bytes==8?3:p.element_bytes==4?2:p.element_bytes==2?1:0;
 if (shift) v=WALK_LOWER(bin)(e,LEX_TOKEN_LESS_LESS,8,false,v,codegen_value_constant(shift));
 codegen_lvalue table={.k=CODEGEN_LVALUE_SYMBOL,.ref=CODEGEN_REFERENCE_MAKE(CODEGEN_REFERENCE_TABLE,n),.align=p.element_bytes};
 int base=WALK_LOWER(laddr)(e,&table);
 v=WALK_LOWER(bin)(e,LEX_TOKEN_PLUS,8,false,v,(codegen_value){.k=CODEGEN_VALUE_REGISTER,.r=(uint8_t)base});
 codegen_lvalue at={.k=CODEGEN_LVALUE_REGISTER,.r=v.r,.align=p.element_bytes};
 WALK_LOWER(load)(e,v.r,&at,p.element_bytes,walk_type_is_signed(c,p.ty));
 r=v.r;
 WALK_LOWER(jump)(e,done);
 walk_label_place(e,fallback);
 codegen_value d=codegen_value_constant(p.fallback);
 int dr=WALK_LOWER(toreg)(e,&d,p.element_bytes);
 WALK_LOWER(mov)(e,r,dr);
 WALK_LOWER(tfree)(e,dr);
 walk_label_place(e,done);
 e->kvalid=0;
 if (p.is_return) {
  WALK_LOWER(ret)(e,&v,W->rty,W->retlbl);
  e->reach=false;
 } else {
  codegen_lvalue dest=walk_lvalue_emit(e,p.destination);
  walk_lvalue_store(e,&dest,p.ty,&v);
  walk_value_free(e,&v);
  if (dest.k==CODEGEN_LVALUE_REGISTER) WALK_LOWER(tfree)(e,dest.r);
 }
 return true;
}
