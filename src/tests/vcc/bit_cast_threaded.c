/* vcc-flags: -std=c23 */
/* vcc-threads-same */
/* Data workers share immutable constant operands, never reader validation state. */
extern void abort(void);
struct Pair { unsigned x,y; };
constexpr struct Pair pair = {0x3f800000u,0x40000000u};
constexpr float scalar = 1.0f;
#define ITEMS(M,P) M(P##0) M(P##1) M(P##2) M(P##3) M(P##4) M(P##5) M(P##6) M(P##7) M(P##8) M(P##9) M(P##a) M(P##b) M(P##c) M(P##d) M(P##e) M(P##f)
#define DATA(N) unsigned a##N=__builtin_bit_cast(unsigned,scalar); unsigned long long b##N=__builtin_bit_cast(unsigned long long,pair);
ITEMS(DATA,p) ITEMS(DATA,q) ITEMS(DATA,r) ITEMS(DATA,s)
#define CHECK(N) if(a##N!=0x3f800000u || b##N!=0x400000003f800000ull) abort();
int main(void) {
  ITEMS(CHECK,p) ITEMS(CHECK,q) ITEMS(CHECK,r) ITEMS(CHECK,s)
  return 0;
}
