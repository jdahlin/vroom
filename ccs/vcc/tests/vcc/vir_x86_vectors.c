/* NaN-aware scalar and packed comparisons, selects, SSE2 multiply and signed conversions. */
#define NI __attribute__((noinline))
#define LOOP(name,op) NI void name(float *o,const float *a,const float *b,const float *c,const float *d,int n) \
 {for(int i=0;i<n;i++){float x=c[i],y=d[i];o[i]=a[i] op b[i]?x:y;}}
LOOP(eq,==) LOOP(ne,!=) LOOP(lt,<) LOOP(le,<=) LOOP(gt,>) LOOP(ge,>=)
NI int compare(double a,double b)
{return (a==b)+2*(a!=b)+4*(a<b)+8*(a<=b)+16*(a>b)+32*(a>=b)+64*__builtin_isunordered(a,b);}
NI void mul(unsigned *o,const unsigned *a,const unsigned *b,int n)
{for(int i=0;i<n;i++)o[i]=a[i]*b[i];}
NI void cvt(float *o,const int *a,int n)
{for(int i=0;i<n;i++)o[i]=(float)a[i];}
NI void cvt64(double *o,const long *a,int n)
{for(int i=0;i<n;i++)o[i]=(double)a[i];}
NI void negate(double *o,const double *a,int n)
{for(int i=0;i<n;i++)o[i]=-a[i];}
int main(void)
{
	union {unsigned u;float f;} nan={0x7fc12345u},inf={0x7f800000u};
	float a[40],b[40],c[40],d[40],o[40];unsigned ia[40],ib[40],io[40];int si[40];long sl[40];double dd[40],od[40];
	for(int i=0;i<40;i++){
		a[i]=i%5==0?nan.f:i%5==1?inf.f:i%5==2?-0.f:(float)i;
		b[i]=i%7==0?nan.f:i%7==1?inf.f:i%7==2?0.f:(float)(40-i);
		c[i]=i+1;d[i]=-i-1;ia[i]=0xf1234567u*i;ib[i]=0x87654321u-i;si[i]=i*1234567-20000000;
		sl[i]=(long)si[i]*100000003L;dd[i]=i*1.25;
	}
	for(int n=0;n<36;n++){
#define CHECK(name,op,err) name(o+1,a+1,b+1,c+1,d+1,n);for(int i=1;i<=n;i++)if(o[i]!=(a[i] op b[i]?c[i]:d[i]))return err;
		CHECK(eq,==,1) CHECK(ne,!=,2) CHECK(lt,<,3) CHECK(le,<=,4) CHECK(gt,>,5) CHECK(ge,>=,6)
		mul(io+1,ia+1,ib+1,n);for(int i=1;i<=n;i++)if(io[i]!=ia[i]*ib[i])return 7;
		cvt(o+1,si+1,n);for(int i=1;i<=n;i++)if(o[i]!=(float)si[i])return 8;
		cvt64(od+1,sl+1,n);for(int i=1;i<=n;i++)if(od[i]!=(double)sl[i])return 9;
		negate(od+1,dd+1,n);for(int i=1;i<=n;i++)if(od[i]!=-dd[i])return 10;
	}
	if(compare(nan.f,1)!=66||compare(1,nan.f)!=66||compare(0,-0.)!=41||compare(1,2)!=14||compare(2,1)!=50)return 11;
	return 0;
}
