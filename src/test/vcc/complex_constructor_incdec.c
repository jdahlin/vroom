/* vcc-flags: -std=c2y -pedantic-errors */
static int nr,ni,np;
static double real(void){nr++;return -0.0;}
static double imag(void){ni++;return -0.0;}
static volatile _Complex double values[2];
static volatile _Complex double *ptr(void){np++;return &values[0];}
int main(void){
 _Complex double z=__builtin_complex(real(),imag());
 if(nr!=1||ni!=1||!__builtin_signbit(__real__ z)||!__builtin_signbit(__imag__ z))return 1;
 double extracted=__real__ __builtin_complex(real(),imag());
 if(nr!=2||ni!=2||!__builtin_signbit(extracted))return 7;
 extracted=__imag__ __builtin_complex(real(),imag());
 if(nr!=3||ni!=3||!__builtin_signbit(extracted))return 8;
 values[0]=z;
 _Complex double old=(*ptr())++;
 if(np!=1||!__builtin_signbit(__real__ old)||!__builtin_signbit(__imag__ old))return 2;
 if(__real__ values[0]!=1||!__builtin_signbit(__imag__ values[0]))return 3;
 _Complex double now=--(*ptr());
 if(np!=2||__real__ now!=0||!__builtin_signbit(__imag__ now))return 4;
 values[0]=__builtin_complex(3.0,__builtin_nans("0x123"));
 old=values[0]++;
 union {double f;unsigned long long u;} a,b;
 a.f=__imag__ old;b.f=__imag__ values[0];
 if(a.u!=0x7ff0000000000123ull||a.u!=b.u||__real__ values[0]!=4)return 5;
 register _Complex double r=__builtin_complex(1.0,-0.0);
 old=r--;now=++r;
 if(__real__ old!=1||__real__ now!=1||!__builtin_signbit(__imag__ now))return 6;
 return 0;
}
