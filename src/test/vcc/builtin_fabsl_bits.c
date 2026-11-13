/* Explicit fabsl is an operation on the sign bit, including signaling NaNs. */
#include <stdint.h>
#include <string.h>
static volatile int calls;
static long double once(void) { calls++; return -3.5L; }
int main(void)
{
  if (__builtin_fabsl(once()) != 3.5L || calls != 1) return 1;
  const unsigned long long fractions[] = {
#if __LDBL_MANT_DIG__ == 64
    0, 0x8000000000000000ULL, 0xc000000000001234ULL, 0x8000000000001234ULL,
#else
    0, 0, 0x1234, 0x1234,
#endif
    1
  };
  for (unsigned negative = 0; negative < 2; negative++)
  for (unsigned i = 0; i < 5; i++) {
    unsigned long long bits[2] = {fractions[i], 0}, got[2] = {0, 0};
#if __LDBL_MANT_DIG__ == 64
    bits[1] = (i >= 1 && i <= 3 ? 0x7fff : 0) | ((unsigned long long)negative << 15);
#elif __LDBL_MANT_DIG__ == 113
    bits[1] = (i >= 1 && i <= 3 ? 0x7fff000000000000ULL : 0) | ((unsigned long long)negative << 63);
    if (i == 2) bits[1] |= 0x0000800000000000ULL;
#else
#error unsupported long double representation
#endif
    long double value, result;
    memcpy(&value, bits, sizeof value);
    result = __builtin_fabsl(value);
    memcpy(got, &result, sizeof result);
#if __LDBL_MANT_DIG__ == 64
    if (got[0] != bits[0] || (got[1] & 0xffff) != (bits[1] & 0x7fff)) return 2 + i;
#else
    if (got[0] != bits[0] || got[1] != (bits[1] & 0x7fffffffffffffffULL)) return 2 + i;
#endif
  }
  return 0;
}
