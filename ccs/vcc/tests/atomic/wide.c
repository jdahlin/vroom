/* Hosted Linux regression: vpp -std=c2y -S, target C driver -pthread -latomic.
 * Run natively or under qemu-aarch64 -L /usr/aarch64-linux-gnu.
 * Whole-object snapshots and CAS must agree with other libatomic callers. */
typedef unsigned long pthread_t;
extern int pthread_create(pthread_t *, const void *, void *(*)(void *), void *);
extern int pthread_join(pthread_t, void **);
struct triple { unsigned v[3]; };
struct block { unsigned long v[8]; };
static _Atomic(struct triple) padded;
static _Atomic(struct block) shared;
static _Atomic(_Complex double) complex_count;
static _Atomic(_Complex long double) long_count;
static _Atomic(unsigned __int128) count;
static int bad, addresses, operands;
static _Atomic(unsigned __int128) *address(void) { addresses++;return &count; }
static unsigned __int128 operand(void) { operands++;return 3; }
static void *writer(void *arg) {
 unsigned long tag=(unsigned long)arg;
 for (unsigned long i=0;i<10000;i++) {
  struct block b={{i+tag,~(i+tag),i+tag,~(i+tag),i+tag,~(i+tag),i+tag,~(i+tag)}};
  shared=b;
  complex_count++; ++long_count; count+=1;
 }
 return 0;
}
static void *reader(void *arg) {
 for (int i=0;i<20000;i++) {
  struct block b=shared;
  for(int j=1;j<8;j++)if(b.v[j]!=(j&1?~b.v[0]:b.v[0]))__atomic_store_n(&bad,1,0);
 }
 return 0;
}
int main(void) {
 *address() += operand();
 if(addresses!=1||operands!=1||count!=3)return 20;
 (*address())++; if(addresses!=2||count!=4)return 21; count=0;
 _Atomic(_Complex double) sn=__builtin_complex(1.,__builtin_nans("0x123"));
 sn++; _Complex double sncopy=sn;
 union {double f;unsigned long u;} bits;bits.f=__imag__ sncopy;
 if(__real__ sncopy!=2||bits.u!=0x7ff0000000000123ul)return 22;
 struct triple p={{1,2,3}}; padded=p;
 struct triple q=padded;
 struct triple want={{9,8,7}};
 if(__c11_atomic_compare_exchange_strong(&padded,&want,p,5,5)||want.v[2]!=3)return 23;
 if(!__c11_atomic_compare_exchange_strong(&padded,&want,p,5,5))return 24;
 __c11_atomic_store(&shared, (struct block){{0,~0ul,0,~0ul,0,~0ul,0,~0ul}},5);
 struct block loaded=__c11_atomic_load(&shared,5);if(loaded.v[1]!=~0ul)return 25;
 if(q.v[0]!=1||q.v[1]!=2||q.v[2]!=3)return 1;
 struct block a={{0,~0ul,0,~0ul,0,~0ul,0,~0ul}}, b={{1,~1ul,1,~1ul,1,~1ul,1,~1ul}}, out;
 shared=a;
 __atomic_exchange(&a,&b,&out,5); if(out.v[0]||a.v[0]!=1)return 2;
 if(__atomic_compare_exchange(&a,&out,&b,0,5,5)||out.v[0]!=1)return 3;
 if(!__atomic_compare_exchange(&a,&out,&b,0,5,5))return 4;
 unsigned __int128 u=3;
 if(__atomic_exchange_n(&u,7,5)!=3||__atomic_load_n(&u,5)!=7)return 5;
 if(__atomic_fetch_add(&u,2,0)!=7||u!=9)return 12;
 if(__atomic_sub_fetch(&u,2,0)!=7)return 13;
 if(__atomic_fetch_or(&u,8,0)!=7||u!=15)return 14;
 if(__atomic_and_fetch(&u,7,0)!=7)return 15;
 if(__atomic_fetch_xor(&u,1,0)!=7||u!=6)return 16;
 if(__atomic_nand_fetch(&u,3,0)!=(unsigned __int128)-3)return 17;
 __atomic_store_n(&u,7,5);
 unsigned __int128 expected=0;
 if(__atomic_compare_exchange_n(&u,&expected,11,0,5,5)||expected!=7)return 6;
 if(!__atomic_compare_exchange_n(&u,&expected,11,0,5,5)||u!=11)return 7;
 complex_count=__builtin_complex(0.,-0.);long_count=__builtin_complex(0.L,-0.L);
 if(__sync_val_compare_and_swap(&u,0,1)!=11||u!=11)return 18;
 if(!__sync_bool_compare_and_swap(&u,11,12)||u!=12)return 19;
 pthread_t t[4];
 if(pthread_create(t,0,writer,(void*)1)||pthread_create(t+1,0,writer,(void*)2)||
    pthread_create(t+2,0,reader,0)||pthread_create(t+3,0,reader,0))return 8;
 for(int i=0;i<4;i++)if(pthread_join(t[i],0))return 9;
 _Complex double z=complex_count;_Complex long double l=long_count;
 if(bad||count!=20000||__real__ z!=20000||__real__ l!=20000)return 10;
 if(!__builtin_signbit(__imag__ z)||!__builtin_signbit(__imag__ l))return 11;
 return 0;
}
