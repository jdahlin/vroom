/* Vcc extension contract: bit-fields are extracted as declared-type values.
 * Clang instead copies containing storage for this operand: not its oracle. */
struct S { unsigned a:3,b:5; };
struct __attribute__((packed)) Tiny { unsigned a:3,b:5; };
__attribute__((noinline)) unsigned bits(struct S *s) { return __builtin_bit_cast(unsigned,s->b); }
__attribute__((noinline)) unsigned tiny_bits(struct Tiny *s) { return __builtin_bit_cast(unsigned,s->b); }
struct Wide { unsigned prefix:3; unsigned __int128 value:100; };
struct __attribute__((packed)) PackedWide { unsigned prefix:3; unsigned __int128 value:100; };
struct SignedWide { unsigned prefix:3; __int128 value:100; };
__attribute__((noinline)) unsigned __int128 wide_bits(struct Wide *s) {
  return __builtin_bit_cast(unsigned __int128,s->value);
}
__attribute__((noinline)) unsigned __int128 signed_wide_bits(struct SignedWide *s) {
  return __builtin_bit_cast(unsigned __int128,s->value);
}
__attribute__((noinline)) unsigned __int128 packed_wide_bits(struct PackedWide *s) {
  return __builtin_bit_cast(unsigned __int128,s->value);
}
int main(void) {
  struct S s={5,11}; struct Tiny t={5,11};
  unsigned __int128 expected=((unsigned __int128)1<<90)+123;
  union { struct Wide value; unsigned __int128 raw; } w={.raw=(expected<<3)|7};
  union { struct SignedWide value; unsigned __int128 raw; } negative={.raw=(((unsigned __int128)(-(__int128)expected)&(((unsigned __int128)1<<100)-1))<<3)|7};
  union { struct PackedWide value; unsigned __int128 raw; } packed={.raw=(expected<<3)|7};
  return packed_wide_bits(&packed.value)!=expected || bits(&s)!=11 || tiny_bits(&t)!=11 || wide_bits(&w.value)!=expected ||
      signed_wide_bits(&negative.value)!=(unsigned __int128)(-(__int128)expected);
}
