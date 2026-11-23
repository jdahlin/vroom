/* the carry-out of __builtin_addc/subc is a parameter of type T *: clang's conversions and diagnostics (it was any pointer, stored through at the pointee's width) */
int f(unsigned a, unsigned b) {
  unsigned long wide; const unsigned ro = 0; unsigned char small; unsigned ok; int sgn;
  unsigned r1 = __builtin_addc(a, b, 0, &wide);
  unsigned r2 = __builtin_addc(a, b, 0, &ro);
  unsigned long long r3 = __builtin_addcll(a, b, 0, &small);
  unsigned r4 = __builtin_subc(a, b, 0, &ok) + __builtin_subc(a, b, 0, &sgn);
  unsigned r5 = __builtin_addc(a, b, 0, 5);
  unsigned r6 = __builtin_addc(a, b, 0, 1.5);
  return r1 + r2 + (int)r3 + r4 + r5 + r6;
}
