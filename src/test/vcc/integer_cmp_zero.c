/* Zero comparisons, including side effects and signed non-equivalences. */
static volatile unsigned observed;
static unsigned calls;
static unsigned next(unsigned x) { calls++; return x; }
int cz_u32(unsigned x) { return x <= 0; }
int cz_reverse(unsigned x) { return 0U >= x; }
int cz_u64(unsigned long long x) { return x <= 0; }
int cz_signed(int x) { return x == 0; }
int cz_effect(unsigned x) { return next(x) <= 0; }
int cz_volatile(void) { return observed <= 0; }
int cz_signed_le(int x) { return x <= 0; }
int cz_dynamic(unsigned x, unsigned y) { return x <= y; }
int main(void)
{
 if (!cz_u32(0) || cz_u32(1) || !cz_reverse(0) || cz_reverse(~0U) ||
     !cz_u64(0) || cz_u64(1ULL << 63) || !cz_signed(0) || cz_signed(-1) ||
     !cz_signed_le(-1) || !cz_dynamic(1, 2) || cz_dynamic(2, 1)) return 1;
 calls = 0;
 if (!cz_effect(0) || calls != 1 || cz_effect(1) || calls != 2) return 2;
 observed = 0;
 if (!cz_volatile()) return 3;
 observed = 1;
 if (cz_volatile()) return 4;
 return 0;
}
