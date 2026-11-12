/* x86 VIR leaf lowering: fixed cl, stack parameters, edge moves and SSE2 loops.
 * bench/vcc-vir-x86.py also requires these functions to take the VIR path. */
#define NI __attribute__((noinline))
NI unsigned vx_shift(unsigned x, unsigned n) { return (x << n) ^ (x >> (n ^ 31)); }
NI long vx_stack(long a,long b,long c,long d,long e,long f,long g,long h)
{ return a + b*3 + c*5 + d*7 + e*9 + f*11 + g*13 + h*17; }
NI double vx_fp(double a,double b,double c,double d,double e,double f,double g,double h,double i,double j)
{ return (a-b) * (c+d) + (e-f) * (g+h) + i-j; }
NI long vx_rotate(long a,long b,long c,int n)
{ for(int i=0;i<n;i++){ long t=a; a=b; b=c; c=t; } return a+3*b+7*c; }
NI void vx_f32(float *a,const float *b,const float *c,long n)
{ for(long i=0;i<n;i++) a[i] = b[i]*c[i] + 1.5f; }
NI void vx_f64(double *a,const double *b,const double *c,long n)
{ for(long i=0;i<n;i++) a[i] = b[i]-c[i]*2.0; }
NI void vx_i32(int *a,const int *b,const int *c,long n)
{ for(long i=0;i<n;i++) a[i] = (b[i]+c[i]) ^ 123; }
NI long vx_sum(const long *a,long n)
{ long s=7; for(long i=0;i<n;i++) s+=a[i]; return s; }
NI void vx_series(long *a,long n)
{ for(long i=0;i<n;i++) a[i]=i+3; }
NI long vx_division(long a,long b) { return a/b; }
NI long vx_srem(long a,long b,long c) {return (a/b)*3+(a%b)*7+c;}
NI unsigned long vx_udiv64(unsigned long a,unsigned long b,unsigned long c) {return a/b+(a%b)*7+c;}
NI unsigned vx_udiv32(unsigned a,unsigned b,unsigned c) {return a/b+a%b+c;}
int main(void)
{
	float a[80],b[80],c[80]; double da[80],db[80],dc[80]; int ia[80],ib[80],ic[80]; long la[80];
	for(int i=0;i<80;i++){b[i]=i*.5f; c[i]=i-20; db[i]=i*.25; dc[i]=i-30; ib[i]=i*37-100; ic[i]=i^11;la[i]=i*1000000003L;}
	for(unsigned n=0;n<32;n++)for(unsigned x=0;x<257;x+=13)
		if(vx_shift(x,n)!=((x<<n)^(x>>(n^31))))return 1;
	if(vx_stack(-1,2,-3,4,-5,6,-7,8)!=(-1+6-15+28-45+66-91+136))return 2;
	if(vx_fp(1,2,3,4,5,6,7,8,9,10)!=-23)return 3;
	for(int n=0;n<20;n++){
		long x=2,y=5,z=9;for(int i=0;i<n;i++){long t=x;x=y;y=z;z=t;}
		if(vx_rotate(2,5,9,n)!=x+3*y+7*z)return 4;
	}
	for(int n=0;n<65;n++){
		for(int i=0;i<80;i++){a[i]=-777;da[i]=-777;ia[i]=-777;}
		vx_f32(a+1,b+1,c+1,n);vx_f64(da+1,db+1,dc+1,n);vx_i32(ia+1,ib+1,ic+1,n);
		for(int i=1;i<=n;i++){
			if(a[i]!=b[i]*c[i]+1.5f)return 5;
			if(da[i]!=db[i]-dc[i]*2.0)return 6;
			if(ia[i]!=((ib[i]+ic[i])^123))return 7;
		}
		if(a[0]!=-777||a[n+1]!=-777||da[n+1]!=-777||ia[n+1]!=-777)return 8;
		long s=7;for(int i=0;i<n;i++)s+=la[i];if(vx_sum(la,n)!=s)return 9;
	}
	vx_series(la,65);for(int i=0;i<65;i++)if(la[i]!=i+3)return 10;
	/* Runtime alias checks must send overlapping iterations to the scalar loop. */
	for(int i=0;i<80;i++)a[i]=i;
	vx_f32(a+1,a,c,63);
	float prev=0;for(int i=0;i<63;i++){prev=prev*c[i]+1.5f;if(a[i+1]!=prev)return 11;}
	if(vx_division(-19,3)!=-6)return 12;
	if(vx_srem(-19,3,100)!=75)return 13;
	if(vx_udiv64(~0UL,65537,91)!=(~0UL/65537+(~0UL%65537)*7+91))return 14;
	if(vx_udiv32(0xf1234567u,7,19)!=(0xf1234567u/7+0xf1234567u%7+19))return 15;
	return 0;
}
