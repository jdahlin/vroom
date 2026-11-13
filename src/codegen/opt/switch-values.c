/*
 * switch-values.c: turn a switch whose cases only produce integer constants (`case 1: return 5;`,
 * `case 2: x = 7; break;`) into a table lookup, when the table is small and the proof is complete.
 *
 * em_switch_values_plan examines the one switch being emitted, with a budget of 64 cases, and fills a
 * recipe or says no; nothing in the AST is rewritten and no IR is built.  A plan that is accepted is
 * queued per function (em_switch_values_queue) and the table is written after the function, inside
 * its chunk (em_switch_values_emit, called by the backends after end_function); a retried function
 * discards the queue (em_switch_values_reset) so regeneration does not duplicate tables.  Used through
 * adapters in walk/switch-values.c, arm64 and wasm.
 */
#include "../codegen-internal.h"

typedef struct svproof {
 cc *c;
 unsigned budget, n, first;
 em_switch_case cases[64];
 uint64_t value[64];
 bool pending_default, default_seen, have_default, mode_set, is_return, assigned;
 uint64_t fallback, pending_value;
 uint32_t destination;
 ast_qualified_type ty;
} svproof;

static bool sv_integer(cc *c, ast_qualified_type t)
{
 uint64_t z = codegen_type_size(c, t);
 return codegen_type_is_integer(c,t) && !codegen_type_is_bitint(c,t) && !codegen_type_is_bitint_big(c,t) &&
        !(T(c,t)->flags & AST_TYPE_FLAG_ATOMIC) && (z==1 || z==2 || z==4 || z==8);
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
 case AST_NODE_NUMBER: break;
 case AST_NODE_CAST:
  if (x->aux!=AST_CAST_INTEGER && x->aux!=AST_CAST_NOP && x->aux!=AST_CAST_BOOL) return false;
  if (!sv_constant(p,x->a,&unused)) return false;
  break;
 case AST_NODE_UNARY:
  if (!sv_constant(p,x->a,&unused)) return false;
  break;
 case AST_NODE_BINARY:
  if (!sv_constant(p,x->a,&unused) || !sv_constant(p,x->b,&unused)) return false;
  break;
 default: return false;
 }
 cc_value cv;
 if (!cc_eval(c,n,&cv) || cv.kind!=CC_VALUE_INTEGER) return false;
 *v=(uint64_t)codegen_integer_truncate(c,cv.i,x->ty);
 return true;
}

static bool sv_terminal(svproof *p, uint32_t n, unsigned depth)
{
 cc *c=p->c;
 if (!n || !p->budget-- || depth>4) return false;
 const node *x=N(n);
 if (x->op==AST_NODE_BLOCK) {
  for (uint32_t i=0;i<x->c;i++)
   if (!sv_terminal(p,c->lists[x->b+i],depth+1)) return false;
  return true;
 }
 bool ret=x->op==AST_NODE_RETURN;
 if (ret || (x->op==AST_NODE_EXPRESSION && x->a && N(x->a)->op==AST_NODE_ASSIGN)) {
  if ((p->first==p->n && !p->pending_default) || p->assigned) return false;
  uint32_t expr=x->a, destination=0;
  ast_qualified_type ty;
  if (!ret) {
   const node *a=N(expr), *lhs=N(a->a);
   if (a->aux || lhs->op!=AST_NODE_VARIABLE) return false;
   const sym *s=&c->syms[lhs->a];
   ty=lhs->ty;
   if (s->kind!=AST_SYMBOL_VARIABLE || !s->depth || s->sc==AST_STORAGE_STATIC || s->sc==AST_STORAGE_EXTERN ||
       (AST_QUALIFIED_TYPE_QUALIFIERS(ty)&AST_QUALIFIER_VOLATILE) || codegen_symbol_attributes(c,lhs->a)->cleanup) return false;
   destination=a->a;
   expr=a->b;
  } else {
   if (!expr) return false;
   ty=N(expr)->ty;
  }
  if (!sv_integer(c,ty)) return false;
  if (p->mode_set && (p->is_return!=ret || AST_QUALIFIED_TYPE_UNQUALIFIED(p->ty)!=AST_QUALIFIED_TYPE_UNQUALIFIED(ty) ||
      (!ret && N(p->destination)->a!=N(destination)->a))) return false;
  uint64_t value;
  if (!sv_constant(p,expr,&value)) return false;
  p->mode_set=true; p->is_return=ret; p->ty=ty;
  if (destination) p->destination=destination;
  if (!ret) {p->assigned=true; p->pending_value=value; return true;}
  p->pending_value=value;
 } else if (x->op==AST_NODE_BREAK) {
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
 if (n>0x0fffffffu || N(n)->op!=AST_NODE_SWITCH) return false;
 const node *sw=N(n), *body=N(sw->b);
 ast_qualified_type ct=N(sw->a)->ty;
 unsigned bits=(unsigned)codegen_type_size(c,ct)*8;
 if (body->op!=AST_NODE_BLOCK || body->c<7 || body->c>128 ||
     !sv_integer(c,ct) || (bits!=32 && bits!=64)) return false;
 svproof p={.c=c,.budget=128};
 for (uint32_t i=0;i<body->c;i++) {
  uint32_t s=c->lists[body->b+i];
  while (s && (N(s)->op==AST_NODE_CASE || N(s)->op==AST_NODE_DEFAULT)) {
   if (!p.budget-- || p.assigned) return false;
   const node *x=N(s);
   if (x->op==AST_NODE_DEFAULT) {
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
 if (!em_switch_prepare(p.cases,p.n,bits,codegen_type_is_signed(c,ct))) return false;
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
 if (N(sw->a)->op==AST_NODE_NUMBER) return false;
 *r=(em_switch_values_recipe){.lo=lo,.fallback=p.fallback,.ty=p.ty,
  .destination=p.destination,.slots=(uint16_t)slots,.selector_bits=(uint8_t)bits,
  .element_bytes=(uint8_t)codegen_type_size(c,p.ty),.is_return=p.is_return};
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

VPRIV bool em_switch_values_queue(codegen_context *e, uint32_t n, const em_switch_values_recipe *r)
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

VPRIV void em_switch_values_reset(codegen_context *e)
{
 while (e->switch_values) {
  struct em_switch_values_data *t=e->switch_values;
  e->switch_values=t->next; free(t);
 }
}

/* Called after function size/CFI/end_function, inside its existing chunk.
 * Retries discard the per-function queue before regeneration. */
VPRIV void em_switch_values_emit(codegen_context *e)
{
 for (struct em_switch_values_data *t=e->switch_values;t;t=t->next) {
  uint32_t ref=CODEGEN_REFERENCE_MAKE(CODEGEN_REFERENCE_TABLE,t->node);
  unsigned p2=t->bytes==8?3:t->bytes==4?2:t->bytes==2?1:0;
  size_t len=(size_t)t->slots*t->bytes, h=codegen_item_open(e,CODEGEN_ITEM_TEXT);
  if (WASM(e->c)) {
   vtext_string_append(&e->out,"\t.section\t.rodata.");codegen_reference_name_append(e->c,&e->out,ref);
   vtext_string_append(&e->out,",\"\",@\n");
  } else vtext_string_append(&e->out,"\t.section\t.rodata,\"a\",@progbits\n");
  vtext_format_append(&e->out,"\t.p2align\t%u\n",p2);
  codegen_item_close(e,h);
  if (codegen_context_is_direct(e)) {
   h=codegen_item_open(e,CODEGEN_ITEM_LABEL);memcpy(codegen_item_room_need(e,4),&ref,4);codegen_item_close(e,h);
   h=codegen_item_open(e,CODEGEN_ITEM_BYTES);memcpy(codegen_item_room_need(e,len),t->data,len);codegen_item_close(e,h);
  } else {
   codegen_reference_name_append(e->c,&e->out,ref);vtext_string_append(&e->out,":\n");
   for (size_t i=0;i<len;i++) {
    vtext_string_append(&e->out,"\t.byte\t");
    vtext_unsigned_append(&e->out,t->data[i]);
    vtext_char_append(&e->out,'\n');
   }
   if (WASM(e->c)) {
    vtext_string_append(&e->out,"\t.size\t");codegen_reference_name_append(e->c,&e->out,ref);
    vtext_format_append(&e->out,", %llu\n",(unsigned long long)len);
   }
  }
 }
 em_switch_values_reset(e);
}
