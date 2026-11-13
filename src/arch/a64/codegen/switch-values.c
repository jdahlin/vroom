/* AArch64 scalar switch-result tables. */
#include "../../../codegen/codegen-internal.h"

VPRIV bool a64_switch_values(codegen_context *e, uint32_t n)
{
 cc *c=e->compile_context;
 if (!e->unit->options->opt || e->unit->cleanups_disabled || e->dry_run_depth || e->function_instance->is_inlined || e->cleanup_count ||
     CODEGEN_IS_DARWIN(c) || __builtin_popcount(e->temporaries_in_use | e->temporaries_reserved)>14) return false;
 em_switch_values_recipe p;
 if (!em_switch_values_plan(c,n,&p) || !em_switch_values_queue(e,n,&p)) return false;
 codegen_value v=gen(e,N(n)->a);
 int r=toreg(e,&v), cx=p.selector_bits==64;
 if (p.lo) opi(e,I_SUBI,cx,r,r,(int64_t)p.lo);
 uint32_t fallback=newlabel(e), done=newlabel(e);
 codegen_value a=codegen_value_register(r), b=codegen_value_constant(p.slots-1);
 int cond=emit_cmp(e,LEX_TOKEN_GREATER,cx,false,&a,&b);
 val_free(e,&b);
 bcond(e,cond,fallback);
 if (!cx) extend(e,r,r,4,false);
 unsigned shift=p.element_bytes==8?3:p.element_bytes==4?2:p.element_bytes==2?1:0;
 if (shift) opbf(e,I_UBFM,1,r,r,64-shift,63-shift);
 int base=treg(e);
 addr_of_ref(e,base,CODEGEN_REFERENCE_MAKE(CODEGEN_REFERENCE_TABLE,n),0);
 op3(e,I_ADD,1,r,r,base);
 tfree(e,base);
 codegen_lvalue at={.kind=CODEGEN_LVALUE_REGISTER,.register_number=(uint8_t)r,.align=p.element_bytes};
 codegen_value result=load(e,&at,p.ty);
 /* load owns its result; keep a single result register across both edges. */
 int rr=toreg(e,&result);
 if (rr!=r) {mov(e,1,r,rr);val_free(e,&result);}
 jump(e,done);
 deflabel(e,fallback);
 movi(e,1,r,(int64_t)p.fallback);
 deflabel(e,done);
 v=codegen_value_register(r); e->known_values_valid=0;
 if (p.is_return) {
  move_to(e,0,-1,&v,p.ty);
  val_free(e,&v);
  jump(e,e->function_instance->return_label);
 } else {
  codegen_lvalue dest=gen_lval(e,p.destination);
  store(e,&dest,p.ty,&v); lfree(e,&dest); val_free(e,&v);
 }
 return true;
}
