/* vcc-flags: -std=c23 */
/* Requires the complete packed boolean vector representation and ABI. */
extern void abort(void);
#define CHECK(x) do { if (!(x)) abort(); } while (0)
typedef _Bool B3 __attribute__((ext_vector_type(3)));
typedef _Bool B8 __attribute__((ext_vector_type(8)));
typedef _Bool B9 __attribute__((ext_vector_type(9)));
typedef _Bool B64 __attribute__((ext_vector_type(64)));
constexpr B8 constant = {1,0,1,0,0,1,0,1};
constexpr unsigned char representation = __builtin_bit_cast(unsigned char,constant);
static_assert(representation == 0xa5);
constexpr B8 restored = __builtin_bit_cast(B8,representation);
static_assert(restored[0] && !restored[1] && restored[7]);
static int calls;
__attribute__((noinline)) static B8 once(unsigned char x) {
  calls++; return __builtin_bit_cast(B8,x);
}
__attribute__((noinline)) static unsigned char byte(B8 x) {
  return __builtin_bit_cast(unsigned char,x);
}
int main(void) {
  CHECK(byte(once(0xa5))==0xa5 && calls==1);
  unsigned char raw3=5;
  B3 three=__builtin_bit_cast(B3,raw3);
  CHECK(three[0] && !three[1] && three[2]);
  CHECK((__builtin_bit_cast(unsigned char,three)&7)==5);
  unsigned short raw9=0x165;
  B9 nine=__builtin_bit_cast(B9,raw9);
  CHECK((__builtin_bit_cast(unsigned short,nine)&0x1ff)==raw9);
  for(int i=0;i<9;i++) CHECK(nine[i]==((raw9>>i)&1));
  unsigned long long raw64=0xa55aa55a01234567ull;
  B64 wide=__builtin_bit_cast(B64,raw64);
  CHECK(__builtin_bit_cast(unsigned long long,wide)==raw64);
  for(int i=0;i<64;i++) CHECK(wide[i]==((raw64>>i)&1));
  return 0;
}
