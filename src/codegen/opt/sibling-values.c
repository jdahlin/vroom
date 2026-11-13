/*
 * sibling-values.c: bounded value numbering of pure expressions, to find an operation whose value an
 * operand or a nearby earlier statement already holds.
 *
 * em_sibling_values(e, n) asks it of the operands of one expression; em_block_value_reuse(e, block,
 * first, i, &from, &to) asks, for statement i of a block, whether a pure expression in it repeats the
 * value of an earlier local (reported as the node `from` that can be replaced by the symbol node
 * `to`); em_decl_reuse_values is its declaration case.  The value table is the fixed 32-entry one of
 * flow/values.c.  Everything is bounded and fails closed: at most eight preceding declarations or
 * pure local assignments are scanned, with a node budget; an effect, a write to a dependency, an
 * unknown form or a spent budget means "no reuse".  Only plain automatic integers whose address is
 * never taken take part.  -O1 and up.  Called by the shared walker and by arm64's and wasm's
 * emitters.
 */
#include "../codegen-internal.h"

typedef struct em_sibling_state {
 em_value_table table;
 uint32_t next, nodes;
 uint32_t target, match, symbol, symnode;
 const uint32_t *killed;
 uint32_t nkilled;
 bool check_killed;
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
 if (!codegen_type_is_integer(c, x->ty) || codegen_type_is_bitint(c, x->ty) ||
     (codegen_type_size(c, x->ty) != 4 && codegen_type_size(c, x->ty) != 8)) return 0;
 if (x->op == AST_NODE_VARIABLE) {
  if (s->check_killed)
   for (uint32_t i = 0; i < s->nkilled; i++)
    if (s->killed[i] == x->a) return 0;
  sym = &c->syms[x->a];
  if (!codegen_symbol_is_local_variable(c, x->a) || sym->kind != AST_SYMBOL_VARIABLE || sym->sc == AST_STORAGE_STATIC || sym->sc == AST_STORAGE_EXTERN ||
      (sym->flags & AST_SYMBOL_FLAG_ADDRESS_TAKEN) || (AST_QUALIFIED_TYPE_QUALIFIERS(sym->ty) & AST_QUALIFIER_VOLATILE) ||
      (T(c, sym->ty)->flags & AST_TYPE_FLAG_ATOMIC) || codegen_symbol_attributes(c, x->a)->asmname) return 0;
  if (s->target && x->a == s->symbol) s->symnode = n;
  return x->a + 1;
 }
 key.type = AST_QUALIFIED_TYPE_UNQUALIFIED(x->ty);
 if (x->op == AST_NODE_NUMBER) {
  key.operation = 1;
  key.immediate = (uint64_t)codegen_integer_truncate(c, c->ipool[x->a], x->ty);
 } else if (x->op == AST_NODE_CAST && (x->aux == AST_CAST_INTEGER || x->aux == AST_CAST_NOP || x->aux == AST_CAST_BOOL)) {
  left = em_sibling_value(c, s, x->a);
  if (!left) return 0;
  key.operation = UINT32_C(0x80000000) | x->aux;
  key.left = left;
 } else if (x->op == AST_NODE_BINARY && (x->aux == LEX_TOKEN_PLUS || x->aux == LEX_TOKEN_MINUS || x->aux == LEX_TOKEN_STAR ||
                                  x->aux == LEX_TOKEN_AMPERSAND || x->aux == LEX_TOKEN_PIPE || x->aux == LEX_TOKEN_CARET)) {
  left = em_sibling_value(c, s, x->a);
  right = em_sibling_value(c, s, x->b);
  if (!left || !right) return 0;
  if (x->aux != LEX_TOKEN_MINUS && left > right) { value = left; left = right; right = value; }
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
  if (x->op == AST_NODE_BINARY) s->match = n;
  return s->target;
 }
 if (value) {
  if (s->target && value == s->target && x->op == AST_NODE_BINARY) s->match = n;
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

VPRIV bool em_sibling_values(codegen_context *e, uint32_t n)
{
 cc *c = e->c;
 const node *x = N(n), *a, *b;
 if (!e->E->o->opt || e->E->noopt || x->op != AST_NODE_BINARY ||
     (x->aux != LEX_TOKEN_PLUS && x->aux != LEX_TOKEN_MINUS && x->aux != LEX_TOKEN_STAR &&
      x->aux != LEX_TOKEN_AMPERSAND && x->aux != LEX_TOKEN_PIPE && x->aux != LEX_TOKEN_CARET)) return false;
 a = N(x->a); b = N(x->b);
 if (a->op != AST_NODE_BINARY || b->op != AST_NODE_BINARY || AST_QUALIFIED_TYPE_UNQUALIFIED(a->ty) != AST_QUALIFIED_TYPE_UNQUALIFIED(x->ty) ||
     AST_QUALIFIED_TYPE_UNQUALIFIED(b->ty) != AST_QUALIFIED_TYPE_UNQUALIFIED(x->ty) || c->nsym >= UINT32_MAX - 32) return false;
 return em_sibling_match(c, x->a, x->b);
}

/* Reuse an ordinary initialized local in a later pure expression. The
 * consumer must already reference that local, so unused-init elimination
 * cannot remove its definition. The value-numbering scan fails closed. */
static __attribute__((noinline)) bool em_decl_reuse_match(cc *c, uint32_t init, uint32_t ret,
                                                            uint32_t symbol, const uint32_t *killed, uint32_t nkilled,
                                                            uint32_t *from, uint32_t *to)
{
 em_sibling_state s = {0};
 uint32_t result;
 s.next = c->nsym + 1; s.nodes = 32; s.symbol = symbol;
 s.killed = killed; s.nkilled = nkilled; s.check_killed = true;
 em_values_reset(&s.table, 64);
 s.target = em_sibling_value(c, &s, init);
 if (!s.target) return false;
 s.target_key = s.last_key;
 s.check_killed = false;
 result = em_sibling_value(c, &s, ret);
 if (!result || !s.match || !s.symnode) return false;
 *from = s.match; *to = s.symnode;
 return true;
}

VPRIV bool em_decl_reuse_values(codegen_context *e, uint32_t decl, uint32_t consumer,
                                const uint32_t *killed, uint32_t nkilled, uint32_t *from, uint32_t *to)
{
 cc *c = e->c;
 const node *d, *r;
 const sym *s;
 uint32_t expr;
 if (!e->E->o->opt || e->E->noopt || !decl || !consumer) return false;
 d = N(decl); r = N(consumer);
 if (d->op != AST_NODE_DECLARATION || !d->b || N(d->b)->op != AST_NODE_BINARY || c->nsym >= UINT32_MAX - 32) return false;
 s = &c->syms[d->a];
 for (uint32_t i = 0; i < nkilled; i++) if (killed[i] == d->a) return false;
 if (!codegen_symbol_is_local_variable(c, d->a) || s->kind != AST_SYMBOL_VARIABLE || s->sc == AST_STORAGE_STATIC || s->sc == AST_STORAGE_EXTERN ||
     (s->flags & AST_SYMBOL_FLAG_ADDRESS_TAKEN) || (AST_QUALIFIED_TYPE_QUALIFIERS(s->ty) & AST_QUALIFIER_VOLATILE) || (T(c, s->ty)->flags & AST_TYPE_FLAG_ATOMIC) ||
     codegen_symbol_attributes(c, d->a)->cleanup || codegen_symbol_attributes(c, d->a)->asmname ||
     AST_QUALIFIED_TYPE_UNQUALIFIED(s->ty) != AST_QUALIFIED_TYPE_UNQUALIFIED(N(d->b)->ty)) return false;
 if (r->op == AST_NODE_RETURN) {
  expr = r->a;
 } else if (r->op == AST_NODE_DECLARATION && r->a != d->a) {
  const sym *out = &c->syms[r->a];
  if (!codegen_symbol_is_local_variable(c, r->a) || out->kind != AST_SYMBOL_VARIABLE || out->sc == AST_STORAGE_STATIC || out->sc == AST_STORAGE_EXTERN ||
      (out->flags & AST_SYMBOL_FLAG_ADDRESS_TAKEN) || (AST_QUALIFIED_TYPE_QUALIFIERS(out->ty) & AST_QUALIFIER_VOLATILE) || (T(c, out->ty)->flags & AST_TYPE_FLAG_ATOMIC) ||
      codegen_symbol_attributes(c, r->a)->cleanup || codegen_symbol_attributes(c, r->a)->asmname || !r->b ||
      AST_QUALIFIED_TYPE_UNQUALIFIED(out->ty) != AST_QUALIFIED_TYPE_UNQUALIFIED(N(r->b)->ty)) return false;
  expr = r->b;
 } else if (r->op == AST_NODE_EXPRESSION && r->a) {
  const node *assign = N(r->a), *lhs;
  const sym *out;
  if (assign->op != AST_NODE_ASSIGN || !assign->a || !assign->b ||
      (assign->aux && assign->aux != LEX_TOKEN_PLUS && assign->aux != LEX_TOKEN_MINUS && assign->aux != LEX_TOKEN_STAR &&
       assign->aux != LEX_TOKEN_AMPERSAND && assign->aux != LEX_TOKEN_PIPE && assign->aux != LEX_TOKEN_CARET)) return false;
  lhs = N(assign->a);
  if (lhs->op != AST_NODE_VARIABLE) return false;
  out = &c->syms[lhs->a];
  if (!codegen_symbol_is_local_variable(c, lhs->a) || out->kind != AST_SYMBOL_VARIABLE || out->sc == AST_STORAGE_STATIC || out->sc == AST_STORAGE_EXTERN ||
      (out->flags & AST_SYMBOL_FLAG_ADDRESS_TAKEN) || (AST_QUALIFIED_TYPE_QUALIFIERS(out->ty) & AST_QUALIFIER_VOLATILE) || (T(c, out->ty)->flags & AST_TYPE_FLAG_ATOMIC) ||
      codegen_symbol_attributes(c, lhs->a)->asmname || AST_QUALIFIED_TYPE_UNQUALIFIED(out->ty) != AST_QUALIFIED_TYPE_UNQUALIFIED(N(assign->b)->ty)) return false;
  expr = assign->b;
 } else return false;
 if (!expr || N(expr)->op != AST_NODE_BINARY) return false;
 /* Common two-variable expressions need no table when an operand was written. */
 if (nkilled) {
  const node *init = N(d->b), *a = N(init->a), *b = N(init->b);
  for (uint32_t i = 0; i < nkilled; i++)
   if ((a->op == AST_NODE_VARIABLE && a->a == killed[i]) || (b->op == AST_NODE_VARIABLE && b->a == killed[i])) return false;
 }
 return em_decl_reuse_match(c, d->b, expr, d->a, killed, nkilled, from, to);
}

/* A declaration of a new, nonobservable scalar with a pure initializer cannot
 * change an earlier local value. Used only while scanning a short block suffix. */
static __attribute__((noinline)) bool em_value_pure_decl(codegen_context *e, uint32_t n)
{
 cc *c = e->c;
 const node *d = N(n);
 const sym *y;
 em_sibling_state s = {0};
 if (d->op != AST_NODE_DECLARATION) return false;
 y = &c->syms[d->a];
 if (!codegen_symbol_is_local_variable(c, d->a) || y->kind != AST_SYMBOL_VARIABLE || y->sc == AST_STORAGE_STATIC || y->sc == AST_STORAGE_EXTERN ||
     (y->flags & AST_SYMBOL_FLAG_ADDRESS_TAKEN) || (AST_QUALIFIED_TYPE_QUALIFIERS(y->ty) & AST_QUALIFIER_VOLATILE) || (T(c, y->ty)->flags & AST_TYPE_FLAG_ATOMIC) ||
     codegen_symbol_attributes(c, d->a)->cleanup || codegen_symbol_attributes(c, d->a)->asmname || !codegen_type_is_integer(c, y->ty) ||
     codegen_type_is_bitint(c, y->ty) || (codegen_type_size(c, y->ty) != 4 && codegen_type_size(c, y->ty) != 8)) return false;
 if (!d->b) return true;
 if (AST_QUALIFIED_TYPE_UNQUALIFIED(y->ty) != AST_QUALIFIED_TYPE_UNQUALIFIED(N(d->b)->ty) || c->nsym >= UINT32_MAX - 32) return false;
 s.next = c->nsym + 1; s.nodes = 32;
 em_values_reset(&s.table, 64);
 return em_sibling_value(c, &s, d->b) != 0;
}

/* Cheap preflight: identify a possible ordinary local write without walking
 * its RHS. The RHS is proved only if a later candidate survives invalidation. */
static bool em_value_local_write(codegen_context *e, uint32_t n, uint32_t *written)
{
 cc *c = e->c;
 const node *stmt = N(n), *a, *lhs;
 const sym *y;
 bool increment;
 if (stmt->op != AST_NODE_EXPRESSION || !stmt->a) return false;
 a = N(stmt->a);
 increment = a->op == AST_NODE_PRE_INCREMENT || a->op == AST_NODE_PRE_DECREMENT || a->op == AST_NODE_POST_INCREMENT || a->op == AST_NODE_POST_DECREMENT;
 if (!increment && (a->op != AST_NODE_ASSIGN || !a->b ||
     (a->aux && a->aux != LEX_TOKEN_PLUS && a->aux != LEX_TOKEN_MINUS && a->aux != LEX_TOKEN_STAR &&
      a->aux != LEX_TOKEN_AMPERSAND && a->aux != LEX_TOKEN_PIPE && a->aux != LEX_TOKEN_CARET))) return false;
 if (!a->a) return false;
 lhs = N(a->a);
 if (lhs->op != AST_NODE_VARIABLE) return false;
 y = &c->syms[lhs->a];
 if (!codegen_symbol_is_local_variable(c, lhs->a) || y->kind != AST_SYMBOL_VARIABLE || y->sc == AST_STORAGE_STATIC || y->sc == AST_STORAGE_EXTERN ||
     (y->flags & AST_SYMBOL_FLAG_ADDRESS_TAKEN) || (AST_QUALIFIED_TYPE_QUALIFIERS(y->ty) & AST_QUALIFIER_VOLATILE) || (T(c, y->ty)->flags & AST_TYPE_FLAG_ATOMIC) ||
     codegen_symbol_attributes(c, lhs->a)->asmname || !codegen_type_is_integer(c, y->ty) || codegen_type_is_bitint(c, y->ty) ||
     (codegen_type_size(c, y->ty) != 4 && codegen_type_size(c, y->ty) != 8) || c->nsym >= UINT32_MAX - 32 ||
     (!increment && AST_QUALIFIED_TYPE_UNQUALIFIED(y->ty) != AST_QUALIFIED_TYPE_UNQUALIFIED(N(a->b)->ty))) return false;
 *written = lhs->a;
 return true;
}

/* Only a surviving candidate pays for the bounded purity proof. */
static __attribute__((noinline)) bool em_value_local_assignment(codegen_context *e, uint32_t n)
{
 cc *c = e->c;
 const node *a = N(N(n)->a);
 em_sibling_state s = {0};
 if (a->op != AST_NODE_ASSIGN) return true; /* preflight accepted an increment */
 s.next = c->nsym + 1; s.nodes = 32;
 em_values_reset(&s.table, 64);
 return em_sibling_value(c, &s, a->b) != 0;
}

/* Scan at most eight preceding declarations or pure local assignments.
 * A written dependency invalidates a candidate; all other effects stop. */
VPRIV bool em_block_value_reuse(codegen_context *e, const node *block, uint32_t first, uint32_t i,
                                   uint32_t *from, uint32_t *to)
{
 cc *c = e->c;
 uint32_t consumer, count = 0, killed[8], writes[8], nkilled = 0;
 if (!e->E->o->opt || e->E->noopt || i <= first) return false;
 consumer = c->lists[block->b + i];
 if (N(consumer)->op == AST_NODE_EXPRESSION) {
  const node *stmt = N(consumer), *assign, *rhs;
  if (!stmt->a || N(stmt->a)->op != AST_NODE_ASSIGN) return false;
  assign = N(stmt->a);
  if (!assign->b || N(assign->b)->op != AST_NODE_BINARY) return false;
  rhs = N(assign->b);
  if (N(rhs->a)->op != AST_NODE_BINARY && N(rhs->b)->op != AST_NODE_BINARY) return false;
 } else if (N(consumer)->op != AST_NODE_RETURN && N(consumer)->op != AST_NODE_DECLARATION) return false;
 while (i > first && count++ < 8) {
  uint32_t prev = c->lists[block->b + --i];
  if (N(prev)->op == AST_NODE_EXPRESSION) {
   uint32_t written;
   if (!em_value_local_write(e, prev, &written)) break;
   writes[nkilled] = prev;
   killed[nkilled++] = written;
   continue;
  }
  if (N(prev)->op != AST_NODE_DECLARATION) break;
  if (em_decl_reuse_values(e, prev, consumer, killed, nkilled, from, to)) {
   for (uint32_t j = 0; j < nkilled; j++)
    if (!em_value_local_assignment(e, writes[j])) return false;
   return true;
  }
  if (!em_value_pure_decl(e, prev)) break;
 }
 return false;
}
