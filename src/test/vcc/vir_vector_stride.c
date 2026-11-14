/* vcc-flags: -O3 */
/* The loop vectorizer (-O3): scalar gathers/scatters must touch only their selected elements. Ordered sums must round at every addition. */
#define NI __attribute__((noinline))
NI void stride(float *o,const float *a,int n) {for(long i=0;i<n;i++)o[3*i]=a[2*i]*2.f;}
NI void intstride(float *o,const float *a,int n) {for(int i=0;i<n;i++)o[2*i]=a[i]*2.f;}
NI void wide(long *o,const long *a,int n) {for(long i=0;i<n;i++)o[3*i]=a[2*i]+1;}
NI void reverse(double *o,const double *a,int n) {for(long i=0;i<n;i++)o[-3*i]=a[-2*i]+1.;}
NI float ordered(const float *a,int n) {float s=0;for(long i=0;i<n;i++)s+=a[i]*2.f;return s;}
NI float scalar(const float *a,int n) {volatile float s=0;for(long i=0;i<n;i++)s=s+a[i]*2.f;return s;}
NI void residue(float *a,int n) {for(long i=0;i<n;i++)a[2*i+1]=a[2*i]+1.f;}
static float ga[128],gb[128];
NI void gather(const int *idx,int n) {for(long i=0;i<n;i++)ga[i]=gb[idx[i]]*2.f;}
NI void carry(float *o,const float *a,int n) {float prev=7;for(int i=0;i<n;i++){float x=a[i]*2.f;o[i]=x+prev;prev=x;}}
NI float chain(const float *a,const float *b,int n) {float s=0;for(int i=0;i<n;i++){s+=a[i];s+=b[i];}return s;}
NI float maxval(const float *a,int n) {float s=-1.e30f;for(int i=0;i<n;i++)if(a[i]>s)s=a[i];return s;}
NI int maxindex(const float *a,int n) {float s=-1.e30f;int idx=-1;for(int i=0;i<n;i++)if(a[i]>s){s=a[i];idx=i;}return idx;}
NI int lastnegative(const float *a,int n) {int idx=-1;for(int i=0;i<n;i++)if(a[i]<0)idx=i;return idx;}
int main(void) {
 float a[128],o[128],r[128]; int idx[128]; long la[128],lo[128]; double d[128],q[128];
 for(int n=0;n<35;n++) {
  for(int i=0;i<128;i++){a[i]=i%3==0?1.e20f:i%3==1?-1.e20f:1.f;o[i]=-77;d[i]=i;q[i]=-77;}
  stride(o,a,n);
  for(int i=0;i<128;i++)if(o[i]!=(i%3==0&&i/3<n?a[2*(i/3)]*2.f:-77.f))return 1;
  for(int i=0;i<128;i++)o[i]=-77;
  intstride(o,a,n);
  for(int i=0;i<128;i++)if(o[i]!=(i%2==0&&i/2<n?a[i/2]*2.f:-77.f))return 11;
  reverse(q+127,d+127,n);
  for(int i=0;i<128;i++)if(q[i]!=((127-i)%3==0&&(127-i)/3<n?d[127-2*((127-i)/3)]+1.:-77.))return 2;
  if(ordered(a,n)!=scalar(a,n))return 3;
  for(int i=0;i<128;i++){idx[i]=(i*17)%128;gb[i]=i;ga[i]=-1;}
  gather(idx,n);for(int i=0;i<n;i++)if(ga[i]!=gb[idx[i]]*2.f)return 5;
  carry(o,a,n);float prev=7;for(int i=0;i<n;i++){float x=a[i]*2.f;if(o[i]!=x+prev)return 6;prev=x;}
  volatile float sum=0;float mx=-1.e30f;for(int i=0;i<n;i++){sum=sum+a[i];sum=sum+gb[i];if(a[i]>mx)mx=a[i];}
  if(chain(a,gb,n)!=sum||maxval(a,n)!=mx)return 7;
  float mm=-1.e30f;int mi=-1,ln=-1;for(int i=0;i<n;i++){if(a[i]>mm){mm=a[i];mi=i;}if(a[i]<0)ln=i;}
  if(maxindex(a,n)!=mi||lastnegative(a,n)!=ln)return 8;
  for(int i=0;i<128;i++){a[i]=r[i]=i;la[i]=((long)i<<40)+i;lo[i]=-77;}
  wide(lo,la,n);for(int i=0;i<128;i++)if(lo[i]!=(i%3==0&&i/3<n?la[2*(i/3)]+1:-77))return 9;
  volatile float *vr=r;for(long i=0;i<n;i++)vr[3*i+1]=vr[2*i]*2.f;
  stride(a+1,a,n);for(int i=0;i<128;i++)if(a[i]!=r[i])return 10;
  residue(a,n);for(long i=0;i<n;i++)if(a[2*i+1]!=a[2*i]+1.f)return 4;
  for(int i=0;i<128;i++)a[i]=r[i]=i;
  for(int i=0;i<n;i++)vr[2*i+1]=vr[i]*2.f;
  intstride(a+1,a,n);for(int i=0;i<128;i++)if(a[i]!=r[i])return 12;
 }
 return 0;
}
