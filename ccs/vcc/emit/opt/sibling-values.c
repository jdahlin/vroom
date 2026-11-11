/* Bounded pure expression value numbering for sibling operands. */
#include "../emitimpl.h"

typedef struct em_sibling_state {
 em_value_table table;
 uint32_t next, nodes;
 uint32_t target, match, symbol, symnode;
 em_value_key last_key, target_key;
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
  if (s->target && x->a == s->symbol) s->symnode = n;
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
 s->last_key = key;
 value = em_values_lookup(&s->table, key);
 if (!s->table.active) return 0;
 /* Keep the initializer's identity available even if intervening expression
  * entries collide with it. Matching every field prevents false hits. */
 if (s->target && key.operation == s->target_key.operation && key.type == s->target_key.type &&
     key.left == s->target_key.left && key.right == s->target_key.right &&
     key.immediate == s->target_key.immediate) {
  if (x->op == N_BINARY) s->match = n;
  return s->target;
 }
 if (value) {
  if (s->target && value == s->target && x->op == N_BINARY) s->match = n;
  return value;
 }
 if (!s->next || !em_values_insert(&s->table, key, s->next)) return 0;
 return s->next++;
}

static __attribute__((noinline)) bool em_sibling_match(cc *c, uint32_t a, uint32_t b)
{
 em_sibling_state s = {0};
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

/* Reuse an ordinary initialized local in a later pure expression. The
 * consumer must already reference that local, so unused-init elimination
 * cannot remove its definition. The value-numbering scan fails closed. */
static __attribute__((noinline)) bool em_decl_reuse_match(cc *c, uint32_t init, uint32_t ret,
                                                            uint32_t symbol, uint32_t *from, uint32_t *to)
{
 em_sibling_state s = {0};
 uint32_t result;
 s.next = c->nsym + 1; s.nodes = 32; s.symbol = symbol;
 em_values_reset(&s.table, 64);
 s.target = em_sibling_value(c, &s, init);
 if (!s.target) return false;
 s.target_key = s.last_key;
 result = em_sibling_value(c, &s, ret);
 if (!result || !s.match || !s.symnode) return false;
 *from = s.match; *to = s.symnode;
 return true;
}

VPRIV bool em_decl_reuse_values(ecx *e, uint32_t decl, uint32_t consumer, uint32_t *from, uint32_t *to)
{
 cc *c = e->c;
 const node *d, *r;
 const sym *s;
 uint32_t expr;
 if (!e->E->o->opt || e->E->noopt || !decl || !consumer) return false;
 d = N(decl); r = N(consumer);
 if (d->op != N_DECL || !d->b || N(d->b)->op != N_BINARY || c->nsym >= UINT32_MAX - 32) return false;
 s = &c->syms[d->a];
 if (!is_local_var(c, d->a) || s->kind != S_VAR || s->sc == SC_STATIC || s->sc == SC_EXTERN ||
     (s->flags & SF_ADDR_TAKEN) || (QQ(s->ty) & Q_VOLATILE) || (T(c, s->ty)->flags & TF_ATOMIC) ||
     sym_attrs(c, d->a)->cleanup || sym_attrs(c, d->a)->asmname ||
     QUNQ(s->ty) != QUNQ(N(d->b)->ty)) return false;
 if (r->op == N_RETURN) {
  expr = r->a;
 } else if (r->op == N_DECL && r->a != d->a) {
  const sym *out = &c->syms[r->a];
  if (!is_local_var(c, r->a) || out->kind != S_VAR || out->sc == SC_STATIC || out->sc == SC_EXTERN ||
      (out->flags & SF_ADDR_TAKEN) || (QQ(out->ty) & Q_VOLATILE) || (T(c, out->ty)->flags & TF_ATOMIC) ||
      sym_attrs(c, r->a)->cleanup || sym_attrs(c, r->a)->asmname || !r->b ||
      QUNQ(out->ty) != QUNQ(N(r->b)->ty)) return false;
  expr = r->b;
 } else return false;
 if (!expr || N(expr)->op != N_BINARY) return false;
 return em_decl_reuse_match(c, d->b, expr, d->a, from, to);
}

/* A declaration of a new, nonobservable scalar with a pure initializer cannot
 * change an earlier local value. Used only while scanning a short block suffix. */
static __attribute__((noinline)) bool em_value_pure_decl(ecx *e, uint32_t n)
{
 cc *c = e->c;
 const node *d = N(n);
 const sym *y;
 em_sibling_state s = {0};
 if (d->op != N_DECL) return false;
 y = &c->syms[d->a];
 if (!is_local_var(c, d->a) || y->kind != S_VAR || y->sc == SC_STATIC || y->sc == SC_EXTERN ||
     (y->flags & SF_ADDR_TAKEN) || (QQ(y->ty) & Q_VOLATILE) || (T(c, y->ty)->flags & TF_ATOMIC) ||
     sym_attrs(c, d->a)->cleanup || sym_attrs(c, d->a)->asmname || !is_integer(c, y->ty) ||
     em_bitint(c, y->ty) || (tsize(c, y->ty) != 4 && tsize(c, y->ty) != 8)) return false;
 if (!d->b) return true;
 if (QUNQ(y->ty) != QUNQ(N(d->b)->ty) || c->nsym >= UINT32_MAX - 32) return false;
 s.next = c->nsym + 1; s.nodes = 32;
 em_values_reset(&s.table, 64);
 return em_sibling_value(c, &s, d->b) != 0;
}

/* Scan at most eight immediately preceding pure local declarations. Any
 * other statement is a barrier; no facts cross a branch or side effect. */
VPRIV bool em_block_value_reuse(ecx *e, const node *block, uint32_t first, uint32_t i,
                                   uint32_t *from, uint32_t *to)
{
 cc *c = e->c;
 uint32_t consumer, count = 0;
 if (!e->E->o->opt || e->E->noopt || i <= first) return false;
 consumer = c->lists[block->b + i];
 if (N(consumer)->op != N_RETURN && N(consumer)->op != N_DECL) return false;
 while (i > first && count++ < 8) {
  uint32_t prev = c->lists[block->b + --i];
  if (N(prev)->op != N_DECL) break;
  if (em_decl_reuse_values(e, prev, consumer, from, to)) return true;
  if (!em_value_pure_decl(e, prev)) break;
 }
 return false;
}
