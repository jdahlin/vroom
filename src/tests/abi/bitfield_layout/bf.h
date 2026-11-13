/* Records whose layout differs between Apple arm64 and Linux (unnamed
   bit-fields leave the alignment on Apple), aligned() bit-fields and
   packed bit-fields that straddle bytes: each half fills and reads the
   other's objects, passed by value and by pointer. */
struct u0 { char a; int :0; char b; };			/* Apple 5/1, Linux 8/4 */
struct u3 { char a; long :7; short s; };		/* Apple 4/2, Linux 8/8 */
struct al { char a; __attribute__((aligned(8))) int :3; char b; int c:5; };
struct __attribute__((packed)) pk { int :12; unsigned long w:63; unsigned long :10; int m; char z; };
struct __attribute__((packed)) pk2 { char q; long s:63; short t:9; char z; };
union un { char a; int :5; };

struct u0 mk_u0(int a, int b);
struct u3 mk_u3(int a, int s);
void fill_al(struct al *p, int a, int b, int c);
void fill_pk(struct pk *p, unsigned long w, int m, int z);
struct pk2 mk_pk2(long s, int t, int z);
long sum_all(struct u0 x, struct u3 y, struct al z, struct pk w, struct pk2 v, union un u);
int sizes(void);
