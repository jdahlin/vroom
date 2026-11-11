/* Bounded pure expression value numbering for sibling operands. */
#include "../emitimpl.h"

typedef struct em_sibling_state {
 em_value_table table;
 uint32_t next, nodes;
} em_sibling_state;

static uint32_t em_sibling_value(cc *c, em_sibling_state *s, uint32_t n)
{
 const node *x;
 em_value_key key = {0};
 uint32_t left, right, value;
 const sym *sym;
 if (!n || !s->nodes) return 0;
 --s->nodes;
 x = N(n);
 if (!is_integer(c, x->ty) || em_bitint(c, x->ty) ||
     (tsize(c, x->ty) != 4 && tsize(c, x->ty) != 8)) return 0;
 if (x->op == N_VAR) {
  sym = &c->syms[x->a];
  if (!is_local_var(c, x->a) || sym->kind != S_VAR || sym->sc == SC_STATIC || sym->sc == SC_EXTERN ||
      (sym->flags & SF_ADDR_TAKEN) || (QQ(sym->ty) & Q_VOLATILE) ||
      (T(c, sym->ty)->flags & TF_ATOMIC) || sym_attrs(c, x->a)->asmname) return 0;
  return x->a + 1;
 }
 key.type = QUNQ(x->ty);
 if (x->op == N_NUM) {
  key.operation = 1;
  key.immediate = (uint64_t)em_trunc_to(c, c->ipool[x->a], x->ty);
 } else if (x->op == N_BINARY && (x->aux == T_PLUS || x->aux == T_MINUS || x->aux == T_STAR ||
                                  x->aux == T_AMP || x->aux == T_PIPE || x->aux == T_CARET)) {
  left = em_sibling_value(c, s, x->a);
  right = em_sibling_value(c, s, x->b);
  if (!left || !right) return 0;
  if (x->aux != T_MINUS && left > right) { value = left; left = right; right = value; }
  key.operation = 2 + x->aux;
  key.left = left; key.right = right;
 } else return 0;
 value = em_values_lookup(&s->table, key);
 if (value) return value;
 if (!s->next || !em_values_insert(&s->table, key, s->next)) return 0;
 return s->next++;
}

static __attribute__((noinline)) bool em_sibling_match(cc *c, uint32_t a, uint32_t b)
{
 em_sibling_state s;
 uint32_t left, right;
 s.next = c->nsym + 1; s.nodes = 32;
 em_values_reset(&s.table, 64);
 left = em_sibling_value(c, &s, a);
 right = em_sibling_value(c, &s, b);
 return left && left == right;
}

VPRIV bool em_sibling_values(ecx *e, uint32_t n)
{
 cc *c = e->c;
 const node *x = N(n), *a, *b;
 if (!e->E->o->opt || e->E->noopt || x->op != N_BINARY ||
     (x->aux != T_PLUS && x->aux != T_MINUS && x->aux != T_STAR &&
      x->aux != T_AMP && x->aux != T_PIPE && x->aux != T_CARET)) return false;
 a = N(x->a); b = N(x->b);
 if (a->op != N_BINARY || b->op != N_BINARY || QUNQ(a->ty) != QUNQ(x->ty) ||
     QUNQ(b->ty) != QUNQ(x->ty) || c->nsym >= UINT32_MAX - 32) return false;
 return em_sibling_match(c, x->a, x->b);
}
