/* Wide constant values are constructed only while parsing. Published AST words
 * are immutable, including while emitters concurrently evaluate initializers. */
#include "ccimpl.h"
#include "../../common/bigint.h"

static bool big_valid(cc*c,const cval*v) { return v->kind==CV_BIGINT && v->nbig && v->big<=c->nlist && v->nbig<=c->nlist-v->big; }
VPRIV bool big_nonzero(cc*c,const cval*v) { return big_valid(c,v)&&vbi_nz(c->lists+v->big,v->nbig); }
static uint32_t big_words(cc*c,const uint32_t*a,size_t n) { uint32_t at=c->nlist;memcpy(c->lists+at,a,n*4);c->nlist+=(uint32_t)n;return at; }
VPRIV uint32_t mk_big_words(cc*c,const uint32_t*a,qty t,uint32_t loc) {
 uint32_t n=mk(c,N_BIGNUM,t,loc);N(n)->b=(uint32_t)(ty_size(c,t)/4);N(n)->a=big_words(c,a,N(n)->b);return n;
}
VPRIV uint32_t big_int_node(cc*c,i128 value,qty t,uint32_t loc) {
 size_t n=(size_t)(ty_size(c,t)/4);uint32_t*a=calloc(n,4);if(!a)fatal(c,loc,"out of memory");
 for(size_t i=0;i<n;i++)a[i]=i<4?(uint32_t)((u128)value>>(i*32)):value<0?UINT32_MAX:0;
 vbi_norm(a,n,int_bits(c,t),is_signed(c,t));uint32_t out=mk_big_words(c,a,t,loc);free(a);return out;
}
VPRIV uint32_t big_literal(cc*c,const char*s,size_t len,unsigned base,bool unsign,uint32_t loc) {
 size_t cap=len/8+4;uint32_t*a=calloc(cap,4);if(!a)fatal(c,loc,"out of memory");size_t used=1;
 size_t start=base==16||base==2?2:0;
 for(size_t i=start;i<len;i++){unsigned d;unsigned char ch=(unsigned char)s[i];if(ch=='\'')continue;if(ch>='0'&&ch<='9')d=ch-'0';else if((ch|32)>='a'&&(ch|32)<='f')d=(ch|32)-'a'+10;else break;
  uint64_t carry=d;for(size_t j=0;j<used;j++){carry+=(uint64_t)a[j]*base;a[j]=(uint32_t)carry;carry>>=32;}if(carry)a[used++]=(uint32_t)carry;
 }
 while(used>1&&!a[used-1])used--;unsigned bits=a[used-1]?(unsigned)((used-1)*32+32-__builtin_clz(a[used-1])):1;bits+=!unsign;
 if(bits>8388608){free(a);fatal(c,loc,"bit-precise integer literal exceeds maximum supported width");}
 qty t=ty_bitint(c,bits,unsign);size_t n=(size_t)(ty_size(c,t)/4);if(n>cap){uint32_t*p=realloc(a,n*4);if(!p){free(a);fatal(c,loc,"out of memory");}a=p;memset(a+cap,0,(n-cap)*4);}
 vbi_norm(a,n,bits,!unsign);uint32_t out=mk_big_words(c,a,t,loc);free(a);return out;
}
static void big_operand(cc*c,uint32_t*out,size_t n,const cval*v,qty t) {
 if(v->kind==CV_BIGINT){bool neg=is_signed(c,t)&&((c->lists[v->big+v->nbig-1]>>31)!=0);vbi_copy(out,n,c->lists+v->big,v->nbig,neg);}
 else {bool neg=is_signed(c,t)&&v->i<0;for(size_t i=0;i<n;i++)out[i]=i<4?(uint32_t)((u128)v->i>>(i*32)):neg?UINT32_MAX:0;}
}
static void big_result(cc*c,cval*v,uint32_t*a,size_t n,qty t,bool ice) {
 vbi_norm(a,n,int_bits(c,t),is_signed(c,t));memset(v,0,sizeof(*v));v->kind=CV_BIGINT;v->ice=ice;v->big=big_words(c,a,n);v->nbig=(uint32_t)n;
 for(size_t i=0;i<n&&i<4;i++)v->i=(i128)((u128)v->i|((u128)a[i]<<(32*i)));
}
/* Integer magnitude to floating point, rounded once in the destination
 * format. The low retained bit represents every discarded bit. */
static bool big_to_float(cc *c, const cval *a, qty from, qty to, cval *v)
{
 if (!big_valid(c,a)) return false;
 size_t n=a->nbig; uint32_t *words=malloc(n*4);
 if (!words) return false;
 bool neg=is_signed(c,from)&&(c->lists[a->big+n-1]>>31);
 if(neg)vbi_neg(words,c->lists+a->big,n);else memcpy(words,c->lists+a->big,n*4);
 size_t used=n;while(used&&!words[used-1])used--;
 unsigned bits=used?(unsigned)((used-1)*32+32-__builtin_clz(words[used-1])):0;
 u128 m=0;
 if(bits){
  int shift=(int)bits-116;
  for(unsigned j=0;j<116;j++){int bit=(int)j+shift;if(bit>=0&&(unsigned)bit<bits)m|=(u128)((words[bit/32]>>(bit%32))&1)<<j;}
  for(int bit=0;bit<shift;bit++)if((words[bit/32]>>(bit%32))&1){m|=1;break;}
 }
 free(words);memset(v,0,sizeof(*v));v->kind=CV_FLT;
 if(is_q(c,to)){v->ld=true;v->q=q_rndp(neg,16383+(int)bits-1,m,q_prec(c,to));v->f=q_to_d(v->q);return true;}
 int k=KIND(c,to),p=k==TY_FLOAT?23:k==TY_FLOAT16?10:k==TY_BFLOAT16?7:52;
 int bias=k==TY_FLOAT16?15:k==TY_FLOAT||k==TY_BFLOAT16?127:1023;
 uint64_t raw=(uint64_t)q_round(neg,bias+(int)bits-1-(112-p),m,p,2*bias+1);
 if(k==TY_FLOAT){uint32_t u=(uint32_t)raw;float f;memcpy(&f,&u,4);v->f=f;}
 else if(k==TY_FLOAT16)v->f=h_to_d((uint16_t)raw);
 else if(k==TY_BFLOAT16)v->f=bf_to_d((uint16_t)raw);
 else memcpy(&v->f,&raw,8);
 return true;
}
/* Truncate binary floating input into a wide integer. Undefined conversions
 * use the existing scalar evaluator's saturating convention, but never gain
 * constexpr/ICE provenance. */
static bool big_from_float(uint32_t *out,size_t n,unsigned bits,bool sign,u128 q)
{
 int neg,e;u128 m;q_un(q,&neg,&e,&m);e-=16383;
 bool range=!q_isnan(q)&&!q_isinf(q);
 memset(out,0,n*4);
 if(q_isnan(q)||e<0)return range;
 if(neg&&!sign)range=false;
 if(e>=(int)bits-(sign?1:0)){
  bool minimum=sign&&neg&&e==(int)bits-1&&m==((u128)1<<112);
  if(!minimum)range=false;
 }
 if(range){
  for(unsigned j=0;j<113;j++)if((m>>j)&1){int bit=(int)j+e-112;if(bit>=0&&(unsigned)bit<bits)out[bit/32]|=1u<<(bit%32);}
  if(neg)vbi_neg(out,out,n);
 }else if(!(neg&&!sign)){
  unsigned top=bits-(sign?1:0);for(unsigned bit=0;bit<top;bit++)out[bit/32]|=1u<<(bit%32);
  if(sign&&neg){memset(out,0,n*4);out[(bits-1)/32]=1u<<((bits-1)%32);}
 }
 vbi_norm(out,n,bits,sign);return range;
}

/* Exactness constraints are checked before implicit constexpr conversion. */
VPRIV bool big_conversion_exact(cc *c,const cval *v,qty from,qty to)
{
 if(!big_valid(c,v))return false;
 size_t n=v->nbig;uint32_t *a=malloc(n*4);if(!a)return false;
 bool neg=is_signed(c,from)&&(c->lists[v->big+n-1]>>31);
 if(neg)vbi_neg(a,c->lists+v->big,n);else memcpy(a,c->lists+v->big,n*4);
 size_t used=n;while(used&&!a[used-1])used--;
 unsigned bits=used?(unsigned)((used-1)*32+32-__builtin_clz(a[used-1])):0;
 bool exact=true;
 if(is_integer(c,to)){
  unsigned limit=int_bits(c,to);bool sign=is_signed(c,to);
  if(KIND(c,to)==TY_BOOL)exact=bits<=1;
  else if(neg&&!sign)exact=false;
  else if(sign){
   if(bits>limit-1){exact=neg&&bits==limit;for(unsigned i=0;exact&&i+1<bits;i++)if((a[i/32]>>(i%32))&1)exact=false;}
  }else exact=bits<=limit;
 }else if(is_float(c,to)){
  int k=KIND(c,to);unsigned precision=k==TY_FLOAT16?11:k==TY_BFLOAT16?8:k==TY_FLOAT?24:k==TY_DOUBLE?53:(unsigned)q_prec(c,to)+1;
  unsigned maximum=k==TY_FLOAT16?16:k==TY_BFLOAT16||k==TY_FLOAT?128:k==TY_DOUBLE?1024:16384;
  exact=bits<=maximum;
  for(unsigned i=0;exact&&i+precision<bits;i++)if((a[i/32]>>(i%32))&1)exact=false;
 }
 free(a);return exact;
}
VPRIV char *big_value_string(cc *c,const cval *v,qty from)
{
 if(!big_valid(c,v))return NULL;
 size_t n=v->nbig;uint32_t *a=malloc(n*4);if(!a)return NULL;
 bool neg=is_signed(c,from)&&(c->lists[v->big+n-1]>>31);
 if(neg)vbi_neg(a,c->lists+v->big,n);else memcpy(a,c->lists+v->big,n*4);
 while(n>1&&!a[n-1])n--;
 char *text=malloc(n*8+4);if(!text){free(a);return NULL;}
 char *p=text;if(neg)*p++='-';*p++='0';*p++='x';
 bool started=false;for(size_t i=n*8;i--;){unsigned digit=(a[i/8]>>(4*(i%8)))&15;if(digit||started||!i){*p++="0123456789abcdef"[digit];started=true;}}
 *p=0;free(a);return text;
}

/* Read-only cases: these may run in parallel data emitters. Arithmetic that
 * needs new words is folded by big_fold_node during parser construction. */
VPRIV bool big_read_eval(cc*c,uint32_t n,cval*v) {
 const node*x=N(n);cval a,b;
 if(x->op==N_BIGNUM){if(!x->b||x->a>c->nlist||x->b>c->nlist-x->a)return false;memset(v,0,sizeof(*v));v->kind=CV_BIGINT;v->ice=!(x->aux&NUM_NO_ICE);v->big=x->a;v->nbig=x->b;for(uint32_t i=0;i<x->b&&i<4;i++)v->i=(i128)((u128)v->i|((u128)c->lists[x->a+i]<<(i*32)));return true;}
 if(x->op==N_CAST&&eval(c,x->a,&a)&&a.kind==CV_BIGINT){
  if(x->aux==CK_NOP&&is_bigint(c,x->ty)&&N(x->a)->ty==x->ty){*v=a;return true;}
  if(x->aux==CK_I2F&&is_float(c,x->ty))return big_to_float(c,&a,N(x->a)->ty,x->ty,v);
  if(x->aux==CK_BOOL){*v=(cval){.kind=CV_INT,.ice=a.ice,.i=big_nonzero(c,&a)};return true;}
  if(is_integer(c,x->ty)&&!is_bigint(c,x->ty)){*v=(cval){.kind=CV_INT,.ice=a.ice,.i=trunc_to(c,a.i,x->ty)};return true;}
 }
 if(x->op==N_UNARY&&x->aux==T_EXCLAIM&&eval(c,x->a,&a)&&a.kind==CV_BIGINT){*v=(cval){.kind=CV_INT,.ice=a.ice,.i=!big_nonzero(c,&a)};return true;}
 if(x->op==N_BINARY&&!is_bigint(c,x->ty)&&eval(c,x->a,&a)&&eval(c,x->b,&b)&&a.kind==CV_BIGINT&&b.kind==CV_BIGINT&&a.nbig==b.nbig){
  bool sa=is_signed(c,N(x->a)->ty),na=sa&&(c->lists[a.big+a.nbig-1]>>31),nb=sa&&(c->lists[b.big+b.nbig-1]>>31);int cmp=na!=nb?(na?-1:1):vbi_cmp(c->lists+a.big,c->lists+b.big,a.nbig);i128 out;
  switch(x->aux){case T_EQUALEQUAL:out=cmp==0;break;case T_EXCLAIMEQ:out=cmp!=0;break;case T_LESS:out=cmp<0;break;case T_LESSEQ:out=cmp<=0;break;case T_GREATER:out=cmp>0;break;case T_GREATEREQ:out=cmp>=0;break;default:return false;}
  *v=(cval){.kind=CV_INT,.ice=a.ice&&b.ice,.i=out};return true;
 }
 return false;
}
VPRIV uint32_t big_fold_node(cc*c,uint32_t node_index) {
 const node*x=N(node_index);qty to=x->ty;bool large=is_bigint(c,to);cval a,b,v;
 if(x->op!=N_CAST&&x->op!=N_UNARY&&x->op!=N_BINARY)return node_index;
 if(!large&&!is_bigint(c,N(x->a)->ty)&&(x->op!=N_BINARY||!is_bigint(c,N(x->b)->ty)))return node_index;
 bool grammar=constexpr_init_ok(c,x->a)&&(x->op!=N_BINARY||constexpr_init_ok(c,x->b));
 if(big_read_eval(c,node_index,&v)&&v.kind==CV_INT){uint32_t result=mk_int(c,v.i,to,x->loc);N(result)->aux|=(!v.ice?NUM_NO_ICE:0)|(!grammar?NUM_NO_CONSTEXPR:0);return result;}
 if(!large||!eval(c,x->a,&a)||(a.kind!=CV_INT&&a.kind!=CV_BIGINT&&!(x->op==N_CAST&&a.kind==CV_FLT)))return node_index;
 size_t n=(size_t)(ty_size(c,to)/4);uint32_t*buf=calloc(n*4+2,4);if(!buf)fatal(c,x->loc,"out of memory");uint32_t*l=buf,*r=l+n,*out=r+n,*scratch=out+n;
 if(a.kind!=CV_FLT)big_operand(c,l,n,&a,N(x->a)->ty);bool ok=true,ice=a.ice;
 if(x->op==N_CAST&&a.kind==CV_FLT){bool valid=big_from_float(out,n,int_bits(c,to),is_signed(c,to),a.ld?a.q:q_of_d(a.f));ice=N(x->a)->op==N_FNUM&&valid;grammar&=valid;}
 else if(x->op==N_CAST)memcpy(out,l,n*4);
 else if(x->op==N_UNARY){switch(x->aux){case T_PLUS:memcpy(out,l,n*4);break;case T_MINUS:vbi_neg(out,l,n);break;case T_TILDE:for(size_t i=0;i<n;i++)out[i]=~l[i];break;default:ok=false;}}
 else if(!eval(c,x->b,&b)||(b.kind!=CV_INT&&b.kind!=CV_BIGINT))ok=false;
 else {big_operand(c,r,n,&b,N(x->b)->ty);ice&=b.ice;switch(x->aux){
  case T_PLUS:vbi_add(out,l,r,n);break;case T_MINUS:vbi_sub(out,l,r,n);break;case T_STAR:vbi_mul(out,l,r,n);break;
  case T_AMP:case T_PIPE:case T_CARET:for(size_t i=0;i<n;i++)out[i]=x->aux==T_AMP?l[i]&r[i]:x->aux==T_PIPE?l[i]|r[i]:l[i]^r[i];break;
  case T_LESSLESS:case T_GREATERGREATER:{bool high=false;for(size_t i=2;i<n;i++)high|=r[i]!=0;uint64_t shift=r[0]|(n>1?(uint64_t)r[1]<<32:0);if(high||shift>=int_bits(c,to)){ok=false;break;}vbi_shift(out,l,n,shift,x->aux==T_GREATERGREATER,is_signed(c,to)&&(l[n-1]>>31));break;}
  case T_SLASH:case T_PERCENT:{bool neg=is_signed(c,to)&&(l[n-1]>>31),neg2=is_signed(c,to)&&(r[n-1]>>31);if(!vbi_nz(r,n)){ok=false;break;}if(neg)vbi_neg(l,l,n);if(neg2)vbi_neg(r,r,n);uint32_t*division=calloc(n*2+2,4);if(!division)fatal(c,x->loc,"out of memory");vbi_div(out,division,l,r,n,division+n+1);if(x->aux==T_PERCENT)memcpy(out,division,n*4);if(x->aux==T_SLASH?neg!=neg2:neg)vbi_neg(out,out,n);free(division);break;}
  default:ok=false;
 }}
 bool overflow=false;
 if(ok&&is_signed(c,to)&&x->op!=N_CAST){
  unsigned bits=int_bits(c,to),at=bits-1;bool sa=(l[at/32]>>(at%32))&1,sb=(r[at/32]>>(at%32))&1,so=(out[at/32]>>(at%32))&1;
  if(x->op==N_UNARY&&x->aux==T_MINUS)overflow=sa&&so;
  else if(x->op==N_BINARY&&x->aux==T_PLUS)overflow=sa==sb&&so!=sa;
  else if(x->op==N_BINARY&&x->aux==T_MINUS)overflow=sa!=sb&&so!=sa;
  else if(x->op==N_BINARY&&x->aux==T_STAR){
   uint32_t *full=calloc(n*6,4);if(!full)fatal(c,x->loc,"out of memory");
   vbi_copy(full,n*2,l,n,sa);vbi_copy(full+n*2,n*2,r,n,sb);vbi_mul(full+n*4,full,full+n*2,n*2);
   vbi_copy(full,n*2,out,n,so);vbi_norm(full,n*2,bits,true);overflow=memcmp(full,full+n*4,n*8)!=0;free(full);
  }else if(x->op==N_BINARY&&x->aux==T_SLASH){
   /* Division above uses magnitudes; a positive result with the sign bit
      set is the signed-minimum / -1 overflow. */
   overflow=is_signed(c,N(x->a)->ty)&&a.kind==CV_BIGINT&&(c->lists[a.big+a.nbig-1]>>31)&&b.kind==CV_BIGINT&&(c->lists[b.big+b.nbig-1]>>31)&&so;
  }
 }
 if(overflow){ice=false;grammar=false;}
 (void)scratch;uint32_t result=node_index;if(ok){big_result(c,&v,out,n,to,ice);result=mk(c,N_BIGNUM,to,x->loc);N(result)->a=v.big;N(result)->b=v.nbig;N(result)->aux=(!ice?NUM_NO_ICE:0)|(!grammar?NUM_NO_CONSTEXPR:0);
 if(overflow){N(result)->flags|=NF_OVF;if(!c->dead&&!c->unevaluated&&!(N(x->a)->flags&NF_OVF)&&(x->op!=N_BINARY||!(N(x->b)->flags&NF_OVF))){char *value=big_value_string(c,&v,to);cc_warnx(c,"integer-overflow",true,NULL,x->loc,NULL,0,NULL,0,"overflow in expression; result is %s with type '%s'",value?value:"<wide integer>",tyname(c,to));free(value);}}
 }free(buf);return result;
}
