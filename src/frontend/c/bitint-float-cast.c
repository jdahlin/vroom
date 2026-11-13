/* Wide integer/floating runtime casts. Build target-independent integer
 * operations, retaining guard/sticky bits until one destination rounding.
 * No floating intermediate or runtime helper can discard high integer limbs. */
#include "ccimpl.h"

typedef struct { cc *c; uint32_t loc; vec body; } bfc;
static uint32_t bfi(bfc *b, i128 v) { return mk_int(b->c,v,prim(TY_INT),b->loc); }
static uint32_t bfu(bfc *b, u128 v) { return mk_int(b->c,(i128)v,prim(TY_UINT128),b->loc); }
static uint32_t bfr(bfc *b,uint32_t v) { return rvalue(b->c,v); }
static uint32_t bfo(bfc *b,int op,uint32_t a,uint32_t d) { return binop(b->c,op,bfr(b,a),bfr(b,d),b->loc); }
static uint32_t bft(bfc *b,uint32_t a,uint32_t y,uint32_t n) { return conditional(b->c,bfr(b,a),bfr(b,y),bfr(b,n),b->loc); }
static void bfs(bfc *b,uint32_t n) { vpush(b->c,&b->body,mkstmt(b->c,N_EXPR,b->loc,n,0,0)); }
static uint32_t bfl(bfc *b,qty t,uint32_t init) {
 cc*c=b->c;uint32_t s=sym_new(c,S_VAR,0,t,b->loc);c->syms[s].sc=SC_AUTO;
 uint32_t n=mk(c,N_VAR,t,b->loc);N(n)->a=s;N(n)->flags=NF_LVALUE;
 vpush(c,&b->body,mk2(c,N_DECL,prim(TY_VOID),b->loc,s,init));return n;
}
static void bfa(bfc *b,uint32_t v,uint32_t x) { bfs(b,assign(b->c,0,v,x,b->loc)); }
static uint32_t bfcvt(bfc*b,uint32_t v,qty t) { return convert(b->c,bfr(b,v),t); }
static uint32_t bfview(bfc*b,uint32_t v,qty t) {
 cc*c=b->c;uint32_t a=mk(c,N_ADDR,ty_ptr(c,N(v)->ty),b->loc);N(a)->a=v;
 return deref(c,cast_node(c,a,ty_ptr(c,t),CK_NOP),b->loc);
}
static uint32_t bfdone(bfc*b,uint32_t result,qty t) {
 cc*c=b->c;bfs(b,bfr(b,result));uint32_t block=mkstmt(c,N_BLOCK,b->loc,0,vfreeze(c,&b->body),b->body.n);
 uint32_t n=mk(c,N_STMTEXPR,t,b->loc);N(n)->a=block;return n;
}

VPRIV uint32_t big_float_cast(cc*c,uint32_t input,qty to)
{
 qty from=QUNQ(N(input)->ty),fp=is_float(c,to)?to:from;
 bool into=is_float(c,to),x87=KIND(c,fp)==TY_LDOUBLE&&c->o->target==PP_LINUX_X86_64;
 int k=KIND(c,fp),p=k==TY_FLOAT16?10:k==TY_BFLOAT16?7:k==TY_FLOAT?23:k==TY_DOUBLE?52:x87?63:112;
 int bias=k==TY_FLOAT16?15:k==TY_BFLOAT16||k==TY_FLOAT?127:k==TY_DOUBLE?1023:16383;
 int eb=k==TY_FLOAT16?5:k==TY_BFLOAT16||k==TY_FLOAT?8:k==TY_DOUBLE?11:15;
 unsigned bits=int_bits(c,into?from:to);
 qty ut=ty_bitint(c,bits,true),rawty=ty_size(c,fp)==2?prim(TY_USHORT):ty_size(c,fp)==4?prim(TY_UINT):ty_size(c,fp)==8?prim(TY_ULLONG):prim(TY_UINT128);
 bfc b={.c=c,.loc=N(input)->loc};vinit(&b.body);
 uint32_t saved=bfl(&b,from,input);
 if(into) {
  uint32_t neg=bfl(&b,prim(TY_INT),is_signed(c,from)?bfo(&b,LEX_TOKEN_LESS,saved,mk_int(c,0,from,b.loc)):bfi(&b,0));
  uint32_t unsigned_value=bfcvt(&b,saved,ut);
  uint32_t magnitude=bfl(&b,ut,bft(&b,neg,bfo(&b,LEX_TOKEN_MINUS,mk_int(c,0,ut,b.loc),unsigned_value),unsigned_value));
  /* Count at the declared precision; signed padding is removed by the
   * unsigned magnitude conversion. The zero fallback gives length zero. */
  uint32_t count=big_bitcount(c,BI_CLZG,bfr(&b,magnitude),bfi(&b,bits),bits,b.loc);
  uint32_t length=bfl(&b,prim(TY_INT),bfo(&b,LEX_TOKEN_MINUS,bfi(&b,bits),count));
  /* Keep the leading significand and two rounding bits. Any lower limb
   * contributes to sticky; no floating intermediate can double-round. */
  uint32_t shift=bfl(&b,prim(TY_INT),bft(&b,bfo(&b,LEX_TOKEN_GREATER,length,bfi(&b,p+3)),bfo(&b,LEX_TOKEN_MINUS,length,bfi(&b,p+3)),bfi(&b,0)));
  uint32_t sig=bfl(&b,prim(TY_UINT128),bfcvt(&b,bfo(&b,LEX_TOKEN_GREATER_GREATER,magnitude,shift),prim(TY_UINT128)));
  uint32_t sticky=bfo(&b,LEX_TOKEN_EXCLAMATION_EQUAL,magnitude,bfo(&b,LEX_TOKEN_LESS_LESS,bfcvt(&b,sig,ut),shift));
  bfa(&b,sig,bfo(&b,LEX_TOKEN_PIPE,sig,bfcvt(&b,sticky,prim(TY_UINT128))));
  bfa(&b,sig,bft(&b,bfo(&b,LEX_TOKEN_LESS,length,bfi(&b,p+3)),bfo(&b,LEX_TOKEN_LESS_LESS,sig,bfo(&b,LEX_TOKEN_MINUS,bfi(&b,p+3),length)),sig));
  uint32_t guard=bfl(&b,prim(TY_INT),bfcvt(&b,bfo(&b,LEX_TOKEN_AMPERSAND,sig,bfu(&b,3)),prim(TY_INT)));
  bfa(&b,sig,bfo(&b,LEX_TOKEN_GREATER_GREATER,sig,bfi(&b,2)));
  uint32_t up=bfo(&b,LEX_TOKEN_PIPE,bfo(&b,LEX_TOKEN_GREATER,guard,bfi(&b,2)),bfo(&b,LEX_TOKEN_AMPERSAND,bfo(&b,LEX_TOKEN_EQUAL_EQUAL,guard,bfi(&b,2)),bfcvt(&b,bfo(&b,LEX_TOKEN_AMPERSAND,sig,bfu(&b,1)),prim(TY_INT))));
  bfa(&b,sig,bfo(&b,LEX_TOKEN_PLUS,sig,bfcvt(&b,up,prim(TY_UINT128))));
  /* Rounding may carry into a new exponent, including overflow to infinity. */
  uint32_t carry=bfl(&b,prim(TY_INT),bfcvt(&b,bfo(&b,LEX_TOKEN_GREATER_GREATER,sig,bfi(&b,p+1)),prim(TY_INT)));
  bfa(&b,sig,bfo(&b,LEX_TOKEN_GREATER_GREATER,sig,carry));
  uint32_t exp=bfl(&b,prim(TY_INT),bfo(&b,LEX_TOKEN_PLUS,bfo(&b,LEX_TOKEN_PLUS,length,bfi(&b,bias-1)),carry));
  uint32_t overflow=bfl(&b,prim(TY_INT),bfo(&b,LEX_TOKEN_GREATER_EQUAL,exp,bfi(&b,2*bias+1)));
  bfa(&b,exp,bft(&b,bfo(&b,LEX_TOKEN_EQUAL_EQUAL,length,bfi(&b,0)),bfi(&b,0),bft(&b,overflow,bfi(&b,2*bias+1),exp)));
  bfa(&b,sig,bft(&b,overflow,bfu(&b,x87?(u128)1<<63:0),sig));
  uint32_t fraction=x87?sig:bfo(&b,LEX_TOKEN_AMPERSAND,sig,bfu(&b,((u128)1<<p)-1));
  uint32_t raw=bfo(&b,LEX_TOKEN_PIPE,fraction,bfo(&b,LEX_TOKEN_LESS_LESS,bfcvt(&b,exp,prim(TY_UINT128)),bfi(&b,x87?64:p)));
  raw=bfo(&b,LEX_TOKEN_PIPE,raw,bfo(&b,LEX_TOKEN_LESS_LESS,bfcvt(&b,neg,prim(TY_UINT128)),bfi(&b,x87?79:p+eb)));
  uint32_t storage=bfl(&b,rawty,bfcvt(&b,raw,rawty));
  return bfdone(&b,bfview(&b,storage,to),to);
 }
 /* Interpret the actual target format: x87 has an explicit integer bit,
  * while every IEEE format has an implicit one for normal values. */
 uint32_t raw=bfl(&b,prim(TY_UINT128),bfcvt(&b,bfview(&b,saved,rawty),prim(TY_UINT128)));
 uint32_t neg=bfl(&b,prim(TY_INT),bfcvt(&b,bfo(&b,LEX_TOKEN_AMPERSAND,bfo(&b,LEX_TOKEN_GREATER_GREATER,raw,bfi(&b,x87?79:p+eb)),bfu(&b,1)),prim(TY_INT)));
 uint32_t encoded=bfl(&b,prim(TY_INT),bfcvt(&b,bfo(&b,LEX_TOKEN_AMPERSAND,bfo(&b,LEX_TOKEN_GREATER_GREATER,raw,bfi(&b,x87?64:p)),bfu(&b,2*bias+1)),prim(TY_INT)));
 uint32_t exp=bfl(&b,prim(TY_INT),bfo(&b,LEX_TOKEN_MINUS,encoded,bfi(&b,bias)));
 uint32_t sig=bfl(&b,prim(TY_UINT128),bfo(&b,LEX_TOKEN_AMPERSAND,raw,bfu(&b,((u128)1<<(x87?64:p))-1)));
 if(!x87)bfa(&b,sig,bfo(&b,LEX_TOKEN_PIPE,sig,bfu(&b,(u128)1<<p)));
 uint32_t value=bfl(&b,ut,mk_int(c,0,ut,b.loc));
 /* A conditional keeps every generated shift bounded. Invalid conversions
  * have no C value requirement; return zero, without allocating by exponent. */
 uint32_t valid=bfo(&b,LEX_TOKEN_AMPERSAND,bfo(&b,LEX_TOKEN_GREATER_EQUAL,exp,bfi(&b,0)),bfo(&b,LEX_TOKEN_LESS,exp,bfi(&b,bits)));
 valid=bfo(&b,LEX_TOKEN_AMPERSAND,valid,bfo(&b,LEX_TOKEN_EXCLAMATION_EQUAL,encoded,bfi(&b,2*bias+1)));
 uint32_t converted=bft(&b,bfo(&b,LEX_TOKEN_GREATER_EQUAL,exp,bfi(&b,p)),
  bfo(&b,LEX_TOKEN_LESS_LESS,bfcvt(&b,sig,ut),bfo(&b,LEX_TOKEN_MINUS,exp,bfi(&b,p))),
  bfcvt(&b,bfo(&b,LEX_TOKEN_GREATER_GREATER,sig,bfo(&b,LEX_TOKEN_MINUS,bfi(&b,p),exp)),ut));
 bfa(&b,value,bft(&b,valid,converted,mk_int(c,0,ut,b.loc)));
 bfa(&b,value,bft(&b,neg,bfo(&b,LEX_TOKEN_MINUS,mk_int(c,0,ut,b.loc),value),value));
 return bfdone(&b,bfcvt(&b,value,to),to);
}
