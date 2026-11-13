/* Wide integer/floating runtime casts. Build target-independent integer
 * operations, retaining guard/sticky bits until one destination rounding.
 * No floating intermediate or runtime helper can discard high integer limbs. */
#include "cc-internal.h"

typedef struct { cc *c; uint32_t loc; cc_id_list body; } cc_bitint_cast_builder;
static uint32_t cc_bitint_cast_int_node(cc_bitint_cast_builder *b, i128 v) { return cc_node_integer_new(b->c,v,cc_type_basic(AST_TYPE_INT),b->loc); }
static uint32_t cc_bitint_cast_u128_node(cc_bitint_cast_builder *b, u128 v) { return cc_node_integer_new(b->c,(i128)v,cc_type_basic(AST_TYPE_UINT128),b->loc); }
static uint32_t cc_bitint_cast_rvalue(cc_bitint_cast_builder *b,uint32_t v) { return cc_node_rvalue(b->c,v); }
static uint32_t cc_bitint_cast_binary(cc_bitint_cast_builder *b,int op,uint32_t a,uint32_t d) { return cc_binary_build(b->c,op,cc_bitint_cast_rvalue(b,a),cc_bitint_cast_rvalue(b,d),b->loc); }
static uint32_t cc_bitint_cast_conditional(cc_bitint_cast_builder *b,uint32_t a,uint32_t y,uint32_t n) { return cc_conditional_build(b->c,cc_bitint_cast_rvalue(b,a),cc_bitint_cast_rvalue(b,y),cc_bitint_cast_rvalue(b,n),b->loc); }
static void cc_bitint_cast_statement_append(cc_bitint_cast_builder *b,uint32_t n) { cc_id_list_append(b->c,&b->body,cc_statement_new(b->c,AST_NODE_EXPRESSION,b->loc,n,0,0)); }
static uint32_t cc_bitint_cast_local_new(cc_bitint_cast_builder *b,ast_qualified_type t,uint32_t init) {
 cc*c=b->c;uint32_t s=cc_symbol_new(c,AST_SYMBOL_VARIABLE,0,t,b->loc);c->syms[s].sc=AST_STORAGE_AUTO;
 uint32_t n=cc_node_new(c,AST_NODE_VARIABLE,t,b->loc);AST_NODE(n)->a=s;AST_NODE(n)->flags=AST_NODE_FLAG_LVALUE;
 cc_id_list_append(c,&b->body,cc_node_new_operands(c,AST_NODE_DECLARATION,cc_type_basic(AST_TYPE_VOID),b->loc,s,init));return n;
}
static void cc_bitint_cast_assignment_append(cc_bitint_cast_builder *b,uint32_t v,uint32_t x) { cc_bitint_cast_statement_append(b,cc_assignment_build(b->c,0,v,x,b->loc)); }
static uint32_t cc_bitint_cast_convert(cc_bitint_cast_builder*b,uint32_t v,ast_qualified_type t) { return cc_conversion_apply(b->c,cc_bitint_cast_rvalue(b,v),t); }
static uint32_t cc_bitint_cast_view(cc_bitint_cast_builder*b,uint32_t v,ast_qualified_type t) {
 cc*c=b->c;uint32_t a=cc_node_new(c,AST_NODE_ADDRESS,cc_type_pointer(c,AST_NODE(v)->ty),b->loc);AST_NODE(a)->a=v;
 return cc_dereference_build(c,cc_cast_insert(c,a,cc_type_pointer(c,t),AST_CAST_NOP),b->loc);
}
static uint32_t cc_bitint_cast_finish(cc_bitint_cast_builder*b,uint32_t result,ast_qualified_type t) {
 cc*c=b->c;cc_bitint_cast_statement_append(b,cc_bitint_cast_rvalue(b,result));uint32_t block=cc_statement_new(c,AST_NODE_BLOCK,b->loc,0,cc_id_list_freeze(c,&b->body),b->body.n);
 uint32_t n=cc_node_new(c,AST_NODE_STATEMENT_EXPRESSION,t,b->loc);AST_NODE(n)->a=block;return n;
}

CC_PRIVATE uint32_t cc_bitint_float_cast_build(cc*c,uint32_t input,ast_qualified_type to)
{
 ast_qualified_type from=AST_QUALIFIED_TYPE_UNQUALIFIED(AST_NODE(input)->ty),fp=cc_type_is_float(c,to)?to:from;
 bool into=cc_type_is_float(c,to),x87=AST_TYPE_KIND(c,fp)==AST_TYPE_LONG_DOUBLE&&c->o->target==PP_LINUX_X86_64;
 int k=AST_TYPE_KIND(c,fp),p=k==AST_TYPE_FLOAT16?10:k==AST_TYPE_BFLOAT16?7:k==AST_TYPE_FLOAT?23:k==AST_TYPE_DOUBLE?52:x87?63:112;
 int bias=k==AST_TYPE_FLOAT16?15:k==AST_TYPE_BFLOAT16||k==AST_TYPE_FLOAT?127:k==AST_TYPE_DOUBLE?1023:16383;
 int eb=k==AST_TYPE_FLOAT16?5:k==AST_TYPE_BFLOAT16||k==AST_TYPE_FLOAT?8:k==AST_TYPE_DOUBLE?11:15;
 unsigned bits=cc_type_integer_bits(c,into?from:to);
 ast_qualified_type ut=cc_type_bitint(c,bits,true),rawty=cc_type_size(c,fp)==2?cc_type_basic(AST_TYPE_UNSIGNED_SHORT):cc_type_size(c,fp)==4?cc_type_basic(AST_TYPE_UNSIGNED_INT):cc_type_size(c,fp)==8?cc_type_basic(AST_TYPE_UNSIGNED_LONG_LONG):cc_type_basic(AST_TYPE_UINT128);
 cc_bitint_cast_builder b={.c=c,.loc=AST_NODE(input)->loc};cc_id_list_init(&b.body);
 uint32_t saved=cc_bitint_cast_local_new(&b,from,input);
 if(into) {
  uint32_t neg=cc_bitint_cast_local_new(&b,cc_type_basic(AST_TYPE_INT),cc_type_is_signed(c,from)?cc_bitint_cast_binary(&b,LEX_TOKEN_LESS,saved,cc_node_integer_new(c,0,from,b.loc)):cc_bitint_cast_int_node(&b,0));
  uint32_t unsigned_value=cc_bitint_cast_convert(&b,saved,ut);
  uint32_t magnitude=cc_bitint_cast_local_new(&b,ut,cc_bitint_cast_conditional(&b,neg,cc_bitint_cast_binary(&b,LEX_TOKEN_MINUS,cc_node_integer_new(c,0,ut,b.loc),unsigned_value),unsigned_value));
  /* Count at the declared precision; signed padding is removed by the
   * unsigned magnitude conversion. The zero fallback gives length zero. */
  uint32_t count=cc_builtin_bit_count_bitint(c,CC_BUILTIN_CLZG,cc_bitint_cast_rvalue(&b,magnitude),cc_bitint_cast_int_node(&b,bits),bits,b.loc);
  uint32_t length=cc_bitint_cast_local_new(&b,cc_type_basic(AST_TYPE_INT),cc_bitint_cast_binary(&b,LEX_TOKEN_MINUS,cc_bitint_cast_int_node(&b,bits),count));
  /* Keep the leading significand and two rounding bits. Any lower limb
   * contributes to sticky; no floating intermediate can double-round. */
  uint32_t shift=cc_bitint_cast_local_new(&b,cc_type_basic(AST_TYPE_INT),cc_bitint_cast_conditional(&b,cc_bitint_cast_binary(&b,LEX_TOKEN_GREATER,length,cc_bitint_cast_int_node(&b,p+3)),cc_bitint_cast_binary(&b,LEX_TOKEN_MINUS,length,cc_bitint_cast_int_node(&b,p+3)),cc_bitint_cast_int_node(&b,0)));
  uint32_t sig=cc_bitint_cast_local_new(&b,cc_type_basic(AST_TYPE_UINT128),cc_bitint_cast_convert(&b,cc_bitint_cast_binary(&b,LEX_TOKEN_GREATER_GREATER,magnitude,shift),cc_type_basic(AST_TYPE_UINT128)));
  uint32_t sticky=cc_bitint_cast_binary(&b,LEX_TOKEN_EXCLAMATION_EQUAL,magnitude,cc_bitint_cast_binary(&b,LEX_TOKEN_LESS_LESS,cc_bitint_cast_convert(&b,sig,ut),shift));
  cc_bitint_cast_assignment_append(&b,sig,cc_bitint_cast_binary(&b,LEX_TOKEN_PIPE,sig,cc_bitint_cast_convert(&b,sticky,cc_type_basic(AST_TYPE_UINT128))));
  cc_bitint_cast_assignment_append(&b,sig,cc_bitint_cast_conditional(&b,cc_bitint_cast_binary(&b,LEX_TOKEN_LESS,length,cc_bitint_cast_int_node(&b,p+3)),cc_bitint_cast_binary(&b,LEX_TOKEN_LESS_LESS,sig,cc_bitint_cast_binary(&b,LEX_TOKEN_MINUS,cc_bitint_cast_int_node(&b,p+3),length)),sig));
  uint32_t guard=cc_bitint_cast_local_new(&b,cc_type_basic(AST_TYPE_INT),cc_bitint_cast_convert(&b,cc_bitint_cast_binary(&b,LEX_TOKEN_AMPERSAND,sig,cc_bitint_cast_u128_node(&b,3)),cc_type_basic(AST_TYPE_INT)));
  cc_bitint_cast_assignment_append(&b,sig,cc_bitint_cast_binary(&b,LEX_TOKEN_GREATER_GREATER,sig,cc_bitint_cast_int_node(&b,2)));
  uint32_t up=cc_bitint_cast_binary(&b,LEX_TOKEN_PIPE,cc_bitint_cast_binary(&b,LEX_TOKEN_GREATER,guard,cc_bitint_cast_int_node(&b,2)),cc_bitint_cast_binary(&b,LEX_TOKEN_AMPERSAND,cc_bitint_cast_binary(&b,LEX_TOKEN_EQUAL_EQUAL,guard,cc_bitint_cast_int_node(&b,2)),cc_bitint_cast_convert(&b,cc_bitint_cast_binary(&b,LEX_TOKEN_AMPERSAND,sig,cc_bitint_cast_u128_node(&b,1)),cc_type_basic(AST_TYPE_INT))));
  cc_bitint_cast_assignment_append(&b,sig,cc_bitint_cast_binary(&b,LEX_TOKEN_PLUS,sig,cc_bitint_cast_convert(&b,up,cc_type_basic(AST_TYPE_UINT128))));
  /* Rounding may carry into a new exponent, including overflow to infinity. */
  uint32_t carry=cc_bitint_cast_local_new(&b,cc_type_basic(AST_TYPE_INT),cc_bitint_cast_convert(&b,cc_bitint_cast_binary(&b,LEX_TOKEN_GREATER_GREATER,sig,cc_bitint_cast_int_node(&b,p+1)),cc_type_basic(AST_TYPE_INT)));
  cc_bitint_cast_assignment_append(&b,sig,cc_bitint_cast_binary(&b,LEX_TOKEN_GREATER_GREATER,sig,carry));
  uint32_t exp=cc_bitint_cast_local_new(&b,cc_type_basic(AST_TYPE_INT),cc_bitint_cast_binary(&b,LEX_TOKEN_PLUS,cc_bitint_cast_binary(&b,LEX_TOKEN_PLUS,length,cc_bitint_cast_int_node(&b,bias-1)),carry));
  uint32_t overflow=cc_bitint_cast_local_new(&b,cc_type_basic(AST_TYPE_INT),cc_bitint_cast_binary(&b,LEX_TOKEN_GREATER_EQUAL,exp,cc_bitint_cast_int_node(&b,2*bias+1)));
  cc_bitint_cast_assignment_append(&b,exp,cc_bitint_cast_conditional(&b,cc_bitint_cast_binary(&b,LEX_TOKEN_EQUAL_EQUAL,length,cc_bitint_cast_int_node(&b,0)),cc_bitint_cast_int_node(&b,0),cc_bitint_cast_conditional(&b,overflow,cc_bitint_cast_int_node(&b,2*bias+1),exp)));
  cc_bitint_cast_assignment_append(&b,sig,cc_bitint_cast_conditional(&b,overflow,cc_bitint_cast_u128_node(&b,x87?(u128)1<<63:0),sig));
  uint32_t fraction=x87?sig:cc_bitint_cast_binary(&b,LEX_TOKEN_AMPERSAND,sig,cc_bitint_cast_u128_node(&b,((u128)1<<p)-1));
  uint32_t raw=cc_bitint_cast_binary(&b,LEX_TOKEN_PIPE,fraction,cc_bitint_cast_binary(&b,LEX_TOKEN_LESS_LESS,cc_bitint_cast_convert(&b,exp,cc_type_basic(AST_TYPE_UINT128)),cc_bitint_cast_int_node(&b,x87?64:p)));
  raw=cc_bitint_cast_binary(&b,LEX_TOKEN_PIPE,raw,cc_bitint_cast_binary(&b,LEX_TOKEN_LESS_LESS,cc_bitint_cast_convert(&b,neg,cc_type_basic(AST_TYPE_UINT128)),cc_bitint_cast_int_node(&b,x87?79:p+eb)));
  uint32_t storage=cc_bitint_cast_local_new(&b,rawty,cc_bitint_cast_convert(&b,raw,rawty));
  return cc_bitint_cast_finish(&b,cc_bitint_cast_view(&b,storage,to),to);
 }
 /* Interpret the actual target format: x87 has an explicit integer bit,
  * while every IEEE format has an implicit one for normal values. */
 uint32_t raw=cc_bitint_cast_local_new(&b,cc_type_basic(AST_TYPE_UINT128),cc_bitint_cast_convert(&b,cc_bitint_cast_view(&b,saved,rawty),cc_type_basic(AST_TYPE_UINT128)));
 uint32_t neg=cc_bitint_cast_local_new(&b,cc_type_basic(AST_TYPE_INT),cc_bitint_cast_convert(&b,cc_bitint_cast_binary(&b,LEX_TOKEN_AMPERSAND,cc_bitint_cast_binary(&b,LEX_TOKEN_GREATER_GREATER,raw,cc_bitint_cast_int_node(&b,x87?79:p+eb)),cc_bitint_cast_u128_node(&b,1)),cc_type_basic(AST_TYPE_INT)));
 uint32_t encoded=cc_bitint_cast_local_new(&b,cc_type_basic(AST_TYPE_INT),cc_bitint_cast_convert(&b,cc_bitint_cast_binary(&b,LEX_TOKEN_AMPERSAND,cc_bitint_cast_binary(&b,LEX_TOKEN_GREATER_GREATER,raw,cc_bitint_cast_int_node(&b,x87?64:p)),cc_bitint_cast_u128_node(&b,2*bias+1)),cc_type_basic(AST_TYPE_INT)));
 uint32_t exp=cc_bitint_cast_local_new(&b,cc_type_basic(AST_TYPE_INT),cc_bitint_cast_binary(&b,LEX_TOKEN_MINUS,encoded,cc_bitint_cast_int_node(&b,bias)));
 uint32_t sig=cc_bitint_cast_local_new(&b,cc_type_basic(AST_TYPE_UINT128),cc_bitint_cast_binary(&b,LEX_TOKEN_AMPERSAND,raw,cc_bitint_cast_u128_node(&b,((u128)1<<(x87?64:p))-1)));
 if(!x87)cc_bitint_cast_assignment_append(&b,sig,cc_bitint_cast_binary(&b,LEX_TOKEN_PIPE,sig,cc_bitint_cast_u128_node(&b,(u128)1<<p)));
 uint32_t value=cc_bitint_cast_local_new(&b,ut,cc_node_integer_new(c,0,ut,b.loc));
 /* A conditional keeps every generated shift bounded. Invalid conversions
  * have no C value requirement; return zero, without allocating by exponent. */
 uint32_t valid=cc_bitint_cast_binary(&b,LEX_TOKEN_AMPERSAND,cc_bitint_cast_binary(&b,LEX_TOKEN_GREATER_EQUAL,exp,cc_bitint_cast_int_node(&b,0)),cc_bitint_cast_binary(&b,LEX_TOKEN_LESS,exp,cc_bitint_cast_int_node(&b,bits)));
 valid=cc_bitint_cast_binary(&b,LEX_TOKEN_AMPERSAND,valid,cc_bitint_cast_binary(&b,LEX_TOKEN_EXCLAMATION_EQUAL,encoded,cc_bitint_cast_int_node(&b,2*bias+1)));
 uint32_t converted=cc_bitint_cast_conditional(&b,cc_bitint_cast_binary(&b,LEX_TOKEN_GREATER_EQUAL,exp,cc_bitint_cast_int_node(&b,p)),
  cc_bitint_cast_binary(&b,LEX_TOKEN_LESS_LESS,cc_bitint_cast_convert(&b,sig,ut),cc_bitint_cast_binary(&b,LEX_TOKEN_MINUS,exp,cc_bitint_cast_int_node(&b,p))),
  cc_bitint_cast_convert(&b,cc_bitint_cast_binary(&b,LEX_TOKEN_GREATER_GREATER,sig,cc_bitint_cast_binary(&b,LEX_TOKEN_MINUS,cc_bitint_cast_int_node(&b,p),exp)),ut));
 cc_bitint_cast_assignment_append(&b,value,cc_bitint_cast_conditional(&b,valid,converted,cc_node_integer_new(c,0,ut,b.loc)));
 cc_bitint_cast_assignment_append(&b,value,cc_bitint_cast_conditional(&b,neg,cc_bitint_cast_binary(&b,LEX_TOKEN_MINUS,cc_node_integer_new(c,0,ut,b.loc),value),value));
 return cc_bitint_cast_finish(&b,cc_bitint_cast_convert(&b,value,to),to);
}
