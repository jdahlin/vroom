/* Bounded integer-result switch tables. This examines only the switch being
 * emitted; it never rewrites the AST or creates an IR/function walk. */
#include "../emitimpl.h"

typedef struct svproof {
 cc *c;
 unsigned budget, n, first;
 em_switch_case cases[64];
 uint64_t value[64];
 bool pending_default, default_seen, have_default, mode_set, is_return, assigned;
 uint64_t fallback, pending_value;
 uint32_t destination;
 qty ty;
} svproof;

static bool sv_integer(cc *c, qty t)
{
 uint64_t z = tsize(c, t);
 return is_integer(c,t) && !em_bitint(c,t) && !em_bigint(c,t) &&
        !(T(c,t)->flags & TF_ATOMIC) && (z==1 || z==2 || z==4 || z==8);
}

/* cc_eval is called only after this small pure integer expression proof. */
static bool sv_constant(svproof *p, uint32_t n, uint64_t *v)
{
 cc *c=p->c;
 if (!n || !p->budget--) return false;
 const node *x=N(n);
 if (!sv_integer(c,x->ty)) return false;
 uint64_t unused;
 switch (x->op) {
 case N_NUM: break;
 case N_CAST:
  if (x->aux!=CK_INT && x->aux!=CK_NOP && x->aux!=CK_BOOL) return false;
  if (!sv_constant(p,x->a,&unused)) return false;
  break;
 case N_UNARY:
  if (!sv_constant(p,x->a,&unused)) return false;
  break;
 case N_BINARY:
  if (!sv_constant(p,x->a,&unused) || !sv_constant(p,x->b,&unused)) return false;
  break;
 default: return false;
 }
 cval cv;
 if (!cc_eval(c,n,&cv) || cv.kind!=CV_INT) return false;
 *v=(uint64_t)em_trunc_to(c,cv.i,x->ty);
 return true;
}

static bool sv_terminal(svproof *p, uint32_t n, unsigned depth)
{
 cc *c=p->c;
 if (!n || !p->budget-- || depth>4) return false;
 const node *x=N(n);
 if (x->op==N_BLOCK) {
  for (uint32_t i=0;i<x->c;i++)
   if (!sv_terminal(p,c->lists[x->b+i],depth+1)) return false;
  return true;
 }
 bool ret=x->op==N_RETURN;
 if (ret || (x->op==N_EXPR && x->a && N(x->a)->op==N_ASSIGN)) {
  if ((p->first==p->n && !p->pending_default) || p->assigned) return false;
  uint32_t expr=x->a, destination=0;
  qty ty;
  if (!ret) {
   const node *a=N(expr), *lhs=N(a->a);
   if (a->aux || lhs->op!=N_VAR) return false;
   const sym *s=&c->syms[lhs->a];
   ty=lhs->ty;
   if (s->kind!=S_VAR || !s->depth || s->sc==SC_STATIC || s->sc==SC_EXTERN ||
       (QQ(ty)&Q_VOLATILE) || sym_attrs(c,lhs->a)->cleanup) return false;
   destination=a->a;
   expr=a->b;
  } else {
   if (!expr) return false;
   ty=N(expr)->ty;
  }
  if (!sv_integer(c,ty)) return false;
  if (p->mode_set && (p->is_return!=ret || QUNQ(p->ty)!=QUNQ(ty) ||
      (!ret && N(p->destination)->a!=N(destination)->a))) return false;
  uint64_t value;
  if (!sv_constant(p,expr,&value)) return false;
  p->mode_set=true; p->is_return=ret; p->ty=ty;
  if (destination) p->destination=destination;
  if (!ret) {p->assigned=true; p->pending_value=value; return true;}
  p->pending_value=value;
 } else if (x->op==N_BREAK) {
  if (!p->assigned) return false;
  p->assigned=false;
 } else return false;
 for (unsigned i=p->first;i<p->n;i++) p->value[i]=p->pending_value;
 p->first=p->n;
 if (p->pending_default) {
  p->fallback=p->pending_value; p->have_default=true; p->pending_default=false;
 }
 return true;
}

VPRIV bool em_switch_values_plan(cc *c, uint32_t n, em_switch_values_recipe *r)
{
 if (n>0x0fffffffu || N(n)->op!=N_SWITCH) return false;
 const node *sw=N(n), *body=N(sw->b);
 qty ct=N(sw->a)->ty;
 unsigned bits=(unsigned)tsize(c,ct)*8;
 if (body->op!=N_BLOCK || body->c<7 || body->c>128 ||
     !sv_integer(c,ct) || (bits!=32 && bits!=64)) return false;
 svproof p={.c=c,.budget=128};
 for (uint32_t i=0;i<body->c;i++) {
  uint32_t s=c->lists[body->b+i];
  while (s && (N(s)->op==N_CASE || N(s)->op==N_DEFAULT)) {
   if (!p.budget-- || p.assigned) return false;
   const node *x=N(s);
   if (x->op==N_DEFAULT) {
    if (p.default_seen) return false;
    p.default_seen=p.pending_default=true;
   } else {
    if (p.n==64) return false;
    p.cases[p.n++]=(em_switch_case){c->ipool[x->a],c->ipool[x->b],0};
   }
   s=x->c;
  }
  if (s && !sv_terminal(&p,s,0)) return false;
 }
 if (!p.have_default || p.assigned || p.pending_default || p.first!=p.n || p.n<4) return false;
 /* Sort copied intervals, retaining each result through the target field. */
 for (unsigned i=0;i<p.n;i++) p.cases[i].target=i;
 if (!em_switch_prepare(p.cases,p.n,bits,is_signed(c,ct))) return false;
 i128 lo=p.cases[0].lo, hi=p.cases[p.n-1].hi;
 u128 span=(u128)hi-(u128)lo;
 if (span>=256) return false;
 unsigned slots=(unsigned)span+1, covered=0, distinct=1;
 uint64_t seen[65]={p.fallback};
 for (unsigned i=0;i<p.n;i++) {
  covered+=(unsigned)((u128)p.cases[i].hi-(u128)p.cases[i].lo)+1;
  uint64_t v=p.value[p.cases[i].target];
  unsigned j=0;
  while (j<distinct && seen[j]!=v) j++;
  if (j==distinct) seen[distinct++]=v;
 }
 if (covered<6 || covered*2<slots || distinct<3) return false;
 /* A literal selector already has a cheaper existing selected-case path. */
 if (N(sw->a)->op==N_NUM) return false;
 *r=(em_switch_values_recipe){.lo=lo,.fallback=p.fallback,.ty=p.ty,
  .destination=p.destination,.slots=(uint16_t)slots,.selector_bits=(uint8_t)bits,
  .element_bytes=(uint8_t)tsize(c,p.ty),.is_return=p.is_return};
 for (unsigned i=0;i<slots;i++) r->values[i]=p.fallback;
 for (unsigned i=0;i<p.n;i++) {
  unsigned a=(unsigned)((u128)p.cases[i].lo-(u128)lo);
  unsigned b=(unsigned)((u128)p.cases[i].hi-(u128)lo);
  for (;a<=b;a++) r->values[a]=p.value[p.cases[i].target];
 }
 return true;
}

struct em_switch_values_data {
 struct em_switch_values_data *next;
 uint32_t node;
 uint16_t slots;
 uint8_t bytes;
 unsigned char data[];
};

VPRIV bool em_switch_values_queue(ecx *e, uint32_t n, const em_switch_values_recipe *r)
{
 size_t len=(size_t)r->slots*r->element_bytes;
 struct em_switch_values_data *t=malloc(sizeof *t+len);
 if (!t) return false;
 t->node=n; t->slots=r->slots; t->bytes=r->element_bytes; t->next=NULL;
 for (unsigned i=0;i<r->slots;i++)
  for (unsigned b=0;b<r->element_bytes;b++)
   t->data[i*r->element_bytes+b]=(unsigned char)(r->values[i]>>(8*b));
 struct em_switch_values_data **tail=&e->switch_values;
 while (*tail) tail=&(*tail)->next;
 *tail=t;
 return true;
}

VPRIV void em_switch_values_reset(ecx *e)
{
 while (e->switch_values) {
  struct em_switch_values_data *t=e->switch_values;
  e->switch_values=t->next; free(t);
 }
}

/* Called after function size/CFI/end_function, inside its existing chunk.
 * Retries discard the per-function queue before regeneration. */
VPRIV void em_switch_values_emit(ecx *e)
{
 for (struct em_switch_values_data *t=e->switch_values;t;t=t->next) {
  uint32_t ref=REF(RK_TABLE,t->node);
  unsigned p2=t->bytes==8?3:t->bytes==4?2:t->bytes==2?1:0;
  size_t len=(size_t)t->slots*t->bytes, h=it_open(e,IT_TEXT);
  if (WASM(e->c)) {
   tb_s(&e->out,"\t.section\t.rodata.");ref_name(e->c,&e->out,ref);
   tb_s(&e->out,",\"\",@\n");
  } else tb_s(&e->out,"\t.section\t.rodata,\"a\",@progbits\n");
  tb_f(&e->out,"\t.p2align\t%u\n",p2);
  it_close(e,h);
  if (direct(e)) {
   h=it_open(e,IT_LABEL);memcpy(it_room(e,4),&ref,4);it_close(e,h);
   h=it_open(e,IT_BYTES);memcpy(it_room(e,len),t->data,len);it_close(e,h);
  } else {
   ref_name(e->c,&e->out,ref);tb_s(&e->out,":\n");
   for (size_t i=0;i<len;i++) tb_f(&e->out,"\t.byte\t%u\n",t->data[i]);
  }
 }
 em_switch_values_reset(e);
}
