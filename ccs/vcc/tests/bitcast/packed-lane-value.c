/* Vcc direct-bit-field policy also applies to packed boolean vector lanes.
 * Clang23 copies containing storage for this operand, so it is not the oracle. */
typedef _Bool B8 __attribute__((ext_vector_type(8)));
__attribute__((noinline)) unsigned char lane(B8 *p, int i) {
  return __builtin_bit_cast(unsigned char,(*p)[i]);
}
int main(void) {
  B8 bits={1,0,1,0,0,1,0,1};
  for(int i=0;i<8;i++) if(lane(&bits,i)!=((0xa5>>i)&1)) return 1;
  int i=2;
  if(__builtin_bit_cast(unsigned char,bits[i++])!=1 || i!=3) return 2;
  return 0;
}
