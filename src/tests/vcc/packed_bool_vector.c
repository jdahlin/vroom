typedef _Bool B __attribute__((ext_vector_type(9)));
typedef int I __attribute__((ext_vector_type(9)));
_Static_assert(sizeof(B)==2 && _Alignof(B)==2,"packed layout");
static B global={1,0,1,0,1,0,1,0,1};
static __attribute__((noinline)) void set(B *p,int i,int x){(*p)[i]=x;}
static __attribute__((noinline)) int get(B *p,int i){return (*p)[i];}
int main(void){
 B a={1,0,1,0,1,0,1,0,1};
 for(int i=0;i<9;i++) if(get(&a,i)!=(i%2==0)||global[i]!=(i%2==0))return 1;
 for(int i=0;i<9;i++)set(&a,i,i%3==0);
 for(int i=0;i<9;i++)if(a[i]!=(i%3==0))return 2;
 B b=~a,c=a|b,d=a&b;
 for(int i=0;i<9;i++)if(c[i]!=1||d[i]!=0)return 3;
 B e=__builtin_convertvector((I){0,2,-2,0,1,0,-1,0,9},B);
 for(int i=0;i<9;i++)if(e[i]!=(i==1||i==2||i==4||i==6||i==8))return 4;
 B splat=3, explicit_int=(B)3, explicit_float=(B)2.5;
 B selected=a?b:splat, neg=!a, less=a<b;
 for(int i=0;i<9;i++) {
  if(explicit_int[i]!=1 || explicit_float[i]!=1 || splat[i]!=1 || selected[i]!=(a[i]?b[i]:1))return 5;
  if(neg[i]==a[i] || less[i]!=(a[i]<b[i]))return 6;
 }
 I back=__builtin_convertvector(e,I);
 for(int i=0;i<9;i++)if(back[i]!=e[i])return 7;
 typedef _Bool B3 __attribute__((ext_vector_type(3)));
 B3 shuffled=__builtin_shufflevector(a,b,8,0,10);
 if(shuffled[0]!=a[8]||shuffled[1]!=a[0]||shuffled[2]!=b[1])return 8;
 return 0;
}
