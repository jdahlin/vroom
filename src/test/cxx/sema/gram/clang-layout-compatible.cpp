// __is_layout_compatible ([basic.types]/11, [class.mem]/26): cv-qualified operands, enumerations, arrays, pointers, void, standard-layout classes with
// bit-fields and [[no_unique_address]]; every value is clang++'s.  (Unions and classes with bases are not followed yet.)
#define Y(a,b) static_assert(__is_layout_compatible(a,b),#a "," #b)
#define N(a,b) static_assert(!__is_layout_compatible(a,b),#a "," #b)
enum E1:int{}; enum E2:int{}; enum E3:long{}; enum class E4:int{}; enum E5{e5};
Y(int,const int); N(int,unsigned); N(int,long); Y(E1,E2); N(E1,E3); Y(E1,E4); N(E1,int); N(int,E1);
Y(int[3],const volatile int[3]); N(int[3],int[4]); N(int[3],int[]); Y(int[],int[]); N(int,int[1]);
Y(int*,int*); N(int*,char*);Y(void,void); Y(const void, volatile void);N(void,int);
struct A{int a; char b;}; struct B{int x; char y;}; struct C{int a; char b; int c;}; struct D{int a; long b;};
struct F{int a:3; char b;}; struct G{int a:4; char b;}; struct H2{int a:3; char b;};
struct V{virtual void f();}; struct W{virtual void f();};
struct Base{int a;}; struct Der:Base{int b;}; struct Flat{int a; int b;};
struct Em1{}; struct Em2{}; Y(Em1,Em2);
struct NU{ int a; [[no_unique_address]] Em1 e; }; struct NU2{int a; Em1 e;}; N(NU,NU2);
N(Der,Flat);
Y(A,B); N(A,C); N(A,D); Y(A,const A); N(F,G); Y(F,H2); N(V,W);
struct AlA { int i; char c; };
struct AlB { int i; alignas(8) char c; };
N(AlA,AlB);
